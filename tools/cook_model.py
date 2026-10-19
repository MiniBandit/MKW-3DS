#!/usr/bin/env python3
"""Convert pinned BRRES parser output into bounded, partitioned Citro3D meshes."""
import argparse,hashlib,io,json,math,re,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'vendor/gclib'))
from gclib.texture_utils import decode_image
from gclib.gx_enums import ImageFormat,PaletteFormat
from PIL import Image
from gx_material import material_state,retail_lighting,GMDL_SUBMESH
from devkitpro import tool
from coplanar_layers import course_layers
from world_uv import local_triangle
from gx_samplers import shape_samplers,block as sampler_block
from world_sampler_policy import world_samplers
WATER_WORDS={'water','sea','river','fountain','lake'}
# Lower-case words of a source name, split at underscores, digits and case
# changes: 'TR_houseALL' -> tr, house, all; 'water0_HIG' -> water, hig.
def name_words(name):return {w.lower() for w in re.findall(r'[A-Z]+(?![a-z])|[A-Z]?[a-z]+',name)}

def cook(source,out,prefix,max_texture=256,preserve_vertices=False,reuse_textures=False,texture_prefix=None,encoded_textures=None,course_geometry=False,texture_overrides=None):
 # Models of one archive may share texture_prefix; encoded_textures names the
 # textures this run already wrote, so each is encoded once.
 data=json.loads((source/'model.json').read_text());(out/'textures').mkdir(parents=True,exist_ok=True);(out/'models').mkdir(parents=True,exist_ok=True)
 depth_layers=course_layers(data['models']) if course_geometry or (prefix.startswith('course_') and '_sky' not in prefix) else {}
 unknown=set(texture_overrides or {})-{t['name'] for t in data['textures']}
 if unknown:raise ValueError('Unknown replacement textures: '+str(sorted(unknown)))
 names=[];audits=[]
 for t in data['textures']:
  raw=(source/(t['file']+'.bin')).read_bytes();palpath=source/(t['file']+'.pal');pal=palpath.read_bytes() if palpath.exists() else b''
  replacement=(texture_overrides or {}).get(t['name'])
  if replacement is not None:
   with Image.open(replacement) as prepared:im=prepared.convert('RGBA')
  else:
   im=decode_image(io.BytesIO(raw),io.BytesIO(pal),ImageFormat(t['format']),PaletteFormat(t['paletteFormat'] or 0),len(pal)//2,t['width'],t['height']).convert('RGBA')
  im.thumbnail((max_texture,max_texture),Image.Resampling.LANCZOS);size=tuple(max(8,1<<(n-1).bit_length()) for n in im.size)
  if size!=im.size:im=im.resize(size,Image.Resampling.LANCZOS)
  name=f'{texture_prefix or prefix}_{len(names):03d}';names.append(name);png=out/'textures'/f'{name}.png'
  replacement_audit=({'preparedImage':str(replacement),'preparedSha256':hashlib.sha256(Path(replacement).read_bytes()).hexdigest()} if replacement is not None else {})
  if encoded_textures is not None and name in encoded_textures:
   audits.append({'source':t['name'],'name':name,'size':size,'alpha':im.getchannel('A').getextrema()[0]<255,**replacement_audit});continue
  if encoded_textures is not None:encoded_textures.add(name)
  if reuse_textures:
   if not png.is_file() or Image.open(png).convert('RGBA').tobytes()!=im.tobytes():raise ValueError('Reusable texture content changed: '+str(png))
  else:im.save(png)
  alpha=im.getchannel('A').getextrema()[0]<255;fmt='etc1a4' if alpha else 'etc1'
  if not reuse_textures:
   subprocess.run([str(tool('tex3ds')),'-f',fmt,'-q','high','-z','none','-o',str(png.with_suffix('.t3x')),str(png)],check=True,stdout=subprocess.DEVNULL)
  elif not png.with_suffix('.t3x').is_file():raise ValueError('Missing reusable texture: '+str(png))
  audits.append({'source':t['name'],'name':name,'size':size,'alpha':alpha,**replacement_audit})
 # Materials tools/export_brres.ts could not translate (its receipt's fallbacks).
 fallbacks={l.split(': ',1)[1] for l in data['limitations'] if l.startswith('Fallback material: ')}
 # Partition by unique vertices and preserve every triangle. Flush only at triangle boundaries.
 parts=[];vertices=[];indices=[];subs=[];lookup={};shapes=[];uv_tracks=[];sampler_tracks=[]
 water_fields=[]
 for model in data['models']:
  for shape in model['shapes']:
   if (shape.get('reflection') or {}).get('mode')!=11:continue
   field=shape.get('waterIndirect')
   if not field:raise ValueError('Projected water is missing its indirect field')
   payload=(source/field['file']).read_bytes()
   size=sum(max(1,field['width']>>i)*max(1,field['height']>>i)*3 for i in range(field['levels']))
   if len(payload)!=size:raise ValueError('Invalid water indirect mip payload')
   record=struct.pack('<IIIi6h11fI',field['width'],field['height'],field['frames'],field['shift'],*field['coefficients'],*field['uv'],*field['velocity'],field['lodBias'],field['minLOD'],field['maxLOD'],field['levels'])+payload
   if record not in water_fields:water_fields.append(record)
   shape['reflection']['constant']=water_fields.index(record)+1
 # GMDL v4 adds the retail course lighting word, ambient register and albedo add per submesh.
 version=5 if any(shape.get('reflection') for model in data['models'] for shape in model['shapes']) else 4 if any(shape.get('retail') for model in data['models'] for shape in model['shapes']) else 3
 def flush():
  nonlocal vertices,indices,subs,lookup,uv_tracks,sampler_tracks
  if not indices:return
  name=f'{prefix}_{len(parts):03d}';path=out/'models'/f'{name}.gmdl'
  with path.open('wb') as f:
   f.write(struct.pack('<4s5I',b'GMDL',version,len(vertices),len(indices),len(subs),len(names)))
   for s in subs:f.write(struct.pack(GMDL_SUBMESH[version],*s))
   for n in names:f.write(n.encode().ljust(64,b'\0'))
   for v in vertices:f.write(v)
   f.write(struct.pack('<'+'H'*len(indices),*indices))
   if version==5 and any(s[-9]==11 for s in subs):
    f.write(struct.pack('<4sI',b'WIN2',len(water_fields)))
    for field in water_fields:f.write(field)
   active=[(i,t) for i,t in enumerate(uv_tracks) if t]
   if active:
    f.write(struct.pack('<4sI',b'UVM1',len(active)))
    for i,(loop,samples) in active:
     f.write(struct.pack('<III',i,len(samples),int(loop)))
     for frame in samples:
      if len(frame)!=12 or not all(math.isfinite(x) for x in frame):raise ValueError('Invalid sampled UV matrix')
      f.write(struct.pack('<12f',*frame))
   if any(s[3] for row in sampler_tracks for s in row):f.write(sampler_block(sampler_tracks))
  parts.append({'name':name,'vertices':len(vertices),'triangles':len(indices)//3,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()});vertices=[];indices=[];subs=[];lookup={};uv_tracks=[];sampler_tracks=[]
 for model in data['models']:
  for shape in model['shapes']:
   shapes.append({k:v for k,v in shape.items() if k!='vertices'})
   flags,alpha_factor,vertex_alpha,alpha_limit,alpha_mix,extended=material_state(shape)
   fixed_menu_alpha=bool((shape.get('retail') or {}).get('word',0)&(1<<21) and (flags>>20)&7==4)
   # Bit 29 is an unused alpha-reference selector whenever alpha test is off.
   # Reuse it for a restrained aqua grade on water whose TEV equation has no
   # translation (the fallback samples its indirect wave mask as colour); the
   # source alpha equation and transparency remain intact. A translated material
   # keeps its own colour, and only whole words count: TR_houseALL is Wario's
   # Gold Mine's building atlas, a_bc_kankyakuseat_m a grandstand.
   words=name_words(shape.get('name',''))|name_words(shape.get('material',''))
   if not flags&1 and shape.get('material') in fallbacks and words&WATER_WORDS:
    flags |= 1<<29
   if alpha_limit:data['limitations'].append(alpha_limit+': '+shape['material'])
   stream=shape['vertices'];assert len(stream)%3==0
   samplers=world_samplers(prefix,shape,shape_samplers(shape))
   for start in range(0,len(stream),3):
    tri=[]
    points=stream[start:start+3]
    if prefix.startswith('course_') and '_sky' not in prefix and not shape.get('waterIndirect'):
     points=local_triangle(points,flags,shape['animationFrames']>0)
    for v in points:
     if not all(math.isfinite(x) for x in v):raise ValueError('non-finite vertex')
     rgba=v.copy();rgba[-1]=v[-1] if fixed_menu_alpha else round((v[-1] if vertex_alpha else 255)*alpha_factor)
     tri.append(struct.pack('<10f4B',*rgba))
    if len(vertices)+3>60000:flush()
    tex=shape['texture']
    if not shape.get('sourceMaterial') and tex>=0 and audits[tex]['alpha'] and not flags&2:flags|=1
    constant=shape['constant'] if alpha_mix is None else (shape['constant']&0xffffff)|(alpha_mix<<24)
    # A menu's final fixed GX alpha is independent of its lit raster alpha
    # (the latter is only the lm_0/lm_1 reflection mask).
    if fixed_menu_alpha:
     constant=(constant&0xffffff)|(round(alpha_factor*255)<<24)
    if shape['animationFrames']>0xffff:raise ValueError('Animation too long for GMDL: '+shape['material'])
    # With alpha testing disabled, bits 16..23 no longer hold a cutoff.
    # Value 1 selects the two-texture RGB equation T0 + T1*T0.alpha.
    if shape.get('textureAlphaAdd'):
     if flags&1:raise ValueError('Texture-alpha colour layer conflicts with alpha cutoff')
     extended |= 1<<16
    if shape.get('skyScreenBlend'):
     if flags&1 or shape.get('textureAlphaAdd') or shape['lightingMode']!=2:
      raise ValueError('Sky screen blend conflicts with another material equation')
     # P + cloud*(1-P), after the authored sky/raster stage.
     extended |= 2<<16
    if shape.get('skyRasterAdd'):
     if flags&1 or shape.get('textureAlphaAdd') or shape.get('skyScreenBlend'):
      raise ValueError('Sky raster add conflicts with another material equation')
     extended |= 3<<16
    material=[tex,flags,shape['secondary'],constant,*shape['uvVelocity'],shape['animationFrames']|extended]
    if version>=4:
     state=retail_lighting(shape)
     if depth_layers.get(id(shape)):
      state[0]|=1<<23
      shape['coplanarLayer']=True
     material+=state
    if version==5:
     reflection=shape.get('reflection')
     material+=([reflection['mode'],reflection['constant'],reflection['add'],*reflection['uv']] if reflection else [0]*9)
    uv_track=(shape.get('uvLoop',False),shape['uvSamples']) if shape.get('uvSamples') else None
    if not subs or subs[-1][2:]!=material or uv_tracks[-1]!=uv_track or sampler_tracks[-1]!=samplers or (preserve_vertices and start==0):
     subs.append([len(indices),0,*material]);uv_tracks.append(uv_track);sampler_tracks.append(samplers)
    for v in tri:
     if preserve_vertices:indices.append(len(vertices));vertices.append(v)
     else:
      if v not in lookup:lookup[v]=len(vertices);vertices.append(v)
      indices.append(lookup[v])
     subs[-1][1]+=1
 flush()
 (out/'models'/f'{prefix}.list').write_text(''.join(p['name']+'\n' for p in parts))
 receipt={'source':str(source),'parts':parts,'textures':audits,'materials':shapes,'lighting':data.get('lighting'),'limitations':data['limitations']}
 (out/'models'/f'{prefix}-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(json.dumps({'prefix':prefix,'parts':len(parts),'triangles':sum(p['triangles'] for p in parts),'geometry_bytes':sum(p['bytes'] for p in parts)}))
 return parts
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--prefix',required=True);p.add_argument('--max-texture',type=int,default=256);a=p.parse_args();cook(a.source,a.output,a.prefix,a.max_texture)
