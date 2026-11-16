#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ring-3 resource API tests with independent hardware/ownership/PTE audits."""
import argparse
import subprocess
import sys
from phase1 import BUILD, emulator, require, wait_for
from phase2 import free_port, connect_and_run
from phase3 import HELPERS

AUDIT = '''python
W=0x09000000
def r(slot,field):
    return v('resources[%d].%s' % (slot,field))
def user_data(i):
    return words(d(i,'space.pages[4]'),1024)
def dynamic_pte(i,va):
    table=d(i,'space.dynamic_table')
    return words(table,1024)[(va-W)//4096] if table else 0
def audit_resources():
    owned=[]
    counts=[0,0]
    expected=[{},{}]
    for slot in range(16):
        state=r(slot,'state')
        check(state in (0,1),'resource left in reclaiming state')
        if state == 0:
            check(r(slot,'references') == 0 and r(slot,'cap.owner_id') == 0,'free resource retains authority')
            continue
        owner=r(slot,'cap.owner_id')-1
        check(owner in (0,1),'invalid owner')
        check(r(slot,'cap.owner_epoch') == d(owner,'epoch'),'stale domain incarnation')
        pa=r(slot,'physical_address')
        check(pa not in owned and pa not in page_lists[0]+page_lists[1],'duplicated or base-page resource')
        owned.append(pa)
        check(v('states[%d]'%(pa>>12)) == 4,'resource not physically allocated')
        counts[owner]+=1
        refs=0
        for j in range(4):
            va=r(slot,'bindings[%d].address'%j)
            rights=r(slot,'bindings[%d].rights'%j)
            if va:
                refs+=1
                check(W <= va < W+65536 and va%4096 == 0,'binding outside window')
                check(va not in expected[owner],'duplicate virtual binding')
                check(rights in (1,3) and rights & r(slot,'cap.rights') == rights,'rights escalation')
                expected[owner][va]=(pa,5 if rights == 1 else 7)
        check(refs == r(slot,'references'),'reference count mismatch')
        print('RESOURCE:',slot+1,'owner=',owner+1,'epoch=',r(slot,'cap.owner_epoch'),'gen=',r(slot,'cap.generation'),'PFN=',hex(pa>>12),'rights=',hex(r(slot,'cap.rights')),'refs=',refs)
    for i in range(2):
        check(counts[i] == d(i,'resource_count') and counts[i] <= 4,'quota/accounting mismatch')
        table=d(i,'space.dynamic_table')
        directory=words(d(i,'space.pages[0]'),1024)
        if table == 0:
            check(not expected[i] and directory[36] == 0,'bindings without a table')
            continue
        check(table not in owned+page_lists[0]+page_lists[1],'table aliases data/base pages')
        check(directory[36] & 0xfffff007 == table|7,'wrong dynamic PDE')
        check(v('states[%d]'%(table>>12)) == 4,'table not reserved from allocator')
        entries=words(table,1024)
        for j,entry in enumerate(entries):
            va=W+j*4096
            if va not in expected[i]:
                check(entry == 0,'untracked user PTE')
            else:
                pa,flags=expected[i][va]
                check(entry & 0xfffff007 == pa|flags,'PTE authority mismatch')
                print('PTE:',i+1,hex(va),hex(pa),hex(entry))
    print('AUDIT: owned=',len(owned),'counts=',counts,'CR3=',hex(v('$cr3')))
def checkpoint(owner,stage):
    check(v('$cs') == 0x1b and v('$ss') == 0x23,'not executing resource test in CPL3')
    check(v('current_domain->id') == owner+1,'wrong domain at checkpoint')
    check(user_data(owner)[10] == stage,'wrong checkpoint stage')
    check(v('$cr3') == d(owner,'space.pages[0]'),'resource operation under wrong CR3')
    audit_resources()
    data=user_data(owner)
    print('CHECKPOINT:',owner+1,stage,'handle=',data[11:13],'PFN=',hex(data[13]))
    return data[11],data[12],data[13]
def hints_to_b(handle):
    gdb.selected_inferior().write_memory(d(1,'space.pages[4]')+44,struct.pack('<III',*handle))
    # Transfer integers to mount a hostile-handle attempt; no PTE or owner is changed.
def reaped():
    inspect_reaped()
    for i in range(2):
        check(d(i,'resource_count') == 0 and d(i,'space.dynamic_table') == 0,'dynamic resources leaked')
        check(d(i,'inbox.index') == 0 and d(i,'inbox.generation') == 0,'stale transfer notification')
    for slot in range(16):
        check(r(slot,'state') == 0 and r(slot,'references') == 0,'resource survives domain teardown')
        check(r(slot,'cap.owner_id') == 0 and r(slot,'cap.owner_epoch') == 0 and r(slot,'cap.rights') == 0,'dead resource retains authority')
        for j in range(4):
            check(r(slot,'bindings[%d].address'%j) == 0 and r(slot,'bindings[%d].rights'%j) == 0,'stale binding record')
        pa=r(slot,'physical_address')
        if pa:
            check(v('states[%d]'%(pa>>12)) == 2 and not any(words(pa,1024)),'resource not cleared/freed')
    print('PASS: all resource handles dead; dynamic tables and pages cleared/freed')
end
'''


