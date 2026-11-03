"""Authored fireBPlane GX indirect sampling, offline only.

The sampler preserves decoded byte channels, bilinear clamp/mirror and Max SRT.
No game resources are modified. See the source parity evidence before reuse.
"""
import math
import numpy as np
def sample(tex,u,v,mirror=False):
 h,w=tex.shape[:2];x=u*w-.5;y=v*h-.5;ix=np.floor(x).astype(int);iy=np.floor(y).astype(int);fx=(x-ix)[...,None];fy=(y-iy)[...,None]
 def wrap(q,n):
  if not mirror:return np.clip(q,0,n-1)
  q=q%(2*n);return np.where(q<n,q,2*n-1-q)
 x0,x1=wrap(ix,w),wrap(ix+1,w);y0,y1=wrap(iy,h),wrap(iy+1,h)
 return (tex[y0,x0]*(1-fx)+tex[y0,x1]*fx)*(1-fy)+(tex[y1,x0]*(1-fx)+tex[y1,x1]*fx)*fy
# Max SRT matrix (brres.ts calcTexMtx_Max), translation zero.
def uv(u,v,scale,degrees):
 c=math.cos(math.radians(degrees));s=math.sin(math.radians(degrees))
 return scale*(c*(u-.5)+s*(v-.5))+.5,scale*(-s*(u-.5)+c*(v-.5))+.5
factor=.0099945068359375

def evaluate(a,b,u,v,frame,indirect=True):
 x,y=uv(u,v,1.149999976158142,2.4*frame)
 if indirect:
  iu,iv=uv(u,v,1.25,-1.2*frame);noise=sample(b,iu,iv,True)
  # GX indirect swizzle ABG, bias STU=-128, matrix0 diag factor.
  x=x+factor*(noise[...,3]-128)/a.shape[1]
  y=y+factor*(noise[...,2]-128)/a.shape[0]
 return sample(a,x,y)

def relative(a,b,x,y,frame):
    iu,iv=uv(x,y,1.25/1.149999976158142,-3.6*frame)
    noise=sample(b,iu,iv,True)
    return sample(a,x+factor*(noise[...,3]-128)/64,y+factor*(noise[...,2]-128)/64)
