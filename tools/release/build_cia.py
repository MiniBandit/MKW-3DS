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
import argparse, hashlib, json, os, re, shutil, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).parent
sys.path[:0]=[str(ROOT/'tools'),str(HERE)]
SD='sd-card/3ds/mkw-3ds'
DISC_MANIFEST=json.loads((HERE/'usa-disc.json').read_text())
MENU_ARCHIVES=['MenuSingle','MenuSingle_U','MenuOther','MenuOther_U','Title','Title_U','Award','Award_U','Globe']
COURSE_OVERLAY_IDS=['1','5','7','8','10','15','19','27','29','31']

# (name, command, outputs that mean the step is done). {py} is this Python,
# {disc} the disc image, {jobs} the worker count. Order is dependency order.
# Intermediates no later step reads, deleted (fresh trees only) once the
# named step succeeds: several GB the player's disk never needs at once.
PRUNE_AFTER={'course material overlay':['build/course-exports','build/object-exports'],
             'full roster':['assets/garage-parts']}

COOK_STEPS=[
 ('Node packages',['npm.cmd' if os.name=='nt' else 'npm','ci','--no-audit','--no-fund'],['node_modules/.package-lock.json']),
 ('BRRES exporters',['node','tools/bundle_cooker.mjs'],['build/export_brres.mjs']),
 # The game module, read as data (menu palettes, layouts, animation tables).
 ('game module',['{py}','tools/extract_disc.py','{disc}','data/extracted','--file','rel/StaticR.rel','--to','build/itembox-retail.rel'],['build/itembox-retail.rel']),
 ('menus, movies and sound',['{py}','tools/extract_disc.py','{disc}','data/extracted','--prefix','Scene/','--prefix','thp/','--prefix','sound/','--prefix','Boot/'],['data/extracted/Scene/UI/Title.szs','data/extracted/sound/revo_kart.brsar','data/extracted/thp/title','data/extracted/Boot/Strap/us/English.szs']),
 # Coconut Mall's sky predates the course importer, which reuses it (README.txt).
 ('Coconut Mall archive',['{py}','tools/unpack_szs.py','data/extracted/Race/Course/shopping_course.szs','assets/coconut-mall'],['assets/coconut-mall/vrcorn_model.brres']),
 ('Coconut Mall sky export',['node','build/export_brres.mjs','assets/coconut-mall/vrcorn_model.brres','assets/cooked-coconut-sky'],['assets/cooked-coconut-sky/model.json']),
 ('Coconut Mall sky',['{py}','tools/cook_model.py','assets/cooked-coconut-sky',SD,'--prefix','coconut_sky'],[SD+'/models/coconut_sky.list']),
 ('race courses and arenas',['{py}','tools/import_courses.py','--jobs','{jobs}'],[SD+'/models/course-catalog.json']),
 ('course objects',['{py}','tools/cook_course_objects.py','--jobs','{jobs}'],[SD+'/models/course_08_visuals.list']),
 ('Coconut Mall escalators and palms',['{py}','tools/cook_coconut_objects.py'],[SD+'/models/coconut_escalator_000.gmdl',SD+'/models/coconut_palm_000.gmdl']),
 ('course lighting',['{py}','tools/lighting/recook_courses.py','--jobs','{jobs}','--no-pack'],[]),
 ('course material overlay',['{py}','tools/cook_course_overlay.py','--ids',*COURSE_OVERLAY_IDS,'--objects'],['build/course-materials/models']),
 ('water mipmaps',['{py}','tools/cook_water_mipmaps.py'],['build/water-mipmaps/receipt.json']),
 ('drivers',['{py}','tools/cook_roster.py','{disc}',SD],[SD+'/models/roster.json']),
 ('karts and bikes',['{py}','tools/cook_garage.py','{disc}',SD],[SD+'/models/garage.json']),
 ('full roster',['{py}','tools/cook_full_roster.py','--jobs','{jobs}'],['build/roster']),
 ('race racer lighting',['{py}','tools/cook_racer_lighting.py'],['build/roster/models/garage.json']),
 ('vehicle shadows',['{py}','tools/cook_vehicle_shadows.py','--jobs','{jobs}'],['build/vehicle-shadows/models/vehicle-shadows.json']),
 ('kart effects',['{py}','tools/cook_effects.py','assets/common/Effect/RKRace.breft',SD+'/textures'],[SD+'/textures/drift-effects.bin']),
 ('bike effects',['{py}','tools/cook_effects.py','assets/common/Effect/RKRace.breft',SD+'/textures','--bike'],[SD+'/textures/drift-effects-bike.bin']),
 # The item box model comes from Luigi Circuit's archive, unpacked where the item cook expects it.
 ('Luigi Circuit archive',['{py}','tools/unpack_szs.py','data/extracted/Race/Course/beginner_course.szs','assets/luigi'],['assets/luigi/itembox.brres']),
 ('items and HUD',['{py}','tools/cook_race_items.py',SD,'--disc','{disc}'],[SD+'/models/race-items.json']),
 ('world item lighting',['{py}','tools/cook_item_lighting.py'],['build/item-lighting/receipt.json']),
 ('race effects',['{py}','tools/cook_race_effects.py','--output','build/vfx/textures'],['build/vfx/textures/race-effects.bin']),
 ('blast effects',['{py}','tools/cook_blast.py'],['build/vfx/models/bomb_blast.gmdl']),
 ('Lakitu',['{py}','tools/cook_lakitu.py'],['build/vfx/models/lakitu_body.dskm']),
 ('award ceremony',['{py}','tools/cook_ceremony.py','{disc}'],[SD+'/models/ceremony.json']),
 ('battle and award screens',['{py}','tools/cook_battle.py'],[SD+'/models/battle.json']),
 ('effect archives',['{py}','tools/cook_effect_archives.py'],[SD+'/models/effect-archives.json']),
 ('name balloons',['{py}','tools/cook_race_balloon.py'],['build/vfx/textures/hud_balloon_line.t3x']),
 ('race flow and results',['{py}','tools/cook_race_flow.py'],['build/race-flow/textures/flow_wipe.t3x']),
 ('audio',['{py}','tools/cook_audio.py'],['build/audio/audio/manifest.json']),
 ('object sound points',['{py}','tools/cook_object_sounds.py'],['include/mkw/object_sound_assets.hpp']),
 # Menu layouts and models, unpacked where the menu cooks read them.
 ('menu archives',[['{py}','tools/unpack_szs.py',f'data/extracted/Scene/UI/{a}.szs',f'assets/menu-ui/{a}'] for a in MENU_ARCHIVES],
  ['assets/menu-ui/Title/title/timg/tt_title_screen_mario2.tpl']),
 ('menu model archives',[['{py}','tools/unpack_szs.py',f'data/extracted/Scene/Model/{a}.szs',f'assets/menu-models/{a}'] for a in ('Driver','BackModel')],
  ['assets/menu-models/Driver','assets/menu-models/BackModel']),
 ('menu interface',['{py}','tools/cook_menu_ui.py'],[SD+'/models/menu-ui.json']),
 ('menu lighting',['{py}','tools/cook_menu_lighting.py'],['include/mkw/menu_lighting.hpp','build/menu-lighting.json']),
 ('menu models',['{py}','tools/cook_menu_models.py','--machines','--jobs','{jobs}'],[SD+'/models/menu-models.json']),
 ('menu movies',['{py}','tools/cook_menu_movies.py','--jobs','{jobs}'],[SD+'/models/menu-movies.json',SD+'/movies/title.wav']),
 ('menu audio',['{py}','tools/cook_menu_audio.py'],[SD+'/models/menu-audio.json']),
 ('race bottom screen',['{py}','tools/cook_race_map.py','--jobs','{jobs}'],['build/race-map/textures/race_bottom_ui.t3x']),
 # The SD build's archives index the finished package, so they come last.
 ('menu archive',['{py}','tools/pack_menu_assets.py'],[SD+'/menu.pak']),
 ('race archive',['{py}','tools/pack_race_assets.py'],[SD+'/race.pak']),
]

