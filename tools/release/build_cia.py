#!/usr/bin/env python3
"""Mario Kart Wii 3DS: build the all-in-one CIA from your own disc.

    python tools/release/build_cia.py "Mario Kart Wii (USA).iso"

Stages, each skipped when its outputs already exist (rerun to resume):
  tools    pinned makerom and bannertool; devkitPro, Python packages, Node
  sources  pinned Kinoko, noclip, gclib and citro3d with this tree's patches
  disc     USA Mario Kart Wii (RMCE01) ISO or WBFS; extracted files must match
           tools/release/usa-disc.json
  cook     every asset, from the disc, in dependency order (COOK_STEPS)
  compile  tools/build.py
  package  tools/package_sd.py --no-zip, then the CIA (package_cia.py)

Only pinned public tools and sources are downloaded. The disc and everything
made from it stay on this machine; nothing from it is in this repository.
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, threading, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).parent
sys.path[:0]=[str(ROOT/'tools'),str(HERE)]
SD='sd-card/3ds/mkw-3ds'
DISC_MANIFEST=json.loads((HERE/'usa-disc.json').read_text())
MENU_ARCHIVES=['MenuSingle','MenuSingle_U','MenuOther','MenuOther_U','Title','Title_U','Award','Award_U','Globe',
               'Channel','Channel_U','Present','Present_U']
MENU_MODEL_ARCHIVES=['Driver','BackModel','MiiBody']
# Offline exporters, bundled one each (tools/bundle_cooker.mjs).
EXPORTERS=['export_brres','export_driver','export_joints','export_star','export_award_scene','export_blast',
           'export_item_box_uv','export_mii_photo']
# Courses whose materials Redux re-converts (docs/MANUAL-RESOURCE-STAGES.md).
COURSE_OVERLAY_IDS=['1','5','7','8','10','15','19','27','29','31']
COURSE_OBJECT_OUTPUTS=[SD+f'/models/course_{course_id:02d}_visuals.list' for course_id in range(32)]
HOST_EXE='.exe' if os.name=='nt' else ''

# (name, command, outputs that mean the step is done). {py} is this Python,
# {disc} the disc image, {jobs} the worker count. Order is dependency order.
# Intermediates no later step reads, deleted (fresh trees only) once the
# named step succeeds: several GB the player's disk never needs at once.
PRUNE_AFTER={'course material overlay':['build/course-exports','build/object-exports'],
             'full roster':['assets/garage-parts']}

# The stages follow docs/MANUAL-RESOURCE-STAGES.md (Epic's Redux), with the
# batch converters for courses, drivers, vehicles and course lighting kept,
# and each of config/generated-inputs.json's headers produced by a step.
COOK_STEPS=[
 ('Node packages',['npm.cmd' if os.name=='nt' else 'npm','ci','--omit=dev','--no-audit','--no-fund'],['node_modules/.package-lock.json']),
 ('BRRES exporters',[['node','tools/bundle_cooker.mjs',f'tools/{e}.ts'] for e in EXPORTERS],[f'build/{e}.mjs' for e in EXPORTERS]),
 # The game module and program, read as data (menu palettes, layouts, animation
 # tables, the retail default Miis, runtime tables).
 ('game module',['{py}','tools/extract_disc.py','{disc}','data/extracted','--file','rel/StaticR.rel','--to','build/itembox-retail.rel'],['build/itembox-retail.rel']),
 ('game program',['{py}','tools/extract_disc.py','{disc}','data/extracted','--dol','--to','build/original/main.dol'],['build/original/main.dol']),
 ('menus, movies and sound',['{py}','tools/extract_disc.py','{disc}','data/extracted','--prefix','Scene/','--prefix','thp/','--prefix','sound/','--prefix','Boot/'],['data/extracted/Scene/UI/Title.szs','data/extracted/sound/revo_kart.brsar','data/extracted/thp/title','data/extracted/Boot/Strap/us/English.szs']),
 # Every course model conversion (the Coconut Mall sky first) reads this policy.
 ('finish-line source policy',['{py}','tools/generate_startline_policy.py'],['tools/course_startline_faces.json']),
 # Coconut Mall's sky predates the course importer, which reuses it (README.txt).
 ('Coconut Mall archive',['{py}','tools/unpack_szs.py','data/extracted/Race/Course/shopping_course.szs','assets/coconut-mall'],['assets/coconut-mall/vrcorn_model.brres']),
 ('Coconut Mall sky export',['node','build/export_brres.mjs','assets/coconut-mall/vrcorn_model.brres','assets/cooked-coconut-sky'],['assets/cooked-coconut-sky/model.json']),
 ('Coconut Mall sky',['{py}','tools/cook_model.py','assets/cooked-coconut-sky',SD,'--prefix','coconut_sky'],[SD+'/models/coconut_sky.list']),
 # The disc's Wii Mii parts (RFLRes01.arc) and its six retail default Miis.
 ('Mii parts',[['{py}','tools/extract_disc.py','{disc}','data/extracted','--file','contents/RFLRes01.arc','--to','data/extracted/contents/RFLRes01.arc'],
               ['{py}','tools/unpack_szs.py','data/extracted/contents/RFLRes01.arc','assets/rfl'],
               ['{py}','tools/cook_mii_bank.py','assets/rfl/RFL_Res.dat',SD+'/mii/parts.mpr']],[SD+'/mii/parts.mpr']),
 ('retail default Miis',[['{py}','tools/host_tools.py','mii-head'],
                         ['{py}','tools/cook_default_mii_portraits.py','build/original/main.dol',SD+'/mii/parts.mpr',
                          'build/host-tools/export_mii_head'+HOST_EXE,'build/mii-parts/retail-defaults']],
  ['build/mii-parts/retail-defaults/default-faces-atlas.png']),
 ('runtime catalog metadata',['{py}','tools/generate_runtime_tables.py','catalog'],['include/mkw/course_catalog.hpp']),
 ('native Mii icon body',['{py}','tools/generate_native_icon.py'],['include/mkw/native_mii_icon_body.hpp']),
 ('race courses and arenas',['{py}','tools/import_courses.py','--jobs','{jobs}'],[SD+'/models/course-catalog.json']),
 ('course objects',['{py}','tools/cook_course_objects.py','--jobs','{jobs}','--ids',*[str(i) for i in range(32)]],COURSE_OBJECT_OUTPUTS),
 ('Coconut Mall escalators and palms',['{py}','tools/cook_coconut_objects.py'],[SD+'/models/coconut_escalator_000.gmdl',SD+'/models/coconut_palm_000.gmdl']),
 ('course lighting',['{py}','tools/lighting/recook_courses.py','--jobs','{jobs}','--no-pack'],[]),
 ('course lighting tables',['{py}','tools/cook_course_lighting.py'],['include/mkw/course_lighting.hpp']),
 ('course bloom tables',['{py}','tools/cook_course_bloom.py'],['include/mkw/course_bloom.hpp']),
 ('course material overlay',['{py}','tools/cook_course_overlay.py','--ids',*COURSE_OVERLAY_IDS],['build/course-materials/overlay-receipt.json']),
 ('drivers',['{py}','tools/cook_roster.py','{disc}',SD],[SD+'/models/roster.json']),
 ('karts and bikes',['{py}','tools/cook_garage.py','{disc}',SD],[SD+'/models/garage.json']),
 ('full roster',['{py}','tools/cook_full_roster.py','--jobs','{jobs}'],['build/roster']),
 ('driver LOD',['{py}','tools/cook_racer_lod.py','--output',SD,'--work','build/racer-lod-cook'],[SD+'/models/mario_mdf_kart_driver_lod.gmdl']),
 ('vehicle LOD',['{py}','tools/cook_vehicle_lod.py','--disc','{disc}','--output',SD,'--work','build/vehicle-lod-cook'],[SD+'/models/mario_mdf_kart_body_lod.gmdl','include/mkw/vehicle_lod_layout.hpp']),
 ('driver effect roots',['{py}','tools/cook_driver_effects.py','--jobs','{jobs}'],['build/driver-effects/receipt.json']),
 ('team vehicle colors',['{py}','tools/cook_team_vehicles.py','{disc}','--jobs','{jobs}'],['build/team-vehicles/team-vehicles.json']),
 ('vehicle shadows',['{py}','tools/cook_vehicle_shadows.py','--jobs','{jobs}'],['build/vehicle-shadows/models/vehicle-shadows.json']),
 ('vehicle rest poses',['{py}','tools/cook_vehicle_rest.py'],['include/mkw/vehicle_rest_data.hpp']),
 ('driver look data',['{py}','tools/cook_driver_look.py'],['include/mkw/driver_look_data.hpp']),
 ('kart effects',['{py}','tools/cook_effects.py','assets/common/Effect/RKRace.breft',SD+'/textures'],[SD+'/textures/drift-effects.bin']),
 ('bike effects',['{py}','tools/cook_effects.py','assets/common/Effect/RKRace.breft',SD+'/textures','--bike'],[SD+'/textures/drift-effects-bike.bin']),
 # The item box model comes from Luigi Circuit's archive, unpacked where the item cook expects it.
 ('Luigi Circuit archive',['{py}','tools/unpack_szs.py','data/extracted/Race/Course/beginner_course.szs','assets/luigi'],['assets/luigi/itembox.brres']),
 ('items and HUD',['{py}','tools/cook_race_items.py',SD,'--disc','{disc}'],[SD+'/models/race-items.json']),
 ('race HUD layout',['{py}','tools/cook_race_hud.py'],['include/mkw/race_hud_layout.hpp']),
 ('world item lighting',['{py}','tools/cook_item_lighting.py'],['build/item-lighting/receipt.json']),
 ('item shadows',['{py}','tools/cook_item_shadows.py'],['build/item-shadows/models/item-shadows.json']),
 ('race effects',['{py}','tools/cook_race_effects.py','--course-only','--output','build/vfx/textures'],['build/vfx/textures/course-effects.json','include/mkw/cannon_effect_assets.hpp']),
 ('blast effects',['{py}','tools/cook_blast.py'],['build/vfx/models/bomb_blast.gmdl']),
 ('Lakitu',['{py}','tools/cook_lakitu.py'],['build/vfx/models/lakitu_body.dskm','build/vfx/models/lakitu_shadow.gmdl']),
 ('name balloons',['{py}','tools/cook_race_balloon.py'],['build/vfx/textures/hud_balloon_line.t3x']),
 # Grand Prix award ceremonies and the GP grade table.
 ('ceremony source assets',['{py}','tools/prepare_award_assets.py','{disc}'],['build/award-source/award-sources.json']),
 ('ceremony scene bindings',['node','build/export_award_scene.mjs','build/award-source'],['build/award-source/scene-bindings.json']),
 ('ceremony cameras',['{py}','tools/cook_award_cameras.py'],['include/mkw/award_camera_data.hpp']),
 ('GP reference times',['{py}','tools/cook_gp_rank.py'],['include/mkw/gp_rank_times.hpp']),
 ('ceremony models and animation',['{py}','tools/cook_award_models.py','--jobs','{jobs}'],['build/award-models/award-models.json','include/mkw/award_model_assets.hpp']),
 ('ceremony environment',['{py}','tools/cook_award_environment.py'],['include/mkw/award_environment_data.hpp']),
 ('ceremony drive',['{py}','tools/cook_award_drive.py'],['include/mkw/award_drive_data.hpp']),
 ('ceremony effects',['{py}','tools/cook_award_effects.py'],['include/mkw/award_effect_anchors.hpp']),
 ('ceremony lighting',['{py}','tools/cook_award_lighting.py'],['include/mkw/award_lighting.hpp']),
 # Menu layouts and models, unpacked where the menu cooks read them.
 ('menu archives',[['{py}','tools/unpack_szs.py',f'data/extracted/Scene/UI/{a}.szs',f'assets/menu-ui/{a}'] for a in MENU_ARCHIVES],
  ['assets/menu-ui/Title/title/timg/tt_title_screen_mario2.tpl','assets/menu-ui/Present_U/message/Common.bmg']),
 ('race flow results and awards',['{py}','tools/cook_race_flow.py'],['build/race-flow/textures/flow_wipe.t3x','build/race-flow/textures/flow_award_font.t3x','build/race-flow/race-flow.json']),
 ('audio',['{py}','tools/cook_audio.py'],['build/audio/audio/manifest.json']),
 ('Wiggler effect joints',['{py}','tools/cook_hanachan_joints.py'],['include/mkw/hanachan_joint_assets.hpp']),
 ('Pokey separation joints',['{py}','tools/cook_pokey_joints.py'],['include/mkw/pokey_joint_assets.hpp']),
 ('object animation sounds',['{py}','tools/cook_object_sounds.py'],['include/mkw/object_sound_assets.hpp','build/object-sounds.json']),
 ('menu model archives',[['{py}','tools/unpack_szs.py',f'data/extracted/Scene/Model/{a}.szs',f'assets/menu-models/{a}'] for a in MENU_MODEL_ARCHIVES],
  [f'assets/menu-models/{a}' for a in MENU_MODEL_ARCHIVES]),
 ('Battle menu labels',['{py}','tools/cook_battle_menu_text.py'],['build/battle-menu-assets/receipt.json','include/mkw/battle_menu_text.inc']),
 ('menu interface',['{py}','tools/cook_menu_ui.py'],[SD+'/models/menu-ui.json']),
 # Run separately after the core menu conversion (docs/MANUAL-BUILD.md).
 ('menu names',['{py}','tools/cook_menu_names.py'],['include/mkw/menu_name_glyphs.hpp','include/mkw/license_name_characters.hpp']),
 ('menu footer',['{py}','tools/cook_menu_footer.py'],['include/mkw/menu_footer_text.inc']),
 ('cup record icons',['{py}','tools/cook_cup_record_icons.py'],['include/mkw/cup_record_icons.hpp']),
 ('menu record text',['{py}','tools/cook_menu_record_text.py'],['include/mkw/menu_record_text.inc']),
 ('menu text styles',['{py}','tools/cook_menu_styles.py'],['include/mkw/menu_text_styles.hpp']),
 ('menu animations',['{py}','tools/cook_menu_animations.py'],['include/mkw/menu_animations.hpp']),
 ('menu unlock messages',['{py}','tools/cook_menu_unlocks.py'],['include/mkw/menu_unlock_texts.hpp']),
 ('Mirror unlock',['{py}','tools/cook_unlock_mirror.py'],[]),
 ('menu movie chapters',['{py}','tools/cook_menu_movie_chapters.py'],['include/mkw/menu_movie_chapters.hpp']),
 ('menu lighting',['{py}','tools/cook_menu_lighting.py'],['include/mkw/menu_lighting.hpp','build/menu-lighting.json']),
 ('menu models',['{py}','tools/cook_menu_models.py','--machines','--jobs','{jobs}'],[SD+'/models/menu-models.json']),
 ('menu movies',['{py}','tools/cook_menu_movies.py','--jobs','{jobs}'],[SD+'/models/menu-movies.json',SD+'/movies/title.wav']),
 ('menu sprite scales',['{py}','tools/host_tools.py','sprite-scales'],['build/host-tools/sprite-scales.json']),
 ('menu texture fitting',['{py}','tools/optimize_menu_textures.py','--jobs','{jobs}'],[SD+'/models/menu-texture-fit.json']),
 ('menu audio',['{py}','tools/cook_menu_audio.py'],[SD+'/models/menu-audio.json']),
 ('race bottom screen',['{py}','tools/cook_race_map.py','--jobs','{jobs}'],['build/race-map/textures/race_bottom_ui.t3x']),
 ('Luma model',['{py}','tools/cook_luma.py'],['build/vfx/luma-receipt.json']),
 ('firebar phase stream',['{py}','tools/prepare_firebar_stream.py'],['build/course-water-feedback/textures/firebar-indirect.ftan']),
 ('star color table',['node','build/export_star.mjs','assets/common/driver.brres','include/mkw/star_colors.hpp'],['include/mkw/star_colors.hpp']),
 ('menu Mii bodies',['{py}','tools/cook_menu_mii_bodies.py'],['build/menu-mii-bodies/menu-mii-bodies.json','include/mkw/menu_mii_materials.hpp']),
 ('race intro UI',['{py}','tools/cook_race_intro.py'],['build/race-intro/receipt.json','include/mkw/race_intro_layout.hpp']),
 # Offline Battle presentation.
 ('Battle balloons',['{py}','tools/cook_battle_balloons.py'],['build/battle-balloon-assets/receipt.json']),
 ('Battle coins',['{py}','tools/cook_battle_coins.py'],['build/battle-coin-assets/receipt.json']),
 ('Battle objects',['{py}','tools/cook_battle_objects.py'],['build/battle-object-assets/receipt.json']),
 ('Battle screens',['{py}','tools/cook_battle_screens.py'],['build/battle-screen-assets/receipt.json']),
 ('Battle HUD',['{py}','tools/cook_battle_hud.py'],['build/battle-hud-assets/receipt.json','include/mkw/battle_hud_assets.hpp','include/mkw/battle_map_assets.hpp']),
 ('Battle HUD motion',['{py}','tools/cook_battle_hud_motion.py'],['build/battle-hud-motion/receipt.json','include/mkw/battle_hud_motion_assets.hpp']),
 ('Battle bottom scores',['{py}','tools/cook_battle_bottom_scores.py'],['build/battle-bottom-scores/receipt.json','include/mkw/battle_bottom_scores.hpp']),
 ('Battle results',['{py}','tools/cook_battle_results.py'],['build/battle-results-assets/receipt.json','include/mkw/battle_result_assets.hpp']),
 ('Coin HUD stacks',['{py}','tools/cook_coin_hud_stack.py'],['build/battle-coin-hud-assets/receipt.json']),
 ('Balloon effects',['{py}','tools/cook_race_effects.py','--balloon-only','--output','build/battle-balloon-effects/textures'],['build/battle-balloon-effects/textures/balloon-effects.json']),
 ('Coin effects',['{py}','tools/cook_race_effects.py','--coin-only','--output','build/battle-coin-effects/textures'],['build/battle-coin-effects/textures/coin-effects.json']),
 ('water mipmaps',['{py}','tools/cook_water_mipmaps.py'],['build/water-mipmaps/receipt.json']),
 ('world mipmaps',['{py}','tools/cook_world_mipmaps.py','--jobs','{jobs}'],['build/world-mipmaps/receipt.json']),
 ('Blooper model',['{py}','tools/cook_blooper.py'],['build/blooper/receipt.json']),
 # Staff credits (normal and full endings).
 ('Staff UI',['{py}','tools/cook_staff_ui.py','--retail-rel','build/itembox-retail.rel','--output','build/staff'],['build/staff/models/staff-ui.json','include/mkw/staff_ui_data.hpp']),
 ('Staff movies',['{py}','tools/cook_staff_movies.py','--output','build/staff'],['build/staff/movies/staff_true.source.json']),
 ('Staff crowd joints',['{py}','tools/generate_staff_crowd.py'],['include/mkw/staff_crowd_data.hpp']),
 ('Staff camera',['{py}','tools/cook_staff_camera.py'],['include/mkw/staff_camera_data.hpp']),
 ('dynamic map sprites',['{py}','tools/cook_map_objects.py'],['include/mkw/map_object_assets.hpp']),
 ('runtime voice tables',['{py}','tools/generate_runtime_tables.py','audio'],['include/mkw/race_voice_tables.hpp']),
 ('runtime trig table',['{py}','tools/generate_runtime_tables.py','math'],['include/mkw/nw4r_sin_table.hpp']),
 ('runtime Mii billboards',['{py}','tools/generate_runtime_tables.py','billboards'],['include/mkw/course_mii_billboards.hpp']),
 ('road depth policy',['{py}','tools/generate_road_policy.py'],['include/mkw/road_sampler_ranges.hpp']),
 ('course draw phases',['{py}','tools/generate_course_phases.py'],['include/mkw/course_phase_ranges.hpp']),
 ('Battle legacy material migration',['{py}','tools/cook_battle_course_feedback.py'],['build/battle-course-feedback/receipt.json']),
 # Mii drivers.
 ('Mii race models',['{py}','tools/cook_mii_racers.py','{disc}','build/mii-racers','--jobs','{jobs}'],['build/mii-racers/models/mii-garage.json']),
 ('Mii menu, team and metadata',['{py}','tools/finish_mii_racers.py','{disc}','build/mii-racers','--jobs','{jobs}'],['build/mii-racers/models/mii-complete.json','include/mkw/mii_garage.hpp']),
 ('Mii award bodies',['{py}','tools/cook_mii_awards.py','build/mii-racers'],['build/mii-racers/models/mii-awards.json','include/mkw/mii_award_materials.hpp']),
 ('Mii voice tables',['{py}','tools/cook_mii_voice.py','build/itembox-retail.rel'],['include/mkw/mii_voice.hpp']),
 # The menu archive indexes the menu files; the race archives are packed with
 # the finished game (tools/package_sd.py), which their file map lists.
 ('menu archive',['{py}','tools/pack_menu_assets.py'],[SD+'/menu.pak']),
]

def log(*a):print('[build_cia]',*a,flush=True)

def run(cmd,**kw):
    log('$',' '.join(str(c) for c in cmd))
    # PYTHONUTF8: text files are UTF-8 on every OS (Windows would default to cp1252).
    # tools/release/hooks: on macOS, directory listings skip '._' files (exFAT/FAT drives).
    env=dict(os.environ,PYTHONUTF8='1',PYTHONPATH=os.pathsep.join(p for p in (str(ROOT/'tools/release/hooks'),str(ROOT/'vendor/gclib'),os.environ.get('PYTHONPATH')) if p))
    # Some steps print nothing for a long time: say the build is still running (#100).
    started=time.monotonic();finished=threading.Event()
    def heartbeat():
        while not finished.wait(300):log(f'still running this step ({int((time.monotonic()-started)/60)} minutes so far)')
    threading.Thread(target=heartbeat,daemon=True).start()
    try:subprocess.run([str(c) for c in cmd],cwd=ROOT,check=True,env=env,**kw)
    finally:finished.set()

def shared_checkout():
    """Development trees link data/, assets/ and sd-card/ to a shared checkout."""
    return [d for d in ('data','assets','sd-card') if (ROOT/d).is_symlink()]

def missing_outputs(root, outputs):
    """Return outputs that are missing, empty files, or empty directories."""
    missing=[]
    for output in outputs:
        path=root/output
        if path.is_file():
            if path.stat().st_size==0:missing.append(output)
        elif path.is_dir():
            if not any(p.is_file() and p.stat().st_size>0 for p in path.rglob('*')):
                missing.append(output)
        else:
            missing.append(output)
    return missing

def keep_console_running():
    # Clicking inside a Windows console window starts a text selection, which
    # freezes the build until it is cleared. Turn that selection mode off (#100).
    if os.name!='nt':return
    import ctypes
    kernel32=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel32.GetStdHandle.restype=ctypes.c_void_p
    kernel32.GetConsoleMode.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint32)]
    kernel32.SetConsoleMode.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    console=kernel32.GetStdHandle(-10);mode=ctypes.c_uint32()
    if kernel32.GetConsoleMode(console,ctypes.byref(mode)):kernel32.SetConsoleMode(console,(mode.value|0x80)&~0x40)

def stage_tools():
    if not ((3,11) <= sys.version_info[:2] < (3,15)):
        raise SystemExit(f'Python 3.11 through 3.14 is required; running {sys.version.split()[0]} at {sys.executable}.')
    from bootstrap_tools import tools
    makerom,bannertool=tools();log('makerom',makerom);log('bannertool',bannertool)
    from devkitpro import tool, root as devkitpro_root
    dkp=devkitpro_root()
    for need in (dkp/'devkitARM',dkp/'libctru',tool('tex3ds')):
        if not need.exists():raise SystemExit(f'Missing {need}: install devkitPro 3ds-dev (https://devkitpro.org/wiki/Getting_Started)')
    # The bundled Citro3D needs a current libctru (CTR_ALIGN, #62).
    if b'CTR_ALIGN' not in (dkp/'libctru/include/3ds/types.h').read_bytes():
        raise SystemExit('devkitPro is out of date. Update it (in the devkitPro MSYS2 window or a terminal: dkp-pacman -Syu), then run the builder again. '
                         'If that is not available, uninstall devkitPro and install the current version: https://devkitpro.org/wiki/Getting_Started')
    # Some of the Windows build tools can't open a path with non-English letters (#89).
    if os.name=='nt' and not str(ROOT).isascii():
        raise SystemExit(f'The builder folder\'s path has letters the build tools can\'t read ({ROOT}). Move the '
                         'MKW-3DS-Builder folder somewhere with plain English letters (for example C:\\MKW-3DS-Builder) and run it again.')
    # OneDrive and similar sync folders lock files while uploading them (#111).
    if any(part.lower().startswith(('onedrive','icloud','dropbox','google drive')) for part in ROOT.parts):
        raise SystemExit(f'The builder is inside a synced folder ({ROOT}). Move the MKW-3DS-Builder folder somewhere '
                         'that is not synced (for example C:\\MKW-3DS-Builder) and run it again.')
    missing=[m for m in ('numpy','PIL','Crypto') if not __import__('importlib.util').util.find_spec(m)]
    if missing:raise SystemExit(f'Missing Python packages {", ".join(missing)} in {sys.executable}: run "{sys.executable}" -m pip install -r requirements.txt')
    if not shutil.which('node'):raise SystemExit('Missing Node.js (https://nodejs.org), used by the model exporters')
    # The two small helper programs are compiled for this computer at step 11:
    # say now if there is no compiler, not hours into the build (#116).
    from host_tools import compiler
    compiler()

def stage_sources():
    # Always run: it fetches what is missing and brings a reused builder folder's
    # Kinoko checkout to this version's patch (nothing is downloaded if current).
    run([sys.executable,'tools/bootstrap.py'])

def stage_disc(disc):
    if disc and not Path(disc).is_file():raise SystemExit(f'No disc image at: {disc}')
    receipt=ROOT/'data/extracted/extraction-receipt.json'
    shared_data=(ROOT/'data').is_symlink() or (ROOT/'data/extracted').is_symlink()
    # An extraction that failed the check (a Europe disc, a modified or NKit
    # dump) is replaced when a different image is given, e.g. the USA ISO.
    # A verified one is kept: later steps read it even if the ISO has moved.
    if disc and receipt.is_file():
        old=json.loads(receipt.read_text())
        got={f['name']:f['sha256'] for f in old.get('files',[])}
        verified=all(old['identity'].get(k)==v for k,v in DISC_MANIFEST['identity'].items()) and all(got.get(n)==h for n,h in DISC_MANIFEST['files'].items())
        if not verified and (old['identity'].get('source')!=str(disc) or old['identity'].get('source_bytes')!=Path(disc).stat().st_size):
            if shared_data:raise SystemExit('The shared data directory contains an unsupported disc extraction. Build from a fresh clone to avoid replacing shared data.')
            log('replacing the previous, unsupported disc extraction')
            shutil.rmtree(ROOT/'data/extracted')
    if not receipt.is_file():
        if shared_data:raise SystemExit('The shared data directory has no verified disc extraction. Build from a fresh clone to avoid writing to shared data.')
        if not disc:raise SystemExit('Give the path to your Mario Kart Wii (USA) ISO or WBFS')
        # NKit images mark themselves "NKIT" at 0x200; their trimmed data fails the checks.
        with open(disc,'rb') as f:f.seek(0x200);nkit=f.read(4)==b'NKIT'
        if nkit:raise SystemExit('This is an NKit image. Convert it to a normal ISO in Dolphin first (right-click the game, Convert File, ISO).')
        log('unpacking and checking your disc: this takes a few minutes with no output')
        run([sys.executable,'tools/extract_disc.py',disc,'data/extracted'])
    r=json.loads(receipt.read_text());ident=DISC_MANIFEST['identity']
    if any(r['identity'][k]!=v for k,v in ident.items()):
        game=r['identity']['game_id'];region={'RMCP01':'Europe (PAL)','RMCJ01':'Japan','RMCK01':'Korea'}.get(game)
        what=f"Mario Kart Wii {region} ({game})" if region else f"disc {game} revision {r['identity']['revision']}"
        raise SystemExit(f"This is {what}. Only the USA version of Mario Kart Wii (RMCE01) works for now.")
    got={f['name']:f['sha256'] for f in r['files']}
    bad=[n for n,h in DISC_MANIFEST['files'].items() if got.get(n)!=h]
    if bad:raise SystemExit(f'{len(bad)} extracted files differ from an unmodified USA disc (first: {bad[0]}). Use a clean dump.')
    log(f"disc verified: {ident['game_id']} revision {ident['revision']}, {len(got)} files")

def check_game_code(disc):
    """The menus read colours from the disc's game code (rel/StaticR.rel),
    which the 53-file disc check does not cover. A copy left by an attempt with
    another image is extracted again; a modified one (a Wiimmfi or otherwise
    patched ISO) stops here with a clear message instead of deep in step 27."""
    from verify_menu_retail import EXPECTED_HASH
    rel=ROOT/'build/itembox-retail.rel'
    def ok():return rel.is_file() and hashlib.sha256(rel.read_bytes()).hexdigest()==EXPECTED_HASH
    if ok():return
    if not disc:
        raise SystemExit('Missing or unsupported build/itembox-retail.rel. Rerun the builder with an unmodified Mario Kart Wii USA (RMCE01) disc image to extract and verify rel/StaticR.rel.')
    rel.unlink(missing_ok=True)
    run([sys.executable,'tools/extract_disc.py',disc,'data/extracted','--file','rel/StaticR.rel','--to',rel])
    if not ok():raise SystemExit('The extracted game code (rel/StaticR.rel) does not match the audited USA revision. Use an unmodified Mario Kart Wii USA (RMCE01) disc image; Python version is not the cause of this check.')

def scrub_appledouble():
    """Delete macOS '._' metadata files from the cooked trees. macOS writes
    one beside every file on volumes without extended attributes (exFAT, FAT),
    and the cookers' globs ('*.brlan', '*.brctr' ...) would read them as data."""
    if sys.platform!='darwin':return
    for top in ('data','assets','sd-card','build'):
        for dirpath,dirnames,filenames in os.walk(ROOT/top):
            for f in filenames:
                if f.startswith('._'):Path(dirpath,f).unlink(missing_ok=True)

