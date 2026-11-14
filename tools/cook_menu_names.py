#!/usr/bin/env python3
"""Original BRFNT single-glyph resources for variable license names.

The glyphs are packed into 512x512 LA4 atlas pages in code order (ASCII
first, so English text needs one page). The font is 4-bit grey with 4-bit
alpha, so LA4 holds every glyph exactly at one byte a texel; each glyph used
to be its own 64x64 RGBA8 texture (16 KB). Each glyph keeps one gutter texel
on every side holding what the texture's REPEAT wrap sampled beside the old
single-glyph texture (transparent padding, or its opposite column when the
glyph filled the width), so bilinear filtering reads the same texels.
"""
import json, struct, hashlib, subprocess
from pathlib import Path
from PIL import Image
from cook_race_hud import sections, font_strings
from cook_roster import ROOT
from devkitpro import tool
import tex3ds_cache

PAGE=512

def pow2(n):return max(8,1<<(n-1).bit_length())

def cook():
 font=ROOT/'assets/race-ui/Font/kart_kanji_font.brfnt'
 data=font.read_bytes();codes=set()
 for tag,_,at in sections(data):
  if tag!=b'CMAP':continue
  lo,hi,kind=struct.unpack_from('>3H',data,at)
  if kind==0:codes.update(range(lo,hi+1))
  elif kind==1:codes.update(c for c in range(lo,hi+1) if struct.unpack_from('>H',data,at+12+2*(c-lo))[0]!=65535)
  else:codes.update(struct.unpack_from('>H',data,at+14+4*i)[0] for i in range(struct.unpack_from('>H',data,at+12)[0]))
 codes=sorted(c for c in codes if c>=32 and not 0xd800<=c<=0xdfff)
 glyphs=font_strings(font,[chr(c) for c in codes]);out=ROOT/'sd-card/3ds/mkw-3ds'
 textures=out/'textures';textures.mkdir(parents=True,exist_ok=True)
 # Single-glyph textures from earlier cooks would still be packed.
 for old in [*textures.glob('ui_name_glyph_*.t3x'),*textures.glob('ui_name_glyph_*.png'),*textures.glob('ui_name_glyphs_*')]:old.unlink()
 cells=[(c,glyphs[chr(c)].convert('RGBA')) for c in codes if glyphs[chr(c)].width>0]
 for c,img in cells:
  t=img.tobytes()
  if t[0::4]!=t[1::4] or t[0::4]!=t[2::4] or any(v%17 for v in t[0::4]+t[3::4]):raise ValueError(f'Glyph {c:04x} is not 4-bit grey; LA4 would change it')
 row=max(img.height for _,img in cells)+1;rows=(PAGE-1)//row
 # Shelf packing in code order: one transparent row between shelves, and a
 # gutter column either side of every glyph.
 places=[];page=x=shelf=0
 for c,img in cells:
  if x+img.width+2>PAGE:x=0;shelf+=1
  if shelf==rows:page+=1;shelf=0
  places.append((page,x+1,1+shelf*row));x+=img.width+2
 count=page+1;sheets=[]
 for p in range(count):
  used=max(y+row for q,_,y in places if q==p)
  sheets.append(Image.new('RGBA',(PAGE,pow2(used) if p==count-1 else PAGE)))
 for (c,img),(p,x,y) in zip(cells,places):
  w,h=img.size;padded=pow2(w),pow2(h)
  sheet=sheets[p];sheet.paste(img,(x,y))
  if w==padded[0]:
   # REPEAT wrapped a full-width glyph onto itself.
   sheet.paste(img.crop((w-1,0,w,h)),(x-1,y));sheet.paste(img.crop((0,0,1,h)),(x+w,y))
 records=[];pages=[]
 for p,sheet in enumerate(sheets):
  name=f'ui_name_glyphs_{p}';png=textures/(name+'.png');sheet.save(png)
  tex3ds_cache.run([str(tool('tex3ds')),'-f','la4','-z','lz11','-o',str(png.with_suffix('.t3x')),str(png)],check=True,stdout=subprocess.DEVNULL)
  path='textures/'+name+'.t3x'
  pages.append({'key':f'glyph_atlas_{p}','path':path,'width':sheet.width,'height':sheet.height,'sha256':hashlib.sha256((out/path).read_bytes()).hexdigest()})
 for (c,img),(p,x,y) in zip(cells,places):
  records.append({'name':f'ui_name_glyph_{c:04x}','code':c,'path':pages[p]['path'],'sha256':pages[p]['sha256'],'page':p,'x':x,'y':y,'width':img.width,'height':img.height})
 lines=[]
 for p in pages:
  lines.append('{'+','.join([json.dumps(p['key']),json.dumps(p['path']),f'{p["width"]}.f',f'{p["height"]}.f','1.f','1.f','""','0','0',str(p['width']*p['height'])])+'},')
 for r in records:
  page=pages[r['page']]
  lines.append('{'+','.join([json.dumps(f'name_glyph_{r["code"]:04x}'),json.dumps(r['path']),f'{r["width"]:.8f}f',f'{r["height"]:.8f}f',f'{r["width"]/page["width"]:.8f}f',f'{r["height"]/page["height"]:.8f}f',json.dumps(chr(r['code'])),'0','0','0'])+'},')
 (ROOT/'include/mkw/menu_name_glyphs.hpp').write_text('\n'.join(lines)+'\n')
 atlas=['#pragma once','namespace mkw {','// Each name glyph\'s atlas page and texel origin, in UiSprites order (tools/cook_menu_names.py).',
        'struct UiGlyphPlace {unsigned short page,x,y;};',
        'inline constexpr const char* UiGlyphPageKeys[]={'+','.join(json.dumps(p['key']) for p in pages)+'};',
        'inline constexpr UiGlyphPlace UiGlyphPlaces[]={'+','.join(f'{{{r["page"]},{r["x"]},{r["y"]}}}' for r in records)+'};','}']
 (ROOT/'include/mkw/menu_name_glyph_atlas.hpp').write_text('\n'.join(atlas)+'\n')
 (ROOT/'include/mkw/license_name_characters.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cstdint>\n#include <iterator>\nnamespace mkw {\ninline constexpr std::uint16_t LicenseNameCharacters[]={'+','.join(str(r['code']) for r in records)+'};\ninline bool licenseNameCharacter(std::uint16_t c){return std::binary_search(std::begin(LicenseNameCharacters),std::end(LicenseNameCharacters),c);}\n}\n')
 (out/'models/menu-name-glyphs.json').write_text(json.dumps({'source':str(font.relative_to(ROOT)),'source_sha256':hashlib.sha256(data).hexdigest(),'pages':pages,'glyphs':records},indent=2)+'\n')
 print(f'{len(records)} original glyphs cooked into {len(pages)} LA4 atlas pages')
if __name__=='__main__':cook()
