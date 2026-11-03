#!/usr/bin/env python3
"""Migrate upstream cook_battle's two original balloon models without recooking UI."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
from cook_model import cook
ROOT=Path(__file__).resolve().parents[1]
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--output',type=Path,default=ROOT/'build/battle-balloon-assets')
 args=parser.parse_args()
 source=ROOT/'assets/common/balloon.brres';before=hashlib.sha256(source.read_bytes()).hexdigest()
 out=args.output.resolve();rows=[]
 for model,color in [('ballon','red'),('ballon_b','blue')]:
  name='battle_balloon_'+color;work=out/'exports'/name;work.mkdir(parents=True,exist_ok=True)
  env={**os.environ,'MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_FREEZE_NONLINEAR_UV':'1'}
  subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work),model],env=env,check=True,stdout=subprocess.DEVNULL)
  parts=cook(work,out,name,256,texture_prefix=name)
  if len(parts)!=1:raise ValueError('Expected one original balloon mesh')
  target=out/'models'/(name+'.gmdl');shutil.copyfile(out/'models'/(parts[0]['name']+'.gmdl'),target)
  rows.append({'model':name,'sourceModel':model,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
 assert hashlib.sha256(source.read_bytes()).hexdigest()==before
 files=[]
 for folder in ['models','textures']:
  for file in sorted((out/folder).iterdir()):
   if file.suffix not in ['.gmdl','.t3x']:continue
   if file.suffix=='.gmdl' and file.stem.endswith('_000'):continue
   relative=file.relative_to(out)
   files.append({'path':relative.as_posix(),'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'bytes':file.stat().st_size})
 (out/'receipt.json').write_text(json.dumps({'upstream':'432e913 cook_battle.py','sourceSha256':before,'models':rows,'files':files},indent=2)+'\n')
 print('Staged original red/blue balloon meshes:',out)
if __name__=='__main__':main()
