#!/usr/bin/env python3
"""Stage only retail Battle half-pipe strips and Funky's overhead LED panels.
Keep all unrelated delivered geometry/materials/UV tracks byte-identical.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess
from cook_model import ROOT,cook
from gx_material import GMDL_SUBMESH_BYTES,GMDL_SUBMESH

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--baseline',type=Path);a.add_argument('--output',type=Path,default=ROOT/'build/battle-course-feedback');args=a.parse_args()
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
 if args.baseline is None:
  # A fresh importer already emits the corrected half-pipe/screen TEV.
  # The legacy patch only migrates archived meshes from older cookers.
  (out/'receipt.json').write_text(json.dumps({'files':[],'baseline':'fresh-disc','policy':'Current course exporter already supplies these materials'},indent=2)+'\n')
  return
 sd=args.baseline.resolve()/'SD卡根目录/3ds/mkw-3ds'
 manifest=json.loads((args.baseline/'manifest.json').read_text());records=[]
 with (sd/'race.pak').open('rb')as archive:
  magic,n,nb=struct.unpack('<4sII',archive.read(12));assert magic==b'MPK1';rows=[struct.unpack('<III',archive.read(12))for _ in range(n)];names=archive.read(nb);directory={names[a:names.index(0,a)].decode():(b,c)for a,b,c in rows}
  def read(name):
   if manifest[name]['storage']=='loose':return(sd/name).read_bytes()
   at,size=directory[name];archive.seek(at);b=archive.read(size);assert hashlib.sha256(b).hexdigest()==manifest[name]['sha256'];return b
  for course,key,selected in [(35,'skate_battle',{'ef_hpipeBoard','etc'}),(34,'casino_battle',{'ef_hpipeBoard'})]:
   source=ROOT/'assets/courses'/key/'course_model.brres';work=out/'exports'/key;work.mkdir(parents=True,exist_ok=True)
   env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_COURSE_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1'}
   subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work)],env=env,check=True,stdout=subprocess.DEVNULL)
   data=json.loads((work/'model.json').read_text());kept=[s for m in data['models']for s in m['shapes']if s['material']in selected]
   assert {s['material']for s in kept}==selected
   assert all(s['textureAlphaAdd']for s in kept if s['material']=='ef_hpipeBoard')
   for s in kept:
    if s['material']=='etc':assert s['dotMatrixScreen'] and s['texture']==7 and s['secondary']==5 and len(s['uvSamples'])==300
   targets={s['texture']:len(s['vertices'])for s in kept};removed={i:0 for i in targets};patches=[]
   listname=f'models/course_{course:02d}.list';parts=read(listname).decode().split()
   for mesh in parts:
    name=f'models/{mesh}.gmdl';original=read(name);patched=bytearray(original);_,v,nv,ni,ns,nt=struct.unpack_from('<4s5I',original);record=GMDL_SUBMESH_BYTES[v]
    for i in range(ns):
     at=24+i*record;s=struct.unpack_from(GMDL_SUBMESH[v],original,at)
     # Legacy fallback used arrow texture0 or dot texture5. The latter is
     # now the secondary layer, but its matching original face stays unique.
     legacy=5 if course==35 and s[2]==5 else s[2]
     wanted=targets.get(legacy)
     if course==35 and legacy==5:wanted=24
     if wanted is not None and s[1]:
      assert s[1]==wanted,(course,mesh,i,s[1],wanted)
      removed[legacy]=removed.get(legacy,0)+s[1];struct.pack_into('<I',patched,at+4,0);patches.append({'mesh':name,'submesh':i,'oldIndices':s[1],'oldTexture':s[2],'countOffset':at+4})
    if patched!=original:
     # Only the existing draw count changes. Position/indices, all other
     # material records and optional UV/sampler chunks are untouched.
     allowed={p['countOffset']+j for p in patches if p['mesh']==name for j in range(4)}
     assert all(i in allowed for i,(a,b)in enumerate(zip(original,patched))if a!=b)
     dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(patched)
   expected={0:504,5:24}if course==35 else{0:432}
   assert {i:n for i,n in removed.items()if n}==expected,removed
   subset=out/'selected'/key;subset.mkdir(parents=True,exist_ok=True)
   for m in data['models']:m['shapes']=[s for s in m['shapes']if s['material']in selected]
   (subset/'model.json').write_text(json.dumps(data))
   # Texture indices/names and all GPU texture bytes remain the baseline's.
   # Only decode for the cook audit; never encode a replacement texture.
   for p in work.glob('tex_*'):
    if p.suffix in ('.bin','.pal'):(subset/p.name).write_bytes(p.read_bytes())
   prefix=f'course_{course:02d}_battle_feedback'
   cooked=cook(subset,out,prefix,256,texture_prefix=f'course_{course:02d}',encoded_textures={f'course_{course:02d}_{i:03d}'for i in range(len(data['textures']))},course_geometry=True)
   dest=out/listname;dest.write_text(''.join(n+'\n'for n in parts+[p['name']for p in cooked]))
   records.append({'course':course,'source':str(source.relative_to(ROOT)),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'materials':sorted(selected),'oldDrawsDisabled':patches,'newTriangles':sum(p['triangles']for p in cooked),'all_other_old_mesh_bytes_unchanged':True,'textures_unchanged':True,'overheadScreen':'4 faces/8triangles, logo+dot TEV and300-frame nonlinear SRT0; no EFB'if course==35 else None})
 files=[]
 for p in sorted((out/'models').glob('*')):
  if p.suffix=='.gmdl' or p.name in ['course_34.list','course_35.list']:files.append({'path':p.relative_to(out).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
 (out/'receipt.json').write_text(json.dumps({'files':files,'baseline':str(args.baseline.resolve()),'courses':records,'scope':'Only corresponding screen/half-pipe faces replaced; original BRRES and all other delivered bytes retained; no indirect bump warp added','constantQuantization':'top logo K=.25 rounds64/255 in PICA 8-bit constant; pre-clamp final RGB error <=1/255 for any two texture samples','GPUAccepted':False},indent=2)+'\n')
 print('PASS staged',len(files),'scoped resources; original bytes retained outside3 draw counts; no texture encodes')
if __name__=='__main__':main()
