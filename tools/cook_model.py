#!/usr/bin/env python3
"""Convert pinned BRRES parser output into bounded, partitioned Citro3D meshes."""
import argparse,hashlib,io,json,math,re,struct,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'vendor/gclib'))
from gclib.texture_utils import decode_image,BLOCK_WIDTHS,BLOCK_HEIGHTS,BLOCK_DATA_SIZES
from gclib.gx_enums import ImageFormat,PaletteFormat
from PIL import Image
from gx_material import material_state,retail_lighting,GMDL_SUBMESH
from devkitpro import tool
WATER_WORDS={'water','sea','river','fountain','lake'}
# Lower-case words of a source name, split at underscores, digits and case
# changes: 'TR_houseALL' -> tr, house, all; 'water0_HIG' -> water, hig.
def name_words(name):return {w.lower() for w in re.findall(r'[A-Z]+(?![a-z])|[A-Z]?[a-z]+',name)}

# Texel budgets of the course, sky and course-object cooks (texel_budget).
# A texture keeps its (power-of-two) source size while width*height fits; above
# that its longer axis halves, never below 256, and no axis exceeds 1024.
# Sky panoramas, baked shadow maps (the secondary texture of lighting mode 1)
# and road textures get the large budget.
COURSE_TEXELS=256*256
LARGE_TEXELS=512*512
def budget_size(width,height,texels):
 size=[min(1024,max(8,1<<(n-1).bit_length())) for n in (width,height)]
 while size[0]*size[1]>texels and max(size)>256:size[0 if size[0]>=size[1] else 1]//=2
 return tuple(size)

def tex0_levels(archives):
 """Authored mip level count of every TEX0 in the BRRES archives, by name
 (first archive first, as tools/export_brres.ts resolves texture names);
 noclip's count: min(levels, maxLOD + 1)."""
 levels={}
 for path in archives:
  data=Path(path).read_bytes()
  def dictionary(offset):
   entries=[]
   for i in range(1,struct.unpack_from('>I',data,offset+4)[0]+1):
    name,target=struct.unpack_from('>II',data,offset+16+16*i);entries.append((data[offset+name:data.index(b'\0',offset+name)].decode('latin1'),offset+target))
   return entries
  for group,at in dictionary(struct.unpack_from('>H',data,12)[0]+8):
   if group=='Textures(NW4R)':
    for name,tex in dictionary(at):
     levels.setdefault(name,max(1,min(struct.unpack_from('>I',data,tex+0x24)[0],math.ceil(struct.unpack_from('>f',data,tex+0x2c)[0]+1))))
 return levels

