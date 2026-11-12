"""Offline poses copied from the retail-backed object presentation controllers.
Keeps DOBJ/source models read-only. No animation bank is added to runtime maps.
"""
import struct
import numpy as np
from functools import lru_cache

@lru_cache(None)
def bank(path):
 b=path.read_bytes();_,_,nv,nm,ns,nn,nc,_,_,_=struct.unpack_from('<4s9I',b)
 source=np.frombuffer(b,dtype=np.dtype([('p','<f4',3),('n','<f4',3),('m','<u4')]),count=nv,offset=40)
 at=40+nv*28+ns*8
 palette=np.frombuffer(b,'<f4',count=nm*12,offset=at).reshape(nm,3,4);at+=nm*48
 nodes={}
 for _ in range(nn):
  name=b[at:at+32].split(b'\0')[0].decode();parent,slot,env=struct.unpack_from('<3i',b,at+32)
  local=np.frombuffer(b,'<f4',count=12,offset=at+44).reshape(3,4)
  world=np.frombuffer(b,'<f4',count=12,offset=at+92).reshape(3,4)
  nodes[name]=(slot,local,world);at+=140
 clips=[]
 for _ in range(nc):
  frames,loop,step,na=struct.unpack_from('<4I',b,at+32);at+=48
  animated=np.frombuffer(b,'<u4',count=na,offset=at);at+=na*4
  base=np.frombuffer(b,'<f4',count=nm*12,offset=at).reshape(nm,3,4);at+=nm*48
  count=(frames-1+step-1)//step+1
  samples=np.frombuffer(b,'<f4',count=count*na*12,offset=at).reshape(count,na,3,4);at+=count*na*48
  clips.append((frames,step,animated,base,samples))
 return source,palette,nodes,clips

def mul(a,b):
 return np.column_stack((a[:,:3]@b[:,:3],a[:,:3]@b[:,3]+a[:,3]))

def skin(part,source,palette):
 m=palette[source['m']]
 part.position=np.einsum('nij,nj->ni',m[:,:,:3],source['p'])+m[:,:,3]
 part.normal=np.einsum('nij,nj->ni',m[:,:,:3],source['n'])

def pose(part,path,oid,state):
 if oid==0x208:
  angle=np.deg2rad(state*5);c,s=np.cos(angle),np.sin(angle)
  rot=np.array([[1,0,0],[0,c,-s],[0,s,c]])
  part.position=part.position@rot.T;part.normal=part.normal@rot.T
  return
 if oid==0x25e:
  # Same clipping of landing ends as escalator_landing.hpp, before live translation.
  dy,dz=10*state,-30*state;y=part.position[:,1]+dy;z=part.position[:,2]+dz
  z=np.where(y<0,z+3*y,z);y=np.clip(y,0,1750)
  # Map tiles live in the fixed landing frame. Animate treads inside it;
  # runtime removes the looping object translation from the map anchor.
  part.position[:,1]=y;part.position[:,2]=z
  return
 source,palette,nodes,clips=bank(path);palette=palette.copy()
 if oid==0x1ff:
  frames,step,animated,palette,samples=clips[0];palette=palette.copy()
  f=min((state%16)*89/15,frames-1);a=int(f)//step;nexta=min(a+1,len(samples)-1)
  t=(f-a*step)/max(1,min((a+1)*step,frames-1)-a*step)
  palette[animated]=samples[a]*(1-t)+samples[nexta]*t
  skin(part,source,palette)
  angle=np.deg2rad((state%16)*45/15)*(-1 if state>=16 else 1)
  c,s=np.cos(angle),np.sin(angle);rz=np.array([[c,-s,0],[s,c,0],[0,0,1]])
  # EGG makeRT is Rz * Ry * Rx: the opening is a WORLD Z rotation,
  # after each half's authored +/-90 degree yaw. Runtime retains that yaw,
  # so bake its conjugate, not a model-local Z roll (which rolls the width).
  yaw=np.deg2rad(-90 if state>=16 else 90);c,s=np.cos(yaw),np.sin(yaw)
  ry=np.array([[c,0,s],[0,1,0],[-s,0,c]])
  rot=ry.T@rz@ry
  part.position=part.position@rot.T;part.normal=part.normal@rot.T
  return
 if oid==0x1fe:
  parent=nodes['joint1'][2]
  for i in range(1,19):
   name=f'joint{i+2}'
   if name not in nodes:continue
   slot,local,_=nodes[name];local=local.copy();z=-1+2*(i+1)/20
   angle=.2*(z+1)**2*(z-1)**2*np.sin(state*3*2*np.pi/120+4*z)
   c,s=np.cos(angle),np.sin(angle);local[:,:3]=[[1,0,0],[0,c,-s],[0,s,c]]
   if slot>=0:palette[slot]=mul(parent,local)
 elif oid==0x202:
  narrow=np.array([[.8,0,0,0],[0,1,0,0],[0,0,1,0]])
  parent=mul(nodes['DKturibashiGC'][2],narrow) if 'DKturibashiGC' in nodes else narrow
  for i in range(37):
   name=f'joint0{i+1}'
   if name not in nodes:continue
   slot,local,_=nodes[name];local=local.copy();z=-1+2*(i+1)/38
   angle=0 if i==0 else .12*(z+1)**2*(z-1)**2*np.sin(np.pi*state*3*2/160+.7*z)
   c,s=np.cos(angle),np.sin(angle);local[:,:3]=[[c,s,0],[-s,c,0],[0,0,1]]
   if slot>=0:palette[slot]=mul(parent,local)
 elif oid==0x20e:
  # Its entire oscillation changes local Y only. Capture the original neutral pose.
  parent=nodes['joint1'][2]
  for k in range(1,24):
   name=f'joint{k+1}'
   if name not in nodes:continue
   slot,local,_=nodes[name];local=local.copy();phase=np.pi*2*600*k/13800
   local[0,3]=600*k;local[1,3]=-910*phase;local[2,3]=0
   if slot>=0:palette[slot]=mul(parent,local)
 else:raise ValueError(oid)
 skin(part,source,palette)
