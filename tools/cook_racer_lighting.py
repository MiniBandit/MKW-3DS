#!/usr/bin/env python3
"""Refresh all 288 race choices from existing verified archives, no disc extraction."""
import copy,json,shutil
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from cook_garage import ROOT,cook_choice
from cook_roster import sha256

def main():
 output=ROOT/'build/roster';shared=ROOT/'sd-card/3ds/mkw-3ds'
 manifest=json.loads((output/'models/garage.json').read_text())
 def one(original):
  row=copy.deepcopy(original);folder=row['prefix'].rstrip('_')
  sources=ROOT/'assets/vehicles'
  if not (sources/folder).is_dir():sources=ROOT/'build/roster-work/vehicles'
  for name,field in [('driver_model.brres','driver_sha256'),('kart_model.brres','kart_sha256')]:
   if sha256(sources/folder/name)!=row[field]:raise ValueError('Changed verified archive: '+folder)
  for name in row['animationBanks']:
   target=output/'models'/(name+'.dskm')
   if not target.is_file():shutil.copyfile(shared/'models'/target.name,target)
  result=cook_choice(row,output,source_root=sources,export_root=ROOT/'build/racer-lighting-exports',models_only=True)
  for name in result['animationBanks']:
   import struct
   bank=(output/'models'/(name+'.dskm')).read_bytes()
   model=(output/'models'/(name+'.gmdl')).read_bytes()
   if struct.unpack_from('<I',bank,8)!=struct.unpack_from('<I',model,8):raise ValueError('Bank vertex mismatch: '+name)
  (ROOT/'build/roster-work/receipts'/(row['prefix']+'.json')).write_text(json.dumps(result,indent=2)+'\n')
  return result
 with ThreadPoolExecutor(max_workers=3) as executor:manifest['choices']=list(executor.map(one,manifest['choices']))
 (output/'models/garage.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print('Refreshed and bank-verified all',len(manifest['choices']),'race choices',flush=True)
if __name__=='__main__':main()
