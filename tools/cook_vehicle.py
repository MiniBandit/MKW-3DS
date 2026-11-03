#!/usr/bin/env python3
"""Cook selected parts and a source driving pose from an extracted vehicle archive."""
import argparse,json,subprocess,shutil
from pathlib import Path
from cook_model import cook
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('archive',type=Path);p.add_argument('output',type=Path);p.add_argument('--prefix',default='');p.add_argument('--front-left',default='tire_fl');p.add_argument('--front-right',default='tire_fr');a=p.parse_args()
for filename,model,part,animation in [('kart_model.brres','body','body',None),('kart_model.brres',a.front_left,'tire_fl',None),('kart_model.brres',a.front_right,'tire_fr',None),('driver_model.brres','model','driver','drive')]:
 name=a.prefix+part;dest=root/'assets'/'parts'/name;cmd=['node',str(root/'build/export_brres.mjs'),str(a.archive/filename),str(dest),model]
 if animation:cmd+=['drive','15']
 subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
 parts=cook(dest,a.output,name,preserve_vertices=part=="driver")
 if len(parts)!=1:raise ValueError(f'{name} expected one small part')
 shutil.copyfile(a.output/'models'/(parts[0]['name']+'.gmdl'),a.output/'models'/(name+'.gmdl'))
