#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""QEMU PS/2 injection -> actual IRQ1 -> authorized Ring3 -> VGA, never serial input."""
import argparse
from contextlib import contextmanager
import json
import os
import signal
import socket
import subprocess
import tempfile
import threading
import time
from phase1 import ROOT, BUILD, ELF, require, wait_for
from phase2 import free_port, connect_and_run
from phase3 import HELPERS

class QMP:
    def __init__(self, endpoint):
        self.sock=socket.socket(socket.AF_UNIX)
        self.sock.settimeout(5)
        self.sock.connect(endpoint)
        self.stream=self.sock.makefile('rwb',buffering=0)
        require('QMP' in json.loads(self.stream.readline()),'QMP greeting missing')
        self.call('qmp_capabilities')
    def call(self, command, arguments=None):
        request={'execute':command}
        if arguments is not None: request['arguments']=arguments
        self.stream.write(json.dumps(request).encode()+b'\n')
        while True:
            response=json.loads(self.stream.readline())
            require('error' not in response,'QMP: '+str(response))
            if 'return' in response: return response['return']
    def close(self):
        self.stream.close()
        self.sock.close()


@contextmanager
def injector(endpoint):
    """GDB posts requests then resumes. QEMU rejects/ignores UI input while
    debug-stopped, so inject only after query-status confirms running."""
    control=endpoint+'.input'
    server=socket.socket(socket.AF_UNIX)
    server.bind(control); server.listen(1); server.settimeout(.1)
    done=threading.Event(); errors=[]
    def worker():
        qmp=None
        try:
            deadline=time.monotonic()+5
            while not os.path.exists(endpoint):
                if done.is_set(): return
                if time.monotonic()>deadline: raise RuntimeError('QMP socket did not appear')
                time.sleep(.001)
            qmp=QMP(endpoint)
            while not done.is_set():
                try: connection,_=server.accept()
                except socket.timeout: continue
                with connection:
                    request=json.loads(connection.makefile('rb').readline())
                    if 'command' in request:
                        result=qmp.call(request['command'],request.get('arguments'))
                        connection.sendall(json.dumps(result).encode()+b'\n')
                        continue
                for code in request['keys']:
                    deadline=time.monotonic()+10
                    while not done.is_set():
                        if time.monotonic()>deadline: raise RuntimeError('VM never resumed for PS/2 input')
                        if not qmp.call('query-status')['running']:
                            time.sleep(.001); continue
                        parts=code.split('-')
                        events=[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':part}}}
                            for down,part in ([(True,p) for p in parts]+[(False,p) for p in reversed(parts)])]
                        try:
                            qmp.call('input-send-event',{'events':events})
                            break
                        except RuntimeError as error:
                            if 'not running' not in str(error): raise
                            # A hardware breakpoint won the race; wait for resume.
                    if done.is_set(): break
        except Exception as error: errors.append(error)
        finally:
            if qmp is not None: qmp.close()
    thread=threading.Thread(target=worker,daemon=True);thread.start()
    try: yield control
    finally:
        done.set();thread.join(timeout=6);server.close()
        require(not thread.is_alive(),'input injector did not stop')
        if errors: raise errors[0]

INJECT = r'''python
import socket,json
def inject(keys):
    with socket.socket(socket.AF_UNIX) as control:
        control.connect(CONTROL)
        control.sendall(json.dumps(dict(keys=keys)).encode()+b'\n')
def query(command,arguments=None):
    with socket.socket(socket.AF_UNIX) as control:
        control.settimeout(5)
        control.connect(CONTROL)
        control.sendall(json.dumps(dict(command=command,arguments=arguments)).encode()+b'\n')
        return json.loads(control.makefile('rb').readline())
end
'''

