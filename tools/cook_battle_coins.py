#!/usr/bin/env python3
"""Cook the original common arena coin/pile/shadow meshes into a staged overlay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from cook_model import cook

ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/battle-coin-assets')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    sources=sorted((ROOT/'assets/courses').glob('*/coin.brres'))
    if len(sources)!=10:raise ValueError('Expected all ten original arena coin archives')
    hashes={str(p.relative_to(ROOT)):digest(p) for p in sources}
    if len(set(hashes.values()))!=1:raise ValueError('Arena coin archives differ; inspect before sharing assets')
    jobs=[('coin','battle_coin'),('coin-shadow','battle_coin_shadow'),
          ('pile_coin','battle_coin_pile'),('pile_coin-shadow','battle_coin_pile_shadow')]
    rows=[]
    for model,name in jobs:
        work=out/'exports'/name;work.mkdir(parents=True,exist_ok=True)
        env={**os.environ,'MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_FREEZE_NONLINEAR_UV':'1'}
        with (work/'export.log').open('w') as log:
            subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(sources[0]),str(work),model],
                           env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        parts=cook(work,out,name,256,texture_prefix=name)
        if len(parts)!=1:raise ValueError('Expected one mesh for '+model)
        target=out/'models'/(name+'.gmdl')
        shutil.copyfile(out/'models'/(parts[0]['name']+'.gmdl'),target)
        rows.append({'sourceModel':model,'model':str(target.relative_to(out)),'sha256':digest(target),
                     'triangles':parts[0]['triangles']})
    if any(digest(ROOT/p)!=sha for p,sha in hashes.items()):raise ValueError('Original resource changed during cook')
    files=[{'path':str(p.relative_to(out)),'sha256':digest(p),'bytes':p.stat().st_size}
           for directory in ('models','textures') for p in sorted((out/directory).iterdir())
           if p.suffix in ('.gmdl','.t3x') and not (p.suffix=='.gmdl' and p.stem.endswith('_000'))]
    (out/'receipt.json').write_text(json.dumps({'sources':hashes,'models':rows,'files':files},indent=2)+'\n')
    print('Cooked original arena coin, pile and shadows:',out)

if __name__=='__main__':main()
