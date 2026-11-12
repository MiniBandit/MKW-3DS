"""Read freshly cooked local geometry with the same overlay order as packaging."""
import struct
from pathlib import Path
from course_catalog import ROOT
class LocalModels:
 def __init__(self):
  self.entries={}
  for directory in ('sd-card/3ds/mkw-3ds','build/course-materials','build/course-water-feedback','build/rainbow-final'):
   base=ROOT/directory
   for p in sorted((base/'models').glob('*.gmdl')):self.entries['models/'+p.name]=p
 def read(self,name):return self.entries[name].read_bytes()
 def close(self):pass
def model(d):
 magic,v,nv,ni,ns,nt=struct.unpack_from('<4s5I',d);assert magic==b'GMDL';stride={3:44,4:56,5:92}[v]
 subs=[struct.unpack_from('<2IiIiI4fI'+('3I' if v>=4 else '')+('3I6f' if v==5 else ''),d,24+i*stride) for i in range(ns)]
 at=24+ns*stride;names=[d[at+64*i:at+64*(i+1)].split(b'\0')[0].decode() for i in range(nt)];at+=64*nt
 vs=[struct.unpack_from('<10f4B',d,at+44*i) for i in range(nv)];at+=44*nv
 ids=struct.unpack_from('<%dH'%ni,d,at);return v,subs,names,vs,ids
