#!/usr/bin/env python3
"""Host-only RFL portrait bake. Input is export_mii_head's assembled head.

Uses the inspected retail icon camera/material/light contract. Raster coverage
and RGBA8 output are an offline adaptation, not GX EFB bit-exact emulation.
No emulator or game process is launched. Output is straight-alpha RGBA PNG.
"""
import argparse,json,math
from pathlib import Path
import numpy as np
from PIL import Image

SKIN=[0xf0d8c4,0xffbc80,0xd88850,0xffb090,0x985030,0x522e1c]
HAIR=[0x1e1a18,0x382015,0x552617,0x704024,0x727278,0x49361a,0x7a5928,0xc19f64]
GLASSES=[0x101010,0x603810,0x981810,0x203060,0x905800,0x605850]
CAP=[0xb84030,0xf07828,0xf8d820,0x80c828,0x007428,0x204898,0x40a0d8,0xe86078,0x702ca8,0x483818,0xe0e0e0,0x181814]

def rgb(value):
    return np.array([(value>>16)&255,(value>>8)&255,value&255],dtype=float)/255

def sample(texture,u,v,wrap):
    h,w,_=texture.shape
    def indices(coord,size,mode):
        if mode==1:return coord%size
        if mode==2:
            coord=coord%(size*2)
            return np.where(coord<size,coord,2*size-1-coord)
        return np.clip(coord,0,size-1)
    x=u*w-.5;y=v*h-.5;x0=np.floor(x).astype(int);y0=np.floor(y).astype(int)
    fx=(x-x0)[...,None];fy=(y-y0)[...,None]
    a=texture[indices(y0,h,wrap[1]),indices(x0,w,wrap[0])]
    b=texture[indices(y0,h,wrap[1]),indices(x0+1,w,wrap[0])]
    c=texture[indices(y0+1,h,wrap[1]),indices(x0,w,wrap[0])]
    d=texture[indices(y0+1,h,wrap[1]),indices(x0+1,w,wrap[0])]
    return (a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy

def bake(source,size=128,samples=2,statue_material=None):
    data=json.loads((source/'head.json').read_text());n=size*samples
    pixels=np.zeros((n,n,4));depth=np.full((n,n),np.inf)
    focal=n/(2*math.tan(math.radians(data['fov'])/2))
    skin,hair,beard,glasses,cap=data['colors']
    constants={0:rgb(SKIN[skin]),1:rgb(SKIN[skin]),2:rgb(SKIN[skin]),3:rgb(HAIR[hair]),4:rgb(CAP[cap]),5:rgb(HAIR[beard]),6:rgb(0),8:rgb(GLASSES[glasses])}
    triangles=0
    for kind in [5,0,1,3,4,2,7,6,8]:
        part=data['parts'][kind]
        if not part['indices']:continue
        vertices=np.array(part['vertices'],dtype=float);z=600-vertices[:,2]
        statue_coords=statue_material.coordinates(vertices) if statue_material is not None else None
        screen=np.column_stack((n/2+focal*vertices[:,0]/z,n/2-focal*(vertices[:,1]-34.5)/z))
        texture=None
        w,h=part['size']
        if w and h:texture=np.frombuffer((source/f'part-{kind}.rgba').read_bytes(),dtype=np.uint8).reshape(h,w,4).astype(float)/255
        indices=np.array(part['indices']).reshape(-1,3)
        if part['mirror']:indices=indices[:,[0,2,1]]
        for tri in indices:
            points=screen[tri];tz=z[tri]
            if np.any(tz<500) or np.any(tz>700):raise ValueError('Portrait geometry outside original camera depth range')
            (ax,ay),(bx,by),(cx,cy)=points
            area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax)
            # Expanded RFL primitive lists have positive front-face image area.
            if abs(area)<1e-10 or (kind!=8 and area<=0):continue
            lo=np.maximum(np.floor(points.min(axis=0)).astype(int),0)
            hi=np.minimum(np.ceil(points.max(axis=0)).astype(int),n-1)
            if np.any(hi<lo):continue
            yy,xx=np.mgrid[lo[1]:hi[1]+1,lo[0]:hi[0]+1];x=xx+.5;y=yy+.5
            b1=((x-ax)*(cy-ay)-(y-ay)*(cx-ax))/area
            b2=((bx-ax)*(y-ay)-(by-ay)*(x-ax))/area;b0=1-b1-b2
            weights=np.stack((b0,b1,b2),axis=-1)
            valid=np.min(weights,axis=-1)>=-1e-9
            inv=weights/tz;den=inv.sum(axis=-1);fragz=1/den
            valid&=fragz<=depth[yy,xx]+1e-7
            if not valid.any():continue
            corrected=inv/den[...,None]
            attrs=corrected@vertices[tri,3:6];light=attrs[...,2:3]/255
            tex=sample(texture,attrs[...,0],attrs[...,1],part['wrap']) if texture is not None else None
            if kind==2:color=constants[kind]*(1-tex[...,1:2])+tex[...,[0,3,2]]*tex[...,1:2]
            elif kind==4:color=constants[kind]*(tex[...,:3]+1)/2
            elif kind==7:color=tex[...,:3]
            elif kind==8:color=constants[kind]*tex[...,:3]
            else:color=np.broadcast_to(constants[kind],(*valid.shape,3))
            alpha=tex[...,3] if kind>=6 else np.ones(valid.shape)
            valid&=alpha>0
            color=(statue_material.shade(color,corrected@statue_coords[tri],kind<6,sample)
                   if statue_material is not None else np.clip(color*light,0,1))
            a=alpha[valid,None];dst=pixels[yy[valid],xx[valid]]
            dst[:,:3]=color[valid]*a+dst[:,:3]*(1-a)
            dst[:,3:]=a+dst[:,3:]*(1-a)
            pixels[yy[valid],xx[valid]]=dst
            if kind<6 or statue_material is not None:depth[yy[valid],xx[valid]]=fragz[valid]
            triangles+=1
    # Downsample premultiplied color first to avoid dark transparent borders.
    pixels=pixels.reshape(size,samples,size,samples,4).mean(axis=(1,3))
    np.divide(pixels[:,:,:3],pixels[:,:,3:],out=pixels[:,:,:3],where=pixels[:,:,3:]>0)
    return Image.fromarray(np.uint8(np.clip(pixels*255+.5,0,255))),triangles

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--size',type=int,choices=[32,64,128,256],default=128)
    p.add_argument('--statue-course',type=Path,help='Bake original course.bmm/btiMat/btiEnv material into the portrait')
    a=p.parse_args()
    from mii_statue_material import StatueMaterial
    material=StatueMaterial(a.statue_course) if a.statue_course else None
    im,triangles=bake(a.source,a.size,statue_material=material);a.output.parent.mkdir(parents=True,exist_ok=True);im.save(a.output)
    print(json.dumps(dict(output=str(a.output),triangles=triangles,alphaBounds=im.getchannel('A').getbbox(),size=im.size)))
if __name__=='__main__':main()
