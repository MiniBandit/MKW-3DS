#!/usr/bin/env python3
"""Read-only extraction of selected files from the user's Wii ISO/WBFS.

Wii partition/FST layout reference: Dolphin DiscIO VolumeWii.cpp and FileSystemGCWii.cpp.
Does not download data, modify the disc, or extract executable code.
"""
import argparse,functools,hashlib,json,struct
from course_catalog import courses
from pathlib import Path
from Crypto.Cipher import AES
U32=lambda b,o=0:struct.unpack_from('>I',b,o)[0]
class Disc:
 def __init__(self,path):
  self.f=path.open('rb');header=self.f.read(512);self.wbfs=header[:4]==b'WBFS'
  if self.wbfs:
   hd=1<<header[8];self.wbfs_size=1<<header[9];slot=next((v for v in header[12:] if v),None)
   if not slot:raise ValueError('empty WBFS')
   self.f.seek(slot*hd+0x100);n=(143432*2)//(self.wbfs_size//0x8000)
   self.map=struct.unpack('>'+str(n)+'H',self.f.read(n*2))
 def read(self,o,n):
  if not self.wbfs:self.f.seek(o);b=self.f.read(n)
  else:
   chunks=[]
   while n:
    block,offset=divmod(o,self.wbfs_size);take=min(n,self.wbfs_size-offset);physical=self.map[block]
    if physical:self.f.seek(physical*self.wbfs_size+offset);chunks.append(self.f.read(take))
    else:chunks.append(bytes(take))
    o+=take;n-=take
   b=b''.join(chunks);n=len(b)
  if len(b)!=n:raise ValueError('truncated disc image')
  return b
class Partition:
 def __init__(self,disc,p):
  self.disc=disc;self.p=p;t=disc.read(p,0x2c0)
  if t[0x1f1]!=0:raise ValueError('Only standard retail Wii common-key index is supported')
  self.key=AES.new(bytes.fromhex('ebe42a225e8593e448d9c5457381aaf7'),AES.MODE_CBC,t[0x1dc:0x1e4]+bytes(8)).decrypt(t[0x1bf:0x1cf])
  self.data=p+U32(t,0x2b8)*4
 @functools.lru_cache(maxsize=128)
 def block(self,i):
  b=self.disc.read(self.data+i*0x8000,0x8000)
  plain=AES.new(self.key,AES.MODE_CBC,b[0x3d0:0x3e0]).decrypt(b[0x400:])
  # Verify H0 hashes so a damaged/incomplete archive cannot silently supply game data.
  hashes=AES.new(self.key,AES.MODE_CBC,bytes(16)).decrypt(b[:0x400])
  for j in range(31):
   if hashlib.sha1(plain[j*0x400:(j+1)*0x400]).digest()!=hashes[j*20:(j+1)*20]:raise ValueError(f'Wii sector H0 mismatch at block {i}, chunk {j}')
  return plain
 def read(self,o,n):
  chunks=[]
  while n:
   block,offset=divmod(o,0x7c00);take=min(n,0x7c00-offset);chunks.append(self.block(block)[offset:offset+take]);o+=take;n-=take
  return b''.join(chunks)
def main():
 a=argparse.ArgumentParser();a.add_argument('disc',type=Path);a.add_argument('output',type=Path);a.add_argument('--list-only',action='store_true')
 # One named file only (the release builder takes rel/StaticR.rel, read as data by the menu cookers).
 a.add_argument('--file');a.add_argument('--to',type=Path)
 # Whole folders instead of the race selection (menus, movies, sound), with their own receipt.
 a.add_argument('--prefix',action='append');args=a.parse_args();d=Disc(args.disc)
 h=d.read(0,0x100)
 if U32(h,0x18)!=0x5d1c9ea3:raise ValueError('Not a Wii ISO/WBFS. Convert RVZ with Dolphin first.')
 identity={'game_id':h[:6].decode('ascii'),'disc_number':h[6],'revision':h[7],'source':str(args.disc),'source_bytes':args.disc.stat().st_size}
 if not h.startswith(b'RMC'):raise ValueError('Not Mario Kart Wii')
 candidates=[]
 for g in range(4):
  n,table=struct.unpack('>II',d.read(0x40000+g*8,8))
  if n>32:raise ValueError('invalid partition count')
  for i in range(n):
   offset,kind=struct.unpack('>II',d.read(table*4+i*8,8))
   if kind==0:candidates.append(offset*4)
 if len(candidates)!=1:raise ValueError('expected one data partition')
 p=Partition(d,candidates[0]);boot=p.read(0,0x440)
 if boot[:6]!=h[:6]:raise ValueError('decrypted partition identity mismatch')
 fst_offset,fst_size=U32(boot,0x424)*4,U32(boot,0x428)*4;fst=p.read(fst_offset,fst_size);count=U32(fst,8);names=count*12
 if names>len(fst):raise ValueError('invalid FST')
 files=[];stack=[('',count)]
 for i in range(1,count):
  while i>=stack[-1][1]:stack.pop()
  info,offset,size=struct.unpack_from('>III',fst,i*12);at=names+(info&0xffffff);end=fst.index(0,at);name=fst[at:end].decode('utf-8')
  if '/' in name or name in ('.','..'):raise ValueError('unsafe FST name')
  name=stack[-1][0]+name
  if info>>24:stack.append((name+'/',size));continue
  files.append({'name':name,'offset':offset*4,'size':size})
 if args.file:
  f=next((f for f in files if f['name']==args.file),None)
  if f is None or not args.to:raise ValueError('--file needs --to and a file on this disc')
  b=p.read(f['offset'],f['size']);args.to.parent.mkdir(parents=True,exist_ok=True);args.to.write_bytes(b);print(args.file,len(b),hashlib.sha256(b).hexdigest());return
 args.output.mkdir(parents=True,exist_ok=True);(args.output/'disc-files.json').write_text(json.dumps({'identity':identity,'files':files},indent=2)+'\n')
 print(json.dumps(identity))
 if args.list_only:return
 receipt=[]
 course_archives={c["archive"] for c in courses()}
 for f in files:
  n=f['name']
  if args.prefix:
   if not n.startswith(tuple(args.prefix)):continue
  elif n not in course_archives and n not in ('Race/Common.szs','Race/Kart/ld_bike-fk.szs') and not (n.startswith('Race/Kart/') and 'mdf_kart' in n and ('mr' in n or n.endswith('/mdf_kart.szs'))) and not (n.startswith('Race/Driver/') and (n.endswith('/mr.szs') or n.endswith('/mario.szs'))):continue
  b=p.read(f['offset'],f['size']);target=args.output/n;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b);receipt.append({'name':n,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()});print(n,len(b))
 name='extraction-receipt'+''.join('-'+x.strip('/').replace('/','-') for x in args.prefix or [])+'.json'
 (args.output/name).write_text(json.dumps({'identity':identity,'sector_h0_verified':True,'files':receipt},indent=2)+'\n')
if __name__=='__main__':main()
