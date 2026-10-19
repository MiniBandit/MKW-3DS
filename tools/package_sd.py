#!/usr/bin/env python3
"""Stage a private playable SD package from the user's locally cooked assets."""
import hashlib,json,shutil,struct,zipfile
from gx_material import GMDL_SUBMESH_BYTES
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];assets=ROOT/'sd-card/3ds/mkw-3ds';out=ROOT/'build/playable/3ds/mkw-3ds';out.mkdir(parents=True,exist_ok=True)
roster_overlay=ROOT/'build/roster'
driver_effects=ROOT/'build/driver-effects'
driver_effect_receipt=driver_effects/'receipt.json'
if not driver_effect_receipt.is_file():raise ValueError('Run tools/cook_driver_effects.py before packaging')
driver_effect_info=json.loads(driver_effect_receipt.read_text())
if driver_effect_info.get('exporterSha256')!=hashlib.sha256((ROOT/'tools/export_driver.ts').read_bytes()).hexdigest():raise ValueError('Re-cook driver effect roots after exporter changes')
driver_effect_files={row['path']:row for row in driver_effect_info['files']}
def roster_asset(relative):
 relative=Path(relative)
 if relative.is_absolute() or '..' in relative.parts:raise ValueError('Invalid roster asset path')
 if relative.as_posix() in driver_effect_files:
  candidate=driver_effects/relative
  if hashlib.sha256(candidate.read_bytes()).hexdigest()!=driver_effect_files[relative.as_posix()]['sha256']:raise ValueError('Driver effect bank changed: '+str(relative))
  return candidate
 candidate=ROOT/'build/team-vehicles'/relative
 if candidate.is_file():return candidate
 candidate=roster_overlay/relative
 return candidate if candidate.is_file() else assets/relative
# Explicit allowlist excludes QA markers, ghosts, receipts and obsolete models.
files={ROOT/'build/mkw-3ds.3dsx':Path('mkw-3ds.3dsx'),ROOT/'data/extracted/Race/Common.szs':Path('Race/Common.szs')}
catalog=json.loads((assets/'models/course-catalog.json').read_text())
if catalog.get('version')!=1 or len(catalog.get('courses',[]))!=42:raise ValueError('Import all 32 races and 10 arenas before packaging')
files[assets/'models/course-catalog.json']=Path('models/course-catalog.json')
models=['coconut_palm_000','coconut_escalator_000']
for course in catalog['courses']:
 if course['status']!='imported':raise ValueError('Course import failed: '+course['name'])
 archive=Path(course['archive']);receipt=Path(course['receipt'])
 if archive.parent!=Path('Race/Course') or archive.suffix!='.szs' or receipt.parent!=Path('models/courses'):raise ValueError('Invalid course path')
 source=ROOT/'data/extracted'/archive
 if hashlib.sha256(source.read_bytes()).hexdigest()!=course['archive_sha256']:raise ValueError('Source archive changed: '+course['name'])
 files[source]=archive;files[assets/receipt]=receipt
 # Placed objects: tools/cook_course_objects.py meshes and animation banks.
 visuals=assets/f"models/course_{course['id']:02d}_visuals.list"
 if course['kind'] in ('race','arena'):
  if not visuals.is_file():raise ValueError('Cook course objects before packaging: '+course['name'])
  files[visuals]=Path('models')/visuals.name
  for line in visuals.read_text().splitlines():
   resource,model,mesh,bank=line.split()
   for name in (mesh,bank):
    if name!='-' and any(c not in 'abcdefghijklmnopqrstuvwxyz_0123456789' for c in name):raise ValueError('Invalid course object name')
   models.append(mesh)
   if bank!='-':files[assets/'models'/(bank+'.dobj')]=Path('models')/(bank+'.dobj')
 for name in course['model_lists']:
  relative=Path(name)
  if relative.parent!=Path('models') or relative.suffix!='.list':raise ValueError('Invalid course model list')
  # Superseded merged per-archive object meshes (pre-visuals importer).
  if relative.stem.startswith(f"course_{course['id']:02d}_obj_"):continue
  files[assets/relative]=relative
  for model in (assets/relative).read_text().split():
   if not model or any(c not in 'abcdefghijklmnopqrstuvwxyz_0123456789' for c in model):raise ValueError('Invalid course model name')
   models.append(model)
