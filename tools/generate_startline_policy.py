#!/usr/bin/env python3
"""Derive finish-line sampler faces from original BRRES/KMP; no stored geometry."""
import concurrent.futures,json,math,os,subprocess
from pathlib import Path
from course_catalog import ROOT,courses
from import_courses import kmp_database
from unpack_szs import unpack
MATERIALS=json.loads((ROOT/'config/startline-materials.json').read_text())
def one(c):
 folder=ROOT/'assets/courses'/c['key'];unpack(ROOT/'data/extracted'/c['archive'],folder)
 out=ROOT/'build/startline-source'/c['key'];out.mkdir(parents=True,exist_ok=True)
 with (out/'export.log').open('w') as log:
  subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(folder/'course_model.brres'),str(out)],env={**os.environ,'MKW_RETAIL_LIGHTING':'1','MKW_COURSE_MATERIALS':'1','MKW_FREEZE_NONLINEAR_UV':'1'},stdout=log,stderr=subprocess.STDOUT,check=True)
 k=kmp_database(folder/'course.kmp');cp=k['CKPT']['records'][0];kt=k['KTPT']['records'][0]['position'];a,b=cp['left'],cp['right'];length=math.dist(a,b);ex,ez=(b[0]-a[0])/length,(b[1]-a[1])/length;mx,mz=(a[0]+b[0])/2,(a[1]+b[1])/2
 names=MATERIALS[c['prefix']];faces=set()
 for m in json.loads((out/'model.json').read_text())['models']:
  for s in m['shapes']:
   if s['material'] not in names:continue
   vs=s['vertices']
   for i in range(0,len(vs)-2,3):
    t=vs[i:i+3];cross=[(v[0]-mx)*-ez+(v[2]-mz)*ex for v in t];along=[(v[0]-mx)*ex+(v[2]-mz)*ez for v in t]
    u=[t[1][j]-t[0][j] for j in range(3)];v=[t[2][j]-t[0][j] for j in range(3)];n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];mag=math.sqrt(sum(x*x for x in n))
    if not mag or abs(n[1])/mag<.7:continue
    if max(cross)-min(cross)>1600 or abs(sum(cross)/3)>1300 or max(along)<-length*.6 or min(along)>length*.6:continue
    if abs(sum(v[1] for v in t)/3-kt[1])>120:continue
    faces.add(','.join(str(round(x*16)) for p in sorted(tuple(v[:3]) for v in t) for x in p))
 if not faces:raise ValueError('No finish-line faces: '+c['key'])
 print(c['id'],len(faces),flush=True)
 return c['prefix'],{'materials':names,'faces':sorted(faces)}
if __name__=='__main__':
 with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:rows=dict(pool.map(one,[c for c in courses() if c['id']<32]))
 (ROOT/'tools/course_startline_faces.json').write_text(json.dumps(rows,indent=2)+'\n')
