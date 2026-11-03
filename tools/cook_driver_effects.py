#!/usr/bin/env python3
"""Preserve original animated driver-effect roots in build-local DSKM8 banks with driver look subtrees."""
import argparse, concurrent.futures, hashlib, json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def cook(output,jobs=6):
    garage=ROOT/'build/roster/models/garage.json'
    if not garage.is_file():garage=ROOT/'sd-card/3ds/mkw-3ds/models/garage.json'
    choices=json.loads(garage.read_text())['choices'];models=output/'models';models.mkdir(parents=True,exist_ok=True)
    def one(choice):
        stem=choice['prefix'].rstrip('_');source=None
        for directory in (ROOT/'assets/vehicles',ROOT/'build/roster-work/vehicles'):
            p=directory/stem/'driver_model.brres'
            if p.is_file():source=p;break
        if source is None:raise ValueError('Missing extracted driver source: '+stem)
        bank=models/(choice['prefix']+'driver.dskm')
        result=subprocess.run(['node',str(ROOT/'build/export_driver.mjs'),str(source),str(bank),'model'],check=True,capture_output=True,text=True)
        b=bank.read_bytes()
        if b[:4]!=b'DSKM' or int.from_bytes(b[4:8],'little') not in (5,6,7,8):raise ValueError('Missing original effect root: '+stem)
        metadata=json.loads(result.stdout)
        return {'path':bank.relative_to(output).as_posix(),'source':str(source.relative_to(ROOT)),'sourceSha256':sha(source),'sha256':sha(bank),'bytes':len(b),'rootSlot':int.from_bytes(b[28:32],'little',signed=True),'paletteCount':metadata['palette'],'formatVersion':int.from_bytes(b[4:8],'little')}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:files=list(pool.map(one,choices))
    receipt={'version':1,'garageSha256':sha(garage),'exporterSha256':sha(ROOT/'tools/export_driver.ts'),'files':files}
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Cooked %d driver-effect banks; %d use existing palettes; %d preserve separate root tracks'%(len(files),sum(f['rootSlot']>=0 for f in files),sum(f['rootSlot']<0 for f in files)))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/driver-effects');p.add_argument('--jobs',type=int,default=6);a=p.parse_args();cook(a.output,a.jobs)
