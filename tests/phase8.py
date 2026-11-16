#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Multiboot modules, CPL3, four-domain ownership and lifecycle audits.
Debugger writes only adversary inputs / explicitly gated diagnostic requests;
it never fabricates IRQs, successful syscalls, page tables or resource records.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
from phase1 import ROOT, BUILD, emulator, require, wait_for
from phase2 import connect_and_run
from phase3 import HELPERS
from phase5 import headers, symbols
from phase72 import vm, injector, INJECT

FIX = BUILD/'phase8'
RESULTS = []
COMMON = r'''
S=SYMBOLS
saved={}
live_bp=None
def dd(i,f): return d(i-1,f)
def setk(name,value): gdb.execute('set variable '+name+' = '+str(value),to_string=True)
def go(site,condition=None):
    global live_bp
    if live_bp is not None: live_bp.delete()
    bp=gdb.Breakpoint('*'+str(site) if isinstance(site,int) else '*'+site,type=gdb.BP_HARDWARE_BREAKPOINT,internal=True)
    live_bp=bp
    if condition: bp.condition=condition
    gdb.execute('continue')
    check(v('$eip')==(site if isinstance(site,int) else v('&'+site)),'unexpected stop at '+str(site))
def pt(i,va):
    directory=words(dd(i,'space.pages[0]'),1024)
    return words((directory[va>>22]&~4095)+((va>>12)&1023)*4,1)[0] if directory[va>>22]&1 else 0
def physical(i,va):
    entry=pt(i,va);check(entry&5==5,'unmapped private input');return (entry&~4095)+(va&4095)
def ur(i,va): return words(physical(i,va),1)[0]
def uw(i,va,value): gdb.selected_inferior().write_memory(physical(i,va),struct.pack('<I',value&0xffffffff))
def offset(field): return v('&((struct worker_control*)0)->'+field)
def w(i,field): return ur(i,S['worker']+offset(field))
def put(i,field,value): uw(i,S['worker']+offset(field),value)
def command(i,value): uw(i,S['worker_command'],value)
def handle(i,field='handles[0]'): return (w(i,field+'.index'),w(i,field+'.generation'))
def inject_handle(i,field,h): put(i,field+'.index',h[0]);put(i,field+'.generation',h[1])
def user(i):
    check(v('current_domain->id')==i and dd(i,'state')==2,'wrong running identity')
    check(v('$cs')==0x1b and v('$ss')==0x23 and v('$cr3')==dd(i,'space.pages[0]'),'CPL3/CR3/SS')
    check(all(v('$'+s)==0x23 for s in ('ds','es','fs','gs')),'user segment')
    check(0x083fc000<=v('$esp')<0x08400000,'user stack range')
    check(v('$eflags')&0x3200==0x200,'IF/IOPL')
    bottom=v('&domains[%d].kernel_stack'%(i-1))
    check(v('tss.esp0')==bottom+8192 and v('tss.ss0')==16 and v('tss.io_map_base')==104,'TSS switch / port IO protection')
def checkpoint(i,stage,after=0):
    stageva=S['worker']+offset('stage');seqva=S['worker']+offset('sequence')
    if stage==1:
        go(S['worker_idle'],'current_domain->id == %d'%i)
    else:
        go(S['worker_observed'],'current_domain->id == %d && *(unsigned int*)%d == %d && *(unsigned int*)%d > %d'%(i,stageva,stage,seqva,after))
    user(i);check(w(i,'stage')==stage and w(i,'sequence')>after,'missed worker observation')
def perform(i,cmd,stage):
    sequence=w(i,'sequence');command(i,cmd);checkpoint(i,stage,sequence)
def image(i,segments,blob):
    pages=[dd(i,'space.pages[%d]'%j) for j in range(dd(i,'space.count'))]
    expected={}
    for kind,off,va,pa,filesz,memsz,flags,align in segments:
        if kind!=1: continue
        for addr in range(va&~4095,(va+memsz+4095)&~4095,4096):
            expected[addr]=5|(2 if flags&2 else 0)
            check(pt(i,addr)&7==expected[addr],'ELF permission')
        for start,n,wanted in ((va,filesz,blob[off:off+filesz]),(va+filesz,memsz-filesz,bytes(memsz-filesz))):
            done=0
            while done<n:
                addr=start+done;count=min(n-done,4096-(addr&4095))
                check(bytes(gdb.selected_inferior().read_memory(physical(i,addr),count))==wanted[done:done+count],'ELF bytes / BSS')
                done+=count
    for va in range(0x083fc000,0x08400000,4096):
        expected[va]=7;check(pt(i,va)&7==7 and not any(words(pt(i,va)&~4095,1024)),'new stack not zero')
    check(len(pages)==len(expected)+2 and len(set(pages))==len(pages),'image page accounting')
    directory=words(pages[0],1024);root=words(v('&page_directory'),1024)
    for index,e in enumerate(directory):
        if index<16: check((e&~0x20)==(root[index]&~0x20) and not e&4,'kernel user accessible')
        elif index==32: check(e&~0x20==pages[1]|7,'ELF PDE')
        else: check(e==0,'unexpected new PDE')
    table=words(pages[1],1024)
    for index,e in enumerate(table):
        va=0x08000000+index*4096
        check((e&7==expected[va]) if va in expected else e==0,'unexpected new PTE')
    check(set(e&~4095 for e in table if e&1)==set(pages[2:]),'untracked backing page')
    check(all(v('states[%d]'%(p>>12))==4 for p in pages),'image RAM state')
    saved[i]=pages
    print('IMAGE id=%d epoch=%d CR3=%#x pages=%d stack=0x083fc000..0x08400000'%(i,dd(i,'epoch'),pages[0],len(pages)))
def ownership():
    allpages=[];counts={i:0 for i in range(1,5)}
    for r in range(16):
        base='resources[%d].'%r;state=v(base+'state')
        if state==0:
            check(v(base+'cap.owner_id')==0 and v(base+'references')==0,'free capability retained owner');continue
        check(state in (1,3),'transient resource at checkpoint')
        owner=v(base+'cap.owner_id');check(owner in counts,'bad resource owner')
        check(v(base+'cap.owner_epoch')==dd(owner,'epoch'),'resource epoch mismatch')
        pa=v(base+'physical_address');allpages.append(pa);counts[owner]+=1
        check(v('states[%d]'%(pa>>12))==4 and pa!=0xb8000,'resource RAM state')
        refs=0
        for j in range(4):
            va=v(base+'bindings[%d].address'%j)
            if va:
                refs+=1;check(pt(owner,va)&0xfffff005==pa|5,'secure binding targets wrong page')
        check(refs==v(base+'references'),'binding reference count')
    check(len(set(allpages))==len(allpages),'physical RAM assigned twice')
    for i,n in counts.items(): check(n==dd(i,'resource_count') and n<=4,'domain quota/accounting')
    check(v('states[%d]'%(0xb8000>>12))==1,'device page became allocatable RAM')
    return counts

def released(i,oldpages=()):
    check(v('current_domain')==0 and v('$cr3')==v('&page_directory') and v('$eflags')&0x200==0,'unsafe reclaim context')
    check(dd(i,'state')==0 and dd(i,'pool_owned')==0,'slot not UNUSED')
    check(dd(i,'space.live')==0 and dd(i,'space.count')==0 and dd(i,'space.dynamic_table')==0 and dd(i,'space.device_table')==0,'space survived')
    check(dd(i,'resource_count')==0 and dd(i,'inbox.index')==0 and dd(i,'inbox.generation')==0,'authority survived')
    check(not any(bytes(gdb.selected_inferior().read_memory(v('&domains[%d].kernel_stack'%(i-1)),8192))),'kernel stack not cleared')
    check(not any(words(v('&domains[%d].context'%(i-1)),15)) and not any(words(v('&domains[%d].segments'%(i-1)),4)),'registers not cleared')
    for pa in oldpages:
        check(v('states[%d]'%(pa>>12))==2 and not any(words(pa,1024)),'RAM not zero/freed')
    check(all(not(v('events[%d].active'%j) and v('events[%d].owner_id'%j)==i) for j in range(8)),'revoke event survived')
def finished():
    go('multi_reaped')
    for i in range(1,5): released(i,saved.get(i,()))
    check(not any(v('resources[%d].state'%r) for r in range(16)),'resource leak')
    check(not any(v('events[%d].active'%j) for j in range(8)),'revoke event leak')
    check(v('vga_device.grant_id')==0 and v('keyboard.grant_id')==0 and v('keyboard.count')==0 and v('keyboard.enabled')==0,'device leak')
    check(v('irq_count')==v('eoi_count') and v('keyboard_irqs')==v('keyboard_eois'),'EOI mismatch')
    actual=bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)
    check(actual==v('multi_free_before'),'physical free count did not recover')
    print('RECOVERY all slots UNUSED; resources=0 events=0 devices=0 free_pages=%d IRQ0=%d IRQ1=%d'%(actual,v('irq_count'),v('keyboard_irqs')))
def ready(expected,images):
    go('multi_prepared')
    check(v('boot.module_count')==len(expected),'actual Multiboot count')
    used=[]
    for n,result in enumerate(expected):
        check(v('multi_loads[%d].result'%n)==result&0xffffffff,'module result %d'%n)
        lo=v('boot.modules[%d].begin'%n);hi=v('boot.modules[%d].end'%n)
        check(lo<hi and all(v('states[%d]'%p)==1 for p in range(lo>>12,(hi+4095)>>12)),'module not reserved')
        if result==0:
            i=v('multi_loads[%d].id'%n);check(i not in used,'two modules one live slot');used.append(i)
            check(dd(i,'state')==1 and dd(i,'pool_owned')==1 and dd(i,'epoch')==v('multi_loads[%d].epoch'%n),'slot identity/state')
            image(i,*images[n])
    pages=[p for i in used for p in saved[i]]
    check(len(set(pages))==len(pages),'independent ELF spaces share RAM')
    return used
'''


