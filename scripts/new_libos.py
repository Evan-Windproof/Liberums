#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create a standalone project in an empty directory from public teaching files."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
def create(output,sdk,example):
    output=Path(output).resolve();sdk=Path(sdk).resolve()
    manifest=json.loads((sdk/'manifest.json').read_text())
    if manifest.get('name')!='Liberums Kernel ABI SDK': raise ValueError('not a Kernel ABI SDK')
    for name,digest in manifest['sha256'].items():
        if Path(name).name!=name or hashlib.sha256((sdk/'include/abi'/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('SDK manifest/header mismatch: '+name)
    if output.exists() and any(output.iterdir()): raise ValueError('destination must be empty')
    output.mkdir(parents=True,exist_ok=True)
    source=ROOT/'examples/libos'/example
    for path in source.iterdir():
        if path.is_file(): shutil.copy2(path,output/path.name)
    shutil.copytree(ROOT/'examples/libos/common',output/'support')
    shutil.copytree(sdk,output/'sdk')
    make=output/'Makefile'
    make.write_text('SDK ?= sdk\n'+make.read_text().replace('LIBOS_SUPPORT ?= ../common','LIBOS_SUPPORT ?= support'))
    readme=output/'README.md'
    if readme.exists():
        version=(ROOT/'VERSION').read_text().strip()
        readme.write_text(readme.read_text().replace('../../../docs/',
            'https://github.com/Evan-Windproof/Liberums/blob/v'+version+'/docs/'))
    shutil.copy2(ROOT/'LICENSE',output/'LICENSE')
    (output/'PROJECT.md').write_text('# Your independent LibOS\n\nRun `make`, then load build/libos.elf in Liberums.\n'
        'All sources and the public SDK are local. No kernel checkout is read by this build.\n'
        'Change main.c or add your own runtime and allocation policy.\n')
    print('Created independent',example,'project:',output)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--example',choices=('minimal','memory-policy','console'),default='minimal');a=p.parse_args()
    create(a.output,a.sdk,a.example)
if __name__=='__main__':main()
