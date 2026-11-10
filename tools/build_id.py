#!/usr/bin/env python3
"""The online build fingerprint: which source code a game was built from.

Every build made from the same source has the same fingerprint, on any
computer: it hashes the game's code (src/, its own headers in include/mkw,
Kinoko's physics in vendor/kinoko/source) and tools/release/VERSION, with
line endings normalised. Headers converted from the player's disc are left
out. Changing the game changes the fingerprint, so the online server can
accept only the builds listed in its allowed-builds file.

    python3 tools/build_id.py      prints this source tree's fingerprint
"""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def build_id(root=ROOT):
    root=Path(root)
    generated=set(json.loads((root/'config/generated-inputs.json').read_text())['files'])
    paths=[root/'tools/release/VERSION']
    paths+=sorted(p for p in (root/'src').rglob('*') if p.suffix in ('.cpp','.c','.h','.hpp','.s','.v','.pica'))
    paths+=sorted(p for p in (root/'include/mkw').rglob('*') if p.suffix in ('.h','.hpp') and p.relative_to(root/'include').as_posix() not in generated)
    paths+=sorted(p for p in (root/'vendor/kinoko/source').rglob('*') if p.suffix in ('.cc','.hh','.cpp','.h','.hpp'))
    digest=hashlib.sha256()
    for p in paths:
        if p.name.startswith('._') or not p.is_file():continue
        digest.update(p.relative_to(root).as_posix().encode()+b'\0'+p.read_bytes().replace(b'\r\n',b'\n')+b'\0')
    return digest.hexdigest()[:16]

if __name__=='__main__':print(build_id())
