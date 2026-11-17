#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Security regression with real external apps and an unmodified external LibOS.
Requires only Liberums-owned fixtures for the core suite. Optional Ael SDK tests
use prebuilt independent artifacts; no source/SDK patch or PTE writes occur.
"""
import argparse
import hashlib
import json
from pathlib import Path
from phase1 import BUILD, require
from phase5 import symbols, headers
from phase8 import run, FIX, HOLD, EXIT, RESULTS, stress

RAM_HELPERS=r'''
def rr(field): return ur(1,R['ram_'+field])
def rw(field,value): uw(1,R['ram_'+field],value)
def ram_idle(stage=None):
    go(R['ram_idle'],'current_domain->id == 1');user(1)
    if stage is not None: check(rr('stage')==stage,'application did not reach stage '+str(stage))
def ram_op(cmd,stage):
    seq=rr('sequence');rw('command',cmd)
    go(R['ram_observed'],'current_domain->id == 1 && *(unsigned int*)%d == %d && *(unsigned int*)%d > %d'%(R['ram_stage'],stage,R['ram_sequence'],seq))
    user(1);check(rr('stage')==stage,'wrong RAM app observation')
def ram_owner():
    slots=[j for j in range(16) if v('resources[%d].state'%j) in (1,3) and v('resources[%d].cap.owner_id'%j)==1]
    check(len(slots)==1,'Ael application must hold one ordinary RAM page')
    j=slots[0];return (j+1,v('resources[%d].cap.generation'%j)),v('resources[%d].physical_address'%j)
def request(i,h,delay,epoch=None,expected=0):
    for f,value in dict(id=i,epoch=dd(i,'epoch') if epoch is None and 1<=i<=4 else (epoch or 0),index=h[0],generation=h[1],delay=delay,submit=1).items(): setk('phase9_control.'+f,value)
    go('phase9_request_done')
    check(v('phase9_control.result')==expected&0xffffffff and v('phase9_control.submit')==0,'trusted request result/one-shot consumption')
    req=v('phase9_control.request');deadline=v('phase9_control.deadline')
    if expected==0:
        events=[j for j in range(8) if v('events[%d].active'%j) and v('events[%d].owner_id'%j)==i and v('events[%d].handle.index'%j)==h[0] and v('events[%d].handle.generation'%j)==h[1]]
        check(len(events)==1,'real request not published');j=events[0]
        check(v('events[%d].owner_epoch'%j)==dd(i,'epoch') and v('events[%d].reason'%j)==1 and v('events[%d].deadline'%j)==deadline and v('events[%d].request_id'%j)==req,'request identity/epoch/deadline/reason')
        print('REQUEST id=%d epoch=%d handle=%r request=%d now=%d deadline=%d'%(i,dd(i,'epoch'),h,req,v('ticks'),deadline))
    else: check(req==0 and deadline==0,'failed trigger retained outputs')
    return req,deadline

def reclaimed(h,pa):
    check(v('resources[%d].state'%(h[0]-1))==0 and v('resources[%d].references'%(h[0]-1))==0 and dd(1,'resource_count')==0,'RAM reclaim accounting')
    check(pt(1,0x09000000)==0 and pt(1,0x09001000)==0 and dd(1,'space.dynamic_table')==0,'actual PTE/PDE aliases survived')
    check(v('states[%d]'%(pa>>12))==2 and not any(words(pa,1024)),'RAM not free/zero')
    check(not any(v('events[%d].active'%j) and v('events[%d].owner_id'%j)==1 for j in range(8)),'request survived completion')
    print('RECLAIM handle=%r PA=%#x PTE=0 state=PAGE_FREE zero_words=1024 acknowledged=%d forced=%d cancelled=%d'%(h,pa,v('acknowledged'),v('forced'),v('cancelled')))

def background():
    for i in (3,4): command(i,0)
    ram_idle(1)
    for i in (3,4): checkpoint(i,1)
    ram_idle(1)
    check(dd(1,'resource_count')==1,'external LibOS did not acquire RAM')
    check(all(dd(i,'resource_count')==4 and dd(i,'epoch')==1 for i in (3,4)),'background worker quota')

trace=[];trace_last=None
class Protocol(gdb.Breakpoint):
    def stop(self):
        global trace_last
        try:
            if v('current_domain->id')!=1: return False
            frame=words(words(v('$esp')+4,1)[0],15)
            ordinal=dd(1,'syscalls');key=(ordinal,frame)
            if trace_last and ordinal==trace_last[0]:
                check(frame==trace_last[1],'duplicate trap changed input frame');return False
            trace_last=key
            check(frame[11]==0x1b and v('$cr3')==dd(1,'space.pages[0]'),'protocol syscall not real protected CPL3 trap')
            if frame[7] in (24,25,26): trace.append((frame[7],frame[4],frame[6],frame[5]))
        except Exception:
            import traceback;gdb.write(traceback.format_exc());gdb.execute('quit 1')
        return False
'''


def case(a,label,files,body,options=''):
    run(a,'phase9-'+label,files,body,'selftest=phase9 '+options)


def abi(a):
    p=BUILD/'phase9/abi-probe.elf';n=symbols(p)
    body=f'''go({n['abi_probe_done']});user(1)
check(ur(1,{n['probe_result']})==(-116&0xffffffff) and ur(1,{n['probe_extra']})==0,'KBD_RELEASE error EDX contract')
print('ABI 1.2 KBD_RELEASE EAX=ESTALE EDX=0')
finished();check(dd(1,'exit_status')==0,'ABI probe failed')
'''
    case(a,'abi-release',[p],body)


def routing(a):
    aa=FIX/'worker-a.elf';bb=FIX/'worker-b.elf'
    body='R={}\n'+RAM_HELPERS+'\n'+HOLD+'''
# Cache target ID=3 at epoch 1, let it EXIT, then reuse it at epoch 2.
cached_id=3;cached_epoch=dd(3,'epoch');old=handle(3)
setk('phase8_control.reuse_id',3);setk('phase8_control.reuse_module',2);command(3,1)
go('domain_pool_reaped');released(3,saved[3])
go('domain_pool_ready');image(3,*ROUTE_IMAGE);command(3,0);inject_handle(3,'attack',old)
checkpoint(3,1);check(dd(3,'epoch')==cached_epoch+1,'target not reused')
request(3,handle(3),4,epoch=cached_epoch,expected=-116)
check(ownership()=={1:4,2:4,3:4,4:4} and not any(v('events[%d].active'%j) for j in range(8)),'old epoch diagnostic affected new occupant')
perform(3,6,3)
# The existing ABI intentionally routes to the CURRENT occupant, not cached_epoch.
h=handle(1);pa=pt(1,0x09000000)&~4095
put(1,'target',cached_id);perform(1,7,4)
check(v('resources[%d].cap.owner_id'%(h[0]-1))==3 and v('resources[%d].cap.owner_epoch'%(h[0]-1))==2,'ID-only transfer did not bind actual new identity')
check(pt(1,0x09000000)==0 and not any(words(pa,1024)),'transfer data/alias leak')
perform(3,8,5);check(w(3,'received_pfn')==pa>>12,'new occupant did not receive the cleared page')
print('TRANSFER syscall=22 cached_target=(3,1) actual_recipient=(3,2); explicit caller routing, no old capability restored')
'''+EXIT
    case(a,'id-only-routing',[aa,bb,aa,bb],body.replace('ROUTE_IMAGE',repr((headers(aa.read_bytes()),aa.read_bytes()))))


def ram_cases(a):
    directory=a.aelthuvor
    ref=directory/'independent-libos/reference.elf';aa=FIX/'worker-a.elf';bb=FIX/'worker-b.elf'
    for policy in ('first','best'):
        app=BUILD/('phase9/ael-'+policy+'/app.elf');names=symbols(app)
        for mode in ('ack','active-busy','force-safe','force-fault','pending-exit','pending-fault','release','late-ack'):
            body='R='+repr(names)+'\n'+RAM_HELPERS+'\nbackground()\nh,pa=ram_owner()\nrw("index",h[0]);rw("generation",h[1]);rw("pfn",pa>>12)\n'
            if mode.startswith('force'): body+='ram_op(11,8)\ncheck(pt(1,0x09001000)&~4095==pa,"legal second alias not mapped")\n'
            if mode=='ack': body+='ram_op(1,2)\ncheck(dd(1,"resource_count")==1,"free incorrectly trimmed before revoke")\n'
            body+='req,deadline=request(1,h,'+('3' if mode.startswith('force') or mode=='late-ack' else '1000')+')\nrw("request",req)\n'
            if mode=='ack':
                body+='''protocol=Protocol('*syscall_dispatch',type=gdb.BP_HARDWARE_BREAKPOINT,internal=True)
rw('command',2);go('revoke_completed')
reclaimed(h,pa)
check(v('acknowledged')==1 and v('forced')==0 and v('ticks')<deadline,'ACK not before deadline')
check(any(x==(24,0,0,0) for x in trace),'external LibOS did not POLL')
check({x[2] for x in trace if x[0]==25 and x[1]==req}=={0,1,2,3},'external LibOS did not INFO all fields')
check((26,h[0],h[1],req) in trace,'external LibOS did not ACK correct identity')
protocol.delete()
# Wait on the recurring private idle point so IRQ timing cannot skip an app checkpoint.
ram_idle(3);check(rr('result')==0,'Ael SDK ACK did not complete safely')
print('EXTERNAL LIBOS protocol trace=',trace)
ram_op(4,4);ram_op(10,7);ram_op(6,6);check(rr('result')==(-116&0xffffffff),'old ACK resurrected completed request')
rw('command',7)
'''
                status=0;fault=0
            elif mode=='active-busy':
                body+='''rw('command',2)
''';status=96;fault=0
            elif mode in ('force-safe','force-fault','late-ack'):
                body+='''go('revoke_completed');reclaimed(h,pa)
check(v('forced')==1 and v('acknowledged')==0 and v('ticks')>=deadline,'deadline not actually reached')
ram_op(4,4);ram_op(10,7)
'''
                if mode=='force-safe': body+="rw('command',2)\n";status=96;fault=0
                elif mode=='force-fault':body+="rw('command',3)\n";status=0;fault=14
                else: body+="ram_op(6,6);check(rr('result')==(-116&0xffffffff),'late ACK succeeded')\nrw('command',9)\n";status=0;fault=0
            elif mode=='release':
                body+="ram_op(5,5);check(rr('result')==0,'pending RELEASE refused')\nreclaimed(h,pa)\ncheck(v('cancelled')==1,'release did not cancel')\nram_op(6,6);check(rr('result')==(-116&0xffffffff),'ACK after release accepted')\nrw('command',9)\n";status=0;fault=0
            else:
                body+="rw('command',"+('8' if mode=='pending-fault' else '9')+")\n";status=0;fault=6 if mode=='pending-fault' else 0
            body+='''for i in (3,4): command(i,1)
finished()
check(v('phase9_control.submit')==0,'diagnostic request left pending')
check(dd(1,'last_state')==STATE and dd(1,'exit_status')==STATUS,'wrong Ael terminal behavior')
check(dd(2,'last_state')==3 and dd(2,'exit_status')==0,'independent LibOS stopped by Ael failure')
for i in (3,4): check(dd(i,'last_state')==3 and dd(i,'exit_status')==0,'worker stopped by Ael failure')
'''.replace('STATE','4' if fault else '3').replace('STATUS',str(status))
            if fault:
                body+=f"check(dd(1,'fault_vector')=={fault},'wrong external fault vector')\n"
                if fault==14: body+=f"check(dd(1,'fault_error')==4 and dd(1,'fault_eip')=={names['ram_fault_read']},'old pointer did not trigger actual expected #PF')\n"
            if mode.startswith('pending-'):body+="check(v('cancelled')==1 and v('forced')==0,'terminal cleanup did not cancel pending request')\n"
            case(a,'ram-'+policy+'-'+mode,[app,ref,aa,bb],body)


def diagnostics(a):
    aa=FIX/'worker-a.elf';bb=FIX/'worker-b.elf'
    # Reuse the same real resource owner and assert failed requests never mutate it.
    body='R={}\n'+RAM_HELPERS+'\n'+HOLD+'''
h=handle(1);initial=ownership()
request(1,h,4,epoch=0,expected=-116)
request(9,h,4,epoch=1,expected=-3)
request(1,(h[0],h[1]+1),4,expected=-116)
request(1,handle(2),4,expected=-1)
request(1,h,0,expected=-75)
check(ownership()==initial and not any(v('events[%d].active'%j) for j in range(8)),'failed trigger changed ownership/events')
print('TRUSTED DIAGNOSTIC rejects stale epoch, missing domain, stale/foreign handle, zero deadline without mutation')
'''+EXIT
    case(a,'diagnostic-denials',[aa,bb,aa,bb],body)
    body=HOLD+'''
setk('phase9_control.submit',1);setk('phase9_control.id',1);setk('phase9_control.epoch',1)
start=v('irq_count');go('timer_irq_capture','irq_count >= %d'%(start+2))
check(v('phase9_control.completed')==0 and not any(v('events[%d].active'%j) for j in range(8)),'diagnostic ran without explicit boot flag')
check(ownership()=={1:4,2:4,3:4,4:4},'disabled diagnostic mutated authority')
setk('phase9_control.submit',0)
'''+EXIT
    run(a,'phase9-diagnostic-disabled',[aa,bb,aa,bb],body)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--aelthuvor',type=Path,help='prebuilt independent Phase 4 build directory')
    p.add_argument('--qemu',default='qemu-system-i386');p.add_argument('--gdb',default='gdb');a=p.parse_args()
    inputs=[BUILD/'liberums.elf',BUILD/'phase9/abi-probe.elf',FIX/'worker-a.elf',FIX/'worker-b.elf']
    if a.aelthuvor:
        inputs += [a.aelthuvor/'independent-libos/reference.elf']
        inputs += [BUILD/('phase9/ael-'+policy+'/app.elf') for policy in ('first','best')]
    before={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    abi(a);routing(a);diagnostics(a)
    stress(a,FIX/'worker-a.elf',FIX/'worker-b.elf',rounds=128)
    if a.aelthuvor: ram_cases(a)
    require(before=={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},'kernel or independent input ELF changed during audit')
    report=dict(guests=len(RESULTS),gdb_audits=len(RESULTS),cases=RESULTS,ael_sdk_e2e=bool(a.aelthuvor),input_sha256=before)
    (BUILD/('phase9-results-e2e.json' if a.aelthuvor else 'phase9-results-core.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: Phase 9',len(RESULTS),'actual QEMU/GDB guests; external SDK RAM E2E=',bool(a.aelthuvor))
if __name__=='__main__':main()
