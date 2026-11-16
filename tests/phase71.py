#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual VGA device memory, CPL3, secure PTEs and lifecycle tests, no GUI required."""
import argparse
from phase1 import emulator, require, wait_for, BUILD
from phase2 import free_port, connect_and_run
from phase3 import HELPERS


def diagnostic(qemu, gdb):
    port = free_port()
    with emulator('debug', qemu, port, append='vga-diag', label='vga-diag') as (process, log):
        output = connect_and_run(process, log, port, gdb, HELPERS + '''
hbreak *vga_diagnostic_written
continue
python
b=bytes(gdb.selected_inferior().read_memory(0xb8000,4000))
check(b[:60:2]==b'Liberums Ring 0 VGA diagnostic','VGA characters missing')
check(b[1:60:2]==bytes([0x1f])*30,'VGA colors missing')
check(v('page_tables[0][184]') & 0xfffff09f == 0xb801b,'unsafe MMIO alias')
check(v('states[184]')==1,'VGA not permanently reserved')
check(v('vga_boot.pat_supported')==0 or v('vga_boot.pat3')==0,'PAT[3] is not UC')
check(v('vga_boot.mode')==3 and v('vga_boot.columns')==80 and v('vga_boot.rows')==25,'not 80x25 text')
print('VGA BOOT:',gdb.parse_and_eval('vga_boot'))
print('PASS: Ring0 VGA text, color and supervisor PCD/PWT PTE')
end
detach
quit
''', 'vga-diag')
        require('PASS: Ring0 VGA' in output, 'diagnostic checks did not execute')
        wait_for(process, log, lambda t: 'VGA: Ring 0 diagnostic written' in t, 'diagnostic serial')
    print('PASS: independent Ring0 VGA diagnostic (actual device memory)')