def run(args,label,files,body,options='',expected=None):
    expected=expected if expected is not None else [0]*len(files)
    images=[(headers(p.read_bytes()),p.read_bytes()) if e==0 else ([],b'') for p,e in zip(files,expected)]
    names=symbols(FIX/'worker-a.elf')
    commands=HELPERS+f'add-symbol-file {FIX/"worker-a.elf"}\n'+INJECT
    commands+='python\n'+COMMON.replace('SYMBOLS',repr(names))
    commands+=f'used=ready({expected!r},{images!r})\n'+body+'\nprint("PASS: phase8 '+label+'")\nend\ndetach\nquit\n'
    with vm(args.qemu,'multi-external selftest=phase8 '+options,'phase8-'+label,','.join(map(str,files))) as (p,log,port,endpoint):
        with injector(endpoint) as control:
            output=connect_and_run(p,log,port,args.gdb,commands.replace('CONTROL',repr(control)),'phase8-'+label)
        wait_for(p,log,lambda s:'Phase 8: multi external execution completed' in s,'multi lifetime completion')
        require('PANIC' not in log.read_text(),'guest panic')
    require('PASS: phase8 '+label in output,'audit not reached')
    RESULTS.append(dict(label=label,modules=len(files),loaded=sum(e==0 for e in expected),log='phase8-'+label+'-gdb.log'))
    print('PASS: Phase 8 '+label)

