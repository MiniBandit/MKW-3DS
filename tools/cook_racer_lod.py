#!/usr/bin/env python3
"""Cook authored driver model_lod; shared source hashes avoid duplicate textures.
Outputs an additive loose overlay. Never edits original archives or existing models.
"""
from pathlib import Path
import argparse,json,os,subprocess,shutil,hashlib
from cook_model import cook,ROOT

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--garage',type=Path,default=ROOT/'build/roster/models/garage.json');a=p.parse_args()
 a.work.mkdir(parents=True,exist_ok=True);a.output.mkdir(parents=True,exist_ok=True)
 rows=json.loads(a.garage.read_text())['choices'];cache={};receipt=[]
 roots=[ROOT/'assets/vehicles',ROOT/'build/roster-work/vehicles']
 for row in rows:
  slug=row['prefix'].rstrip('_');src=next((r/slug/'driver_model.brres' for r in roots if (r/slug/'driver_model.brres').exists()),None)
  if src is None:
   src=ROOT/'assets/roster'/row['driverSlug']/'driver_model.brres'
  digest=hashlib.sha256(src.read_bytes()).hexdigest()
  if digest!=row['driver_sha256']:raise ValueError('Source mismatch '+slug)
  if digest not in cache:
   dest=a.work/digest;subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(src),str(dest),'model_lod'],check=True,stdout=subprocess.DEVNULL,env={**os.environ,'MKW_RACE_MATERIALS':'1'})
   data=json.loads((dest/'model.json').read_text());assert len(data['models'])==1
   shapes=data['models'][0]['shapes'];used=sorted({s[k] for s in shapes for k in ['texture','secondary'] if s[k]>=0})
   for s in shapes:
    for k in ['texture','secondary']:
     if s[k]>=0:s[k]=used.index(s[k])
   data['textures']=[data['textures'][i] for i in used];(dest/'model.json').write_text(json.dumps(data))
   name='lod_'+digest[:16];parts=cook(dest,a.output,name);assert len(parts)==1
   cache[digest]=(a.output/'models'/(parts[0]['name']+'.gmdl'),sum(len(s['vertices'])//3 for s in shapes))
  model,tri=cache[digest];target=a.output/'models'/(row['prefix']+'driver_lod.gmdl');shutil.copyfile(model,target)
  receipt.append({'prefix':row['prefix'],'source':digest,'triangles':tri,'model':target.name})
 (a.work/'receipt.json').write_text(json.dumps(receipt,indent=2));print('LOD drivers',len(receipt),'unique',len(cache),flush=True)
if __name__=='__main__':main()
