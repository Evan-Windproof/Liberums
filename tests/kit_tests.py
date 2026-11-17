#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Kit packaging/diagnostic checks, separate from actual QEMU execution."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def run(*args,ok=True):
    p=subprocess.run(list(args),cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert (p.returncode==0)==ok,p.stdout
    return p
with tempfile.TemporaryDirectory(prefix='liberums-sdk-test-') as temp:
    base=Path(temp);sdk=base/'sdk'
    run(sys.executable,'scripts/export_kernel_sdk.py','--output',str(sdk))
    manifest=json.loads((sdk/'manifest.json').read_text())
    assert len(manifest['sha256'])==6
    for name,digest in manifest['sha256'].items():
        assert hashlib.sha256((ROOT/'include/abi'/name).read_bytes()).hexdigest()==digest
        assert (sdk/'include/abi'/name).read_bytes()==(ROOT/'include/abi'/name).read_bytes()
    stamp=(sdk/'include/abi/elf.h').stat().st_mtime_ns
    run(sys.executable,'scripts/export_kernel_sdk.py','--output',str(sdk))
    assert stamp==(sdk/'include/abi/elf.h').stat().st_mtime_ns,'unchanged SDK header rewritten'
    # A source archive without .git still exports/builds, without another checkout.
    source=base/'source';(source/'scripts').mkdir(parents=True)
    shutil.copy2(ROOT/'scripts/export_kernel_sdk.py',source/'scripts/export_kernel_sdk.py')
    shutil.copytree(ROOT/'include/abi',source/'include/abi');shutil.copy2(ROOT/'LICENSE',source/'LICENSE')
    run(sys.executable,str(source/'scripts/export_kernel_sdk.py'),'--output',str(base/'archive-sdk'))
    assert json.loads((base/'archive-sdk/manifest.json').read_text())['source_commit'] is None
    missing=run(sys.executable,'scripts/check_tools.py','--cc','liberums-missing-compiler',ok=False)
    assert 'Missing liberums-missing-compiler' in missing.stdout
    bad=run('make','-C','examples/libos/minimal','SDK='+str(base/'absent'),ok=False)
    assert 'Set SDK' in bad.stdout
    project=base/'project';project.mkdir();(project/'keep').write_text('user content')
    run(sys.executable,'scripts/new_libos.py','--sdk',str(sdk),'--output',str(project),ok=False)
    assert (project/'keep').read_text()=='user content'
    (sdk/'include/abi/elf.h').write_text('corrupted public header')
    run(sys.executable,'scripts/new_libos.py','--sdk',str(sdk),'--output',str(base/'rejected'),ok=False)
print('PASS: canonical six-header SDK, stable exports, source archive without Git, missing tools/SDK, preserved directory, corrupt SDK rejection')