# ---- Step fingerprints --------------------------------------------------------
# A step's done-stamp records a fingerprint of the code that produced it: the
# tool scripts it runs, the local modules they import, and the BRRES exporter
# sources when they call them. An update that changes that code re-runs the
# step (and, as before, every step after it). Line endings are normalised so a
# Windows checkout fingerprints the same as macOS.
_IMPORT=re.compile(r'^\s*(?:from\s+([A-Za-z_]\w*)\s+import|import\s+([A-Za-z_][\w, ]*))',re.M)
_TOOL_DIRS=('tools','tools/release','tools/lighting')
def _local_module(name):
    for d in _TOOL_DIRS:
        f=ROOT/d/(name+'.py')
        if f.is_file():return f
    return None
def step_fingerprint(cmd,windows_npm=False):
    commands=cmd if cmd and isinstance(cmd[0],list) else [cmd]
    # Only checked-in sources count: build/export_*.mjs are generated bundles
    # (machine-specific), represented by the tools/*.ts they are built from.
    files=set();queue=[ROOT/a for c in commands for a in c if isinstance(a,str) and a.startswith('tools/') and a.endswith(('.py','.mjs')) and (ROOT/a).is_file()]
    # cook_menu_ui launches the fitter as a subprocess; player builds size the
    # menu textures from the shipped measurements (tools/optimize_menu_textures.py),
    # so the menu code itself does not count: menu code changes stay quick updates.
    if any('tools/cook_menu_ui.py' in c for c in commands):
        queue += [ROOT/'tools/optimize_menu_textures.py', ROOT/'tools/menu_sprite_scales.json']
    exporters=any('build/export_' in a or 'bundle_cooker' in a for c in commands for a in c if isinstance(a,str))
    if any(a.startswith('npm') for c in commands for a in c if isinstance(a,str)):queue.append(ROOT/'package-lock.json')
    while queue:
        f=queue.pop()
        if f in files:continue
        files.add(f)
        if f.suffix!='.py':continue
        text=f.read_text(encoding='utf-8',errors='replace')
        exporters|='.mjs' in text
        # Exporters kept in tools/ceremony/ (bundled by tools/ceremony/bundle.mjs).
        if 'tools/ceremony/' in text:files|={p for p in (ROOT/'tools/ceremony').glob('*') if p.is_file()}
        for a,b in _IMPORT.findall(text):
            for name in ([a] if a else [n.strip().split(' ')[0] for n in b.split(',')]):
                m=_local_module(name)
                if m:queue.append(m)
    if exporters:files|={*(ROOT/'tools').glob('*.ts'),ROOT/'tools/bundle_cooker.mjs'}
    # npm is 'npm.cmd' on Windows: the same step, so it must not change the
    # fingerprint (0.5.0 and 0.5.1 let it: see legacy_windows_fingerprint).
    commands=[[('npm.cmd' if windows_npm else 'npm') if a in ('npm','npm.cmd') else a for a in c] for c in commands]
    digest=hashlib.sha256(json.dumps(commands).encode())
    for f in sorted(files,key=lambda f:f.relative_to(ROOT).as_posix()):
        digest.update(f.relative_to(ROOT).as_posix().encode()+b'\0'+f.read_bytes().replace(b'\r\n',b'\n'))
    return digest.hexdigest()

