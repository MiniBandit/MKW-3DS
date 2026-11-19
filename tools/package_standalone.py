#!/usr/bin/env python3
"""Embed the current allowlisted private assets into a self-contained 3DSX."""
import argparse, hashlib, json, os, shutil, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build';SOURCE=OUT/'playable/3ds/mkw-3ds';STAGE=OUT/'standalone-romfs'
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def stage_assets(stage):
    """Link (or copy) every RomFS asset of the staged SD package into stage; return {name: sha256}."""
    manifest=json.loads((SOURCE/'manifest.json').read_text())
    # Cook receipts (JSON) are build records the game never reads: they stay out.
    # The packs hold every texture (package_sd.py ships none loose that they
    # contain); the content lists below are read at run time.
    runtime_json={'models/ceremony.json','models/battle.json','models/effect-archives.json'}
    # Every pack (menu.pak, race.pak, stream.pak: since 0.8.0 they hold every
    # game file) and anything else the package keeps in a folder. A fixed
    # list here once left stream.pak (movies and sound) and mii/ out.
    assets={name:h for name,h in manifest.items() if name.endswith('.pak') or name in runtime_json
            or ('/' in name and not name.endswith('.json'))}
    if not assets:raise ValueError('Run tools/package_sd.py first')
    left_out=sorted(n for n in manifest if n not in assets and n not in ('mkw-3ds.3dsx','CONTROLS.txt') and not n.endswith('.json'))
    if left_out:raise ValueError('Game files left out of the CIA: '+', '.join(left_out[:10]))
    stage.mkdir(exist_ok=True)
    for stale in stage.rglob('*'):
        if stale.is_file() and stale.relative_to(stage).as_posix() not in assets:stale.unlink()
    for name,h in assets.items():
        source=SOURCE/name
        if digest(source)!=h:raise ValueError('Package asset hash mismatch: '+name)
        dest=stage/name;dest.parent.mkdir(parents=True,exist_ok=True)
        # Both trees are generated staging on the same volume. Linking avoids
        # duplicating several GiB while producing the standalone container.
        dest.unlink(missing_ok=True)
        # Drives without hard links (exFAT/FAT32, some network shares) fail with
        # different errors per OS; any failure falls back to a copy.
        try:os.link(source,dest)
        except OSError:shutil.copyfile(source,dest)
    # Avoid filesystem metadata/resource forks on external macOS volumes.
    for stale in stage.rglob('._*'):
        if stale.is_file():stale.unlink()
    return assets
def package(stage):
    assets=stage_assets(stage)
    from devkitpro import tool, root as devkitpro_root
    dkp=devkitpro_root()
    smdh=OUT/'mkw-3ds.smdh'
    subprocess.run([str(tool('smdhtool')),'--create','MKW/ MARIO KART RECOMPILED 3DS','32 race imports / 10 arena assets','MKW 3DS',str(dkp/'libctru/default_icon.png'),str(smdh)],check=True)
    target=OUT/'mkw-3ds-standalone.3dsx'
    temporary=target.with_suffix('.tmp.3dsx')
    try:
        subprocess.run([str(tool('3dsxtool')),str(OUT/'mkw-3ds.elf'),str(temporary),'--smdh='+str(smdh),'--romfs='+str(stage)],check=True)
        temporary.replace(target)
    finally:temporary.unlink(missing_ok=True)
    receipt={'file':str(target),'sha256':digest(target),'bytes':target.stat().st_size,
             'elf_sha256':digest(OUT/'mkw-3ds.elf'),'assets':assets,'asset_count':len(assets),
             'physical_device_test':False,'manual_test_pending':True}
    (OUT/'standalone-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in receipt.items() if k!='assets'},indent=2))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--temporary-staging',action='store_true',help='Use the system temporary volume for RomFS staging when the project volume is nearly full.')
    args=parser.parse_args()
    if args.temporary_staging:
        with tempfile.TemporaryDirectory(prefix='mkw-romfs-') as directory:package(Path(directory))
    else:package(STAGE)

if __name__=='__main__':main()