HOLD='''for i in used: command(i,0)
for i in used: checkpoint(i,1)
check(all(n==4 for i,n in ownership().items() if i in used),'four RAM pages per worker')
'''
EXIT='''for i in used: command(i,1)
finished()
for i in used: check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'worker failed its ABI assertions')
'''


def create_cases(args,a,b):
    for count in (2,3,4,5,8):
        files=[a if i%2==0 else b for i in range(count)]
        run(args,'modules-'+str(count),files,HOLD+EXIT,expected=[0]*min(count,4)+[-28]*max(0,count-4))
    bad=FIX/'invalid.elf';bad.write_bytes(b'not an ELF')
    run(args,'invalid-middle',[a,bad,b],HOLD+EXIT,expected=[0,-8,0])
    blob=bytearray(b.read_bytes());note=next(h for h in headers(blob) if h[0]==4);struct.pack_into('<I',blob,note[1]+24,2)
    badabi=FIX/'abi.elf';badabi.write_bytes(blob)
    run(args,'abi-middle',[a,badabi,b],HOLD+EXIT,expected=[0,-93,0])
    run(args,'partial-oom',[a,b,b],"check(dd(2,'space.rollback_pages')==0,'last valid creation did not reset rollback count')\n"+HOLD+EXIT,'phase8-oom=2',expected=[0,-12,0])
    # The diagnostic pressure allocator holds real RAM. Stop the failed slot's
    # actual reaped checkpoint *before* the third module overwrites its record.
    # A separate two-module guest leaves rollback_pages visible for inspection.
    run(args,'oom-five-pages',[a,b],"check(dd(2,'space.rollback_pages')==5 and dd(2,'state')==0,'partial OOM did not roll back five real pages')\n"+HOLD+EXIT,'phase8-oom=2',expected=[0,-12])


