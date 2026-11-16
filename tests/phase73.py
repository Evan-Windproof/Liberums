#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""External ELF/profile/ABI independence, actual PS/2 -> Ring3 -> VGA and rollback."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
from phase1 import ROOT,BUILD,ELF,require,wait_for
from phase2 import connect_and_run
from phase3 import HELPERS
from phase5 import GDB_HELPERS,headers,symbols
from phase72 import vm,injector,INJECT,actions

EXTERNAL=BUILD/'external'
RESULTS=[]

def build():
    before=hashlib.sha256(ELF.read_bytes()).hexdigest()
    before_mtime=ELF.stat().st_mtime_ns
    subprocess.run(['make','external-console'],cwd=ROOT,check=True)
    subprocess.run(['make','-C',str(ROOT/'user/external_console'),'OUT='+str(EXTERNAL),'fixtures'],check=True)
    # Build from copied user sources and ONLY a copied public SDK, with no kernel headers/objects.
    isolated=BUILD/'phase73-standalone'
    shutil.copytree(ROOT/'user/console',isolated/'console',dirs_exist_ok=True)
    shutil.copytree(ROOT/'user/external_console',isolated/'external_console',dirs_exist_ok=True)
    shutil.copytree(ROOT/'include/abi',isolated/'sdk/abi',dirs_exist_ok=True)
    source=isolated/'external_console/main.c'
    source.write_text(source.read_text().replace('External ELF Interactive Console','Rebuilt Without Kernel Relink'))
    editor=isolated/'console/core.c'
    editor.write_text(editor.read_text().replace('help clear about status', 'help clear about status [independent]'))
    subprocess.run(['make','-C',str(isolated/'external_console'),'SDK='+str(isolated/'sdk'),'OUT='+str(isolated/'out')],check=True)
    after=hashlib.sha256(ELF.read_bytes()).hexdigest()
    require(before==after and before_mtime==ELF.stat().st_mtime_ns,'independent application build relinked kernel ELF')
    require(b'Rebuilt Without Kernel Relink' in (isolated/'out/console.elf').read_bytes(),'edited source not built')
    require(not subprocess.check_output(['nm','-u',str(EXTERNAL/'console.elf')]).strip(),'unresolved external dependencies')
    print('PASS: independent public-SDK build + edited application; kernel SHA256 unchanged '+after)
    return isolated/'out/console.elf'

CLEAN = '''
check(v('vga_device.grant_id')==0 and v('vga_device.active')==0,'VGA grant leaked')
check(v('keyboard.grant_id')==0 and v('keyboard.active')==0 and v('keyboard.enabled')==0,'keyboard grant/IRQ leaked')
check(v('keyboard.count')==0,'keystrokes survived cleanup')
check(d(0,'space.device_table')==0,'device table survived')
'''

