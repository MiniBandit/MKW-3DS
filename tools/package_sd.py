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
# Course parts' vertex-cache triangle order (ethanseagull's 1.2.0), stored as
# <part>.cord beside each part: the same code the game runs at course load,
# which uses a file only when it matches its part (tools/course_cache_order.cpp).
sys.path.insert(0,str(ROOT/'tools'))
import host_tools
order_tool=paks/('course_cache_order.exe' if sys.platform=='win32' else 'course_cache_order')
subprocess.run([host_tools.compiler(),'-O2','-std=c++17','-ffp-contract=off','-I'+str(ROOT/'include'),'-o',str(order_tool),str(ROOT/'tools/course_cache_order.cpp')],cwd=ROOT,check=True)
course_parts=[]
for course in json.loads((assets/'models/course-catalog.json').read_text())['courses']:
 # The track's own parts: its first model list (our catalog), or Ethan's prefix.
 track_list=assets/'models'/(course['prefix']+'.list') if 'prefix' in course else assets/course['model_lists'][0]
 if track_list.is_file():course_parts+=[str(assets/'models'/(name+'.gmdl')) for name in track_list.read_text().split()]
subprocess.run([str(order_tool),*course_parts],cwd=ROOT,check=True)
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
tool('pack_mii_racers.py','--base',paks/'race-lod.pak','--overlay',ROOT/'build/mii-racers','--output',paks/'race-mii.pak')
# Opponents' compact driver banks (.dsku, about 40% smaller), from ethanseagull's work.
tool('pack_unlit_banks.py',paks/'race-mii.pak',paks/'race-unlit.pak')
# Animated racer parts: welded, cache-ordered index buffers, the same picture (ethanseagull's 1.2.0).
tool('pack_racer_weld.py',paks/'race-unlit.pak',paks/'race.pak','--report',paks/'race-weld.json')
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
# The racer weld (ethanseagull's 1.2) rewrites racer parts' index buffers on purpose.
welded=set(json.loads((paks/'race-weld.json').read_text())['models'])
for archive in ('menu.pak','race.pak'):
 for name,data in pak_entries(out/archive):
  if name.endswith(('.t3x','.gmdl','.dskm','.dobj')) and name in manifest:
   rewritten=Path(name).name.startswith(('mii_','ui_mii_','award_unpacked_Award_')) or '_lod' in Path(name).name or name in welded
   if not rewritten and hashlib.sha256(data).hexdigest()!=manifest[name]:raise ValueError(f'{archive} is older than {name}: rebuild the archives')
   (out/name).unlink();del manifest[name]
controls='''Mario Kart Wii 3DS - built from your own Mario Kart Wii disc.
Copy the 3ds folder to the SD card root.
Race: Circle Pad steer; A accelerate; B brake; R hop/drift; L use item;
Y or D-pad Up trick / wheelie; X or ZL look behind; START pause;
SELECT speedometer. Touch the lower screen to switch the map view.
Settings has presets and the CRT look; the notification LED is in License Settings.
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
# menu.pak and race.pak become MPK2 (tools/pakpack.c, from ethanseagull's work):
# entries in the order the game reads them (tools/pak-order/), LZ4-compressed
# where that saves space. Lossless; race.pak drops from about 1.8 to 1 GB.
sys.path.insert(0,str(ROOT/'tools'))
import host_tools
pakpack=paks/('pakpack.exe' if sys.platform=='win32' else 'pakpack')
subprocess.run([host_tools.compiler(),'-x','c','-O2','-pthread','-o',str(pakpack),str(ROOT/'tools/pakpack.c')],cwd=ROOT,check=True)
# Every other game file goes into a pack too, as in ethanseagull's package:
# movies and sound into stream.pak (stored, each on a 512-byte SD sector, so
# streamed reads stay whole sectors), the Mii resources into menu.pak, the rest
# into race.pak. The game's fopen looks in the packs first (asset_pack.cpp
# openFile). On a 3DS each file created on the SD card costs most of a second,
# so the first start's copy of about 1,900 loose files took tens of minutes;
# now it copies a handful of large files.
Loose=('mkw-3ds.3dsx','menu.pak','race.pak')
def mpk1_names(path):
 with path.open('rb') as f:
  magic,count,size=struct.unpack('<4sII',f.read(12))
  if magic!=b'MPK1':raise ValueError('Not an MPK1 archive: '+path.name)
  rows=[struct.unpack('<III',f.read(12)) for _ in range(count)];names=f.read(size)
  return {names[r[0]:names.index(0,r[0])].decode() for r in rows}
# A file a pack already holds stays the pack's: the game has always read the
# pack first (pack::read, then openFile), so its loose copy was never used.
already=mpk1_names(out/'menu.pak')|mpk1_names(out/'race.pak')
loose={'menu':[],'race':[],'stream':[]}
for name in sorted(manifest):
 if name in Loose or '/' not in name:continue
 if name in already:(out/name).unlink();del manifest[name];continue
 pack='stream' if name.startswith(('movies/','audio/')) else 'menu' if name.startswith('mii/') else 'race'
 loose[pack].append(name)
for index,name in enumerate(('menu','race','stream')):
 source=out/f'{name}.pak';packed=paks/f'{name}-mpk2.pak'
 (paks/f'{name}-loose.txt').write_text(''.join(f'{n}={out/n}\n' for n in loose[name]))
 if name=='stream':
  (paks/'stream-order.txt').write_text('')
  args=['--stored','--align','512','-']
 else:
  order=subprocess.run([sys.executable,str(ROOT/'tools/pak_order.py'),str(source),str(index),str(ROOT/f'tools/pak-order/{name}.txt')],
                       cwd=ROOT,check=True,capture_output=True,text=True).stdout
  (paks/f'{name}-order.txt').write_text(order)
  args=[str(source)]
 packed.unlink(missing_ok=True)
 subprocess.run([str(pakpack),'--threads','6',*args,str(paks/f'{name}-order.txt'),str(packed),'@'+str(paks/f'{name}-loose.txt')],cwd=ROOT,check=True)
 shutil.copyfile(packed,source);packed.unlink()
 manifest[f'{name}.pak']=hashlib.sha256(source.read_bytes()).hexdigest()
 for n in loose[name]:(out/n).unlink();del manifest[n]
# Folders the packed files left empty.
for folder in sorted((p for p in out.rglob('*') if p.is_dir()),key=lambda p:len(p.parts),reverse=True):
 if not any(folder.iterdir()):folder.rmdir()
# After packing, every file the sources name must still be found: in one of
# the three MPK2 packs or loose.
def mpk2_names(path):
 with path.open('rb') as f:
  magic,count,size=struct.unpack('<4sII',f.read(12))
  if magic!=b'MPK2':raise ValueError('Not an MPK2 archive: '+path.name)
  rows=[struct.unpack('<IIII',f.read(16)) for _ in range(count)];names=f.read(size)
  return {names[r[0]:names.index(0,r[0])].decode() for r in rows}
packed_names=set().union(*(mpk2_names(out/f'{n}.pak') for n in ('menu','race','stream')))
lost=sorted(n for n in referenced if (n in in_paks or (ROOT/'build/paks').exists()) and n not in packed_names and not (out/n).is_file() and n not in absent)
if lost:raise ValueError('Packed package lost files the game names: '+', '.join(lost[:10]))
print(f'{len(packed_names)} files in menu.pak, race.pak and stream.pak; loose: {sorted(manifest)}',file=sys.stderr)
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
