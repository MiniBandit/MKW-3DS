#!/usr/bin/env python3
"""Move complete fresh compile inputs outside the publication source tree."""
import argparse,hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def collect(output,move=False):
 output=Path(output).resolve()
 if output==ROOT or output.is_relative_to(ROOT):raise ValueError('Generated include directory must be outside this source tree')
 template=json.loads((ROOT/'config/generated-inputs.json').read_text());rows={}
 for name in template['files']:
  p=ROOT/'include'/name
  if not p.is_file():raise ValueError('Generate required compile input first: '+name)
  rows[name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
 for name in rows:
  src=ROOT/'include'/name;dst=output/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
  if move:src.unlink()
 manifest=output/'generated-inputs.json';manifest.write_text(json.dumps({'schema':1,'files':rows,'provenance':'Fresh local disc cook; hashes identify this cook, not an old shipped cache'},indent=2)+'\n')
 return manifest
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--move',action='store_true');a=p.parse_args();print(collect(a.output,a.move))
