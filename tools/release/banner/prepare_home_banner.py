"""Prepare source GX textures, baked vertex light/env UV and rigid bone data.
The intermediate glTF carries bone-local mode-1 positions for our CGFX converter.
"""
import json, math, struct, shutil, hashlib
from pathlib import Path
import numpy as np
from PIL import Image
from banner_paths import ROOT, WORK as W, OUTPUT as O, SCRIPTS;O.mkdir(exist_ok=True)
# Reuse the already validated original GX lighting evaluator without rebaking.
bakefile=SCRIPTS/'bake_original_materials.py'
g={'__file__':str(bakefile)}
exec(bakefile.read_text().split("meshes=[bake_model")[0],g)
recipe=g['recipe'];datasets=g['datasets']
bank=(W/'exports/mario_all_actions.dskm').read_bytes()
_,_,nv,npal,nclip,nshape,_=struct.unpack_from('<4s6I',bank)
local=np.frombuffer(bank,dtype=np.dtype([('p','<f4',3),('n','<f4',3),('j','<u4')]),count=nv,offset=32)
off=32+nv*28+nshape*8
for _ in range(nclip):
 name=bank[off:off+32].split(b'\0')[0].decode();duration,loop,nf=struct.unpack_from('<3I',bank,off+32);off+=44
 if name=='good':
  poses=np.stack([np.frombuffer(bank,dtype='<f4',count=npal*21,offset=off+f*(npal*84+nshape*4)).reshape(npal,21)[:,:12].reshape(npal,3,4) for f in range(nf)])
 off+=nf*(npal*84+nshape*4)
poses=poses.astype(float);poses[:,:,:,3]/=100
# Matrix columns carry scale; source palette transforms are global rigid TRS.
from soften_banner_head_nod import soften_head_nod, soften_torso_lean
poses=soften_head_nod(soften_torso_lean(poses))
scale=np.linalg.norm(poses[:,:,:,:3],axis=2);rot=poses[:,:,:,:3]/scale[:,:,None,:]
euler=np.stack((np.arctan2(rot[:,:,2,1],rot[:,:,2,2]),np.arcsin(np.clip(-rot[:,:,2,0],-1,1)),np.arctan2(rot[:,:,1,0],rot[:,:,0,0])),axis=-1)
euler=np.unwrap(euler,axis=0)
def matrix(t,r,s):
 x,y,z=r;cx,sx=math.cos(x),math.sin(x);cy,sy=math.cos(y),math.sin(y);cz,sz=math.cos(z),math.sin(z)
 R=np.array([[cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx],[sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx],[-sy,cy*sx,cy*cx]])
 out=np.eye(4);out[:3,:3]=R*np.array(s)[None,:];out[:3,3]=t;return out
error=max(float(np.abs(matrix(poses[f,j,:,3],euler[f,j],scale[f,j])[:3]-poses[f,j]).max()) for f in range(len(poses)) for j in range(npal))
assert error<1e-5,error
buf=bytearray();doc=dict(asset={'version':'2.0','generator':'MKW original GX to HOME mode1 intermediate'},scene=0,scenes=[{'nodes':[0]}],nodes=[],meshes=[],materials=[],textures=[],images=[],samplers=[],skins=[],accessors=[],bufferViews=[],buffers=[])
def acc(values,kind,ctype=5126):
 a=np.asarray(values,dtype={5126:'<f4',5123:'<u2',5121:'u1'}[ctype]);pad=(-len(buf))%4;buf.extend(bytes(pad));start=len(buf);buf.extend(a.tobytes());bv=len(doc['bufferViews']);doc['bufferViews'].append(dict(buffer=0,byteOffset=start,byteLength=a.nbytes));i=len(doc['accessors']);v=dict(bufferView=bv,componentType=ctype,count=len(a),type=kind)
 if ctype==5126:v.update(min=np.atleast_1d(a.min(axis=0)).tolist(),max=np.atleast_1d(a.max(axis=0)).tolist())
 if ctype==5121 and kind=='VEC4':v['normalized']=True
 doc['accessors'].append(v);return i
