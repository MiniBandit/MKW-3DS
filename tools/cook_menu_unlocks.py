"""Cook the retail locked-choice emblem without synthesizing an icon."""
import json,hashlib
from cook_race_items import cook_image_sprite,decode_tpl
from cook_roster import ROOT

def cook():
 source=ROOT/'assets/menu-ui/MenuSingle/button/timg/tt_hatena_64x64.tpl'
 r=cook_image_sprite(decode_tpl(source),'ui_locked_choice',ROOT/'sd-card/3ds/mkw-3ds')
 row='{'+','.join([json.dumps('locked_choice'),json.dumps(r['path']),*(f'{r[k]:.8f}f' for k in ('width','height','u','v')),'""','0','0',str(r['padded'][0]*r['padded'][1]*4)])+'},\n'
 header=ROOT/'include/mkw/menu_assets.hpp';text=header.read_text()
 if '"locked_choice",' not in text:header.write_text(text.replace('#include <mkw/menu_name_glyphs.hpp>',row+'#include <mkw/menu_name_glyphs.hpp>'))
 manifest=ROOT/'sd-card/3ds/mkw-3ds/models/menu-ui.json';data=json.loads(manifest.read_text())
 r['source']=str(source.relative_to(ROOT));r['source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
 data['sprites']['locked_choice']=r;data['sourceHashes'][r['source']]=r['source_sha256']
 manifest.write_text(json.dumps(data,indent=2)+'\n')
if __name__=='__main__':cook()
