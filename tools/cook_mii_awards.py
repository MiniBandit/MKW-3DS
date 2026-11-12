#!/usr/bin/env python3
"""Refresh original Mii award bodies with the current race material converter."""
import argparse,json,struct
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from cook_award_models import cook_one,ROOT

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);a=p.parse_args()
 stems=[f'{s}{o}_mii_{g}' for s in 'sml' for o in 'ab' for g in 'mf']
 with ThreadPoolExecutor(max_workers=3) as pool:
  records=list(pool.map(lambda stem:cook_one(('unpacked/Award/'+stem+'.brres','model',True,0),ROOT/'build/award-source',a.output),stems))
 roles=[]
 for stem,r in zip(stems,records):
  shapes=json.loads((ROOT/'build/award-exports'/r['prefix']/'model.json').read_text())['models'][0]['shapes']
  b=(a.output/'models'/(r['parts'][0]+'.gmdl')).read_bytes();v,_,_,ns,_=struct.unpack_from('<5I',b,4);assert ns==len(shapes)
  for i,s in enumerate(shapes):
   if s['material'] not in ('mii_color','mii_skin'):continue
   assert s['retail']['word']&(1<<24)
   first,count=struct.unpack_from('<2I',b,24+i*{3:44,4:56,5:92}[v])
   roles.append(' {"%s",%d,%d,%s},'%(stem,first,count,str(s['material']=='mii_skin').lower()))
 (ROOT/'include/mkw/mii_award_materials.hpp').write_text('#pragma once\nnamespace mkw {\nstruct MiiAwardMaterial {const char* stem;unsigned first,count;bool skin;};\ninline constexpr MiiAwardMaterial MiiAwardMaterials[]={\n'+'\n'.join(roles)+'\n};\n}\n')
 (a.output/'models/mii-awards.json').write_text(json.dumps(records,indent=2)+'\n')
if __name__=='__main__':main()