def resources(args,a,b):
    body=HOLD+'''
old=handle(1);pa=pt(1,0x09000000)&~4095
put(1,'target',3);put(1,'expected_error',-28);perform(1,5,2)
check(handle(1)==old and pt(1,0x09000000)&~4095==pa and ownership()[1]==4,'failed transfer changed ownership')
perform(3,6,3);check(ownership()[3]==3,'release quota')
put(1,'target',3);perform(1,7,4)
check(pt(1,0x09000000)==0 and not any(words(pa,1024)),'transfer retained alias or bytes')
check(dd(3,'inbox.index')==old[0] and dd(3,'inbox.generation')==old[1]+1,'inbox generation')
check(ownership()=={1:3,2:4,3:4,4:4},'transfer counts')
perform(3,8,5);check(w(3,'received_pfn')==pa//4096,'receive wrong physical page')
foreign=handle(3);inject_handle(4,'attack',foreign);put(4,'expected_error',-1);perform(4,10,6)
check(pt(4,0x09005000)==0,'foreign capability mapped memory')
# Stale generation against a live slot, independently of foreign-owner failure.
inject_handle(4,'attack',old);put(4,'expected_error',-116);perform(4,10,6)
# A terminal target must not receive a page; hold the caller's other valid handle.
command(2,1);go('domain_exit','current_domain->id == 2');go('domain_run_slice','domain->id == 3')
# At this point domain 2 has returned and its resources have been cleaned.
check(dd(2,'state')==3 and dd(2,'resource_count')==0,'exited recipient still owns RAM')
inject_handle(1,'handles[0]',handle(1,'handles[1]'));put(1,'target',2);put(1,'expected_error',-3);perform(1,9,2)
put(1,'target',9);perform(1,9,2)
# Physical aliases exist only as supervisor mappings. A fourth domain reading a
# third domain's physical RAM must fault; other domains still reach EXIT.
put(4,'address',pa);command(4,4)
for i in (1,3): command(i,1)
finished()
check(dd(4,'last_state')==4 and dd(4,'fault_vector')==14 and dd(4,'fault_error')==5 and dd(4,'fault_address')==pa,'cross-domain physical read not isolated')
for i in (1,2,3): check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'survivor failed')
'''
    run(args,'transfer-isolation',[a,b,a,b],body)
    body=HOLD+'''
perform(3,6,3);perform(3,12,8)
old=handle(1);put(1,'target',3);perform(1,7,4)
check(dd(3,'resource_count')==3 and dd(3,'inbox.index')==old[0],'unread inbox not accounted')
received=(dd(3,'inbox.index'),dd(3,'inbox.generation'))
source=handle(2);source_pa=pt(2,0x09000000)&~4095
put(2,'target',3);put(2,'expected_error',-11);perform(2,5,2)
check(handle(2)==source and pt(2,0x09000000)&~4095==source_pa and dd(3,'inbox.index')==received[0],'full inbox rejection changed authority')
setk('phase8_control.reuse_id',3);setk('phase8_control.reuse_module',2);command(3,1)
go('domain_pool_reaped');released(3,saved[3]);check(ownership()=={1:3,2:4,3:0,4:4},'unreceived resource leaked')
go('domain_pool_ready');image(3,*INBOX_IMAGE);inject_handle(3,'attack',received);command(3,0)
checkpoint(3,1);check(dd(3,'epoch')==2 and dd(3,'inbox.index')==0,'new epoch inherited inbox')
check(ownership()=={1:3,2:4,3:4,4:4},'reused recipient resource count')
for i in used: command(i,1)
finished()
for i in used: check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'inbox cleanup survivor failed')
'''
    run(args,'inbox-exit-reuse',[a,b,a,b],body.replace('INBOX_IMAGE',repr((headers(a.read_bytes()),a.read_bytes()))))


