#!/usr/bin/env python3
"""Cook original Staff controls/font/photo bodies and USA REL credit timings.

The result is a resource candidate; it does not implement Staff runtime flow.
The timing data is pinned to the local relocated RMCE01 StaticR REL.
"""
import argparse,json,struct,subprocess,os,hashlib
from pathlib import Path
from PIL import Image
from cook_race_flow import (parse_layout,controller_clips,controller_variants,
 emit_layout,font_glyphs,pack_atlas,texture_file,digest,f)
from cook_race_items import decode_tpl,cook_image_sprite
from menu_layout import controller_animations,messages
from cook_model import cook
ROOT=Path(__file__).resolve().parents[1]
BASE=0x8050bf60

def timeline(data,address,count):
 rows=[]
 for i in range(count):
  values=struct.unpack_from('>8H',data,address-BASE+i*16)
  ids,start,end=values[:6],*values[6:]
  if not all(v==0 or 8900<=v<9000 for v in ids) or not 0<=start<end<=9000:
   raise ValueError(f'Wrong USA REL credit table at {address+i*16:#x}: {values}')
  if rows and start<rows[-1]['end']:raise ValueError('Unexpected credit overlap')
  rows.append(dict(ids=ids,start=start,end=end,address=hex(address+i*16)))
 return rows