# The content stamp says which converted game files a build has: the update CIA
# is compiled with it (tools/build.py -> MKW_CONTENT), the builder writes it to
# the SD folder (content.txt), and the game refuses files with another stamp.
# It is a number kept by hand (tools/release/content_stamp.txt) and changes only
# when the converted files themselves change, so edits to the cook tools that
# leave their output as it was (a guard, a message, a refactor) never make
# players rebuild. recipe_fingerprint() hashes every conversion step and the
# packaging scripts; check_content_stamp() tells a maintainer when that recipe
# differs from the one last reviewed, to decide: output unchanged (record the
# new recipe) or changed (new stamp, and a rebuild release).
PACKAGE_SCRIPTS=('tools/package_sd.py','tools/package_standalone.py')
STAMP_FILE=Path(__file__).with_name('content_stamp.txt')

def recipe_fingerprint(windows_npm=False):
    digest=hashlib.sha256()
    for name,cmd,_ in COOK_STEPS:digest.update(f'{name}\0{step_fingerprint(cmd,windows_npm)}\n'.encode())
    for script in PACKAGE_SCRIPTS:digest.update((ROOT/script).read_bytes().replace(b'\r\n',b'\n'))
    return digest.hexdigest()[:32]

def _stamp_values():
    values=dict(line.split(None,1) for line in STAMP_FILE.read_text().splitlines() if line.strip() and not line.startswith('#'))
    return {k:v.strip() for k,v in values.items()}