def log(*a):print('[build_cia]',*a,flush=True)

def run(cmd,**kw):
    log('$',' '.join(str(c) for c in cmd))
    # PYTHONUTF8: text files are UTF-8 on every OS (Windows would default to cp1252).
    env=dict(os.environ,PYTHONUTF8='1',PYTHONPATH=os.pathsep.join(p for p in (str(ROOT/'vendor/gclib'),os.environ.get('PYTHONPATH')) if p))
    subprocess.run([str(c) for c in cmd],cwd=ROOT,check=True,env=env,**kw)

def shared_checkout():
    """Development trees link data/, assets/ and sd-card/ to a shared checkout."""
    return [d for d in ('data','assets','sd-card') if (ROOT/d).is_symlink()]

def stage_tools():
    from bootstrap_tools import tools
    makerom,bannertool=tools();log('makerom',makerom);log('bannertool',bannertool)
    from devkitpro import tool, root as devkitpro_root
    dkp=devkitpro_root()
    for need in (dkp/'devkitARM',dkp/'libctru',tool('tex3ds')):
        if not need.exists():raise SystemExit(f'Missing {need}: install devkitPro 3ds-dev (https://devkitpro.org/wiki/Getting_Started)')
    missing=[m for m in ('numpy','PIL','Crypto') if not __import__('importlib.util').util.find_spec(m)]
    if missing:raise SystemExit('Missing Python packages '+', '.join(missing)+': pip install -r requirements.txt')
    if not shutil.which('node'):raise SystemExit('Missing Node.js (https://nodejs.org), used by the model exporters')