def lifecycle(qemu,gdb,label,file,append,mode=None,expected=0,fault=0,extra=(),exit_status=0):
    blob=file.read_bytes() if file else b''
    seg=headers(blob) if expected==0 else []
    names=symbols(file) if file and expected==0 else {}
    commands=HELPERS+GDB_HELPERS
    for round_number in range(2):
        commands+='delete breakpoints\nhbreak *external_prepared\ncontinue\npython\n'
        commands+=f"check(v('external_load_result')==({expected}&0xffffffff),'unexpected launch result')\n"
        if expected==0:
            commands+=f'inspect_external({seg!r},{blob!r})\n'
            if label.startswith('ael') or label=='untrusted-note' or label=='untrusted-filename':commands+=CLEAN
            if mode is not None:
                va=names['probe_mode'];old=names['probe_old_generation']
                commands+=f"gdb.selected_inferior().write_memory((elf_pt({va})&~4095)+({va}&4095),struct.pack('<I',{mode}))\n"
                commands+=f"gdb.selected_inferior().write_memory((elf_pt({old})&~4095)+({old}&4095),struct.pack('<I',v('keyboard.cap.generation')))\n"
        else:
            commands+="check(d(0,'space.live')==0 and d(0,'space.count')==0,'failed program left a space')\ncheck(v('external_free_before_load')==v('external_free_after_load'),'partial launch rollback leaked RAM')\n"+CLEAN
        commands+='end\n'
        if expected==0:
            entry=names['layout_second'] if 'layout_second' in names else names['_start']
            commands+=f'delete breakpoints\nhbreak *{entry:#x}\ncondition $bpnum current_domain->id == 1\ncontinue\npython\ninspect_external_user()\nend\n'
        commands+='delete breakpoints\nhbreak *external_reaped\ncontinue\npython\n'
        if expected==0:
            commands+='inspect_external_reaped()\n'
            if fault:
                commands+=f"check(d(0,'state')==4 and d(0,'fault_vector')=={fault} and d(0,'fault_error')==7,'readonly device write not protected')\n"
            else: commands+=f"check(d(0,'state')==3 and d(0,'exit_status')=={exit_status},'external assertions failed: '+str(d(0,'exit_status')))\n"
        else:
            commands+="check(d(0,'space.count')==0 and d(1,'space.count')==0,'failed lifetime leaked space')\n"
            commands+="check(bytes(gdb.selected_inferior().read_memory(v('&states'),16384)).count(2)==v('external_free_before'),'failure lifetime leaked RAM')\n"
        commands+=CLEAN+f"check(v('external_completed_rounds')=={round_number+1},'lifetime missing')\nend\n"
    commands+="detach\nquit\n"
    with vm(qemu,append+' external-repeat','phase73-'+label,file,extra) as (p,log,port,_):
        connect_and_run(p,log,port,gdb,commands,'phase73-'+label)
        wait_for(p,log,lambda s:'Phase 5: external execution completed' in s,'external lifetime complete')
        require('PANIC' not in log.read_text(),'launch panicked')
    RESULTS.append(dict(label=label,completed_lifetimes=2,kind='terminal',expected_result=expected))
    print('PASS: external '+label+' two actual ELF/domain/device lifecycles')