@contextmanager
def vm(qemu, append, label):
    with tempfile.TemporaryDirectory(prefix='liberums-kbd-') as temp:
        endpoint=temp+'/qmp.sock'
        port=free_port()
        log=BUILD/(label+'-serial.log')
        with log.open('w') as output:
            process=subprocess.Popen([qemu,'-machine','pc','-accel','tcg','-cpu','qemu32',
                '-m','32M','-smp','1','-display','none','-serial','stdio','-monitor','none',
                '-nic','none','-no-reboot','-no-shutdown','-kernel',str(ELF),'-append',append,
                '-qmp','unix:'+endpoint+',server=on,wait=off','-S','-gdb','tcp:127.0.0.1:'+str(port)],
                cwd=ROOT,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
            try: yield process,log,port,endpoint
            finally:
                try: os.killpg(process.pid,signal.SIGTERM)
                except ProcessLookupError: pass
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid,signal.SIGKILL)
                    process.wait(timeout=3)

def diagnostic(qemu,gdb):
    with vm(qemu,'keyboard-diag','keyboard-diag') as (process,log,port,endpoint):
        with injector(endpoint) as control:
            return diagnostic_run(process,log,port,control,gdb)

def diagnostic_run(process,log,port,control,gdb):
    script=HELPERS+INJECT.replace('CONTROL',repr(control))+f'''hbreak *keyboard_diagnostic
continue
python
import sys
sys.path.insert(0,{str(ROOT/'tests')!r})
inject(['h'])
end
delete breakpoints
hbreak *keyboard_diagnostic_done
continue
python
check(v('keyboard_irqs')>0 and v('keyboard_irqs')==v('keyboard_eois'),'IRQ1/EOI missing')
check(v('keyboard_bytes')>0 and v('keyboard.count')==0,'unowned keyboard data was retained')
check(v('idt[33].attributes')==0x8e,'IRQ1 gate DPL unsafe')
print('ACTUAL IRQ1:',v('keyboard_irqs'),'bytes=',v('keyboard_bytes'),'EOI=',v('keyboard_eois'))
print(query('human-monitor-command',{{'command-line':'info pic'}}))
print('PASS: PS/2 injection reached actual IRQ1, master EOI, unowned input discarded')
end
detach
quit
'''
    output=connect_and_run(process,log,port,gdb,script,'keyboard-diag')
    require('PASS: PS/2 injection' in output,'diagnostic did not execute')
    wait_for(process,log,lambda s:'Keyboard IRQ1 diagnostic completed' in s,'IRQ1 diagnostic')
    print('PASS: independent real PS/2 IRQ1 diagnostic')

