#!/usr/bin/env python3
"""List existing runtime inputs only; does not copy files or build a package."""
import hashlib,json,struct
from gx_material import GMDL_SUBMESH_BYTES
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];assets=ROOT/'sd-card/3ds/mkw-3ds'
roster_overlay=ROOT/'build/roster'
driver_effects=ROOT/'build/driver-effects'
driver_effect_receipt=driver_effects/'receipt.json'
if not driver_effect_receipt.is_file():raise ValueError('Run tools/cook_driver_effects.py before packaging')
driver_effect_info=json.loads(driver_effect_receipt.read_text())
if driver_effect_info.get('exporterSha256')!=hashlib.sha256((ROOT/'tools/export_driver.ts').read_bytes()).hexdigest():raise ValueError('Re-cook driver effect roots after exporter changes')
driver_effect_files={row['path']:row for row in driver_effect_info['files']}
battle_root=ROOT/'build/battle-balloon-assets'
battle=json.loads((battle_root/'receipt.json').read_text())
battle_files={row['path']:row for row in battle['files']}
def roster_asset(relative):
 relative=Path(relative)
 if relative.is_absolute() or '..' in relative.parts:raise ValueError('Invalid roster asset path')
 if relative.as_posix() in battle_files:
  candidate=battle_root/relative
  if hashlib.sha256(candidate.read_bytes()).hexdigest()!=battle_files[relative.as_posix()]['sha256']:raise ValueError('Battle balloon asset changed: '+str(relative))
  return candidate
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
files[assets/'mii/parts.mpr']=Path('mii/parts.mpr')
# License portraits use the native 3DS resource supplied by the owner.
native_mii=ROOT/'build/native-mii-resource'
native_info=json.loads((native_mii/'receipt.json').read_text())
native_resource=native_mii/'mii/CFL_Res.dat'
if native_resource.stat().st_size!=native_info['bytes'] or hashlib.sha256(native_resource.read_bytes()).hexdigest()!=native_info['sha256']:
 raise ValueError('Native Mii resource changed; run stage_native_mii_resource.py')
files[native_resource]=Path('mii/CFL_Res.dat')
# Real ceremony kart simulation reads its original KMP/KCL through ResourceManager.
for name in ('winningrun_demo','ending_demo'):
 files[ROOT/f'build/award-source/Race/Course/{name}.szs']=Path(f'Race/Course/{name}.szs')
staff=ROOT/'build/staff'
staff_receipt=staff/'models/staff-ui.json'
if not staff_receipt.is_file():raise ValueError('Cook Staff UI and both movies into build/staff before packaging')
for folder,suffix in (('textures','.t3x'),('models','.gmdl'),('movies','.gmv'),('movies','.wav')):
 for source in (staff/folder).glob('*'+suffix):files[source]=source.relative_to(staff)
