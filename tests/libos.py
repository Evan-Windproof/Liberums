#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hardware audit of independently built LibOS ELF files (no LibOS source linkage)."""
from pathlib import Path
import struct
import subprocess
from phase1 import BUILD, emulator, require, wait_for
from phase2 import free_port, connect_and_run
from phase3 import HELPERS
from phase5 import GDB_HELPERS, headers, symbols

LIBOS_HELPERS = '''python
resource_pages=[]
resource_tables=[]
def inspect_libos(policy):
    global resource_pages,resource_tables
    inspect_external_user()
    check(v('ael_runtime.version') == 0x10001 and v('ael_runtime.features')&31 == 31,'ABI query results')
    check(v('ael_runtime.quota')==4 and v('ael_runtime.bindings')==4,'capacity query results')
    check(v('ael_runtime.heap.policy')==policy,'wrong user allocation policy')
    check(v('ael_runtime.heap.live')==1 and v('ael_runtime.pages.owned')==1,'user allocator state')
    va=v('ael_demo_memory')
    inspect_resource(va,0xa3110000)
    table=d(0,'space.dynamic_table')
    resource_tables.append(table)
    owned=[i for i in range(16) if v('resources[%d].state'%i)==1]
    check(len(owned)==1,'unexpected resources')
    slot=owned[0]
    pa=v('resources[%d].physical_address'%slot)
    resource_pages.append(pa)
    check(v('resources[%d].cap.owner_epoch'%slot)==d(0,'epoch'),'stale epoch')
    check(v('resources[%d].cap.rights'%slot)==47,'LibOS requested excess rights')
    check(words(pa,1024)==tuple(0xa3110000|i for i in range(1024)),'user data writes incomplete')
    entries=words(table,1024)
    check(sum(bool(p) for p in entries)==1,'unexpected dynamic mapping')
    peer=words(d(1,'space.pages[0]'),1024)
    check(peer[36]==0 and d(1,'space.dynamic_table')==0,'LibOS resource exposed to peer')
    print('LIBOS: policy=',policy,'live=1 VA=',hex(va),'PFN=',hex(pa>>12),'generation=',v('resources[%d].cap.generation'%slot))
def inspect_libos_cleanup():
    inspect_external_reaped()
    for page in resource_pages+resource_tables:
        check(v('states[%d]'%(page>>12))==2 and not any(words(page,1024)),'LibOS resource/table not zeroed and freed')
    check(d(1,'state')==3 and d(1,'exit_status')==0 and d(1,'yields')==1,'peer isolation failed')
end
'''


