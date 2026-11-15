#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real IRQ, register resumption, PTE revocation and old-binary compatibility.

GDB reads hardware/state; the checkpoints contain no success decisions.
"""
import argparse
import hashlib
import json
import subprocess
import struct
from pathlib import Path
from phase1 import ROOT, BUILD, emulator, wait_for, require
from phase2 import connect_and_run, free_port
from phase3 import HELPERS
from phase5 import run_guest as external_case, headers

AUDIT = r'''python
import re
seen = dict(irq=0, kernel=0, kernel_restored=0, user=0, resumed=0, peer=0, request=0, complete=0, zero=0, reowned=0)
loop_pc=LOOP_PC
checkpoint_pc=CHECKPOINT_PC
mode=MODE
old=None
raw_irq=None
kernel_return_bp=None
kernel_snapshot=None
def kernel_return():
    expected,segments,sp,flags=kernel_snapshot
    actual=tuple(v('$'+r) for r in ('edi','esi','ebp','ebx','edx','ecx','eax'))
    check(actual==expected and v('$esp')==sp+12,'kernel IRQ corrupted GP registers/ESP')
    check(tuple(v('$'+r) for r in ('ds','es','fs','gs'))==segments,'kernel IRQ corrupted segments')
    check(v('$eflags')&0xed5==flags&0xcd5,'kernel IRQ flags not restored with IF cleared')
    check(v('$cr3')==v('&page_directory') and v('current_domain')==0,'kernel IRQ switched domain')
    seen['kernel_restored']+=1
    kernel_return_bp.enabled=False
    irq_bp.enabled=True
class Audit(gdb.Breakpoint):
    def __init__(self, name, action):
        super().__init__('*'+name, type=gdb.BP_HARDWARE_BREAKPOINT, internal=True)
        self.action=action
    def stop(self):
        try:
            self.action()
        except Exception as error:
            import traceback
            gdb.write('PHASE6 AUDIT FAILURE: '+traceback.format_exc()+'\n')
            gdb.execute('quit 1')
        return False
# Software breakpoints on shared physical code can corrupt cross-CR3 fixtures;
# never install more than four hardware breakpoints simultaneously.
def irq_entry():
    global raw_irq,kernel_return_bp,kernel_snapshot
    cpu=words(v('$esp'),3)
    check(v('$cs')==8 and v('$ss')==16 and v('$eflags')&0x200==0,'IRQ did not enter trusted IF=0 Ring0')
    raw_irq=(cpu,tuple(v('$'+r) for r in ('edi','esi','ebp','ebx','edx','ecx','eax')))
    seen['irq']+=1
    if cpu[1]&3==0:
        seen['kernel']+=1
        check(cpu[1]==8,'bad kernel IRQ CS')
        kernel_snapshot=(raw_irq[1],tuple(v('$'+r) for r in ('ds','es','fs','gs')),v('$esp'),cpu[2])
        irq_bp.enabled=False
        kernel_return_bp=Audit('phase6_kernel_irq_return',kernel_return)
    else:
        seen['user']+=1
        i=v('current_domain->id')-1
        top=v('&domains[%d].kernel_stack'%i)+8192
        check(v('$esp')==top-20 and v('tss.esp0')==top,'IRQ did not use TSS stack')
        check(v('$cr3')==d(i,'space.pages[0]') and cpu[1]==0x1b,'IRQ wrong CR3/CPL')
        check(words(v('$esp')+12,2)[1]==0x23,'IRQ outer SS missing')
        check(cpu[2]&0x200 and cpu[2]&0x3000==0,'saved user IF/IOPL unsafe')
def dispatch():
    frame=words(v('$esp')+4,1)[0]
    regs=words(frame,17)
    cpu=regs[4:]
    check(raw_irq is not None and tuple(cpu[10:13])==raw_irq[0],'CPU IRQ frame corrupted')
    check(tuple(cpu[j] for j in (0,1,2,4,5,6,7))==raw_irq[1],'GP regs overwritten before save')
    check(cpu[8]==32 and cpu[9]==0,'IRQ normalization broken')
    check(v('$ds')==16 and v('$es')==16 and v('$eflags')&0x400==0,'kernel DS/DF unsafe')
    if cpu[11]&3:
        i=v('current_domain->id')-1
        check(frame>=v('&domains[%d].kernel_stack'%i) and frame+76<=v('tss.esp0'),'IRQ frame outside trusted stack')
        if mode=='spin' and cpu[10]==loop_pc:
            check(tuple(cpu[j] for j in (7,4,6,5,1,0,2))==tuple(i*0x11111111 for i in range(1,8)),'spin GP sentinels corrupted')
            check(regs[:4]==(0x18,0x21,0x20,3),'segment sentinels lost')
            check(cpu[12]&0x401==0x401,'user CF/DF lost')
def entered_peer():
    check(v('$cs')==0x1b and v('$ss')==0x23,'peer not CPL3')
    check(v('$cr3')==d(1,'space.pages[0]'),'peer CR3 wrong')
    seen['peer']+=1
irq_bp=Audit('timer_irq_entry',irq_entry)
dispatch_bp=Audit('timer_dispatch',dispatch)
end
'''
# Keep IRQ breakpoints for the first two A interrupts, then use those debug
# registers to inspect resumption and arbitration. A breakpoint on every
# instruction of an infinite loop would itself prevent timer progress.
SPIN = r'''python
saved=None
resume_bp=None
def resumed():
    global saved
    check(saved is not None,'resumed without saved context')
    actual=tuple(v('$'+r) for r in ('edi','esi','ebp','ebx','edx','ecx','eax','eip','cs','eflags','esp','ss','gs','fs','es','ds'))
    check(actual==saved,'asynchronous iret did not restore complete context: actual=%r saved=%r'%(actual,saved))
    check(v('$cr3')==d(0,'space.pages[0]'),'resumed under wrong CR3')
    seen['resumed']+=1
    resume_bp.enabled=False
def saved_preempt():
    global saved,resume_bp
    if v('current_domain->id')!=1: return
    pic=gdb.execute('monitor info pic',to_string=True)
    check(re.search(r'pic0:.*imr=fe.*isr=00.*irq_base=20',pic) is not None,'PIC master mask/vector/EOI incorrect: '+pic)
    check(re.search(r'pic1:.*imr=ff.*isr=00.*irq_base=28',pic) is not None,'PIC slave unexpectedly open: '+pic)
    check(v('eoi_count')==v('irq_count'),'EOI missing or duplicate')
    frame=words(v('$esp')+4,1)[0]
    cpu=words(frame,15)
    if cpu[10]!=loop_pc: return
    # Inspect the return path on the SECOND or later preemption; A resumes
    # after B has run. GP/segments/flags are preserved byte-for-byte.
    # RF is debug/restart state, deliberately stripped by the return sanitizer.
    expected_cpu=list(cpu)
    expected_cpu[12]=(cpu[12]&0xcd5)|0x202
    # Returning across privilege levels revalidates data segments. QEMU's
    # IRET canonicalizes the unusable null DS=3 to selector 0. Both are null;
    # the non-null selectors, including their RPL, must match exactly.
    saved=tuple(expected_cpu[j] for j in (0,1,2,4,5,6,7,10,11,12,13,14))+(0x18,0x21,0x20,0)
    irq_bp.enabled=False
    dispatch_bp.enabled=False
    preempt_bp.enabled=False
    if resume_bp is None:
        resume_bp=Audit(hex(loop_pc),resumed)
        Audit(hex(checkpoint_pc),lambda: entered_peer() if v('current_domain->id')==2 else None)
preempt_bp=Audit('domain_preempt',saved_preempt)
end
'''
REVOKE = r'''python
# The request checkpoint follows a real user allocation and two real maps.
def requested():
    global old,old_deadline
    i=v('test_request')
    a=words(d(0,'space.pages[4]'),16)
    slot=a[3]-1
    old=(slot,a[4],a[5]*4096,d(0,'epoch'),d(0,'space.dynamic_table'),i)
    check(v('resources[%d].state'%slot)==3,'not REVOKE_PENDING')
    check(v('resources[%d].references'%slot)==2,'aliases not actually installed')
    check(words(old[4],2)==(old[2]|0x67,old[2]|0x67) or all((x&~0x60)==(old[2]|7) for x in words(old[4],2)),'alias PTEs wrong')
    live=[j for j in range(4) if v('events[%d].active'%j)]
    check(len(live)==1,'event lost/duplicated')
    j=live[0]
    old_deadline=v('events[%d].deadline'%j)
    check(v('events[%d].request_id'%j)==i and v('events[%d].owner_epoch'%j)==old[3],'event identity wrong')
    check(v('events[%d].handle.generation'%j)==old[1],'event generation truncated')
    seen['request']+=1
    # Enough raw IRQs observed; free debug registers for protection evidence.
    irq_bp.enabled=False
    dispatch_bp.enabled=False
    request_bp.enabled=False
    if mode in ('force','ack'):
        complete_bp.enabled=True
    claim_bp.enabled=True
    peer_bp.enabled=True
def completed():
    check(old is not None,'completion without request')
    slot,gen,pa,epoch,table,req=old
    check(v('resources[%d].state'%slot)==0 and v('resources[%d].references'%slot)==0,'resource not reclaimed')
    check(words(d(0,'space.pages[0]')+36*4,1)[0]==0 and d(0,'space.dynamic_table')==0,'old PDE still present')
    check(not any(words(table,1024)),'old aliases/table not cleared')
    check(not any(words(pa,1024)) and v('states[%d]'%(pa>>12))==2,'reclaim not zero/free')
    check(not any(v('events[%d].active'%j) for j in range(4)),'notification survived completion')
    if mode=='force':
        check(v('ticks')>=old_deadline,'forced before deadline')
        check(v('$cr3')==v('&page_directory'),'forced reclaim on active user CR3')
        check(d(0,'yields')==0 and d(0,'preemptions')>=1,'noncooperative owner not preempted')
    seen['complete']+=1
def claim_entry():
    if old is None: return
    args=words(v('$esp')+4,5)
    if args[0]!=v('&domains[1]') or not args[2] or args[3]!=old[2]>>12: return
    if v('states[%d]'%(old[2]>>12))!=2: return
    check(not any(words(old[2],1024)),'new owner could read old bytes')
    check(words(d(0,'space.pages[0]')+36*4,1)[0]==0,'new owner granted before old mappings removed')
    check(v('resources[%d].state'%old[0])==0,'two owners overlap')
    seen['zero']+=1
    claim_bp.enabled=False
def peer_checkpoint():
    if v('current_domain->id')!=2: return
    entered_peer()
    if words(v('$esp')+4,1)[0]!=3: return
    slot,gen,pa,epoch,table,req=old
    check(v('resources[%d].cap.owner_id'%slot)==2,'resource not reowned by B')
    check(v('resources[%d].cap.generation'%slot)>gen,'stale generation reused')
    check(v('resources[%d].physical_address'%slot)==pa,'different PFN hid reuse')
    check(words(pa,1024)==tuple(0xb0000000|i for i in range(1024)),'new-owner writes incomplete')
    check(d(1,'space.dynamic_table')!=0 and words(d(1,'space.dynamic_table'),1)[0]&~0x60==pa|7,'new owner PTE missing')
    seen['reowned']+=1
request_bp=Audit('phase6_requested',requested)
complete_bp=Audit('revoke_completed',completed)
complete_bp.enabled=False
claim_bp=Audit('resource_claim',claim_entry)
claim_bp.enabled=False
peer_bp=Audit(hex(checkpoint_pc),peer_checkpoint)
peer_bp.enabled=False
end
'''


def symbols(image):
    out = subprocess.check_output(['nm', '-n', str(image)], text=True)
    return {s[2]: int(s[0], 16) for line in out.splitlines() if len(s := line.split()) == 3}


def experiment(qemu, gdb, mode):
    names = symbols(BUILD / 'phase6-user.elf')
    # Dynamically disabled breakpoints do not consume a hardware debug register.
    commands = HELPERS + AUDIT.replace('LOOP_PC', str(names['phase6_spin_loop'])).replace('CHECKPOINT_PC', str(names['phase6_user_checkpoint'])).replace('MODE', repr(mode))
    if mode == 'spin':
        commands += SPIN
    else:
        commands += REVOKE
    commands += 'hbreak *phase6_reaped\n'
    for iteration in range(2):
        commands += 'continue\npython\n'
        commands += "check(v('$pc')==v('&phase6_reaped'),'wrong final checkpoint')\n"
        commands += "check(v('phase6_rounds')==%d,'duplicate reaper stop: actual round=%%d'%%v('phase6_rounds'))\n" % (iteration+1)
        commands += "check(v('$cr3')==v('&page_directory') and v('$eflags')&0x200==0,'unsafe scheduler state')\n"
        commands += "check(bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)==v('free_before_phase6'),'lifetime page leak')\n"
        commands += "check(seen['kernel']>=1 and seen['kernel_restored']>=1,'kernel-origin IRQ not restored')\n"
        if mode in ('spin', 'force'):
            commands += "check(seen['user']>=1,'hardware user IRQ not exercised')\n"
        commands += "check(v('eoi_count')==v('irq_count') and v('kernel_irqs')>=1,'EOI or kernel IRQ return broken')\n"
        commands += "check(d(1,'state')==3 and d(1,'exit_status')==0,'B did not exit normally')\n"
        commands += "check(d(0,'resource_count')==0 and d(1,'resource_count')==0,'resource leak')\n"
        commands += "check(not any(v('events[%d].active'%j) for j in range(4)),'event leak')\n"
        commands += "for i in range(2):\n    check(d(i,'space.live')==0 and d(i,'space.count')==0,'space survived teardown')\n    for j in range(6):\n        pa=d(i,'space.pages[%d]'%j)\n        check(v('states[%d]'%(pa>>12))==2 and not any(words(pa,1024)),'base pages not zero/free')\n"
        if mode == 'spin':
            commands += "check(d(0,'state')==3 and d(0,'exit_status')==(-16&0xffffffff),'diagnostic CPU budget not applied')\ncheck(d(0,'yields')==0 and d(0,'preemptions')==20,'A infinite loop not hardware-preempted')\ncheck(seen['resumed']>=1 and seen['peer']>=1 and d(1,'syscalls')>=2,'resume/peer syscall proof missing')\n"
        else:
            commands += "check(seen['request']==%d and seen['zero']==%d and seen['reowned']==%d,'protocol protection evidence missing: %%r rounds=%%d epochs=%%d/%%d cancelled=%%d Astatus=%%d'%%(seen,v('phase6_rounds'),d(0,'epoch'),d(1,'epoch'),v('cancelled'),d(0,'exit_status')))\n" % ((iteration+1,)*3)
            if mode == 'force':
                commands += "check(seen['complete']==%d and v('forced')==%d,'forced completion missing')\n" % ((iteration+1,)*2)
                commands += "check(d(0,'state')==4 and d(0,'fault_vector')==14 and d(0,'fault_error')==6 and d(0,'fault_address') in (0x9000000,0x9001000),'old owner did not hardware-fault on stale write')\ncheck(d(0,'yields')==0,'malicious loop secretly yielded')\n"
                commands += "check(d(0,'fault_eip')==%d+(0 if d(0,'fault_address')==0x9000000 else 10),'fault did not execute stale write instruction')\n" % names['phase6_stale_write']
            else:
                commands += "check(d(0,'state')==3 and d(0,'exit_status')==0,'cooperating owner failed')\n"
                if mode == 'ack':
                    commands += "check(v('acknowledged')==%d and v('forced')==0,'ACK confused with timeout')\n" % (iteration+1)
                else:
                    commands += "check(v('cancelled')==%d and v('forced')==0,'release/exit event not cancelled')\n" % (iteration+1)
        commands += "print('PHASE6 HARDWARE:',mode,'round',%d,seen,'ticks',v('ticks'))\nend\n" % (iteration+1)
        # GDB/QEMU may report the same hardware breakpoint again when DR
        # slots are reconfigured at its PC. Execute this empty checkpoint's
        # RET before rearming; verify the actual round counter above.
        if iteration == 0:
            commands += "stepi\npython check(v('$pc')!=v('&phase6_reaped'),'did not leave reaper')\n"
        # Disable the completed-round breakpoints and arm new request inspection.
        if mode != 'spin':
            commands += 'python\nclaim_bp.enabled=False\npeer_bp.enabled=False\ncomplete_bp.enabled=False\nrequest_bp.enabled=True\nend\n'
    commands += 'delete breakpoints\ndetach\nquit\n'
    label = 'phase6-' + mode
    port = free_port()
    with emulator('debug', qemu, port, append='phase6=' + mode, label=label) as (process, log):
        connect_and_run(process, log, port, gdb, commands, label)
        wait_for(process, log, lambda text: 'Phase 6: experiments completed' in text, 'Phase 6 completion')
        require('PANIC' not in log.read_text(), 'kernel panic: ' + str(log))
    print('PASS:', label, 'two real hardware lifecycles')


def external_preempt(qemu, gdb):
    image = BUILD / 'external/preempt.elf'
    names = symbols(image)
    # The same note is rejected in the default profile (no false feature claim).
    external_case(qemu, gdb, 'phase6-profile-rejection', image, -93, image)
    commands = HELPERS + AUDIT.replace('LOOP_PC', '0').replace('CHECKPOINT_PC', '0').replace('MODE', repr('external'))
    commands += 'add-symbol-file "' + str(image) + '"\n'
    commands += r'''python
external_stages=[]
def external_checkpoint():
    check(v('$cs')==0x1b and v('$cr3')==d(0,'space.pages[0]'),'external not CPL3/private CR3')
    check(v('$eflags')&0x200 and v('$eflags')&0x3000==0,'external IF/IOPL wrong')
    stage=words(v('$esp')+4,1)[0]
    external_stages.append(stage)
    check(v('external_version')==0x10002 and v('external_features')==119,'external ABI profile wrong')
    if stage==2:
        check(v('external_counter')==20000000 and d(0,'preemptions')>=2,'external CPU work not preempted')
        check(d(1,'state')==3 and d(1,'exit_status')==0,'peer starved by external CPU loop')
        check(d(0,'yields')==0,'external secretly yielded')
        check(words(0x09000000,1)[0]==0x600dbeef,'resource corrupted by context switches')
external_bp=Audit(hex(EXTERNAL_CHECKPOINT),external_checkpoint)
end
'''.replace('EXTERNAL_CHECKPOINT', str(names['external_preempt_checkpoint']))
    commands += 'hbreak *external_reaped\n'
    for iteration in range(2):
        commands += 'continue\npython\n'
        commands += "check(v('$pc')==v('&external_reaped'),'wrong external end checkpoint')\n"
        commands += "check(v('external_completed_rounds')==%d,'duplicate external reaper stop')\n" % (iteration+1)
        commands += "check(external_stages==[1,2]*%d,'external program did not finish')\n" % (iteration+1)
        commands += "check(d(0,'state')==3 and d(0,'exit_status')==0 and d(0,'yields')==0,'external exit failed')\n"
        commands += "check(d(0,'resource_count')==0 and d(0,'space.dynamic_table')==0,'external leak')\n"
        commands += "check(v('irq_count')==v('eoi_count') and seen['user']>2,'external hardware IRQ missing')\n"
        commands += "check(bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)==v('external_free_before'),'external lifetime page leak')\n"
        commands += "print('PHASE6 EXTERNAL: preemptions',d(0,'preemptions'),'peer',d(1,'state'),'IRQs',v('irq_count'))\nend\n"
        if iteration == 0:
            commands += "stepi\npython check(v('$pc')!=v('&external_reaped'),'did not leave external reaper')\n"
    commands += 'delete breakpoints\ndetach\nquit\n'
    label = 'phase6-external-preempt'
    port = free_port()
    with emulator('debug', qemu, port, append='external preempt', initrd=image, label=label) as (process, log):
        connect_and_run(process, log, port, gdb, commands, label)
        wait_for(process, log, lambda text: 'Phase 5: external execution completed' in text, 'external preemption completion')
        require('PANIC' not in log.read_text(), 'external preemption panic')
    print('PASS:', label, 'two independently built ELF lifecycles')


def reject_cooperative_note(qemu, gdb, image):
    commands = HELPERS
    for _ in range(2):
        commands += "delete breakpoints\nhbreak *external_prepared\ncontinue\npython\n"
        commands += "check(v('external_load_result')==(-93&0xffffffff),'COOPERATIVE note silently admitted into preempt profile')\n"
        commands += "check(d(0,'space.live')==0 and d(0,'space.count')==0,'rejected profile created space')\n"
        commands += "check(v('external_free_before_load')==v('external_free_after_load'),'profile rejection allocated pages')\nend\n"
        commands += "delete breakpoints\nhbreak *external_reaped\ncontinue\npython\n"
        commands += "check(d(1,'state')==3 and d(1,'exit_status')==0,'rejection harmed peer')\n"
        commands += "check(bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)==v('external_free_before'),'profile rejection leaked pages')\nend\n"
    commands += "delete breakpoints\ndetach\nquit\n"
    label = 'phase6-cooperative-note-rejected'
    port = free_port()
    with emulator('debug', qemu, port, append='external preempt', initrd=image, label=label) as (process, log):
        connect_and_run(process, log, port, gdb, commands, label)
        wait_for(process, log, lambda text: 'Phase 5: external execution completed' in text, 'profile rejection')
        require('PANIC' not in log.read_text(), 'profile rejection panic')
    print('PASS:', label, 'no COOPERATIVE promise under preemption')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-i386')
    parser.add_argument('--gdb', default='gdb')
    args = parser.parse_args()
    subprocess.run(['make', '--no-print-directory', '-j2', 'all', 'test-phase6-native', 'external-fixtures'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    for mode in ('spin', 'ack', 'force', 'release', 'exit-pending'):
        experiment(args.qemu, args.gdb, mode)
    # Never rebuild the compatibility binary with the new SDK.
    fixture = ROOT / 'tests/fixtures/compat/abi11.elf'
    manifest = json.loads(fixture.with_name('manifest.json').read_text())
    require(hashlib.sha256(fixture.read_bytes()).hexdigest() == manifest['sha256'], 'old ABI fixture changed')
    external_case(args.qemu, args.gdb, 'phase6-immutable-abi11', fixture, 0, fixture)
    # Only patch the requirement note in a build/ copy; instruction bytes and
    # the archived ABI 1.1 SDK executable remain unchanged.
    coop = bytearray(fixture.read_bytes())
    note = next(h for h in headers(coop) if h[0] == 4)
    struct.pack_into('<I', coop, note[1] + 32, 31)
    coop_file = BUILD / 'abi11-cooperative.elf'
    coop_file.write_bytes(coop)
    external_case(args.qemu, args.gdb, 'phase6-cooperative-note', coop_file, 0, fixture)
    reject_cooperative_note(args.qemu, args.gdb, coop_file)
    external_preempt(args.qemu, args.gdb)
    print('PASS: Phase 6 QEMU guests=10 / lifecycle rounds=20; native boundary suites=2')


if __name__ == '__main__':
    main()
