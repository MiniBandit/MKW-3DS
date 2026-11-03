#!/usr/bin/env python3
"""A small original-font score atlas for the 3DS Battle standings column.
Keeps the existing timer/portrait atlas and race maps byte-for-byte unchanged.
"""
from pathlib import Path
import argparse,hashlib,json,sys
import numpy as np
from PIL import Image,ImageFilter
from cook_race_flow import ROOT,UI,font_glyphs,texture_file

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/battle-bottom-scores');a=p.parse_args();out=a.output;(out/'textures').mkdir(parents=True,exist_ok=True)
 font=UI/'Font/kart_kanji_font.brfnt';_,glyphs=font_glyphs(font,'0123456789')
 atlas=Image.new('RGBA',(128,32));rects=[]
 for d in range(10):
  image=glyphs[str(d)]['image'].convert('RGBA');image=image.crop(image.getchannel('A').getbbox()).resize((7,13),Image.Resampling.LANCZOS)
  # The BRFNT's intensity selects black/white, alpha retains coverage.
  pixels=np.asarray(image).copy();pixels[...,:3]=pixels[...,:1];image=Image.fromarray(pixels,'RGBA')
  cell=Image.new('RGBA',(9,15));mask=Image.new('L',cell.size);mask.paste(image.getchannel('A'),(1,1));outline=Image.new('RGBA',cell.size,(0,0,0,255));outline.putalpha(mask.filter(ImageFilter.MaxFilter(3)));cell.alpha_composite(outline);cell.alpha_composite(image,(1,1))
  x=1+d*11;atlas.alpha_composite(cell,(x,1));rects.append([x,1,9,15])
 atlas.save(out/'preview.png');texture=texture_file(atlas,'battle_bottom_scores',out,'rgba8');f=out/texture
 header=ROOT/'include/mkw/battle_bottom_scores.hpp';header.write_text('#pragma once\n#include <mkw/race_map_assets.hpp>\nnamespace mkw {\ninline constexpr const char* BattleBottomScoreTexture='+json.dumps(texture)+';\ninline constexpr unsigned BattleBottomScoreAtlasWidth=128,BattleBottomScoreAtlasHeight=32;\ninline constexpr RaceMapRect BattleBottomScoreDigits[10]={'+','.join('{'+','.join(map(str,r))+'}' for r in rects)+'};\n}\n')
 receipt={'sources':{str(font.relative_to(ROOT)):hashlib.sha256(font.read_bytes()).hexdigest()},'files':[{'path':texture,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}],'glyphCell':[9,15],'visibleGlyph':[7,13],'scoreDigits':rects,'atlas':[128,32],'policy':'Original kart_kanji_font intensity/alpha, white with black outline at native bottom-screen pixel size; Battle mode only, no timer/old atlas recook'};(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('Staged native-size Battle score digits:',texture)
if __name__=='__main__':main()