AUDIT = r'''python
label=LABEL
request=REQUEST
rounds=ROUNDS
seen=dict(irq=0,returns=0,events=0,reaped=0)
raw_frame=None
index=raw_index=0
expected=ACTIONS
active=[]
persistent_done=False
last_sequence=0
last_observation=None
def c(field): return v('console_state.'+field)
def pte(va):
    directory=words(d(0,'space.pages[0]'),1024)
    return words((directory[va>>22]&~4095)+((va>>12)&1023)*4,1)[0] if directory[va>>22]&1 else 0
def framebuffer():
    b=bytes(gdb.selected_inferior().read_memory(0xb8000,4000))
    return '\n'.join(b[i*160:(i+1)*160:2].decode().rstrip() for i in range(25))
def post_request():
    if request in (3,4):
        gdb.selected_inferior().write_memory(v('&diagnostic_revoke'),struct.pack('<I',4 if request==3 else 1))
    gdb.selected_inferior().write_memory(v('&console_test_request'),struct.pack('<I',request))
def send_next():
    global raw_index
    if index<len(expected):
        raw_index=0;inject([expected[index]['key']])
    elif request==0:
        global persistent_done
        persistent_done=True
    else: post_request()
def checkpoint():
    global index,raw_index,last_sequence,last_observation
    check(v('$cs')==0x1b and v('$ss')==0x23 and v('$cr3')==d(0,'space.pages[0]') and 0x083fc000<=v('$esp')<0x08400000,'external CPL3/CR3/stack')
    check(v('$eflags')&0x3200==0x200,'external Console profile lacks IF or has IOPL')
    stage,raw,sequence=words(v('&console_observation'),3)
    if sequence==last_sequence:
        check((stage,raw)==last_observation,'observation changed without a new poll')
        return
    check(sequence==last_sequence+1,'missed application observation')
    last_sequence=sequence;last_observation=(stage,raw)
    check(pte(0x09400000)&0xfffff09f==0xb801f,'VGA PA/permissions/cache mismatch')
    check(v('keyboard.cap.owner_id')==1 and v('keyboard.cap.owner_epoch')==d(0,'epoch'),'keyboard capability identity wrong')
    if stage==1:
        index=raw_index=0
        check(c('length')==0 and v('keyboard.count')==0,'stale input inherited')
        if label=='amber':check('Independent Amber Console' in framebuffer(),'alternate program did not draw its own welcome')
        if label=='rebuilt':check('Rebuilt Without Kernel Relink' in framebuffer(),'edited program not executed')
        check(bytes(gdb.selected_inferior().read_memory(0xb8001,1))[0]==(c('background')<<4 | c('foreground')),'external theme not drawn')
        send_next();return
    check(stage==2,'unexpected application checkpoint')
    action=expected[index]
    check(raw==action['raw'][raw_index],'PS/2 byte actual=%#x expected=%#x action=%d raw_index=%d round=%d'%(raw,action['raw'][raw_index],index,raw_index,seen['reaped']))
    raw_index+=1;seen['events']+=1
    if raw_index!=len(action['raw']):return
    line=bytes(gdb.selected_inferior().read_memory(v('&console_state.line'),64)).split(b'\0')[0].decode()
    check(line==action['line'] and c('column')==len(line)+2,'external editing/cursor mismatch')
    if 'contains' in action:
        check(action['contains'] in framebuffer(),'external command output absent')
        if label=='rebuilt' and action['contains']=='help clear about status':check('help clear about status [independent]' in framebuffer(),'edited shared editor was not executed')
    if action.get('clear'): check(framebuffer().strip()=='>','external clear did not clear actual VGA')
    index+=1;send_next()
def irq_entry():
    global raw_frame
    sp=v('$esp')+4
    ordinal=v('keyboard_irqs')
    cpu=words(sp,3);cpu=words(sp,5) if cpu[1]&3 else cpu
    snapshot=(sp,cpu,tuple(v('$'+r) for r in ('eax','ebx','ecx','edx','esi','edi','ebp','ds','es','fs','gs')),v('$cr3'))
    if ordinal==seen['irq']-1:
        check(snapshot==raw_frame,'duplicate debugger stop changed raw IRQ frame')
        return
    check(ordinal==seen['irq'],'missed actual IRQ1 dispatch')
    raw_frame=snapshot
    check(v('$cs')==8 and v('$eflags')&0x200==0,'unprotected IRQ1')
    if cpu[1]&3: check(sp==v('tss.esp0')-20,'IRQ1 missed TSS stack')
    seen['irq']+=1
def irq_return():
    sp,cpu,regs,cr3=raw_frame
    check(v('$esp')==sp and words(sp,len(cpu))==cpu,'IRQ1 IRET frame changed')
    check(tuple(v('$'+r) for r in ('eax','ebx','ecx','edx','esi','edi','ebp','ds','es','fs','gs'))==regs and v('$cr3')==cr3,'IRQ1 corrupted GP/segments/CR3')
    ordinal=v('keyboard_irqs')
    check(ordinal==v('keyboard_eois'),'IRQ1 EOI missing')
    if ordinal==seen['returns']:return
    check(ordinal==seen['returns']+1,'missed actual IRQ1 return')
    seen['returns']+=1
def prepared():
    global active,last_sequence,last_observation
    last_sequence=0;last_observation=None
    inspect_external(SEGMENTS,BLOB)
    check(v('vga_device.grant_id')==1 and v('keyboard.grant_id')==1,'trusted launch did not grant')
    check(v('vga_device.grant_epoch')==d(0,'epoch') and v('keyboard.grant_epoch')==d(0,'epoch'),'stale grant epoch')
    check(v('vga_device.active')==0 and v('keyboard.active')==0,'loader acquired user handles')

def reaped():
    global active
    inspect_external_reaped()
    check(v('vga_device.grant_id')==0 and v('keyboard.grant_id')==0 and v('keyboard.enabled')==0,'device authority survived terminal state')
    check(d(0,'space.device_table')==0 and v('keyboard.count')==0,'device table/queue leak')
    check(v('irq_count')==v('eoi_count') and v('ticks')>0,'IRQ0 damaged')
    if request in (2,4):
        check(d(0,'state')==4 and d(0,'fault_vector')==(6 if request==2 else 14),'expected fault missing')
        if request==4: check(d(0,'fault_address')==0x09400000 and d(0,'fault_error')==6,'revoked mapping still accessible')
    else: check(d(0,'state')==3 and d(0,'exit_status')==0,'application failed: '+str(d(0,'exit_status')))
    seen['reaped']+=1
    check(seen['irq']==seen['returns']>0,'IRQ1 audit not complete')
    print('PASS: external actual CPL3/ELF/PTE/IRQ1/IRET/input/cleanup',label,seen)
class Audit(gdb.Breakpoint):
    def __init__(self,loc,action):
        super().__init__('*'+loc,type=gdb.BP_HARDWARE_BREAKPOINT,internal=True);self.action=action
    def stop(self):
        try:
            self.action()
            return persistent_done
        except Exception:
            import traceback
            gdb.write(traceback.format_exc());gdb.execute('quit 1')
        return False
end
'''

