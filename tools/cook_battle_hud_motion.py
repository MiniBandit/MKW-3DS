#!/usr/bin/env python3
"""Stage original Battle popup signs, font metrics and authored HUD motion."""
import argparse,hashlib,json
from pathlib import Path
from cook_roster import ROOT
from cook_race_hud import animation,sample,font_strings,variant
from cook_race_items import cook_image_sprite
from menu_layout import pane_nodes,controller_animations

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/battle-hud-motion')
    out=parser.parse_args().output.resolve();out.mkdir(parents=True,exist_ok=True)
    root=ROOT/'assets/race-ui/Race/game_image';sources={};files=[]
    def source(path):sources[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest();return path
    def value(tracks,name,kind,target,default,frame):
        return sample(tracks[name,kind,target],frame) if (name,kind,target) in tracks else default
    def rgba(values):return '0x'+''.join(f'{max(0,min(255,round(v))):02x}' for v in values)+'u'
    lines=['#pragma once','#include <mkw/battle_hud_assets.hpp>','namespace mkw {',
      '// Generated original CWDH metrics and BRCTR/BRLAN transitions.',
      'struct BattleGlyphMetric {float advance,origin;};',
      'struct BattleTeamFrame {float sx,sy,angle;unsigned black,white;};',
      'struct BattlePopupFrame {float y,sx,sy,alpha;};']
    signs=[];metrics=[]
    for team in ('red','blue'):
        font=source(ROOT/f'assets/race-ui/Font/mario_font_number_{team}.brfnt');mm={}
        glyphs=font_strings(font,list('0123456789+-'),metrics=mm);metrics.append(mm);row=[]
        for char,name in [('+','plus'),('-','minus')]:
            r=cook_image_sprite(glyphs[char],f'battle_hud_{team}_{name}',out);row.append(r)
            p=out/r['path'];files.append({'path':r['path'],'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size})
        signs.append(row)
    lines+=['inline constexpr BattleGlyphMetric BattleGlyphMetrics[2][12]={'+','.join('{'+','.join('{'+f'{row[c]["advance"]}.f,{row[c]["origin"]}.f'+'}' for c in '0123456789+-')+'}' for row in metrics)+'};']
    lines+=['inline constexpr BattleHudArt BattleSignArt[2][2]={'+','.join('{'+','.join('{'+json.dumps(r['path'])+','+','.join(f'{r[k]:.9f}f' for k in ['width','height','u','v'])+'}' for r in row)+'}' for row in signs)+'};']
    panes={p['name']:p for p in pane_nodes(source(root/'blyt/battle_team_score_red_vs_blue.brlyt'))}
    names=['off','on','vitory','stop'];durations=[];frames=[]
    for anim in names:
        duration,tracks=animation(source(root/f'anim/battle_team_score_red_vs_blue_{anim}.brlan'));durations.append(duration);teams=[]
        for team in ('red','blue'):
            digits=[]
            for suffix in 'abc':
                pane=panes[f'{team}_{suffix}'];name=pane['material'];values=pane['values'];digit=[]
                for frame in range(47):
                    f=min(frame,duration)
                    sx=value(tracks,pane['name'],'RLPA',6,values[6],f)/values[6]
                    sy=value(tracks,pane['name'],'RLPA',7,values[7],f)/values[7]
                    angle=-value(tracks,pane['name'],'RLPA',5,values[5],f)*3.141592653589793/180
                    colors=[[value(tracks,name,'RLMC',4+4*j+c,pane['materialColors'][j][c],f) for c in range(4)] for j in range(2)]
                    digit.append('{'+f'{sx:.9f}f,{sy:.9f}f,{angle:.9f}f,'+','.join(rgba(x) for x in colors)+'}')
                digits.append('{'+','.join(digit)+'}')
            teams.append('{'+','.join(digits)+'}')
        frames.append('{'+','.join(teams)+'}')
    lines+=['inline constexpr unsigned BattleTeamDurations[4]={'+','.join(map(str,durations))+'};',
      'inline constexpr BattleTeamFrame BattleTeamFrames[4][2][3][47]={'+',\n'.join(frames)+'};']
    duration,tracks=animation(source(root/'anim/battle_point_set_position_point_on.brlan'));popup=[]
    for frame in range(duration+1):
        v=[value(tracks,'set_p','RLPA',1,0,frame),value(tracks,'set_p','RLPA',6,1,frame),value(tracks,'set_p','RLPA',7,1,frame),value(tracks,'set_p','RLVC',16,255,frame)/255]
        popup.append('{'+','.join(f'{x:.9f}f' for x in v)+'}')
    lines+=['inline constexpr BattlePopupFrame BattlePopupFrames[]={'+','.join(popup)+'};']
    ctrl=source(root/'ctrl/BattleAddPoint.brctr');x,y,_,sx,sy=variant(ctrl,'BattleAddPoint_1_0')
    lines+=['inline constexpr float BattlePopupOriginX='+f'{200+x*240/456:.9f}f;',
      'inline constexpr float BattlePopupOriginY='+f'{120-y*240/456:.9f}f;',
      'inline constexpr float BattlePopupScale='+f'{sx*240/456:.9f}f;',
      'inline constexpr float BattlePopupFontHeight=63.799999237f;', '}', '']
    chains={}
    for name in ('BattleAddPoint','battle_total_point'):
        ctrl=source(root/f'ctrl/{name}.brctr');chains[name]=controller_animations(ctrl)
    for path,sha in sources.items():assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha
    header=ROOT/'include/mkw/battle_hud_motion_assets.hpp';header.write_text('\n'.join(lines))
    (out/'receipt.json').write_text(json.dumps({'sources':sources,'files':files,'fontMetrics':metrics,'controllerGroups':chains,'teamDurations':durations,'popupDuration':duration,'generatedHeaderSha256':hashlib.sha256(header.read_bytes()).hexdigest()},indent=2)+'\n')
    print('Staged Battle HUD signs/metrics/motion:',len(files),'new textures')
if __name__=='__main__':main()
