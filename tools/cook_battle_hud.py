#!/usr/bin/env python3
"""Stage original Battle HUD art and the ten authored arena minimaps."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
from cook_roster import ROOT
from cook_race_hud import layout,variant,font_strings,sections
from cook_race_items import cook_image_sprite,decode_tpl
from cook_race_map import courses
from devkitpro import tool

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/battle-hud-assets')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source_hashes={};files=[]
    def source(p):
        key=str(p.relative_to(ROOT));sha=hashlib.sha256(p.read_bytes()).hexdigest()
        if key in source_hashes and source_hashes[key]!=sha:raise ValueError('Source changed: '+key)
        source_hashes[key]=sha
    def record(p):files.append({'path':str(p.relative_to(out)),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    def sprite(p,name):
        source(p);r=cook_image_sprite(decode_tpl(p),name,out);record(out/r['path']);return r
    def art(r):return '{'+','.join([json.dumps(r['path'])]+[f'{r[k]:.9f}f' for k in ('width','height','u','v')])+'}'
    ui=ROOT/'assets/race-ui/Race/game_image'
    digits=[[sprite(ui/f'timg/tt_mario_battle_font_{team}_{d:02d}.tpl',f'battle_hud_{team}_{d}') for d in range(10)] for team in ('red','blue')]
    dash=sprite(ui/'timg/tt_mario_battle_font_red_haifun.tpl','battle_hud_dash')
    personal=[]
    for team in ('red','blue'):
        p=ROOT/f'assets/race-ui/Font/mario_font_number_{team}.brfnt';source(p)
        glyphs=font_strings(p,list(map(str,range(10))))
        personal.append([])
        for d in range(10):
            r=cook_image_sprite(glyphs[str(d)],f'battle_hud_personal_{team}_{d}',out)
            record(out/r['path']);personal[-1].append(r)
    rects={}
    def group(ctrl,name,side):
        p=ui/f'ctrl/{ctrl}.brctr';source(p);x,y,_,sx,sy=variant(p,name);unit=240/456
        gx=(x+416)*unit if side=='left' else 400-(416-x)*unit
        return gx,(228-y)*unit,sx*unit,sy*unit
    def rect(name,pose,pane,parent):
        gx,gy,sx,sy=pose;w=pane['w']*pane['sx']*sx;h=pane['h']*pane['sy']*sy
        rects[name]=[gx+(parent['x']+pane['x'])*sx-w/2,gy-(parent['y']+pane['y'])*sy-h/2,w,h]
    p=ui/'blyt/battle_team_score_red_vs_blue.brlyt';source(p);panes=layout(p)
    for name in ('red_a','red_b','red_c','hifun','blue_a','blue_b','blue_c'):
        rect(name,group('battle_total_point','total_point_1','right'),panes[name],panes['race_null'])
    p=ui/'blyt/coin.brlyt';source(p);panes=layout(p)
    rect('personal',group('point','CtrlRacePoint_1_0','left'),panes['mario_text_red'],panes['race_null'])
    lines=['#pragma once','#include <array>','namespace mkw {',
           '// Generated from original BRCTR/BRLYT, TPL and BRFNT by cook_battle_hud.py.',
           'struct BattleHudArt {const char* path;float w,h,u,v;};',
           'struct BattleHudRect {float x,y,w,h;};',
           'inline constexpr BattleHudArt BattleHudDigits[2][10]={'+','.join('{'+','.join(art(r) for r in row)+'}' for row in digits)+'};',
           'inline constexpr BattleHudArt BattleHudPersonalDigits[2][10]={'+','.join('{'+','.join(art(r) for r in row)+'}' for row in personal)+'};',
           'inline constexpr BattleHudArt BattleHudDash='+art(dash)+';']
    for name,values in rects.items():lines.append('inline constexpr BattleHudRect BattleHud_'+name+'{'+','.join(f'{v:.9f}f' for v in values)+'};')
    # FINF width/height are 48/64. The coin text pane uses that same font size,
    # scaled by the pane (.55), controller (2) and 3DS canvas conversion.
    lines+=['inline constexpr float BattleHudPersonalScale=.55f*2.f*240.f/456.f;','}','']
    (ROOT/'include/mkw/battle_hud_assets.hpp').write_text('\n'.join(lines))
    maps=[]
    for index,course in enumerate(courses()[32:],32):
        p=ROOT/'assets/courses'/course['key']/'map_model.brres';source(p)
        work=out/'exports'/course['key'];work.mkdir(parents=True,exist_ok=True)
        receipt=json.loads(subprocess.check_output(['node',str(ROOT/'build/export_brres.mjs'),str(p),str(work)],text=True))
        nodes={n['name']:n['t'] for m in receipt['nodes'] for n in m['nodes']}
        ld,ru=nodes['posLD'],nodes['posRU'];x0,x1=sorted([ld[0],ru[0]]);z0,z1=sorted([ld[2],ru[2]])
        if x1<=x0 or z1<=z0:raise ValueError('Invalid minimap camera: '+course['key'])
        models=json.loads((work/'model.json').read_text())['models']
        image=Image.new('RGBA',(1024,1024));draw=ImageDraw.Draw(image)
        for model in models:
            for shape in model['shapes']:
                vv=shape['vertices']
                for k in range(0,len(vv),3):
                    tri=vv[k:k+3]
                    points=[((v[0]-x0)/(x1-x0)*1023,(v[2]-z0)/(z1-z0)*1023) for v in tri]
                    color=tuple(round(sum(v[10+c] for v in tri)/3) for c in range(4))
                    draw.polygon(points,fill=color)
        for suffix,size in [('',512),('_overview',256)]:
            target=out/f'textures/battle_map_{index}{suffix}.png';target.parent.mkdir(parents=True,exist_ok=True)
            image.resize((size,size),Image.Resampling.LANCZOS).save(target)
            subprocess.run([str(tool('tex3ds')),'-f','rgba4','-z','none','-o',str(target.with_suffix('.t3x')),str(target)],check=True,stdout=subprocess.DEVNULL)
            record(target.with_suffix('.t3x'))
        maps.append({'index':index,'course':course['name'],'bounds':[x0,z0,x1,z1],'nodes':nodes})
    lines=['#pragma once','#include <mkw/race_map_assets.hpp>','namespace mkw {',
           '// Original arena map_model mesh and posLD/posRU world camera.',
           'inline constexpr std::array<RaceMapCourse,10> BattleMapCourses{{']
    for m in maps:
        i=m['index'];frame=','.join(f'{v:.9f}f' for v in m['bounds'])
        lines.append(f'{{"textures/battle_map_{i}.t3x","textures/battle_map_{i}_overview.t3x",{frame},512,512,256,256,64.f,{{0,0}},{{0,0}}}}, // {m["course"]}')
    lines+=['}};','inline const RaceMapCourse* raceMapCourse(unsigned index){',
            'return index<RaceMapCourses.size()?&RaceMapCourses[index]:index-32<BattleMapCourses.size()?&BattleMapCourses[index-32]:nullptr;','}','}','']
    (ROOT/'include/mkw/battle_map_assets.hpp').write_text('\n'.join(lines))
    for p,sha in source_hashes.items():
        if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=sha:raise ValueError('Original resource changed: '+p)
    (out/'receipt.json').write_text(json.dumps({'sources':source_hashes,'rectangles':rects,'maps':maps,'mapFormat':'RGBA4','mapResidentBytes':512*512*2+256*256*2,'files':files},indent=2)+'\n')
    print('Staged original Battle HUD and',len(maps),'arena minimaps:',len(files),'files',flush=True)
if __name__=='__main__':main()
