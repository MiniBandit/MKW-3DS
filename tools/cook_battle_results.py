#!/usr/bin/env python3
"""Stage native BattleWin and Battle team-column/win-count result controls."""
import argparse,json,hashlib
from pathlib import Path
from cook_race_flow import ROOT,UI,parse_layout,controller_clips,controller_variants,font_glyphs,pack_atlas,texture_file,emit_layout,f,brlan,TRACK_KINDS
from menu_layout import messages
from cook_race_items import decode_tpl

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/battle-results-assets');out=p.parse_args().output.resolve();out.mkdir(parents=True,exist_ok=True);(out/'textures').mkdir(exist_ok=True)
    game=UI/'Race/game_image';result=UI/'Race/result';sources=[]
    def load(folder,control):
        ctr=folder/f'ctrl/{control}.brctr';from menu_layout import controller_animations
        path=folder/('blyt/'+controller_animations(ctr)['layout']+'.brlyt');layout=parse_layout(path)
        clips=controller_clips(ctr,layout,folder/'anim');sources.extend([ctr,path]+[c['source'] for c in clips]);return layout,clips,controller_variants(ctr)
    title,clips,variants=load(game,'BattleWin');point,pointclips,pointvars=load(result,'ResultTeamPoint')
    _,_,columns=load(result,'ResultBTTeamNULL')
    marks=[load(UI/'Race/control',name) for name in ['BattleSetPointRed','BattleSetPointBlue']]
    msg=UI/'Race_U/message/Race.bmg';m=messages(msg);sources.append(msg);ids=[0x4be,0x4bf,0x4c2,0x528]
    font=UI/'Font/kart_kanji_font.brfnt';sources.append(font)
    info,glyphs=font_glyphs(font,''.join(m[i] for i in ids)+'0123456789 ')
    atlas,rects=pack_atlas({c:g['image'] for c,g in glyphs.items()});texture=texture_file(atlas,'battle_result_font',out,'la4');textures=[texture]
    header=['#pragma once','#include <mkw/race_flow_layout.hpp>','namespace mkw {',
      '// Native BattleWin, ResultBTTeamNULL and ResultTeamPoint. Separate mode bank.',
      'inline constexpr const char* BattleResultTexture='+json.dumps(texture)+';']
    # Native Hide controls the parent count_down_null even though BRCTR's
    # fade group lists only its text children. Keep that authored visibility.
    for clip in clips:
        _,_,raw_tracks=brlan(clip['source'])
        for track in raw_tracks:
            if track['element']=='count_down_null' and track['kind']=='RLVI':
                target=next(i for i,p in enumerate(title['panes']) if p['name']==track['element'])
                clip['tracks'].append({'target':target,'kind':TRACK_KINDS['RLVI'],'index':track['index'],'step':True,'keys':track['keys']})
    emit_layout('FlowBattleWin',title,clips,lambda _: -1,header)
    emit_layout('FlowBattleWinPoint',point,pointclips,lambda _: -1,header)
    def control(v):return '{'+','.join(f(v[k]) for k in ['x','y','sx','sy','delay'])+'}'
    header+=['inline constexpr FlowControl FlowBattleWinControl='+control(variants['BattleWin'])+';',
      'inline constexpr FlowControl FlowBattleColumns[2]={'+','.join(control(columns[f'ResultVSTeamNULL{i}']) for i in range(2))+'};',
      'inline constexpr FlowControl FlowBattleWinPoints[2]={'+','.join(control(pointvars[k]) for k in ['red','blue'])+'};',
      'inline constexpr const char* FlowBattleWinText[3]={'+','.join(json.dumps(m[i]) for i in ids[:3])+'};',
      'inline constexpr const char* FlowBattleWinsUnit='+json.dumps(m[0x528])+';',
      'inline constexpr FlowGlyph FlowBattleWinGlyphs[]={']
    for c in sorted(glyphs):
        x,y,_,_=rects[c];g=glyphs[c];header.append(f'{{char16_t({ord(c)}),{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
    header+=['};',f'inline constexpr FlowFont FlowBattleWinFont{{0,{atlas.width},{atlas.height},{info["cellH"]},{info["width"]},{info["height"]},{info["linefeed"]},FlowBattleWinGlyphs,{len(glyphs)}}};','}','']
    # Native Number.bmg messages1900/1901 select coloured font4/5. The
    # cleared ResultTeamPoint has only its point pane, no pts/shadow binding.
    header=header[:-2]
    for team in ['red','blue']:
        font=UI/f'Font/mario_font_number_{team}.brfnt';sources.append(font)
        ni,ng=font_glyphs(font,'0123456789 ');na,nr=pack_atlas({c:g['image'] for c,g in ng.items()})
        textures.append(texture_file(na,'battle_result_number_'+team,out,'rgba8'))
        symbol='FlowBattleNumber'+team.title()
        header.append('inline constexpr FlowGlyph '+symbol+'Glyphs[]={')
        for c in sorted(ng):
            x,y,_,_=nr[c];g=ng[c];header.append(f'{{char16_t({ord(c)}),{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
        header += ['};',f'inline constexpr FlowFont {symbol}{{{len(textures)-1},{na.width},{na.height},{ni["cellH"]},{ni["width"]},{ni["height"]},{ni["linefeed"]},{symbol}Glyphs,{len(ng)}}};']
    for tex in ['tt_glass_sphere.tpl','tt_star_01.tpl']:
        path=UI/'Race/control/timg'/tex;sources.append(path);textures.append(texture_file(decode_tpl(path),'battle_result_'+path.stem,out,'rgba8'))
    for team,(layout,clips,variants) in zip(['Red','Blue'],marks):
        # tex3ds stores the 40x40 sphere in a 64x64 allocation. Unlike
        # the 32x32 star, its authored 0..1 UVs cannot sample that allocation
        # directly: doing so shifts/shrinks the bubble beneath its light.
        for pane in layout['panes']:
            if pane['kind']!=1 or pane['material']<0:continue
            tex=layout['materials'][pane['material']]['texture']
            if tex!='tt_glass_sphere.tpl':continue
            width,height=decode_tpl(UI/'Race/control/timg'/tex).size
            scale=(width/max(8,1<<(width-1).bit_length()),height/max(8,1<<(height-1).bit_length()))
            pane['uv']=[value*scale[i%2] for i,value in enumerate(pane['uv'])]
        emit_layout('FlowBattleMark'+team,layout,clips,lambda name:3 if name=='tt_glass_sphere.tpl' else 4,header)
    header += ['inline constexpr const char* BattleResultTextures[]={'+','.join(json.dumps(t) for t in textures)+'};','}','']
    output=ROOT/'include/mkw/battle_result_assets.hpp';output.write_text('\n'.join(header))
    path=out/texture
    receipt={'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'files':[{'path':t,'bytes':(out/t).stat().st_size,'sha256':hashlib.sha256((out/t).read_bytes()).hexdigest()} for t in textures],
      'messages':{hex(i):m[i] for i in ids},'columns':columns,'headerSha256':hashlib.sha256(output.read_bytes()).hexdigest()}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('Staged original Battle result font/controls:',path)
if __name__=='__main__':main()