roster=json.loads(roster_asset('models/roster.json').read_text())
if roster.get('version')!=1 or len(roster.get('characters',[]))!=24:raise ValueError('Cook all 24 retail drivers with tools/cook_full_roster.py before packaging')
files[roster_asset('models/roster.json')]=Path('models/roster.json')
garage=json.loads(roster_asset('models/garage.json').read_text())
if garage.get('version')!=1 or len(garage.get('choices',[]))!=288:raise ValueError('Cook all 288 retail driver/vehicle combinations before packaging')
files[roster_asset('models/garage.json')]=Path('models/garage.json')
for choice in garage['choices']:
 prefix=choice['prefix']
 if not prefix or any(c not in 'abcdefghijklmnopqrstuvwxyz_' for c in prefix):raise ValueError('Invalid garage prefix')
 for name in choice['models']:
  if not name.startswith(prefix) or any(c not in 'abcdefghijklmnopqrstuvwxyz_' for c in name):raise ValueError('Invalid garage model name')
  models.append(name)
 for name in choice['animationBanks']:
  if name not in choice['models']:raise ValueError('Animation bank has no matching model')
  files[roster_asset(Path('models')/(name+'.dskm'))]=Path('models')/(name+'.dskm')
shadow_root=ROOT/'build/vehicle-shadows'
shadow_manifest=json.loads((shadow_root/'models/vehicle-shadows.json').read_text())
team_root=ROOT/'build/team-vehicles'
team_info=json.loads((team_root/'team-vehicles.json').read_text())
expected_teams={row['prefix']+team+'_' for row in garage['choices'] if row['vehicle'] in (0,1,2,18,19,20) for team in ('red','blue')}
if {row['prefix'] for row in team_info['choices']}!=expected_teams:raise ValueError('Cook every standard team vehicle before packaging')
for row in team_info['choices']:
 for name in row['models']:
  if not name.startswith(row['prefix']) or any(c not in 'abcdefghijklmnopqrstuvwxyz_' for c in name):raise ValueError('Invalid team model name')
  models.append(name)
 for name in row['animationBanks']:
  if not name.startswith(row['prefix']) or any(c not in 'abcdefghijklmnopqrstuvwxyz_' for c in name):raise ValueError('Invalid team animation name')
  files[team_root/'models'/(name+'.dskm')]=Path('models')/(name+'.dskm')