def run(qemu, gdb, name, image, kind='normal', policy=0, expected=0, oom=False):
    blob=image.read_bytes()
    names=symbols(image)
    segments=headers(blob)
    peer=symbols(BUILD/'user_b.elf')
    commands=HELPERS+GDB_HELPERS+LIBOS_HELPERS
    if expected==0:
        # The debug symbols come from the external ELF. The kernel has no such symbols.
        commands+='add-symbol-file "'+str(image)+'"\n'
    for iteration in range(2):
        commands+='delete breakpoints\nhbreak *external_prepared\ncontinue\npython\n'
        commands+='resource_pages=[]\nresource_tables=[]\n'
        commands+=f"check(v('external_load_result')==({expected}&0xffffffff),'unexpected LibOS load result')\n"
        if expected==0:
            commands+=f'inspect_external({segments!r},{blob!r})\n'
        else:
            commands+="external_pages=[]\npeer_pages=[d(1,'space.pages[%d]'%i) for i in range(6)]\ncheck(d(0,'space.live')==0 and d(0,'space.count')==0,'rejected LibOS activated')\ncheck(v('external_free_before_load')==v('external_free_after_load'),'LibOS rollback leak')\n"
            if oom:
                commands+="check(d(0,'space.rollback_pages')==5,'partial LibOS allocation not exercised')\nfor i in range(5):\n    page=d(0,'space.pages[%d]'%i)\n    check(v('states[%d]'%(page>>12))==2 and not any(words(page,1024)),'LibOS partial page not cleared')\n"
        commands+='end\n'
        if expected==0:
            commands+=f"delete breakpoints\nhbreak *{names['_start']:#x}\ncontinue\npython inspect_external_user()\n"
            commands+='delete breakpoints\nhbreak *syscall_dispatch\ncontinue\npython\n'
            commands+="cpu=inspect_trap(0)\ncheck(cpu[7]==3,'LibOS must query ABI before resource operations')\nend\n"
            if kind!='runtime':
                commands+='delete breakpoints\nhbreak *syscall_dispatch\ncontinue\npython\n'
                commands+="cpu=inspect_trap(0)\ncheck(cpu[7]==4,'LibOS must query runtime limits')\nend\n"
                commands+=f"delete breakpoints\nhbreak *{names['ael_checkpoint']:#x}\ncontinue\npython\ninspect_external_user()\ncheck(words(v('$esp')+4,1)[0]==1,'runtime initialization checkpoint missing')\nend\n"
                commands+=f"delete breakpoints\nhbreak *{names['ael_checkpoint']:#x}\ncontinue\npython\ncheck(words(v('$esp')+4,1)[0]==2,'memory tests did not finish')\ninspect_libos({policy})\nend\n"
        commands+=f"delete breakpoints\nhbreak *{peer['user_checkpoint']:#x}\ncondition $bpnum current_domain->id == 2\ncontinue\npython\ncheck(v('$cs')==0x1b and v('$ss')==0x23 and v('$cr3')==d(1,'space.pages[0]'),'peer not independent CPL3')\nend\n"
        if expected==0 and kind in ('fault','readonly'):
            address=0x08800000 if kind=='fault' else 0x08000000
            error=4 if kind=='fault' else 7
            commands+='delete breakpoints\nhbreak *domain_fault\ncontinue\npython\n'
            commands+=f"cpu=inspect_trap(0)\ncheck(cpu[8]==14 and cpu[9]=={error} and cpu[10]=={names['ael_demo_fault']},'wrong LibOS hardware fault')\ncheck(v('$cr2')=={address},'wrong LibOS CR2')\nprint('LIBOS PF:',hex(cpu[10]),hex(v('$cr2')),cpu[9])\nend\n"
        elif expected==0 and kind=='normal':
            commands+=f"delete breakpoints\nhbreak *{names['ael_checkpoint']:#x}\ncontinue\npython\ninspect_external_user()\ncheck(words(v('$esp')+4,1)[0]==3,'release checkpoint missing')\ncheck(d(0,'resource_count')==0 and d(0,'space.dynamic_table')==0,'LibOS release leaked')\ncheck(v('ael_runtime.heap.live')==0 and v('ael_runtime.pages.owned')==0,'LibOS metadata not released')\nend\n"
        commands+='delete breakpoints\nhbreak *external_reaped\ncontinue\npython\ninspect_libos_cleanup()\n'
        if expected==0:
            commands+=f"check(d(0,'state')=={4 if kind in ('fault','readonly') else 3},'wrong LibOS termination state')\n"
            if kind not in ('fault','readonly'):
                commands+=f"check(d(0,'exit_status')=={93 if kind=='runtime' else 0},'LibOS application checks failed')\n"
        commands+='end\n'
    commands+='delete breakpoints\ndetach\nquit\n'
    label='phase5-aelthuvor-'+name
    port=free_port()
    with emulator('debug',qemu,port,append='external'+(' external-oom' if oom else ''),initrd=image,label=label) as (process,log):
        connect_and_run(process,log,port,gdb,commands,label)
        wait_for(process,log,lambda text:'Phase 5: external execution completed' in text,'LibOS lifecycles')
        require('PANIC' not in log.read_text(),'LibOS caused kernel panic')
    print('PASS:',label,'two lifecycles; actual CPL3/ABI/PTE/ownership/peer/reclamation')


def suite(directory, qemu, gdb):
    directory=Path(directory).resolve()
    for variant,kind,policy in [('first','normal',0),('best','normal',1),('fault','fault',0),
                                ('readonly','readonly',0),('exit-live','exit-live',0),('runtime-abi','runtime',0)]:
        run(qemu,gdb,variant,directory/(variant+'.elf'),kind,policy)
    reference=directory/'first.elf'
    data=reference.read_bytes()
    note=next(h for h in headers(data) if h[0]==4)
    for name,field,value in [('bad-major',24,2),('missing-feature',32,0x80000000)]:
        bad=bytearray(data)
        struct.pack_into('<I',bad,note[1]+field,value)
        file=BUILD/('aelthuvor-'+name+'.elf')
        file.write_bytes(bad)
        run(qemu,gdb,name,file,expected=-93)
    run(qemu,gdb,'oom',reference,expected=-12,oom=True)
    print('PASS: Aelthuvor integration 9 QEMU guests / 18 lifecycles; no skipped checks')
