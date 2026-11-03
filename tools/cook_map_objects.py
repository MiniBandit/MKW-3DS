#!/usr/bin/env python3
"""Offline transparent overhead sprites for the live minimap object layer.
No second camera or mesh rendering on 3DS; originals and existing maps untouched.
"""
import io,json,hashlib,sys,subprocess,struct,argparse,re,os
from pathlib import Path
import numpy as np
from PIL import Image
import race_map_bake as rb
from devkitpro import tool
from import_courses import kmp_database
from map_object_pose import pose
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'vendor/gclib'))
from gclib.texture_utils import decode_image
from gclib.gx_enums import ImageFormat,PaletteFormat
OUT=ROOT/'build/map-objects'
SOURCE=ROOT/'build/standalone-romfs'
# ids are the retail GOBJ ids. Palette choice matches classifyObject.
COURSES=[(2,2,'kinoko_course',[(0x1f5,0,'kinoko','kinoko_r'),(0x1f5,1,'kinoko','kinoko_d_r'),(0x1f6,0,'kinoko','kinoko_r'),(0x1f6,1,'kinoko','kinoko_d_r'),(0x1fa,0,'kinoko','kinoko_g'),(0x1fa,1,'kinoko','kinoko_d_g')]),
 (3,4,'factory_course',[(0x1fb,-1,'Crane','Crane'),(0x25a,-1,'BeltEasy','BeltEasy'),(0x25b,-1,'BeltCrossing','BeltCrossing'),(0x25c,-1,'BeltCurveA','BeltCurveA')]),
 (15,13,'rainbow_course',[(0x204,-1,'aurora','aurora')]),
 (22,23,'old_town_ds',[(0x1ff,-1,'TownBridgeDS','TownBridgeDS',phase) for phase in range(32)])]
