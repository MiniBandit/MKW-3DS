"""Write the final HOME CGFX with rigid-per-vertex CHR0 and baked GX lighting."""
import sys,json,struct,math
from pathlib import Path
from PIL import Image
from banner_paths import ROOT, WORK as W, OUTPUT as O, SCRIPTS
from banner_paths import PYCGFX
sys.path.insert(0,str(PYCGFX))
import main as pc, gltflib
from cgfx.shared import StandardObject,Matrix,Vector4
from cgfx.sobj import BillboardMode,SkeletonFlag,SkeletonScalingRule
from cgfx.primitives import InterleavedVertexStream,VertexAttributeUsage as U
from cgfx.mtob import TexInfo,MTOBFlag,FragmentLightingFlags,ColorFloat,ConstantColorSource,TextureSampler
from cgfx.txob import ReferenceTexture
from cgfx import swizzler
from cgfx.canm import CANM,CANMBoneTransform,FloatAnimationCurve,FloatSegment,InterpolationType,QuantizationType,StepLinear64Key,RepeatMethod
# Standard CGFX sampler is 32 bytes: type,parent,minFilter,borderRGBA,LODBias.
# Upstream pycgfx omits LODBias and serializes only28bytes. Keep its legacy
# format elsewhere; explicitly complete the logo sampler before enabling minification.
class CompleteLogoSampler(TextureSampler):
 struct=struct.Struct('Iiifffff')
 def values(self):return (*super().values(),0.0)
meta=json.loads((O/'production_data.json').read_text());gltf=gltflib.GLTF.load(str(O/'banner.gltf'),load_file_resources=True);cg=pc.convert_gltf(gltf);m=cg.data.models['COMMON']
class MeshNode(StandardObject):
 struct=struct.Struct('iI')
 def __init__(self,name):self.name=name
 def values(self):return self.name,1
# Restore source ordering, removing only the synthetic skin mesh node.
old=list(m.skeleton.bones);order=['Scene root']+[n.name for n in gltf.model.nodes if n.name!='mario_mesh'];new={n:i for i,n in enumerate(order)}
bones={n:m.skeleton.bones[n] for n in old};synthetic=bones['mario_mesh']
for name,b in bones.items():
 if b.child is synthetic:b.child=synthetic.next_sibling
 if b.next_sibling is synthetic:b.next_sibling=synthetic.next_sibling
 if b.previous_sibling is synthetic:b.previous_sibling=synthetic.previous_sibling

def reorder(d,names):
 nodes={n.name:n for n in d.dict.nodes[1:]};d.dict.nodes=[d.dict.nodes[0]]+[nodes[n] for n in names];d.dict.regenerate()
reorder(m.skeleton.bones,order);group=m.animation_group_descriptions.dict['SkeletalAnimation'];reorder(group.members,order)
m.skeleton.flags=SkeletonFlag.IsTranslateAnimationEnabled;m.skeleton.scaling_rule=SkeletonScalingRule.Maya

def trs_matrix(b):
 x,y,z=b.rotation.values();cx,sx=math.cos(x),math.sin(x);cy,sy=math.cos(y),math.sin(y);cz,sz=math.cos(z),math.sin(z);sc=list(b.scale.values());t=b.position.values()
 rows=[[cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx],[sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx],[-sy,cy*sx,cy*cx]]
 return [[row[j]*sc[j] for j in range(3)]+[t[i]] for i,row in enumerate(rows)]
for name in order:
 b=bones[name];b.joint_id=new[name];b.parent_id=new[b.parent.name] if b.parent else -1;rows=trs_matrix(b);b.local=Matrix(*(Vector4(*r) for r in rows))
 # mode-1 skin vertices are already in bone-local space; retain the source
 # inverse bind for readers, while rigid skinning uses the current matrix.
 if name.startswith('mario_') and name[6:].isdigit():
  scale=list(b.scale.values());inv=[[rows[j][i]/(scale[i]**2) for j in range(3)] for i in range(3)];b.inverse_base=Matrix(*(Vector4(*r,-sum(r[j]*rows[j][3] for j in range(3))) for r in inv))
