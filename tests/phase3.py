#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect real ring-3 execution, traps, address spaces and destruction in QEMU."""
import argparse
import subprocess
import sys
from phase1 import ROOT, BUILD, emulator, require, wait_for
from phase2 import free_port, connect_and_run

DONE = 'Phase 3: user domains completed'


def user_symbols():
    text = subprocess.check_output(['nm', '-n', str(BUILD / 'user_a.elf')], text=True)
    return {parts[2]: int(parts[0], 16) for line in text.splitlines()
            if len(parts := line.split()) == 3}


# GDB helpers read the actual page structures, never invoke kernel functions.
HELPERS = '''python
import gdb, struct
def v(expression):
    return int(gdb.parse_and_eval(expression)) & 0xffffffff
def check(condition, message):
    if not condition:
        raise gdb.GdbError(message)
def words(address, count):
    return struct.unpack('<%dI' % count, gdb.selected_inferior().read_memory(address, count*4))
def d(index, field):
    return v('domains[%d].%s' % (index, field))
def inspect_spaces():
    global page_lists
    page_lists = [[d(i, 'space.pages[%d]' % j) for j in range(6)] for i in range(2)]
    check(len(set(page_lists[0] + page_lists[1])) == 12, 'domains share backing pages')
    for i, pages in enumerate(page_lists):
        check(d(i,'space.live') == 1 and d(i,'space.count') == 6, 'incomplete space')
        directory = words(pages[0],1024)
        root = words(v('&page_directory'),1024)
        check(all(directory[j] == root[j] and directory[j] & 4 == 0 for j in range(16)), 'kernel mappings changed')
        probe_index = 33 if i == 0 else 34
        for j in range(16,1024):
            expected = pages[1] | 7 if j == 32 else pages[2] | 7 if j == probe_index else 0
            check(directory[j] == expected, 'unexpected user PDE')
        table = words(pages[1],1024)
        probe = words(pages[2],1024)
        check(table[:3] == (pages[3]|5, pages[4]|7, pages[5]|7), 'user permissions or private pages wrong')
        check(not any(table[3:]), 'unintended user mapping')
        check(probe[0] == pages[4]|7 and not any(probe[1:]), 'private alias wrong')
        for address in pages:
            check(address % 4096 == 0 and v('states[%d]' % (address>>12)) == 4, 'unsafe backing page')
        data = words(pages[4],1024)
        check(data[0] == (0xaaaaaaaa if i == 0 else 0xbbbbbbbb), 'private data marker')
        check(data[8:] == (0,)*1016, 'reused private data not cleared')
        check(not any(words(pages[5],1024)), 'user stack not cleared')
    check(v('tss.ss0') == 16 and v('tss.io_map_base') == 104, 'TSS I/O permissions')
    gates = gdb.parse_and_eval('idt')
    for i in range(256):
        attr = int(gates[i]['attributes'])
        check((attr & 0x60) == (0x60 if i == 128 else 0), 'unexpected DPL3 gate')
    print('PASS: two private CR3 trees, supervisor kernel, bounded user pages and cleared data/stacks')
def inspect_reaped():
    check(v('$cr3') == v('&page_directory'), 'freeing under user CR3')
    check(v('current_domain') == 0, 'stale current domain')
    for i, pages in enumerate(page_lists):
        check(d(i,'space.live') == 0 and d(i,'space.count') == 0, 'live space after teardown')
        for address in pages:
            check(v('states[%d]' % (address>>12)) == 2, 'page leaked or freed twice')
            check(not any(words(address,1024)), 'released page not zero')
    state_bytes = bytes(gdb.selected_inferior().read_memory(v('&states'),16384))
    check(state_bytes.count(2) == v('free_before_domains'), 'free count did not recover')
    print('PASS: kernel CR3 restored; all 12 pages unmapped, zeroed and freed')
def inspect_user(index):
    check(v('$cr0') & 8 == 8, 'unsaved FPU state exposed')
    check(v('$cs') == 0x1b and v('$ss') == 0x23, 'not real CPL 3')
    check(all(v('$'+r) == 0x23 for r in ('ds','es','fs','gs')), 'user segments')
    check(v('$cr3') == d(index,'space.pages[0]'), 'wrong per-domain CR3')
    check(0x08002000 <= v('$esp') < 0x08003000, 'not on user stack')
    check(v('$eflags') & 0x23200 == 0, 'IOPL/IF/VM unsafe')
    check(v('tss.esp0') == v('(unsigned int)&domains[%d].kernel_stack' % index) + 8192, 'wrong TSS ESP0')
    print('USER%d: CS=%#x SS=%#x EIP=%#x ESP=%#x CR0=%#x CR3=%#x ESP0=%#x' % (index+1,v('$cs'),v('$ss'),v('$eip'),v('$esp'),v('$cr0'),v('$cr3'),v('tss.esp0')))
    print('PASS: actual user CPL=3, segments, CR3 and TSS ESP0 for domain', index+1)
def inspect_trap(index):
    check(v('$cs') == 8 and v('$ss') == 16, 'trap is not ring 0')
    check(all(v('$'+r) == 16 for r in ('ds','es','fs','gs')), 'kernel segments not restored')
    frame = words(v('$esp')+4,1)[0]
    bottom = v('(unsigned int)&domains[%d].kernel_stack' % index)
    check(bottom <= v('$esp') < bottom+8192, 'C handler using user stack')
    check(bottom <= frame <= bottom+8192-60, 'frame outside TSS stack')
    cpu = words(frame,15)
    check(cpu[11] == 0x1b and cpu[14] == 0x23, 'outer-level CS/SS missing')
    check(v('$cr3') == d(index,'space.pages[0]'), 'trap address space wrong')
    check(v('tss.esp0') == bottom+8192, 'TSS stack selection mismatch')
    print('TRAP%d: CPL=%d ESP=%#x FRAME=%#x USER_ESP=%#x USER_SS=%#x' % (index+1,v('$cs') & 3,v('$esp'),frame,cpu[13],cpu[14]))
    print('PASS: hardware outer ESP/SS and trusted per-domain trap stack', index+1)
    return cpu
end
'''