def names_for(mode):
    kind = 'BASIC' if mode == 'basic' else 'TRANSFER' if mode.startswith('transfer') else 'FAULT'
    text = subprocess.check_output(['nm','-n',str(BUILD / f'resource_{kind}.elf')],text=True)
    return {parts[2]:int(parts[0],16) for line in text.splitlines() if len(parts:=line.split())==3}


def checks(mode, owner, stage):
    commands=f'python\nh=checkpoint({owner},{stage})\n'
    if owner == 0 and stage == 1:
        commands+='a=h\nhints_to_b(h)\ncheck(r(a[0]-1,"references") == 2,"aliases missing")\n'
    if mode == 'basic':
        if owner == 1:
            commands+='check(h[2] != a[2],"domains own the same physical page")\n'
        elif stage == 3:
            commands+='check(r(a[0]-1,"state") == 0 and dynamic_pte(0,W+0x3000) == 0 and dynamic_pte(0,W+0x5000) == 0,"release left aliases")\ncheck(not any(words(a[2]*4096,1024)),"release did not clear full page")\n'
        elif stage == 4:
            commands+='check(h[2] == a[2] and h[0] == a[0] and h[1] != a[1],"specific claim/index reuse/generation not honored")\ncheck(dynamic_pte(0,W+0x3000)&7 == 5 and not any(words(h[2]*4096,1024)),"RO reallocation leaked bytes")\n'
    elif mode.startswith('transfer'):
        if owner == 0 and stage == 3:
            commands+='check(r(a[0]-1,"cap.owner_id") == 1 and r(a[0]-1,"cap.generation") == a[1],"failed transfer changed ownership")\ncheck(r(a[0]-1,"references") == 2 and words(a[2]*4096,1)[0] == 0xdeadbeef,"failed transfer revoked/data changed")\n'
        if owner == 0 and stage == 4:
            commands+='check(r(a[0]-1,"cap.owner_id") == 2 and r(a[0]-1,"cap.generation") == a[1]+1,"transfer identity/generation wrong")\ncheck(r(a[0]-1,"references") == 0 and d(0,"space.dynamic_table") == 0,"transfer kept old aliases")\ncheck(not any(words(a[2]*4096,1024)),"transfer leaked private contents")\n'
        if owner == 1 and stage == 5:
            commands+='check(h[2] == a[2] and h[1] != a[1],"receiver authorization wrong")\ncheck(dynamic_pte(1,W+0x3000)&7 == 5,"receiver escalated permissions")\ncheck(not any(words(a[2]*4096,1024)),"receiver read sender secret")\n'
    elif owner == 0 and stage == 6:
        commands+='a=h\n'
        if mode == 'claim-failure':
            commands+='check(d(0,"resource_count") == 0 and d(0,"space.dynamic_table") == 0,"failed claim published state")\n'
        elif mode in ('release-fault','map-failure'):
            commands+='check(r(h[0]-1,"state") == 0 and d(0,"space.dynamic_table") == 0,"release/map failure leaked state")\n'
        elif mode == 'unmap-fault':
            commands+='check(r(h[0]-1,"state") == 1 and r(h[0]-1,"references") == 1,"unmap lost owner or wrong alias count")\ncheck(dynamic_pte(0,W+0x3000) == 0 and dynamic_pte(0,W+0x5000)&1 == 1,"partial unmap/table lifetime wrong")\n'
        elif mode == 'readonly':
            commands+='check(dynamic_pte(0,W+0x3000)&7 == 5,"readonly mapping is writable")\n'
    elif mode == 'generation' and owner == 0:
        commands+='check(r(0,"state") == 0 and r(0,"cap.generation") == 0xffffffff and h[0] != 1,"generation retirement failed")\n'
    if owner == 1 and stage == 8:
        commands+='check(d(0,"state") in (3,4) and d(0,"resource_count") == 0 and d(0,"space.dynamic_table") == 0,"terminated sender retained access")\n'
    return commands+'end\n'


