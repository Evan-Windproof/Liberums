#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the documented release checks, recording actual exit codes and input hashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def source_inputs():
    result={}
    for directory in ('arch','kernel','include','user','examples','scripts','tests'):
        for path in (ROOT/directory).rglob('*'):
            if path.is_file() and not {'build','__pycache__'}&set(path.relative_to(ROOT).parts):
                result[str(path.relative_to(ROOT))]=sha(path)
    for name in ('Makefile','LICENSE','VERSION'):result[name]=sha(ROOT/name)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--aelthuvor-build',type=Path)
    p.add_argument('--historical-checkouts',type=Path,nargs=3,metavar=('PHASE3','PHASE2','ABI11'))
    a=p.parse_args();initial=source_inputs();records=[]
    commands=[['make','clean'],['make','-j2'],['make','test'],['make','test-phase8'],['make','test-phase9'],['make','test-libos-examples'],['python3','tests/kit_tests.py'],['python3','scripts/check_docs.py']]
    if a.aelthuvor_build:
        b=a.aelthuvor_build.resolve();repo=b.parent
        commands += [['make','test-phase9-e2e','PHASE9_AEL_BUILD='+str(b)],
            ['python3','tests/phase10_compat.py','--aelthuvor',str(b)],
            ['make','-C',str(repo),'phase4-integration','LIBERUMS='+str(ROOT)],
            ['make','-C',str(repo),'integration','LIBERUMS='+str(ROOT)],
            ['make','-C',str(repo),'console-integration','LIBERUMS='+str(ROOT)]]
    if a.historical_checkouts:
        phase3,phase2,old=a.historical_checkouts
        for repo,target in ((phase3,'integration'),(phase3,'console-integration'),(phase2,'integration')):
            commands.append(['make','-C',str(repo.resolve()),target,'LIBERUMS='+str(ROOT)])
        commands.append(['make','test-vga-compat','AELTHUVOR='+str(old.resolve())])
    kernel=None
    for n,command in enumerate(commands):
        print('RUN',shlex.join(command),flush=True)
        output=subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        logs=ROOT/'build/release-checks';logs.mkdir(parents=True,exist_ok=True)
        log=logs/('%02d.log'%n)
        text='\n'.join(line.rstrip() for line in output.stdout.decode().splitlines()).rstrip()+'\n'
        log.write_text(text)
        records.append(dict(command=command,returncode=output.returncode,log=str(log.relative_to(ROOT)),log_sha256=sha(log)))
        if n==1 and output.returncode==0:kernel=sha(ROOT/'build/liberums.elf')
        report=dict(commands=records,complete=False,source_inputs_sha256=initial,kernel_sha256=kernel)
        if output.returncode:
            (ROOT/'build/release-validation.json').write_text(json.dumps(report,indent=2)+'\n')
            print('FAILED: see',log,file=sys.stderr);sys.exit(output.returncode)
        if kernel and sha(ROOT/'build/liberums.elf')!=kernel:raise RuntimeError('a test replaced the frozen kernel')
    if source_inputs()!=initial:raise RuntimeError('validated source inputs changed during the suite')
    tools={}
    prefix=os.environ.get('CROSS_COMPILE','')
    for name,command in {'gcc':prefix+'gcc','ld':prefix+'ld','make':'make','qemu':'qemu-system-i386','gdb':'gdb'}.items():
        tools[name]=subprocess.check_output([command,'--version'],text=True).splitlines()[0]
    tools['python']=sys.version.split()[0]
    report.update(complete=True,tools=tools,external_phase4=bool(a.aelthuvor_build),historical_matrix=bool(a.historical_checkouts))
    (ROOT/'build/release-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: release validation;',len(records),'commands, actual return codes all zero; kernel SHA256',kernel)
if __name__=='__main__':main()