files[staff_receipt]=Path('models/staff-ui.json')
for name in ('kartParam.bin','driverParam.bin'):
 files[ROOT/'assets/common'/name]=Path('models')/('award_'+name)
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
 # Arenas may also have cooked objects (including Block Plaza Mii heads).
 # Match ObjectAssets::load: race lists are required; existing arena lists ship.
 if course['kind']=='race' or visuals.is_file():
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
if roster.get('version')!=1 or len(roster.get('characters',[]))!=24:raise ValueError('Supply the manually prepared 24-driver roster before listing resources')
files[roster_asset('models/roster.json')]=Path('models/roster.json')
garage=json.loads(roster_asset('models/garage.json').read_text())
if garage.get('version')!=1 or len(garage.get('choices',[]))!=288:raise ValueError('Cook all 288 retail driver/vehicle combinations before packaging')
if {row['prefix']+'driver.dskm' for row in garage['choices']}!={Path(path).name for path in driver_effect_files}:raise ValueError('Cook every garage driver effect bank before packaging')
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
 if relative.parent!=Path('movies') or relative.suffix!='.gmv' or '..' in source_relative.parts or not str(source_relative).startswith('data/extracted/thp/'):raise ValueError('Invalid menu movie path')
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
name_manifest=assets/'models/menu-name-glyphs.json'
name_font=json.loads(name_manifest.read_text())
if digest_file(ROOT/name_font['source'])!=name_font['source_sha256']:raise ValueError('License name font source changed')
files[name_manifest]=Path('models/menu-name-glyphs.json')
for glyph in name_font['glyphs']:
 relative=Path(glyph['path'])
 if relative.parent!=Path('textures') or relative.suffix!='.t3x' or digest_file(assets/relative)!=glyph['sha256']:raise ValueError('License name glyph changed')
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
# Complete menu Mii bodies, with their own animations and texture dependencies.
# Identity heads are built from the selected part bank, outside race rendering.
mii_root=ROOT/'build/menu-mii-bodies'
mii_info=json.loads((mii_root/'menu-mii-bodies.json').read_text())
expected_mii={f'{size}{outfit}_mii_{sex}' for size in 'sml' for outfit in 'ab' for sex in 'mf'}
if {row['stem'] for row in mii_info['models']}!=expected_mii or len(mii_info['models'])!=12:raise ValueError('Cook all twelve menu Mii bodies')
for row in mii_info['models']:
 prefix='ui_mii_'+row['stem']
 if row['model']['name']!=prefix+'_000':raise ValueError('Invalid Mii body model name')
 if hashlib.sha256((ROOT/row['source']).read_bytes()).hexdigest()!=row['sourceSha256']:raise ValueError('Original Mii body changed')
 for relative,sha in [(Path('models')/(prefix+'_000.gmdl'),row['model']['sha256']),(Path('models')/(prefix+'.dskm'),row['animationSha256'])]:
  source=mii_root/relative
  if hashlib.sha256(source.read_bytes()).hexdigest()!=sha:raise ValueError('Menu Mii asset changed: '+str(relative))
  files[source]=relative
 data=(mii_root/'models'/(prefix+'_000.gmdl')).read_bytes()
 magic,version,nv,ni,ns,nt=struct.unpack_from('<4s5I',data)
 if magic!=b'GMDL' or version not in GMDL_SUBMESH_BYTES:raise ValueError('Unsupported menu Mii model')
 offset=24+ns*GMDL_SUBMESH_BYTES[version]
 for index in range(nt):
  name=data[offset+64*index:offset+64*(index+1)].split(b'\0')[0].decode()
  if not name or any(c not in 'abcdefghijklmnopqrstuvwxyz_0123456789' for c in name):raise ValueError('Invalid Mii texture name')
  relative=Path('textures')/(name+'.t3x')
  if not (mii_root/relative).is_file():raise ValueError('Missing menu Mii texture: '+name)
  files[mii_root/relative]=relative
# Battle mode overlays are private staged outputs, never old SD fallbacks.
for directory in ('battle-hud-assets','battle-hud-motion','battle-coin-assets','battle-menu-assets','battle-results-assets','battle-coin-hud-assets','battle-bottom-scores','battle-screen-assets','menu-record-assets','desert-impact-effects'):
 staged=ROOT/'build'/directory
 if directory=='desert-impact-effects' and not (staged/'receipt.json').is_file():continue # fresh per-course banks already contain these roots
 receipt=json.loads((staged/'receipt.json').read_text())
 for row in receipt['files']:
  relative=Path(row['path'])
  if relative.is_absolute() or '..' in relative.parts or relative.parts[0] not in ('models','textures'):raise ValueError('Invalid Battle asset path')
  source=staged/relative
  if not source.is_file() or digest_file(source)!=row['sha256']:raise ValueError('Battle staged asset changed: '+str(source))
  files[source]=relative
# Migrated Balloon Battle uses the original Common.szs balloon meshes.
if digest_file(ROOT/'assets/common/balloon.brres')!=battle['sourceSha256']:raise ValueError('Original battle balloons changed')
for row in battle['models']:
 name=row['model']
 if name not in ('battle_balloon_red','battle_balloon_blue'):raise ValueError('Invalid battle balloon')
 if digest_file(roster_asset(Path('models')/(name+'.gmdl')))!=row['sha256']:raise ValueError('Battle balloon model changed')
 models.append(name)
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
# Runtime loads per-course banks only; an aggregate is optional legacy data.
overlay={};effect_tables=[]
if (vfx/'race-effects.bin').is_file():
 overlay['textures/race-effects.bin']=vfx/'race-effects.bin'
 effect_tables.append((vfx/'race-effects.bin').read_bytes())