AUDIT = r'''python
mode=MODE
label=LABEL
seen=dict(irq=0,returned=0,peer_irq=0,events=0,overflow=0,reaped=0)
raw_frame=None
index=0
raw_index=0
generations=[]
expected=ACTION_LIST
start_bytes=0
last_data_pages=[]
def c(field): return v('((struct console*)0x08001100)->'+field)
def data(i,word): return words(d(i,'space.pages[4]')+word*4,1)[0]
def write_data(word,value):
    gdb.selected_inferior().write_memory(d(0,'space.pages[4]')+word*4,struct.pack('<I',value))
def framebuffer():
    b=bytes(gdb.selected_inferior().read_memory(0xb8000,4000))
    return '\n'.join(b[row*160:(row+1)*160:2].decode('ascii',errors='replace').rstrip() for row in range(25))
def irq_entry():
    global raw_frame
    regs=tuple(v('$'+r) for r in ('eax','ebx','ecx','edx','esi','edi','ebp'))
    # First dummy error word is already pushed; GP registers are untouched.
    # Avoid the first IRQ instruction: GDB/TCG can report that PC twice when
    # interrupt entry coincides with stepping over another hardware breakpoint.
    sp=v('$esp')+4
    check(v('keyboard_irqs')==seen['irq'],'duplicate or missed IRQ1 capture')
    cpu=words(sp,3)
    outer=words(sp,5) if cpu[1]&3 else cpu
    segments=tuple(v('$'+r) for r in ('ds','es','fs','gs'))
    check(v('$cs')==8 and v('$eflags')&0x200==0,'IRQ1 entry not protected IF=0')
    if cpu[1]&3:
        check(cpu[1]==0x1b and outer[4]==0x23,'IRQ1 did not enter from Ring3')
        i=v('current_domain->id')-1
        check(sp==v('tss.esp0')-20 and v('$cr3')==d(i,'space.pages[0]'),'IRQ1 did not use domain TSS/CR3')
        if cpu[0]==PEER_SPIN:
            check(regs==tuple(j*0x11111111 for j in (1,2,3,4,5,6,7)),'peer GP sentinel corrupted by IRQ0/IRQ1')
            check(cpu[2]&0x401==0x401,'peer CF/DF lost')
            seen['peer_irq']+=1
    raw_frame=(sp,outer,regs,segments,v('$cr3'))
    seen['irq']+=1
def irq_return():
    check(raw_frame is not None,'missing raw IRQ1 frame')
    sp,cpu,regs,segments,cr3=raw_frame
    check(v('$esp')==sp and words(sp,len(cpu))==cpu,'IRQ1 iret frame/ESP changed')
    check(tuple(v('$'+r) for r in ('eax','ebx','ecx','edx','esi','edi','ebp'))==regs,'IRQ1 GP registers changed')
    check(tuple(v('$'+r) for r in ('ds','es','fs','gs'))==segments and v('$cr3')==cr3,'IRQ1 segments/CR3 changed')
    check(v('keyboard_irqs')==v('keyboard_eois')==seen['returned']+1,'IRQ1 missing/duplicated dispatch or EOI')
    check(v('keyboard.count')<=64,'queue exceeded bound')
    seen['returned']+=1
    if mode==8 and v('keyboard_bytes')-start_bytes>=80 and v('keyboard.overflow'):
        write_data(8,1)
def send_next():
    global index,raw_index
    if index<len(expected):
        raw_index=0
        inject([expected[index]['key']])
    else:
        write_data(9,1 if mode in (0,8) else mode)
def checkpoint():
    global index,raw_index,start_bytes,last_data_pages,peer_marker
    check(v('$cs')==0x1b and v('$ss')==0x23 and v('$cr3')==d(0,'space.pages[0]'),'editor not real Ring3')
    check(v('$eflags')&0x3200==0x200,'interactive IF/IOPL unsafe')
    stage=data(0,2)
    if stage==1:
        index=raw_index=0
        peer_marker=0
        check(v('keyboard.count')==0 and v('keyboard.lost')==0,'old keyboard data/state inherited')
        generation=data(0,4)
        check(not generations or generation>generations[-1],'old generation reused')
        generations.append(generation)
        last_data_pages=[d(i,'space.pages[4]') for i in range(2)]
        check(v('keyboard.grant_epoch')==d(0,'epoch') and v('keyboard.cap.owner_id')==1,'keyboard authority wrong')
        check(v('idt[33].attributes')==0x8e,'user IRQ gate exposed')
        check(v('states[184]')==1,'VGA page entered RAM allocator')
        if mode==8:
            start_bytes=v('keyboard_bytes')
            inject(['a']*40)
        else: send_next()
        print('READY: epoch=%d keyboard generation=%d' % (d(0,'epoch'),generation))
        return
    if stage==3:
        check(mode==8 and data(0,7)>=80,'missing actual overflow report')
        check(v('keyboard.count')==0 and v('keyboard.overflow')==0,'overflow batch not discarded')
        check(c('length')==0 and c('shift')==0 and c('extended')==0,'user decoder/editor not reset')
        check('Keyboard overflow: input reset' in framebuffer(),'overflow not visible')
        seen['overflow']+=1
        send_next(); return
    check(stage==2,'unexpected user checkpoint')
    action=expected[index]
    raw=data(0,7)
    check(raw==action['raw'][raw_index],'unexpected physical scan byte: %x expected %x'%(raw,action['raw'][raw_index]))
    raw_index+=1;seen['events']+=1
    if raw_index!=len(action['raw']): return
    # Read via the user ELF's type/layout, not a guessed C struct offset.
    address=v('&((struct console*)0x08001100)->line')
    line=bytes(gdb.selected_inferior().read_memory(address,64)).split(b'\0')[0].decode()
    check(line==action['line'],'editing mismatch: '+repr(line)+' expected '+repr(action['line']))
    check(c('column')==len(line)+2 and c('column')<80 and c('row')<25,'cursor out of bounds')
    if 'contains' in action:
        check(action['contains'] in framebuffer(),'command output missing: '+action['contains'])
        print('COMMAND:',action['key'],action['contains'])
    if action.get('clear'):
        check(framebuffer().strip()=='>','clear command did not clear actual VGA cells')
    index+=1;send_next()
def reaped():
    check(v('keyboard.enabled')==0 and v('keyboard.active')==0 and v('keyboard.grant_id')==0,'keyboard authority survived exit')
    check(v('keyboard.count')==0 and not any(bytes(gdb.selected_inferior().read_memory(v('&keyboard.bytes'),64))),'queued keystrokes survived exit')
    check(v('vga_device.active')==0 and d(0,'space.device_table')==0,'VGA mapping survived exit')
    check(data_saved_peer()==10 and d(1,'fault_vector')==0,'unauthorized peer failed authority/frame checks')
    check(d(1,'preemptions')>0 and v('irq_count')==v('eoi_count'),'IRQ0 preemption/EOI broken')
    if mode in (2,3,4,5):
        check(d(0,'state')==4,'illegal access did not fault')
        check(d(0,'fault_vector')==(6 if mode==5 else 13),'wrong isolation fault')
        check(d(0,'fault_error')==(0x10a if mode==4 else 0),'wrong INT/IO error')
        print('ISOLATION: vector=%d error=%#x'%(d(0,'fault_vector'),d(0,'fault_error')))
    else:
        check(d(0,'state')==3 and d(0,'exit_status')==0,'user assertions failed: '+str(d(0,'exit_status')))
    state_bytes=bytes(gdb.selected_inferior().read_memory(v('&states'),16384))
    check(state_bytes.count(2)==v('phase72_free_before'),'device/space page leak')
    pic=query('human-monitor-command',{'command-line':'info pic'})
    import re
    master=re.search(r'pic0: irr=([0-9a-f]+) imr=([0-9a-f]+) isr=([0-9a-f]+)',pic)
    check(master and int(master[2],16)==0xfe and int(master[3],16)&2==0,'IRQ1 mask/EOI damaged IRQ0')
    seen['reaped']+=1
    check(v('phase72_rounds')==seen['reaped'],'lifetime count mismatch')
    print('REAPED: epoch=%d IRQ0=%d peer preemptions=%d IRQ1=%d EOI=%d PIC imr=%s isr=%s' %
          (d(0,'epoch'),v('irq_count'),d(1,'preemptions'),v('keyboard_irqs'),v('keyboard_eois'),master[2],master[3]))
    if seen['reaped']==2:
        check(seen['irq']==seen['returned'] and seen['irq']>0,'IRQ1 frame audit mismatch: '+repr(seen))
        check(seen['peer_irq']>0,'IRQ1 never interrupted the noncooperative peer')
        check(seen['overflow']==(2 if mode==8 else 0),'overflow rounds missing')
        query('screendump',{'filename':str(BUILD_PATH/('keyboard-'+label+'.png')),'format':'png'})
        print('PASS: actual keyboard IRQ1/CPL3/editor/lifetime audit',label,seen)
        gdb.execute('detach');gdb.execute('quit')
# DATA RAM is zeroed on teardown; capture the peer marker before reaping by
# sampling it at each IRQ1 return while the corresponding domain is live.
peer_marker=0
original_irq_return=irq_return
def irq_return():
    global peer_marker
    original_irq_return()
    if d(1,'space.live'): peer_marker=data(1,2)
def data_saved_peer(): return peer_marker
class Audit(gdb.Breakpoint):
    def __init__(self,loc,action):
        super().__init__('*'+loc,type=gdb.BP_HARDWARE_BREAKPOINT,internal=True);self.action=action
    def stop(self):
        try: self.action()
        except Exception:
            import traceback
            gdb.write(traceback.format_exc());gdb.execute('quit 1')
        return False
Audit('keyboard_irq_capture',irq_entry)
Audit('keyboard_irq_return',irq_return)
Audit('USER_CHECKPOINT',checkpoint)
Audit('phase72_reaped',reaped)
end
continue
'''

