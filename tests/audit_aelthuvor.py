#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Baseline-specific ABI 1.1 LibOS audit against Phase 6; changes no guest bytes.

Build both repositories and Aelthuvor's integration variants first. The original
integration target is expected to reject current-header byte drift. This separate
audit retains SDK hashes/provenance and every hardware/lifecycle check, adapting
only the historical GDB assertion that the running kernel must report ABI 1.1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
import libos
from phase1 import ROOT, BUILD, require, emulator, wait_for
from phase2 import connect_and_run, free_port
from phase3 import HELPERS
from phase5 import GDB_HELPERS, headers, symbols
from phase6 import reject_cooperative_note

KERNEL = '85a493466afe2038ca22a9870f6f9ce37dd7f3a8'
LIBOS = 'b10d337d254b18e98204990203a77a215c82c568'
SDK = '49d5dbf2e78f7ad7ed6ec4acdeece68b1b6568d0'


def git(directory, *args):
    return subprocess.check_output(['git', '-C', str(directory), *args])


def peer_probe(image, mode, qemu, gdb):
    """Give the existing peer a hostile user input; do not alter guest code.

    GDB writes only the peer's own user data through its physical alias before
    scheduling. Its existing USER_CROSS_READ/WRITE path then executes the actual
    access, while the real external Aelthuvor resource is still live.
    """
    blob = image.read_bytes()
    names = symbols(image)
    peer = symbols(BUILD/'user_b.elf')
    commands = HELPERS + GDB_HELPERS + libos.LIBOS_HELPERS
    commands += 'add-symbol-file "'+str(image)+'"\n'
    for iteration in range(2):
        commands += 'delete breakpoints\nhbreak *external_prepared\ncontinue\npython\n'
        commands += "resource_pages=[]\nresource_tables=[]\ncheck(v('external_load_result')==0,'LibOS rejected')\n"
        commands += f'inspect_external({headers(blob)!r},{blob!r})\n'
        commands += f"gdb.selected_inferior().write_memory(d(1,'space.pages[4]')+4,struct.pack('<II',{mode},0x09000000))\n"
        commands += "print('AUDIT INPUT: peer user data selects hostile access to 0x09000000')\nend\n"
        commands += f"delete breakpoints\nhbreak *{names['ael_checkpoint']:#x}\ncontinue\ncontinue\npython\n"
        commands += "check(words(v('$esp')+4,1)[0]==2,'allocator checks not finished')\n"
        commands += f"inspect_libos({0 if image.stem == 'first' else 1})\nend\n"
        commands += 'delete breakpoints\nhbreak *domain_fault\ncontinue\npython\n'
        commands += "check(v('current_domain->id')==2,'wrong faulting domain')\ncpu=inspect_trap(1)\n"
        error = 4 if mode == 3 else 6
        fault = peer['user_fault_cross_read' if mode == 3 else 'user_fault_cross_write']
        commands += f"check(cpu[8]==14 and cpu[9]=={error} and cpu[10]=={fault},'peer fault instruction/error mismatch')\n"
        commands += "check(v('$cr2')==0x09000000,'peer did not attempt LibOS resource access')\n"
        commands += f"print('AUDIT PEER PF: vector=14 error={error} CR2=0x09000000 EIP=',hex(cpu[10]))\nend\n"
        commands += 'delete breakpoints\nhbreak *external_reaped\ncontinue\npython\ninspect_external_reaped()\n'
        commands += f"check(v('external_completed_rounds')=={iteration+1},'wrong lifecycle')\n"
        commands += "check(d(0,'state')==3 and d(0,'exit_status')==0,'LibOS did not survive hostile peer')\n"
        commands += "check(d(1,'state')==4 and d(1,'fault_vector')==14,'hostile peer not isolated')\n"
        commands += "for page in resource_pages+resource_tables:\n    check(v('states[%d]'%(page>>12))==2 and not any(words(page,1024)),'resource/table not zero/free')\n"
        commands += "print('AUDIT PEER: Aelthuvor exit=0; hostile peer faulted; all pages zero/free')\nend\n"
    commands += 'delete breakpoints\ndetach\nquit\n'
    label = 'audit-aelthuvor-'+image.stem+('-peer-read' if mode == 3 else '-peer-write')
    port = free_port()
    with emulator('debug', qemu, port, append='external', initrd=image, label=label) as (process, log):
        connect_and_run(process, log, port, gdb, commands, label)
        wait_for(process, log, lambda t: 'Phase 5: external execution completed' in t,
                 'LibOS with hostile peer')
        require('PANIC' not in log.read_text(), 'hostile peer panicked kernel')
    print('PASS:', label, 'two actual denied-access lifecycles', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aelthuvor', type=Path, required=True)
    parser.add_argument('--qemu', default='qemu-system-i386')
    parser.add_argument('--gdb', default='gdb')
    args = parser.parse_args()
    directory = args.aelthuvor.resolve()
    require(git(directory, 'rev-parse', 'HEAD').decode().strip() == LIBOS,
            'this audit requires the reviewed Aelthuvor commit')
    require(not git(directory, 'status', '--porcelain', '--untracked-files=no'),
            'Aelthuvor tracked files changed')
    # Allow documentation/audit-only descendants, but no changed kernel or
    # historical test helpers. The exact baseline must exist in local history.
    git(ROOT, 'diff', '--exit-code', KERNEL, '--', 'kernel', 'arch', 'include',
        'user', 'Makefile', 'tests', ':(exclude)tests/audit_aelthuvor.py')
    subprocess.run([sys.executable, str(directory/'tests/check_sdk.py')], check=True)
    manifest = json.loads((directory/'sdk/manifest.json').read_text())
    require(manifest['commit'] == SDK and manifest['abi'] == {'major': 1, 'minor': 1},
            'unreviewed SDK provenance')
    for name, digest in manifest['sha256'].items():
        data = (directory/'sdk/include/abi'/name).read_bytes()
        require(hashlib.sha256(data).hexdigest() == digest, 'SDK digest: '+name)
        require(git(ROOT, 'show', SDK+':include/abi/'+name) == data,
                'pinned SDK source mismatch: '+name)
        current = (ROOT/'include/abi'/name).read_bytes()
        print('SDK:', name, digest, 'current-identical=', current == data, flush=True)
    variants = ('first', 'best', 'fault', 'readonly', 'exit-live', 'runtime-abi')
    images = {name: directory/'build'/(name+'.elf') for name in variants}
    before = {name: hashlib.sha256(p.read_bytes()).hexdigest() for name, p in images.items()}
    print('Kernel source baseline:', KERNEL, 'LibOS:', LIBOS, flush=True)
    for name, digest in before.items():
        print('ELF SHA256:', name, digest, flush=True)
    old = "v('ael_runtime.version') == 0x10001"
    require(libos.LIBOS_HELPERS.count(old) == 1, 'historical GDB oracle changed')
    libos.LIBOS_HELPERS = libos.LIBOS_HELPERS.replace(
        old, "v('ael_runtime.version') == 0x10002", 1)
    libos.LIBOS_HELPERS = libos.LIBOS_HELPERS.replace(
        "    va=v('ael_demo_memory')",
        "    print('AUDIT ABI: version=',hex(v('ael_runtime.version')),'features=',v('ael_runtime.features'))\n"
        "    check(d(0,'preemptions')==0 and v('$eflags')&0x200==0,'cooperative behavior changed')\n"
        "    va=v('ael_demo_memory')", 1)
    print('AUDIT: only ABI version oracle adapted to 1.2; all SDK, PTE, CPL3, '
          'allocator, peer, zeroing and reclamation assertions retained', flush=True)
    libos.suite(directory/'build', args.qemu, args.gdb)
    # Unmodified original binaries require COOPERATIVE, absent in preempt mode.
    for name in ('first', 'best'):
        reject_cooperative_note(args.qemu, args.gdb, images[name])
        for suffix in ('.gdb', '-gdb.log', '-serial.log'):
            shutil.copy2(BUILD/('phase6-cooperative-note-rejected'+suffix),
                         BUILD/('audit-aelthuvor-'+name+'-preempt-rejected'+suffix))
        for mode in (3, 4):
            peer_probe(images[name], mode, args.qemu, args.gdb)
    after = {name: hashlib.sha256(p.read_bytes()).hexdigest() for name, p in images.items()}
    require(before == after, 'external ELF changed during audit')
    require(not git(directory, 'status', '--porcelain', '--untracked-files=no'),
            'audit changed Aelthuvor tracked files')
    print('PASS: ABI 1.1 Aelthuvor on ABI 1.2 cooperative kernel; '
          '9 integration guests / 18 lifecycles plus 2 preempt rejection guests / '
          '4 lifecycles plus 4 hostile-peer guests / 8 lifecycles. '
          'No Aelthuvor revocation handling claimed.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
