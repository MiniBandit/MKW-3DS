#!/usr/bin/env python3
"""Restore pinned public source dependencies; never fetch game files.

Kinoko, Citro3D and CFL ship patched in vendor/. noclip (with
patches/noclip-brres.patch), gclib and cfl-tool (its Mii icon body, read by
tools/generate_native_icon.py) are cloned at their pinned revisions."""
import hashlib,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];deps=json.loads((root/'dependencies.json').read_text())
PATCHES={'noclip':'patches/noclip-brres.patch'}
def patch_sha(path):
 return hashlib.sha256((root/path).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
def git(dest,*args):
 subprocess.run(['git','-C',str(dest),*args],check=True)
def apply_patch(name,dest):
 # Which patch this checkout carries is recorded inside its git directory.
 git(dest,'apply',str(root/PATCHES[name]))
 (dest/'.git'/'mkw-patch.sha').write_text(patch_sha(PATCHES[name])+'\n')
for name,folder in (('noclip','noclip'),('gclib','gclib'),('cfl_tool','cfl-tool')):
 dest=root/'vendor'/folder;entry=deps[name]
 if dest.exists():
  actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
  if actual!=entry['commit']:raise SystemExit(f'{folder}: unexpected revision; refusing to overwrite existing checkout')
  if name in PATCHES:
   # A builder folder reused across versions: when the patch changed, return the
   # pinned checkout to its clean state and apply the current patch (it is
   # third-party source; nothing of the player's lives there).
   marker=dest/'.git'/'mkw-patch.sha'
   if not marker.is_file() or marker.read_text().strip()!=patch_sha(PATCHES[name]):
    print(f'{folder}: updating to this builder\'s patch')
    git(dest,'checkout','--force','--detach',entry['commit']);git(dest,'clean','-fdq')
    apply_patch(name,dest)
  continue
 subprocess.run(['git','clone','--no-checkout',entry['url'],str(dest)],check=True)
 git(dest,'checkout','--detach',entry['commit'])
 if name in PATCHES:apply_patch(name,dest)
print('Source dependencies ready. Run npm ci and install requirements.txt into a project virtualenv.')
