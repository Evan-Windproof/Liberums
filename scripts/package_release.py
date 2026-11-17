#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create deterministic source/SDK/example archives for a verified Git commit."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from export_kernel_sdk import export,ROOT,git

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def compress(raw,target,epoch):
    with target.open('wb') as out:
        with gzip.GzipFile(filename='',mode='wb',fileobj=out,mtime=epoch) as stream:stream.write(raw)
def directory_tar(directory,prefix):
    data=io.BytesIO()
    with tarfile.open(fileobj=data,mode='w') as archive:
        for path in sorted(directory.rglob('*')):
            if not path.is_file():continue
            info=tarfile.TarInfo(prefix+'/'+str(path.relative_to(directory)))
            content=path.read_bytes();info.size=len(content);info.mode=0o644
            archive.addfile(info,io.BytesIO(content))
    return data.getvalue()
def package(revision,version,output,validation,kernel):
    commit=git('rev-parse',revision+'^{commit}').decode().strip()
    if git('show',commit+':VERSION').decode().strip()!=version:raise ValueError('VERSION does not match requested release')
    report=json.loads(validation.read_text())
    if not report.get('complete') or any(r['returncode'] for r in report['commands']):raise ValueError('complete passing validation is required')
    for name,digest in report['source_inputs_sha256'].items():
        if hashlib.sha256(git('show',commit+':'+name)).hexdigest()!=digest:raise ValueError('validated input differs from commit: '+name)
    if sha(kernel)!=report['kernel_sha256']:raise ValueError('kernel does not match the validated ELF')
    epoch=int(git('show','-s','--format=%ct',commit));output.mkdir(parents=True,exist_ok=True)
    prefix='Liberums-v'+version
    allowed={prefix+'-source.tar.gz',prefix+'-sdk.tar.gz',prefix+'-examples.tar.gz','RELEASE-NOTES.md','validation.json','liberums-i686.elf','manifest.json','SHA256SUMS'}
    if set(p.name for p in output.iterdir())-allowed:raise ValueError('release output contains unrelated files')
    raw=git('archive','--format=tar','--prefix='+prefix+'/',commit)
    compress(raw,output/(prefix+'-source.tar.gz'),epoch)
    with tempfile.TemporaryDirectory(prefix='liberums-package-') as tmp:
        sdk=Path(tmp)/'sdk';export_manifest=export(sdk,commit)
        compress(directory_tar(sdk,prefix+'-sdk'),output/(prefix+'-sdk.tar.gz'),epoch)
    raw=git('archive','--format=tar','--prefix='+prefix+'/',commit,'examples/libos')
    compress(raw,output/(prefix+'-examples.tar.gz'),epoch)
    (output/'RELEASE-NOTES.md').write_bytes(git('show',commit+':RELEASE-NOTES.md')+('\nSource commit: `'+commit+'`\n').encode())
    shutil.copyfile(validation,output/'validation.json')
    shutil.copyfile(kernel,output/'liberums-i686.elf')
    files=sorted(p for p in output.iterdir() if p.name not in ('manifest.json','SHA256SUMS'))
    manifest=dict(name='Liberums Educational Release',version=version,source_commit=commit,source_epoch=epoch,
        abi=export_manifest['abi'],license='GPL-3.0-or-later',kernel_sha256=sha(kernel),toolchain=report.get('tools',{}),
        note='Teaching/ABI baseline, not a production security or universal hardware guarantee.',
        sha256={p.name:sha(p) for p in files})
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    allfiles=sorted(p for p in output.iterdir() if p.name!='SHA256SUMS')
    (output/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in allfiles))
    print('Prepared release:',output,'source commit:',commit)
    print('These files are not a published GitHub Release.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--revision',default='HEAD');p.add_argument('--version',required=True)
    p.add_argument('--output',type=Path,default=ROOT/'build/release');p.add_argument('--validation',type=Path,default=ROOT/'build/release-validation.json')
    p.add_argument('--kernel',type=Path,default=ROOT/'build/liberums.elf');a=p.parse_args()
    package(a.revision,a.version,a.output,a.validation,a.kernel)
if __name__=='__main__':main()
