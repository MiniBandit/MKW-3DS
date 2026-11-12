#!/usr/bin/env python3
"""Cook original RFL records into an indexed, on-demand native part bank."""
import argparse,struct
from pathlib import Path
from rfl_resource import Resource,SHAPE_GROUPS,shape_data,texture_image

def cook(source):
 resource=Resource(source);payloads=[]
 for group,g in enumerate(resource.groups):
  for index,record in enumerate(g.records):
   if group in SHAPE_GROUPS:
    kind=0;shape_type=SHAPE_GROUPS.index(group);s=shape_data(record,shape_type);vertices=[];indices=[];lookup={}
    def vert(ref):
     if ref not in lookup:
      lookup[ref]=len(vertices);vertices.append((*s['positions'][ref[0]],*s['normals'][ref[1]],*(s['uvs'][ref[2]] if len(ref)==3 else (0,0))))
     return lookup[ref]
    for primitive in s['primitives']:
     refs=primitive['indices']
     if primitive['kind']==0x90:
      if len(refs)%3:raise ValueError('incomplete triangles')
      triangles=[refs[i:i+3] for i in range(0,len(refs),3)]
     elif primitive['kind']==0x98:triangles=[(refs[i-1],refs[i-2],refs[i]) if i%2 else (refs[i-2],refs[i-1],refs[i]) for i in range(2,len(refs))]
     else:raise ValueError('unsupported primitive')
     for tri in triangles:indices.extend(vert(tuple(ref)) for ref in tri)
    payload=struct.pack('<III9f',shape_type,len(vertices),len(indices),*(s['anchors'] or [0]*9))
    payload+=b''.join(struct.pack('<8h',*v) for v in vertices)+struct.pack(f'<{len(indices)}H',*indices)
   else:
    kind=1;im=texture_image(record);payload=struct.pack('<4I',im.width,im.height,record[6],record[7])+im.tobytes()
   payloads.append((group,index,kind,payload))
 offset=16+20*len(payloads);directory=[]
 for group,index,kind,payload in payloads:directory.append(struct.pack('<5I',group,index,kind,offset,len(payload)));offset+=len(payload)
 return struct.pack('<4s3I',b'MPR1',1,len(payloads),16)+b''.join(directory)+b''.join(p[3] for p in payloads)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
 data=cook(a.source.read_bytes());a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data);print(len(data),'bytes; 472 indexed records')
