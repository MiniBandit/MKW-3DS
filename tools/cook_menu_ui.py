#!/usr/bin/env python3
"""Bake original retail Wii UI resources for the top-screen native frontend.
Uses original TPL/BRFNT, BRLYT pane trees, BRCTR transforms and BRLAN curves.
No system fonts or replacement map silhouettes. See the emitted provenance.
"""
import json, struct, subprocess, re, io, hashlib, math, sys
from fractions import Fraction
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from cook_race_hud import sections,string,layout,animation,sample,font_strings,variant
from cook_race_items import cook_image_sprite,decode_tpl,digest
from cook_roster import ROOT
from menu_layout import render_layout,controllers,controller_alphas,controller_animations,messages,message_font_indices,pane_nodes
from menu_movies import extract_movies,ThpMovie
from menu_unlock_messages import entries as unlock_entries
OUT=ROOT/'sd-card/3ds/mkw-3ds'
UI=ROOT/'assets/menu-ui/MenuSingle'
records={}; evidence={}; panel_records={}; pane_records={}; node_records={}; movie_records=[]; glow_records=[]; focus_records=[]; panel_layer_records={}; border_records=[]; border_slices=[]; runtime_palette=[]
def source(p):
 key=str(p.relative_to(ROOT))
 if key not in evidence:evidence[key]=digest(p)
def save(name,img,p=None):
 native_width,native_height=img.size
 if max(img.size)>1024:
  scale=1024/max(img.size);img=img.resize((round(img.width*scale),round(img.height*scale)),Image.Resampling.LANCZOS)
 width,height=img.size; padded=[max(8,1<<(v-1).bit_length()) for v in img.size]
 target=OUT/'textures'/('ui_'+name+'.png')
 cached=False
 if target.exists() and target.with_suffix('.t3x').exists():
  with Image.open(target) as previous:
   cached=previous.size==tuple(padded) and previous.crop((0,0,width,height)).convert('RGBA').tobytes()==img.convert('RGBA').tobytes()
 r={'name':'ui_'+name,'path':'textures/ui_'+name+'.t3x','width':width,'height':height,'padded':padded,'u':width/padded[0],'v':height/padded[1]} if cached else cook_image_sprite(img,'ui_'+name,OUT)
 if p:source(p);r.update(source=str(p.relative_to(ROOT)),source_sha256=evidence[str(p.relative_to(ROOT))])
 if (native_width,native_height)!=(width,height):r.update(width=native_width,height=native_height,rasterWidth=width,rasterHeight=height)
 if name=='background':
  # One source row before/after content, filled periodically through POT padding.
  # Runtime adds 1/textureHeight to V. This preserves bilinear continuity.
  from menu_background_texture import write_wrapped_background
  write_wrapped_background(img,OUT)
 records[name]=r
 return r
def tpl(name,p):return save(name,decode_tpl(p),p)

def font_style(image,colors,vertices):
 # Native nw4r::ut::CharWriter::SetupGXWithColorMapping uses TEXC for RGB
 # interpolation and TEXA for alpha, followed by the text vertex colour.
 rgba=np.asarray(image,dtype=np.float32)/255
 lo,hi=np.asarray(colors,dtype=np.float32)/255
 mapped=lo+(hi-lo)*rgba
 top,bottom=np.asarray(vertices,dtype=np.float32)/255
 gradient=top+(bottom-top)*(np.arange(image.height,dtype=np.float32)[:,None,None]+.5)/image.height
 return Image.fromarray(np.uint8(np.clip(mapped*gradient*255,0,255)))

def constant_rectangle(changed):
 """Largest unchanged rectangle; its complement is four disjoint strips."""
 heights=np.zeros(changed.shape[1],dtype=int);best=(0,0,0,0);area=0
 for y,row in enumerate(changed):
  heights=np.where(row,0,heights+1);stack=[]
  for x,h in enumerate([*heights,0]):
   start=x
   while stack and stack[-1][1]>h:
    sx,sh=stack.pop();candidate=sh*(x-sx)
    if candidate>area:area=candidate;best=(sx,y-sh+1,x-sx,sh)
    start=sx
   if not stack or stack[-1][1]<h:stack.append((start,h))
 return tuple(int(v) for v in best)

def bake_border_loop(key,p,control,bindings,animations,animation_groups,render_args,rect,base):
 if not bindings or not animations:return
 loop=next((g for g in bindings['groups'] if g['name']=='Loop' and g['panes']=='fuchi_pattern'),None)
 if not loop:return
 indices=[i for i,g in enumerate(animation_groups or []) if g==loop['panes']]
 if len(indices)!=1:return
 index=indices[0];duration,tracks=animation(animations[index])
 uv=tracks.get(('fuchi_pattern','RLTS',0))
 if not uv or len(uv)!=2:return
 span=uv[-1][0]-uv[0][0];rate=(uv[-1][1]-uv[0][1])/span
 if not rate or any(abs(k[2]-rate)>1e-6 for k in uv):return
 period=Fraction(abs(rate)).limit_denominator(512).denominator
 if period>256:raise ValueError('Unexpected menu border loop period: '+key)
 images=[base]
 for frame in range(1,period):
  times=[0]*len(animations);times[index]=frame
  image,_,_=render_layout(p,source,animations,bounds=rect,animation_frames=times,**render_args)
  images.append(image)
 first=np.asarray(base);changed=np.zeros(first.shape[:2],dtype=bool)
 for image in images[1:]:changed|=np.any(np.asarray(image)!=first,axis=2)
 if not np.any(changed):return
 cx,cy,cw,ch=constant_rectangle(changed);width,height=base.size
 boxes=[(0,0,width,cy),(0,cy+ch,width,height),(0,cy,cx,cy+ch),(cx+cw,cy,width,cy+ch)]
 boxes=[box for box in boxes if box[2]>box[0] and box[3]>box[1]]
 crops=[]
 for frame,image in enumerate(images):
  for box in boxes:
   crop=image.crop(box);bbox=crop.getbbox()
   if not bbox:continue
   l,t,r,b=box[0]+bbox[0],box[1]+bbox[1],box[0]+bbox[2],box[1]+bbox[3]
   parts=max(1,math.ceil((r-l)/254));step=math.ceil((r-l)/parts)
   for left in range(l,r,step):
    small=(left,t,min(r,left+step),b)
    crops.append(dict(frame=frame,box=small,image=image.crop(small)))
 # Pack padded thin strips, rather than allocating a full button per frame.
 # A neighboring source texel protects bilinear samples at atlas boundaries.
 atlases=[];atlas=[];x=y=row_h=used_w=0
 for crop in sorted(crops,key=lambda c:(-c['image'].height,-c['image'].width)):
  w,h=crop['image'].width+2,crop['image'].height+2
  if w>1024 or h>1024:raise ValueError('Border strip exceeds native texture size')
  if x+w>1024:x=0;y+=row_h;row_h=0
  if y+h>1024:
   atlases.append((atlas,used_w,y+row_h));atlas=[];x=y=row_h=used_w=0
  crop['atlasXY']=(x+1,y+1);atlas.append(crop);x+=w;used_w=max(used_w,x);row_h=max(row_h,h)
 if atlas:atlases.append((atlas,used_w,y+row_h))
 first_slice=len(border_slices)
 for atlas_id,(entries,used_w,used_h) in enumerate(atlases):
  w=max(8,1<<(used_w-1).bit_length());h=max(8,1<<(used_h-1).bit_length());image=Image.new('RGBA',(w,h))
  atlas_key='border_'+key+'_'+str(atlas_id)
  for entry in entries:
   px,py=entry['atlasXY'];crop=entry['image'];l,t,r,b=entry['box']
   # Gutters contain the neighboring source pixels, including the static
   # center, so bilinear interpolation remains continuous across each split.
   image.paste(images[entry['frame']].crop((l-1,t-1,r+1,b+1)),(px-1,py-1))
   border_slices.append(dict(frame=entry['frame'],key=atlas_key,rect=[rect[0]+l,rect[1]+t,r-l,b-t],uv=[px/w,py/h,(px+crop.width)/w,(py+crop.height)/h]))
  save(atlas_key,image,p)
 border_records.append(dict(key=key,control=control.stem,frames=period,duration=duration,rate=rate,center=[rect[0]+cx,rect[1]+cy,cw,ch],firstSlice=first_slice,sliceCount=len(border_slices)-first_slice,source=str(animations[index].relative_to(ROOT)),frameHashes=[hashlib.sha256(image.tobytes()).hexdigest() for image in images]))

