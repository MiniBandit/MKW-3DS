#!/usr/bin/env python3
"""Restore pinned public source dependencies; never fetch game files."""
import hashlib,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];deps=json.loads((root/'dependencies.json').read_text())
def patch_sha(path):
 return hashlib.sha256((root/path).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
def git(dest,*args):
 subprocess.run(['git','-C',str(dest),*args],check=True)
def apply_kinoko_patch(dest):
 # Which patch this checkout carries is recorded inside its git directory.
 git(dest,'apply',str(root/'patches/kinoko-arm11.patch'))
 (dest/'.git'/'mkw-patch.sha').write_text(patch_sha('patches/kinoko-arm11.patch')+'\n')
for name in ('kinoko','noclip','gclib','citro3d'):
 dest=root/'vendor'/name;entry=deps[name]
 if dest.exists():
  actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
  if actual!=entry['commit']:raise SystemExit(f'{name}: unexpected revision; refusing to overwrite existing checkout')
  if name=='citro3d':
   subprocess.run(['git','-C',str(dest),'apply','--reverse','--check',str(root/entry['patch'])],check=True)
  if name=='kinoko':
   # A builder folder reused across versions: when the patch changed, return the
   # pinned checkout to its clean state and apply the current patch (it is
   # third-party source; nothing of the player's lives there).
   marker=dest/'.git'/'mkw-patch.sha'
   if not marker.is_file() or marker.read_text().strip()!=patch_sha('patches/kinoko-arm11.patch'):
    print('kinoko: updating to this builder\'s patch')
    git(dest,'checkout','--force','--detach',entry['commit']);git(dest,'clean','-fdq')
    apply_kinoko_patch(dest)
  continue
 subprocess.run(['git','clone','--no-checkout',entry['url'],str(dest)],check=True)
 subprocess.run(['git','-C',str(dest),'checkout','--detach',entry['commit']],check=True)
 if name=='kinoko':apply_kinoko_patch(dest)
 if name=='citro3d':subprocess.run(['git','-C',str(dest),'apply',str(root/entry['patch'])],check=True)
print('Source dependencies ready. Run npm ci and install requirements.txt into a project virtualenv.')
