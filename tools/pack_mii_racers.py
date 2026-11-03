#!/usr/bin/env python3
"""Merge Mii race/menu/award resources into MPK1 with texture sharing.
Geometry/materials remain unchanged except the texture-name table. Base entries
are preserved byte for byte. Reads stream, so no full race archive is in RAM.
"""
import argparse,hashlib,json,os,struct
from pathlib import Path
from gx_material import GMDL_SUBMESH_BYTES

def index(f):
 magic,n,size=struct.unpack('<4sII',f.read(12));assert magic==b'MPK1'
 rows=[struct.unpack('<III',f.read(12)) for _ in range(n)];names=f.read(size)
 return {names[a:names.index(b'\0',a)].decode():(o,s) for a,o,s in rows}
def digest(f,offset,size):
 f.seek(offset);h=hashlib.sha256()
 while size:
  b=f.read(min(size,1<<20));assert b;h.update(b);size-=len(b)
 return h.hexdigest()
def merge(base,overlay,output):
 # Canonical *_000 exports are duplicated by the race aliases. Keep only
 # aliases, menu machines, and original award parts used by the runtime.
 models=sorted(p for p in (overlay/'models').glob('*.gmdl')
               if (p.name.startswith('mii_') and ('_000' not in p.stem or p.stem.endswith('menu_000')))
               or p.name.startswith('award_unpacked_Award_'))
 assert models,'No Mii model aliases'
 temp=output.with_suffix(output.suffix+'.tmp');output.parent.mkdir(parents=True,exist_ok=True)
 with base.open('rb') as source:
  old=index(source);canonical={};original={}
  for name,(offset,size) in old.items():
   h=digest(source,offset,size);original[name]=h
   if name.startswith('textures/') and name.endswith('.t3x'):canonical.setdefault(h,name)
  added={};rewrite={};refs=set();reuse=0
  for p in (overlay/'models').glob('*.dskm'):
   if p.name.startswith(('mii_','ui_mii_','award_unpacked_Award_')):added['models/'+p.name]=p.read_bytes()
  for model in models:
   b=bytearray(model.read_bytes());magic,version,nv,ni,ns,nt=struct.unpack_from('<4s5I',b);assert magic==b'GMDL'
   start=24+ns*GMDL_SUBMESH_BYTES[version]
   for i in range(nt):
    at=start+i*64;name=bytes(b[at:at+64]).split(b'\0')[0].decode();relative='textures/'+name+'.t3x'
    if relative not in rewrite:
     data=(overlay/relative).read_bytes();h=hashlib.sha256(data).hexdigest()
     if h in canonical:reuse+=1
     else:
      target='textures/miitex_'+h[:32]+'.t3x';canonical[h]=target;added[target]=data
     rewrite[relative]=canonical[h]
    target=rewrite[relative];refs.add(target);stem=Path(target).stem.encode();assert len(stem)<64
    b[at:at+64]=stem.ljust(64,b'\0')
   name='models/'+model.name
   if name in old:assert name.startswith(('models/award_unpacked_Award_','models/mii_')),name
   added[name]=bytes(b)
  allnames=sorted(old.keys()|added.keys());blob=b''.join(n.encode()+b'\0' for n in allnames)
  at=(12+len(allnames)*12+len(blob)+31)&~31;rows=[];nameAt=0
  for name in allnames:
   size=len(added[name]) if name in added else old[name][1]
   rows.append((nameAt,at,size));nameAt+=len(name.encode())+1;at+=(size+31)&~31
  assert at<1<<31
  with temp.open('wb') as out:
   out.write(struct.pack('<4sII',b'MPK1',len(rows),len(blob)))
   for row in rows:out.write(struct.pack('<III',*row))
   out.write(blob);out.write(bytes((-out.tell())%32))
   for name,(_,offset,size) in zip(allnames,rows):
    assert out.tell()==offset
    if name in added:out.write(added[name])
    else:
     source.seek(old[name][0]);left=size
     while left:
      data=source.read(min(left,1<<20));assert data;out.write(data);left-=len(data)
    out.write(bytes((-size)%32))
  with temp.open('rb') as check:
   entries=index(check);assert set(entries)==set(allnames) and refs<=entries.keys()
   for name,(offset,size) in entries.items():
    expected=hashlib.sha256(added[name]).hexdigest() if name in added else original[name]
    assert digest(check,offset,size)==expected,name
   packagehash=digest(check,0,temp.stat().st_size)
 os.replace(temp,output)
 report={'passed':True,'base_entries_preserved':len(old.keys()-added.keys()),'replaced_entries':sorted(old.keys()&added.keys()),'models':len(models),'texture_names_before':len(rewrite),'unique_texture_references_after':len(refs),'added_textures':sum(n.startswith('textures/') for n in added),'reused_texture_files':reuse,'bytes':output.stat().st_size,'sha256':packagehash,'all_entries_reverse_checked':True}
 output.with_suffix(output.suffix+'.mii-receipt.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--overlay',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(json.dumps(merge(a.base,a.overlay,a.output),indent=2))