def console(qemu,gdb,label,file,request=1,rounds=2):
    blob=file.read_bytes();names=symbols(file)
    with vm(qemu,'external-console'+(' selftest=device-revoke' if request in (3,4) else '')+(' external-repeat' if rounds==2 else ''),'phase73-'+label,file) as (p,log,port,endpoint):
        with injector(endpoint) as control:
            script='add-symbol-file "'+str(file)+'"\n'+HELPERS+GDB_HELPERS+INJECT.replace('CONTROL',repr(control))
            script+='python\nfrom pathlib import Path\nBUILD_PATH=Path('+repr(str(BUILD))+')\nend\n'
            audit=AUDIT.replace('LABEL',repr(label)).replace('REQUEST',str(request)).replace('ROUNDS',str(rounds))
            audit=audit.replace('ACTIONS',repr(actions(request==1))).replace('CHECKPOINT',hex(names['external_console_observed']))
            script+=audit.replace('SEGMENTS',repr(headers(blob))).replace('BLOB',repr(blob))
            for iteration in range(rounds):
                script+='delete breakpoints\nhbreak *external_prepared\ncontinue\npython\nprepared()\nend\ndelete breakpoints\npython\n'
                script+="active=[Audit('keyboard_irq_capture',irq_entry),Audit('keyboard_irq_return',irq_return),Audit('"+hex(names['external_console_observed'])+"',checkpoint)]\n"
                if request!=0: script+="gdb.Breakpoint('*external_reaped',type=gdb.BP_HARDWARE_BREAKPOINT,internal=True)\n"
                script+='end\ncontinue\npython\n'
                if request==0: script+="check(persistent_done and seen['irq']==seen['returns']>0,'persistent input audit incomplete')\nprint('PASS: external actual persistent program without kernel probes',label,seen)\n"
                else: script+='reaped()\n'
                script+='end\n'
            script+="python\nquery('screendump',{'filename':str(BUILD_PATH/('phase73-'+label+'.png')),'format':'png'})\nend\ndetach\nquit\n"
            output=connect_and_run(p,log,port,gdb,script,'phase73-'+label)
            require('PASS: external actual' in output,'external audit did not finish')
            if request!=0: wait_for(p,log,lambda s:'Phase 5: external execution completed' in s,'external complete')
    RESULTS.append(dict(label=label,completed_lifetimes=rounds if request else 0,kind='terminal' if request else 'persistent input verified'))
    print('PASS: external interactive '+label+' '+(str(rounds)+' completed lifecycles' if request else 'persistent input verified'))