m.skeleton.bones['name'].billboard_mode=BillboardMode.YAxial
owners=[];vertex_count=0
for mesh in m.meshes.data.contents:
 shape=m.shapes.data.contents[mesh.shape_index];ps=shape.primitive_sets.data.contents[0];related=[old[i] for i in ps.related_bones.data.contents];skin=ps.skinning_mode==2;owner=related[0]
 if skin:
  assert owner=='mario_mesh';related=related[1:];ps.skinning_mode=1
 ps.related_bones.data.contents=[new[n] for n in related];mesh.mesh_node_name=owner;owners.append(owner)
 if owner.startswith('tire_') or owner=='body_bob':
  # Original rim is a one-sided disc. Reuse its vertices for explicit back
  # faces, because HOME ignores the high-level material cull enum.
  for primitive in ps.primitives.data.contents:
   for idx in primitive.index_streams.data.contents:
    assert int(idx.data_type)==5123
    values=struct.unpack('<'+'H'*(len(idx.face_data)//2),idx.face_data)
    back=[v for i in range(0,len(values),3) for v in (values[i],values[i+2],values[i+1])]
    idx.face_data+=struct.pack('<'+'H'*len(back),*back)
 attrs=[]
 for a in shape.vertex_attributes.data.contents:
  if a.usage==U.BoneWeight:continue
  if a.usage==U.BoneIndex:
   raw=a.vertex_stream_data;assert a.components_count==4 and all(v==1 for i,v in enumerate(raw) if i%4);a.vertex_stream_data=bytes(v-1 for v in raw[::4]);a.components_count=1
  if a.usage==U.Color:a.scale=1/255
  attrs.append(a)
 # Interleaved stream with real offsets, matching the proven mode-1 layout.
 sizes=[a.components_count*({5121:1,5126:4}[int(a.format_type)]) for a in attrs];counts=[len(a.vertex_stream_data)//size for a,size in zip(attrs,sizes)];assert len(set(counts))==1
 stride=(sum(sizes)+3)//4*4;data=bytearray(counts[0]*stride);vertex_count+=counts[0]
 offset=0
 for a,size in zip(attrs,sizes):
  a.vert_offset=offset
  for v in range(counts[0]):data[v*stride+offset:v*stride+offset+size]=a.vertex_stream_data[v*size:(v+1)*size]
  a.vertex_stream_data=b'';offset+=size
 stream=InterleavedVertexStream();stream.usage=U.Interlave;stream.vertex_stream_data=bytes(data);stream.vertex_data_entry_size=stride;stream.vertex_streams.data.contents=attrs;shape.vertex_attributes.data.contents=[stream]
 if skin:ps.primitives.data.contents[0].flags=8
for n in dict.fromkeys(owners):m.mesh_nodes.add(n,MeshNode(n))
for mesh in m.meshes.data.contents:mesh.mesh_node_visibility_index=m.mesh_nodes.get_index(mesh.mesh_node_name)
# PICA TEV translations of these source materials. All light and environment
# coordinates are baked; HOME lighting is disabled to prevent double shading.
for name in m.materials:
 mat=m.materials[name];source=meta['materials'][name];mat.flags=MTOBFlag(0);fs=mat.fragment_shader;fs.fragment_lighting.flags=FragmentLightingFlags(0);fs.fragment_lighting_table.distribution_0_sampler=None
 mat.material_color.constant[0]=ColorFloat(1,1,1,1);mat.material_color.constant[1]=ColorFloat(1,1,1,1)
 for stage in fs.texture_combiners:stage.src_rgb=stage.src_alpha=0xFFF;stage.combine_rgb=stage.combine_alpha=0;stage.tev_ops=0
 s0,s1,s2=fs.texture_combiners[:3]
 if source is None:
  s0.src_rgb=s0.src_alpha=0x333
  if name=='banner_logo_alpha':
   mapper=mat.texture_mappers[0]
   mapper.commands[2].head=(mapper.commands[2].head&~7)|6
   sampler=CompleteLogoSampler(mapper);sampler.min_filter=1;mapper.sampler=sampler
  continue
 for unit,sampler in enumerate(source['samplers'][1:],1):
  texname=meta['textures'][sampler['name']]
  if texname not in cg.data.textures:
   tx=swizzler.to_txob(Image.open(O/texname).convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM));tx.name=texname;cg.data.textures.add(texname,tx)
  tx=cg.data.textures[texname];info=TexInfo(ReferenceTexture(tx));info.commands[0].head+=8*unit;info.commands[1].head+=8*unit+8;info.commands[2].head|=3|[0,2,3][sampler['wrapS']]<<12|[0,2,3][sampler['wrapT']]<<8
  mat.texture_mappers[unit]=info;mat.texture_coordinators[unit].source_coordinate=1
 mat.used_texture_coordinates_count=len(source['samplers'])
 if name.startswith('mario'):
  # lm0 * raster.a + raster.rgb ; albedo * previous ; lm1 * mask + previous
  s0.src_rgb=0x004;s0.tev_ops=0x20;s0.combine_rgb=8
  s1.src_rgb=0x0F3;s1.combine_rgb=1;s1.src_alpha=0x003;s1.combine_alpha=1
  s2.src_rgb=0xFF5;s2.tev_ops=0x20;s2.combine_rgb=8;s2.src_alpha=0xEEE;s2.constant=ConstantColorSource.Constant1
 else:
  s0.src_rgb=0x003;s0.combine_rgb=1;s0.src_alpha=0x0E3;s0.combine_alpha=1
  ka=source['colorConstants'][2]['a'] if name=='mat_body' else 1.;mat.material_color.constant[0]=ColorFloat(1,1,1,ka)
  s1.src_rgb=0x0F4 if name=='mat_tire' else 0xFF4;s1.combine_rgb=2 if name=='mat_tire' else 8;s1.tev_ops=0 if name=='mat_tire' else 0x20
  # PICA ADD is 2 (GX ADD enums differ).
  s1.src_alpha=0xEEE;s1.constant=ConstantColorSource.Constant1
# Preserve vehicle periods while the 49-tick character track repeats independently.
character_period=meta['frameCount'];total_ticks=math.lcm(360,character_period)
anim=CANM();anim.name='COMMON';anim.target_animation_group_name='SkeletalAnimation';anim.frame_size=float(total_ticks);cg.data.skeletal_animations.add('COMMON',anim)
def curve(keys,period=360):
 if max(v for _,v in keys)-min(v for _,v in keys)<5e-6:return float(keys[0][1])
 c=FloatAnimationCurve();c.start_frame=0.;c.end_frame=float(period);c.pre_repeat_method=c.post_repeat_method=RepeatMethod.Repeat;s=FloatSegment();s.start_frame=0.;s.end_frame=float(period);s.interpolation=InterpolationType.Linear;s.quantization=QuantizationType.StepLinear64;s.keys=[StepLinear64Key(float(t),float(v)) for t,v in keys];c.segments=[s];return c
for name in order:
 if not (name in ('body_bob','mario_root') or name.startswith('tire_') or (name.startswith('mario_') and name[6:].isdigit())):continue
 b=bones[name];a=CANMBoneTransform();a.bone_path=name
 for pref,vals in [('scale',b.scale.values()),('rot',b.rotation.values()),('pos',b.position.values())]:
  for axis,v in zip('xyz',vals):setattr(a,pref+'_'+axis,float(v))
 if name=='body_bob':a.pos_y=curve([(i*10,.008*math.sin(i*math.pi/3)) for i in range(37)])
 elif name.startswith('tire_'):a.rot_x=curve([(0,-12*math.pi),(360,12*math.pi)])
 elif name!='mario_root':
  j=int(name[6:]);nf=meta['frameCount']
  for pref,src in [('scale',meta['scale']),('rot',meta['euler']),('pos',[[[row[3] for row in p] for p in frame] for frame in meta['poses']])]:
   for axis,k in zip('xyz',range(3)):
    keys=[(f,src[f][j][k]) for f in range(nf)]+[(nf,src[0][j][k])];setattr(a,pref+'_'+axis,curve(keys,nf))
 anim.member_animations_data.add(name,a)
payload=pc.write(cg);assert len(payload)<512*1024,(len(payload),'CGFX size exceeds HOME budget');(O/'banner.cgfx').write_bytes(payload)
receipt=dict(bytes=len(payload),limit=512*1024,vertices=vertex_count,triangles=sum(meta['counts'].values())+4+sum(v for k,v in meta['counts'].items() if k.startswith('tire_') or k=='body_bob'),bones=order,meshOwners=owners,animations=list(anim.member_animations_data),animationTicks=total_ticks,characterPeriodTicks=character_period,vehiclePeriodTicks=360,sourceFrameCount=meta['frameCount'],textureNames=list(cg.data.textures),sourceTRSError=meta['matrixReconstructionError'],materials={name:dict(offset=m.materials[name].offset,stages=[dict(rgb=s.src_rgb,alpha=s.src_alpha,op=s.tev_ops,rgbCombine=s.combine_rgb,alphaCombine=s.combine_alpha) for s in m.materials[name].fragment_shader.texture_combiners]) for name in m.materials},animationOffsets={n:anim.member_animations_data[n].offset for n in anim.member_animations_data},boneOffsets={n:bones[n].offset for n in order})
mapper=m.materials['banner_logo_alpha'].texture_mappers[0]
receipt['logoSampler']=dict(offset=mapper.sampler.offset,mapperOffset=mapper.offset,bytes=mapper.sampler.size(),param=mapper.commands[2].head,minFilter=mapper.sampler.min_filter,lodBias=0,textureFormat='RGBA4 single texture')
receipt['shapes']=[dict(owner=mesh.mesh_node_name,streamOffset=m.shapes.data.contents[mesh.shape_index].vertex_attributes.data.contents[0].offset,primitiveOffset=m.shapes.data.contents[mesh.shape_index].primitive_sets.data.contents[0].offset,indexOffset=m.shapes.data.contents[mesh.shape_index].primitive_sets.data.contents[0].primitives.data.contents[0].index_streams.data.contents[0].offset) for mesh in m.meshes.data.contents]
from cgfx.mtob import FragmentLighting
for name in m.materials:
 receipt['materials'][name]['stageOffset']=m.materials[name].fragment_shader.offset+16+FragmentLighting.struct.size+4
 receipt['materials'][name]['constants']=[list(c.values()) for c in m.materials[name].material_color.constant]
(O/'CGFX_BUILD.json').write_text(json.dumps(receipt,indent=2));print('CGFX_OK',len(payload),vertex_count,'vertices',len(order),'bones',len(receipt['animations']),'animated')
