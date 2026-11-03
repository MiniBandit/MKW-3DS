#!/usr/bin/env python3
"""Stage the original Funky Stadium single-view EFB screen.
The original0x18a object uses the same screen constructor as WLscreenGC and
DK monitor. Only its six-vertex full model is used for single-view capture.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from cook_model import ROOT,cook

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/battle-screen-assets');a=p.parse_args();out=a.output.resolve();work=out/'export';work.mkdir(parents=True,exist_ok=True)
 source=ROOT/'assets/courses/skate_battle/fks_screen_wii.brres'
 env={**os.environ,'MKW_OBJECT_BANK':str(work/'bank.dobj'),'MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1'}
 subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work),'fks_screen_wii'],env=env,stdout=subprocess.DEVNULL,check=True)
 info=json.loads((work/'bank.json').read_text());assert not any(info[k] for k in ['chr','srt','pat','clr'])
 parts=cook(work,out,'obj35_fks_screen_wii__fks_screen_wii',256,preserve_vertices=True,texture_prefix='obj35_fks_screen_wii');assert len(parts)==1 and parts[0]['triangles']==2
 (out/'models/course_35_visuals.list').write_text('fks_screen_wii fks_screen_wii '+parts[0]['name']+' -\n')
 files=[{'path':f.relative_to(out).as_posix(),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for folder in ['models','textures'] for f in sorted((out/folder).iterdir()) if f.suffix in ['.gmdl','.t3x','.list']]
 (out/'receipt.json').write_text(json.dumps({'files':files,'originalSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'objectId':'0x18a','placements':2,'fullModel':{'triangles':2,'captureTexture':2,'textureName':'wl_screen1'},'native':'80811094..1118c common ctor8084bdcc; single-view loader8084bf58..c084; shared EFB TextureHolder','targetAcceptance':False},indent=2)+'\n')
if __name__=='__main__':main()
