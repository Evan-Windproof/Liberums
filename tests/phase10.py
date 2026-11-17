#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Outside consumers + real QEMU/GDB of the public educational kit, no Ael needed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
from phase1 import ROOT,BUILD,ELF,require,wait_for
from phase2 import connect_and_run
from phase3 import HELPERS
from phase5 import headers,symbols
from phase8 import COMMON
from phase72 import vm,injector,INJECT,SCANS
from phase9 import RAM_HELPERS
RESULTS=[]
INPUTS={}
BUILDS=[]

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def freeze(path): INPUTS[str(path)]=sha(path)
def build_project(path,*args):
    result=subprocess.run(['make','-C',str(path),*args],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    with (BUILD/'phase10-consumer-build.log').open('a') as log: log.write(result.stdout)
    require(result.returncode==0,'outside project build failed: '+result.stdout)
    for dep in (path/'build').glob('*.d'):
        rule=dep.read_text().replace('\\\n','').splitlines()[0]
        for name in shlex.split(rule.split(':',1)[1]):
            target=(path/name).resolve()
            require(target.is_relative_to(path.resolve()),'consumer read outside its exported project: '+str(target))
    image=path/'build/libos.elf'
    require(not subprocess.check_output(['nm','-u',str(image)]).strip(),'undefined freestanding symbols')
    table=(path/'build/libos.map').read_text()
    require('libael' not in table and 'ael_' not in subprocess.check_output(['nm',str(image)],text=True),'hidden Ael dependency')
    BUILDS.append(dict(command=['make','-C',str(path),*args],elf_sha256=sha(image),
        source_sha256={str(p.relative_to(path)):sha(p) for p in path.rglob('*') if p.is_file() and 'build' not in p.relative_to(path).parts},
        dependencies={p.name:p.read_text() for p in (path/'build').glob('*.d')},link_map=table))
    return image

def create_consumer(path,example):
    subprocess.run(['python3',str(ROOT/'scripts/new_libos.py'),'--sdk',str(BUILD/'kernel-sdk'),'--output',str(path),'--example',example],check=True,stdout=subprocess.DEVNULL)
    for source in path.rglob('*'):
        if source.suffix in ('.c','.h','.S','.mk') or source.name=='Makefile':
            data=source.read_text()
            require('aelthuvor/' not in data and 'kernel/' not in data and '/workspace/' not in data,'private/cloud build dependency')
    return build_project(path)

def run(a,label,files,body,options=''):
    before=sha(ELF);names=[symbols(p) for p in files]
    commands=HELPERS+''.join('add-symbol-file "'+str(p)+'"\n' for p in files)+INJECT
    commands+='python\n'+COMMON.replace('SYMBOLS','{}')
    commands+=f'N={names!r}\nused=ready({[0]*len(files)!r},{[(headers(p.read_bytes()),p.read_bytes()) for p in files]!r})\n'
    commands+=body+f'\nprint("PASS: educational {label}")\nend\ndetach\nquit\n'
    with vm(a.qemu,'multi-external '+options,'phase10-'+label,','.join(map(str,files))) as (p,log,port,endpoint):
        with injector(endpoint) as control:
            out=connect_and_run(p,log,port,a.gdb,commands.replace('CONTROL',repr(control)),'phase10-'+label)
        wait_for(p,log,lambda t:'Phase 8: multi external execution completed' in t,'educational lifecycle')
        require('PANIC' not in log.read_text(),'guest panic')
    require(before==sha(ELF),'consumer testing rebuilt/replaced the kernel')
    require('PASS: educational '+label in out,'audit incomplete')
    RESULTS.append(dict(label=label,kind='QEMU/GDB',modules=len(files),elf_sha256=[sha(p) for p in files]))
    print('PASS: educational',label)

AUDIT=r'''
seen={i:0 for i in used};ram={};tables={};irq=set();last={i:None for i in used};bps=[]
def observe(i,kind):
    if v('current_domain')==0 or v('current_domain->id')!=i:return
    user(i)
    stage=ur(i,N[i-1]['example_stage']);seq=ur(i,N[i-1]['example_sequence']);value=ur(i,N[i-1]['example_value'])
    if seq==seen[i]: check((stage,value)==last[i],'duplicate user stop changed data');return
    check(seq==seen[i]+1,'missed actual observation');seen[i]=seq;last[i]=(stage,value)
    info=words(physical(i,N[i-1]['libos_info']),4)
    check(info==(0x10002,119,4,4),'runtime ABI/capacity probe')
    if kind=='minimal':check(stage==1 and value==0x10002,'minimal runtime never reached app');return
    if stage==1:
        check(dd(i,'resource_count')==1 and value==0x0900f000,'pool ownership/VA')
        pa=pt(i,value)&~4095;ram[i]=pa;tables[i]=dd(i,'space.dynamic_table')
        check(pt(i,value)&7==7,'pool PTE rights')
        actual=bytes(gdb.selected_inferior().read_memory(pa,4096))
        size=SIZES[i]
        check(actual==bytes((j//size)^(j%size)^0x5a for j in range(4096)),'bitmap pool data/strategy')
        slots=[j for j in range(16) if v('resources[%d].state'%j)==1 and v('resources[%d].cap.owner_id'%j)==i]
        check(len(slots)==1,'pool resource count')
        base='resources[%d].'%slots[0]
        check(v(base+'cap.owner_epoch')==dd(i,'epoch') and v(base+'cap.rights')==47 and v(base+'references')==1,'owner epoch/rights/binding')
        if len(ram)==sum(k=='pool' for k in KINDS.values()):
            check(len(set(ram.values()))==len(ram),'unapproved shared physical RAM')
            for peer in ram: check(dd(peer,'resource_count')==1,'pool did not coexist while live')
            for peer in ram:
                if FAULT and peer==1:uw(peer,N[peer-1]['lesson_command'],4)
                uw(peer,N[peer-1]['lesson_gate'],1)
    elif stage==2:
        check(dd(i,'resource_count')==1,'pool released before explicit close')
    elif stage==3:
        check(dd(i,'resource_count')==0 and dd(i,'space.dynamic_table')==0,'pool did not unmap/release')
        check(not any(words(ram[i],1024)) and v('states[%d]'%(ram[i]>>12))==2,'resource not zero/freed')
    else:check(False,'unexpected pool observation')
def preempt():
    i=v('current_domain->id')
    if KINDS[i]!='pool':return
    frame=words(words(v('$esp')+4,1)[0],15)
    check(frame[8]==32 and frame[11]==0x1b and frame[12]&0x3200==0x200,'not actual protected PIT preemption')
    bottom=v('&domains[%d].kernel_stack'%(i-1));check(bottom<=words(v('$esp')+4,1)[0]<bottom+8192-59,'IRQ frame used wrong stack')
    irq.add(i)
class Audit(gdb.Breakpoint):
    def __init__(self,site,fn):super().__init__('*'+str(site),type=gdb.BP_HARDWARE_BREAKPOINT,internal=True);self.fn=fn
    def stop(self):
        try:self.fn()
        except Exception:
            import traceback;gdb.write(traceback.format_exc());gdb.execute('quit 1')
        return False
for i,kind in KINDS.items():
    if kind=='pool':uw(i,N[i-1]['lesson_command'],5)
    bps.append(Audit(N[i-1]['example_observed'],lambda i=i,kind=kind:observe(i,kind)))
if 'pool' in KINDS.values():bps.append(Audit('domain_preempt',preempt))
finished()
for i,kind in KINDS.items():
    check(dd(i,'last_state')==(4 if FAULT and i==1 else 3),'wrong terminal kind')
    if FAULT and i==1:check(dd(i,'fault_vector')==6,'wrong isolation fault')
    else:
        check(dd(i,'exit_status')==0,'example self-check failed')
        check(seen[i]==(3 if kind=='pool' else 1),'missing example completion')
        if kind=='pool':check(i in irq and dd(i,'preemptions')>0,'pool never actually preempted')
for pa in list(ram.values())+list(tables.values()):check(v('states[%d]'%(pa>>12))==2 and not any(words(pa,1024)),'resource/table leak')
print('POLICIES sizes=',SIZES,'actual CR3s=',[hex(saved[i][0]) for i in used],'PIT domains=',irq)
'''

def normal(a,label,files,kinds,sizes=None,fault=False):
    body=f'KINDS={dict(enumerate(kinds,1))!r}\nSIZES={sizes or {}!r}\nFAULT={fault!r}\n'+AUDIT
    run(a,label,files,body)


def revocation(a,pool,peer):
    for mode in ('ack','safe','fault'):
        cmd={'ack':1,'safe':2,'fault':3}[mode]
        body='R={}\n'+RAM_HELPERS+f'''\nuw(1,N[0]['lesson_command'],{cmd});uw(2,N[1]['lesson_command'],5)
va=N[0]['example_stage']
go(N[0]['example_observed'],'current_domain->id == 1 && *(unsigned int*)%d == {2 if mode=='ack' else 1}'%va)
user(1)
pa=pt(1,0x0900f000)&~4095
slot=next(j for j in range(16) if v('resources[%d].state'%j)==1 and v('resources[%d].cap.owner_id'%j)==1)
h=(slot+1,v('resources[%d].cap.generation'%slot))
req,deadline=request(1,h,{1000 if mode=='ack' else 3})
'''
        if mode=='ack':body+="protocol=Protocol('*syscall_dispatch',type=gdb.BP_HARDWARE_BREAKPOINT,internal=True)\nuw(1,N[0]['lesson_gate'],1)\n"
        body+="""go('revoke_completed')
check(pt(1,0x0900f000)==0 and dd(1,'resource_count')==0,'resource mapping survived revoke')
check(v('states[%d]'%(pa>>12))==2 and not any(words(pa,1024)),'reclaimed RAM not zero/free')
check(dd(2,'resource_count')==1 and dd(2,'state')==1,'background pool not live at completion')
"""
        if mode=='ack':
            body+="""check(v('acknowledged')==1 and v('forced')==0 and v('ticks')<deadline,'ACK deadline')
check((26,h[0],h[1],req) in trace and {x[2] for x in trace if x[0]==25}=={0,1,2,3},'independent LibOS did not use actual INFO/ACK')
print('TEACHING PROTOCOL actual syscall trace=',trace);protocol.delete()
uw(2,N[1]['lesson_gate'],1)
"""
        else:
            body+="check(v('forced')==1 and v('ticks')>=deadline,'not real timeout')\nuw(1,N[0]['lesson_gate'],1)\n"
            site='domain_fault' if mode=='fault' else 'domain_exit'
            body+=f"go('{site}','current_domain->id == 1')\ncheck(dd(2,'state')==1 and dd(2,'resource_count')==1,'peer stopped before owner failure')\nuw(2,N[1]['lesson_gate'],1)\n"
        body+='finished()\n'
        body+="check(dd(2,'last_state')==3 and dd(2,'exit_status')==0,'failure affected independent peer')\n"
        if mode=='fault':body+="check(dd(1,'last_state')==4 and dd(1,'fault_vector')==14 and dd(1,'fault_error')==4 and dd(1,'fault_address')==0x0900f000,'old pointer did not really fault')\n"
        else:body+=f"check(dd(1,'last_state')==3 and dd(1,'exit_status')=={96 if mode=='safe' else 0},'wrong safe termination')\n"
        run(a,'revoke-'+mode,[pool,peer],body,'selftest=phase9')


def console(a,console,minimal):
    body="finished()\ncheck(dd(1,'exit_status')==77 and dd(2,'exit_status')==0,'INFO was mistaken for authority')\n"
    run(a,'console-no-grant',[console,minimal],body,'multi-vga=2 multi-keyboard=2')
    actions=[]
    for text,wanted in [('help','help clear about status exit'),('status','Ring 3 | ABI 1.2'),('exit','Console finished')]:
        for key in text:actions.append((key,[SCANS[key],SCANS[key]|0x80],None))
        actions.append(('ret',[0x1c] if text=='exit' else [0x1c,0x9c],wanted))
    body=f'''sequence=0;events=0
class ConsoleAudit(gdb.Breakpoint):
    def stop(self):
        global sequence,events
        try:
            if v('current_domain->id')!=1:return False
            user(1);seq=ur(1,N[0]['example_sequence'])
            if seq==sequence:return False
            check(seq==sequence+1,'missed console observation');sequence=seq
            stage=ur(1,N[0]['example_stage']);raw=ur(1,N[0]['example_value'])
            check(pt(1,0x09400000)&0xfffff09f==0xb801f,'VGA PTE/cache/rights')
            if stage==1:return True
            check(stage==2,'unexpected console stage')
            events+=1
            return True
        except Exception:
            import traceback;gdb.write(traceback.format_exc());gdb.execute('quit 1')
observer=ConsoleAudit('*'+str(N[0]['example_observed']),type=gdb.BP_HARDWARE_BREAKPOINT,internal=True)
# go() owns only its own breakpoint; disable its prepared observer before continuing.
if live_bp is not None:live_bp.delete();live_bp=None
gdb.execute('continue');check(sequence==1,'console never initialized')
for key,raw,wanted in {actions!r}:
    inject([key])
    for byte in raw:
        old=sequence;gdb.execute('continue')
        check(sequence==old+1 and ur(1,N[0]['example_value'])==byte,'real PS/2 byte mismatch')
    if wanted:
        fb=bytes(gdb.selected_inferior().read_memory(0xb8000,4000))
        text='\\n'.join(fb[r*160:(r+1)*160:2].decode('ascii',errors='replace') for r in range(25))
        check(wanted in text,'console did not draw '+wanted)
        if wanted.startswith('Ring 3'):query('screendump',{{'filename':{str(BUILD/'phase10-console.png')!r},'format':'png'}})
observer.delete();finished()
check(dd(1,'exit_status')==0 and v('keyboard_irqs')>0,'console failed to release devices')
print('CONSOLE real PS/2 bytes=',events,'IRQ1=',v('keyboard_irqs'),'EOI=',v('keyboard_eois'))
'''
    run(a,'console',[console],body,'multi-vga=1 multi-keyboard=1')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-i386');p.add_argument('--gdb',default='gdb')
    a=p.parse_args();before=sha(ELF);mtime=ELF.stat().st_mtime_ns
    (BUILD/'phase10-consumer-build.log').write_text('')
    examples=BUILD/'examples';minimal=examples/'minimal/libos.elf';pool=examples/'memory-policy/libos.elf';con=examples/'console/libos.elf'
    for image in (minimal,pool,con):freeze(image)
    normal(a,'minimal',[minimal],['minimal'])
    normal(a,'bitmap',[pool],['pool'],{1:64})
    with tempfile.TemporaryDirectory(prefix='liberums-student-') as tmp:
        project=Path(tmp)/'hello';first=create_consumer(project,'minimal');first_hash=sha(first)
        source=project/'main.c';source.write_text(source.read_text().replace('Hello from my independent LibOS','Edited outside consumer'))
        edited=build_project(project);require(sha(edited)!=first_hash,'edited user ELF unchanged')
        normal(a,'outside-edited',[edited],['minimal'])
        require('Edited outside consumer' in (BUILD/'phase10-outside-edited-serial.log').read_text(),'edited message never ran')
        policy=Path(tmp)/'policy';other=create_consumer(policy,'memory-policy')
        other=build_project(policy,'CFLAGS=-DOBJECT_BYTES=128')
        normal(a,'two-policies',[pool,other],['pool','pool'],{1:64,2:128})
        normal(a,'fault-isolation',[pool,other],['pool','pool'],{1:64,2:128},fault=True)
        revocation(a,pool,other)
        # Switch runtime profile in the SAME output directory; no stale .o allowed.
        legacy=build_project(project,'PROFILE=coop','MIN_MINOR=1')
        from phase73 import lifecycle
        lifecycle(a.qemu,a.gdb,'phase10-abi11',legacy,'external')
        RESULTS.append(dict(label='ABI1.1-cooperative',kind='QEMU/GDB',modules=1,lifetimes=2,elf_sha256=[sha(legacy)]))
        dependencies={str(d.relative_to(Path(tmp))):d.read_text() for d in Path(tmp).rglob('*.d')}
        maps={str(d.relative_to(Path(tmp))):d.read_text() for d in Path(tmp).rglob('*.map')}
        (BUILD/'phase10-consumer-dependencies.json').write_text(json.dumps(dict(builds=BUILDS,dependencies=dependencies,link_maps=maps),indent=2)+'\n')
    console(a,con,minimal)
    require(sha(ELF)==before and ELF.stat().st_mtime_ns==mtime,'outside consumer relinked kernel')
    require(all(sha(path)==digest for path,digest in INPUTS.items()),'base example ELF changed during audit')
    report=dict(guests=len(RESULTS),kernel_sha256=before,kernel_unchanged=True,examples_sha256=INPUTS,cases=RESULTS)
    (BUILD/'phase10-results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: Phase 10',len(RESULTS),'real QEMU/GDB guests; outside SDK consumers; unchanged kernel')
if __name__=='__main__':main()
