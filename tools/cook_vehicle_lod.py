#!/usr/bin/env python3
"""Export authored split-screen vehicle LODs and explicit front-wheel-free variants."""
import argparse,json,os,subprocess,hashlib,shutil,copy,struct
from pathlib import Path
from cook_model import cook,ROOT
from extract_disc import Disc
from cook_roster import data_partition
from unpack_szs import unpack
from cook_course_objects import mdl0_names

def main():
 p=argparse.ArgumentParser();p.add_argument('--disc',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--garage',type=Path,default=ROOT/'build/roster/models/garage.json');a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
 rows=json.loads(a.garage.read_text())['choices'];inv=json.loads((ROOT/'data/extracted/disc-files.json').read_text());files={f['name']:f for f in inv['files']};disc=Disc(a.disc);assert disc.read(0,6).decode()==inv['identity']['game_id'];part=data_partition(disc);cache={};receipt=[]
 try:
  for row in rows:
   variants=['']+(['red_','blue_'] if row['vehicle'] in [0,1,2,18,19,20] else [])
   for team in variants:
    stem,driver=Path(row['archive']).stem.split('-');key=f'Race/Kart/{stem}{"_"+team[:-1] if team else ""}-{driver}_2.szs'
    if key not in files:key=key.replace('_2.szs','_4.szs')
    f=files[key]
    folder=a.work/'archives'/Path(key).stem;folder.mkdir(parents=True,exist_ok=True);source=folder/'source.szs'
    if not source.exists():source.write_bytes(part.read(f['offset'],f['size']));unpack(source,folder)
    brres=folder/'kart_model.brres';override=None
    if not mdl0_names(brres):
     assert team,('Empty non-team geometry',key)
     override=brres;brres=a.work/'archives'/Path(key.replace('_'+team[:-1]+'-','-')).stem/'kart_model.brres'
     assert brres.exists()
    digest=hashlib.sha256(brres.read_bytes()+(override.read_bytes() if override else b'')).hexdigest()
    if digest not in cache:
     existing={}
     for role in ['body','body_rear','handle','handle_rear']:
      path=a.output/'models'/('vlod_'+digest[:12]+'_'+role+'_000.gmdl')
      if path.exists():existing[role]=(path,struct.unpack_from('<I',path.read_bytes(),12)[0]//3)
     if 'body' in existing and 'body_rear' in existing and (not row['bike'] or 'handle' in existing and 'handle_rear' in existing):
      existing['separate_wheels']=sum(existing[k][1]-existing[k+'_rear'][1] for k in ['body','handle'] if k in existing)==0
      cache[digest]=existing
    if digest not in cache:
     dest=a.work/'export'/digest;subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(brres),str(dest)],check=True,stdout=subprocess.DEVNULL,env={**os.environ,'MKW_RACE_MATERIALS':'1'})
     data=json.loads((dest/'model.json').read_text())
     if override:
      texDest=dest/'team';subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(override),str(texDest)],check=True,stdout=subprocess.DEVNULL)
      teamData=json.loads((texDest/'model.json').read_text());byName={t['name']:t for t in teamData['textures']}
      for t in data['textures']:
       if t['name'] not in byName:continue
       other=byName[t['name']];file=t['file']
       for ext in ['.bin','.pal']:
        sourceTex=texDest/(other['file']+ext)
        if sourceTex.exists():shutil.copyfile(sourceTex,dest/(file+ext))
       t.update(other);t['file']=file
     models={m['name']:m for m in data['models']};assert 'model_lod' in models
     built={};removed=0
     for native,role in [('model_lod','body'),('handle_lod','handle')]:
      if native not in models:continue
      for rear in [False,True]:
       item=copy.deepcopy(models[native]);before=sum(len(s['vertices']) for s in item['shapes'])
       if rear:
        for s in item['shapes']:
         # Source wheel materials, never guess by chassis vertex positions.
         if not any(w in s['material'].lower() for w in ['tire','tyre']):continue
         if row['bike']:
          if role=='handle':s['vertices']=[]
         else:
          vs=s['vertices'];parents=list(range(len(vs)//3));seen={}
          def root(n):
           while parents[n]!=n:parents[n]=parents[parents[n]];n=parents[n]
           return n
          for i,v in enumerate(vs):
           keyPos=tuple(round(c,4) for c in v[:3]);tri=i//3
           if keyPos in seen:parents[root(tri)]=root(seen[keyPos])
           seen[keyPos]=tri
          groups={}
          for i in range(len(parents)):groups.setdefault(root(i),[]).extend(vs[3*i:3*i+3])
          # Connected wheel islands can cross Z=0; keep or drop whole islands.
          s['vertices']=[v for group in groups.values() if min(v[2] for v in group)+max(v[2] for v in group)<0 for v in group]
        item['shapes']=[s for s in item['shapes'] if s['vertices']]
       after=sum(len(s['vertices']) for s in item['shapes']);removed+=before-after
       shapes=item['shapes'];used=sorted({s[k] for s in shapes for k in ['texture','secondary'] if s[k]>=0})
       for s in shapes:
        for k in ['texture','secondary']:
         if s[k]>=0:s[k]=used.index(s[k])
       trimmed={**data,'textures':[data['textures'][i] for i in used],'models':[item]};(dest/'model.json').write_text(json.dumps(trimmed));name='vlod_'+digest[:12]+'_'+role+('_rear' if rear else '')
       parts=cook(dest,a.output,name);assert len(parts)==1;built[role+('_rear' if rear else '')]=(a.output/'models'/(parts[0]['name']+'.gmdl'),after//3)
     built['separate_wheels']=removed==0
     cache[digest]=built
    prefix=row['prefix']+team;built=cache[digest]
    for role,value in built.items():
     if role=='separate_wheels':continue
     model,tri=value;shutil.copyfile(model,a.output/'models'/(prefix+role.replace('_rear','')+'_lod'+('_rear' if role.endswith('_rear') else '')+'.gmdl'))
    receipt.append({'prefix':prefix,'archive':key,'source':digest,'vehicle':row['vehicle'],'separate_wheels':built['separate_wheels'],'parts':{k:v[1] for k,v in built.items() if k!='separate_wheels'}})
 finally:disc.f.close()
 flags={}
 for r in receipt:
  f=(r['separate_wheels'],'handle' in r['parts'])
  if r['vehicle'] in flags:assert flags[r['vehicle']]==f
  flags[r['vehicle']]=f
 assert len(flags)==36
 header='#pragma once\nnamespace mkw {\nstruct VehicleLodParts {bool wheels,handle;};\ninline constexpr VehicleLodParts VehicleLodLayout[]={\n'+''.join('{'+','.join(str(v).lower() for v in flags[i])+'},\n' for i in range(36))+'};\n}\n'
 (ROOT/'include/mkw/vehicle_lod_layout.hpp').write_text(header)
 (a.work/'receipt.json').write_text(json.dumps(receipt,indent=2));print('Vehicle LOD',len(receipt),'unique',len(cache),flush=True)
if __name__=='__main__':main()