def experiment(qemu,gdb,mode):
    names=names_for(mode)
    is_fault=mode in ('unmap-fault','release-fault','readonly','transfer-fault')
    sequence=([(0,1),(1,2),(0,3),(0,4)] if mode=='basic' else
              [(0,1),(1,2),(0,3),(0,4)] if mode.startswith('transfer') else
              [(0,7)] if mode=='generation' else [(0,6)])
    commands=HELPERS+AUDIT
    for round_number in range(2):
        commands+='delete breakpoints\nhbreak *domains_prepared\ncontinue\npython\ninspect_spaces()\n'
        if mode=='basic' and round_number==1:
            commands+="gdb.selected_inferior().write_memory(d(0,'space.pages[4]')+56,struct.pack('<II',a[0],a[1]))\n"
        if mode=='generation' and round_number==0:
            commands+="check(r(0,'state') == 0,'generation test slot is live')\ngdb.execute('set resources[0].cap.generation=0xfffffffe')\n"
        commands+='end\n'
        commands+=f'delete breakpoints\nhbreak *{names["resource_checkpoint_observed"]:#x}\n'
        for owner,stage in sequence:
            # QEMU/GDB can report a shared virtual checkpoint twice across CR3
            # changes. Stop at the required real domain/stage; retain every
            # original identity, stage, PTE, ownership and cleanup assertion.
            commands+=f'condition $bpnum current_domain->id == {owner+1} && *(unsigned int*)0x08001028 == {stage}\ncontinue\n'+checks(mode,owner,stage)
        if is_fault:
            expected_error=7 if mode=='readonly' else 4
            site=names['resource_write_site' if mode=='readonly' else 'resource_read_site']
            va=0x09005000 if mode=='transfer-fault' else 0x09003000
            commands+='delete breakpoints\nhbreak *domain_fault\ncontinue\npython\n'
            commands+=f"cpu=inspect_trap(0)\ncheck(cpu[8] == 14 and cpu[9] == {expected_error},'resource fault vector/error')\ncheck(cpu[10] == {site} and v('$cr2') == {va},'resource fault EIP/CR2')\nprint('HARDWARE PF:',hex(cpu[10]),hex(v('$cr2')),cpu[9])\nend\n"
            commands+=f'delete breakpoints\nhbreak *{names["resource_checkpoint_observed"]:#x}\n'
        if mode.startswith('transfer'):
            commands+='condition $bpnum current_domain->id == 2 && *(unsigned int*)0x08001028 == 5\ncontinue\n'+checks(mode,1,5)
        elif mode!='basic':
            commands+='condition $bpnum current_domain->id == 2 && *(unsigned int*)0x08001028 == 8\ncontinue\n'+checks(mode,1,8)
        commands+='delete breakpoints\nhbreak *domains_reaped\ncontinue\npython\nreaped()\n'
        commands+=f"check(d(0,'state') == {4 if is_fault else 3},'wrong sender terminal state')\n"
        if not is_fault:
            commands+="check(d(0,'exit_status') == 0,'sender resource test failed')\n"
        commands+="check(d(1,'state') == 3 and d(1,'exit_status') == 0,'peer resource test failed')\nend\n"
    commands+='delete breakpoints\ndetach\nquit\n'
    label='phase4-'+mode
    port=free_port()
    with emulator('debug',qemu,port,append='resource-test='+mode,label=label) as (process,log):
        connect_and_run(process,log,port,gdb,commands,label)
        wait_for(process,log,lambda text:'Phase 4: resource experiment completed' in text,'Phase 4 completion')
        text=log.read_text()
        require('PANIC' not in text,'kernel panic in '+label)
        require(text.count('USER FAULT domain=') == (2 if is_fault else 0),'unexpected domain faults in '+label)
    print('PASS:',label,'ownership, PTEs, generation, hardware access and two clean lifecycles')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',default='qemu-system-i386')
    parser.add_argument('--gdb',default='gdb')
    parser.add_argument('--mode',default='all')
    args=parser.parse_args()
    modes=['basic','transfer','unmap-fault','release-fault','readonly','transfer-fault','exit','generation','map-failure','claim-failure']
    require(args.mode=='all' or args.mode in modes,'unknown mode')
    for mode in modes if args.mode=='all' else [args.mode]:
        experiment(args.qemu,args.gdb,mode)


if __name__=='__main__':
    try:
        main()
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        print('FAIL:',error,file=sys.stderr)
        sys.exit(1)
