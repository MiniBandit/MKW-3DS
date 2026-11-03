from pathlib import Path
import sys,json,struct,re,subprocess,os
from course_catalog import ROOT as root
w=root/'build/draw-policies';w.mkdir(parents=True,exist_ok=True)

from course_catalog import courses
from model_sources import LocalModels,model
pack=LocalModels()
rows=[];table=[]
def key(vs):return tuple(sorted(tuple(round(float(x),2) for x in v[:3]) for v in vs))
for c in sorted(courses(),key=lambda c:c['id']):
 cid=c['id'];k=c['key'];paths=[root/'build/startline-source'/k,root/'build/course-exports'/k/'course']
 src=next((p for p in paths if (p/'model.json').exists() and (p/'materials.json').exists()),None)
 if src is None:
  src=root/'build/phase-source'/k;src.mkdir(parents=True,exist_ok=True)
  env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_COURSE_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1'}
  subprocess.run(['node',str(root/'build/export_brres.mjs'),str(root/'assets/courses'/k/'course_model.brres'),str(src)],env=env,check=True,stdout=subprocess.DEVNULL)
 mats={m['name']:m for group in json.loads((src/'materials.json').read_text()) for m in group['materials']}
 lookup={};eligible=set()
 for group in json.loads((src/'model.json').read_text())['models']:
  for shape in group['shapes']:
   m=mats[shape['material']];g=m['gxMaterial']['ropInfo']
   if not m['translucent'] and g['blendMode']==1 and g['depthTest'] and not g['depthWrite']:
    eligible.add(m['name'])
    v=shape['vertices']
    for i in range(0,len(v)-2,3):lookup.setdefault(key(v[i:i+3]),set()).add(m['name'])
 selected=[];unmatched=0
 for name in sorted(n for n in pack.entries if re.fullmatch(fr'models/course_{cid:02d}_\d+\.gmdl',n)):
  raw=pack.read(name);version,subs,names,verts,indices=model(raw)
  _,_,nv,ni,ns,nt=struct.unpack_from('<4s5I',raw)
  for sub in subs:
   first,count,tex,flags=sub[:4]
   if not flags&2 or flags&(1<<24):continue
   found=[lookup.get(key([verts[j] for j in indices[i:i+3]]),set()) for i in range(first,first+count,3)]
   if found and all(found):
    material=sorted(set().union(*found));selected.append({'mesh':name,'first':first,'count':count,'materials':material})
    table.append((Path(name).name,nv,ni,first,count))
   elif any(found):unmatched+=1
 rows.append({'course':cid,'key':k,'eligible_source_materials':sorted(eligible),'selected':selected,'partially_matched_submeshes':unmatched})
 assert not unmatched,(k,unmatched)
header=['// Generated against packaged HW81 meshes and original DrawOpa materials.','#pragma once','#include <cstring>','namespace mkw::coursePhase {','struct Range {const char* name;unsigned vertices,indices,first,count;};','inline constexpr Range Ranges[]={']
header += ['{"%s",%d,%d,%d,%d},'%r for r in table]
header += ['};','template<class Model> void apply(Model& model,const char* path){','const char* name=std::strrchr(path,\'/\');name=name?name+1:path;','for(const auto& r:Ranges)if(model.vertexCount==r.vertices&&model.indexCount==r.indices&&!std::strcmp(name,r.name))','for(auto& s:model.submeshes)if(s.first==r.first&&s.count==r.count)s.lighting|=1u<<26;','}','}']
(root/'include/mkw/course_phase_ranges.hpp').write_text('\n'.join(header)+'\n')
(w/'course-phase-audit.json').write_text(json.dumps({'courses':rows,'selected_submeshes':len(table),'limitations':'Read-only source/pack audit; no gameplay traversal'},ensure_ascii=False,indent=2)+'\n')
print('PASS',len(rows),'courses;',len(table),'source DrawOpa blended submeshes matched exactly')