def stress(args,a,b):
    body=HOLD+'''
free_running=bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)
for cycle in range(24):
    old_epoch=dd(1,'epoch');old=handle(1)
    oldpages=saved[1]+[dd(1,'space.dynamic_table')]
    oldpages += [v('resources[%d].physical_address'%r) for r in range(16) if v('resources[%d].cap.owner_id'%r)==1]
    setk('phase8_control.reuse_id',1);setk('phase8_control.reuse_module',cycle%2);setk('phase8_control.reuse_devices',0)
    command(1,2 if cycle%2 else 1)
    go('domain_pool_reaped');check(v('pool_last->id')==1,'wrong slot reaped')
    released(1,oldpages)
    check(dd(1,'last_state')==(4 if cycle%2 else 3),'wrong terminal result')
    if cycle%2: check(dd(1,'fault_vector')==6,'UD fault not recorded')
    else: check(dd(1,'exit_status')==0,'worker exit assertions failed')
    check(ownership()=={1:0,2:4,3:4,4:4},'survivor resources changed')
    go('domain_pool_ready');check(v('pool_last->id')==1 and dd(1,'epoch')==old_epoch+1,'slot epoch did not advance')
    # Both compiled workers have the same declared ELF segment layout but
    # distinct data initializers. Check the actual selected module bytes.
    image(1,*STRESS_IMAGES[cycle%2])
    command(1,0);inject_handle(1,'attack',old)
    checkpoint(1,1)
    check(ownership()=={1:4,2:4,3:4,4:4},'reuse resource quota/identity failure')
    now=bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)
    check(now==free_running,'per-cycle free count drift')
    check(all(dd(i,'state') in (1,2) and dd(i,'epoch')==1 for i in (2,3,4)),'survivor identity lost')
    print('REUSE cycle=%d id=1 old_epoch=%d new_epoch=%d terminal=%s owned=16 free=%d recovered=yes'%(cycle+1,old_epoch,dd(1,'epoch'),'FAULT' if cycle%2 else 'EXIT',now))
for i in used: command(i,1)
finished()
check(v('phase8_control.reuse_done')==24 and dd(1,'epoch')==25,'missing reuse lifetimes')
for i in used: check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'final worker failed')
'''
    images=[(headers(p.read_bytes()),p.read_bytes()) for p in (a,b)]
    run(args,'reuse-24',[a,b,a,b],body.replace('STRESS_IMAGES',repr(images)))


