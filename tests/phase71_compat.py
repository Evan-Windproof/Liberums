#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run unchanged historical Aelthuvor ELF32 against the Phase 7.1 kernel.

Accepts an already-built checkout of the reviewed commit. Never builds, edits or
pushes Aelthuvor. Old SDK provenance and all existing GDB checks are retained;
only the obsolete exact ABI 1.1 runtime-version expectation is adapted to 1.2,
exactly as in the Phase 6 independent integration audit.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import libos
from audit_aelthuvor import LIBOS, SDK, git, peer_probe
from phase1 import ROOT, BUILD, require
from phase6 import reject_cooperative_note


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aelthuvor', type=Path, required=True)
    parser.add_argument('--qemu', default='qemu-system-i386')
    parser.add_argument('--gdb', default='gdb')
    args = parser.parse_args()
    directory = args.aelthuvor.resolve()
    require(git(directory,'rev-parse','HEAD').decode().strip()==LIBOS,'unreviewed Aelthuvor commit')
    require(not git(directory,'status','--porcelain','--untracked-files=no'),'Aelthuvor source changed')
    subprocess.run(['python3',str(directory/'tests/check_sdk.py')],cwd=directory,check=True)
    manifest=json.loads((directory/'sdk/manifest.json').read_text())
    require(manifest['commit']==SDK and manifest['abi']=={'major':1,'minor':1},'SDK provenance changed')
    for name,digest in manifest['sha256'].items():
        data=(directory/'sdk/include/abi'/name).read_bytes()
        require(hashlib.sha256(data).hexdigest()==digest,'SDK hash mismatch')
        require(git(ROOT,'show',SDK+':include/abi/'+name)==data,'SDK does not match pinned source')
    images={name:directory/'build'/(name+'.elf') for name in
            ('first','best','fault','readonly','exit-live','runtime-abi')}
    before={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in images.items()}
    old="v('ael_runtime.version') == 0x10001"
    require(libos.LIBOS_HELPERS.count(old)==1,'historical version oracle changed')
    libos.LIBOS_HELPERS=libos.LIBOS_HELPERS.replace(old,"v('ael_runtime.version') == 0x10002",1)
    libos.suite(directory/'build',args.qemu,args.gdb)
    for name in ('first','best'):
        reject_cooperative_note(args.qemu,args.gdb,images[name])
        for mode in (3,4):
            peer_probe(images[name],mode,args.qemu,args.gdb)
    require(before=={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in images.items()},'ELF bytes changed')
    require(not git(directory,'status','--porcelain','--untracked-files=no'),'Aelthuvor source changed')
    (BUILD/'phase71-compat.json').write_text(json.dumps(dict(source_commit=LIBOS,sdk_commit=SDK,
        elf_sha256=before,guests=15,lifecycles=30),indent=2)+'\n')
    print('PASS: Phase 7.1 unchanged Aelthuvor compatibility; 15 guests / 30 lifecycles; source and ELF bytes preserved')


if __name__=='__main__':
    main()