def mip_chain(t,raw,pal,base,authored):
 """Mip levels 1..n of a cooked base image: down to the Wii's smallest
 authored level, and no PICA level below 8 px. A level the same size as an
 authored GX level is that level's texels; others are box-filtered from the
 level above."""
 fmt=ImageFormat(t['format']);w,h=base.size
 smallest=(max(1,t['width']>>(authored-1)),max(1,t['height']>>(authored-1)))
 offsets=[0]
 for i in range(authored-1):
  lw,lh=max(1,t['width']>>i),max(1,t['height']>>i)
  offsets.append(offsets[-1]+-(-lw//BLOCK_WIDTHS[fmt])*-(-lh//BLOCK_HEIGHTS[fmt])*BLOCK_DATA_SIZES[fmt])
 levels=[]
 while min(w,h)>8 and w//2>=smallest[0] and h//2>=smallest[1]:
  w//=2;h//=2
  level=next((i for i in range(1,authored) if (t['width']>>i,t['height']>>i)==(w,h)),None)
  if level is not None:image=decode_image(io.BytesIO(raw[offsets[level]:]),io.BytesIO(pal),fmt,PaletteFormat(t['paletteFormat'] or 0),len(pal)//2,w,h).convert('RGBA')
  else:image=(levels[-1] if levels else base).resize((w,h),Image.Resampling.BOX)
  levels.append(image)
 return levels

def encode_t3x(png,fmt,levels):
 """tex3ds the base PNG, then append each mip level's own tex3ds payload
 (the contiguous layout citro3d and tools/cook_water_mipmaps.py use)."""
 t3x=png.with_suffix('.t3x')
 subprocess.run([str(tool('tex3ds')),'-f',fmt,'-q','high','-z','none','-o',str(t3x),str(png)],check=True,stdout=subprocess.DEVNULL)
 if not levels:return
 data=t3x.read_bytes();at=5+12*struct.unpack_from('<H',data)[0];payloads=[data[at+4:]]
 if data[4] or data[at] or struct.unpack_from('<I',data,at)[0]>>8!=len(payloads[0]):raise ValueError('Unexpected tex3ds output: '+str(t3x))
 with tempfile.TemporaryDirectory() as work:
  for i,level in enumerate(levels,1):
   source=Path(work)/f'{i}.png';target=source.with_suffix('.bin');level.save(source)
   subprocess.run([str(tool('tex3ds')),'-f',fmt,'-q','high','-z','none','-r','-o',str(target),str(source)],check=True,stdout=subprocess.DEVNULL)
   # tex3ds -r keeps the 4-byte compression header (type 0: none, 24-bit size).
   raw=target.read_bytes();payloads.append(raw[4:])
   if raw[0] or struct.unpack_from('<I',raw)[0]>>8!=len(payloads[-1]) or len(payloads[-1])!=len(payloads[0])>>(2*i):raise ValueError('Mip level size mismatch: '+str(t3x))
 size=sum(map(len,payloads))
 t3x.write_bytes(data[:4]+bytes([len(levels)])+data[5:at]+struct.pack('<I',size<<8)+b''.join(payloads))

def t3x_bytes(path):
 """GPU bytes of a .t3x: its payload after the header."""
 with open(path,'rb') as f:count=struct.unpack('<H',f.read(2))[0]
 return Path(path).stat().st_size-(9+12*count)

# exact_formats: the PICA format that holds each GX source format's texels
# unchanged (GX I/IA sample as (I,I,I,A), as PICA L/LA do).
EXACT_FORMATS={ImageFormat.I4:'la4',ImageFormat.IA4:'la4',ImageFormat.I8:'la8',ImageFormat.IA8:'la8',ImageFormat.RGB565:'rgb565'}
def cook(source,out,prefix,max_texture=256,preserve_vertices=False,reuse_textures=False,texture_prefix=None,encoded_textures=None,texel_budget=None,brres=(),exact_formats=False):
 # Models of one archive may share texture_prefix; encoded_textures names the
 # textures this run already wrote, so each is encoded once.
 # exact_formats replaces ETC1/ETC1A4 with EXACT_FORMATS (RGBA8 for the other
 # GX formats): for small effect textures whose per-texel colours ETC1 loses.
 # texel_budget (COURSE_TEXELS or LARGE_TEXELS) replaces the max_texture cap
 # with budget_size, adds mip levels to textures whose source has authored GX
 # levels and that some GX sampler of the archive (materials.json) minifies
 # with mips, and writes GMDL v6 with each submesh's sampler state; brres are
 # the source archives (tex0_levels).
 data=json.loads((source/'model.json').read_text());(out/'textures').mkdir(parents=True,exist_ok=True);(out/'models').mkdir(parents=True,exist_ok=True)
 names=[];audits=[]
 if texel_budget:
  authored=tex0_levels(brres)
  mipmapped={s['name'] for m in json.loads((source/'materials.json').read_text()) for material in m['materials'] for s in material['samplers'] if s['minFilter']>=2}
  shadows={shape['secondary'] for model in data['models'] for shape in model['shapes'] if shape['lightingMode']==1 and shape['secondary']>=0}
 for index,t in enumerate(data['textures']):
  raw=(source/(t['file']+'.bin')).read_bytes();palpath=source/(t['file']+'.pal');pal=palpath.read_bytes() if palpath.exists() else b''
  im=decode_image(io.BytesIO(raw),io.BytesIO(pal),ImageFormat(t['format']),PaletteFormat(t['paletteFormat'] or 0),len(pal)//2,t['width'],t['height']).convert('RGBA')
  if texel_budget:
   large=index in shadows or 'road' in t['name'].lower() or 'michi' in t['name'].lower()
   size=budget_size(*im.size,max(texel_budget,LARGE_TEXELS) if large else texel_budget)
  else:
   im.thumbnail((max_texture,max_texture),Image.Resampling.LANCZOS);size=tuple(max(8,1<<(n-1).bit_length()) for n in im.size)
  if size!=im.size:im=im.resize(size,Image.Resampling.LANCZOS)
  name=f'{texture_prefix or prefix}_{len(names):03d}';names.append(name);png=out/'textures'/f'{name}.png'
  if encoded_textures is not None and name in encoded_textures:
   audits.append({'source':t['name'],'name':name,'size':size,'alpha':im.getchannel('A').getextrema()[0]<255});continue
  if encoded_textures is not None:encoded_textures.add(name)
  if reuse_textures:
   if not png.is_file() or Image.open(png).convert('RGBA').tobytes()!=im.tobytes():raise ValueError('Reusable texture content changed: '+str(png))
  else:im.save(png)
  alpha=im.getchannel('A').getextrema()[0]<255;fmt='etc1a4' if alpha else 'etc1'
  if exact_formats:fmt=EXACT_FORMATS.get(ImageFormat(t['format']),'rgba8')
  levels=mip_chain(t,raw,pal,im,authored[t['name']]) if texel_budget and t['name'] in mipmapped and authored.get(t['name'],1)>1 else []
  if not reuse_textures:encode_t3x(png,fmt,levels)
  elif not png.with_suffix('.t3x').is_file():raise ValueError('Missing reusable texture: '+str(png))
  audits.append({'source':t['name'],'name':name,'size':size,'alpha':alpha})
 if texel_budget:
  # Source size, mip levels and GPU bytes (the .t3x payload) of every texture
  # this model lists, also those an earlier model of the archive encoded;
  # tools/cook_course_overlay.py cooks meshes only (no .t3x in its output).
  for a,t in zip(audits,data['textures']):
   a['sourceSize']=[t['width'],t['height']];t3x=out/'textures'/f"{a['name']}.t3x"
   if t3x.is_file():a['mipLevels']=t3x.read_bytes()[4];a['bytes']=t3x_bytes(t3x)
 # Materials tools/export_brres.ts could not translate (its receipt's fallbacks).
 fallbacks={l.split(': ',1)[1] for l in data['limitations'] if l.startswith('Fallback material: ')}
 # Partition by unique vertices and preserve every triangle. Flush only at triangle boundaries.
 parts=[];vertices=[];indices=[];subs=[];lookup={};shapes=[]
 # GMDL v4 adds the retail course lighting word, ambient register and albedo add per submesh.
 version=5 if any(shape.get('reflection') for model in data['models'] for shape in model['shapes']) else 4 if any(shape.get('retail') for model in data['models'] for shape in model['shapes']) else 3
 # GMDL v6 (texel_budget cooks) appends the GX sampler state of texture units 0
 # and 1: per unit 16 bits, the GX minFilter + 1 (0: unknown) and the lodBias in
 # signed 1/32 steps (the GX register's precision) at bit 8. Unit 0's bit 3: a
 # GX projection-mapped texgen (tools/export_brres.ts projectedGlow), sampled in
 # PICA projection mode through the texture SRT stored in the reflection uv.
 if texel_budget:version=6
 if version<6 and any(shape.get('projectedGlow') for model in data['models'] for shape in model['shapes']):
  raise ValueError('A projected texgen needs GMDL v6 (texel_budget)')
 def sampler(texture,map_index,samplers):
  if texture<0:return 0
  name=data['textures'][texture]['name']
  s=samplers[map_index] if 0<=map_index<len(samplers) and samplers[map_index]['name']==name else next((s for s in samplers if s['name']==name),None)
  return (s['minFilter']+1)|(max(-128,min(127,round(s['lodBias']*32)))&255)<<8 if s else 0
 def flush():
  nonlocal vertices,indices,subs,lookup
  if not indices:return
  name=f'{prefix}_{len(parts):03d}';path=out/'models'/f'{name}.gmdl'
  with path.open('wb') as f:
   f.write(struct.pack('<4s5I',b'GMDL',version,len(vertices),len(indices),len(subs),len(names)))
   for s in subs:f.write(struct.pack(GMDL_SUBMESH[version],*s))
   for n in names:f.write(n.encode().ljust(64,b'\0'))
   for v in vertices:f.write(v)
   f.write(struct.pack('<'+'H'*len(indices),*indices))
  parts.append({'name':name,'vertices':len(vertices),'triangles':len(indices)//3,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()});vertices=[];indices=[];subs=[];lookup={}
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
   for start in range(0,len(stream),3):
    tri=[]
    for v in stream[start:start+3]:
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
    glow=shape.get('projectedGlow')
    if glow:
     if flags&1 or shape.get('textureAlphaAdd') or shape.get('skyScreenBlend'):
      raise ValueError('Projected glow conflicts with another material equation')
     # GX sea: P*(1+T1) + Z*(1-T1) after the stage-0 colour lerp P (Z: reflection constant).
     extended |= 3<<16
    material=[tex,flags,shape['secondary'],constant,*shape['uvVelocity'],shape['animationFrames']|extended]
    if version>=4:material+=retail_lighting(shape)
    if version>=5:
     reflection=shape.get('reflection')
     material+=([reflection['mode'],reflection['constant'],reflection['add'],*reflection['uv']] if reflection else [0,glow['constant'],0,*glow['uv']] if glow else [0]*9)
    if version==6:
     samplers=(shape.get('sourceMaterial') or {}).get('samplers',[])
     material.append(sampler(tex,shape['primaryMap'],samplers)|(8 if glow else 0)|sampler(shape['secondary'],shape['secondaryMap'],samplers)<<16)
    if not subs or subs[-1][2:]!=material or (preserve_vertices and start==0):subs.append([len(indices),0,*material])
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
 print(json.dumps({'prefix':prefix,'parts':len(parts),'triangles':sum(p['triangles'] for p in parts),'geometry_bytes':sum(p['bytes'] for p in parts),**({'texture_bytes':sum(a.get('bytes',0) for a in audits)} if texel_budget else {})}))
 return parts
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--prefix',required=True);p.add_argument('--max-texture',type=int,default=256);a=p.parse_args();cook(a.source,a.output,a.prefix,a.max_texture)
