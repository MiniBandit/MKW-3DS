#!/usr/bin/env python3
"""Host regressions for the selective migration; no game/runtime claims.

Usage: python3 tools/tests/run_necessary.py [--only license_progression,...]
retail_save additionally takes --fixture (a copied or synthetic rksys.dat).
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SAVE=['src/license_save.cpp','src/license_records.cpp','src/license_mii_store.cpp']
TESTS={name:[] for name in ('gp_rank','gp_telemetry','bullet_gravity','driver_look_target','driver_motion_blend','driver_vehicle_response','luma_motion','required_resources','kart_start_effects','inseki_motion','cpu_roster','license_unlocks')}
TESTS.update(race_teams=['src/race_results.cpp'],camera_collision=['src/race_camera.cpp'],license_progression=SAVE,license_mii_store=SAVE,retail_save=SAVE)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--only');p.add_argument('--fixture',type=Path);p.add_argument('--compiler',default=os.environ.get('CXX','clang++'));a=p.parse_args()
 names=a.only.split(',') if a.only else list(TESTS)
 out=ROOT/'build/necessary-host-checks';out.mkdir(parents=True,exist_ok=True);results=[]
 for name in names:
  if name not in TESTS:p.error('Unknown test: '+name)
  if name=='retail_save' and a.fixture is None:continue
  inputs=[ROOT/'tools/tests'/(name+'.cpp'),*[ROOT/q for q in TESTS[name]]];exe=out/name
  cmd=[a.compiler,'-std=c++23','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'include'),*map(str,inputs),'-o',str(exe)]
  r=subprocess.run(cmd,capture_output=True,text=True)
  if not r.returncode:r=subprocess.run([str(exe),*([str(a.fixture.resolve())] if name=='retail_save' else [])],capture_output=True,text=True)
  (out/(name+'.log')).write_text(r.stdout+r.stderr)
  row={'name':name,'passed':r.returncode==0,'exit':r.returncode,'inputs':{str(q.relative_to(ROOT)):hashlib.sha256(q.read_bytes()).hexdigest() for q in inputs}}
  results.append(row);print(name,'PASS' if row['passed'] else 'FAIL',flush=True)
 (out/'receipt.json').write_text(json.dumps(results,indent=2)+'\n')
 raise SystemExit(not all(r['passed'] for r in results))
if __name__=='__main__':main()
