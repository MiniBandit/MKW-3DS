#!/usr/bin/env python3
"""Original BRFNT single-glyph resources for variable license names."""
import json, struct, hashlib, concurrent.futures
from pathlib import Path
from cook_race_hud import sections, font_strings
from cook_race_items import cook_image_sprite
from cook_roster import ROOT

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
 def one(c):
  img=glyphs[chr(c)]
  if img.width<=0:return None
  r=cook_image_sprite(img,f'ui_name_glyph_{c:04x}',out);r['code']=c
  r['sha256']=hashlib.sha256((out/r['path']).read_bytes()).hexdigest();return r
 with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:records=[r for r in pool.map(one,codes) if r]
 lines=[]
 for r in records:
  lines.append('{'+','.join([json.dumps(f'name_glyph_{r["code"]:04x}'),json.dumps(r['path']),*(f'{r[k]:.8f}f' for k in ('width','height','u','v')),json.dumps(chr(r['code'])), '0','0',str(r['padded'][0]*r['padded'][1]*4)])+'},')
 (ROOT/'include/mkw/menu_name_glyphs.hpp').write_text('\n'.join(lines)+'\n')
 (ROOT/'include/mkw/license_name_characters.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cstdint>\n#include <iterator>\nnamespace mkw {\ninline constexpr std::uint16_t LicenseNameCharacters[]={'+','.join(str(r['code']) for r in records)+'};\ninline bool licenseNameCharacter(std::uint16_t c){return std::binary_search(std::begin(LicenseNameCharacters),std::end(LicenseNameCharacters),c);}\n}\n')
 (out/'models/menu-name-glyphs.json').write_text(json.dumps({'source':str(font.relative_to(ROOT)),'source_sha256':hashlib.sha256(data).hexdigest(),'glyphs':records},indent=2)+'\n')
 print(f'{len(records)} original glyphs cooked; loaded individually by visible names')
if __name__=='__main__':cook()