def content_fingerprint():
    return _stamp_values()['stamp']

def legacy_windows_fingerprint():
    """The stamp Windows builders of 0.5.0 and 0.5.1 wrote (npm.cmd was part of
    the recipe then). Games accept it as well as the real one."""
    return _stamp_values()['windows_legacy']

def check_content_stamp():
    """(True, '') when the cook recipe is the one last reviewed against the stamp."""
    reviewed=_stamp_values()['recipe'];now=recipe_fingerprint()
    if now==reviewed:return True,''
    return False,(f'The cook recipe changed ({reviewed} -> {now}) since the content stamp '
        f'{content_fingerprint()} was last reviewed. If the converted files are unchanged, set "recipe {now}" in '
        f'tools/release/content_stamp.txt. If they changed, set a new "stamp": every player then has to rebuild.')

def stamp_done(stamp,name,fingerprint):
    if not stamp.is_file():return False
    text=stamp.read_text().strip()
    # A stamp from before fingerprints (builders before 0.3.5) cannot say what
    # it was cooked from: run the step again rather than mix old and new output.
    return text.startswith('fingerprint ') and text.split()[1]==fingerprint

# Names the retail sound archive uses with different case from the disc's own
# files (issue #18). Case-sensitive file systems (Linux) get a link under the
# referenced name; elsewhere it already resolves. Kept here, outside the cook
# tools, so the content stamp (and every player's quick updates) is unchanged.
DISC_CASE_ALIASES={'data/extracted/sound/strm/n_Snowboard32_f.brstm':'data/extracted/sound/strm/n_Snowboard32_F.brstm'}

