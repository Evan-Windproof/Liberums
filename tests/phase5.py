#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""External, independently compiled ELF32 integration and hostile-input tests."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from phase1 import ROOT, BUILD, emulator, require, wait_for
from phase2 import free_port, connect_and_run
from phase3 import HELPERS

BASE = 0x08000000
STACK = 0x083fc000
TOP = 0x08400000


def headers(data):
    offset = struct.unpack_from('<I', data, 28)[0]
    count = struct.unpack_from('<H', data, 44)[0]
    return [struct.unpack_from('<8I', data, offset+i*32) for i in range(count)]


def symbols(file):
    text = subprocess.check_output(['nm', '-n', str(file)], text=True)
    return {p[2]: int(p[0], 16) for line in text.splitlines() if len(p := line.split()) == 3}


def sdk_build():
    sdk = BUILD / 'sdk' / 'include'
    shutil.copytree(ROOT / 'include/abi', sdk / 'abi', dirs_exist_ok=True)
    out = BUILD / 'external-sdk'
    subprocess.run(['make', '-C', str(ROOT / 'tests/fixtures/external_user'),
                    'SDK='+str(sdk), 'OUT='+str(out)], check=True)
    return out


def corpus(reference):
    original = reference.read_bytes()
    phoff = struct.unpack_from('<I', original, 28)[0]
    note = headers(original)[3]
    cases = []
    def change(name, writes, expected=-8):
        data = bytearray(original)
        for offset, fmt, value in writes:
            struct.pack_into('<'+fmt, data, offset, value)
        path = BUILD / 'external-sdk' / (name+'.elf')
        path.write_bytes(data)
        cases.append((name, path, expected))
    change('bad-magic', [(0,'I',0)])
    change('elf64', [(4,'B',2)])
    change('big-endian', [(5,'B',2)])
    change('bad-ident-version', [(6,'B',2)])
    change('wrong-osabi', [(7,'B',3)])
    change('wrong-machine', [(18,'H',62)])
    change('dynamic-type', [(16,'H',3)])
    change('elf-flags', [(36,'I',1)])
    change('header-size', [(40,'H',64)])
    change('ph-overflow', [(28,'I',0xfffffff0)])
    change('ph-overlap-header', [(28,'I',4)])
    change('ph-count', [(44,'H',0xffff)])
    change('ph-entry-size', [(42,'H',31)])
    change('offset-overflow', [(phoff+4,'I',0xfffffff0)])
    change('filesz-overflow', [(phoff+16,'I',0xffffffff)])
    change('filesz-gt-memsz', [(phoff+20,'I',1)])
    change('zero-load', [(phoff+16,'I',0),(phoff+20,'I',0)])
    change('kernel-address', [(phoff+8,'I',0x100000)])
    change('resource-address', [(phoff+8,'I',0x09000000)])
    change('address-wrap', [(phoff+8,'I',0xfffff000),(phoff+20,'I',0x4000)])
    change('memory-overflow', [(phoff+20,'I',0xffffffff)])
    change('bad-alignment', [(phoff+28,'I',3)])
    change('huge-alignment', [(phoff+28,'I',0x200000)])
    change('noncongruent', [(phoff+4,'I',0x1001)])
    change('load-overlap', [(phoff+32+8,'I',BASE)])
    change('page-overlap', [(phoff+32+8,'I',BASE+0x2010),(phoff+32+4,'I',0x4010),(phoff+32+28,'I',1)])
    change('writable-code', [(phoff+24,'I',7)])
    change('unreadable', [(phoff+24,'I',1)])
    change('entry-null', [(24,'I',0)])
    change('entry-data', [(24,'I',headers(original)[2][2])])
    change('entry-bss', [(24,'I',BASE+headers(original)[0][4]),(phoff+20,'I',headers(original)[0][4]+64)])
    change('interp', [(phoff,'I',3)])
    change('dynamic-header', [(phoff,'I',2)])
    change('tls', [(phoff,'I',7)])
    change('missing-note', [(phoff+96,'I',0)])
    change('bad-note-size', [(phoff+96+16,'I',35)])
    change('bad-note-name', [(note[1]+12,'B',ord('X'))])
    change('abi-major', [(note[1]+24,'I',2)], -93)
    change('abi-minor', [(note[1]+28,'I',struct.unpack_from('<I', original, note[1]+28)[0]+1)], -93)
    change('abi-features', [(note[1]+32,'I',0x80000000)], -93)
    path = BUILD / 'external-sdk/truncated.elf'
    path.write_bytes(original[:51])
    cases.append(('truncated',path,-8))
    path = BUILD / 'external-sdk/oversized.elf'
    path.write_bytes(original + bytes(0x40001-len(original)))
    cases.append(('oversized',path,-8))
    # Nine individually valid, nonoverlapping LOADs exceed the segment budget.
    data = bytearray(original)
    struct.pack_into('<H',data,44,10)
    for i in range(9):
        struct.pack_into('<8I',data,phoff+i*32,1,0x1000,BASE+i*4096,0,4,4096,5 if i==0 else 4,4096)
    struct.pack_into('<8I',data,phoff+9*32,*note)
    path=BUILD/'external-sdk/segment-budget.elf'
    path.write_bytes(data)
    cases.append(('segment-budget',path,-8))
    return cases


