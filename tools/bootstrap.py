#!/usr/bin/env python3
"""Restore pinned public source dependencies; never fetch game files."""
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];deps=json.loads((root/'dependencies.json').read_text())
kinoko_patch=root/'patches/kinoko-arm11.patch'
for name in ('kinoko','noclip','gclib','citro3d'):
 dest=root/'vendor'/name;entry=deps[name]
 if dest.exists():
  actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
  if actual!=entry['commit']:raise SystemExit(f'{name}: unexpected revision; refusing to overwrite existing checkout')
  if name=='citro3d':
   subprocess.run(['git','-C',str(dest),'apply','--reverse','--check',str(root/entry['patch'])],check=True)
  # A builder unzipped over an older one still has the older kinoko patch:
  # stash it (git stash list keeps it) and apply this version's.
  if name=='kinoko' and subprocess.run(['git','-C',str(dest),'apply','--reverse','--check',str(kinoko_patch)],capture_output=True).returncode:
   print('kinoko: patch is from another builder version; stashing it and applying this one')
   subprocess.run(['git','-c','user.name=mkw-3ds','-c','user.email=mkw-3ds@localhost','-C',str(dest),'stash','push','--include-untracked','-m','older kinoko-arm11.patch'],check=True)
   subprocess.run(['git','-C',str(dest),'apply',str(kinoko_patch)],check=True)
  continue
 subprocess.run(['git','clone','--no-checkout',entry['url'],str(dest)],check=True)
 subprocess.run(['git','-C',str(dest),'checkout','--detach',entry['commit']],check=True)
 if name=='kinoko':subprocess.run(['git','-C',str(dest),'apply',str(kinoko_patch)],check=True)
 if name=='citro3d':subprocess.run(['git','-C',str(dest),'apply',str(root/entry['patch'])],check=True)
print('Source dependencies ready. Run npm ci and install requirements.txt into a project virtualenv.')