def finish_guest(process, log):
    wait_for(process, log, lambda text: DONE in text, 'Phase 3 completion')
    text = log.read_text()
    require('PANIC' not in text, f'kernel panic; see {log}')
    require(text.count('Domains: round completed, all pages reclaimed') == 2,
            'did not complete both domain lifetimes')
    return text


def normal(qemu, gdb, names, repetition, allocation_failure=False):
    label = f'phase3-normal-{repetition}'
    port = free_port()
    with emulator('debug', qemu, port, label=label,
                  append='domain-test=allocation-failure' if allocation_failure else None) as (process, log):
        commands = HELPERS + f'''hbreak *domains_prepared
continue
python inspect_spaces()
monitor info registers
delete breakpoints
hbreak *{names['user_checkpoint']:#x}
continue
python inspect_user(0)
delete breakpoints
hbreak *syscall_dispatch
continue
python inspect_trap(0)
delete breakpoints
hbreak *{names['user_checkpoint']:#x}
continue
python inspect_user(1)
delete breakpoints
hbreak *syscall_dispatch
continue
python inspect_trap(1)
delete breakpoints
hbreak *domains_reaped
continue
python
inspect_reaped()
for i in range(2):
    check(d(i,'state') == 3 and d(i,'exit_status') == 0, 'normal user program failed')
    check(d(i,'yields') == 1 and d(i,'syscalls') == 7, 'syscalls/unknown number/register tests incomplete')
print('PASS: normal/unknown/invalid scalar syscalls and register/NT/DF preservation')
end
delete breakpoints
hbreak *domains_prepared
continue
python inspect_spaces()
delete breakpoints
hbreak *domains_reaped
continue
python
inspect_reaped()
check(v('completed_rounds') == 2, 'second lifecycle incomplete')
for i in range(2):
    check(d(i,'state') == 3 and d(i,'exit_status') == 0, 'recreated domain failed')
end
delete breakpoints
detach
quit
'''
        output = connect_and_run(process, log, port, gdb, commands, label)
        text = finish_guest(process, log)
        if allocation_failure:
            require('SELFTEST: partial address-space allocation rolled back' in text, 'rollback test did not execute')
        require(text.count('ABab') == 2 and 'USER FAULT' not in text, 'normal execution order wrong')
        require('TR =0028' in output or 'TR =0028' in output.replace('  ',' '), 'LTR evidence missing')
    print(f'PASS: normal boot {repetition}, real CPL3/TSS/CR3/syscalls/registers/repeated cleanup')


