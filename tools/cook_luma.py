#!/usr/bin/env python3
"""Cook Rosalina's separate retail Tico model without modifying source assets.

TicoModel constructor RMCE01 807cad0c binds common/tico.brres, model tico,
looping CHR0 wait. Position/orientation is supplied by TicoModel at runtime;
this asset does not bake a guessed offset into the driver mesh.
"""
import hashlib,json,os,subprocess,shutil
from pathlib import Path
from cook_model import cook
ROOT=Path(__file__).resolve().parents[1]

def main():
 source=ROOT/'assets/common/tico.brres'
 out=ROOT/'build/vfx';work=out/'luma-src'
 before=hashlib.sha256(source.read_bytes()).hexdigest()
 work.mkdir(parents=True,exist_ok=True);(out/'models').mkdir(parents=True,exist_ok=True)
 env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_RACE_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1'}
 subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work),'tico','wait','0'],env=env,check=True)
 parts=cook(work,out,'luma',preserve_vertices=True)
 if len(parts)!=1:raise ValueError('Luma must remain a single bounded model')
 shutil.copyfile(out/'models'/(parts[0]['name']+'.gmdl'),out/'models/luma.gmdl')
 result=subprocess.run(['node',str(ROOT/'build/export_driver.mjs'),str(source),str(out/'models/luma.dskm'),'tico'],capture_output=True,text=True,check=True)
 inventory=json.loads(subprocess.run(['node',str(ROOT/'build/export_driver.mjs'),str(source),'--inventory','tico'],capture_output=True,text=True,check=True).stdout)
 assert len(inventory['chr0'])==1 and inventory['chr0'][0]['name']=='wait' and inventory['chr0'][0]['loop']==1
 assert hashlib.sha256(source.read_bytes()).hexdigest()==before
 artifacts=[out/'models/luma.gmdl',out/'models/luma.dskm']
 receipt={'source':str(source),'sourceSha256':before,'original':'TicoModel USA807cad0c: tico.brres / tico / wait',
          'inventory':inventory,'animationExport':json.loads(result.stdout),
          'artifacts':{str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts},
          'boundary':'Asset candidate only; runtime Tico follower and local/CPU draw integration still required. No emulator validation.'}
 (out/'luma-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