expected={choice['prefix'] for choice in garage['choices']}
if {row['prefix'] for row in shadow_manifest['choices']}!=expected:raise ValueError('Cook every vehicle shadow before packaging')
for row in shadow_manifest['choices']:
 relative=Path('models')/(row['prefix']+'shadow.gmdl');source=shadow_root/relative
 if hashlib.sha256(source.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('Vehicle shadow changed: '+str(relative))
 files[source]=relative
files[shadow_root/'models/vehicle-shadows.json']=Path('models/vehicle-shadows.json')
item_shadow_root=ROOT/'build/item-shadows'
item_shadow_manifest=json.loads((item_shadow_root/'models/item-shadows.json').read_text())
expected_item_shadows={'models/item_shadow_shell.gmdl','models/item_shadow_banana.gmdl'}
if item_shadow_manifest.get('version')!=1 or {row['path'] for row in item_shadow_manifest['models']}!=expected_item_shadows:
 raise ValueError('Cook both original item shadows before packaging')
for row in item_shadow_manifest['models']:
 relative=Path(row['path']);source=item_shadow_root/relative
 if hashlib.sha256(source.read_bytes()).hexdigest()!=row['sha256'] or hashlib.sha256((ROOT/row['source']).read_bytes()).hexdigest()!=row['source_sha256']:
  raise ValueError('Original item shadow changed: '+str(relative))
 files[source]=relative
files[item_shadow_root/'models/item-shadows.json']=Path('models/item-shadows.json')
textures={'drift_sparkTex2','drift_flash2','drift_rk_smoke2'}
files[assets/'textures/drift-effects.bin']=Path('textures/drift-effects.bin')
files[assets/'textures/drift-effects-bike.bin']=Path('textures/drift-effects-bike.bin')
menu_ui=json.loads((assets/'models/menu-ui.json').read_text())
if menu_ui.get('version') not in (1,2) or len(menu_ui.get('maps',[]))!=32:raise ValueError('Cook source layouts for all 32 race maps before packaging')
files[assets/'models/menu-ui.json']=Path('models/menu-ui.json')
def digest_file(path):
 digest=hashlib.sha256()
 with path.open('rb') as source:
  for block in iter(lambda:source.read(1024*1024),b''):digest.update(block)
 return digest.hexdigest()
# Validate source receipts before mixing cooked UI revisions into a package.
for name,expected in menu_ui.get('sourceHashes',{}).items():
 source=ROOT/name
 if '..' in Path(name).parts or not source.is_file() or digest_file(source)!=expected:raise ValueError('Menu UI source changed: '+name)
movie_manifest=json.loads((assets/'models/menu-movies.json').read_text())
if movie_manifest.get('version')!=1 or not movie_manifest.get('everySourceFrame') or len(movie_manifest.get('streams',[]))<17:raise ValueError('Cook complete retail menu movies before packaging')
files[assets/'models/menu-movies.json']=Path('models/menu-movies.json')
for movie in movie_manifest['streams']:
 relative=Path(movie['path']);source_relative=Path(movie['source']);source=ROOT/source_relative
 if relative.parent!=Path('movies') or relative.suffix!='.gmv' or '..' in source_relative.parts or not source_relative.as_posix().startswith('data/extracted/thp/'):raise ValueError('Invalid menu movie path')
 if digest_file(source)!=movie['sourceSha256']:raise ValueError('Menu movie source changed: '+str(source_relative))
 path=assets/relative
 with path.open('rb') as stream:header=stream.read(64)
 if len(header)!=64:raise ValueError('Truncated GMV header')
 magic,version,width,height,count,numerator,denominator,frame_bytes,offset=struct.unpack('<4sIHHIIIII32x',header)
 if magic!=b'GMV1' or version!=1 or [width,height]!=movie['textureSize'] or count!=movie['frames'] or numerator!=movie['fpsNumerator'] or denominator!=movie['fpsDenominator'] or frame_bytes!=width*height//2 or offset!=64 or path.stat().st_size!=64+count*frame_bytes:raise ValueError('Invalid full-rate GMV stream: '+str(relative))
 if digest_file(path)!=movie.get('sha256'):raise ValueError('Menu movie payload changed: '+str(relative))
 files[path]=relative
intro_audio=movie_manifest['introAudio'];audio_relative=Path(intro_audio['path'])
if audio_relative!=Path('movies/title.wav') or digest_file(assets/audio_relative)!=intro_audio['sha256']:raise ValueError('Opening movie audio changed')
if digest_file(ROOT/intro_audio['source'])!=intro_audio['sourceSha256']:raise ValueError('Opening movie audio source changed')
files[assets/audio_relative]=audio_relative
# Credits movies and their audio (tools/cook_credits.py), checked like the menu movies.
credits=json.loads((assets/'models/credits.json').read_text())
if credits.get('version')!=1 or not credits.get('everySourceFrame') or len(credits.get('streams',[]))!=2:raise ValueError('Cook the credits movies (tools/cook_credits.py) before packaging')
files[assets/'models/credits.json']=Path('models/credits.json')
for movie in credits['streams']:
 relative=Path(movie['path']);source_relative=Path(movie['source'])
 if relative.parent!=Path('movies') or relative.suffix!='.gmv' or not source_relative.as_posix().startswith('data/extracted/thp/ending/') or '..' in source_relative.parts:raise ValueError('Invalid credits movie path')
 if digest_file(ROOT/source_relative)!=movie['sourceSha256']:raise ValueError('Credits movie source changed: '+str(source_relative))
 path=assets/relative
 with path.open('rb') as stream:header=stream.read(64)
 magic,version,width,height,count,numerator,denominator,frame_bytes,offset=struct.unpack('<4sIHHIIIII32x',header)
 if magic!=b'GMV1' or [width,height]!=movie['textureSize'] or count!=movie['frames'] or frame_bytes!=width*height//2 or path.stat().st_size!=64+count*frame_bytes:raise ValueError('Invalid credits GMV stream: '+str(relative))
 if digest_file(path)!=movie['sha256']:raise ValueError('Credits movie payload changed: '+str(relative))
 files[path]=relative
for sound in credits['audio']:
 relative=Path(sound['path'])
 if relative.parent!=Path('movies') or relative.suffix!='.wav' or digest_file(assets/relative)!=sound['sha256']:raise ValueError('Credits audio changed: '+str(relative))
 files[assets/relative]=relative
menu_audio=json.loads((assets/'models/menu-audio.json').read_text())
if menu_audio.get('version')!=1 or digest_file(ROOT/menu_audio['source'])!=menu_audio['sourceSha256']:raise ValueError('Title music source changed')
files[assets/'models/menu-audio.json']=Path('models/menu-audio.json')
for sound in menu_audio['waves']:
 relative=Path(sound['path'])
 if relative.parent!=Path('movies') or relative.suffix!='.wav' or digest_file(assets/relative)!=sound['sha256']:raise ValueError('Title music payload changed')
 files[assets/relative]=relative


for sprite in menu_ui['sprites'].values():
 relative=Path(sprite['path'])
 if relative.parent!=Path('textures') or relative.suffix!='.t3x':raise ValueError('Invalid menu UI texture path')
 textures.add(relative.stem)
menu_models=json.loads((assets/'models/menu-models.json').read_text())
files[assets/'models/menu-models.json']=Path('models/menu-models.json')
for entry in menu_models['models']:
 prefix=entry['prefix'];model=entry['model']['name']
 if not prefix.startswith(('ui_driver_','ui_back_','ui_machine_')) or any(c not in 'abcdefghijklmnopqrstuvwxyz_0123456789' for c in prefix):raise ValueError('Invalid menu model prefix')
 if model!=prefix+'_000':raise ValueError('Invalid menu model name')
 models.append(model)
 animation=assets/'models'/(prefix+'.dskm')
 if entry.get('animation'):
  if hashlib.sha256(animation.read_bytes()).hexdigest()!=entry['animation_sha256']:raise ValueError('Menu animation changed: '+prefix)
  files[animation]=Path('models')/animation.name
race_items=json.loads((assets/'models/race-items.json').read_text())
if race_items.get('version')!=1:raise ValueError('Cook current race item/HUD assets before packaging')
files[assets/'models/race-items.json']=Path('models/race-items.json')
for item in [*race_items['items'].values(),race_items['itemBox'],race_items['startSignal'],race_items['bombCore']]:
 relative=Path(item['path'])
 if relative.parent!=Path('models') or relative.suffix!='.gmdl':raise ValueError('Invalid race item model path')
 models.append(relative.stem)
 if item.get('animation'):
  animation=Path(item['animation'])
  if animation!=relative.with_suffix('.dskm'):raise ValueError('Invalid race item animation path')
  files[assets/animation]=animation
for category in ['hudItems','hudRanks','hudDigits','hudExtras']:
 for sprite in race_items[category].values():
  relative=Path(sprite['path'])
  if relative.parent!=Path('textures') or relative.suffix!='.t3x':raise ValueError('Invalid race HUD texture path')
  textures.add(relative.stem)
# Award ceremony, losing and ending scenes (tools/cook_ceremony.py): stages as
# courses (archive for Kinoko, model lists, placed objects), award characters
# with their animation banks, podium, trophies and camera moves.
def safe_name(name,what):
 if not name or any(c not in 'abcdefghijklmnopqrstuvwxyz_0123456789' for c in name):raise ValueError('Invalid '+what+' name: '+name)
 return name
ceremony=json.loads((assets/'models/ceremony.json').read_text())
if ceremony.get('version')!=1:raise ValueError('Cook the award ceremony (tools/cook_ceremony.py) before packaging')
files[assets/'models/ceremony.json']=Path('models/ceremony.json')
for stage in ceremony['stages']:
 archive=Path('Race/Course')/(safe_name(stage['key'],'stage')+'.szs');files[ROOT/'data/extracted'/archive]=archive
 receipt=Path(stage['receipt'])
 if receipt.parent!=Path('models/courses'):raise ValueError('Invalid stage receipt')
 files[assets/receipt]=receipt
 for name in stage['model_lists']:
  relative=Path(name)
  if relative.parent!=Path('models') or relative.suffix!='.list':raise ValueError('Invalid stage model list')
  files[assets/relative]=relative;models+=[safe_name(m,'stage model') for m in (assets/relative).read_text().split()]
 if stage.get('object_visuals'):
  visuals=Path(stage['object_visuals'])
  if visuals.parent!=Path('models') or visuals.suffix!='.list':raise ValueError('Invalid stage object list')
  files[assets/visuals]=visuals
  for line in (assets/visuals).read_text().splitlines():
   resource,model,mesh,bank=line.split()
   models.append(safe_name(mesh,'stage object'))
   if bank!='-':files[assets/'models'/(safe_name(bank,'stage object bank')+'.dobj')]=Path('models')/(bank+'.dobj')
for character in ceremony['award']['characters']:
 model,bank=Path(character['model']),Path(character['bank'])
 if model.parent!=Path('models') or bank!=model.with_suffix('.dskm'):raise ValueError('Invalid award character paths')
 models.append(safe_name(model.stem,'award model'));files[assets/bank]=bank
models+=[safe_name(n,'award model') for n in [*ceremony['award']['stands'],ceremony['award']['shadow'],*ceremony['trophies'].values()]]
for camera in ceremony['cameras']:
 relative=Path(camera['file'])
 if relative.parent!=Path('models/ceremony') or relative.suffix!='.scam':raise ValueError('Invalid ceremony camera path')
 files[assets/relative]=relative
# Battle content and race/award UI images (tools/cook_battle.py).
battle=json.loads((assets/'models/battle.json').read_text())
if battle.get('version')!=1:raise ValueError('Cook the battle content (tools/cook_battle.py) before packaging')
files[assets/'models/battle.json']=Path('models/battle.json')
models+=[safe_name(n,'battle model') for n in [*battle['balloons'].values(),battle['lakituBoard']]]
luma=battle['luma'];model,bank=Path(luma['model']),Path(luma['bank'])
if model.parent!=Path('models') or bank!=model.with_suffix('.dskm'):raise ValueError('Invalid Luma paths')
models.append(safe_name(model.stem,'Luma model'));files[assets/bank]=bank
for key,rows in battle['ui'].items():
 for row in rows:
  relative=Path(row['texture'])
  if relative.parent!=Path('textures/ui')/safe_name(key,'UI archive') or relative.suffix!='.t3x':raise ValueError('Invalid UI texture path')
  safe_name(relative.stem,'UI texture');files[assets/relative]=relative
for name in battle['uiData']:
 relative=Path(name)
 if relative.parts[0]!='ui-data' or len(relative.parts)!=3 or relative.suffix not in ('.brfnt','.bmg'):raise ValueError('Invalid UI data path')
 safe_name(relative.parent.name,'UI archive');safe_name(relative.stem,'UI data');files[assets/relative]=relative
# Every effect archive (tools/cook_effect_archives.py): REFX tables and textures.
effect_archives=json.loads((assets/'models/effect-archives.json').read_text())
if effect_archives.get('version')!=1:raise ValueError('Cook the effect archives (tools/cook_effect_archives.py) before packaging')
files[assets/'models/effect-archives.json']=Path('models/effect-archives.json')
folded={}
for archive in effect_archives['archives']:
 folder=Path(archive['folder'])
 if folder.parent!=Path('textures/fx') or safe_name(folder.name,'effect archive')!=folder.name:raise ValueError('Invalid effect archive folder')
 for table in archive['tables']:
  relative=Path(table['table'])
  if relative.parent!=folder or relative.suffix!='.refx':raise ValueError('Invalid effect table path')
  files[assets/relative]=relative
  for texture in table['textures']:
   # Original names keep their capitals; FAT32 ignores case, so no two may differ only by it.
   safe_name(texture.lower(),'effect texture')
   if (folder/texture.lower()) in folded and folded[folder/texture.lower()]!=texture:raise ValueError('Effect textures differ only by case: '+texture)
   folded[folder/texture.lower()]=texture;files[assets/folder/(texture+'.t3x')]=folder/(texture+'.t3x')
for name in models:
 p=roster_asset(Path('models')/(name+'.gmdl'));b=p.read_bytes();magic,version,nv,ni,ns,nt=struct.unpack_from('<4s5I',b)
 if magic!=b'GMDL' or version not in GMDL_SUBMESH_BYTES:raise ValueError('Regenerate supported GMDL: '+str(p))
 stride=GMDL_SUBMESH_BYTES[version]
 for i in range(nt):textures.add(b[24+ns*stride+i*64:24+ns*stride+(i+1)*64].split(b'\0')[0].decode())
 files[p]=Path('models')/p.name
for name in sorted(textures):files[roster_asset(Path('textures')/(name+'.t3x'))]=Path('textures')/(name+'.t3x')
# Race VFX cooked in this tree (tools/cook_race_effects.py, tools/cook_effects.py
# --output build/vfx/textures) replace or extend the shared SD assets.
vfx=ROOT/'build/vfx/textures'
if not (vfx/'course-effects.json').is_file():raise ValueError('Run tools/cook_race_effects.py --course-only before packaging')
overlay={}
effect_tables=[]
effect_profiles=json.loads((vfx/'course-effects.json').read_text())
if len(effect_profiles)!=42 or {p['id'] for p in effect_profiles}!=set(range(42)):raise ValueError('Cook all 42 course effect banks')
for profile in effect_profiles:
 bank=vfx/profile['bank']
 if not bank.is_file():raise ValueError('Missing course effect bank: '+str(bank))
 overlay['textures/'+bank.name]=bank
 effect_tables.append(bank.read_bytes())
# REFX v3 (NW4R EF resources): a 20-byte header, then 40-byte texture entries (name,
# GX and padded sizes).
for table in effect_tables:
 if len(table)<20 or struct.unpack_from('<4sI',table)!=(b'REFX',3):raise ValueError('Effect bank is not REFX v3')
 texture_count=struct.unpack_from('<I',table,12)[0]
 if len(table)<20+texture_count*40:raise ValueError('Truncated effect texture table')
 for i in range(texture_count):
  name=table[20+i*40:52+i*40].split(b'\0')[0].decode()
  if not name.startswith('fx_') or any(c not in 'abcdefghijklmnopqrstuvwxyz_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ' for c in name):raise ValueError('Invalid effect texture name')
  overlay['textures/'+name+'.t3x']=vfx/(name+'.t3x')
for name in ('drift-effects.bin','drift-effects-bike.bin'):
 if (vfx/name).is_file():overlay['textures/'+name]=vfx/name
# CtrlRaceNameBalloon names and line (tools/cook_race_balloon.py).
for extra in sorted(vfx.glob('hud_*.t3x')):overlay['textures/'+extra.name]=extra
# Lakitu (tools/cook_lakitu.py): models, CHR0 banks, joint tracks, textures.
vfx_models=ROOT/'build/vfx/models'
for name in ('body','rod','flag','signal','lap','lapf','reverse'):
 model=vfx_models/f'lakitu_{name}.gmdl'
 if not model.is_file():raise ValueError('Run tools/cook_lakitu.py before packaging')
 overlay['models/'+model.name]=model
 b=model.read_bytes();version=struct.unpack_from('<I',b,4)[0];ns,nt=struct.unpack_from('<II',b,16)
 table=24+ns*GMDL_SUBMESH_BYTES[version]
 for i in range(nt):
  tex=b[table+i*64:table+(i+1)*64].split(b'\0')[0].decode();overlay['textures/'+tex+'.t3x']=vfx/(tex+'.t3x')
for extra in ('lakitu_body.dskm','lakitu_rod.dskm','lakitu_flag.dskm','lakitu_body.djnt','lakitu_rod.djnt'):overlay['models/'+extra]=vfx_models/extra
# Rosalina's separate common/tico.brres model and looping wait animation.
for name in ('luma.gmdl','luma.dskm'):
 src=vfx_models/name
 if not src.is_file():raise ValueError('Run tools/cook_luma.py before packaging')
 overlay['models/'+name]=src
b=(vfx_models/'luma.gmdl').read_bytes();version=struct.unpack_from('<I',b,4)[0];ns,nt=struct.unpack_from('<II',b,16)
table=24+ns*GMDL_SUBMESH_BYTES[version]
for i in range(nt):
 tex=b[table+i*64:table+(i+1)*64].split(b'\0')[0].decode()
 overlay['textures/'+tex+'.t3x']=vfx/(tex+'.t3x')
# Course/sky meshes and receipts re-cooked in this tree (tools/cook_course_overlay.py);
# part lists and textures are unchanged, so only these files are replaced.
course_overlay=ROOT/'build/course-materials';packaged={r.as_posix() for r in files.values()}
for src in sorted(course_overlay.glob('models/*.gmdl'))+sorted(course_overlay.glob('models/*.dobj'))+sorted(course_overlay.glob('models/courses/*.json')):
 rel=src.relative_to(course_overlay).as_posix()
 if src.name.startswith('._'):continue
 if rel not in packaged:raise ValueError('Course overlay file is not packaged: '+rel)
 overlay[rel]=src
# FireBPlane's seekable phases are shared by all firebar children in a course.
fire_phases=ROOT/'build/firebar/textures/firebar-indirect.ftan'
fire_receipt=json.loads(fire_phases.with_suffix('.json').read_text())
if digest_file(fire_phases)!=fire_receipt['sha256']:raise ValueError('Fire phase stream hash mismatch')
overlay['textures/firebar-indirect.ftan']=fire_phases
# Psea distance filtering: source base levels plus verified small mip chains.
water_mipmaps=ROOT/'build/water-mipmaps'
if (water_mipmaps/'receipt.json').is_file():
 water=json.loads((water_mipmaps/'receipt.json').read_text())
 for rel,info in water['textures'].items():
  src=water_mipmaps/rel
  if rel not in packaged or not rel.startswith('textures/obj16_psea_') or not rel.endswith('.t3x'):
   raise ValueError('Invalid water mipmap override: '+rel)
  if digest_file(src)!=info['sha256'] or digest_file(assets/rel)!=info['originalSha256']:
   raise ValueError('Water mipmap source changed: '+rel)
  if digest_file((assets/rel).with_suffix('.png'))!=info['pngSha256']:
   raise ValueError('Water mipmap pixels changed: '+rel)
  overlay[rel]=src
# Static world minification levels, retaining maximum-resolution pixels.
world_mipmaps=ROOT/'build/world-mipmaps'
if (world_mipmaps/'receipt.json').is_file():
 world=json.loads((world_mipmaps/'receipt.json').read_text())
 for rel,info in world['models'].items():
  if digest_file(ROOT/info['source'])!=info['sha256']:raise ValueError('World mipmap model changed: '+rel)
 for rel,info in world['textures'].items():
  src=world_mipmaps/rel
  if rel not in packaged or Path(rel).parent!=Path('textures') or Path(rel).suffix!='.t3x':
   raise ValueError('Invalid world mipmap override: '+rel)
  if digest_file(src)!=info['sha256'] or digest_file(assets/rel)!=info['originalSha256'] or digest_file((assets/rel).with_suffix('.png'))!=info['pngSha256']:
   raise ValueError('World mipmap source changed: '+rel)
  overlay[rel]=src
# Original item channels and reflection maps, refreshed without disc extraction.
item_lighting=ROOT/'build/item-lighting'
if (item_lighting/'receipt.json').is_file():
 for rel,info in json.loads((item_lighting/'receipt.json').read_text())['files'].items():
  src=item_lighting/rel
  if rel not in packaged or Path(rel).parts[0] not in ('models','textures'):
   raise ValueError('Invalid item lighting override: '+rel)
  if digest_file(src)!=info['sha256'] or digest_file(assets/rel)!=info['originalSha256']:
   raise ValueError('Item lighting source changed: '+rel)
  overlay[rel]=src
# Original Blooper screen model and all three source clips (cook_blooper.py).
blooper=ROOT/'build/blooper'
if (blooper/'receipt.json').is_file():
 info=json.loads((blooper/'receipt.json').read_text())
 if digest_file(ROOT/info['source'])!=info['source_sha256']:raise ValueError('Blooper source changed')
 for rel in ('models/item_12.gmdl','models/item_12.dskm'):overlay[rel]=blooper/rel
 b=(blooper/'models/item_12.gmdl').read_bytes();version,ns,nt=struct.unpack_from('<I',b,4)[0],*struct.unpack_from('<II',b,16)
 stride={3:44,4:56,5:92}.get(version)
 if stride is None:raise ValueError('Unsupported Blooper GMDL version')
 for i in range(nt):
  tex=b[24+ns*stride+i*64:24+ns*stride+(i+1)*64].split(b'\0')[0].decode()
  overlay['textures/'+tex+'.t3x']=blooper/'textures'/(tex+'.t3x')
else:raise ValueError('Run tools/cook_blooper.py before packaging')
# Race-flow UI (tools/cook_race_flow.py): out-of-bounds wipe, result board and
# end-of-race menu textures, cooked into this tree's own overlay directory.
flow=ROOT/'build/race-flow/textures'
if not (flow/'flow_wipe.t3x').is_file():raise ValueError('Run tools/cook_race_flow.py before packaging')
for extra in sorted(flow.glob('flow_*.t3x')):
 if not extra.name.startswith('._'):overlay['textures/'+extra.name]=extra
# Race bottom screen (tools/cook_race_map.py): 32 course maps, their overviews
# and the UI atlas, cooked into this tree's own overlay directory.
race_map=ROOT/'build/race-map/textures'
if not (race_map/'race_bottom_ui.t3x').is_file():raise ValueError('Run tools/cook_race_map.py before packaging')
for extra in sorted(race_map.glob('race_*.t3x')):
 if not extra.name.startswith('._'):overlay['textures/'+extra.name]=extra
# Sound engine (tools/cook_audio.py): the cooked tables from build/audio and
# the user's retail BRSAR and BRSTM streams, unmodified, read by offset.
audio_manifest=ROOT/'build/audio/audio/manifest.json'
if not audio_manifest.is_file():raise ValueError('Run tools/cook_audio.py before packaging')
audio=json.loads(audio_manifest.read_text())
sound_source=ROOT/'data/extracted/sound'
if audio.get('version')!=1 or digest_file(ROOT/audio['source'])!=audio['sourceSha256']:raise ValueError('Sound archive changed: rerun tools/cook_audio.py')
if hashlib.sha256((ROOT/'build/audio/audio/sound.arc').read_bytes()).hexdigest()!=audio['arcSha256']:raise ValueError('sound.arc does not match its manifest')
overlay['audio/sound.arc']=ROOT/'build/audio/audio/sound.arc'
overlay['audio/revo_kart.brsar']=ROOT/audio['source']
for rel,info in audio['streams'].items():
 stream=sound_source/rel
 if Path(rel).parent!=Path('strm') or stream.suffix!='.brstm' or stream.stat().st_size!=info['bytes']:raise ValueError('Sound stream changed: '+rel)
 overlay['audio/'+rel]=stream
# Blue Shell / Bob-omb blast with raw UVs for its retail binder (tools/cook_blast.py).
blast=vfx_models/'bomb_blast.gmdl'
if not blast.is_file():raise ValueError('Run tools/cook_blast.py before packaging')
overlay['models/bomb_blast.gmdl']=blast
b=blast.read_bytes();ns,nt=struct.unpack_from('<II',b,16)
for i in range(nt):
 tex=b[24+ns*44+i*64:24+ns*44+(i+1)*64].split(b'\0')[0].decode();overlay['textures/'+tex+'.t3x']=vfx/(tex+'.t3x')
for rel,src in overlay.items():
 for old in [s for s,r in files.items() if r==Path(rel)]:del files[old]
 files[src]=Path(rel)
import sys
# Indexed archives (tools/pack_menu_assets.py, tools/pack_race_assets.py): the
# runtime reads packed assets with one open handle; loose files remain as a
# fallback for anything not packed.
for archive in ('menu.pak','race.pak'):
 if (assets/archive).is_file():files[assets/archive]=Path(archive)
if '--list' in sys.argv:
 print(json.dumps({rel.as_posix():str(src) for src,rel in files.items()}))
 raise SystemExit(0)
manifest={}
for src,rel in files.items():
 dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);manifest[rel.as_posix()]=hashlib.sha256(dest.read_bytes()).hexdigest()