profiles=json.loads((vfx/'course-effects.json').read_text())
mode_vfx=[ROOT/'build'/f'battle-{mode}-effects/textures' for mode in ('balloon','coin')]
for mode,stage in zip(('balloon','coin'),mode_vfx):
 expected={f'race-effects-{i:02d}-{mode}.bin' for i in range(32,42)}
 missing=expected-{p['bank'] for p in profiles}
 if missing:
  staged=json.loads((stage/f'{mode}-effects.json').read_text())
  profiles.extend(p for p in staged if p['bank'] in missing)
 if not expected<={p['bank'] for p in profiles}:raise ValueError('Cook all ten '+mode+' effect banks')
for profile in profiles:
 bank=vfx/profile['bank']
 if not bank.is_file():bank=next((stage/profile['bank'] for stage in mode_vfx if (stage/profile['bank']).is_file()),bank)
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
  source=vfx/(name+'.t3x')
  if not source.is_file():source=next((stage/(name+'.t3x') for stage in mode_vfx if (stage/(name+'.t3x')).is_file()),source)
  if not source.is_file():raise ValueError('Missing effect texture: '+name)
  overlay['textures/'+name+'.t3x']=source
for name in ('drift-effects.bin','drift-effects-bike.bin'):
 if (vfx/name).is_file():overlay['textures/'+name]=vfx/name
# CtrlRaceNameBalloon names and line (tools/cook_race_balloon.py).
for extra in sorted(vfx.glob('hud_*.t3x')):overlay['textures/'+extra.name]=extra
# Lakitu (tools/cook_lakitu.py): models, CHR0 banks, joint tracks, textures.
vfx_models=ROOT/'build/vfx/models'
for name in ('body','rod','flag','signal','lap','lapf','reverse','shadow'):
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
packaged={r.as_posix() for r in files.values()}
# Later source-verified water variants override only their recooked meshes/receipts.
for course_overlay in (ROOT/'build/course-materials',ROOT/'build/course-water-feedback',ROOT/'build/rainbow-final'):
 for src in sorted(course_overlay.glob('models/*.gmdl'))+sorted(course_overlay.glob('models/*.dobj'))+sorted(course_overlay.glob('models/courses/*.json')):
  rel=src.relative_to(course_overlay).as_posix()
  if src.name.startswith('._'):continue
  if rel not in packaged:raise ValueError('Course overlay file is not packaged: '+rel)
  overlay[rel]=src
# FireBPlane's seekable phases are shared by all firebar children in a course.
fire_phases=ROOT/'build/course-water-feedback/textures/firebar-indirect.ftan'
fire_receipt=json.loads(fire_phases.with_suffix('.json').read_text())
if digest_file(fire_phases)!=fire_receipt['sha256']:raise ValueError('Fire phase stream hash mismatch')
overlay['textures/firebar-indirect.ftan']=fire_phases
# Thunder Cloud original CLR0 material animation, generated with the item cook.
cloud_bank=ROOT/'build/cloud-feedback/models/item_14.dobj'
if not cloud_bank.is_file():cloud_bank=ROOT/'build/item-lighting/models/item_14.dobj'
if not cloud_bank.is_file():cloud_bank=assets/'models/item_14.dobj'
if not cloud_bank.is_file():raise ValueError('Cook Thunder Cloud material bank before packaging')
overlay['models/item_14.dobj']=cloud_bank
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
flow_receipt=json.loads((flow.parent/'race-flow.json').read_text())
if 'awards' not in flow_receipt:raise ValueError('Run tools/cook_race_flow.py to generate the original Award UI')
for rel in flow_receipt['awards']['textures']:
 if not (flow.parent/rel).is_file():raise ValueError('Missing award texture: '+rel)
for extra in sorted(flow.glob('flow_*.t3x')):
 if not extra.name.startswith('._'):overlay['textures/'+extra.name]=extra
# Ceremony geometry and matching animation banks are a separate scene bank.
award_root=ROOT/'build/award-models'
award_info=json.loads((award_root/'award-models.json').read_text())
for rel in award_info['files']:
 path=Path(rel)
 if path.parent.as_posix() not in ('models','textures') or path.suffix not in ('.gmdl','.dskm','.dobj','.list','.t3x','.kcl','.bin'):continue
 source=award_root/path
 if not source.is_file():raise ValueError('Missing ceremony asset: '+rel)
 overlay[rel]=source
# Opening-pan controls/font/icons, prepared privately before packaging.
intro=ROOT/'build/race-intro'
if not (intro/'receipt.json').is_file():raise ValueError('Run tools/cook_race_intro.py before packaging')
intro_info=json.loads((intro/'receipt.json').read_text())
for rel,expected in intro_info['sources'].items():
 if digest_file(ROOT/rel)!=expected:raise ValueError('Intro source changed: '+rel)
