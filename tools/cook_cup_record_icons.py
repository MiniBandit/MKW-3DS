from pathlib import Path
import sys,json,hashlib
import argparse
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description='Cook original trophy and rank glyphs for the cup record footer')
parser.add_argument('--output',type=Path,default=root/'build/cup-record-icons')
work=parser.parse_args().output
sys.path[:0]=[str(root/'tools'),str(root/'vendor/gclib')]
from cook_race_flow import font_glyphs,pack_atlas,texture_file
from menu_layout import messages
font=root/'assets/race-ui/Font/tt_kart_extension_font.brfnt'
bmg=root/'assets/menu-ui/MenuSingle_U/message/Menu.bmg';messages_=messages(bmg,True)
# Original Menu.bmg d2a..d2c are gold/silver/bronze trophy glyphs;
# d2d..d34 are three/two/one stars and grades A..E.
codes=[messages_[i] for i in range(0xd2a,0xd35)]
assert len(codes)==11 and all(len(c)==1 for c in codes)
info,glyphs=font_glyphs(font,''.join(codes));atlas,rects=pack_atlas({c:g['image'] for c,g in glyphs.items()})
(work/'textures').mkdir(parents=True,exist_ok=True)
name=texture_file(atlas,'ui_cup_record_icons',work,'rgba8')
header=['// Original Menu.bmg d2a..d34 / tt_kart_extension_font.brfnt, RGBA glyphs.','#pragma once','namespace mkw {',f'inline constexpr unsigned CupRecordAtlasWidth={atlas.width},CupRecordAtlasHeight={atlas.height};','inline constexpr unsigned CupRecordIconRects[11][4]={']
header += ['{'+','.join(map(str,rects[c]))+'},' for c in codes];header+=['};','}']
(root/'include/mkw/cup_record_icons.hpp').write_text('\n'.join(header)+'\n')
(work/'record-icon-receipt.json').write_text(json.dumps({'font_sha256':hashlib.sha256(font.read_bytes()).hexdigest(),'bmg_sha256':hashlib.sha256(bmg.read_bytes()).hexdigest(),'messages':{hex(i):messages_[i] for i in range(0xd2a,0xd35)},'texture':name,'width':atlas.width,'height':atlas.height,'rects':[rects[c] for c in codes]},indent=2)+'\n')
print(name,atlas.size)