# FAT32 allows one folder 65,536 entries, and a long file name takes several:
# the loose textures folder outgrew it. menu.pak and race.pak already hold every
# texture and the game reads them first (src/renderer.cpp Texture::load), so a
# texture inside a pak is not also copied loose. Each one is checked against
# its pak entry, so a stale pak fails here instead of on the console.
def pak_entries(path):
 b=path.read_bytes()
 if b[:4]!=b'MPK1':raise ValueError('Not an MPK1 archive: '+path.name)
 count,names=struct.unpack_from('<II',b,4);table=12;blob=12+12*count
 for i in range(count):
  name_at,offset,size=struct.unpack_from('<III',b,table+i*12)
  yield b[blob+name_at:b.index(0,blob+name_at)].decode(),b[offset:offset+size]
for archive in ('menu.pak','race.pak'):
 if not (out/archive).is_file():continue
 for name,data in pak_entries(out/archive):
  if name.endswith('.t3x') and name in manifest:
   if hashlib.sha256(data).hexdigest()!=manifest[name]:raise ValueError(f'{archive} is older than {name}: run tools/pack_menu_assets.py / tools/pack_race_assets.py')
   (out/name).unlink();del manifest[name]
controls='''MKW 3DS - native Wii menu asset port, 32 race archives and 10 arena asset sets
Copy the 3ds folder to the SD card root. Launch mkw-3ds.3dsx with Homebrew Launcher.
Menu: D-pad / Circle Pad choose, A confirm, B back. A or START skips the opening.
The opening retains every retail movie frame and its original stereo soundtrack.
The title screen uses its original BRSAR wave and authored loop point.
Audio requires a working 3DS DSP service; missing DSP leaves video playback usable.
All menus use the top LCD. The lower LCD is unused during menu selection.
During races the lower LCD shows the course map, the standings and the time;
tap it to switch between the close-up map and the whole course.
To race: Single Player > VS > Solo, then any of the 24 retail characters and one
of the six vehicles in the top three rows, drift, a cup and any of the 32 courses.
Grand Prix, Time Trials, Battle, Team VS, the remaining vehicle choices and
the two Mii outfits can be browsed but show "Not available" on the page where they
are chosen; nothing silently starts a different mode. Licenses are session slots,
not an implementation of Wii save profiles, unlocks, Mii creation or statistics.
Each race opens with the course's original preview cameras; A or START skips it.
Race: Circle Pad steer; A accelerate; B brake; R hop/drift; L use item;
D-pad trick; START pause. Pause offers continue, restart, or quit confirmation.
After finishing, press A for results. Next Race returns to course selection;
the final race returns to the main menu. VS races have 12 racers.
Bike: D-pad up starts a wheelie; down ends it.
Original source THP menu atlases stream at their native frame rates. Menu backgrounds,
character and vehicle displays use original source models and animations.
All 32 courses and 10 arenas are imported; arena race rules are not implemented.
Race music, kart sounds, character voices, item sounds and course sound events
use the original sound archive. Menu sound effects and selection music are present.
Online play, persistent profiles, the Mii identity path and unsupported race modes
remain unfinished. Rosalina's separate Luma companion is not yet rendered.
This expanded roster needs console playtesting; this is not complete Wii parity.
Original local game assets are packaged privately for this user's device.
Do not replace only the executable; use the complete package.
'''

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