GDB_HELPERS = '''python
import gdb, struct
external_pages=[]
peer_pages=[]
def elf_pt(va):
    return words(d(0,'space.pages[1]'),1024)[(va>>12)&1023]
def inspect_external(segments,blob):
    global external_pages,peer_pages
    check(v('external_load_result') == 0,'external loader rejected valid image')
    count=d(0,'space.count')
    external_pages=[d(0,'space.pages[%d]'%i) for i in range(count)]
    peer_pages=[d(1,'space.pages[%d]'%i) for i in range(6)]
    check(len(set(external_pages+peer_pages)) == count+6,'backing page alias')
    expected={}
    for kind,off,va,pa,filesz,memsz,flags,align in segments:
        if kind != 1:
            continue
        for page in range(va & ~4095,(va+memsz+4095)&~4095,4096):
            entry=elf_pt(page)
            expected[page]=5 | (2 if flags&2 else 0)
            check(entry&7 == expected[page],'ELF PTE permissions')
        # File and BSS comparisons use physical aliases, so R/O mapping is not bypassed by user code.
        for start,size,wanted in ((va,filesz,blob[off:off+filesz]),(va+filesz,memsz-filesz,bytes(memsz-filesz))):
            done=0
            while done<size:
                address=start+done
                entry=elf_pt(address)
                chunk=min(size-done,4096-(address&4095))
                actual=bytes(gdb.selected_inferior().read_memory((entry&~4095)+(address&4095),chunk))
                check(actual == wanted[done:done+chunk],'file copy/BSS mismatch')
                done+=chunk
    for page in range(0x083fc000,0x08400000,4096):
        entry=elf_pt(page)
        expected[page]=7
        check(entry&7 == 7 and not any(words(entry&~4095,1024)),'stack not independently zeroed')
    check(count == len(expected)+2,'wrong external page budget')
    table=words(external_pages[1],1024)
    targets=[]
    for i,entry in enumerate(table):
        va=0x08000000+i*4096
        if va in expected:
            check(entry&7 == expected[va],'mapped permission mismatch')
            targets.append(entry&~4095)
        else:
            check(entry == 0,'unexpected external user mapping')
    check(set(targets) == set(external_pages[2:]),'untracked image/stack page')
    directory=words(external_pages[0],1024)
    root=words(v('&page_directory'),1024)
    check(directory[32]&0xfffff007 == external_pages[1]|7,'wrong user PDE')
    for i,entry in enumerate(directory):
        if i<16:
            check(entry == root[i] and entry&4 == 0,'kernel exposed to external user')
        elif i!=32:
            check(entry == 0,'unexpected external PDE')
    for page in external_pages:
        check(v('states[%d]'%(page>>12)) == 4,'backing page not tracked')
    print('ELF: image+stack pages=',len(expected),'CR3=',hex(external_pages[0]),'stack_top=0x08400000')
def inspect_external_user():
    check(v('$cs') == 0x1b and v('$ss') == 0x23,'external code not CPL3')
    check(v('$cr3') == d(0,'space.pages[0]'),'wrong external CR3')
    check(0x083fc000<=v('$esp')<0x08400000,'external stack pointer')
    print('CPL3: EIP=',hex(v('$eip')),'ESP=',hex(v('$esp')),'CR3=',hex(v('$cr3')))
def inspect_resource(va,marker):
    inspect_external_user()
    check(d(0,'resource_count') == 1,'external resource/quota test incomplete')
    owned=[i for i in range(16) if v('resources[%d].state'%i)==1 and v('resources[%d].cap.owner_id'%i)==1]
    check(len(owned)==1,'external ownership missing')
    slot=owned[0]
    pa=v('resources[%d].physical_address'%slot)
    check(v('resources[%d].references'%slot)==1,'external binding reference')
    table=d(0,'space.dynamic_table')
    entry=words(table,1024)[(va-0x09000000)//4096]
    check(entry&0xfffff007 == pa|7 and words(pa,1)[0] == marker,'resource PTE/access not real')
    print('RESOURCE: PFN=',hex(pa>>12),'PTE=',hex(entry),'data=',hex(words(pa,1)[0]))
def inspect_external_reaped():
    check(v('$cr3') == v('&page_directory') and v('current_domain') == 0,'unsafe teardown CR3')
    for i in range(2):
        check(d(i,'space.count')==0 and d(i,'space.live')==0,'image not destroyed')
        check(d(i,'resource_count')==0 and d(i,'space.dynamic_table')==0,'external resource leak')
    for page in external_pages+peer_pages:
        check(v('states[%d]'%(page>>12))==2 and not any(words(page,1024)),'image/stack page not cleared and released')
    for slot in range(16):
        check(v('resources[%d].state'%slot)==0 and v('resources[%d].references'%slot)==0,'live resource after exit/fault')
    states=bytes(gdb.selected_inferior().read_memory(v('&states'),16384))
    check(states.count(2)==v('external_free_before'),'external lifetime leak')
    print('PASS: external image, stack, tables and dynamic resources reclaimed')
end
'''


