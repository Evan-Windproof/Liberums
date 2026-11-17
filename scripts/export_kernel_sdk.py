#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export the one canonical public ABI; no runtime, kernel headers or LibOS API."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
HEADERS=('elf.h','event.h','keyboard.h','resource.h','syscall.h','vga.h')

def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args],stderr=subprocess.PIPE)

def export(output,revision=None):
    output=Path(output).resolve()
    if output==ROOT or output in ROOT.parents:
        raise ValueError('SDK output must be a separate directory')
    try:
        commit=git('rev-parse',revision or 'HEAD').decode().strip()
    except (subprocess.CalledProcessError,FileNotFoundError):
        if revision: raise ValueError('--revision requires a Git checkout')
        commit=None
    read=lambda name: git('show',commit+':'+name) if revision else (ROOT/name).read_bytes()
    directory=output/'include/abi'
    directory.mkdir(parents=True,exist_ok=True)
    extra=set(p.name for p in directory.iterdir())-set(HEADERS)
    if extra: raise ValueError('unexpected SDK headers: '+str(sorted(extra)))
    hashes={}
    for name in HEADERS:
        data=read('include/abi/'+name)
        if not (directory/name).exists() or (directory/name).read_bytes()!=data: (directory/name).write_bytes(data)
        hashes[name]=hashlib.sha256(data).hexdigest()
    (output/'LICENSE').write_bytes(read('LICENSE'))
    header=read('include/abi/syscall.h').decode()
    version={name:int(re.search(r'#define LIBERUMS_ABI_'+name.upper()+r' (\d+)u',header)[1]) for name in ('major','minor')}
    manifest=dict(name='Liberums Kernel ABI SDK',abi=version,license='GPL-3.0-or-later',
        repository='https://github.com/Evan-Windproof/Liberums',source_commit=commit,
        export_mode='git-revision' if revision else ('working-tree' if commit else 'source-directory'),sha256=hashes)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    (output/'README.md').write_text('# Liberums Kernel ABI SDK\n\n'
        'Only public ABI headers from include/abi/, with their original license.\n'
        'Use -I/path/to/sdk/include. No libc, kernel private header or Aelthuvor is required.\n'
        'ABI 1.2 and its optional device calls are described in docs/public-abi.md in the source release.\n'
        'This SDK does not implement an application allocator or runtime.\n'
        'manifest.json records the source revision and SHA256 of every public header.\n')
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--revision')
    a=p.parse_args();m=export(a.output,a.revision)
    print('Exported Kernel ABI 1.2 SDK:',a.output,'headers=',len(m['sha256']))
if __name__=='__main__':main()
