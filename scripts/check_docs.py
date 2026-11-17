#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check every local Markdown destination; external URLs require manual review."""
from pathlib import Path
import re
import sys
from urllib.parse import unquote
ROOT=Path(__file__).resolve().parents[1]
def main():
    files=[p for p in ROOT.rglob('*.md') if not {'build','.git','__pycache__'}&set(p.relative_to(ROOT).parts)]
    checked=0;errors=[]
    for path in files:
        # Exclude command/code examples containing illustrative Markdown.
        text=re.sub(r'```.*?```','',path.read_text(),flags=re.S)
        for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)',text):
            target=target.strip()
            if target.startswith('<'):target=target[1:target.index('>')]
            else:target=target.split(' "',1)[0]
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',target) or target.startswith('#'):continue
            local=unquote(target.split('#',1)[0])
            if not local:continue
            resolved=(path.parent/local).resolve();checked+=1
            if not resolved.exists():errors.append(str(path.relative_to(ROOT))+': '+target)
    for error in errors:print('BROKEN:',error,file=sys.stderr)
    if errors:sys.exit(1)
    print('PASS:',len(files),'Markdown files;',checked,'local destinations resolve; external URLs not network-validated')
if __name__=='__main__':main()