def main():
 from cook_staff_camera import cook as cook_camera
 cook_camera()
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--retail-rel',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 args=p.parse_args();out=args.output.resolve();(out/'textures').mkdir(parents=True,exist_ok=True)
 source=ROOT/'assets/menu-ui/MenuOther/ending';bmg=ROOT/'assets/menu-ui/MenuOther_U/message/StaffRoll.bmg'
 text=messages(bmg);data=args.retail_rel.read_bytes()
 # The 16-byte records start four bytes AFTER the first pointer-table marker.
 # Six message IDs precede their two frame values. Validate every field.
 first=timeline(data,0x80892ef0,11);second=timeline(data,0x80892fa0,17)
 assert first[0]['ids'][:2]==(8910,8911) and first[0]['start']==420
 assert first[-1]['end']==6255 and second[0]['start']==60 and second[-1]['end']==9000
 header=['// Generated from the original Staff BMG/BRLYT/BRCTR/BRLAN and USA REL.',
 '#pragma once','#include <mkw/race_flow_layout.hpp>','namespace mkw {']
 controls=[];sources=[bmg]
 for name in ('StaffRollTopTitle','StaffRollTopName','StaffRollCenterTitle','StaffRollCenterName','StaffRollBottomTitle','StaffRollBottomName'):
  ctr=source/f'ctrl/{name}.brctr';layout_path=source/'blyt'/(controller_animations(ctr)['layout']+'.brlyt')
  layout=parse_layout(layout_path);clips=controller_clips(ctr,layout,source/'anim');variants=controller_variants(ctr)
  prefix='Flow'+name;emit_layout(prefix,layout,clips,lambda _: -1,header)
  header.append(f'inline constexpr FlowControl {prefix}Variants[]={{')
  for v in variants.values():header.append('{'+','.join(f(v[k]) for k in ('x','y','sx','sy','delay'))+'},')
  header.append('};');controls.append(dict(name=name,variants=variants));sources +=[ctr,layout_path]+[c['source'] for c in clips]
 photo_layout_path=source/'blyt/ending_photo_snap_bg.brlyt'
 photo_layout=parse_layout(photo_layout_path)
 for pane in photo_layout['panes']:
  if pane['kind']==1:pane['kind']=0 # photo itself is streamed separately
 emit_layout('FlowStaffPhotoCaption',photo_layout,[],lambda _: -1,header)
 sources.append(photo_layout_path)
 ids=sorted({i for r in first+second for i in r['ids'] if i})
 content=''.join(text[i] for i in ids)+'CONGRATULATIONS!'
 font=ROOT/'assets/race-ui/Font/kart_kanji_font.brfnt';sources.append(font)
 info,glyphs=font_glyphs(font,content.replace('\n',''));atlas,rects=pack_atlas({c:g['image'] for c,g in glyphs.items()})
 path=texture_file(atlas,'staff_font',out,'la4')
 header.append('inline constexpr FlowGlyph StaffGlyphs[]={')
 for c,g in glyphs.items():
  x,y,_,_=rects[c];header.append(f'{{char16_t({ord(c)}),{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
 header+=['};',f'inline constexpr FlowFont StaffFont{{0,{atlas.width},{atlas.height},{info["cellH"]},{info["width"]},{info["height"]},{info["linefeed"]},StaffGlyphs,{len(glyphs)}}};',
 'struct StaffMessage {unsigned id;const char* text;};','inline constexpr StaffMessage StaffMessages[]={']
 header +=['{'+str(i)+','+json.dumps(text[i],ensure_ascii=True)+'},' for i in ids]
 header+=['};','struct StaffCredit {std::array<unsigned short,6> ids;unsigned short start,end;};']
 for name,records in [('Demo',first),('Movie',second)]:
  header.append(f'inline constexpr StaffCredit Staff{name}Credits[]={{')
  header +=['{{{'+','.join(str(i) for i in row['ids'])+'}},'+str(row['start'])+','+str(row['end'])+'},' for row in records]
  header.append('};')
 photos=[]
 for variant,stem in [('normal','tt_ending_bg'),('true','tt_ending_sono2_bg')]:
  tpl=source/f'timg/{stem}.tpl';sources.append(tpl)
  image=decode_tpl(tpl).convert('RGBA').resize((400,240),Image.Resampling.LANCZOS)
  photos.append(cook_image_sprite(image,f'staff_photo_{variant}',out))
 header.append('inline constexpr const char* StaffPhotoPaths[]={'+','.join(json.dumps(x['path']) for x in photos)+'};')
 header+=['inline constexpr float StaffPhotoU=400.f/512,StaffPhotoV=240.f/256;','}']
 bodies=[]
 for sex in ('m','f'):
  original=ROOT/f'assets/menu-ui/MenuOther/ma_mii_{sex}.brres';sources.append(original)
  export=out/'staff-exports'/sex
  subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(original),str(export),'model','sel_wait','0'],check=True,stdout=subprocess.DEVNULL,env={**os.environ,'MKW_MENU_MATERIALS':'1'})
  prefix=f'staff_mii_{sex}';parts=cook(export,out,prefix,preserve_vertices=True)
  result=subprocess.run(['node',str(ROOT/'build/export_mii_photo.mjs'),str(original)],check=True,capture_output=True,text=True)
  pose=json.loads(result.stdout)
  if len(parts)!=1 or len(pose['poses'])!=2 or len(pose['colours'])!=12:raise ValueError('Unexpected photo body/head pose palette')
  bodies.append(dict(sex=sex,model=parts[0],head=pose,note='Original textured photo billboard, two authored face_1 poses and twelve colour textures; head supplied separately by native 3DS CFL.'))

 header.pop() # append independently authored full-body photo anchors
 header.append('struct StaffPhotoBody {const char* model;std::array<float,12> head;std::array<unsigned,12> colours;};')
 header.append('inline constexpr StaffPhotoBody StaffPhotoBodies[]={')
 for body in bodies:
  header.append('{'+json.dumps('models/'+body['model']['name']+'.gmdl')+',{{'+','.join(f(v) for v in body['head']['poses'][0])+'}},{{'+','.join(str(c['texture']) for c in body['head']['colours'])+'}}},')
 header+=['};','}']
 (ROOT/'include/mkw/staff_ui_data.hpp').write_text('\n'.join(header)+'\n')

 receipt=dict(version=1,relSha256=digest(args.retail_rel),base=hex(BASE),demo=first,movie=second,
  controls=controls,font=path,photos=photos,bodies=bodies,sources={str(p.relative_to(ROOT)):digest(p) for p in sorted(set(sources))},
  status='source resources only; runtime/timing/photo placement and target validation still required')
 (out/'models/staff-ui.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print('Prepared 11 demo and 17 movie credit records, 6 original controllers, original glyphs, both photos and both photo Mii bodies.',flush=True)
if __name__=='__main__':main()