SCANS=dict(zip('qwertyuiop',[16,17,18,19,20,21,22,23,24,25]))
SCANS.update(zip('asdfghjkl',[30,31,32,33,34,35,36,37,38]))
SCANS.update(zip('zxcvbnm',[44,45,46,47,48,49,50]))
SCANS.update(dict(ret=28,backspace=14,shift=42,caps_lock=58))

def actions(functional):
    result=[];line=''
    def key(code,line_after,contains=None,clear=False):
        parts=code.split('-')
        if code=='right': raw=[0xe0,0x4d,0xe0,0xcd]
        else: raw=[SCANS[p] for p in parts]+[SCANS[p]|0x80 for p in reversed(parts)]
        item=dict(key=code,raw=raw,line=line_after)
        if contains: item['contains']=contains
        if clear: item['clear']=True
        result.append(item)
    def type_text(s):
        nonlocal line
        for ch in s: line+=ch;key(ch,line)
    if functional:
        type_text('help');line='';key('ret',line,'help clear about status')
        type_text('clx');line=line[:-1];key('backspace',line)
        type_text('ear');line='';key('ret',line,clear=True)
        key('backspace','');key('right','')
        key('caps_lock','');key('h','H');key('shift-e','He');key('caps_lock','He')
        key('backspace','H');key('backspace','')
        for i in range(65): key('a','a'*min(i+1,63))
        for i in range(64): key('backspace','a'*max(62-i,0))
        line='';type_text('about');line='';key('ret',line,'Liberums Exokernel - Ring 3 interactive console')
        type_text('status');line='';key('ret',line,'Ring 3 | ABI 1.2 | IRQ0 preempt | PS/2 IRQ1')
    else:
        type_text('help');line='';key('ret',line,'help clear about status')
    return result

