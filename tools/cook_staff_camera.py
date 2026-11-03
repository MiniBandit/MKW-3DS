"""Original ending_demo's 19 CAME shots and POTI rails, no substitute path."""
import json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def cook():
 k=json.loads((ROOT/'build/award-source/award-sources.json').read_text())['scenes']['ending_demo']['kmp']
 o=['#pragma once','#include <mkw/race_camera.hpp>','namespace mkw {','inline RaceCameraCourse staffCameraCourse(){RaceCameraCourse out;']
 for r in k['CAME']['records']:
  b=bytes.fromhex(r['raw_hex']);v=lambda at:'{'+','.join(repr(x)+'f' for x in struct.unpack_from('>3f',b,at))+'}'
  f=lambda at:repr(struct.unpack_from('>f',b,at)[0])+'f'
  o.append('out.cameras.push_back({'+','.join([str(b[0]),str(b[1]),str(b[3]),str(struct.unpack_from('>H',b,4)[0]),str(struct.unpack_from('>H',b,6)[0]),str(struct.unpack_from('>H',b,8)[0]),v(12),v(44),v(56),f(36),f(40),f(68)])+'});')
 for r in k['POTI']['records']:
  o.append('{RaceCameraCourse::Route r;r.smooth='+str(bool(r['smooth'])).lower()+';r.oscillating='+str(bool(r['loop'])).lower()+';')
  for p in r['points']:o.append('r.points.push_back({'+','.join(repr(float(x))+'f' for x in p['position'])+'});r.speeds.push_back('+str(p['settings'][0])+');')
  o.append('out.routes.push_back(std::move(r));}')
 o+=['return out;}','}'];result='\n'.join(o)+'\n'
 (ROOT/'include/mkw/staff_camera_data.hpp').write_text(result)
 return result
if __name__=='__main__':cook()
