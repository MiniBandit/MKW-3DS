#!/usr/bin/env python3
"""Stage missing placed Battle assets, merging accepted arena visual lists."""
from pathlib import Path
import argparse,json,hashlib,os,subprocess,struct,sys
from cook_model import ROOT,cook
from cook_course_objects import mdl0_names,safe,EXCLUDED
from course_catalog import courses
from import_courses import kmp_database

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path,default=ROOT/'build/battle-object-assets');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);sd=a.baseline.resolve()/'SD卡根目录/3ds/mkw-3ds' if a.baseline else ROOT/'sd-card/3ds/mkw-3ds'
 raw=(ROOT/'assets/common/ObjFlow.bin').read_bytes();flow={}
 for i in range(struct.unpack_from('>H',raw)[0]):
  at=2+i*116;flow[struct.unpack_from('>H',raw,at)[0]]=(raw[at+2:at+34].split(b'\0')[0].decode(),raw[at+34:at+98].split(b'\0')[0].decode())
 records=[];written=[]
 for c in courses():
  if c['kind']!='arena':continue
  folder=ROOT/'assets/courses'/c['key'];listname=f"models/course_{c['id']:02d}_visuals.list";prior=(sd/listname).read_text()if(sd/listname).is_file()else'';lines=prior.splitlines();have={line.split()[0]for line in lines};wanted=set()
  for obj in kmp_database(folder/'course.kmp')['GOBJ']['records']:
   if not obj['presence']&1:continue
   name,deps=flow.get(obj['id'],('',''))
   # These have separate item/effect/environment renderers or no visible mesh.
   if name in ['itembox','s_itembox','coin','pocha','pochaMori','sun','lensFX','eline_control','Flash_S']:continue
   for r in deps.split(';'):
    if r not in have and(folder/(r+'.brres')).is_file():wanted.add(r)
  if not wanted:continue
  for resource in sorted(wanted):
   archive=folder/(resource+'.brres');encoded=set();prefix=f"obj{c['id']:02d}_{safe(resource)}"
   for model in mdl0_names(archive):
    name=prefix+'__'+safe(model);work=out/'exports'/c['key']/safe(resource)/safe(model);work.mkdir(parents=True,exist_ok=True);bank=work/'bank.dobj'
    env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_OBJECT_MATERIALS':'1','MKW_OBJECT_BANK':str(bank),'MKW_RETAIL_LIGHTING':'1','MKW_TEXTURE_SOURCES':os.pathsep.join(str(folder/n)for n in ['course_model.brres','vrcorn_model.brres']if(folder/n).is_file())}
    subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(archive),str(work),model],env=env,stdout=subprocess.DEVNULL,check=True)
    parts=cook(work,out,name,256,preserve_vertices=True,texture_prefix=prefix,encoded_textures=encoded);assert len(parts)==1
    info=json.loads((work/'bank.json').read_text());animated=info['chr']or info['srt']or info['clr']or info['pat']or info['palette']>1;bankname=name if animated else '-'
    if animated:(out/'models'/(name+'.dobj')).write_bytes(bank.read_bytes())
    lines.append(' '.join([resource,model,parts[0]['name'],bankname]));records.append({'course':c['id'],'resource':resource,'model':model,'mesh':parts[0]['name'],'bank':bankname,'triangles':parts[0]['triangles'],'animations':info,'originalSha256':hashlib.sha256(archive.read_bytes()).hexdigest()})
  dest=out/listname;dest.parent.mkdir(exist_ok=True,parents=True);dest.write_text('\n'.join(lines)+'\n');written.append(listname)
 files=[]
 keep={f'models/{r["mesh"]}.gmdl'for r in records}|{f'models/{r["bank"]}.dobj'for r in records if r['bank']!='-'}|set(written)
 for r in records:
  b=(out/'models'/(r['mesh']+'.gmdl')).read_bytes();_,v,nv,ni,ns,nt=struct.unpack_from('<4s5I',b);off=24+ns*{3:44,4:56,5:92}[v]
  for i in range(nt):keep.add('textures/'+b[off+i*64:off+(i+1)*64].split(b'\0')[0].decode()+'.t3x')
 for rel in sorted(keep):
  p=out/rel;files.append({'path':rel,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
 (out/'receipt.json').write_text(json.dumps({'files':files,'models':records,'merged_visual_lists':written,'baseline':str(a.baseline.resolve()) if a.baseline else 'fresh-disc','policy':'Original resources read only; existing arena visuals preserved, shared item/effect/water volumes excluded','GPUAccepted':False},indent=2)+'\n');print('PASS staged',len(records),'missing placed models;',len(files),'files')
if __name__=='__main__':main()
