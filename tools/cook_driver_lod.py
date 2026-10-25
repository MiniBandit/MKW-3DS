#!/usr/bin/env python3
"""Cook every driver/vehicle choice's original low-detail driver for distant opponents.

Each Race/Kart driver_model.brres holds, beside the animated "model", an MDL0
"model_lod": one rigid mesh on a single body joint, already seated. The game
draws it in place of the animated driver for far opponents. Writes
<prefix>driver_lod.gmdl into build/roster and lists it in that garage.json;
a character's textures are encoded once per kart/bike outfit and reused."""
import json,os,shutil,subprocess
from pathlib import Path
from cook_model import cook

ROOT=Path(__file__).resolve().parents[1]

def main():
 output=ROOT/'build/roster';work=ROOT/'build/driver-lod'
 path=output/'models/garage.json';manifest=json.loads(path.read_text())
 shared=set();cooked=0
 for choice in manifest['choices']:
  prefix=choice['prefix'];folder=prefix.rstrip('_')
  sources=ROOT/'assets/vehicles'
  if not (sources/folder).is_dir():sources=ROOT/'build/roster-work/vehicles'
  brres=sources/folder/'driver_model.brres'
  if not brres.is_file() or b'model_lod' not in brres.read_bytes():continue
  name=prefix+'driver_lod';dest=work/name
  subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(brres),str(dest),'model_lod'],check=True,stdout=subprocess.DEVNULL,
                 env={**os.environ,'MKW_RACE_MATERIALS':'1'})
  outfit=f"{choice['driverSlug']}_{'bike' if choice['bike'] else 'kart'}_driver_lod"
  try:parts=cook(dest,output,name,max_texture=128,reuse_textures=outfit in shared,texture_prefix=outfit)
  except ValueError:parts=cook(dest,output,name,max_texture=128)  # this vehicle's outfit differs
  shared.add(outfit)
  if len(parts)!=1:raise ValueError('Low-detail driver must be one model: '+name)
  source=output/'models'/(parts[0]['name']+'.gmdl');target=output/'models'/(name+'.gmdl')
  if source!=target:shutil.move(source,target)
  if name not in choice['models']:choice['models'].append(name)
  shutil.rmtree(dest,ignore_errors=True);cooked+=1
 path.write_text(json.dumps(manifest,indent=2)+'\n')
 print('low-detail drivers:',cooked,'of',len(manifest['choices']))

if __name__=='__main__':main()
