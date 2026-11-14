#!/usr/bin/env python3
"""Bake source GX raster lighting, TEV stages and environment maps to unique UVs.

MenuSingle BLIGHT/BLMAP is the light profile. The project retail generator
supplies lm_0/lm_1; original grayscale metal maps and authored SRT are sampled.
This is a fixed-view, fixed-pose authoring bake, not a dynamic Wii renderer.
prepare_home_banner.py runs this module for its evaluator (raster lighting, light
maps, view) and the decoded source images.
"""
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path
import numpy as np
from PIL import Image

from banner_paths import ROOT, WORK
OUT=WORK/'baked';OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'tools'))
from cook_course_lighting import parse_blight,parse_blmap
recipe=json.loads((WORK/'exports/scene_recipe.json').read_text())
lighting=parse_blight((WORK/'source/menu_lighting/default.blight').read_bytes())
lighting['textures']=parse_blmap((WORK/'source/menu_lighting/default.blmap').read_bytes())['textures']
positions=np.load(WORK/'exports/funky_action_positions.npz')['jump']
normals=np.load(WORK/'exports/funky_action_normals.npz')['jump']
BAKE_FRAME=0
CAMERA=np.array([240.,200.,320.])
TARGET=np.array([0.,62.,5.])
unit=lambda x:x/np.maximum(1e-12,np.linalg.norm(x,axis=-1,keepdims=True))
forward=unit(TARGET-CAMERA);right=unit(np.cross(forward,[0.,1.,0.]));up=np.cross(right,forward)
view=np.array([right,up,-forward])
subprocess.run([str(WORK/'tools/export_menu_light_textures'),str(OUT),*[str(v) for v in view.ravel()]],check=True)
directions=np.fromfile(OUT/'view_light_directions.f32',dtype='<f4').reshape(16,3)
light_maps={}
for i in range(2):
    rgba=np.fromfile(OUT/f'lm_{i}.rgba',dtype=np.uint8).reshape(32,32,4)
    Image.fromarray(rgba).save(OUT/f'lm_{i}.png');light_maps[f'lm_{i}']=rgba/255.


def index_wrap(i,size,mode):
    if mode==0:return np.clip(i,0,size-1)
    if mode==1:return i%size
    assert mode==2
    i=i%(2*size)
    return np.where(i<size,i,2*size-1-i)


def sample(image,uv,sampler):
    h,w=image.shape[:2]
    # Inputs use GX top-left texture coordinates. Byte values are evaluated
    # in GX colour space, not Blender's scene-linear space.
    x=uv[:,0]*w-.5;y=uv[:,1]*h-.5
    x0=np.floor(x).astype(int);y0=np.floor(y).astype(int);fx=x-x0;fy=y-y0
    x1=index_wrap(x0+1,w,sampler['wrapS']);y1=index_wrap(y0+1,h,sampler['wrapT'])
    x0=index_wrap(x0,w,sampler['wrapS']);y0=index_wrap(y0,h,sampler['wrapT'])
    a=image[y0,x0]*(1-fx[:,None])+image[y0,x1]*fx[:,None]
    b=image[y1,x0]*(1-fx[:,None])+image[y1,x1]*fx[:,None]
    return a*(1-fy[:,None])+b*fy[:,None]


def rgba(c):return np.array([c[k] for k in ('r','g','b','a')],dtype=float)


def raster(material,source_colors,normal):
    channel=material['gxMaterial']['lightChannels'][0]
    idx=material['lightSetIdx']
    if not 0<=idx<8:return source_colors
    light=lighting['lights'][idx]
    # G3D light set k = light k + ambient 0, as recovered by the project.
    ambient=np.array(lighting['ambients'][0])/255.
    mat_ambient=rgba(material['colorAmbRegs'][0])
    nview=normal@view.T
    dot=np.sum(nview*(-directions[idx]),axis=1)
    out=source_colors.copy()
    for key,cols in (('colorChannel',slice(0,3)),('alphaChannel',slice(3,4))):
        ch=channel[key]
        if not ch['lightingEnabled']:continue
        diff=ch['diffuseFunction']
        d=np.ones(len(dot)) if diff==0 else dot if diff==1 else np.maximum(dot,0)
        a=ambient[cols]*mat_ambient[cols]
        value=np.clip(a+np.array(light['color'])[cols]/255.*d[:,None],0,1)
        out[:,cols]*=value
    return out


