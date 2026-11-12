#!/usr/bin/env python3
"""Unpack local Yaz0/U8 archives with strict bounds and safe paths."""
import argparse,io,struct
from pathlib import Path
from gclib.yaz0_yay0 import Yaz0

def unpack(source,output):
 b=source.read_bytes()
 if b[:4]==b'Yaz0':b=Yaz0.decompress(io.BytesIO(b)).getvalue()
 if b[:4]!=b'U\xaa8-':raise ValueError('not a U8 archive')
 root=struct.unpack_from('>I',b,4)[0];count=struct.unpack_from('>I',b,root+8)[0];strings=root+count*12
 stack=[(Path(),count)];output.mkdir(parents=True,exist_ok=True)
 for i in range(1,count):
  while i>=stack[-1][1]:stack.pop()
  info,offset,size=struct.unpack_from('>III',b,root+i*12);start=strings+(info&0xffffff);name=b[start:b.index(0,start)].decode()
  if '/' in name or '\\' in name or name == '..' or (name == '.' and not info>>24):raise ValueError('unsafe archive path')
  relative=stack[-1][0]/name;target=output/relative
  if info>>24:stack.append((relative,size));target.mkdir(exist_ok=True,parents=True)
  else:
   if offset+size>len(b):raise ValueError('truncated archive')
   target.parent.mkdir(exist_ok=True,parents=True);target.write_bytes(b[offset:offset+size])
 print(source,'->',output,count-1,'entries')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args();unpack(a.source,a.output)