def registers(args,a,b):
    body='''for i in used: command(i,3)
for i in used: go(S['worker_spin_begin'],'current_domain->id == %d'%i);user(i)
check(ownership()=={1:4,2:4,3:4,4:4},'four-domain resource exhaustion')
seen=[]
for target in used:
    # Raw IRQ entry precedes pushal and selector normalization. Keep sampling
    # until this domain is inside the deliberately non-cooperative GP sentinel loop.
    while True:
        go('timer_irq_capture')
        cpu=words(v('$esp')+4,5)
        i=v('current_domain->id');seen.append(i)
        if i==target and S['worker_spin_begin']<=cpu[0]<S['worker_spin_end']: break
    check(cpu[1]==0x1b and cpu[4]==0x23 and v('$esp')+4==v('tss.esp0')-20,'IRQ0 raw outer frame/TSS')
    regs={r:v('$'+r) for r in ('eax','ebx','ecx','edx','esi','edi','ebp','ds','es','fs','gs')}
    expected=(0x11111111,0x22222222,0x33333333,0x44444444,0x55555555,0x66666666,0x77777777)
    check(tuple(regs[r] for r in ('eax','ebx','ecx','edx','esi','edi','ebp'))==expected,'raw GP sentinel')
    cr3=v('$cr3');check(cr3==dd(target,'space.pages[0]'),'raw IRQ0 CR3')
    go(cpu[0],'current_domain->id == %d'%target)
    user(target)
    check(tuple(v('$'+r) for r in regs)==tuple(regs.values()),'GP/segment restore mismatch')
    check(v('$esp')==cpu[3] and v('$eflags')&0xcd5==cpu[2]&0xcd5 and v('$cr3')==cr3,'ESP/EFLAGS/CR3 restore')
    check(dd(target,'preemptions')>0,'noncooperative domain never preempted')
    frame=dd(target,'last_kernel_frame');bottom=v('&domains[%d].kernel_stack'%(target-1))
    check(bottom<=frame<bottom+8192-59,'saved frame on another kernel stack')
    print('IRQ0 RESTORE id=%d CPL=3 CR3=%#x TSS.ESP0=%#x EIP=%#x EFLAGS=%#x GP=7 segment=4'%(target,cr3,v('tss.esp0'),cpu[0],cpu[2]))
check(set(seen)==set(used),'not all four domains received IRQ0')
print('IRQ0 round-robin observed domain IDs:',seen)
for i in used: command(i,1)
finished()
for i in used: check(dd(i,'exit_status')==0 and dd(i,'preemptions')>0,'preemption survivor failed')
'''
    run(args,'registers-four',[a,b,a,b],body)


def revoke(args,a,b):
    body=HOLD+'''
h=handle(1);setk('phase8_control.revoke_id',1);setk('phase8_control.index',h[0]);setk('phase8_control.generation',h[1]);setk('phase8_control.delay',1000)
go('phase8_action_done');check(v('phase8_control.result')==0 and v('phase8_control.request')>0,'revoke request failed')
request=v('phase8_control.request')
put(1,'target',3);put(1,'expected_error',-16);perform(1,14,2)
check(ownership()[1]==4,'pending transfer lost owner')
setk('phase8_control.reuse_id',1);setk('phase8_control.reuse_module',0);command(1,1)
go('domain_pool_reaped');released(1,saved[1]);check(v('cancelled')==1,'pending revocation not cancelled')
go('domain_pool_ready');image(1,*REVOKE_IMAGE);command(1,0);inject_handle(1,'attack',h);put(1,'old_request',request)
checkpoint(1,1);check(ownership()=={1:4,2:4,3:4,4:4},'stale ACK reached new domain')
# A different owner is forcibly reclaimed while the other three continue.
h=handle(3);setk('phase8_control.revoke_id',3);setk('phase8_control.index',h[0]);setk('phase8_control.generation',h[1]);setk('phase8_control.delay',2)
go('phase8_action_done');check(v('phase8_control.result')==0,'forced revoke setup')
command(3,3);go('revoke_completed')
check(v('forced')==1 and dd(3,'resource_count')==3 and pt(3,0x09000000)==0,'forced revoke alias survived')
command(3,13)
for i in (1,2,4): command(i,1)
finished()
check(dd(3,'last_state')==4 and dd(3,'fault_vector')==14 and dd(3,'fault_error')==4,'revoked access did not fault')
for i in (1,2,4): check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'revoke damaged survivor')
'''
    image=(headers(a.read_bytes()),a.read_bytes())
    run(args,'revoke-reuse',[a,b,a,b],body.replace('REVOKE_IMAGE',repr(image)))
    body=HOLD+'''
for i in used:
    for j in (0,1):
        h=handle(i,'handles[%d]'%j)
        setk('phase8_control.revoke_id',i);setk('phase8_control.index',h[0]);setk('phase8_control.generation',h[1]);setk('phase8_control.delay',1000)
        go('phase8_action_done');check(v('phase8_control.result')==0,'four-owner event capacity failed')
check(sum(v('events[%d].active'%j) for j in range(8))==8,'eight active events not represented')
check(all(sum(v('events[%d].active'%j) and v('events[%d].owner_id'%j)==i and v('events[%d].owner_epoch'%j)==dd(i,'epoch') for j in range(8))==2 for i in used),'per-owner event quota/identity')
h=handle(4,'handles[2]');setk('phase8_control.revoke_id',4);setk('phase8_control.index',h[0]);setk('phase8_control.generation',h[1]);setk('phase8_control.delay',1000)
go('phase8_action_done');check(v('phase8_control.result')==(-28&0xffffffff),'excess event not refused')
check(v('resources[%d].state'%(h[0]-1))==1,'failed request changed resource state')
print('REVOKE events=8 owners=4 per_owner=2 excess=ENOSPC owned=',sum(ownership().values()))
for i in used: command(i,1)
finished();check(v('cancelled')==8 and v('forced')==0,'exit did not cancel exactly eight requests')
for i in used: check(dd(i,'exit_status')==0,'revocation cleanup damaged worker')
'''
    run(args,'revoke-eight',[a,b,a,b],body)