def stage_sources():
    if all((ROOT/'vendor'/n).is_dir() for n in ('kinoko','noclip','gclib','citro3d')):log('sources present');return
    run([sys.executable,'tools/bootstrap.py'])

def stage_disc(disc):
    if disc and not Path(disc).is_file():raise SystemExit(f'No disc image at: {disc}')
    receipt=ROOT/'data/extracted/extraction-receipt.json'
    # An extraction that failed the check (a Europe disc, a modified or NKit
    # dump) is replaced when a different image is given, e.g. the USA ISO.
    # A verified one is kept: later steps read it even if the ISO has moved.
    if disc and receipt.is_file():
        old=json.loads(receipt.read_text())
        got={f['name']:f['sha256'] for f in old.get('files',[])}
        verified=all(old['identity'].get(k)==v for k,v in DISC_MANIFEST['identity'].items()) and all(got.get(n)==h for n,h in DISC_MANIFEST['files'].items())
        if not verified and (old['identity'].get('source')!=str(disc) or old['identity'].get('source_bytes')!=Path(disc).stat().st_size):
            log('replacing the previous, unsupported disc extraction')
            shutil.rmtree(ROOT/'data/extracted')
    if not receipt.is_file():
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
    if ok() or not disc:return
    rel.unlink(missing_ok=True)
    run([sys.executable,'tools/extract_disc.py',disc,'data/extracted','--file','rel/StaticR.rel','--to',rel])
    if not ok():raise SystemExit('The game code on this disc (rel/StaticR.rel) is modified. Patched images (Wiimmfi, mods, translations) do not work: use a clean dump of your disc.')

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

# The content stamp: every conversion step's fingerprint plus the packaging
# scripts that lay out the SD folder. The update CIA is compiled with it
# (tools/build.py -> MKW_CONTENT) and the builder writes it to the SD folder
# (content.txt), so code and game files from different builders don't mix.
PACKAGE_SCRIPTS=('tools/package_sd.py','tools/package_standalone.py')
def content_fingerprint(windows_npm=False):
    digest=hashlib.sha256()
    for name,cmd,_ in COOK_STEPS:digest.update(f'{name}\0{step_fingerprint(cmd,windows_npm)}\n'.encode())
    for script in PACKAGE_SCRIPTS:digest.update((ROOT/script).read_bytes().replace(b'\r\n',b'\n'))
    return digest.hexdigest()[:32]

def legacy_windows_fingerprint():
    """The stamp Windows builders of 0.5.0 and 0.5.1 wrote: the same recipe, but
    with npm.cmd in the first step. Games accept it as well as the real one."""
    return content_fingerprint(True)

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
        present=bool(outputs) and all((ROOT/o).exists() for o in outputs)
        # A fresh tree trusts only success stamps (a failed step can leave its
        # outputs half written) and, once a step runs, re-runs every later step.
        # Development trees, cooked by hand, are judged by their outputs; so are
        # the Node packages, which a development tree links from elsewhere.
        done=(present if shared or name=='Node packages' else stamp_done(stamp,name,fingerprint)) and not rerun
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
        stamp.write_text(f'fingerprint {fingerprint}\n{time.monotonic()-start:.0f} s\n')
        for path in PRUNE_AFTER.get(name,()):shutil.rmtree(ROOT/path,ignore_errors=True)

def stage_compile(jobs):
    run([sys.executable,'tools/build.py','--jobs',str(jobs),'--release'])

SD_OUTPUT=ROOT/'build/release/sd-card'

def stage_package(version,cci,keep_stage):
    run([sys.executable,'tools/package_sd.py','--no-zip'])
    import package_cia
    target=package_cia.package(cci,version)
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
    p.add_argument('--check',action='store_true',help='verify tools, disc and cooked assets, then stop')
    p.add_argument('--cook-only',action='store_true',help='stop after cooking')
    a=p.parse_args()
    stage_tools();stage_sources();stage_disc(a.disc);check_game_code(a.disc);stage_cook(a.disc,a.jobs,a.force_cook,a.check)
    if a.check:log('check passed: ready to compile and package');return
    if a.cook_only:log('cook complete');return
    if not a.skip_compile:stage_compile(a.jobs)
    cia=stage_package(a.version,False,a.keep_stage or a.cci)
    if a.cci:stage_package(a.version,True,a.keep_stage)
    log('done:',cia)
    log('Copy it to your SD card and install it with FBI, or use FBI remote install.')
    log('For quick updates later, also copy build/release/sd-card/3ds to the root of your SD card (see README).')

if __name__=='__main__':main()