def faults(qemu, gdb, names):
    cases = [
        ('kernel-read',14,5,'kernel_read','0x100000'),
        ('kernel-write',14,7,'kernel_write','0x100000'),
        ('cross-read',14,4,'cross_read','0x08800000'),
        ('cross-write',14,6,'cross_write','0x08800000'),
        ('cli',13,0,'cli','0'), ('io',13,0,'io','0'), ('gate',13,0x1a,'gate','0'),
        ('table-write',14,7,'table_write',"d(0,'space.pages[0]')"),
        ('physical-read',14,5,'physical_read',"d(1,'space.pages[4]')"),
        ('code-write',14,7,'code_write','0x08000000'),
        ('bad-stack',6,0,'bad_stack','0'),
        ('cross-read',14,4,'cross_read','0x08400000'),
        ('cross-write',14,6,'cross_write','0x08400000'),
        ('fpu',7,0,'fpu','0'),
    ]
    for case, (mode, vector, error, symbol, address) in enumerate(cases):
        reverse = case in (11,12)
        bad, good = (1,0) if reverse else (0,1)
        label = 'phase3-' + mode + ('-reverse' if reverse else '')
        port = free_port()
        append = 'domain-test=' + mode + (' domain-reverse' if reverse else '')
        with emulator('debug', qemu, port, append=append, label=label) as (process, log):
            commands = HELPERS + f'''hbreak *domains_prepared
continue
python
inspect_spaces()
expected_address = {address}
end
delete breakpoints
hbreak *domain_fault
continue
python
cpu = inspect_trap({bad})
check(cpu[8] == {vector} and cpu[9] == {error}, 'wrong hardware vector/error')
check(cpu[10] == {names['user_fault_'+symbol]}, 'wrong faulting instruction EIP')
check(v('$cr2') == expected_address if {vector} == 14 else True, 'hardware CR2 mismatch')
check(cpu[13] == 0 if '{mode}' == 'bad-stack' else 0x08002000 <= cpu[13] < 0x08003000, 'outer ESP corrupted')
print('PASS: user hardware fault vector/error/EIP/CR2 checked:',{vector},{error},hex(cpu[10]),hex(expected_address))
end
delete breakpoints
hbreak *domains_reaped
continue
python
inspect_reaped()
check(d({bad},'state') == 4 and d({bad},'fault_vector') == {vector}, 'fault domain survived')
check(d({bad},'fault_error') == {error} and d({bad},'fault_address') == expected_address, 'recorded fault mismatch')
check(d({good},'state') == 3 and d({good},'exit_status') == 0, 'other domain did not finish safely')
end
continue
python
inspect_reaped()
check(v('completed_rounds') == 2, 'second fault lifecycle incomplete')
check(d({bad},'state') == 4 and d({good},'state') == 3 and d({good},'exit_status') == 0, 'second isolation run failed')
end
delete breakpoints
detach
quit
'''
            connect_and_run(process, log, port, gdb, commands, label)
            text = finish_guest(process, log)
            require(text.count('USER FAULT domain=') == 2, f'{label}: fault count wrong')
            require(('\na\nDomains:' if reverse else 'Bb') in text, f'{label}: survivor output missing')
        print(f'PASS: {label}, hardware denial, surviving peer and two clean lifecycles')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-i386')
    parser.add_argument('--gdb', default='gdb')
    args = parser.parse_args()
    names = user_symbols()
    normal(args.qemu, args.gdb, names, 1)
    normal(args.qemu, args.gdb, names, 2)
    normal(args.qemu, args.gdb, names, 3, allocation_failure=True)
    faults(args.qemu, args.gdb, names)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}',file=sys.stderr)
        sys.exit(1)