def devices(args,a,b):
    body='''for i in used: command(i,0)
put(1,'want_devices',1)
for i in used: checkpoint(i,1)
check(ownership()=={1:4,2:4,3:4,4:4},'device scenario resources')
check(pt(1,0x09400000)&0xfffff09f==0xb801f,'VGA PTE PA/cache/permissions')
check(all(pt(i,0x09400000)==0 for i in (2,3,4)),'device alias in unauthorized domain')
oldv=handle(1,'vga');oldk=handle(1,'keyboard');old=handle(1)
# Inject a real PS/2 key, leave both bytes queued without letting user poll.
inject(['a']);go('timer_irq_capture','keyboard.count >= 2')
check(v('keyboard_irqs')>=2 and v('keyboard_irqs')==v('keyboard_eois'),'IRQ1 not real / EOI missing')
print('QUEUED actual PS/2 bytes=',v('keyboard.count'),'IRQ1=',v('keyboard_irqs'))
setk('phase8_control.reuse_id',1);setk('phase8_control.reuse_module',0);setk('phase8_control.reuse_devices',0);command(1,1)
go('domain_pool_reaped');released(1,saved[1]);check(v('keyboard.count')==0 and v('keyboard.grant_id')==0 and v('vga_device.grant_id')==0,'device cleanup')
check(words(0xb8000,1)[0]&0xffff==0x1f38,'device memory zeroed like RAM')
go('domain_pool_ready');image(1,*DEVICE_IMAGE);command(1,0);inject_handle(1,'attack',old)
checkpoint(1,1);check(v('keyboard.grant_id')==0 and v('vga_device.grant_id')==0,'new epoch inherited grants')
# Trusted explicit re-grant. The new owner rejects both old handle generations.
setk('phase8_control.reuse_id',1);setk('phase8_control.reuse_module',0);setk('phase8_control.reuse_devices',7);command(1,1)
go('domain_pool_reaped');released(1,saved[1]);go('domain_pool_ready');image(1,*DEVICE_IMAGE)
command(1,0);put(1,'want_devices',1);inject_handle(1,'old_vga',oldv);inject_handle(1,'old_keyboard',oldk)
checkpoint(1,1)
check(w(1,'vga.generation')==oldv[1]+1 and w(1,'keyboard.generation')==oldk[1]+1,'device generation reuse')
check(v('keyboard.count')==0 and v('keyboard.cap.owner_epoch')==3 and v('vga_device.cap.owner_epoch')==3,'queued input or old epoch inherited')
command(1,11);inject(['b']);checkpoint(1,7,1);check(w(1,'raw')==0x30,'new real make byte mismatch')
perform(1,11,7);check(w(1,'raw')==0xb0,'new real break byte mismatch')
# Fault the device owner, then explicitly re-grant to a fourth lifetime.
secondv=handle(1,'vga');secondk=handle(1,'keyboard')
setk('phase8_control.reuse_id',1);setk('phase8_control.reuse_module',0);setk('phase8_control.reuse_devices',7);command(1,2)
go('domain_pool_reaped');released(1,saved[1]);check(dd(1,'last_state')==4 and dd(1,'fault_vector')==6,'device fault not isolated')
check(v('keyboard.count')==0 and v('keyboard.grant_id')==0 and v('vga_device.grant_id')==0,'fault did not reclaim device')
go('domain_pool_ready');image(1,*DEVICE_IMAGE);command(1,0);put(1,'want_devices',1)
inject_handle(1,'old_vga',secondv);inject_handle(1,'old_keyboard',secondk);checkpoint(1,1)
check(dd(1,'epoch')==4 and w(1,'vga.generation')==secondv[1]+1 and w(1,'keyboard.generation')==secondk[1]+1 and v('keyboard.count')==0,'fault re-grant inherited old identity/input')
for i in used: command(i,1)
finished()
for i in used: check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'device cleanup survivor failed')
'''
    image=(headers(a.read_bytes()),a.read_bytes())
    run(args,'device-lifetimes',[a,b,a,b],body.replace('DEVICE_IMAGE',repr(image)),'multi-vga=1 multi-keyboard=1')
    for device,error in (('vga',-16),('keyboard',-5)):
        # Keep the first owner's trusted grant while a second, conflicting
        # creation rolls back. Third module must not inherit ordinal 2 grants.
        # First owner uses no device syscall and exits normally. Its grant must
        # survive the second creation's rollback, before scheduling starts.
        layout=BUILD/'external/layout.elf'
        run(args,'exclusive-'+device,[layout,b,b],"command(2,0)\ncheck(v('"+('vga_device' if device=='vga' else 'keyboard')+".grant_id')==1,'existing grant lost before scheduling')\ncommand(2,1)\nfinished()\ncheck(dd(1,'exit_status')==0 and dd(2,'exit_status')==0,'exclusive rollback damaged runnable domains')",'multi-'+device+'=1 multi-'+device+'=2',expected=[0,error,0])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',default='qemu-system-i386');parser.add_argument('--gdb',default='gdb')
    parser.add_argument('--case',choices=['create','resources','stress','registers','revoke','devices','all'],default='all')
    args=parser.parse_args();a=FIX/'worker-a.elf';b=FIX/'worker-b.elf'
    for name,fn in [('create',create_cases),('resources',resources),('stress',stress),('registers',registers),('revoke',revoke),('devices',devices)]:
        if args.case in ('all',name): fn(args,a,b)
    if args.case=='all':
        with emulator('run-multi',args.qemu,label='phase8-run-target') as (process,log):
            wait_for(process,log,lambda s:'Phase 8: multi external execution completed' in s,'make run-multi')
            actual=log.read_text();require('PANIC' not in actual and actual.count('Multi terminal id=')==2 and actual.count('state=0x00000003 status=0x00000000')==2,'default run-multi worker failure')
        RESULTS.append(dict(label='run-target',modules=2,loaded=2,log='phase8-run-target-serial.log'))
    audits=sum(case['label']!='run-target' for case in RESULTS)
    (BUILD/('phase8-results-'+args.case+'.json')).write_text(json.dumps(dict(guests=len(RESULTS),gdb_audits=audits,cases=RESULTS,reuses=24 if args.case in ('all','stress') else 0),indent=2)+'\n')
    print('PASS: Phase 8',len(RESULTS),'actual QEMU guests;',audits,'GDB audits')
if __name__=='__main__':main()