for rel,info in intro_info['textures'].items():
 if Path(rel).parts[0]!='textures' or digest_file(intro/rel)!=info['sha256']:raise ValueError('Invalid intro texture: '+rel)
 overlay[rel]=intro/rel
# Race bottom screen (tools/cook_race_map.py): 32 course maps, their overviews
# and the UI atlas, cooked into this tree's own overlay directory.
race_map=ROOT/'build/race-map/textures'
if not (race_map/'race_bottom_ui.t3x').is_file():raise ValueError('Run tools/cook_race_map.py before packaging')
for extra in sorted(race_map.glob('race_*.t3x')):
 if not extra.name.startswith('._'):overlay['textures/'+extra.name]=extra
# Offline sprites for the live minimap object layer; only the active course is resident.
map_objects=ROOT/'build/map-objects/textures'
for course in (2,3,5,10,11,12,14,15,22,30):
 extra=map_objects/f'map_objects_{course:02d}.t3x'
 if not extra.is_file():raise ValueError('Run tools/cook_map_objects.py before packaging')
 overlay['textures/'+extra.name]=extra
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
# Apply targeted Battle course feedback after generic world-mip overlays.
# It preserves their delivered bytes and disables only the three replaced draws;
# an older full-course overlay must not restore those stale material records.
battle_course_feedback=ROOT/'build/battle-course-feedback'
for row in json.loads((battle_course_feedback/'receipt.json').read_text())['files']:
 relative=Path(row['path']);source=battle_course_feedback/relative
 if relative.is_absolute() or '..' in relative.parts or relative.parts[0]!='models' or relative.suffix not in ('.gmdl','.list'):
  raise ValueError('Invalid targeted Battle course resource')
 if not source.is_file() or digest_file(source)!=row['sha256']:
  raise ValueError('Targeted Battle course resource changed: '+str(source))
 overlay[relative.as_posix()]=source
# Placed Battle object models/banks/textures and their loose visual lists.
battle_objects=ROOT/'build/battle-object-assets'
for row in json.loads((battle_objects/'receipt.json').read_text())['files']:
 relative=Path(row['path']);source=battle_objects/relative
 if relative.is_absolute() or '..' in relative.parts or relative.parts[0] not in ('models','textures') or relative.suffix not in ('.gmdl','.dobj','.list','.t3x'):
  raise ValueError('Invalid Battle object resource')
 if not source.is_file() or digest_file(source)!=row['sha256']:
  raise ValueError('Battle object resource changed: '+str(source))
 overlay[relative.as_posix()]=source
# Playable Mii resources; no personal identity is cooked or packaged.
mii_race=ROOT/'build/mii-racers'
if (mii_race/'models/mii-complete.json').is_file():
 for src in sorted((mii_race/'models').glob('*')):
  if src.suffix not in ('.gmdl','.dskm','.list') or src.name.startswith('._'):continue
  if src.suffix=='.gmdl' and '_000' in src.stem and not src.stem.endswith('menu_000') and not src.name.startswith('award_'):continue
  overlay['models/'+src.name]=src
  if src.suffix=='.gmdl':
   data=src.read_bytes();v=struct.unpack_from('<I',data,4)[0];ns,nt=struct.unpack_from('<II',data,16);start=24+ns*GMDL_SUBMESH_BYTES[v]
   for i in range(nt):
    name=data[start+64*i:start+64*(i+1)].split(b'\0')[0].decode()
    overlay['textures/'+name+'.t3x']=mii_race/'textures'/(name+'.t3x')
# Index once: the expanded world mip overlay must not scan ~21,000 source
# paths again for every replacement (or repeatedly construct the same Path).
by_relative={}
for src,rel in files.items():by_relative.setdefault(rel.as_posix(),[]).append(src)
for rel,src in overlay.items():
 for old in by_relative.get(rel,()):del files[old]
 files[src]=Path(rel)
import sys
# Indexed archives (tools/pack_menu_assets.py, tools/pack_race_assets.py): the
# runtime reads packed assets with one open handle; loose files remain as a
# fallback for anything not packed.
for archive in ('menu.pak','race.pak'):
 if (assets/archive).is_file():files[assets/archive]=Path(archive)
print(json.dumps({str(rel):str(src) for src,rel in files.items()},indent=2))
