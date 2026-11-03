#!/usr/bin/env python3
"""One coverage/filter profile for all description captions and GP record fragments."""
import argparse,hashlib,json,re
from pathlib import Path
from PIL import Image
from cook_roster import ROOT
from cook_race_hud import font_strings
from cook_race_items import cook_image_sprite
from cook_menu_ui import font_style

def footer_ink(raw):
    # Same original MenuObiInstructionText material for every caption. Baking
    # before minification avoids runtime remap versus precoloured filtering.
    image=font_style(raw,[[47,35,71,0],[255,255,255,255]],[[255]*4,[255]*4])
    scale=min(28/image.height,1000/image.width)
    w=max(1,round(image.width*scale));h=max(1,round(image.height*scale))
    image=image.resize((w,h),Image.Resampling.LANCZOS)
    padded=Image.new('RGBA',(w+4,h));padded.alpha_composite(image,(2,0))
    return padded

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/menu-footer-hw57');out=p.parse_args().output
    source=ROOT/'include/mkw/menu_assets.hpp';data=source.read_text()
    # Only original style33 descriptions. UiHelp contains dynamically chosen
    # captions too; all are emitted by the same footer pane at runtime.
    labels=set()
    for line in data.splitlines():
        if not line.startswith('{"'):continue
        try:r=json.loads('['+re.sub(r'(?<=\d)f\b','',re.sub(r'(\d+)\.f\b',r'\1.0',line[1:line.rfind('}')]))+']')
        except ValueError:continue
        if len(r)==10 and r[8]==33 and r[6]:labels.add(r[6])
    for match in re.finditer(r'inline constexpr const char\* (?:UiHelp\w*|UiVsRulesHelp)\[\]=\{([^;]+)\};',data):
        labels.update(json.loads('['+match[1]+']'))
    assert labels
    raw=font_strings(ROOT/'assets/race-ui/Font/kart_kanji_font.brfnt',sorted({t.replace('\n',' ') for t in labels}),full_ink=True)
    rows=[];files=[]
    for i,text in enumerate(sorted(labels)):
        key=f'footer_caption_{i:03d}';image=footer_ink(raw[text.replace('\n',' ')]);r=cook_image_sprite(image,'ui_'+key,out)
        rows.append('{'+','.join([json.dumps(key),json.dumps(r['path']),f"{r['width']}.f",f"{r['height']}.f",f"{r['u']:.9f}f",f"{r['v']:.9f}f",json.dumps(text),'0','33',str(r['padded'][0]*r['padded'][1]*4)])+'},')
        files.append({'text':text,'path':r['path'],'sha256':hashlib.sha256((out/r['path']).read_bytes()).hexdigest(),'size':image.size})
    (ROOT/'include/mkw/menu_footer_text.inc').write_text('// Generated original BRFNT descriptions; complete ink, shared 28px raster.\n'+'\n'.join(rows)+'\n')
    (out/'receipt.json').write_text(json.dumps({'fontSha256':hashlib.sha256((ROOT/'assets/race-ui/Font/kart_kanji_font.brfnt').read_bytes()).hexdigest(),'files':files},indent=2)+'\n')
    print('Footer captions',len(files))
if __name__=='__main__':main()
