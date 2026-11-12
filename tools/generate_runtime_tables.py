#!/usr/bin/env python3
"""Generate compile-time course/audio metadata from verified RMCE01 inputs."""
import argparse,json,math,re,struct,subprocess
from pathlib import Path
from course_catalog import ROOT,courses
from retail_rel import relocated,BASE
from retail_dol import Dol

def output(name,text):
 p=ROOT/'include/mkw'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text+'\n')
def arr(v):
 if isinstance(v,(list,tuple)):return '{'+','.join(arr(x) for x in v)+'}'
 if isinstance(v,float):return repr(v)+'f'
 return str(v)
def catalog():
 h=['// Generated from source course catalog and imported models.','#pragma once','#include <Common.hh>','namespace mkw {','struct CourseChoice {const char* name;Kinoko::Course id;const char* archive;const char* models;const char* sky;const char* cup;bool supported;bool cpuCompletionVerified;};','inline constexpr CourseChoice Courses[]={']
 for c in courses():
  sky='coconut_sky' if c['id']==5 else c['prefix']+'_sky'
  h.append('{'+','.join([json.dumps(c['name']),'Kinoko::Course::'+c['enum'],json.dumps(c['archive']),json.dumps('models/'+c['prefix']+'.list'),json.dumps('models/'+sky+'.list'),json.dumps(c['cup']),'true','true' if c['id']<32 else 'false'])+'},')
 h+=['};','inline constexpr unsigned CourseCount=sizeof(Courses)/sizeof(Courses[0]);','}'];output('course_catalog.hpp','\n'.join(h))
def audio():
 import unicorn as u
 from unicorn.ppc_const import UC_PPC_REG_1,UC_PPC_REG_3,UC_PPC_REG_4,UC_PPC_REG_LR,UC_PPC_REG_MSR
 d=relocated(ROOT/'build/itembox-retail.rel')
 read=lambda a,f:struct.unpack_from('>'+f,d,a-BASE)
 ranges=[[[list(read(read(table+4*c,'I')[0]+t*4,'HH')) for t in range(29)] for table in (0x808d7298,0x808d7358)] for c in range(24)]
 cpu=u.Uc(u.UC_ARCH_PPC,u.UC_MODE_32|u.UC_MODE_BIG_ENDIAN);cpu.mem_map(0x80500000,0x500000);cpu.mem_write(BASE,bytes(d));cpu.mem_map(0x81000000,0x10000);cpu.reg_write(UC_PPC_REG_1,0x81008000);cpu.reg_write(UC_PPC_REG_MSR,0x2000)
 remap=[]
 for c in range(24):
  by_local=[]
  for local in range(2):
   row=[]
   for t in range(32):
    cpu.mem_write(0x81000000,struct.pack('>I',t));cpu.reg_write(UC_PPC_REG_3,0x81000000);cpu.reg_write(UC_PPC_REG_4,local);cpu.reg_write(UC_PPC_REG_LR,0x81001000)
    cpu.emu_start(read(0x808ab560+c*4,'I')[0],0x81001000,count=1000)
    row.append(struct.unpack('>i',cpu.mem_read(0x81000000,4))[0])
   by_local.append(row)
  remap.append(by_local)
 from nw4r_audio import Rsar
 # Use generated archive indices, not hand-copied sound IDs.
 text=(ROOT/'include/mkw/sound_ids.hpp').read_text();ids={n:int(v) for n,v in re.findall(r'\b(VO_\w+)=(\d+)',text)}
 from cook_roster import ROSTER
 stems={c:stem.upper() for _,_,c,stem,_ in ROSTER}
 goal=[[ids.get('VO_'+stems[c]+'_GOL_'+event,0) for event in ('TOP','GOD','BAD')] for c in range(24)]
 h=['#pragma once','#include <cstdint>','namespace mkw::voice {']
 for name,ctype,dims,data in [('Range','std::uint16_t','[24][2][29][2]',ranges),('Remap','std::int8_t','[24][2][32]',remap),('Priority','std::uint8_t','[32][2]',[list(read(0x808ab860+i*2,'BB')) for i in range(32)]),('Volume3D','float','[24]',list(read(0x808ab620,'24f'))),('Volume2D','float','[24]',list(read(0x808ab6e0,'24f'))),('Goal','std::uint16_t','[24][3]',goal)]:
  h.append('inline constexpr '+ctype+' '+name+dims+'='+arr(data)+';')
 h+=['}'];output('race_voice_tables.hpp','\n'.join(h))
 # These menu ranges are resource data too; do not distribute their generated result.
 h=['#pragma once','#include <cstdint>','namespace mkw {','struct MenuVoiceRange {std::uint16_t first,last;};','inline constexpr MenuVoiceRange MenuVoiceRanges[24][2]='+arr([[read(table+c*4,'HH') for table in (0x808ace50,0x808acf10)] for c in range(24)])+';']
 from cook_menu_models import DRIVERS
 frames=[]
 for i,stem in enumerate(DRIVERS):
  source=ROOT/f'sd-card/3ds/mkw-3ds/models/ui_driver_{i}-source.json'
  data=json.loads(source.read_text())['animation']
  clip=next((c for c in data['clips'] if c['name']=='sel_gut'),None)
  if clip is None:raise ValueError('Missing sel_gut duration: '+str(source))
  frames.append(clip['duration'])
 h+=['inline constexpr unsigned MenuSelectAnimationFrames[24]='+arr(frames)+';','}'];output('menu_voice_assets.hpp','\n'.join(h))
def math_table():
 d=Dol(ROOT/'build/original/main.dol');needle=struct.pack('>4f',0,1,0.024541,-0.000301);at=d.data.find(needle)
 if at<0:raise ValueError('NW4R trigonometric table anchor missing')
 table=struct.unpack_from('>'+str(257*4)+'f',d.data,at)
 output('nw4r_sin_table.hpp','#pragma once\nnamespace mkw::ef {\ninline constexpr float SinCosTable[257][4]='+arr([table[i:i+4] for i in range(0,len(table),4)])+';\n}')
def billboards():
 assignments=json.loads((ROOT/'config/course-mii-assignments.json').read_text())['textures'];specs={};rows=set()
 for a in assignments:
  resource=a['resource'];source=ROOT/'assets/courses'/a['course']/(resource+'.brres')
  if not source.exists():continue
  specs.setdefault((resource,source.name),source)
 from cook_course_objects import mdl0_names
 for (resource,_),source in specs.items():
  for model in mdl0_names(source):
   data=json.loads(subprocess.check_output(['node',str(ROOT/'build/export_driver.mjs'),str(source),'--inventory',model],text=True))
   for n in data['nodes']:
    if n['billboardMode'] in (4,5):rows.add((resource,model,n['name'],n['billboardMode']))
 h=['#pragma once','#include <cstring>','namespace mkw {','struct CourseMiiBillboard {const char* resource;const char* model;const char* node;unsigned mode;};','inline constexpr CourseMiiBillboard CourseMiiBillboards[]={']
 h+=['{'+','.join([json.dumps(r),json.dumps(m),json.dumps(n),str(mode)])+'},' for r,m,n,mode in sorted(rows)]
 h+=['};','}'];output('course_mii_billboards.hpp','\n'.join(h))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('kind',choices=['catalog','audio','math','billboards']);a=p.parse_args();{'catalog':catalog,'audio':audio,'math':math_table,'billboards':billboards}[a.kind]()