def bake_panel(name,resource,states=('free','select'),exclude=(),bounds=None,overrides=None,hidden=(),group_states=None):
 p=ROOT/'assets/menu-ui'/resource
 node_records[name]=pane_nodes(p)
 bindings=None;control=None
 for control in sorted((p.parent.parent/'ctrl').glob('*.brctr')):
  if control.name.startswith('._'):continue
  candidate=controller_animations(control)
  if candidate['layout']==p.stem:source(control);bindings=candidate;break
 # These picture panes have their own Select-group alpha/scale, independent
 # of the button's animated parent. A flattened frame either freezes their
 # free-state shade or bakes a transparent selected-state glow forever.
 dynamic_panes=set()
 if bindings:
  selection=next((g for g in bindings['groups'] if g['name']=='Select'),None)
  if selection:
   for entry in selection['animations']:
    _,tracks=animation(p.parent.parent/'anim'/entry['file'])
    dynamic_panes.update(node for node,kind,target in tracks
                         if node in ('color_down','pikapika','waku_light') and
                         (kind=='RLPA' or kind=='RLVI' or (kind=='RLVC' and target==16)))
 for state in states:
  ap=p.parent.parent/'anim'/f'{p.stem}_{state}.brlan'
  animations=[ap] if ap.exists() else []
  animation_groups=None
  if bindings and (state or group_states):
   animations=[];animation_groups=[]
   for group in bindings['groups']:
    # PushButton initializes these groups with their native stop clips. The
    # BRLYT often retains a visible author-preview glow (notably waku_light).
    wanted={'Select':state.title(),'Loop':'Loop','SelectIn':'SelectStop','OK':'OKStop','Active':'Active'}.get(group['name'])
    if group_states and group['name'] in group_states:wanted=group_states[group['name']]
    for entry in group['animations']:
     if entry['name']==wanted:
      animations.append(p.parent.parent/'anim'/entry['file']);animation_groups.append(group['panes'])
  # ControlManipulator switches to table entry five when no player points at
  # this button; entry one is the first player's highlighted colour scheme.
  c0,c1,base=runtime_palette[5 if state=='free' else 1]
  colors={'fuchi_pattern':[c0,c1,[255,255,255,255]],'color_base':[base,base,[255,255,255,255]]} if states!=('',) else {}
  pane_overrides=dict(overrides or {})
  for hidden_pane in hidden:pane_overrides[hidden_pane]={'visible':False}
  if name=='character':
   # ButtonDriver::{Reset,Set}ButtonColours modifies its black_base pane in
   # addition to the shared PushButton border palette (RMCU 807dde6c/807de0a0).
   if state=='free':
    color=[255,255,255,255];vertex=[20,20,20,255]
   else:
    color=[200,200,200,240]
    vertex=[int((runtime_palette[1][0][i]+runtime_palette[1][1][i])*.5) for i in range(3)]+[96]
   colors['black_base']=[color,color,[255,255,255,255]]
   pane_overrides['black_base']={'alpha':128/255,'vertexColors':[vertex]*4}
  movie_panes=('black_base','black_base_00','black_base_01','black_base_02') if name=='mode' else ('black_base',) if name in ('main_button','main_multi','main_wifi','class','drift','vs','course','battle') else ()
  separate=tuple(sorted(dynamic_panes))
  img,rect,panes=render_layout(p,source,animations,exclude=exclude,bounds=bounds,pane_overrides=pane_overrides,material_overrides=colors,animation_groups=animation_groups,exclude_exact=movie_panes+separate)
  # Split at independently animated pictures, in BRLYT submission order.
  # Excluded pictures still write source depth in render_layout. Later static
  # chunks therefore need both depth-film-present and film-absent variants.
  pictures=[n['name'] for n in node_records[name] if panes.get(n['name'],{}).get('kind') in ('pic1','wnd1')]
  ordered=[n for n in pictures if n in dynamic_panes]
  if len(ordered)>2:raise ValueError(f'More than two independently animated depth films: {name}: {ordered}')
  first_dynamic=pictures.index(ordered[0]) if ordered else len(pictures)
  before=tuple(pictures[:first_dynamic])
  if ordered:
   img,_,_=render_layout(p,source,animations,exclude=exclude,bounds=rect,pane_overrides=pane_overrides,material_overrides=colors,animation_groups=animation_groups,exclude_exact=movie_panes+separate,include=before)
  key=name+('_'+state if state else '')
  save(key,img,p);panel_records[key]=rect
  pane_records[key]=panes
  loop_key=key;loop_image=img;loop_z=None
  if movie_panes:
   movie_z=panes[movie_panes[0]]['z']
   for suffix,z_range in [('underlay',(-float('inf'),movie_z)),('overlay',(movie_z,float('inf')))]:
    layer,_,_=render_layout(p,source,animations,exclude=exclude,bounds=rect,pane_overrides=pane_overrides,material_overrides=colors,animation_groups=animation_groups,exclude_exact=movie_panes+separate,z_range=z_range,include=before if ordered else None)
    save(key+'_'+suffix,layer,p);panel_records[key+'_'+suffix]=rect
    if suffix=='underlay':loop_key=key+'_'+suffix;loop_image=layer;loop_z=z_range
  bake_border_loop(loop_key,p,control,bindings,animations,animation_groups,
                   dict(exclude=exclude,pane_overrides=pane_overrides,material_overrides=colors,animation_groups=animation_groups,exclude_exact=movie_panes+separate,z_range=loop_z,include=before if ordered else None),rect,loop_image)
  for at,member in enumerate(ordered):
   begin=pictures.index(member)+1;end=pictures.index(ordered[at+1]) if at+1<len(ordered) else len(pictures)
   after=tuple(n for n in pictures[begin:end] if n not in movie_panes and not any(x in n for x in exclude))
   if not after:continue
   for part,z_range in ([(1,(-float('inf'),movie_z)),(2,(movie_z,float('inf')))] if movie_panes else [(0,None)]):
    entry=panel_layer_records.setdefault((name,at,part),dict(panel=name,after=member,part=part,free=['']*4,select=['']*4))
    for mask in range(1<<(at+1)):
     depth_overrides=dict(pane_overrides)
     for bit,film in enumerate(ordered[:at+1]):depth_overrides[film]={**depth_overrides.get(film,{}),'alpha':float(bool(mask&(1<<bit))),'visible':True}
     tail,_,_=render_layout(p,source,animations,exclude=exclude,bounds=rect,pane_overrides=depth_overrides,material_overrides=colors,animation_groups=animation_groups,exclude_exact=movie_panes+separate,z_range=z_range,include=after)
     box=tail.getbbox()
     if not box:continue
     tail_key=f'panel_tail_{name}_{state}_{at}_{part}_{mask}'
     save(tail_key,tail.crop(box),p);panel_records[tail_key]=[rect[0]+box[0],rect[1]+box[1],rect[0]+box[2],rect[1]+box[3]]
     entry[state][mask]=tail_key
  if state in ('select',''):
   pane_records[name]=panes
  if state=='select':
   nodes={n['name']:n for n in node_records[name]}
   for member in ordered:
    if member not in panes or panes[member]['kind']!='pic1' or any(x in member for x in exclude):continue
    # Keep the exact selected reference geometry, but bake full inherited
    # opacity; uiTextMotion restores the complete live alpha hierarchy.
    focus_overrides=dict(pane_overrides);ancestor=member;hidden_ancestor=False
    while ancestor:
     if focus_overrides.get(ancestor,{}).get('visible') is False:hidden_ancestor=True
     focus_overrides[ancestor]={**focus_overrides.get(ancestor,{}),'alpha':1,'visible':True}
     ancestor=nodes[ancestor]['parent']
    if hidden_ancestor:continue
    try:image,focus_rect,focus_panes=render_layout(p,source,animations,include=(member,),pane_overrides=focus_overrides,material_overrides=colors,animation_groups=animation_groups)
    except ValueError as error:
     if 'no visible picture panes' not in str(error):raise
     continue
    if not image.getbbox():continue
    blend=panes[member]['blend']
    if blend not in ([1,4,5,3],[1,4,1,3]):raise ValueError(f'Unsupported focus blend: {name}/{member}: {blend}')
    focus_key='focus_'+name+'_'+member;save(focus_key,image,p);panel_records[focus_key]=focus_rect
    part=(1 if panes[member]['z']<movie_z else 2) if movie_panes else 0
    focus_records.append(dict(panel=name,pane=member,key=focus_key,rect=focus_rect,pivot=focus_panes[member]['pivot'],part=part,blend=int(blend[2]==1),depthBit=1<<ordered.index(member)))

def bake_ok_glows(name,resource,drivers,cups,group_name='OK'):
 p=ROOT/'assets/menu-ui'/resource;binding=None
 for control in sorted((p.parent.parent/'ctrl').glob('*.brctr')):
  if control.name.startswith('._'):continue
  candidate=controller_animations(control)
  if candidate['layout']==p.stem:source(control);binding=candidate;break
 if not binding:return
 ok=next((g for g in binding['groups'] if g['name']==group_name),None)
 if not ok:return
 animations=[];animation_groups=[]
 for group in binding['groups']:
  wanted='Select' if group['name']=='Select' else 'Loop' if group['name']=='Loop' else None
  for entry in group['animations']:
   if entry['name']==wanted:animations.append(p.parent.parent/'anim'/entry['file']);animation_groups.append(group['panes'])
 data=p.read_bytes();members=[]
 for tag,_,s in sections(data):
  if tag==b'grp1' and string(data,s)==ok['panes']:
   members=[data[s+20+i*16:s+36+i*16].split(b'\0',1)[0].decode() for i in range(struct.unpack_from('>H',data,s+16)[0])]
 # The evaluator supplies each glow's own animated scale and alpha. Keep its
 # parent hierarchy in the selected pose and rasterize at unit own scale.
 overrides={member:{'sx':1,'sy':1,'alpha':1,'visible':True} for member in members}
 _,_,panes=render_layout(p,source,animations,bounds=(-500,-250,500,250),pane_overrides=overrides,animation_groups=animation_groups)
 suffix='_ok' if group_name=='OK' else '_selectin'
 pane_records[name+suffix]={member:panes[member] for member in members}
 for member in members:
  pane=panes[member]
  if pane['kind'] not in ('pic1','txt1'):continue
  blend=pane['blend']
  if blend not in ([1,4,5,3],[1,4,1,3]):raise ValueError(f'Unsupported OK glow blend: {name}/{member}: {blend}')
  entry=dict(panel=name,pane=member,key='',group=group_name,variant=0,blend=1 if blend[2]==1 else 0,pivot=pane['pivot'],sourceBlend=blend,materialColors=pane.get('materialColors',pane.get('textColors')))
  if pane['kind']=='pic1':
   replacements=[('',None)]
   if name=='character' and group_name=='OK':
    entry.update(key='driver_glow_',variant=1)
    replacements=[('driver_glow_'+str(i),UI/'button/timg'/f'tt_{driver}_64x64.tpl') for i,driver in enumerate(drivers)]
   elif name=='cup' and group_name=='OK':
    entry.update(key='cup_glow_',variant=2)
    replacements=[('cup_glow_'+str(i),UI/'button/timg'/f'tt_cup_icon_{cup}_00.tpl') for i,cup in enumerate(cups)]
    replacements += [('cup_glow_'+str(8+i),UI/'button/timg'/texture) for i,texture in enumerate(['tt_wii_icon_128.tpl','tt_classic_icon_sphere_128.tpl'])]
   elif name=='vehicle' and group_name=='OK':
    # Retail supplies the generated machine thumbnail to this pane; retain
    # its material mapping for the runtime image instead of baking a dummy.
    entry.update(variant=3);replacements=[]
   else:entry['key']=name+suffix+'_'+member
   for key,replacement in replacements:
    image,rect,_=render_layout(p,source,animations,include=(member,),pane_overrides=overrides,animation_groups=animation_groups,texture_overrides={member:replacement} if replacement else None)
    key=key or entry['key'];save(key,image,replacement or p);panel_records[key]=rect
  glow_records.append(entry)