def add_case_aliases():
    for alias,real in DISC_CASE_ALIASES.items():
        alias,real=ROOT/alias,ROOT/real
        if alias.exists() or not real.is_file():continue
        try:os.link(real,alias)
        except OSError:shutil.copyfile(real,alias)
        log(f'linked {alias.name} to {real.name} (case-sensitive file system)')

# Peak memory of one worker in steps that run several at once. The course
# maps render 1024-2048 px float images: about 0.9 GiB per worker, so a many-
# core PC with modest RAM ran out with one per core. The worker count is not
# part of any fingerprint, so this changes no stamp.
JOB_MEMORY={'race bottom screen':1.25*2**30}

def available_memory():
    """Free physical memory in bytes, or None when unknown."""
    try:
        if sys.platform=='win32':
            import ctypes
            class Status(ctypes.Structure):
                _fields_=[('length',ctypes.c_ulong),('load',ctypes.c_ulong),('total',ctypes.c_ulonglong),('avail',ctypes.c_ulonglong),
                          ('totalPage',ctypes.c_ulonglong),('availPage',ctypes.c_ulonglong),('totalVirtual',ctypes.c_ulonglong),
                          ('availVirtual',ctypes.c_ulonglong),('availExtended',ctypes.c_ulonglong)]
            st=Status();st.length=ctypes.sizeof(Status)
            return st.avail if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st)) else None
        if sys.platform=='darwin':
            # macOS reclaims cached memory freely: count most of the total.
            return int(int(subprocess.run(['sysctl','-n','hw.memsize'],capture_output=True,text=True).stdout)*0.6)
        for line in open('/proc/meminfo'):
            if line.startswith('MemAvailable:'):return int(line.split()[1])*1024
    except Exception:pass
    return None

