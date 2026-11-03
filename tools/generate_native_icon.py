#!/usr/bin/env python3
"""Convert the pinned public CFL-tool icon body at build time; no mesh in Git."""
import struct,subprocess
from pathlib import Path
from course_catalog import ROOT
PIN='be72a29be54b04201372516aa64bab129e3ead7a'
def main():
 repo=ROOT/'vendor/cfl-tool'
 if not repo.exists():raise FileNotFoundError('Acquire the pinned cfl-tool checkout manually; see dependencies.json and docs/MANUAL-BUILD.md')
 actual=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
 if actual!=PIN:raise ValueError('cfl-tool revision mismatch')
 b=(repo/'data/IconBody.iqm').read_bytes()
 if b[:16]!=b'INTERQUAKEMODEL\0':raise ValueError('Not IQM')
 h=struct.unpack_from('<27I',b,16);text=b[h[4]:h[4]+h[3]];attrs={}
 for i in range(h[7]):
  kind,flags,fmt,size,off=struct.unpack_from('<5I',b,h[9]+20*i)
  if kind in (0,2):
   if (fmt,size)!=(7,3):raise ValueError('Unsupported icon attribute')
   attrs[kind]=[struct.unpack_from('<3f',b,off+12*j) for j in range(h[8])]
 indices=struct.unpack_from('<'+str(h[10]*3)+'I',b,h[11]);neck=None
 for i in range(h[13]):
  off=h[14]+i*48;name=struct.unpack_from('<I',b,off)[0]
  if text[name:].split(b'\0')[0]==b'head':neck=struct.unpack_from('<f',b,off+12)[0]
 if neck is None or max(indices)>=256:raise ValueError('Unexpected icon topology')
 f=lambda x:repr(x)+'f'
 rows=['{'+','.join('{'+','.join(map(f,attrs[k][i]))+'}' for k in (0,2))+',{0,0}}' for i in range(h[8])]
 s='#pragma once\nnamespace mkw {\nstruct NativeMiiIconVertex {float position[3],normal[3],uv[2];};\ninline constexpr NativeMiiIconVertex NativeMiiIconBody[]={\n'+',\n'.join(rows)+'\n};\ninline constexpr unsigned char NativeMiiIconBodyIndices[]={'+','.join(map(str,indices))+'};\ninline constexpr float NativeMiiIconNeckY='+f(neck)+';\n}\n'
 (ROOT/'include/mkw/native_mii_icon_body.hpp').write_text(s)
if __name__=='__main__':main()