def cook_movies():
 cups=['kinoko','flower','star','special','koura','banana','konoha','thunder']
 names=['thp/course/'+n+'.thp' for n in cups]+['thp/button/single_top.thp','thp/button/class_top.thp','thp/button/drift_select.thp','thp/title/top_menu.thp','thp/button/indiv_team.thp','thp/battle/battle_wii.thp','thp/battle/battle_retro.thp','thp/course/cup_select.thp','thp/battle/battle_select.thp','thp/battle/battle_cup_select.thp']
 paths=extract_movies(names)
 for path in paths:
  source(path);movie=ThpMovie(path);first=movie.frame(0);w,h=first.size
  if path.stem in cups:
   cup=cups.index(path.stem);regions=[(f'course_preview_{cup*4+i}',(0,i*h//4,w,(i+1)*h//4),(200,72)) for i in range(4)]
  elif path.stem=='cup_select':
   # CupSelect::OnCupButtonSelect 0x807dfbe8: 2 columns x 4 rows,
   # exactly 180 pixels per row; the atlas's final 16 pixels are unused.
   regions=[('cup_movie_'+str(i),((i//4)*336,(i%4)*180,(i//4+1)*336,(i%4+1)*180),(168,90)) for i in range(8)]
  elif path.stem=='single_top':
   regions=[('mode_movie_'+str(i),((i//2)*w//2,(i%2)*h//2,(i//2+1)*w//2,(i%2+1)*h//2),(128,h//4)) for i in range(4)]
  elif path.stem=='top_menu':
   # Seven MovieLayout::CropMovie UVs from MainMenu::CreateControl 0x80830f6c.
   # CropMovie passes V before U: Multiplayer is the upper-right three
   # vertical strips; Nintendo WFC is the lower-left two vertical strips.
   boxes=[(0,0,w/2,h/2),(w/2,0,w*2/3,h/2),(w*2/3,0,w*5/6,h/2),(w*5/6,0,w,h/2),
          (0,h/2,w/4,h),(w/4,h/2,w/2,h),(w/2,h/2,w,h)]
   regions=[('main_movie_'+str(i),box,(round((box[2]-box[0])/2),round((box[3]-box[1])/2))) for i,box in enumerate(boxes)]
  elif path.stem=='class_top':
   regions=[('class_movie_'+str(i),(0,i*h//6,w,(i+1)*h//6),(248,32)) for i in range(6)]
  elif path.stem=='drift_select':
   regions=[('drift_movie_'+str(i),(0,i*h//2,w,(i+1)*h//2),(176,64)) for i in range(2)]
  elif path.stem=='indiv_team':
   regions=[('vs_movie_'+str(i),(0,i*h//2,w,(i+1)*h//2),(248,h//4)) for i in range(2)]
  elif path.stem=='battle_select':
   regions=[('battle_mode_movie_'+str(i),(0,i*h/2,w,(i+1)*h/2),(248,60)) for i in range(2)]
  elif path.stem=='battle_cup_select':
   regions=[('battle_cup_movie_'+str(i),(0,i*250,w,(i+1)*250),(168,125)) for i in range(2)]
  else:
   base=0 if path.stem=='battle_wii' else 5
   regions=[('battle_preview_'+str(base+i),((i//3)*w/2,(i%3)*h/3,(i//3+1)*w/2,(i%3+1)*h/3),(168,125)) for i in range(5)]
  for frame in range(16):
   frame_index=frame*movie.count//16;im=movie.frame(frame_index)
   for key,box,target in regions:
    r=save(key+'_'+str(frame),im.resize(target,Image.Resampling.LANCZOS,box=box),path)
    r.update(movieFrame=frame_index,sourceCrop=list(box))
    if frame==0:records[key]=r
  movie_records.append(dict(source=str(path.relative_to(ROOT)),fps=movie.fps,frames=movie.count,sampleFrames=[i*movie.count//16 for i in range(16)],regions=[dict(key=k,crop=list(b)) for k,b,t in regions],sector_h0_verified=True))

def cook_strap():
 from gclib.yaz0_yay0 import Yaz0
 from gclib.gx_enums import ImageFormat,PaletteFormat
 from gclib.texture_utils import decode_image
 archive=extract_movies(['Boot/Strap/us/English.szs'])[0];source(archive)
 brres=ROOT/'assets/menu-ui/Strap_US/English.brres';brres.parent.mkdir(parents=True,exist_ok=True)
 brres.write_bytes(Yaz0.decompress(io.BytesIO(archive.read_bytes())).getvalue());source(brres)
 temp=ROOT/'build/menu-strap';temp.mkdir(parents=True,exist_ok=True)
 subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(brres),str(temp)],check=True,stdout=subprocess.DEVNULL)
 for t in json.loads((temp/'model.json').read_text())['textures']:
  if '_16_9_' not in t['name']:continue
  img=decode_image(io.BytesIO((temp/(t['file']+'.bin')).read_bytes()),io.BytesIO(),ImageFormat(t['format']),PaletteFormat(0),0,t['width'],t['height']).convert('RGBA')
  key='strap_a' if t['name'].startswith('strapA') else 'strap_b'
  record=save(key,img,brres);record['sourceTexture']=t['name'];panel_records[key]=[-304,-228,304,228]

def main():
 global runtime_palette
 # Exact dynamic PushButton palette written by retail Update at0x805b60f4.
 # The values come from the original REL, not hand-picked UI colours.
 from verify_menu_retail import BASE,EXPECTED_HASH
 rel=ROOT/'build/itembox-retail.rel';source(rel)
 if evidence[str(rel.relative_to(ROOT))]!=EXPECTED_HASH:raise ValueError('Menu palette requires the audited RMCU REL')
 raw=rel.read_bytes()
 runtime_palette=[[list(struct.unpack_from('>4h',raw,0x8088fd18-BASE+slot*24+i*8)) for i in range(3)] for slot in range(6)]
 # Source audit retains every UI layout/controller/animation, including deferred pages.
 inventory={}
 for archive in ['MenuSingle','MenuSingle_U','MenuOther','MenuOther_U','Title','Title_U','Award','Award_U','Globe','Channel','Channel_U','Present','Present_U']:
  p=ROOT/'data/extracted/Scene/UI'/f'{archive}.szs';source(p)
  base=ROOT/'assets/menu-ui'/archive
  inventory[archive]={ext:len(list(base.rglob('*.'+ext))) for ext in ['brlyt','brctr','brlan','tpl','bmg']}
 unlock_messages=unlock_entries()
 for kind in ['Course','Driver','Vehicle','Mii']:
  suffix=kind.lower()
  control_root=ROOT/'assets/menu-ui/Present/control'
  body_controller=control_root/'ctrl'/(kind+'_MessageWindowHalf.brctr')
  title_controller=control_root/'ctrl'/(kind+'_MenuObiTitleText.brctr')
  source(body_controller);source(title_controller)
  body_layout=controller_animations(body_controller)['layout']
  title_layout=controller_animations(title_controller)['layout']
  bake_panel('unlock_'+suffix,'Present/control/blyt/'+body_layout+'.brlyt',states=('',),exclude=('text',))
  bake_panel('unlock_title_'+suffix,'Present/control/blyt/'+title_layout+'.brlyt',states=('',),exclude=('text',),bounds=(-304,-228,304,228))
 bake_panel('unlock_next','Present/common/blyt/common_w084_earth_delete.brlyt',exclude=('text',))
 # Each page uses its own authored button layout; no substitute rectangles.
 specs=[
  ('button','MenuSingle/button/blyt/common_w004_menu.brlyt',('text',)),
  ('main','Title/button/blyt/common_w069_title_large_waku.brlyt',('text',)),
  ('main_button','Title/button/blyt/common_w072_title_large_1parts.brlyt',('text',)),
  ('main_multi','Title/button/blyt/common_w067_title_large_3parts.brlyt',('text',)),
  ('main_wifi','Title/button/blyt/common_w071_title_large_2parts.brlyt',('text',)),
  ('mode','MenuSingle/button/blyt/common_w129_movie_button_single_top.brlyt',('text',)),
  ('class','MenuSingle/button/blyt/common_w012_class.brlyt',('text','icon','class_','kinoko_set_p')),
  ('license','Title/button/blyt/common_w076_license_icon_center.brlyt',('text','mii','cc','hatena','bronze','silver','gold','n_','r_')),
  ('option','Title/button/blyt/open_icon_option.brlyt',('text',)),
  ('settings_button','Channel/button/blyt/common_w102_option_menu.brlyt',('text',)),
  ('record_next','MenuOther/button/blyt/common_w084_earth_delete.brlyt',('text',)),
  ('license_management','MenuOther/button/blyt/common_w097_license_screen_menu.brlyt',('text',)),
  ('license_edit','Title/button/blyt/open_icon_lisence.brlyt',('text','mii')),
  ('drift_help','MenuSingle/button/blyt/open_icon_rule.brlyt',('text',)),
  ('character','MenuSingle/button/blyt/common_w006_chara.brlyt',('chara','hatena','ok_','text')),
  ('vehicle','MenuSingle/button/blyt/common_w030_machine_12.brlyt',('machine','chara','hatena','ok_','text')),
  ('drift','MenuSingle/button/blyt/common_w110_drift_menu.brlyt',('text',)),
  ('cup','MenuSingle/button/blyt/common_w010_cup.brlyt',('text','icon','cup','hatena','ok_')),
  ('course','MenuSingle/button/blyt/common_w087_course_movie.brlyt',('text',)),
  ('back','MenuSingle/button/blyt/common_w008_back.brlyt',('text',)),
  ('vs','MenuSingle/button/blyt/common_w115_individual_or_team_movie.brlyt',('text',)),
  ('battle','MenuSingle/button/blyt/common_w094_battle_menu.brlyt',('text',)),
  ('ghost','MenuSingle/button/blyt/common_w083_earth_chat_message.brlyt',('text',)),
  ('ghost_arrow_left','MenuSingle/button/blyt/common_w016_yajirushi_left.brlyt',('text',)),
  ('ghost_arrow_right','MenuSingle/button/blyt/common_w021_yajirushi_right.brlyt',('text',)),
  ('confirm','MenuSingle/button/blyt/common_w109_go_button.brlyt',('text',)),
 ]
 for name,resource,exclude in specs:bake_panel(name,resource,exclude=exclude)
 for team in ('red','blue'):
  for human in (False,True):
   bake_panel('team_'+team+('_human' if human else '_cpu'),'MenuSingle/control/blyt/common_w051_team_blue_red_chara_point.brlyt',states=('',),exclude=('mii_name','point','pts','chara_icon'),hidden=('blue_null' if team=='red' else 'red_null','point','pts'),group_states={'Select':'SelectOn' if human else 'SelectOff','Select2':'Select2On' if human else 'Select2Off'})
 bake_panel('record_row','MenuOther/control/blyt/record_parts.brlyt',states=('',),exclude=('menu_text','score'),hidden=('grade',))
 bake_panel('rule_base','MenuSingle/control/blyt/common_w023_rule_menu.brlyt',exclude=('text',))
 bake_panel('rule_option','MenuSingle/control/blyt/common_w024_rule_icon.brlyt',exclude=('text',),group_states={'Choice':'ChoiceOff'})
 bake_panel('rule_chosen','MenuSingle/control/blyt/common_w024_rule_icon.brlyt',exclude=('text',),group_states={'Choice':'ChoiceOn'})
 bake_panel('rule_value','MenuSingle/control/blyt/common_w048_rule_rap.brlyt')
 bake_panel('rule_count','MenuSingle/control/blyt/common_w048_rule_rap_text.brlyt',exclude=('text',),bounds=(-120,-25,120,25),group_states={'Slide':'Show'})
 bake_panel('rule_left','MenuSingle/control/blyt/common_w134_rule_rap_yaji_l.brlyt')
 bake_panel('rule_right','MenuSingle/control/blyt/common_w135_rule_rap_yaji_r.brlyt')
 # PressStart is a real layout/control, with a separate expanding light
 # text pane and a squash/stretch confirmation animation. The fade group is
 # evaluated live; keep an opaque reference pose for the text textures.
 bake_panel('press_start','Title/press_start/blyt/press_start.brlyt',states=('',),exclude=('text',),bounds=(-200,-30,200,30),overrides={'press_start_null':{'alpha':1}})
 # Pages::LicenseSelect::OnInit uses top/bottom variants. Empty RKPD slots
 # hide only the native data panes; the authored blank card stays intact.
 empty_license=['mii']+[f'{prefix}_{i:02d}' for prefix in ['cc50','cc100','cc150','cc_mirror'] for i in range(8)]
 empty_license+=['n_0_kinoko','n_1_flower','n_2_star','n_3_special','r_0_koura','r_1_banana','r_2_konoha','r_3_thunder','n_2_hatena','n_3_hatena','r_2_hatena','r_3_hatena']
 bake_panel('license_center_empty','MenuOther/control/blyt/common_w076_license_icon_center.brlyt',states=('',),hidden=empty_license)
 # Populated cards keep cup icons and expose trophy panes to the composer;
 # only the earned trophy is drawn at each of the 4 x 8 source positions.
 used_hidden=['mii']
 trophy_prefixes=('cc50_','cc100_','cc150_','cc_mirror_','n_','r_')
 bake_panel('license_center_used','MenuOther/control/blyt/common_w076_license_icon_center.brlyt',states=('',),hidden=used_hidden,exclude=trophy_prefixes)
 trophy_layout=ROOT/'assets/menu-ui/Title/button/blyt/common_w076_license_icon_tex_top.brlyt'
 nodes=pane_nodes(trophy_layout)
 # LicenseControl::Refresh toggles these original pictures independently.
 # Keep the card's own artwork, rather than substituting menu cup icons.
 card_icons=['n_0_kinoko','n_1_flower','n_2_star','n_3_special','r_0_koura','r_1_banana','r_2_konoha','r_3_thunder','n_2_hatena','n_3_hatena','r_2_hatena','r_3_hatena']
 card_nodes={n['name']:n for n in nodes}
 for field in card_icons:
  isolated={n['name']:{'visible':False} for n in nodes if n['kind'] in ('pic1','wnd1')}
  parent=card_nodes[field]['parent']
  while parent:
   isolated[parent]={'x':0,'y':0,'sx':1,'sy':1,'alpha':1,'visible':True};parent=card_nodes[parent]['parent']
  isolated[field]={'x':32,'y':-32,'w':64,'h':64,'sx':1,'sy':1,'alpha':1,'visible':True}
  icon,_,_=render_layout(trophy_layout,source,bounds=(0,0,64,64),include=(field,),pane_overrides=isolated,aspect_scale=(1,1))
  save('license_icon_'+field,icon,trophy_layout)
 overrides={n['name']:{'visible':False} for n in nodes if n['kind'] in ('pic1','wnd1')}
 by_name={n['name']:n for n in nodes};parent=by_name['cc50_00']['parent']
 while parent:
  overrides[parent]={'x':0,'y':0,'sx':1,'sy':1,'alpha':1,'visible':True};parent=by_name[parent]['parent']
 overrides['cc50_00']={'x':32,'y':-32,'w':64,'h':64,'sx':1,'sy':1,'alpha':1,'visible':True}
 for rank,color in enumerate(('gold','silver','bronze'),1):
  palette=by_name[color+'_color']['materialColors']+[[255,255,255,255]]
  icon,_,_=render_layout(trophy_layout,source,bounds=(0,0,64,64),include=('cc50_00',),pane_overrides=overrides,material_overrides={'cc50_00':palette},aspect_scale=(1,1))
  save('license_trophy_'+str(rank),icon,trophy_layout)

 for position in ['top','bottom']:
  bake_panel('license_'+position+'_used','Title/button/blyt/common_w076_license_icon_tex_'+position+'.brlyt',hidden=used_hidden,exclude=trophy_prefixes)
  bake_panel('license_'+position+'_empty','Title/button/blyt/common_w076_license_icon_tex_'+position+'.brlyt',hidden=empty_license)
 bake_panel('background','MenuSingle/bg/blyt/bg.brlyt',states=('',),bounds=(-304,-228,304,228))
 bake_panel('header','MenuSingle/bg/blyt/obi_top.brlyt',states=('',),bounds=(-304,-228,304,-125),overrides={'RootPane':{'x':2.1972661,'y':210}})
 bake_panel('footer','MenuSingle/bg/blyt/obi_bottom.brlyt',states=('',),bounds=(-304,125,304,228))
 bake_panel('footer_help','MenuSingle/bg/blyt/obi_bottom_message.brlyt',states=('',),bounds=(-304,125,304,228))
 bake_panel('confirm_top','MenuSingle/bg/blyt/obi_top_go.brlyt',states=('',),bounds=(-304,-110,304,40))
 bake_panel('confirm_bottom','MenuSingle/bg/blyt/obi_bottom_go.brlyt',states=('',),bounds=(-304,-20,304,100))
 bake_panel('confirm_back','MenuSingle/message_window/blyt/black_down_race.brlyt',states=('',),bounds=(-304,-228,304,228))
 bake_panel('drift_message','MenuSingle/message_window/blyt/common_w017_message.brlyt',states=('',),exclude=('text',))
 bake_panel('message_ok','MenuSingle/message_window/blyt/a_button_for_message_window.brlyt',states=('',),exclude=('a_button',),bounds=(-32,-24,32,24))
 bake_panel('cup_course','MenuSingle/control/blyt/common_w137_course_movie_text_only.brlyt',bounds=(-154,-26,154,26),exclude=('text',))
 # BattleCup::ControlStages::Load explicitly hides the noninteractive row's
 # frame; its source text boxes remain populated with the five stage names.
 bake_panel('battle_course','MenuSingle/control/blyt/common_w137_course_movie_text_only.brlyt',bounds=(-154,-26,154,26),exclude=('text',),hidden=('waku_null',))
 bake_panel('ghost_info','MenuSingle/control/blyt/common_w075_ghost_data.brlyt',states=('',),exclude=('text',),hidden=('handle','mii','nationality','chara','nintendo','machine','new_icon'),group_states={'Slide':'Show','NewIcon':'NewHide'})
 bake_panel('ghost_page','MenuSingle/control/blyt/common_w104_page_number.brlyt',states=('',),bounds=(-150,-50,150,50),exclude=('text',))
 _,_,pane_records['header_text']=render_layout(UI/'bg/blyt/obi_top_text.brlyt',source,bounds=(-304,-228,304,228))
 _,_,pane_records['class_switch']=render_layout(UI/'control/blyt/common_w105_close_text.brlyt',source,bounds=(-304,-228,304,228))
 for name,resource in [('character_name','MenuSingle/control/blyt/common_w027_chara_name.brlyt'),('vehicle_name','MenuSingle/control/blyt/common_w028_machine_name.brlyt'),('vehicle_ability','MenuSingle/control/blyt/common_w099_machine_ability.brlyt'),('course_movie_frame','MenuSingle/control/blyt/common_w089_course_big_movie.brlyt')]:
  bake_panel(name,resource,states=('',),exclude=('text','ability_graph') if name=='vehicle_ability' else ('text',))
 graph=UI/'control/blyt/common_w099_machine_ability.brlyt'
 # CtrlMenuMachineGraph::InitSelf 0x8083fe6c sets corner zero white and the
 # remaining corners to the arithmetic mean of the player's two HUD colours.
 blend=struct.unpack_from('>f',raw,0x808a9c50-BASE)[0]
 graph_color=[min(255,int((runtime_palette[1][0][i]+runtime_palette[1][1][i])*blend)) for i in range(3)]+[255]
 im,rect,panes=render_layout(graph,source,include=('ability_graph',),pane_overrides={'ability_graph':{'vertexColors':[[255,255,255,255],graph_color,graph_color,graph_color]}})
 save('vehicle_ability_graph',im,graph);panel_records['vehicle_ability_graph']=rect;pane_records['vehicle_ability_graph']=panes
 tpl('cursor',UI/'button/timg/tt_yajirushi_type1.tpl')
 tpl('checker',UI/'bg/timg/tt_pattern_chek_alpha_nasi_32x32.tpl')
 tpl('header_strip',UI/'bg/timg/tt_obi_top_curve_000.tpl')
 tpl('glass_button',UI/'button/timg/tt_glass_waku_180.tpl')
 tpl('glass_focus',UI/'button/timg/tt_glass_sphere_waku_sono1.tpl')
 tpl('title',ROOT/'assets/menu-ui/Title/title/timg/tt_title_screen_mario2.tpl')
 tpl('logo',ROOT/'assets/menu-ui/Title_U/title/timg/tt_title_screen_title_rogo_r_only.tpl')
 # Original title art spans exactly 832x456 once the title logo pane has
 # settled. Keep the source's two image sizes and edge-to-edge composition.
 title=Image.new('RGBA',(832,456),(255,255,255,255))
 title.alpha_composite(decode_tpl(ROOT/'assets/menu-ui/Title/title/timg/tt_title_screen_mario2.tpl'),(0,140))
 title.alpha_composite(decode_tpl(ROOT/'assets/menu-ui/Title_U/title/timg/tt_title_screen_title_rogo_r_only.tpl'),(0,0))
 save('title_scene',title.crop((112,0,720,456)));panel_records['title_scene']=[-304,-228,304,228]
 blur=Image.new('RGBA',(832,456),(255,255,255,255))
 for archive,name,y in [('Title','tt_title_screen_mario2_bokeboke.tpl',140),('Title_U','tt_title_screen_title_rogo_bokeboke.tpl',0)]:
  p=ROOT/'assets/menu-ui'/archive/'title/timg'/name;source(p);blur.alpha_composite(decode_tpl(p),(0,y))
 save('title_blur_scene',blur.crop((112,0,720,456)));panel_records['title_blur_scene']=[-304,-228,304,228]
 # Retail TitleImage: default mario_luiji, then five unlocked alternatives.
 for index,(archive,art) in enumerate([('Title_U','mario0'),('Title','mario'),('Title','mario2'),('Title','peachi'),('Title','luigi'),('Title','koopa')]):
  for blurred in (False,True):
   suffix='_bokeboke' if blurred else ''
   logo='tt_title_screen_title_rogo_bokeboke.tpl' if blurred else 'tt_title_screen_title_rogo_r_only.tpl'
   scene=Image.new('RGBA',(832,456),'white')
   for image,y in [(ROOT/'assets/menu-ui'/archive/'title/timg'/f'tt_title_screen_{art}{suffix}.tpl',140),(ROOT/'assets/menu-ui/Title_U/title/timg'/logo,0)]:
    source(image);scene.alpha_composite(decode_tpl(image),(0,y))
   key=f'title_{"blur_scene" if blurred else "scene"}_{index}'
   save(key,scene.crop((112,0,720,456)));panel_records[key]=[-304,-228,304,228]
 tpl('locked_choice',UI/'button/timg/tt_hatena_64x64.tpl')
 drivers=['mario','luigi','peach','yoshi','baby_mario','baby_peach','kinopio','noko','wario','waluigi','donky','koopa','baby_daisy','baby_luigi','catherine','daisy','didy','karon','hone_koopa','funky','teresa','roseta','kinopico','koopa_jr','mii_suit_a','mii_suit_b']
 for i,n in enumerate(drivers):tpl('driver_'+str(i),UI/'button/timg'/f'tt_{n}_64x64.tpl')
 cups=['kinoko','flower','star','oukan','koura','banana','konoha','thunder']
 for i,n in enumerate(cups):tpl('cup_'+str(i),UI/'button/timg'/f'tt_cup_icon_{n}_00.tpl')
 tpl('cup_mirror',ROOT/'assets/menu-ui/Present/control/timg/tt_cup_icon_mirror_00.tpl')
 for i,n in enumerate(['tt_wii_icon_128.tpl','tt_classic_icon_sphere_128.tpl']):
  tpl('battle_cup_'+str(i),UI/'button/timg'/n)
 for i,n in enumerate(['1','2','3','m']):tpl('class_'+str(i),UI/'button/timg'/f'tt_class_kinoko_{n}.tpl')
 for weight in 'sml':
  for i,part in enumerate(['df_kart','a_kart','b_kart','df_bike','a_bike','b_bike','c_kart','d_kart','e_kart','c_bike','d_bike','e_bike']):tpl('vehicle_'+weight+str(i),UI/'control/timg'/f'st_{weight}{part}.tpl')
 for group in ['OK','SelectIn']:
  for name,resource,_ in specs:bake_ok_glows(name,resource,drivers,cups,group)
  for position in ['top','bottom']:
   for kind in ['empty','used']:bake_ok_glows('license_'+position+'_'+kind,'Title/button/blyt/common_w076_license_icon_tex_'+position+'.brlyt',drivers,cups,group)
 cook_movies()
 cook_strap()
 message_sources={}
 for archive in ['Title_U','MenuSingle_U','Present_U']:
  for p in sorted((ROOT/'assets/menu-ui'/archive/'message').glob('*.bmg')):
   if p.name.startswith('._'):continue
   source(p);message_sources[str(p.relative_to(ROOT))]=messages(p)
   if message_font_indices(p)!={1}:raise ValueError('This localized menu font mapping requires BMG font index 1: '+str(p))
 menu_messages=message_sources['assets/menu-ui/MenuSingle_U/message/Menu.bmg']
 help_ids={'UiHelpMain':[0x7e4,0x7e5,0x7e8,0x7ea],'UiHelpMainMulti':[0x7e5,0x7e6,0x7e7],'UiHelpWfc':[0x7e8,0x7e9],'UiHelpMode':[0xbea,0xbeb,0xbec,0xbee],'UiHelpEngineClasses':[0xbf4,0xbf5,0xbf6,0xbf7],'UiHelpDrift':[0xd16,0xd17],'UiHelpVSMode':[0xd8c,0xd8b]}
 help_ids['UiHelpEngineClassesAllKartBike']=[0xbf8,0xbf9,0xbf6,0xbf7]
 help_ids['UiDriftHelpMessage']=[0xced]
 help_ids['UiVsRulesHelp']=[0xd57,0xd58,0xd59,0xd5a,0xd61,0xd62,0xd63,0xd64,0xd6a,0xd6b,0xd6c,0xd6c,0xd74,0xd75,0xd76,0xd76,0xd9d,0xd9e,0xd9f,0xda0,0xd89,0xd90]
 help_text={name:[menu_messages[i] for i in ids] for name,ids in help_ids.items()}
 multiline_specs={menu_messages[i]:.7 for i in range(0x8fd,0x901)}
 multiline_specs.update({menu_messages[i]:.5 for i in range(0xbbe,0xbc4)})
 stats_path=UI/'parameter/machine_para.bin';source(stats_path);stats=struct.unpack('>441h',stats_path.read_bytes());driver_stats=[list(stats[i*7:i*7+7]) for i in range(27)]
 vehicle_order=[0,3,6,18,21,24,9,12,15,27,30,33]
 vehicle_stats=[[[*stats[(27+base+weight)*7:(28+base+weight)*7]] for base in vehicle_order] for weight in range(3)]
 # Whole labels keep glyph spacing from the original BRFNT CWDH tables.
 roster=json.loads((OUT/'models/roster.json').read_text());garage=json.loads((OUT/'models/garage.json').read_text())
 courses=json.loads((OUT/'models/course-catalog.json').read_text())['courses']
 course_names=re.findall(r'\{"([^"]+)"', (ROOT/'include/mkw/course_catalog.hpp').read_text())[:32]
 labels=['--','0%','General','Favorites','Other Records','Next','Save failed','Erase License?','Single Player','Multiplayer','Nintendo WFC','Main Menu','Grand Prix','Time Trials','VS','Battle','Not available','Character','Character Select','Vehicle','Machine Select','Drift','Drift Type','Automatic','Manual','Cup','Select Cup','Course','Start Race','Pause','Continue','Restart','Quit','Quit this race?','Yes','No','Results','Race Again','Change Course','Change Character','Main Menu','Press A','A: Select    B: Back','A: Select    B: Cancel','A: Start    B: Back','START: Pause','A: Next    B: Back','Loading...','Race unavailable','Choose another course','Finish','Racing','12 racers','No teams','150cc','A: Accelerate   B: Brake','R: Drift   L: Item','Circle Pad: Steer','D-pad: Trick / Wheelie','Hold down + L: Throw back','A: Confirm    B: Cancel','Manual drift','Automatic drift','VS Race','Course Select','Single race','Mushroom Cup','Flower Cup','Star Cup','Special Cup','Shell Cup','Banana Cup','Leaf Cup','Lightning Cup']
 labels+=['Mario','Luigi','Peach','Yoshi','Baby Mario','Baby Peach','Toad','Koopa Troopa','Wario','Waluigi','Donkey Kong','Bowser']
 labels+=course_names+[c['name'] for c in garage['choices']]+list('0123456789:.-/%')
 labels+=[text for values in help_text.values() for text in values]
 labels+=re.findall(r'\"([^\"]+)\"',(ROOT/'include/mkw/menu_ui.hpp').read_text())
 labels+=['Characters','Vehicles','Engine Classes','Cups','Courses','Select License','New','License','License 1','License 2','License 3','License 4','50cc','100cc','150cc','Mirror','Solo Race','Team Race','Balloon Battle','Coin Runners','Wii Stages','Retro Stages','Select Stage','Speed','Weight','Acceleration','Handling','Drift','Off-Road','Mini-Turbo','Karts Only','Bikes Only','Karts and Bikes','Press the A Button','OK','Back','Options','Mario Kart Channel','Grand Prix Race','Time Trial','VS Settings','Battle Settings','Unsupported race configuration','Create a license','Attach the Wii Remote wrist strap.','Press A to continue.','Please use the wrist strap.','A: Select','1 Player','2 Players','3 Players','4 Players','Race This Ghost','Watch the Replay','Solo Time Trials','Player','Players','Booster Seat','Flame Runner','Jet Bubble']
 unlock_texts=list(dict.fromkeys(text for row in unlock_messages for text in [row['title'],row['body']]))
 labels+=unlock_texts+[line for text in unlock_texts for line in text.split('\n')]
 labels+=menu_messages[0xced].split('\n')
 labels=[x for x in dict.fromkeys(labels) if x and not x.startswith('mkw/')]
 # Preserve each source text pane's TEV mapping. Bake only strings used by
 # that control, with identical images sharing the same native texture.
 header_source=(ROOT/'include/mkw/menu_ui.hpp').read_text()
 def header_strings(start,end):return re.findall(r'"([^"]+)"',header_source.split(start,1)[1].split(end,1)[0])
 character_labels=header_strings('UiCharacters{{','}};')
 vehicle_labels=header_strings('UiVehicleNames[3][12]=','};')
 battle_labels=header_strings('UiBattleStageNames[]=',';')
 roles={
  'header_text':['Main Menu','Select License','Characters','Vehicles','Engine Classes','Cups','Courses','Drift','Grand Prix','Time Trials','Single Player','VS','Battle','Stages','Ghosts','Wii Stages','Retro Stages','Select Stage','Start Race','Rules','Teams','Settings','License Settings','Erase License?','General','Favorites','Other Records'],
  'footer_help':[v for vv in help_text.values() for v in vv],
  'main':['Single Player','Multiplayer','Nintendo WFC','Mario Kart Channel'],
  'main_button':['1','Player','1\nPlayer'],'main_multi':['2','3','4','Players','2\nPlayers','3\nPlayers','4\nPlayers'],'main_wifi':['1','2','Player','Players','1\nPlayer','2\nPlayers'],
  'mode':['Grand Prix','Time Trials','VS','Battle'], 'class':['50cc','100cc','150cc','Mirror']+[menu_messages[i] for i in range(0xbbe,0xbc4)],
  'class_switch':['Switch'],
  'license':['NEW','New','License','License 1','License 2','License 3','License 4','50cc','100cc','150cc','Mirror'],
  'option':['Settings'],'license_edit':['License Settings'],
  'record_next':['Next'],
  'record_row':['Overall Ranking','Distance Traveled','Total Race Count','Total Battle Count','Wii Wheel %','Tricks','Favorite Character','Favorite Vehicle','Favorite Course','Favorite Stage','First-Place Appearances','% Race Time in 1st Place','Item Hits Delivered','Item Hits Received','Ghost Races','Tournaments Played','--','0%']+course_names+[c['name'] for c in garage['choices']]+[info['name'] for info in roster['characters']]+battle_labels+list('0123456789%'),
  'license_management':['Check Records','Change Miis','Erase License'],
  'settings_button':['Nintendo GameCube Controller','Network Settings','Mario Kart Channel'],
  'rule_base':['Class','CPU','CPU Vehicles','Courses','Items','Race Count'],
  'rule_option':['50cc','100cc','150cc','Mirror','Easy','Normal','Hard','Karts','Bikes','All','Choose','Random','In Order','Balanced','Aggressive','Strategic','None'],
  'rule_chosen':['50cc','100cc','150cc','Mirror','Easy','Normal','Hard','Karts','Bikes','All','Choose','Random','In Order','Balanced','Aggressive','Strategic','None'],
  'rule_count':['Races: '+str(n) for n in (2,3,4,5,8,10,12,16,32)],
  'drift_help':['Help','Rules'],'drift_message':[menu_messages[0xced]],'message_ok':['A'],
  'license_top_empty':['NEW'],'license_bottom_empty':['NEW'],
  'license_top_used':['License 1','License 2','License 3','License 4','50cc','100cc','150cc','Mirror'],'license_bottom_used':['License 1','License 2','License 3','License 4','50cc','100cc','150cc','Mirror'],'license_center_used':['License 1','License 2','License 3','License 4','50cc','100cc','150cc','Mirror'],
  'team_red_human':character_labels,'team_red_cpu':character_labels,'team_blue_human':character_labels,'team_blue_cpu':character_labels,
  'character_name':character_labels,'vehicle_name':vehicle_labels,
  'vehicle_ability':['Speed','Weight','Acceleration','Handling','Drift','Off-Road','Mini-Turbo'],
  'drift':['Automatic','Manual'],'cup':['Mushroom Cup','Flower Cup','Star Cup','Special Cup','Shell Cup','Banana Cup','Leaf Cup','Lightning Cup','Wii Stages','Retro Stages'], 'character':[], 'vehicle':[],
  'cup_course':course_names,
  'battle_course':battle_labels,
  'ghost':['Race This Ghost','Watch the Replay','Solo Time Trials'],
  'ghost_info':course_names+['There are no ghosts for this course.'],
  'course':course_names+battle_labels,'vs':['Solo Race','Team Race'],'battle':['Balloon Battle','Coin Runners'],
  'confirm':['OK','Start Race','Race This Ghost','Watch the Replay','Solo Time Trials'],
  'back':['←Back','Back'],
  'button':['Race This Ghost','Watch the Replay','Solo Time Trials','Yes','No','Continue','Restart','Quit','Race Again','Change Course','Change Character','Main Menu','Not available','OK']}
 for kind in ['Course','Driver','Vehicle','Mii']:
  roles['unlock_'+kind.lower()]=[row['body'] for row in unlock_messages if row['kind']==kind]
  roles['unlock_title_'+kind.lower()]=[row['title'] for row in unlock_messages if row['kind']==kind]
 roles['unlock_next']=['Next']
 for required in roles.values():
  for label in required:
   if label not in labels:labels.append(label)
 roles['press_start']=['Press the A Button']
 text_styles=[];style_labels={}
 for panel,panes in pane_records.items():
  role=re.sub(r'_(free|select|ok|selectin)$','',panel)
  for name,pane in panes.items():
   if pane.get('kind')!='txt1':continue
   # SetMessage replaces the authored font with the BMG INF1 choice. Every
   # consumed English BMG record selects retail font 1, kart_kanji_font.
   # Its normalized index in UiFontCells/UiSprites is zero.
   pane.setdefault('authoredFont',pane['font']);pane['font']=0;pane['messageFontIndex']=1
   style={'colors':pane['textColors'],'vertices':pane['textVertexColors']}
   if style not in text_styles:text_styles.append(style)
   index=text_styles.index(style)+1;pane['textStyle']=index
   if panel.endswith(('_ok','_selectin')) or name not in ('text_light_01','text_light_02'):
    style_labels.setdefault(index,set()).update(roles.get(role,[]))
 font_cells=[]
 for font_id,fontname in enumerate(['kart_kanji_font.brfnt','tt_kart_font_rodan_ntlg_pro_b.brfnt']):
  font=ROOT/'assets/race-ui/Font'/fontname;source(font)
  data=font.read_bytes()
  for tag,_,at in sections(data):
   if tag==b'FINF':font_cells.append([data[at+21],data[at+20]])
  available=set()
  for tag,_,at in sections(data):
   if tag==b'CMAP':
    lo,hi,kind=struct.unpack_from('>3H',data,at)
    if kind==0:available.update(range(lo,hi+1))
    elif kind==1:available.update(c for c in range(lo,hi+1) if struct.unpack_from('>H',data,at+12+2*(c-lo))[0]!=65535)
    else:
     count=struct.unpack_from('>H',data,at+12)[0];available.update(struct.unpack_from('>H',data,at+14+i*4)[0] for i in range(count))
  supported=[label for label in labels if '\n' not in label and all(ord(c) in available for c in label)]
  glyphs=font_strings(font,supported)
  if font_id==0:
   # BMG 0x8fd..0x900 use font-size tag 0x0046 (70 percent); engine-class
   # messages 0xbbe..0xbc3 use 0x0032 (50 percent) after their line feed.
   # MessageBox uses the authored line breaks in BMG 0xCED. Keep all lines
   # in one sprite so the original pane controls alignment and fitting.
   message=menu_messages[0xced];lines=message.split('\n')
   line_h=max(glyphs[line].height for line in lines if line)
   block=Image.new('RGBA',(max(glyphs[line].width for line in lines if line),line_h*len(lines)))
   for row,line in enumerate(lines):
    if line:block.alpha_composite(glyphs[line],(0,row*line_h))
   glyphs[message]=block
   for message in unlock_texts:
    if '\n' not in message:continue
    lines=message.split('\n')
    if any(line and line not in glyphs for line in lines):raise ValueError('Missing unlock font glyph: '+repr(message))
    line_h=max(glyphs[line].height for line in lines if line)
    block=Image.new('RGBA',(max(glyphs[line].width for line in lines if line),line_h*len(lines)))
    for row,line in enumerate(lines):
     if line:block.alpha_composite(glyphs[line],((block.width-glyphs[line].width)//2,row*line_h))
    glyphs[message]=block
   for label,scale in multiline_specs.items():
    first,second=label.split('\n');number=glyphs[first];word=glyphs[second]
    word=word.resize((round(word.width*scale),round(word.height*scale)),Image.Resampling.LANCZOS)
    result=Image.new('RGBA',(max(number.width,word.width),number.height+word.height))
    result.alpha_composite(number,((result.width-number.width)//2,0));result.alpha_composite(word,((result.width-word.width)//2,number.height))
    glyphs[label]=result
  for i,label in enumerate(labels):
   if label not in glyphs:continue
   r=save(('text_' if font_id==0 else 'text_rodan_')+str(i),glyphs[label],font);r.update(text=label,font=font_id)
   unique={hashlib.sha256(glyphs[label].tobytes()).digest():r}
   for style_id,required in style_labels.items():
    if label not in required:continue
    style=text_styles[style_id-1];styled=font_style(glyphs[label],style['colors'],style['vertices'])
    pixel_hash=hashlib.sha256(styled.tobytes()).digest();key=f'text_style_{style_id}_{font_id}_{i}'
    if pixel_hash in unique:
     record=dict(unique[pixel_hash]);records[key]=record
    else:
     record=save(key,styled,font);unique[pixel_hash]=record
    record.update(text=label,font=font_id,style=style_id)
 # Controller glyphs are original BRFNT extension glyphs, selected by BMG controls.
 extension=ROOT/'assets/race-ui/Font/tt_kart_extension_font.brfnt';source(extension)
 glyph=font_strings(extension,['\uf058','\uf03c','\uf05e','\uf05f'])
 save('button_b',glyph['\uf058'],extension);save('button_a',glyph['\uf03c'],extension)
 arrowfont=ROOT/'assets/race-ui/Font/kart_kanji_font.brfnt';parts=font_strings(arrowfont,['←','Back'])
 style=text_styles[pane_records['back_free']['text']['textStyle']-1]
 arrow=font_style(parts['←'],style['colors'],style['vertices']);word=font_style(parts['Back'],style['colors'],style['vertices'])
 back=Image.new('RGBA',(arrow.width+glyph['\uf058'].width+word.width,51));back.alpha_composite(arrow,(0,0));back.alpha_composite(glyph['\uf058'],(arrow.width,0));back.alpha_composite(word,(arrow.width+glyph['\uf058'].width,0));save('button_back',back,extension)
 # BMG 0xbc4 substitutes 0x902 for the GameCube controller. Group-3 tag
 # [F05E,30,F05F,30] alternates the original Start glyph every thirty ticks;
 # 30 is its animation duration, not an invented glyph size or advance.
 switch=font_strings(arrowfont,['Switch'])['Switch']
 for pane_name,suffix in [('close',''),('close_sha','_shadow')]:
  pane=pane_records['class_switch'][pane_name];style=text_styles[pane['textStyle']-1]
  word=font_style(switch,style['colors'],style['vertices'])
  for i,code in enumerate(['\uf05e','\uf05f']):
   start=glyph[code];combined=Image.new('RGBA',(start.width+word.width,max(start.height,word.height)))
   combined.alpha_composite(start,(0,0));combined.alpha_composite(word,(start.width,0))
   save('button_switch'+suffix+'_'+str(i),combined,extension)
 maps=[]
 # Catalog order differs from cup order. Resolve via the runtime archive names.
 archives=re.findall(r'"Race/Course/([^"]+)\.szs"',(ROOT/'include/mkw/course_catalog.hpp').read_text())[:32]
 for i,archive in enumerate(archives):
  path=ROOT/'assets/courses'/archive/'map_model.brres';source(path)
  temp=ROOT/'build/menu-maps'/archive;temp.mkdir(parents=True,exist_ok=True)
  receipt=json.loads(subprocess.check_output(['node',str(ROOT/'build/export_brres.mjs'),str(path),str(temp)],text=True))
  model=json.loads((temp/'model.json').read_text());nodes={n['name']:n['t'] for m in receipt['nodes'] for n in m['nodes']}
  ld,ru=nodes['posLD'],nodes['posRU'];xmin,xmax=sorted([ld[0],ru[0]]);zmin,zmax=sorted([ld[2],ru[2]])
  size=256;img=Image.new('RGBA',(size,size));draw=ImageDraw.Draw(img)
  # Top-down source mesh projection in the authored map camera rectangle.
  for m in model['models']:
   for shape in m['shapes']:
    vv=shape['vertices']
    for k in range(0,len(vv),3):
     tri=vv[k:k+3];pts=[((v[0]-xmin)/(xmax-xmin)*(size-1),(v[2]-zmin)/(zmax-zmin)*(size-1)) for v in tri]
     color=tuple(round(sum(v[10+c] for v in tri)/3) for c in range(4));draw.polygon(pts,fill=color)
  r=save('map_'+str(i),img,path);maps.append({'bounds':[xmin,zmin,xmax,zmax],'sourceNodes':nodes,'sprite':'map_'+str(i),'course':course_names[i]})
 # Recover selection magnification and entry duration from retail BRLAN.
 anim=UI/'button/anim/common_w004_menu_free_to_select.brlan';source(anim);dur,tracks=animation(anim)
 scales=[sample(tracks[('pause_w_fade_n','RLPA',6)],i) for i in range(dur)]
 ctr={};alphas={};qualified_controllers={}
 controller_paths=[p for archive in ['Title','MenuSingle'] for p in sorted((ROOT/'assets/menu-ui'/archive).rglob('*.brctr'))]
 controller_paths += [ROOT/'assets/menu-ui/MenuOther'/part/'ctrl'/(name+'.brctr') for part,name in [('button','LicenseManagementButton'),('control','LicenseManagement'),('control','Record'),('button','NextRecordButton')]]
 controller_paths += [ROOT/'assets/menu-ui/Channel/button/ctrl/OptionTopButton.brctr']
 controller_paths += [ROOT/'assets/menu-ui/Present/common/ctrl/NextButton.brctr']
 controller_paths += list((ROOT/'assets/menu-ui/Present/control/ctrl').glob('*.brctr'))
 controller_paths += [ROOT/'assets/race-ui/Race/button/ctrl'/(name+'.brctr') for name in ['PauseMenuVS','PauseMenuGrandPrix','PauseMenuTimeAttack','PauseMenuBT']]
 for p in controller_paths:
  if p.name.startswith('._'):continue
  source(p);data=controllers(p);ctr.setdefault(p.stem,{}).update(data);alphas.setdefault(p.stem,{}).update(controller_alphas(p));qualified_controllers[str(p.relative_to(ROOT))]=data
 manifest={'version':2,'canvas':{'width':608,'height':456,'artBleedWidth':832,'aspectAdjustScale':[608/832,1.0],'displayAspect':'16:9','target':'400x225 centered on top LCD; lower LCD unused'},'sprites':records,'maps':maps,'sourceHashes':evidence,'archiveInventory':inventory,'controllers':ctr,'controllerAlphas':alphas,'controllerSources':qualified_controllers,'messageSources':message_sources,'helpMessageIds':help_ids,'runtimePalette':{'source':'build/itembox-retail.rel','tableAddress':'0x8088fd18','slots':runtime_palette,'borderPane':'fuchi_pattern','basePane':'color_base'},'textStyles':text_styles,'driverStats':driver_stats,'vehicleStats':vehicle_stats,'panels':panel_records,'panes':pane_records,'paneNodes':node_records,'movies':movie_records,'glows':glow_records,'focusSprites':focus_records,'panelTails':list(panel_layer_records.values()),'borderLoops':border_records,'borderSlices':border_slices,'selectionScale60Hz':scales,'limitations':['This menu inventory uses 16 source samples across each retail loop for host previews and fallback. Full-rate streamed movies are cooked and receipted separately.','Offline pane rasterization supports 2D affine transforms and authored TEV arithmetic; live 3D preview objects are rendered separately.','Original font glyphs and spacing are retained; per-page dynamic text effects are runtime responsibilities.']}
 (OUT/'models/menu-ui.json').write_text(json.dumps(manifest,indent=2)+'\n')
 notice_header=['#pragma once','namespace mkw {','struct UiUnlockText {unsigned condition,bit;const char* title;const char* body;const char* kind;int retailId;};','inline constexpr UiUnlockText UiUnlockTexts[]={']
 for row in unlock_messages:notice_header.append('{'+','.join([str(row['condition']),str(row['bit']),json.dumps(row['title']),json.dumps(row['body']),json.dumps(row['kind']),str(row['retailId'])])+'},')
 notice_header+=['};','}']
 (ROOT/'include/mkw/menu_unlock_texts.hpp').write_text('\n'.join(notice_header)+'\n')
 lines=['#pragma once','#include <array>','namespace mkw {','struct UiSpriteInfo {const char* key;const char* path;float w,h,u,v;const char* text;unsigned font,style,gpuBytes;};','inline constexpr UiSpriteInfo UiSprites[]={']
 for n,r in records.items():lines.append('{'+','.join([json.dumps(n),json.dumps(r['path']),*(f'{float(r[k]):.8f}f' for k in ['width','height','u','v']),json.dumps(r.get('text','')),str(r.get('font',0)),str(r.get('style',0)),str(r['padded'][0]*r['padded'][1]*4)])+'},')
 from cook_menu_names import cook as cook_names
 cook_names()
 from cook_menu_record_text import cook as cook_record_text
 cook_record_text()
 lines+=['#include <mkw/menu_name_glyphs.hpp>','#include <mkw/battle_menu_text.inc>','#include <mkw/menu_record_text.inc>','#include <mkw/menu_footer_text.inc>','{"cup_record_rank_atlas","textures/ui_cup_record_icons.t3x",256.f,256.f,1.f,1.f,"",0,0,262144},','};']
 for name,values in help_text.items():lines.append('inline constexpr const char* '+name+'[]={'+','.join(json.dumps(v) for v in values)+'};')
 lines+=['inline constexpr unsigned UiDriverStats[27][7]={'+','.join('{'+','.join(str(v) for v in row)+'}' for row in driver_stats)+'};','inline constexpr unsigned UiVehicleStats[3][12][7]={'+','.join('{'+','.join('{'+','.join(str(v) for v in row)+'}' for row in group)+'}' for group in vehicle_stats)+'};']
 lines+=['struct UiControlPose {const char* control;const char* variant;float x,y,z,sx,sy,alpha;};','inline constexpr UiControlPose UiControlPoses[]={']
 for control,variants in ctr.items():
  for name,values in variants.items():lines.append('{'+','.join([json.dumps(control),json.dumps(name),*(f'{float(x):.8f}f' for x in values+[alphas[control][name]/255])])+'},')
 lines+=['};','struct UiPanelInfo {const char* key;float x,y,w,h;};','inline constexpr UiPanelInfo UiPanels[]={']
 for key,(x,y,r,b) in panel_records.items():lines.append('{'+','.join([json.dumps(key),*(f'{float(v):.8f}f' for v in [x,y,r-x,b-y])])+'},')
 lines+=['};','struct UiPaneInfo {const char* panel;const char* name;float x,y,w,h,fontW,fontH;unsigned textAlign,textPosition,font,textStyle;float alpha,z;unsigned vertexColors[4];bool visible;};','inline constexpr UiPaneInfo UiPanes[]={']
 for panel,panes in pane_records.items():
  for name,p in panes.items():
   x,y,r,b=p['rect'];fw,fh=p.get('fontSize',[0,0]);vertices=p.get('vertexColors',[[255]*4]*4);packed=['0x'+''.join(f'{c:02x}' for c in v)+'u' for v in vertices]
   lines.append('{'+','.join([json.dumps(panel),json.dumps(name),*(f'{float(v):.8f}f' for v in [x,y,r-x,b-y,fw,fh]),str(p.get('textAlign',1)),str(p.get('textPosition',4)),str(p.get('font',0)),str(p.get('textStyle',0)),f'{float(p["alpha"]):.8f}f',f'{float(p["z"]):.8f}f','{'+','.join(packed)+'}',str(bool(p['visible'])).lower()])+'},')
 lines+=['};','struct UiPaneNode {const char* panel;const char* name;const char* parent;const char* material;float x,y,z,rx,ry,rz,sx,sy,w,h,alpha,fontW,fontH;unsigned flags,origin;short materialColors[2][4];unsigned vertexColors[4];};','inline constexpr UiPaneNode UiPaneNodes[]={']
 for panel,nodes in node_records.items():
  for node in nodes:
   colors='{'+','.join('{'+','.join(str(c) for c in color)+'}' for color in node['materialColors'])+'}'
   vertices='{'+','.join('0x'+''.join(f'{c:02x}' for c in color)+'u' for color in node['vertexColors'])+'}'
   lines.append('{'+','.join([json.dumps(panel),*(json.dumps(node[k]) for k in ['name','parent','material']),*(f'{v:.8f}f' for v in node['values']+[node['alpha']]+node['fontSize']),str(node['flags']),str(node['origin']),colors,vertices])+'},')
 lines+=['};','struct UiGlowInfo {const char* panel;const char* pane;const char* key;const char* group;unsigned variant,blend;float pivotX,pivotY;unsigned materialColors[2];};','inline constexpr UiGlowInfo UiGlows[]={']
 for glow in glow_records:
  packed=['0x'+''.join(f'{max(0,min(255,c)):02x}' for c in color)+'u' for color in glow['materialColors']]
  lines.append('{'+','.join([*(json.dumps(glow[k]) for k in ['panel','pane','key','group']),str(glow['variant']),str(glow['blend']),*(f'{v:.8f}f' for v in glow['pivot']),'{'+','.join(packed)+'}'])+'},')
 lines+=['};','struct UiFocusSprite {const char* panel;const char* pane;const char* key;unsigned part,blend,depthBit;};','inline constexpr UiFocusSprite UiFocusSprites[]={']
 for focus in focus_records:lines.append('{'+','.join([*(json.dumps(focus[k]) for k in ['panel','pane','key']),str(focus['part']),str(focus['blend']),str(focus['depthBit'])])+'},')
 lines+=['};','struct UiPanelTail {const char* panel;const char* after;unsigned part;const char* keys[2][4];};','inline constexpr UiPanelTail UiPanelTails[]={']
 for tail in panel_layer_records.values():
  keys='{'+','.join('{'+','.join(json.dumps(k) for k in tail[state])+'}' for state in ('free','select'))+'}'
  lines.append('{'+','.join([json.dumps(tail['panel']),json.dumps(tail['after']),str(tail['part']),keys])+'},')
 lines+=['};','struct UiBorderLoop {const char* key;const char* control;unsigned frames;float x,y,w,h;unsigned firstSlice,sliceCount;};','inline constexpr UiBorderLoop UiBorderLoops[]={']
 for loop in border_records:
  lines.append('{'+','.join([json.dumps(loop['key']),json.dumps(loop['control']),str(loop['frames']),*(f'{v:.8f}f' for v in loop['center']),str(loop['firstSlice']),str(loop['sliceCount'])])+'},')
 lines+=['};','struct UiBorderSlice {unsigned frame;const char* key;float x,y,w,h,u0,v0,u1,v1;};','inline constexpr UiBorderSlice UiBorderSlices[]={']
 for s in border_slices:
  lines.append('{'+','.join([str(s['frame']),json.dumps(s['key']),*(f'{v:.8f}f' for v in s['rect']+s['uv'])])+'},')
 lines+=['};','inline constexpr float UiFontCells[2][2]={'+','.join('{'+','.join(str(v)+'.f' for v in cell)+'}' for cell in font_cells)+'};','struct UiMovieInfo {const char* key;unsigned frames;float fps;};','inline constexpr UiMovieInfo UiMovies[]={']
 for movie in movie_records:
  for region in movie['regions']:lines.append('{'+','.join([json.dumps(region['key']),str(movie['frames']),f"{movie['fps']:.8f}f"] )+'},')
 lines+=['};','inline constexpr unsigned UiMovieFrameCount=16;','inline constexpr float UiSelectScale[]={'+','.join(f'{x:.8f}f' for x in scales)+'};','inline constexpr float UiMapBounds[32][4]={']
 lines += ['{'+','.join(f'{x:.8f}f' for x in m['bounds'])+'},' for m in maps]
 light_blend=pane_records['press_start']['text_light']['blend']
 if light_blend not in ([1,4,5,3],[1,4,1,3]):raise ValueError('Unsupported PressStart light blend: '+str(light_blend))
 lines+=['};','inline constexpr bool UiPressStartLightAdditive='+str(light_blend[2]==1).lower()+';','}'];(ROOT/'include/mkw/menu_assets.hpp').write_text('\n'.join(lines)+'\n')
 print(f'Cooked {len(records)} source UI sprites, {len(panel_records)} layout panels, {len(movie_records)} retail movies, 32 source minimaps; {len(evidence)} hashed sources')
 # The runtime textures are fitted to the 3DS screen and LZ11-compressed from
 # these full-resolution PNGs; that pass also rewrites the header's UVs.
 # Texture fitting runs after menu movies generate its compile-time metadata.
if __name__=='__main__':main()
