#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unmodified Aelthuvor App SDK and the neutral teaching LibOS share one kernel."""
import argparse
import json
from pathlib import Path
from phase1 import BUILD
from phase10 import run,sha,RESULTS

AUDIT=r'''
seen={1:set(),2:set()};ram={};irqs=set();bps=[]
def inspect(i,stage):
    user(i);seen[i].add(stage)
    if stage in (1,2) and i==1 and stage==2:
        check(v('ael_runtime.heap.policy')==POLICY and v('ael_runtime.pages.owned')==1,'Ael policy/runtime')
        check(pt(1,0x09000000)&7==7,'Ael resource PTE');ram[1]=pt(1,0x09000000)&~4095
        check(words(ram[1],1024)==tuple(0xa4e10000|j for j in range(1024)),'Ael app data')
    elif i==2 and stage==1:
        check(pt(2,0x0900f000)&7==7 and dd(2,'resource_count')==1,'teaching pool resource')
        ram[2]=pt(2,0x0900f000)&~4095
        check(bytes(gdb.selected_inferior().read_memory(ram[2],4096))==bytes((j//64)^(j%64)^0x5a for j in range(4096)),'bitmap data')
    if len(ram)==2 and not started[0]:
        check(dd(1,'resource_count')==dd(2,'resource_count')==1 and ram[1]!=ram[2],'LibOS pages not independently coexisting')
        check(pt(1,0x0900f000)==0 and pt(2,0x09000000)==0,'unauthorized mapping shared')
        if FAULT:uw(2,N[1]['lesson_command'],4)
        uw(2,N[1]['lesson_gate'],1);started[0]=True

def observed(i):
    if v('current_domain')==0 or v('current_domain->id')!=i:return
    stage=words(v('$esp')+4,1)[0] if i==1 else ur(2,N[1]['example_stage'])
    inspect(i,stage)
def irq():
    i=v('current_domain->id');frame=words(words(v('$esp')+4,1)[0],15)
    check(frame[8]==32 and frame[11]==0x1b and v('$cr3')==dd(i,'space.pages[0]'),'not real isolated user IRQ0')
    irqs.add(i)
class Observer(gdb.Breakpoint):
    def __init__(self,where,fn):super().__init__('*'+str(where),type=gdb.BP_HARDWARE_BREAKPOINT,internal=True);self.fn=fn
    def stop(self):
        try:self.fn()
        except Exception:
            import traceback;gdb.write(traceback.format_exc());gdb.execute('quit 1')
        return False
started=[False];uw(2,N[1]['lesson_command'],5)
bps.append(Observer(N[0]['memory_observed'],lambda:observed(1)))
bps.append(Observer(N[1]['example_observed'],lambda:observed(2)))
bps.append(Observer('domain_preempt',irq))
finished()
check(started[0] and 2 in seen[1] and 3 in seen[1],'Ael app did not run memory checks through release')
check(dd(1,'last_state')==3 and dd(1,'exit_status')==0 and 1 in irqs,'Ael failed or was never preempted')
if FAULT:check(dd(2,'last_state')==4 and dd(2,'fault_vector')==6,'fault not isolated to teaching LibOS')
else:check(dd(2,'last_state')==3 and dd(2,'exit_status')==0 and 3 in seen[2] and 2 in irqs,'teaching LibOS failed')
for pa in ram.values():check(v('states[%d]'%(pa>>12))==2 and not any(words(pa,1024)),'mixed LibOS RAM leak')
print('TWO LIBOS policies=variable-size/bitmap; CR3s=',[hex(saved[i][0]) for i in used],'IRQ0 domains=',irqs,'fault=',FAULT)
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--aelthuvor',type=Path,required=True,help='prebuilt Phase 4 build directory')
    p.add_argument('--qemu',default='qemu-system-i386');p.add_argument('--gdb',default='gdb');a=p.parse_args()
    files=[a.aelthuvor/'apps/memory/app.elf',a.aelthuvor/'apps/memory-best/app.elf']
    before=[sha(p) for p in files]
    for policy,f in enumerate(files):
        for fault in (False,True):
            body=f'POLICY={policy}\nFAULT={fault!r}\n'+AUDIT
            run(a,'ael-bitmap-'+str(policy)+('-fault' if fault else ''),[f,BUILD/'examples/memory-policy/libos.elf'],body)
    assert before==[sha(p) for p in files]
    (BUILD/'phase10-compat-results.json').write_text(json.dumps(dict(guests=len(RESULTS),cases=RESULTS,input_sha256=before),indent=2)+'\n')
    print('PASS: four Aelthuvor/neutral LibOS coexistence and fault-isolation guests; no external ELF changed')
if __name__=='__main__':main()
