#!/usr/bin/env python3
"""Stage a private playable SD package from the user's locally cooked assets.

The runtime files are the ones tools/resource_manifest.py lists. menu.pak is
packed by the builder's menu archive step; race.pak is packed here from that
list, then the low-detail racers (tools/pack_racer_lod.py) and the Mii racers
(tools/pack_mii_racers.py) are merged into it, as docs/MANUAL-BUILD.md orders.

    package_sd.py            build/mkw-3ds-SD.zip
    package_sd.py --no-zip   staged in build/playable/3ds/mkw-3ds only
"""
import hashlib,json,re,shutil,struct,subprocess,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];assets=ROOT/'sd-card/3ds/mkw-3ds';out=ROOT/'build/playable/3ds/mkw-3ds';out.mkdir(parents=True,exist_ok=True)
paks=ROOT/'build/paks';paks.mkdir(parents=True,exist_ok=True)
def tool(script,*args):subprocess.run([sys.executable,str(ROOT/'tools'/script),*map(str,args)],cwd=ROOT,check=True)
mapping=json.loads(subprocess.check_output([sys.executable,str(ROOT/'tools/resource_manifest.py')],cwd=ROOT,text=True))
# pack_racer_lod.py refuses a base that already holds its models (pack_mii_racers.py
# replaces its own, so those stay listed).
def overlay_model(rel):
 name=Path(rel).name
 return rel.startswith('models/') and '_lod' in name and name.endswith('.gmdl')
base={rel:src for rel,src in mapping.items() if not overlay_model(rel)}
(paks/'runtime-files.json').write_text(json.dumps(base,indent=2)+'\n')
tool('pack_race_assets.py',paks/'runtime-files.json',paks/'race-base.pak','--exclude-menu',assets/'menu.pak')
tool('pack_racer_lod.py','--base',paks/'race-base.pak','--overlay',assets,'--output',paks/'race-lod.pak')
tool('pack_mii_racers.py','--base',paks/'race-lod.pak','--overlay',ROOT/'build/mii-racers','--output',paks/'race.pak')
files={Path(src):Path(rel) for rel,src in base.items()}
files[assets/'menu.pak']=Path('menu.pak');files[paks/'race.pak']=Path('race.pak')
manifest={}
for src,rel in files.items():
 dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);manifest[rel.as_posix()]=hashlib.sha256(dest.read_bytes()).hexdigest()
# FAT32 allows one folder 65,536 entries, and a long file name takes several:
# the loose textures folder outgrew it. menu.pak and race.pak already hold the
# textures and models and the game reads them first (pack::read, pack::size), so
# what is inside a pak is not also copied loose. Each one is checked against its
# pak entry, so a stale pak fails here instead of on the console; the overlays'
# models differ on purpose (their texture tables are rewritten).
def pak_entries(path):
 with path.open('rb') as f:
  magic,count,size=struct.unpack('<4sII',f.read(12))
  if magic!=b'MPK1':raise ValueError('Not an MPK1 archive: '+path.name)
  rows=[struct.unpack('<III',f.read(12)) for _ in range(count)];names=f.read(size)
  for name_at,offset,length in rows:
   f.seek(offset);yield names[name_at:names.index(0,name_at)].decode(),f.read(length)
for archive in ('menu.pak','race.pak'):
 for name,data in pak_entries(out/archive):
  if name.endswith(('.t3x','.gmdl','.dskm','.dobj')) and name in manifest:
   rewritten=Path(name).name.startswith(('mii_','ui_mii_','award_unpacked_Award_')) or '_lod' in Path(name).name
   if not rewritten and hashlib.sha256(data).hexdigest()!=manifest[name]:raise ValueError(f'{archive} is older than {name}: rebuild the archives')
   (out/name).unlink();del manifest[name]
controls='''Mario Kart Wii 3DS - built from your own Mario Kart Wii disc.
Copy the 3ds folder to the SD card root.
Race: Circle Pad steer; A accelerate; B brake; R hop/drift; L use item;
Y or D-pad Up trick / wheelie; X or ZL look behind; START pause;
SELECT speedometer. Touch the lower screen to switch the map view.
Dynamic shadows and the notification LED are in License Settings.
Original local game assets are packaged privately for this user's device.
Do not replace only the executable; use the complete package.
'''
# Every asset path the game's sources name should have reached the package;
# a gap here is a stalled menu or a failed race on the console.
in_paks={name for archive in ('menu.pak','race.pak') for name,_ in pak_entries(out/archive)}
referenced={}
for source in [*(ROOT/'include/mkw').iterdir(),*(ROOT/'src').glob('*.cpp')]:
 if source.is_file() and not source.name.startswith('._'):
  for name in re.findall(r'"((?:textures|models|movies|mii)/[A-Za-z0-9_./-]+\.[a-z0-9]+)"',source.read_text(errors='ignore')):referenced.setdefault(name,source.name)
absent=sorted(name for name in referenced if name not in in_paks and not (out/name).is_file())
for name in absent[:40]:print(f'WARNING: {name} (named in {referenced[name]}) is not in the package',file=sys.stderr)
if absent:print(f'WARNING: {len(absent)} of {len(referenced)} referenced assets are not in the package',file=sys.stderr)
(out/'CONTROLS.txt').write_text(controls);manifest['CONTROLS.txt']=hashlib.sha256((out/'CONTROLS.txt').read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
# This directory is generated package staging. Keep it as strict as the ZIP,
# so copying the staged 3ds folder cannot retain obsolete replay/QA assets.
for stale in out.rglob('*'):
 if stale.is_file() and stale.relative_to(out).as_posix() not in {*manifest,'manifest.json'}:stale.unlink()
if '--no-zip' in sys.argv:
 print(json.dumps({'staged':str(out),'files':len(manifest)+1},indent=2))
 raise SystemExit(0)
zip_path=ROOT/'build/mkw-3ds-SD.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
 for rel in [*manifest,'manifest.json']:z.write(out/rel,Path('3ds/mkw-3ds')/rel)
print(json.dumps({'package':str(zip_path),'files':len(manifest)+1,'bytes':zip_path.stat().st_size,'sha256':hashlib.sha256(zip_path.read_bytes()).hexdigest()},indent=2))
