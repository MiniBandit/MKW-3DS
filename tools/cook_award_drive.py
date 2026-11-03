"""Export winningrun_demo's three ENPH lanes and CAME/POTI shot chain."""
import json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def cook_drive(source):
 k=json.loads((source/'award-sources.json').read_text())['scenes']['winningrun_demo']['kmp']
 out=['// Generated from winningrun_demo/course.kmp by tools/cook_award_drive.py.', '#pragma once', '#include <mkw/math.hpp>', 'namespace mkw {']
 def raw(name,points,smooth,loop=1):
  data=struct.pack('>HBB',len(points),smooth,loop)
  for p in points:data+=struct.pack('>3f2H',*p['position'],*p['settings'])
  out.append('alignas(4) inline constexpr unsigned char '+name+'[]={'+','.join(map(str,data))+'};')
 for i,g in enumerate(k['ENPH']['records']):raw('AwardDriveLane'+str(i),k['ENPT']['records'][g['start']:g['start']+g['length']],1)
 out.append('inline constexpr const unsigned char* AwardDriveLanes[]={AwardDriveLane0,AwardDriveLane1,AwardDriveLane2};')
 for r in k['POTI']['records']:raw('AwardDriveRail'+str(r['index']),r['points'],r['smooth'],r['loop'])
 out.append('inline constexpr const unsigned char* AwardDriveRails[]={'+','.join('AwardDriveRail'+str(r['index']) for r in k['POTI']['records'])+'};')
 out.append('struct AwardDriveShot {unsigned type,next,route,fovSpeed,viewSpeed;Vec3 position,viewStart,viewEnd;float fovStart,fovEnd,duration;};')
 def f(x):return repr(float(x))+'f'
 def vec(b,o):return '{'+','.join(map(f,struct.unpack_from('>3f',b,o)))+'}'
 out.append('inline constexpr AwardDriveShot AwardDriveShots[]={')
 for r in k['CAME']['records']:
  b=bytes.fromhex(r['raw_hex']);out.append('{'+','.join([str(b[0]),str(b[1]),str(b[3]),str(struct.unpack_from('>H',b,6)[0]),str(struct.unpack_from('>H',b,8)[0]),vec(b,12),vec(b,44),vec(b,56),f(struct.unpack_from('>f',b,36)[0]),f(struct.unpack_from('>f',b,40)[0]),f(struct.unpack_from('>f',b,68)[0])])+'},')
 out+=['};','}'];(ROOT/'include/mkw/award_drive_data.hpp').write_text('\n'.join(out)+'\n')
if __name__=='__main__':cook_drive(ROOT/'build/award-source')