def console(qemu,gdb,label,mode):
    symbols=subprocess.check_output(['nm','-n',str(BUILD/'interactive-user.elf')],text=True)
    names={line.split()[2]:int(line.split()[0],16) for line in symbols.splitlines() if len(line.split())==3}
    with vm(qemu,'interactive-console console-test'+(' console-overflow' if mode==8 else ''),'keyboard-'+label) as (p,log,port,endpoint):
        with injector(endpoint) as control:
            script='add-symbol-file "'+str(BUILD/'interactive-user.elf')+'"\n'+HELPERS+INJECT.replace('CONTROL',repr(control))
            script+='python\nfrom pathlib import Path\nBUILD_PATH=Path('+repr(str(BUILD))+')\nend\n'
            script+=AUDIT.replace('MODE',str(mode)).replace('LABEL',repr(label)).replace('ACTION_LIST',repr(actions(mode==0)))
            script=script.replace('USER_CHECKPOINT',hex(names['interactive_checkpoint'])).replace('PEER_SPIN',hex(names['interactive_peer_spin']))
            output=connect_and_run(p,log,port,gdb,script,'keyboard-'+label)
            require('PASS: actual keyboard' in output,'keyboard audit did not finish')
            wait_for(p,log,lambda s:'Phase 7.2: interactive lifetimes completed' in s,'interactive lifecycle completion')
    print('PASS: keyboard '+label+' two real IRQ1/Ring3 lifecycles')


def run_target(qemu):
    log=BUILD/'run-console-serial.log'
    with log.open('w') as output:
        process=subprocess.Popen(['make','--no-print-directory','run-console','QEMU='+qemu,'QEMU_DISPLAY=none'],
            cwd=ROOT,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            wait_for(process,log,lambda t:'Interactive console: PS/2 IRQ1 ready' in t,'actual run-console startup')
            require('PANIC' not in log.read_text(),'run-console panicked')
        finally:
            try: os.killpg(process.pid,signal.SIGTERM)
            except ProcessLookupError: pass
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=3)
    print('PASS: actual make run-console QEMU_DISPLAY=none startup')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',default='qemu-system-i386')
    parser.add_argument('--gdb',default='gdb')
    args=parser.parse_args()
    diagnostic(args.qemu,args.gdb)
    for label,mode in [('functional',0),('in-port',2),('out-port',3),('int-gate',4),('fault-cleanup',5),('revoke',6),('release',7),('overflow',8)]:
        console(args.qemu,args.gdb,label,mode)
    run_target(args.qemu)

if __name__=='__main__': main()
