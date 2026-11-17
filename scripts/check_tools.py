#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small, explicit prerequisite diagnostics; never installs tools or uses a network."""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

def check(command,flags):
    argv=shlex.split(command)
    if not argv or not shutil.which(argv[0]): raise RuntimeError('Missing '+command+'; see README prerequisites')
    result=subprocess.run(argv+flags,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError(command+' failed: '+result.stdout.strip())
    return result.stdout

def main():
    p=argparse.ArgumentParser();p.add_argument('--test',action='store_true');p.add_argument('--cc',default='gcc');p.add_argument('--ld',default='ld')
    p.add_argument('--qemu',default='qemu-system-i386');p.add_argument('--gdb',default='gdb');a=p.parse_args()
    check(a.cc,['--version']);check(a.ld,['--version'])
    if 'elf_i386' not in check(a.ld,['-V']): raise RuntimeError('GNU ld needs elf_i386 support')
    with tempfile.TemporaryDirectory(prefix='liberums-tools-') as tmp:
        src=Path(tmp)/'check.c';src.write_text('#include <stdint.h>\n_Static_assert(sizeof(uintptr_t)==4,"i686");\nint f(void){return 0;}\n')
        check(a.cc,['-m32','-ffreestanding','-fno-pie','-c',str(src),'-o',str(Path(tmp)/'check.o')])
    if a.test:
        check(a.qemu,['--version']);check(a.gdb,['-q','-nx','-batch','-ex','python import gdb'])
    print('PASS: freestanding i686 toolchain'+(' / QEMU / GDB Python' if a.test else ''))
if __name__=='__main__':
    try: main()
    except (OSError,RuntimeError) as e: print('ERROR:',e,file=sys.stderr);sys.exit(1)