def run_targets(qemu):
    import os,signal
    log=BUILD/'phase73-run-target-serial.log'
    with log.open('w') as output:
        process=subprocess.Popen(['make','--no-print-directory','run-external-console','QEMU='+qemu,
            'QEMU_DISPLAY=none','CONSOLE_ELF='+str(EXTERNAL/'amber.elf')],cwd=ROOT,
            stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            wait_for(process,log,lambda t:'External devices: grants established' in t,'actual external launch target')
            require('PANIC' not in log.read_text(),'external target panicked')
        finally:
            try:os.killpg(process.pid,signal.SIGTERM)
            except ProcessLookupError:pass
            try:process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=3)
    bad=subprocess.run(['make','--no-print-directory','run-external-console','QEMU='+qemu,
        'QEMU_DISPLAY=none','CONSOLE_ELF='+str(BUILD/'nonexistent-phase73.elf')],cwd=ROOT,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (BUILD/'phase73-missing-file.log').write_text(bad.stdout)
    require(bad.returncode!=0,'missing ELF unexpectedly launched')
    RESULTS.append(dict(label='run-target',completed_lifetimes=0,kind='startup verified, VM stopped by test'))
    print('PASS: actual run-external-console selected ELF and missing file rejection')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',default='qemu-system-i386');parser.add_argument('--gdb',default='gdb')
    parser.add_argument('--aelthuvor',type=Path)
    args=parser.parse_args();qemu,gdb=args.qemu,args.gdb
    rebuilt=build()
    console(qemu,gdb,'console',EXTERNAL/'console-test.elf')
    console(qemu,gdb,'persistent',EXTERNAL/'console.elf',0,1)
    console(qemu,gdb,'amber',EXTERNAL/'amber.elf',0,1)
    console(qemu,gdb,'rebuilt',rebuilt,0,1)
    console(qemu,gdb,'fault',EXTERNAL/'console-test.elf',2)
    console(qemu,gdb,'revoke-keyboard',EXTERNAL/'console-test.elf',3)
    console(qemu,gdb,'revoke-vga',EXTERNAL/'console-test.elf',4)
    lifecycle(qemu,gdb,'untrusted-note',EXTERNAL/'probe.elf','external preempt',mode=0)
    renamed=BUILD/'phase73-untrusted/console.elf';renamed.parent.mkdir(exist_ok=True);shutil.copyfile(EXTERNAL/'probe.elf',renamed)
    probe_blob=renamed.read_bytes();console_blob=(EXTERNAL/'console.elf').read_bytes()
    pn=next(h for h in headers(probe_blob) if h[0]==4);cn=next(h for h in headers(console_blob) if h[0]==4)
    require(probe_blob[pn[1]:pn[1]+pn[4]]==console_blob[cn[1]:cn[1]+cn[4]],'negative probe note differs')
    lifecycle(qemu,gdb,'untrusted-filename',renamed,'external preempt',mode=0)
    lifecycle(qemu,gdb,'partial-user-init',EXTERNAL/'console.elf','external external-keyboard',exit_status=86)
    lifecycle(qemu,gdb,'readonly',EXTERNAL/'probe.elf','external preempt external-vga-readonly',mode=1,fault=14)
    lifecycle(qemu,gdb,'handles',EXTERNAL/'probe.elf','external-console',mode=2)
    lifecycle(qemu,gdb,'layout',EXTERNAL/'layout.elf','external preempt')
    lifecycle(qemu,gdb,'missing',None,'external-console',expected=-8)
    bad=EXTERNAL/'bad-console.elf';bad.write_bytes(b'not ELF')
    lifecycle(qemu,gdb,'invalid',bad,'external-console',expected=-8)
    blob=bytearray((EXTERNAL/'console.elf').read_bytes());note=next(h for h in headers(blob) if h[0]==4)
    struct.pack_into('<I',blob,note[1]+24,2);bad.write_bytes(blob)
    lifecycle(qemu,gdb,'abi',bad,'external-console',expected=-93)
    lifecycle(qemu,gdb,'oom',EXTERNAL/'console.elf','external-console external-oom',expected=-12)
    lifecycle(qemu,gdb,'no-vga',EXTERNAL/'console.elf','external-console',expected=-2,extra=['-vga','none'])
    lifecycle(qemu,gdb,'no-ps2',EXTERNAL/'console.elf','external-console',expected=-5,extra=['-machine','pc,i8042=off'])
    if args.aelthuvor:
        lifecycle(qemu,gdb,'ael-cooperative-no-grant',args.aelthuvor/'build/first.elf','external')
        lifecycle(qemu,gdb,'ael-preempt-no-grant',args.aelthuvor/'build/preempt-first.elf','external preempt')
    run_targets(qemu)
    (BUILD/'phase73-results.json').write_text(json.dumps(dict(cases=RESULTS,qemu_guests=len(RESULTS),completed_lifetimes=sum(r['completed_lifetimes'] for r in RESULTS)),indent=2)+'\n')
    print('PASS: Phase 7.3 external console suite',len(RESULTS),'QEMU guests,',sum(r['completed_lifetimes'] for r in RESULTS),'completed lifecycles')

if __name__=='__main__':main()