def konst(material,selector,alpha=False):
    if selector<8:value=np.full(4,(8-selector)/8)
    elif not alpha and 12<=selector<=15:value=rgba(material['colorConstants'][selector-12])
    else:
        assert 16<=selector<=31,selector
        c=material['colorConstants'][(selector-16)%4]
        component=('r','g','b','a')[(selector-16)//4]
        value=np.full(4,c[component])
    return value[3] if alpha else value[:3]


def tev(material,textures,raster_color):
    count=len(raster_color)
    regs=[np.tile(rgba(c),(count,1)) for c in material['colorRegisters']]
    scales=(1,2,4,.5)
    for stage,tex in zip(material['gxMaterial']['tevStages'],textures):
        assert stage['texSwapTable']==[0,1,2,3] and stage['rasSwapTable']==[0,1,2,3]
        kc=konst(material,stage['konstColorSel']);ka=konst(material,stage['konstAlphaSel'],True)
        def color(k):
            if k<8:return regs[k//2][:,:3] if k%2==0 else np.repeat(regs[k//2][:,3:4],3,axis=1)
            if k==8:return tex[:,:3]
            if k==9:return np.repeat(tex[:,3:4],3,axis=1)
            if k==10:return raster_color[:,:3]
            if k==11:return np.repeat(raster_color[:,3:4],3,axis=1)
            return {12:1.,13:.5,14:kc,15:0.}[k]
        def alpha(k):
            if k<4:return regs[k][:,3]
            return {4:tex[:,3],5:raster_color[:,3],6:ka,7:0.}[k]
        a,b,c,d=[color(stage['colorIn'+k]) for k in 'ABCD']
        aa,bb,cc,dd=[alpha(stage['alphaIn'+k]) for k in 'ABCD']
        assert stage['colorOp'] in (0,1) and stage['alphaOp'] in (0,1)
        rgb=d+(-1 if stage['colorOp'] else 1)*((1-c)*a+c*b)
        al=dd+(-1 if stage['alphaOp'] else 1)*((1-cc)*aa+cc*bb)
        rgb=(rgb+(0,.5,-.5)[stage['colorBias']])*scales[stage['colorScale']]
        al=(al+(0,.5,-.5)[stage['alphaBias']])*scales[stage['alphaScale']]
        if stage['colorClamp']:rgb=np.clip(rgb,0,1)
        if stage['alphaClamp']:al=np.clip(al,0,1)
        regs[stage['colorRegId']][:,:3]=rgb
        regs[stage['alphaRegId']][:,3]=al
    return regs[0]


def material_samples(folder,data,shape,weights,triangle,normal_triangle):
    material=shape['sourceMaterial']
    ras=raster(material,triangle[:,10:14]/255.,normal_triangle)
    ras=weights@ras
    primary_uv=triangle[:,6:8].copy();primary_uv[:,1]=1-primary_uv[:,1]
    nview=unit(normal_triangle@view.T)
    env=np.column_stack((.5+.5*nview[:,0],.5-.5*nview[:,1]))
    stage_samples=[]
    for stage in material['gxMaterial']['tevStages']:
        tex_map=stage['texMap']
        if tex_map==255:
            stage_samples.append(np.ones((len(weights),4)));continue
        sampler=material['samplers'][tex_map]
        name=sampler['name']
        if name in light_maps:image=light_maps[name]
        else:
            image=IMAGE_CACHE[(folder.name,name)]
        coord=stage['texCoordId']
        texgen=material['gxMaterial']['texGens'][coord]
        if texgen['source']==1:
            matrix=material['texSrts'][coord]['srtMtx']
            at=lambda i:matrix[str(i)]
            uv=np.column_stack((env[:,0]*at(0)+env[:,1]*at(4)+at(12),env[:,0]*at(1)+env[:,1]*at(5)+at(13)))
        else:
            assert texgen['source']==4
            uv=primary_uv
        stage_samples.append(sample(image,weights@uv,sampler))
    return tev(material,stage_samples,ras)


IMAGE_CACHE={}
datasets={}
for folder_name in ('funky_pose','flame_runner'):
    folder=WORK/'exports'/folder_name
    data=json.loads((folder/'model.json').read_text());datasets[folder_name]=data
    for t in data['textures']:
        IMAGE_CACHE[(folder_name,t['name'])]=np.array(Image.open(folder/(t['file']+'.png')).convert('RGBA'))/255.

