"""Add the retail Present Mirror emblem to an existing menu cook.
The full menu cooker contains the same source; this avoids recooking movies.
"""
import json,hashlib,re
from cook_roster import ROOT
from cook_race_items import cook_image_sprite,decode_tpl

def cook():
 p=ROOT/'assets/menu-ui/Present/control/timg/tt_cup_icon_mirror_00.tpl'
 r=cook_image_sprite(decode_tpl(p),'ui_cup_mirror',ROOT/'sd-card/3ds/mkw-3ds')
 row='{'+','.join([json.dumps('cup_mirror'),json.dumps(r['path']),*(f'{float(r[k]):.8f}f' for k in ['width','height','u','v']),'""','0','0',str(r['padded'][0]*r['padded'][1]*4)])+'},'
 header=ROOT/'include/mkw/menu_assets.hpp';s=header.read_text();s=re.sub(r'^\{"cup_mirror",.*\n','',s,flags=re.M);s=s.replace('#include <mkw/menu_name_glyphs.hpp>',row+'\n#include <mkw/menu_name_glyphs.hpp>');header.write_text(s)
 manifest=ROOT/'sd-card/3ds/mkw-3ds/models/menu-ui.json';d=json.loads(manifest.read_text());d['sprites']['cup_mirror']=r;d['sourceHashes'][str(p.relative_to(ROOT))]=hashlib.sha256(p.read_bytes()).hexdigest();manifest.write_text(json.dumps(d,indent=2)+'\n')
 print('Retail Mirror emblem',r['width'],r['height'])
if __name__=='__main__':cook()
