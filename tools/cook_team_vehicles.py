#!/usr/bin/env python3
"""Cook original red/blue standard vehicles; reuse normal driver banks."""
import argparse, copy, json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from cook_garage import ROOT, cook_choice, inspect_kart, unpack, Disc, data_partition

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('disc',type=Path);p.add_argument('--output',type=Path,default=ROOT/'build/team-vehicles')
 p.add_argument('--jobs',type=int,default=6);a=p.parse_args()
 inventory={r['name']:r for r in json.loads((ROOT/'data/extracted/disc-files.json').read_text())['files']}
 choices=json.loads((ROOT/'assets/vehicles/garage-audit.json').read_text())['choices']
 source=ROOT/'assets/team-vehicles';source.mkdir(parents=True,exist_ok=True)
 work=[];partition=None
 for original in choices:
  if original['vehicle'] not in (0,1,2,18,19,20):continue
  for team in ('red','blue'):
   stem,driver=Path(original['archive']).name.split('-',1)
   archive_name=f'Race/Kart/{stem}_{team}-{driver}'
   entry=inventory[archive_name];compressed=ROOT/'data/extracted'/archive_name
   if not compressed.exists():
    if partition is None:partition=data_partition(Disc(a.disc))
    compressed.write_bytes(partition.read(entry['offset'],entry['size']))
   row=copy.deepcopy(original);row['prefix']+=team+'_';row['archive']=archive_name;row['team']=team
   archive=source/row['prefix'].rstrip('_')
   if not (archive/'kart_model.brres').exists():unpack(compressed,archive)
   kart=inspect_kart(archive);row['parts']=[m['model'] for m in kart['nodes']]
   work.append(row)
 a.output.mkdir(parents=True,exist_ok=True)
 def process(row):
  cooked=cook_choice(row,a.output,source_root=source,export_root=ROOT/'assets/team-vehicle-parts',include_driver=False)
  print(row['prefix'],flush=True);return cooked
 with ThreadPoolExecutor(max_workers=a.jobs) as pool:rows=list(pool.map(process,work))
 (a.output/'team-vehicles.json').write_text(json.dumps({'version':1,'choices':rows},indent=2)+'\n')
 print(f'Cooked {len(rows)} source team vehicle variants',flush=True)
if __name__=='__main__':main()
