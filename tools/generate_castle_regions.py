#!/usr/bin/env python3
"""Bake the authored indoor/outdoor checkpoint optimization from disc geometry."""
import argparse,json,math,struct,subprocess
from pathlib import Path
from course_catalog import ROOT
from model_sources import LocalModels,model
from import_courses import kmp_database

def main():
 parser=argparse.ArgumentParser(description='Optional development-only rebake; normal builds use the bundled HW132 policy.')
 parser.add_argument('--output',type=Path,required=True,help='External candidate header; never overwrites the bundled policy')
 args=parser.parse_args();header=args.output.resolve()
 if header.is_relative_to(ROOT):raise ValueError('Candidate output must remain outside the publication source tree')
 out=header.parent;out.mkdir(parents=True,exist_ok=True)
 raw=LocalModels().read('models/course_12_000.gmdl');ver,subs,names,vs,ids=model(raw)
 k=kmp_database(ROOT/'assets/courses/koopa_course/course.kmp');route=[r['position'] for r in k['ENPT']['records']];groups=k['ENPH']['records'];edges=[]
 for g in groups:
  points=list(range(g['start'],g['start']+g['length']));edges+=list(zip(points,points[1:]));edges += [(points[-1],groups[n]['start']) for n in g['next'] if n!=255]
 region=lambda i:3 if i in (25,43,58) else 1 if 26<=i<=42 or 46<=i<=57 else 2
 poses=[]
 for i,j in edges:
  a,b=route[i],route[j];length=math.hypot(b[0]-a[0],b[2]-a[2]);side=(-(b[2]-a[2])/length*900,0,(b[0]-a[0])/length*900)
  for t in (0,.25,.5,.75):
   p=[a[v]+(b[v]-a[v])*t for v in range(3)]
   for sx,dy in (((0,300),(1,450),(-1,450),(0,850)) if t in (0,.5) else ((4/3,350),(-4/3,650))):
    eye=[p[0]+sx*side[0],p[1]+dy,p[2]+sx*side[2]];poses.append((*eye,region(i),0,0,0))
 # Full-resolution forward/rear road views retain narrow visible faces.
 for i,p in enumerate(route):
  aim=route[min(i+2,len(route)-1)];d=[aim[k]-p[k] for k in range(3)];norm=math.sqrt(sum(v*v for v in d));d=[v/norm for v in d] if norm>1 else [0,0,-1]
  eye=[p[k]-d[k]*650+(350 if k==1 else 0) for k in range(3)]
  target=[p[k]+d[k]*600+(150 if k==1 else 0) for k in range(3)];f=[target[k]-eye[k] for k in range(3)];n=math.sqrt(sum(v*v for v in f));f=[v/n for v in f]
  mask=1 if 26<=i<=43 or 46<=i<=58 else 2
  poses.append((*eye,mask,*f));poses.append((2*target[0]-eye[0],eye[1],2*target[2]-eye[2],mask,-f[0],f[1],-f[2]))
 flags=[0]*(len(ids)//3)
 for s in subs:
  for t in range(s[0]//3,(s[0]+s[1])//3):flags[t]=s[3]
 from castle_webgl import bake
 masks=bake(ROOT,out,vs,ids,subs,poses,route)
 flags=[0]*(len(ids)//3)
 for sub in subs:
  for t in range(sub[0]//3,(sub[0]+sub[1])//3):flags[t]=sub[3]
 masks=bytes(m if m and not(flags[i]&3) else 3 for i,m in enumerate(masks))
 from castle_visibility_margin import expand
 masks=expand(masks,vs,ids,subs)
 (out/'castle-masks.bin').write_bytes(masks);runs=[]
 for i,m in enumerate(masks):
  if runs and runs[-1][1]==m:runs[-1]=(i+1,m)
  else:runs.append((i+1,m))
 def fnv(b):
  h=2166136261
  for x in b:h=((h^x)*16777619)&0xffffffff
  return h
 nv=len(vs);at=24+len(subs)*{3:44,4:56,5:92}[ver]+len(names)*64
 h='''// Generated from disc geometry by generate_castle_regions.py.
#pragma once
#include <cstdint>
namespace mkw::castleRegion {
inline constexpr unsigned Both=3,Inside=1,Outside=2;
constexpr unsigned checkpoint(unsigned ck){
 if(ck>=58||ck==18||ck==29||ck==34)return Both;
 return ((ck>=19&&ck<=28)||(ck>=32&&ck<=33))?Inside:Outside;
}
struct Run {uint16_t end;uint8_t mask;};
'''
 h+=f'inline constexpr uint32_t VertexHash=0x{fnv(raw[at:at+nv*44]):08x}u,IndexHash=0x{fnv(struct.pack("<%dH"%len(ids),*ids)):08x}u;\n'
 h+='inline constexpr Run Runs[]={\n'+''.join(f' {{{end},{m}}},\n' for end,m in runs)+'};\n}\n'
 header.write_text(h)
 (out/'castle-report.json').write_text(json.dumps({'poses':len(poses),'triangles':len(masks),'runs':len(runs),'both':masks.count(3),'inside':masks.count(1),'outside':masks.count(2),'method':'offline original WebGL 128x128 six-direction plus forward/rear road views; subpixel and connected-surface conservative unions; unobserved/alpha shared'},indent=2)+'\n')
 print('Baked castle:',len(poses),'poses,',len(runs),'runs')
if __name__=='__main__':main()