def node(name,parent=None,t=(0,0,0),r=(0,0,0),s=(1,1,1)):
 x,y,z=np.array(r)/2;cx,sx=math.cos(x),math.sin(x);cy,sy=math.cos(y),math.sin(y);cz,sz=math.cos(z),math.sin(z)
 q=[sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz]
 i=len(doc['nodes']);doc['nodes'].append(dict(name=name,translation=list(t),rotation=q,scale=list(s),children=[]))
 if parent is not None:doc['nodes'][parent]['children'].append(i)
 return i
root=node('COMMON');logoNode=node('name',root);world=node('world',root,t=(0,-7.775,0),r=(0,math.radians(45),0),s=(7.5,7.5,7.5));body=node('body_bob',world,t=(0,0,-.12));mr=node('mario_root',body,t=(0,0,-.081))
joints=[node(f'mario_{j:02}',mr,t=poses[0,j,:,3],r=euler[0,j],s=scale[0,j]) for j in range(npal)]
mn=node('mario_mesh',mr);doc['skins'].append({'joints':joints});doc['nodes'][mn]['skin']=0
wheel_positions=[[w['position'][0]/100,.21 if i<2 else .27,w['position'][2]/100] for i,w in enumerate(recipe['wheels'])]
wheel_scales=[1.,1.,1.29,1.29]
wheels=[node(w['name'],body,t=t,s=(sc,sc,sc)) for w,t,sc in zip(recipe['wheels'],wheel_positions,wheel_scales)]
textures={};specs={}
for folder,d in datasets.items():
 for tex in d['textures']:
  name=tex['name']
  if name.startswith('mario_eye.') and name!='mario_eye.3':continue
  if name in ('lm_0','lm_1'):im=Image.open(W/'baked'/(name+'.png')).convert('RGBA')
  else:im=Image.open(W/'exports'/folder/(tex['file']+'.png')).convert('RGBA')
  # Actual RGBA4 decode, also used by the production preview evaluator.
  a=(np.array(im)//16)*17;im=Image.fromarray(a);fn=name.replace('.','_')+'.png';im.save(O/fn);textures[name]=fn
  if name in g['light_maps']:g['light_maps'][name]=a/255.
  g['IMAGE_CACHE'][(folder,name)]=a/255.
# Wide logo with transparent power-of-two padding, preserved aspect.
im=Image.open(ROOT/'tools/release/artwork/logo.png').convert('RGBA');im=im.crop(im.getbbox());ratio=im.width/im.height
im=im.resize((256,round(256/ratio)),Image.Resampling.LANCZOS);canvas=Image.new('RGBA',(256,128));canvas.paste(im,(0,(128-im.height)//2));canvas.save(O/'logo_unbled.png')
import subprocess, sys
subprocess.run([sys.executable,str(SCRIPTS/'prepare_logo_texture.py'),str(O/'logo_unbled.png'),str(O/'logo.png')],check=True)
im=Image.open(O/'logo.png').convert('RGBA');Image.fromarray((np.array(im)//16)*17).save(O/'logo.png')
def material(name,source=None):
 if name in specs:return list(specs).index(name)
 if source:
  sam=source['samplers'];tn=sam[0]['name'];tn='mario_eye.3' if tn.startswith('mario_eye.') else tn
  wr=[33071,10497,33648];sampler=dict(magFilter=9729,minFilter=9729,wrapS=wr[sam[0]['wrapS']],wrapT=wr[sam[0]['wrapT']]);fn=textures[tn]
 else:sampler=dict(magFilter=9729,minFilter=9729,wrapS=33071,wrapT=33071);fn='logo.png'
 imageId=len(doc['images']);doc['images'].append({'uri':fn,'name':fn});si=len(doc['samplers']);doc['samplers'].append(sampler);ti=len(doc['textures']);doc['textures'].append(dict(source=imageId,sampler=si));mi=len(doc['materials']);mat=dict(name=name,pbrMetallicRoughness=dict(baseColorTexture={'index':ti},metallicFactor=0,roughnessFactor=1),doubleSided=False)
 if source is None:mat.update(alphaMode='BLEND')
 doc['materials'].append(mat);specs[name]=source;return mi
counts={}
def mesh(ni,folder,modelName,animated=False,translate=(0,0,0)):
 d=datasets[folder];model=next(m for m in d['models'] if m['name']==modelName);pr=[];cursor=0
 for sh in model['shapes']:
  vs=np.array(sh['vertices']);n=len(vs);mat=sh['sourceMaterial'];mi=material(sh['material'],mat)
  ns=g['normals'][12,cursor:cursor+n] if animated else vs[:,3:6]
  ras=g['raster'](mat,vs[:,10:14]/255.,ns);ras=np.round(ras*255).astype(np.uint8)
  env=g['unit'](ns@g['view'].T);env=np.column_stack((.5+.5*env[:,0],.5-.5*env[:,1]));mt=mat['texSrts'][1]['srtMtx'];get=lambda i:mt[str(i)]
  env=np.column_stack((env[:,0]*get(0)+env[:,1]*get(4)+get(12),env[:,0]*get(1)+env[:,1]*get(5)+get(13)));# glTF/pycgfx consumes the GX top-left convention
  if animated:
   # Both lm texture generators must share the baked coordinate.
   assert mat['texSrts'][1]['srtMtx']==mat['texSrts'][2]['srtMtx']
   p=local['p'][cursor:cursor+n]/100;normal=local['n'][cursor:cursor+n];js=local['j'][cursor:cursor+n]
  else:p=vs[:,:3]/100+translate;normal=vs[:,3:6];js=np.zeros(n,dtype=np.uint8)
  rows=np.column_stack((p,normal,vs[:,6:8],env,ras,js)).astype('<f4');unique,indices=np.unique(rows,axis=0,return_inverse=True);idx=indices.reshape(-1,3)[:,[0,2,1]].ravel()
  uv0=unique[:,6:8].copy();uv0[:,1]=1-uv0[:,1]
  attrs=dict(POSITION=acc(unique[:,:3],'VEC3'),NORMAL=acc(unique[:,3:6],'VEC3'),TEXCOORD_0=acc(uv0,'VEC2'),TEXCOORD_1=acc(unique[:,8:10],'VEC2'),COLOR_0=acc(unique[:,10:14],'VEC4',5121))
  if animated:
   ja=np.zeros((len(unique),4),dtype=np.uint8);ja[:,0]=unique[:,14];attrs['JOINTS_0']=acc(ja,'VEC4',5121);doc['accessors'][-1].pop('normalized',None);weights=np.zeros((len(unique),4));weights[:,0]=1;attrs['WEIGHTS_0']=acc(weights,'VEC4')
  if ni==body:
   from repair_banner_axle import axle_arrays
   aa=dict(POSITION=unique[:,:3],NORMAL=unique[:,3:6],TEXCOORD_0=uv0,TEXCOORD_1=unique[:,8:10],COLOR_0=unique[:,10:14].astype(np.uint8))
   aa,idx=axle_arrays(aa,idx)
   attrs={k:acc(v,{'POSITION':'VEC3','NORMAL':'VEC3','TEXCOORD_0':'VEC2','TEXCOORD_1':'VEC2','COLOR_0':'VEC4'}[k],5121 if k=='COLOR_0' else 5126) for k,v in aa.items()}
   n=len(idx)
  pr.append(dict(attributes=attrs,indices=acc(idx,'SCALAR',5123),material=mi));cursor+=n
 counts[doc['nodes'][ni]['name']]=cursor//3
 doc['nodes'][ni]['mesh']=len(doc['meshes']);doc['meshes'].append({'name':doc['nodes'][ni]['name'],'primitives':pr})
mesh(mn,'mario_1st','model',True);mesh(body,'standard_kart','body',translate=(0,.20,0))
for ni,w in zip(wheels,recipe['wheels']):mesh(ni,'standard_kart',w['sourceModel'])
# Same Y-axial billboard UV convention as the finished trilogy.
mi=material('banner_logo_alpha');width=28.64849008618765;height=width/2;center=6.5;depth=-12.
p=[[-width/2,center-height/2,depth],[width/2,center-height/2,depth],[width/2,center+height/2,depth],[-width/2,center+height/2,depth]]
attrs=dict(POSITION=acc(p,'VEC3'),NORMAL=acc([[0,0,-1]]*4,'VEC3'),TEXCOORD_0=acc([[0,0],[0,1],[1,1],[1,0]],'VEC2'))
# Match the proven trilogy's glTF UV convention (V-down).
attrs['TEXCOORD_0']=acc([[0,1],[1,1],[1,0],[0,0]],'VEC2')
doc['nodes'][logoNode]['mesh']=len(doc['meshes']);doc['meshes'].append({'name':'name','primitives':[dict(attributes=attrs,indices=acc([0,2,1,0,3,2,0,1,2,0,2,3],'SCALAR',5123),material=mi)]})
# Procedural soft contact shadow; no external game's banner assets required.
size=64
y,x=np.mgrid[-1:1:complex(size),-1:1:complex(size)]
rgba=np.zeros((size,size,4),dtype=np.uint8)
rgba[:,:,3]=(96*np.maximum(0,1-x*x-y*y)**2).astype(np.uint8)
Image.fromarray((rgba//16)*17).save(O/'contact_shadow.png')
si=len(doc['samplers']);doc['samplers'].append(dict(magFilter=9729,minFilter=9729,wrapS=33071,wrapT=33071))
ii=len(doc['images']);doc['images'].append(dict(uri='contact_shadow.png',name='contact_shadow.png'))
ti=len(doc['textures']);doc['textures'].append(dict(source=ii,sampler=si));mi=len(doc['materials'])
doc['materials'].append(dict(name='contact_shadow',alphaMode='BLEND',doubleSided=False,pbrMetallicRoughness=dict(baseColorTexture={'index':ti},metallicFactor=0,roughnessFactor=1)));specs['contact_shadow']=None
sn=node('contact_shadow',world);doc['nodes'][sn]['mesh']=len(doc['meshes'])
doc['meshes'].append(dict(name='contact_shadow',primitives=[dict(attributes=dict(POSITION=acc([[-.85,-.002,-1],[.85,-.002,-1],[.85,-.002,1],[-.85,-.002,1]],'VEC3'),NORMAL=acc([[0,1,0]]*4,'VEC3'),TEXCOORD_0=acc([[0,0],[1,0],[1,1],[0,1]],'VEC2')),indices=acc([0,2,1,0,3,2],'SCALAR',5123),material=mi)]));counts['contact_shadow']=2
# Curves are authored directly in the final converter to preserve XYZ unwrap.
meta=dict(headNodEdit="Below 10deg elevation retain 25percent pitch excursion; neck translation and arms unchanged",materials=specs,textures=textures,counts=counts,poses=poses.tolist(),euler=euler.tolist(),scale=scale.tolist(),matrixReconstructionError=error,frameCount=len(poses),wheelPositions=wheel_positions,wheelScale=1.0,wheelScales=wheel_scales,menuChassisCorrection=-.08,composition=dict(scale=7.5,y=-7.775,yaw=45,logoWidth=width,logoHeight=height,logoCenter=center,logoDepth=depth,subjectRearOffset=-.12))
from repair_banner_axle import AXLE_NOTE
meta['torsoLeanEdit']='Positive forward spine lean retained at 25percent; torso, right arm and head rotate about spine pivot; original left-arm palettes retained'
meta['axleEdit']=AXLE_NOTE
(O/'production_data.json').write_text(json.dumps(meta,separators=(',',':')))
doc['buffers']=[dict(uri='banner.bin',byteLength=len(buf))];(O/'banner.bin').write_bytes(buf);(O/'banner.gltf').write_text(json.dumps(doc,separators=(',',':')))
print('PREPARED',counts,'TRS reconstruction max error',error)