AUDIT = r'''python
mode=MODE
seen=dict(user=0,stopped=0,finished=0,reaped=0,drawn=0)
tables=set()
last_generation=0
def pte(i):
    pd=d(i,'space.pages[0]')
    pde=words(pd+(0x09400000>>22)*4,1)[0]
    return words((pde&~4095),1)[0] if pde&1 else 0
def buffer():
    return bytes(gdb.selected_inferior().read_memory(0xb8000,4000))
def screen_check():
    b=buffer()
    lines=[('Liberums Exokernel',0x1f),('VGA Text Console Prototype',0x1e),
           ('Ring 3: running',0x1a),('VGA: authorized device mapping',0x1a),
           ('COM1: retained',0x1a),('ABI 1.2: compatible',0x1a)]
    for row,(s,color) in enumerate(lines):
        chunk=b[row*160:row*160+len(s)*2]
        check(chunk[::2]==s.encode() and chunk[1::2]==bytes([color])*len(s),'wrong text/color row '+str(row))
        tail=b[row*160+len(s)*2:(row+1)*160]
        check(tail==bytes([32,0x17])*(80-len(s)),'clear/newline tail incorrect')
    check(b[960:]==bytes([32,0x17])*(2000-480),'clear screen incomplete')
def checkpoint():
    global last_generation
    check(v('$cs')==0x1b and v('$ss')==0x23,'renderer not actual Ring3')
    check(v('$cr3')==d(0,'space.pages[0]'),'wrong console CR3')
    check(v('$eflags')&0x3000==0,'user IOPL')
    a=words(d(0,'space.pages[4]'),6)
    stage=a[2]
    check(stage in (1,2),'unexpected stage')
    check(v('states[184]')==1,'VGA became RAM')
    check(v('vga_device.cap.owner_epoch')==d(0,'epoch'),'wrong grant epoch')
    check(a[3]==0x10001 and a[4]>last_generation,'handle generation reused')
    last_generation=a[4]
    entry=pte(0)
    check(entry&0xfffff09f==(0xb801d if stage==1 else 0xb801f),'wrong PA or permissions/cache flags')
    check(pte(1)==0,'peer got device alias')
    table=d(0,'space.device_table')
    tables.add(table)
    check(not any(words(table+4,1023)),'mapping extends beyond device page')
    check(words(d(0,'space.pages[0]'),1)[0]&4==0,'kernel PDE user accessible')
    check(v('page_tables[0][184]')&0xfffff09f==0xb801b,'kernel cache alias mismatch')
    check(d(0,'resource_count')==0,'device counted as RAM')
    if stage==2:
        screen_check()
        seen['drawn']+=1
    print('USER VGA: round=%d stage=%d CPL=%d CR3=%#x PTE=%#x handle=%#x:%d' %
          (v('phase71_rounds')+1,stage,v('$cs')&3,v('$cr3'),entry,a[3],a[4]))
    if stage==1:
        check(words(d(0,'space.pages[4]')+24,1)[0]==int.from_bytes(buffer()[:2],'little'),'RO read failed')
    seen['user']+=1
def stopped():
    i=v('phase71_last->id')-1
    a=words(d(i,'space.pages[4]'),6)
    state=d(i,'state')
    if state==4:
        if i==1:
            vector,address,error=14,0x09400000,6
            check(a[2]==9,'peer failed before actual unauthorized access')
        else:
            expected={1:(14,0x09400000,6),2:(14,0x09401000,6),3:(14,0xb8000,7),
                      4:(14,0x09400000,7),5:(14,0x100000,7),6:(13,0,0),
                      7:(14,d(0,'space.pages[0]'),7),8:(14,0x09400000,6),10:(14,0x093fffff,6)}
            check(mode in expected,'normal console faulted')
            vector,address,error=expected[mode]
        check(d(i,'fault_vector')==vector and d(i,'fault_address')==address
              and d(i,'fault_error')==error,'wrong hardware fault')
    elif state==3:
        check(i==0 and mode==0 and d(i,'exit_status')==0,'user assertion failure '+str(d(i,'exit_status')))
    else:
        check(state==1 and i==0,'bad scheduler state')
        if a[2]==2 and mode==1:
            check(pte(0)==0 and v('vga_device.active')==0 and v('vga_device.grant_id')==0,'forced revoke retained authority')
            check(d(0,'space.device_table')==0,'revoke leaked table')
    seen['stopped']+=1
def finished():
    check(v('$cr3')==v('&page_directory') and v('current_domain')==0,'unsafe cleanup CR3')
    check(v('vga_device.active')==0 and v('vga_device.mapped')==0 and v('vga_device.grant_id')==0,'device authority survived terminal state')
    for i in range(2):
        check(d(i,'space.device_table')==0 and pte(i)==0,'terminal domain mapping survived')
    for table in tables:
        check(v('states[%d]'%(table>>12))==2 and not any(words(table,1024)),'device table not freed/zeroed')
    if mode!=4: screen_check()
    check(v('states[184]')==1,'device freed as RAM')
    seen['finished']+=1
def reaped():
    check(v('phase71_rounds')==seen['reaped']+1,'round not completed')
    for i in range(2):
        check(d(i,'space.live')==0 and d(i,'space.count')==0,'space not reaped')
    state_bytes=bytes(gdb.selected_inferior().read_memory(v('&states'),16384))
    check(state_bytes.count(2)==v('phase71_free_before'),'physical page leak')
    tables.clear()
    seen['reaped']+=1
    if seen['reaped']==2:
        expected_user=2 if mode==4 else 4
        check(seen['user']==expected_user and seen['finished']==2,'missing checkpoints')
        check(seen['drawn']==(0 if mode==4 else 2),'renderer checks not executed')
        print('PASS: VGA hardware audit mode=%d counts=%s' % (mode,seen))
        gdb.execute('detach')
        gdb.execute('quit')
class Audit(gdb.Breakpoint):
    def __init__(self,loc,action):
        super().__init__('*'+loc,type=gdb.BP_HARDWARE_BREAKPOINT,internal=True)
        self.action=action
    def stop(self):
        try: self.action()
        except Exception:
            import traceback
            gdb.write(traceback.format_exc())
            gdb.execute('quit 1')
        return False
Audit('USER_CHECKPOINT',checkpoint)
Audit('phase71_stopped',stopped)
Audit('phase71_finished',finished)
Audit('phase71_reaped',reaped)
end
continue
'''


def console(qemu, gdb, mode, number):
    import subprocess
    symbols = subprocess.check_output(['nm', '-n', str(BUILD/'vga-user.elf')], text=True)
    pc = next(int(line.split()[0],16) for line in symbols.splitlines()
              if line.split()[-1]=='vga_user_checkpoint')
    port = free_port()
    label = 'vga-' + mode
    with emulator('debug', qemu, port, append='vga-console vga-test='+mode, label=label) as (process, log):
        script = HELPERS + AUDIT.replace('MODE', str(number)).replace('USER_CHECKPOINT', hex(pc))
        output = connect_and_run(process, log, port, gdb, script, label)
        require('PASS: VGA hardware audit' in output, 'VGA audit did not complete')
        if mode=='oom':
            require(log.read_text().count('VGA: actual page-table OOM rollback passed')==2,'OOM test did not execute twice')
        wait_for(process, log, lambda t: 'Phase 7.1: VGA user experiment completed' in t, 'console complete')
    print('PASS: VGA '+mode+' / two lifecycles, actual CPL3/PTE/fault/cleanup checks')