def run_guest(qemu,gdb,name,file,expected,reference,kind='normal',oom=False):
    data=file.read_bytes() if file else b''
    segments=headers(data) if expected==0 else []
    names=symbols(reference)
    peer_names=symbols(BUILD/'user_b.elf')
    commands=HELPERS+GDB_HELPERS
    for round_number in range(2):
        commands+='delete breakpoints\nhbreak *external_prepared\ncontinue\npython\n'
        commands+=f"check(v('external_load_result') == ({expected}&0xffffffff),'wrong load result')\n"
        if expected==0:
            commands+=f'inspect_external({segments!r},{data!r})\n'
        else:
            commands+="external_pages=[]\npeer_pages=[d(1,'space.pages[%d]'%i) for i in range(6)]\ncheck(d(0,'space.count')==0 and d(0,'space.live')==0,'failed image retained pages')\ncheck(v('external_free_before_load')==v('external_free_after_load'),'load failure leaked pages')\n"
            if oom:
                commands+="check(d(0,'space.rollback_pages')==5,'did not exercise partial allocation rollback')\nfor i in range(5):\n    page=d(0,'space.pages[%d]'%i)\n    check(v('states[%d]'%(page>>12))==2 and not any(words(page,1024)),'rollback did not clear partial page')\n"
        commands+='end\n'
        if expected==0:
            commands+=f"delete breakpoints\nhbreak *{struct.unpack_from('<I',data,24)[0]:#x}\ncontinue\npython inspect_external_user()\n"
            commands+=f"delete breakpoints\nhbreak *{names['external_user_checkpoint']:#x}\ncontinue\npython inspect_external_user()\n"
            commands+='delete breakpoints\nhbreak *syscall_dispatch\ncontinue\npython\ncpu=inspect_trap(0)\ncheck(cpu[7]==3,\'first external call was not ABI query\')\nend\n'
            if kind!='runtime':
                commands+=f"delete breakpoints\nhbreak *{names['external_user_checkpoint']:#x}\ncontinue\npython inspect_resource({0x09002000 if kind=='minimal' else 0x09007000},{0x12345678 if kind=='minimal' else 0xaabb0000})\n"
        commands+=f"delete breakpoints\nhbreak *{peer_names['user_checkpoint']:#x}\ncondition $bpnum current_domain->id == 2\ncontinue\npython\ncheck(v('$cs')==0x1b and v('$cr3')==d(1,'space.pages[0]'),'peer not real isolated user')\nend\n"
        if expected==0 and kind in ('fault','code-fault'):
            error=7 if kind=='code-fault' else 5
            address=BASE if kind=='code-fault' else 0x100000
            commands+='delete breakpoints\nhbreak *domain_fault\ncontinue\npython\n'
            commands+=f"cpu=inspect_trap(0)\ncheck(cpu[8]==14 and cpu[9]=={error} and cpu[10]=={names['external_fault']},'external fault frame wrong')\ncheck(v('$cr2')=={address},'external CR2 wrong')\nprint('HARDWARE PF:',hex(cpu[10]),hex(v('$cr2')),cpu[9])\nend\n"
        elif expected==0 and kind!='runtime':
            commands+=f"delete breakpoints\nhbreak *{names['external_user_checkpoint']:#x}\ncontinue\npython\ninspect_external_user()\ncheck(d(0,'resource_count')==0 and d(0,'space.dynamic_table')==0,'explicit external release failed')\nend\n"
        commands+='delete breakpoints\nhbreak *external_reaped\ncontinue\npython\ninspect_external_reaped()\n'
        if expected==0:
            if kind in ('fault','code-fault'):
                commands+="check(d(0,'state')==4,'external fault did not terminate domain')\n"
            else:
                commands+=f"check(d(0,'state')==3 and d(0,'exit_status')=={93 if kind=='runtime' else 0},'external user failed')\n"
        commands+="check(d(1,'state')==3 and d(1,'exit_status')==0 and d(1,'yields')==1,'peer corrupted')\nend\n"
    commands+='delete breakpoints\ndetach\nquit\n'
    port=free_port()
    label='phase5-'+name
    with emulator('debug',qemu,port,append='external'+(' external-oom' if oom else ''),initrd=file,label=label) as (process,log):
        connect_and_run(process,log,port,gdb,commands,label)
        wait_for(process,log,lambda text:'Phase 5: external execution completed' in text,'external completion')
        require('PANIC' not in log.read_text(),'kernel panic in '+name)
    print('PASS:',name,'ELF/ABI/hardware/peer/rollback and two lifecycles')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',default='qemu-system-i386')
    parser.add_argument('--gdb',default='gdb')
    parser.add_argument('--mode',choices=['all','normal'],default='all')
    parser.add_argument('--aelthuvor', type=Path, help='audit independent LibOS build directory instead of fixtures')
    args=parser.parse_args()
    if args.aelthuvor:
        from libos import suite
        suite(args.aelthuvor, args.qemu, args.gdb)
        return
    out=sdk_build()
    positives=[('minimal',out/'minimal.elf','minimal'),('multi',out/'normal.elf','normal'),
               ('kernel-fault',out/'fault.elf','fault'),('code-fault',out/'code-fault.elf','code-fault'),
               ('runtime-abi',out/'runtime-abi.elf','runtime')]
    for name,file,kind in positives if args.mode=='all' else positives[:2]:
        subprocess.run([str(BUILD/'elf-tests'),str(file),'0'],check=True)
        run_guest(args.qemu,args.gdb,name,file,0,file,kind)
    if args.mode=='normal':
        return
    # Section headers are optional for execution; program headers are authoritative.
    stripped=bytearray((out/'normal.elf').read_bytes())
    struct.pack_into('<I',stripped,32,0)
    struct.pack_into('<HHH',stripped,46,0,0,0)
    file=out/'no-sections.elf'
    file.write_bytes(stripped)
    subprocess.run([str(BUILD/'elf-tests'),str(file),'0'],check=True)
    run_guest(args.qemu,args.gdb,'no-sections',file,0,out/'normal.elf')
    run_guest(args.qemu,args.gdb,'repeat',out/'normal.elf',0,out/'normal.elf')
    run_guest(args.qemu,args.gdb,'oom',out/'normal.elf',-12,out/'normal.elf',oom=True)
    run_guest(args.qemu,args.gdb,'no-module',None,-8,out/'normal.elf')
    cases=corpus(out/'normal.elf')
    for name,file,expected in cases:
        subprocess.run([str(BUILD/'elf-tests'),str(file),str(expected)],check=True)
        run_guest(args.qemu,args.gdb,name,file,expected,out/'normal.elf')
    print('PASS: Phase 5 guest total=',9+len(cases),'parser corpus=',6+len(cases))


if __name__=='__main__':
    try:
        main()
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        print('FAIL:',error,file=sys.stderr)
        sys.exit(1)
