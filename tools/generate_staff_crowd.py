#!/usr/bin/env python3
"""Generate ending-demo billboard slots directly from the original BRRES nodes."""
import json,subprocess
from course_catalog import ROOT

def main():
 rows=[]
 for path in sorted((ROOT/'build/award-source/unpacked/ending_demo').glob('MiiObj*.brres')):
  data=json.loads(subprocess.check_output(['node',str(ROOT/'build/export_driver.mjs'),str(path),'--inventory',path.stem],text=True))
  for node in data['nodes']:
   if node['billboardMode'] in (4,5) and node['matrix'] in data['paletteIds']:
    rows.append((path.stem,data['paletteIds'].index(node['matrix']),node['billboardMode']))
 if not rows:raise ValueError('Ending demo crowd was not extracted')
 h=['// Generated from original ending_demo node modes and exporter palette.','#pragma once','namespace mkw {','struct StaffCrowdJoint {const char* model;unsigned slot,mode;};','inline constexpr StaffCrowdJoint StaffCrowdJoints[]={']
 h += ['{'+json.dumps(m)+','+str(i)+','+str(mode)+'},'for m,i,mode in rows]
 h += ['};','}'];(ROOT/'include/mkw/staff_crowd_data.hpp').write_text('\n'.join(h)+'\n')
if __name__=='__main__':main()