def screenshot(qemu):
    """Ask QEMU's real VGA renderer for pixels, independently of serial/GDB."""
    import hashlib
    import json
    import os
    import signal
    import socket
    import subprocess
    import tempfile
    from phase1 import ROOT, ELF
    with tempfile.TemporaryDirectory(prefix='liberums-vga-') as directory:
        endpoint = directory + '/qmp.sock'
        log = BUILD/'vga-screenshot-serial.log'
        with log.open('w') as output:
            process = subprocess.Popen([qemu, '-machine','pc','-accel','tcg','-cpu','qemu32',
                '-m','32M','-smp','1','-display','none','-serial','stdio','-monitor','none',
                '-nic','none','-no-reboot','-no-shutdown','-kernel',str(ELF),
                '-append','vga-console','-qmp','unix:'+endpoint+',server=on,wait=off'],
                cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                wait_for(process,log,lambda t:'Phase 7.1: VGA user experiment completed' in t,'screenshot boot')
                with socket.socket(socket.AF_UNIX) as sock:
                    sock.settimeout(5)
                    sock.connect(endpoint)
                    stream = sock.makefile('rwb', buffering=0)
                    require('QMP' in json.loads(stream.readline()),'missing QMP greeting')
                    def command(name, arguments=None):
                        request={'execute':name}
                        if arguments is not None: request['arguments']=arguments
                        stream.write(json.dumps(request).encode()+b'\n')
                        while True:
                            response=json.loads(stream.readline())
                            require('error' not in response, 'QMP error: '+str(response))
                            if 'return' in response: return response['return']
                    command('qmp_capabilities')
                    command('stop')
                    image=BUILD/'vga-console.ppm'
                    image.unlink(missing_ok=True)
                    command('screendump',{'filename':str(image)})
                    # Also retain a convenient shareable PNG from the same renderer.
                    command('screendump',{'filename':str(BUILD/'vga-console.png'),'format':'png'})
                    blob=image.read_bytes()
                    magic,size,maximum,pixels=blob.split(b'\n',3)
                    width,height=map(int,size.split())
                    require(magic==b'P6' and maximum==b'255' and (width,height)==(720,400), 'not VGA 80x25 9x16 glyphs')
                    require(len(pixels)==width*height*3,'truncated screenshot')
                    # VGA DAC uses six-bit channels; QEMU versions expand these
                    # to RGB8 with slightly different rounding (e.g. 168 vs 170).
                    for row in range(3):
                        band=pixels[row*16*width*3:(row+1)*16*width*3]
                        colors=[tuple(band[i:i+3]) for i in range(0,len(band),3)]
                        def foreground(rgb):
                            r,g,b=rgb
                            return (min(rgb)>240 if row==0 else
                                    r>240 and g>240 and 60<b<100 if row==1 else
                                    60<r<100 and g>240 and 60<b<100)
                        require(sum(map(foreground,colors))>100,'missing foreground pixels on row '+str(row))
                    last=pixels[-16*width*3:]
                    background=last[:3]
                    require(background[:2]==b'\x00\x00' and 150<background[2]<180
                            and last==background*(16*width),'screen background not cleared')
                    digest=hashlib.sha256(blob).hexdigest()
                    (BUILD/'vga-screenshot.json').write_text(json.dumps(dict(width=width,height=height,sha256=digest,backend='QMP screendump, display none'),indent=2)+'\n')
                    print('PASS: QEMU VGA renderer screenshot 720x400, white/yellow/green text, blue cleared background; SHA256='+digest)
            finally:
                try: os.killpg(process.pid,signal.SIGTERM)
                except ProcessLookupError: pass
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid,signal.SIGKILL)
                    process.wait(timeout=3)


def run_targets(qemu):
    import os
    import signal
    import subprocess
    from phase1 import ROOT
    for target,marker in [('run-vga-diag','VGA: Ring 0 diagnostic written'),
                          ('run-vga','Phase 7.1: VGA user experiment completed')]:
        log=BUILD/(target+'-serial.log')
        with log.open('w') as output:
            process=subprocess.Popen(['make','--no-print-directory',target,
                'QEMU='+qemu,'QEMU_DISPLAY=none'],cwd=ROOT,stdout=output,
                stderr=subprocess.STDOUT,start_new_session=True)
            try:
                wait_for(process,log,lambda text:marker in text,target+' actual boot')
                require('PANIC' not in log.read_text(),target+' panicked')
            finally:
                try: os.killpg(process.pid,signal.SIGTERM)
                except ProcessLookupError: pass
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid,signal.SIGKILL)
                    process.wait(timeout=3)
        print('PASS: actual make '+target+' QEMU_DISPLAY=none functional boot')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-i386')
    parser.add_argument('--gdb', default='gdb')
    args = parser.parse_args()
    diagnostic(args.qemu, args.gdb)
    for mode, number in [('normal',0),('revoke',1),('upper',2),('physical',3),
                         ('readonly',4),('kernel',5),('io',6),('table',7),('release',8),('lower',10),('oom',0)]:
        console(args.qemu, args.gdb, mode, number)
    screenshot(args.qemu)
    run_targets(args.qemu)


if __name__ == '__main__':
    main()
