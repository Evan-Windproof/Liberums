#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional integration with an independently built, unmodified Aelthuvor Phase 3.
No build, source, SDK or git operation is performed on that repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
from phase1 import BUILD, require
from phase5 import symbols
from phase72 import SCANS
from phase8 import run, FIX, RESULTS


def memory(args,directory,policy):
    elf=directory/('preempt-best.elf' if policy else 'preempt-first.elf')
    n=symbols(elf)
    body=f'''gdb.execute('add-symbol-file {elf}',to_string=True)
for i in (2,3,4): command(i,0)
go({n['ael_checkpoint']},'current_domain->id == 1 && *(unsigned int*)($esp+4) == 2')
user(1)
check(v('ael_runtime.heap.policy')=={policy} and v('ael_runtime.heap.live')==1 and dd(1,'resource_count')==1,'Aelthuvor policy/heap/quota')
va=v('ael_demo_memory');pa=pt(1,va)&~4095
check(words(pa,1024)==tuple(0xa3110000|j for j in range(1024)),'Aelthuvor memory checks/data failed')
for i in (2,3,4): checkpoint(i,1)
check(ownership()=={{1:1,2:4,3:4,4:4}},'four live programs do not own independent resources')
check(dd(1,'state')==1 and dd(1,'preemptions')>0 and dd(1,'yields')==0,'Ael CPU protection/coexistence')
print('AEL COEXIST policy={policy} independent_ELFs=4 owned=13 Ael_preemptions=',dd(1,'preemptions'))
for i in (2,3,4): command(i,1)
finished()
for i in used: check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'coexisting ELF failed its assertions')
check(dd(1,'preemptions')>0 and dd(1,'yields')==0,'Ael noncooperative CPU profile changed')
'''
    run(args,'ael-coexist-'+str(policy),[elf,FIX/'worker-a.elf',FIX/'worker-b.elf',FIX/'worker-a.elf'],body)


def console(args,directory):
    elf=directory/'console.elf';n=symbols(elf)
    actions=[]
    for text,wanted in [('help','help clear about status memtest exit'),('status','ABI 1.2 features=119'),('memtest','memtest: PASS'),('about','Aelthuvor: independent Ring 3 Library OS'),('exit','Console exiting')]:
        for ch in text: actions.append((ch,[SCANS[ch],SCANS[ch]|0x80],None))
        actions.append(('ret',[0x1c] if text=='exit' else [0x1c,0x9c],wanted))
    body=f'''gdb.execute('add-symbol-file {elf}',to_string=True)
for i in (2,3,4): command(i,0)
go({n['ael_console_observed']},'current_domain->id == 1 && ael_console_sequence == 1')
user(1)
check(v('ael_console_error')==0 and v('ael_console.length')==0,'console initial state')
check(v('keyboard.count')==0 and v('keyboard.cap.owner_id')==1,'console grant/initial queue')
line=v('ael_console.line');check(0x09000000<=line<0x09010000 and pt(1,line)&7==7,'console input buffer not RAM')
for i in (2,3,4): checkpoint(i,1)
check(ownership()=={{1:1,2:4,3:4,4:4}},'console/background resources overlap')
check(all(pt(i,0x09400000)==0 for i in (2,3,4)) and pt(1,0x09400000)&0xfffff09f==0xb801f,'exclusive VGA mapping')
sequence=1;events=0
for code,expected,wanted in {actions!r}:
    inject([code])
    for byte in expected:
        go({n['ael_console_observed']},'current_domain->id == 1 && ael_console_sequence > %d'%sequence)
        user(1);sequence+=1;events+=1
        check(v('ael_console_sequence')==sequence and v('ael_console_raw')==byte and v('ael_console_error')==0,'console actual scan code/sequence')
    if wanted:
        fb=bytes(gdb.selected_inferior().read_memory(0xb8000,4000))
        text='\\n'.join(fb[r*160:(r+1)*160:2].decode('ascii',errors='replace') for r in range(25))
        check(wanted in text,'Ring3 command did not draw '+wanted)
        if wanted=='memtest: PASS':
            check(v('ael_console.memtests')==1 and dd(1,'resource_count')==1,'memtest did not trim heap')
            query('screendump',{{'filename':{str(BUILD/'phase8-ael-console.png')!r},'format':'png'}})
print('AEL CONSOLE + three ELF workers: actual PS/2 bytes=',events,'IRQ1=',v('keyboard_irqs'),'IRQ0=',v('irq_count'))
for i in (2,3,4): command(i,1)
finished()
for i in used: check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'console coexist/cleanup failed')
'''
    run(args,'ael-console-coexist',[elf,FIX/'worker-a.elf',FIX/'worker-b.elf',FIX/'worker-a.elf'],body,'multi-vga=1 multi-keyboard=1')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--aelthuvor',type=Path,required=True,help='independent Phase 3 build directory')
    p.add_argument('--qemu',default='qemu-system-i386');p.add_argument('--gdb',default='gdb');a=p.parse_args()
    files=[a.aelthuvor/n for n in ('preempt-first.elf','preempt-best.elf','console.elf')]
    before=[hashlib.sha256(f.read_bytes()).hexdigest() for f in files]
    for policy in (0,1): memory(a,a.aelthuvor,policy)
    console(a,a.aelthuvor)
    require(before==[hashlib.sha256(f.read_bytes()).hexdigest() for f in files],'independent ELF changed during tests')
    (BUILD/'phase8-ael-results.json').write_text(json.dumps(dict(guests=len(RESULTS),cases=RESULTS,elf_sha256=dict(zip([f.name for f in files],before))),indent=2)+'\n')
    print('PASS: three Aelthuvor/four-domain QEMU guests; ELF hashes unchanged')
if __name__=='__main__':main()
