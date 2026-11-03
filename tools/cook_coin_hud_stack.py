#!/usr/bin/env python3
"""Stage the original screen-space pile_coin model and its count-driven SRT0."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
from cook_model import cook
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/battle-coin-hud-assets');out=p.parse_args().output.resolve()
    work=out/'exports';work.mkdir(parents=True,exist_ok=True);source=ROOT/'assets/courses/block_battle/coin.brres'
    before=hashlib.sha256(source.read_bytes()).hexdigest();bank=work/'pile_coin.dobj';name='battle_hud_coin_stack'
    env={**os.environ,'MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_FREEZE_NONLINEAR_UV':'1','MKW_OBJECT_BANK':str(bank)}
    subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work),'pile_coin'],env=env,check=True)
    parts=cook(work,out,name,256,preserve_vertices=True,texture_prefix=name);assert len(parts)==1
    target=out/'models'/(name+'.gmdl');shutil.copyfile(out/'models'/(parts[0]['name']+'.gmdl'),target)
    targetBank=out/'models'/(name+'.dobj');shutil.copyfile(bank,targetBank)
    info=json.loads((work/'bank.json').read_text());assert info['srt']
    assert hashlib.sha256(source.read_bytes()).hexdigest()==before
    # Exclude the duplicate numbered model, but retain every texture in the
    # GMDL table: Model::load loads the entire table, including entry000.
    files=[{'path':str(f.relative_to(out)),'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'bytes':f.stat().st_size} for folder in ('models','textures') for f in sorted((out/folder).iterdir()) if f.suffix in ('.gmdl','.dobj','.t3x') and not (folder=='models' and f.stem.endswith('_000'))]
    (out/'receipt.json').write_text(json.dumps({'source':str(source.relative_to(ROOT)),'sourceSha256':before,'bank':info,'files':files,'purpose':'UI-local held-coin stacks, archive coin / model pile_coin, SRT0 selected by column count minus1'},indent=2)+'\n')
    print('Staged original HUD coin stack with nonlinear SRT0 bank:',out)
if __name__=='__main__':main()
