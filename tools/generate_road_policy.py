"""Build executable-only draw ranges from the current meshes and retail KCL.
No PAK/texture/geometry payload is rewritten. Existing source layer flags are
reused, plus the inspected source material exceptions (Luigi's grey bands are
part of a_MainRoad_m, not a separate stripe or centre-line material).
"""
from pathlib import Path
import sys,json,struct,math,collections,re,hashlib
import numpy as np
from course_catalog import ROOT
w=ROOT/'build/draw-policies';w.mkdir(parents=True,exist_ok=True)

from model_sources import LocalModels,model
from race_map_bake import kcl_triangles,ROAD_TYPES
from course_catalog import courses
EXTRA={0:{'k_road_02','k_road_03'},1:{'tuti_tire'},2:{'union_01','taiya'},5:{'line_m','z_road_line_m'},6:{'K_road_kizu00'},8:{'a_MainRoad_m'},10:{'road03','K_road_kizu00'},11:{'line_m'},13:{'logo'},14:{'start_m'},18:{'wl_StartLine','wl_doro1'},19:{'line'},24:{'a_checkline','a_line'},25:{'a_checkline'},26:{'a_checkline'},29:{'c_michi','c_tire'},30:{'a_check'},31:{'a_check'},32:{'union_01'},33:{'x_maru_fuchi'},35:{'fkrogo00'},39:{'a_line'},40:{'a_line'}}
EXCLUDE=re.compile(r'grass|kusa|tree|water|sea|lake|yougan|yogan|kabe|wall|roof|fence|kanban|spot|ha_|glass|hilight|taki|kira|luminous|WT_Upipe_ROAD|^ef_' ,re.I)
pack=LocalModels();models=[];audit=[];allmats=[]
for c in sorted(courses(),key=lambda c:c['id']):
 cid=c['id'];receipt=json.loads((ROOT/f'sd-card/3ds/mkw-3ds/models/course_{cid:02d}-receipt.json').read_text());bytex=collections.defaultdict(set)
 for m in receipt['materials']:bytex[m['texture']].add(m['material'])
 a,b,cc,kind,norm=kcl_triangles(ROOT/'assets/courses'/c['key']/'course.kcl');keep=np.isin(kind,list(ROAD_TYPES))&(norm[:,1]>.2);a,b,cc,norm=[x[keep]for x in(a,b,cc,norm)];k=np.stack((a,b,cc),axis=1);bins=collections.defaultdict(list);step=1500
 for i,t in enumerate(k):
  for x in range(math.floor(t[:,0].min()/step),math.floor(t[:,0].max()/step)+1):
   for z in range(math.floor(t[:,2].min()/step),math.floor(t[:,2].max()/step)+1):bins[(x,z)].append(i)
 def onroad(t):
  p=t.mean(axis=0);ids=bins.get((math.floor(p[0]/step),math.floor(p[2]/step)),[])
  if not ids:return False
  ts=k[ids];p0=ts[:,0];v=ts[:,1]-p0;u=ts[:,2]-p0;px=p[0]-p0[:,0];pz=p[2]-p0[:,2];det=v[:,0]*u[:,2]-u[:,0]*v[:,2]
  with np.errstate(divide='ignore',invalid='ignore'):
   sv=(px*u[:,2]-u[:,0]*pz)/det;su=(v[:,0]*pz-px*v[:,2])/det
  inside=(sv>=-1e-5)&(su>=-1e-5)&(sv+su<=1.00001);height=p0[:,1]+sv*v[:,1]+su*u[:,1]
  return bool(np.any(inside&(np.abs(height-p[1])<=32)))
 count=0;coursechanges=[]
 for name in sorted(n for n in pack.entries if re.fullmatch(fr'models/course_{cid:02d}_\d+\.gmdl',n)):
  raw=pack.read(name);version,subs,names,verts,indices=model(raw);_,_,nv,ni,ns,nt=struct.unpack_from('<4s5I',raw);sm=raw.rfind(b'SMP1');assert sm>=0 and struct.unpack_from('<I',raw,sm+4)[0]==ns
  ranges=[];selected=[];per=[]
  for si,s in enumerate(subs):
   if s[2]<0:continue
   tex=int(names[s[2]].rsplit('_',1)[1]);mats=bytex[tex];layer=bool(version>=4 and s[11]&(1<<23) and s[3]&2 and not s[3]&(1<<24));extra=bool(mats&EXTRA.get(cid,set()))
   # A shared texture is not by itself a material classification.
   candidate=(layer or extra)and not any(EXCLUDE.search(m)for m in mats)
   before=struct.unpack_from('<f4B',raw,sm+8+si*24)
   chosen=[]
   if candidate:
    for at in range(s[0],s[0]+s[1],3):
     tri=np.array([verts[indices[at+j]][:3]for j in range(3)]);n=np.cross(tri[1]-tri[0],tri[2]-tri[0]);length=np.linalg.norm(n)
     if length<1e-7 or abs(n[1]/length)<.3:continue
     if not (cid==8 and mats=={'a_MainRoad_m'}) and not onroad(tri):continue
     chosen.append(at)
     if before[3] and before[1]>=2:selected.append((at,s[2]))
   if chosen:per.append({'submesh':si,'materials':sorted(mats),'source_texture':receipt['textures'][tex]['source'],'source_layer':bool(layer),'selected_faces':len(chosen),'old_min_filter':before[1],'changed_faces':len(chosen)if before[3]and before[1]>=2 else 0})
  for at,tex in sorted(selected):
   if ranges and ranges[-1][0]+ranges[-1][1]==at and ranges[-1][2]==tex:ranges[-1][1]+=3
   else:ranges.append([at,3,tex])
  if ranges:models.append({'name':Path(name).name,'vertices':nv,'indices':ni,'submeshes':ns,'ranges':ranges,'sha256':hashlib.sha256(raw).hexdigest()})
  coursechanges.extend(per);count+=len(selected)
  allmats.append({'model':name,'vertices':nv,'indices':ni,'submeshes':ns,'sha256':hashlib.sha256(raw).hexdigest(),'changes':per})
 audit.append({'course':cid,'archive':c['key'],'selected_changed_faces':count,'source_BRRES_sha256':hashlib.sha256((ROOT/'assets/courses'/c['key']/'course_model.brres').read_bytes()).hexdigest(),'KCL_sha256':hashlib.sha256((ROOT/'assets/courses'/c['key']/'course.kcl').read_bytes()).hexdigest(),'changes':coursechanges});print(cid,count,[(r['materials'],r['changed_faces'])for r in coursechanges],flush=True)
pack.close()
(w/'road-policy.json').write_text(json.dumps({'models':models,'courses':audit,'inspected_models':allmats},indent=2)+'\n')
head=['// Generated from HW62 course meshes and original KCL; see hw64/generate_policy.py.\n#pragma once\n#include <cstdint>\nnamespace mkw::roadSampling {\nstruct Range {uint32_t first,count;int32_t texture;};\nstruct Mesh {const char* name;uint32_t vertices,indices,submeshes;const Range* ranges;unsigned count;};\n']
for i,m in enumerate(models):head.append(f'inline constexpr Range R{i}[]={{'+','.join('{'+','.join(map(str,r))+'}'for r in m['ranges'])+'};\n')
head.append('inline constexpr Mesh Meshes[]={\n')
for i,m in enumerate(models):head.append(f'{{"{m["name"]}",{m["vertices"]},{m["indices"]},{m["submeshes"]},R{i},{len(m["ranges"])} }},\n')
head.append('};\n}\n');(ROOT/'include/mkw/road_sampler_ranges.hpp').write_text(''.join(head));print('models',len(models),'ranges',sum(len(m['ranges'])for m in models))
