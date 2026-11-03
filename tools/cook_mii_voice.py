#!/usr/bin/env python3
"""Recover the original Mii voice selector and base picker ranges (USA REL)."""
import argparse,struct
from pathlib import Path
from retail_rel import relocated,BASE
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('rel',type=Path);a=p.parse_args();b=relocated(a.rel)
 def read(addr,fmt):return struct.unpack_from('>'+fmt,b,addr-BASE)
 ranges=[]
 for table in (0x808d7298,0x808d7358):
  ptr=read(table+24*4,'I')[0];vals=read(ptr,'58H');ranges.append([vals[i:i+2]for i in range(0,58,2)])
 mapping={0x808648e4:0,0x808648f0:1,0x808648fc:2,0x80864908:3}
 colors=[mapping[x] for x in read(0x808d7620,'12I')]
 lines=['#pragma once','#include <mkw/mii_appearance.hpp>','#include <mkw/race_voice_tables.hpp>','namespace mkw {',
 '// USA808648c0 / 80861dfc: favourite colour, size, sex -> one of 24 voices.',
 'inline unsigned miiVoiceIndex(const MiiAppearance& a){',
 ' constexpr unsigned color[]={'+','.join(map(str,colors))+'};',
 ' const unsigned sum=a.height+a.weight,size=sum>150?2:sum>104?0:1;',
 ' return color[a.favoriteColor%12]*3+size+(a.female?12:0);','}',
 'inline constexpr unsigned MiiVoiceRanges[2][29][2]={']
 for r in ranges:lines.append(' {'+','.join('{%d,%d}'%v for v in r)+'},')
 lines+=['};','// USA808638d4: 24 menu Mii ranges, shared by driver and vehicle confirmation.', 'inline constexpr unsigned MiiMenuVoiceRanges[24][2]={'+','.join('{%d,%d}'%read(0x808ad150+i*4,'2H') for i in range(24))+'};',
 'inline unsigned raceVoiceRange(unsigned character,bool local,unsigned type,unsigned end,unsigned mii){return character<24?voice::Range[character][local?0:1][type][end]:MiiVoiceRanges[local?0:1][type][end]+118*mii;}',
 '// USA8085f4b8 delegates nonlocal remapping to the common character path.',
 'inline int raceVoiceRemap(unsigned character,bool local,unsigned type){return character<24?voice::Remap[character][local?1:0][type]:local?(type==10?6:type==7?19:int(type)):voice::Remap[0][0][type];}',
 '}']
 (ROOT/'include/mkw/mii_voice.hpp').write_text('\n'.join(lines)+'\n')
if __name__=='__main__':main()
