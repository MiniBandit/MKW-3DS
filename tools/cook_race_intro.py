#!/usr/bin/env python3
"""Cook original opening-pan labels/font/icons into a private build overlay.
No source ROM extraction, shared SD writes or package creation.
"""
import json,re,struct
from pathlib import Path
from menu_layout import messages,controller_animations
from cook_race_flow import ROOT,UI,digest,parse_layout,controller_variants,font_glyphs,pack_atlas,texture_file,emit_layout,f
from cook_race_items import decode_tpl

def templates(path):
    d=path.read_bytes();at=32;blocks={}
    for _ in range(struct.unpack_from('>I',d,12)[0]):
        blocks[d[at:at+4]]=at;at+=struct.unpack_from('>I',d,at+4)[0]
    info=blocks[b'INF1'];n,stride=struct.unpack_from('>HH',d,info+8);out={}
    for i in range(n):
        mid=struct.unpack_from('>I',d,blocks[b'MID1']+16+i*4)[0]
        if not 0x581<=mid<=0x58c:continue
        assert d[info+16+i*stride+4]==1,'Opening labels must use BMG font 1'
        p=blocks[b'DAT1']+8+struct.unpack_from('>I',d,info+16+i*stride)[0];s=''
        while d[p:p+2]!=b'\0\0':
            if d[p:p+2]==b'\0\x1a':
                size=d[p+2];command=d[p:p+size]
                if command[:6]==bytes.fromhex('001a08020011'):s+='%s'
                elif command[:6]==bytes.fromhex('001a0a020010'):s+='%u'
                else:raise ValueError('Unknown original intro message tag '+command.hex())
                p+=size
            else:s+=d[p:p+2].decode('utf-16be');p+=2
        out[mid]=s
    return out

def main():
    out=ROOT/'build/race-intro';(out/'textures').mkdir(parents=True,exist_ok=True)
    race_path=UI/'Race_U/message/Race.bmg';common_path=UI/'Race_U/message/Common.bmg'
    race=templates(race_path);common=messages(common_path)
    # All catalog entries, including the ten Battle arenas, feed the shared drawIntro bounds.
    course_names=re.findall(r'\{"([^"]+)",Kinoko::Course::', (ROOT/'include/mkw/course_catalog.hpp').read_text())
    ids=[]
    for name in course_names:
        matches=[i for i,t in common.items() if t==name]
        if not matches:raise ValueError('Course name missing from original BMG: '+name)
        ids.append(matches)
    cup_names=[common[0x23f0+i] for i in range(8)]
    chars=''.join(course_names+cup_names+list(race.values()))+'0123456789'
    font=UI/'Font/kart_kanji_font.brfnt';info,glyphs=font_glyphs(font,chars.replace('%',''),preserve_ink='1')
    atlas,rects=pack_atlas({c:g['image'] for c,g in glyphs.items()})
    paths=[texture_file(atlas,'intro_font',out,'la4')]
    sources=[race_path,common_path,font]
    icon_names=['tt_cup_icon_'+n+'_00' for n in ['kinoko','flower','star','oukan','koura','banana','konoha','thunder']]+['st_battle_icon_flag','st_battle_icon_balloon','st_battle_icon_coin']
    for i,name in enumerate(icon_names):
        p=UI/'Race/demo/timg'/(name+'.tpl');sources.append(p)
        paths.append(texture_file(decode_tpl(p),'intro_icon_'+str(i),out,'rgba8'))
    h=['// Generated from original demo BRLYT/BRCTR, BMG font-1 and original icon TPLs.', '#pragma once','#include <mkw/race_flow_layout.hpp>','namespace mkw {']
    h.append('inline constexpr const char* IntroTextures[]={'+','.join(json.dumps(p) for p in paths)+'};')
    h.append('inline constexpr FlowGlyph IntroGlyphs[]={')
    for c in sorted(glyphs):
        x,y,w,_=rects[c];g=glyphs[c];escaped='\\'+c if c in "'\\" else c
        h.append(f"{{u'{escaped}',{x},{y},{g['width']},{g['left']},{g['advance']}}},")
    h+=['};',f'inline constexpr FlowFont IntroFont{{0,{atlas.width},{atlas.height},{info["cellH"]},{info["width"]},{info["height"]},{info["linefeed"]},IntroGlyphs,{len(glyphs)}}};']
    h.append('inline constexpr const char* IntroCourseNames[]={'+','.join(json.dumps(n) for n in course_names)+'};')
    h.append('inline constexpr const char* IntroCupNames[]={'+','.join(json.dumps(n) for n in cup_names)+'};')
    h.append('inline constexpr const char* IntroEngineNames[]={'+','.join(json.dumps(race[0x589+i]) for i in range(4))+'};')
    h.append('inline constexpr const char* IntroGPTemplates[]={'+','.join(json.dumps(race[0x582+i]) for i in range(4))+'};')
    for key,mid in [('VS',0x586),('TeamVS',0x581),('Balloon',0x587),('Coin',0x588)]:h.append('inline constexpr const char* Intro'+key+'Template='+json.dumps(race[mid])+';')
    controls={};layouts={}
    for name,prefix in [('course_name','IntroCourse'),('cup_name','IntroCup')]:
        p=UI/'Race/demo/blyt'/(name+'.brlyt');c=UI/'Race/demo/ctrl'/(name+'.brctr');sources +=[p,c]
        if controller_animations(c)['groups']:raise ValueError('New intro animations need runtime binding')
        layout=parse_layout(p);layouts[name]=layout
        # The 3DS FlowDrawer maps both axes by the same 240/456 scale.
        # Wii's 608/832 compensation is paired with an anamorphic widescreen
        # viewport; baking it here would squeeze fonts/icons a second time.
        emit_layout(prefix,layout,[],lambda n:1,h)
        v=controller_variants(c)[name];controls[name]=v
        h.append(f'inline constexpr FlowControl {prefix}Control{{{f(v["x"])},{f(v["y"])},{f(v["sx"])},{f(v["sy"])},{f(v["delay"])}}};')
    h.append('}')
    (ROOT/'include/mkw/race_intro_layout.hpp').write_text('\n'.join(h)+'\n')
    receipt={'version':1,'sourceCall':'RMCU 80833778: course/cup labels and message arguments','sources':{str(p.relative_to(ROOT)):digest(p) for p in sources},'templates':race,'courseMessageIds':ids,'controls':controls,'layouts':layouts,'font':info,'textures':{p:{'bytes':(out/p).stat().st_size,'sha256':digest(out/p)} for p in paths},'runtime':'Only font and current cup/mode icon resident during preview; target appearance not tested'}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'glyphs':len(glyphs),'fontAtlas':[atlas.width,atlas.height],'icons':len(icon_names),'courses':len(course_names),'packaged':False}))
if __name__=='__main__':main()