COURSES += [
 (5,5,'shopping_course',[(0x25e,-1,'escalator','escalator',n) for n in range(20)]),
 (10,11,'treehouse_course',[(0x20e,-1,'ami','ami')]),
 (11,3,'volcano_course',[(0x1f7,n,'VolcanoRock1',f'VolcanoRock{n+1}') for n in range(2)]+[(0x1fc,n,'VolcanoPiece1',f'VolcanoPiece{n}') for n in [0,1,2,3,4,5,7,8,9,11,13,14,16,18]]),
 (12,14,'desert_course',[(0x208,-1,'dc_pillar','dc_pillar',n) for n in range(25)]+[(0x209,h,'dc_sandcone','dc_sandcone',n) for h in [600,750,1130,1810,850,1200,640] for n in range(16)]),
 (14,12,'koopa_course',[(0x1fe,-1,'TwistedWay','TwistedWay',n) for n in range(40)]),
 (30,19,'old_donkey_gc',[(0x202,-1,'DKturibashiGC','DKturibashiGC',n) for n in range(54)]),
]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument("--course",type=int);args=parser.parse_args()
 (OUT/'textures').mkdir(parents=True,exist_ok=True)
 headers=[];reports=[]
 for course,number,key,entries in COURSES:
  if args.course is not None and course!=args.course:continue
  atlas=Image.new('RGBA',(512,1024));x=y=rowh=0;cache={};rows=[]
  lights,ambient=rb.course_lighting(f'Race/Course/{key}.szs')
  kmp=kmp_database(ROOT/'assets/courses'/key/'course.kmp')
  for entry in entries:
   oid,variant,res,model=entry[:4];state=entry[4] if len(entry)>4 else 0
   cachekey=(res,model,state,variant if oid==0x209 else 0)
   if cachekey not in cache:
    stem='coconut_escalator_000' if oid==0x25e else f'obj{number:02d}_{res.lower()}__{model.lower()}_000'
    from model_sources import LocalModels
    source_models=LocalModels();path=source_models.entries[f'models/{stem}.gmdl']
    rawmodel=path.read_bytes();version,nv,ni,ns,nt=struct.unpack_from('<5I',rawmodel,4)
    if version==5:
     # Minimap freezes view-dependent material effects to their base texture.
     reduced=struct.pack('<4s5I',b'GMDL',4,nv,ni,ns,nt)+b''.join(rawmodel[24+i*92:24+i*92+56] for i in range(ns))+rawmodel[24+ns*92:]
     temp=OUT/f'{stem}.gmdl';temp.write_bytes(reduced);part=rb.Part(temp)
    else:part=rb.Part(path)
    if oid==0x1fe:
     # Map-only cutaway in the authored bind pose, before the live twist.
     # Road lies at y=0; the enclosing roof is at y=1600..1688.
     # Keep the floor/lower walls and the original framing for every phase.
     for j,sub in enumerate(part.submeshes):
      first,count=sub[:2];tri=part.indices[first:first+count].reshape(-1,3)
      keep=part.position[tri,1].min(axis=1)<=rb.CLEARANCE
      kept=tri[keep].reshape(-1)
      part.indices[first:first+len(kept)]=kept
      part.submeshes[j]=(first,len(kept),*sub[2:])
    if oid==0x204:
     # Original aurora uses source RGB (ONE, INVSRCCLR), not vertex alpha
     # (which is zero). Freeze its luminous road colour into an opaque map
     # footprint; the full camera-dependent screen blend stays on the 3D pass.
     part.color[:,3]=1

    export=ROOT/'build/coconut-exports/coconut_escalator' if oid==0x25e else ROOT/'build/object-exports'/key/res.lower()/model.lower()
    if not (export/'model.json').is_file():
     export.mkdir(parents=True,exist_ok=True)
     folder=ROOT/'assets/courses'/key
     env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_OBJECT_BANK':str(export/'bank.dobj'),'MKW_TEXTURE_SOURCES':os.pathsep.join(str(folder/n) for n in ('course_model.brres','vrcorn_model.brres') if (folder/n).is_file())}
     subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(folder/(res+'.brres')),str(export),model],env=env,check=True,stdout=subprocess.DEVNULL)
    if oid in (0x1ff,0x1fe,0x202,0x20e,0x208,0x25e):pose(part,export/'bank.dobj',oid,state)
    data=json.loads((export/'model.json').read_text())
    for name,t in zip(part.textures,data['textures']):
     if name in rb.Texture.cache:continue
     raw=(export/(t['file']+'.bin')).read_bytes();pp=export/(t['file']+'.pal');pal=pp.read_bytes() if pp.exists() else b''
     im=decode_image(io.BytesIO(raw),io.BytesIO(pal),ImageFormat(t['format']),PaletteFormat(t['paletteFormat'] or 0),len(pal)//2,t['width'],t['height']).convert('RGBA')
     tex=object.__new__(rb.Texture);level=np.asarray(im,dtype=np.float32)/255;tex.levels=[level]
     while min(level.shape[:2])>1:
      h,w=level.shape[0]//2*2,level.shape[1]//2*2
      level=level[:h,:w].reshape(h//2,2,w//2,2,4).mean(axis=(1,3));tex.levels.append(level)
     rb.Texture.cache[name]=tex
    lo=part.position.min(axis=0);hi=part.position.max(axis=0)
    span=hi-lo;size=np.maximum(8,np.ceil(span[[0,2]]/max(span[[0,2]])*(252 if oid in (0x204,0x20e) else 60 if oid in (0x1ff,0x1fe,0x202,0x208,0x25e,0x209) else 96)).astype(int))
    # Two transparent border texels; bounds enlarged by corresponding world units.
    pad=span[[0,2]]/size*2;bounds=[lo[0]-pad[0],lo[2]-pad[1],hi[0]+pad[0],hi[2]+pad[1]]
    if oid==0x25e:
     # Original fixed landing elevations 0..1750 and slope dz/dy=-3.
     # Treads beneath those landings are hidden by the course in 3D; do
     # not stamp the buried tails over the baked map. Fixed frame for all phases.
     bounds=[-900.,-5250.,900.,0.];size=np.array([24,70])
    w,h=map(int,size+4);frame=rb.Frame(*bounds,w*2,h*2);ceiling=np.full((h*2,w*2),np.inf)
    black,depth=rb.render([part],frame,ceiling,lights,ambient,(0,0,0))
    white,_=rb.render([part],frame,ceiling,lights,ambient,(1,1,1))
    alpha=np.clip(1-(white-black).mean(axis=2),0,1)
    if oid==0x209:
     placement=next(r for r in kmp['GOBJ']['records'] if r['id']==oid and r['settings'][1]==variant)
     scale=placement['scale'];pos=placement['position'];delta=variant*state/15
     # Only the emergent surface, not the buried cone, belongs on the map.
     worldframe=rb.Frame(bounds[0]*scale[0]+pos[0],bounds[1]*scale[2]+pos[2],bounds[2]*scale[0]+pos[0],bounds[3]*scale[2]+pos[2],w*2,h*2)
     road,ground=rb.floor_maps(rb.kcl_triangles(ROOT/'assets/courses'/key/'course.kcl'),worldframe)
     visible=depth*scale[1]+pos[1]+delta>=ground-1
     alpha*=visible

    rgb=np.divide(black,alpha[...,None],out=np.zeros_like(black),where=alpha[...,None]>1e-6)
    rgba=np.concatenate([np.clip(rgb,0,1),alpha[...,None]],axis=2)
    image=Image.fromarray(np.round(rgba*255).astype('uint8'),'RGBA').resize((w,h),Image.Resampling.LANCZOS)
    if x+w>512:x=0;y+=rowh;rowh=0
    assert y+h<=1024,(key,w,h,y)
    atlas.alpha_composite(image,(x,y))
    # Representative surface height retains the cap centre when it tilts.
    # Rigid sprite approximation: local XZ silhouette, median upward-facing surface Y.
    tris=np.concatenate([part.indices[sub[0]:sub[0]+sub[1]] for sub in part.submeshes]).reshape(-1,3);verts=part.position[tris]
    area=np.abs(np.cross(verts[:,1]-verts[:,0],verts[:,2]-verts[:,0])[:,1]);height=float(np.average(verts[:,:,1].mean(axis=1),weights=area))
    cache[cachekey]=(x,y,w,h,bounds,height)
    reports.append(dict(course=course,model=model,source=str(path),sha256=digest(path),bounds=bounds,height=height,opaquePixels=int((alpha>.01).sum()),size=[w,h]))
    x+=w;rowh=max(rowh,h)
   px,py,w,h,bounds,height=cache[cachekey]
   rows.append((oid,variant,state,px,py,w,h,*bounds,height))
  file=f'map_objects_{course:02d}'
  aw=1<<(max(r[3]+r[5] for r in rows)-1).bit_length();ah=1<<(y+rowh-1).bit_length()
  atlas=atlas.crop((0,0,aw,ah))
  # Keep each resident map layer <=256 KiB of RGBA4 even for many states.
  while aw*ah>131072:
   aw//=2;ah//=2;atlas=atlas.resize((aw,ah),Image.Resampling.LANCZOS)
   rows=[tuple(list(r[:3])+[v*.5 for v in r[3:7]]+list(r[7:])) for r in rows]
  atlas.save(OUT/'textures'/f'{file}.png')
  subprocess.run([str(tool('tex3ds')),'-f','rgba4','-z','none','-o',str(OUT/'textures'/f'{file}.t3x'),str(OUT/'textures'/f'{file}.png')],check=True)
  headers.append((course,file,rows,aw,ah))
  placements=[r for r in kmp['GOBJ']['records'] if any(r['id']==e[0] for e in entries)]
  (OUT/f'placements_{course:02d}.json').write_text(json.dumps(placements,indent=2))
 lines=['// Generated by cook_map_objects.py. Retail model-local overhead sprites.','#pragma once','#include <cstdint>','namespace mkw {','struct MapObjectArt { uint16_t id; int16_t variant; uint16_t state; float x,y,w,h; float x0,z0,x1,z1,height; };','struct MapObjectAtlas { const char* texture; const MapObjectArt* art; unsigned count,width,height; };']
 for course,file,rows,aw,ah in headers:
  lines.append(f'inline constexpr MapObjectArt MapObjectArt{course}[]={{')
  for row in rows:lines.append('{'+','.join(str(v) for v in row[:3])+','+','.join(f'{v:.8f}f' for v in row[3:])+'},')
  lines.append('};')
 lines.append('inline MapObjectAtlas mapObjectAtlas(unsigned course) { switch(course) {')
 for course,file,rows,aw,ah in headers:lines.append(f'case {course}: return {{"textures/{file}.t3x",MapObjectArt{course},{len(rows)},{aw},{ah}}};')
 lines+=['default:return {nullptr,nullptr,0,0,0}; }}','}']
 header_path=ROOT/'include/mkw/map_object_assets.hpp'
 text='\n'.join(lines)+'\n'
 if args.course is not None:
  old=header_path.read_text()
  block=rf'inline constexpr MapObjectArt MapObjectArt{args.course}\[\]=\{{.*?\n\}};'
  replacement=re.search(block,text,re.S).group(0)
  old,n=re.subn(block,lambda _:replacement,old,flags=re.S);assert n==1
  case=rf'case {args.course}: return .*?;'
  old,n=re.subn(case,lambda _:re.search(case,text).group(0),old);assert n==1
  text=old
 header_path.write_text(text)
 (OUT/'receipt.json').write_text(json.dumps(reports,indent=2))
 print('cooked',len(reports),'unique sprites,',len(headers),'atlases')
if __name__=='__main__':main()
