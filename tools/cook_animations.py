#!/usr/bin/env python3
"""Cook every original driver CHR0/PAT0 using compact per-frame matrix palettes."""
import argparse,hashlib,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('archive',type=Path);p.add_argument('output',type=Path);p.add_argument('--prefix',default='');a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);out=a.output/(a.prefix+'driver.dskm')
r=subprocess.run(['node',str(root/'build/export_driver.mjs'),str(a.archive/'driver_model.brres'),str(out)],check=True,capture_output=True,text=True)
audit=json.loads(r.stdout);audit.update(source=str(a.archive/'driver_model.brres'),source_sha256=hashlib.sha256((a.archive/'driver_model.brres').read_bytes()).hexdigest(),sha256=hashlib.sha256(out.read_bytes()).hexdigest(),sampling='Source CHR0 evaluated at every 60 Hz frame; fractional steering frames interpolate palette matrices. PAT0 texture changes retain original frame numbers.')
out.with_suffix('.json').write_text(json.dumps(audit,indent=2)+'\n');print(out,out.stat().st_size,'bytes',len(audit['clips']),'clips')