def step_jobs(name,jobs):
    per=JOB_MEMORY.get(name);free=available_memory() if per else None
    if not free:return jobs
    fit=max(1,int(free*0.8//per))
    if fit<jobs:log(f'{name}: {fit} at a time ({free/2**30:.1f} GiB free memory)')
    return min(jobs,fit)

def stage_cook(disc,jobs,force,check=False):
    shared=shared_checkout();stamps=ROOT/'build/release/cook';stamps.mkdir(parents=True,exist_ok=True)
    rerun=force
    for name,cmd,outputs in COOK_STEPS:
        stamp=stamps/(name.replace(' ','-')+'.done');fingerprint=step_fingerprint(cmd)
        missing=missing_outputs(ROOT,outputs)
        present=bool(outputs) and not missing
        # A fresh tree trusts only success stamps (a failed step can leave its
        # outputs half written) and, once a step runs, re-runs every later step.
        # Development trees, cooked by hand, are judged by their outputs; so are
        # the Node packages, which a development tree links from elsewhere.
        if not outputs:
            done=stamp_done(stamp,name,fingerprint) and not rerun
        else:
            done=(present and (shared or name=='Node packages' or stamp_done(stamp,name,fingerprint))) and not rerun
        if done:log('cooked:',name);continue
        # The course lighting re-cook rewrites files in place: no output of its own.
        if shared and not outputs:log('cooked (development tree, assumed):',name);continue
        if check:raise SystemExit(f"Cook step '{name}' has not run")
        if shared:raise SystemExit(f"Cook step '{name}' is missing, but {', '.join(shared)} link to a shared checkout. Build from a fresh clone.")
        rerun=True
        commands=cmd if cmd and isinstance(cmd[0],list) else [cmd]
        if any('{disc}' in c for c in commands) and not disc:raise SystemExit(f"Cook step '{name}' needs the disc image path")
        start=time.monotonic()
        log(f'step {[n for n,*_ in COOK_STEPS].index(name)+1}/{len(COOK_STEPS)}: {name}')
        for c in commands:
            scrub_appledouble();add_case_aliases();run([str(a).replace('{py}',sys.executable).replace('{disc}',str(disc)).replace('{jobs}',str(step_jobs(name,jobs))) for a in c])
        missing=missing_outputs(ROOT,outputs)
        if missing:raise SystemExit(f"Cook step '{name}' did not produce valid outputs: {', '.join(missing)}")
        stamp.write_text(f'fingerprint {fingerprint}\n{time.monotonic()-start:.0f} s\n')
        for path in PRUNE_AFTER.get(name,()):shutil.rmtree(ROOT/path,ignore_errors=True)

def stage_compile(jobs):
    run([sys.executable,'tools/build.py','--jobs',str(jobs),'--release'])

SD_OUTPUT=ROOT/'build/release/sd-card'

def stage_package(version,cci,keep_stage,embed_assets=False):
    # Optional: the 3DS's own Mii resource, placed beside this builder, goes on the
    # SD card. Without it the game asks the console for its copy.
    if (ROOT/'CFL_Res.dat').is_file():run([sys.executable,'tools/stage_native_mii_resource.py','CFL_Res.dat'])
    run([sys.executable,'tools/package_sd.py','--no-zip'])
    import package_cia
    # The CIA reads the game files from the SD card unless asked to carry them
    # (a cartridge image for emulators always does).
    target=package_cia.package(cci,version,sd_assets=not (cci or embed_assets))
    # The staged files become the SD card copy for the update CIA:
    # build/release/sd-card/3ds/mkw-3ds plus its content stamp.
    if not cci:
        shutil.rmtree(SD_OUTPUT,ignore_errors=True)
        if keep_stage:shutil.copytree(ROOT/'build/playable',SD_OUTPUT)
        else:shutil.move(str(ROOT/'build/playable'),str(SD_OUTPUT))
        (SD_OUTPUT/'3ds/mkw-3ds/content.txt').write_text(content_fingerprint()+'\n')
        log('SD card files for update CIAs:',SD_OUTPUT/'3ds')
    elif not keep_stage:shutil.rmtree(ROOT/'build/playable',ignore_errors=True)
    return target

def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('disc',nargs='?',type=Path,help='your Mario Kart Wii (USA) ISO or WBFS (not needed once extracted and cooked)')
    # The HOME Menu refreshes a title's cached icon and banner only when its
    # version rises: bump tools/release/VERSION for every build you share.
    p.add_argument('--version',default=(Path(__file__).parent/'VERSION').read_text().strip())
    p.add_argument('--jobs',type=int,default=max(1,(os.cpu_count() or 2)-1))
    p.add_argument('--cci',action='store_true',help='also build a cartridge image for emulators')
    p.add_argument('--force-cook',action='store_true',help='re-run every cook step')
    p.add_argument('--skip-compile',action='store_true',help='package the existing build/mkw-3ds.elf')
    p.add_argument('--keep-stage',action='store_true',help='keep build/playable after packaging')
    p.add_argument('--embed-assets',action='store_true',help='put the game files in the CIA too (several GB; the SD copy is still made)')
    p.add_argument('--check',action='store_true',help='verify tools, disc and cooked assets, then stop')
    p.add_argument('--cook-only',action='store_true',help='stop after cooking')
    a=p.parse_args()
    keep_console_running();stage_tools();stage_sources();stage_disc(a.disc);check_game_code(a.disc);stage_cook(a.disc,a.jobs,a.force_cook,a.check)
    if a.check:log('check passed: ready to compile and package');return
    if a.cook_only:log('cook complete');return
    if not a.skip_compile:stage_compile(a.jobs)
    cia=stage_package(a.version,False,a.keep_stage or a.cci,a.embed_assets)
    if a.cci:stage_package(a.version,True,a.keep_stage)
    log('done:',cia)
    log('1. Copy the mkw-3ds folder from build/release/sd-card/3ds into the 3ds folder on your SD card (the game files). Updating? Copy the files inside it into the card\'s mkw-3ds folder; don\'t replace that folder (it holds your saves).')
    if a.embed_assets:log('2. Copy the CIA to your SD card and install it with FBI, or use FBI remote install.')
    else:log('2. Install the CIA with FBI (it is small: the game reads its files from the SD card).')

if __name__=='__main__':main()
