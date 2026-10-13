#!/usr/bin/env python3
"""Incremental native ARM11 build. No game data downloaded or embedded by default."""
import argparse, concurrent.futures, hashlib, json, os, subprocess, sys
from pathlib import Path
from build_hardware_sdk import build_citro3d
from devkitpro import tool, arm, root as devkitpro_root
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--jobs',type=int,default=6)
 # Release (the CIA builder): no test hooks, logs or engine assert text; no build paths.
 p.add_argument('--release',action='store_true');p.add_argument('--external-assets',action='store_true',help='3DSX test program using an existing compatible SD asset set');a=p.parse_args()
 dkp=devkitpro_root();out=ROOT/'build';out.mkdir(exist_ok=True)
 citro=build_citro3d(out,dkp)
 kin=ROOT/'vendor/kinoko';cxx=arm('arm-none-eabi-g++')
 # DVLE 0: scene shader; DVLE 1: NW4R EF particles (src/effect.v.pica).
 subprocess.run([str(tool('picasso')),'-o',str(out/'mkw.shbin'),str(ROOT/'src/mkw.v.pica'),str(ROOT/'src/effect.v.pica')],check=True)
 # The content stamp the update CIA checks against the SD card's content.txt.
 sys.path.insert(0,str(ROOT/'tools/release'));from build_cia import content_fingerprint,legacy_windows_fingerprint
 stamp='#pragma once\n// Converted game files this code expects (tools/release/build_cia.py).\n#define MKW_CONTENT "'+content_fingerprint()+'"\n// Written by Windows builders of 0.5.0 and 0.5.1 (npm.cmd in the recipe): also accepted.\n#define MKW_CONTENT_WINDOWS_LEGACY "'+legacy_windows_fingerprint()+'"\n'
 # Shown on the lower screen at start-up, so a console shows which update it runs.
 stamp+='#define MKW_VERSION "'+(ROOT/'tools/release/VERSION').read_text().strip()+'"\n'
 # Shown once on the lower screen after an update (tools/release/WHATS_NEW.txt):
 # at most 16 lines of 38 characters, the lower screen's 8x8 text grid.
 news=[l.rstrip() for l in (ROOT/'tools/release/WHATS_NEW.txt').read_text(encoding='utf-8').splitlines() if l.strip()]
 if len(news)>16 or any(len(l)>38 or not l.isascii() or '"' in l or '\\' in l for l in news):raise SystemExit('WHATS_NEW.txt: at most 16 ASCII lines of 38 characters, no quotes or backslashes')
 stamp+='#define MKW_WHATS_NEW "'+'\\n'.join(news)+'"\n'
 # The GitHub update build (MKW_QUICK_UPDATE=1, .github/workflows/update-cia.yml).
 stamp+='#define MKW_QUICK_UPDATE '+('1' if os.environ.get('MKW_QUICK_UPDATE')=='1' else '0')+'\n'
 if not (out/'content_version.hpp').is_file() or (out/'content_version.hpp').read_text()!=stamp:(out/'content_version.hpp').write_text(stamp)
 (out/'shader_data.hpp').write_text('#pragma once\n#include <cstdint>\nalignas(4) static const uint8_t mkw_shader[]={'+','.join(map(str,(out/'mkw.shbin').read_bytes()))+'};\n')
 arch=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft']
 flags=arch+['-std=gnu++23','-O2','-g','-D__3DS__','-DREVOLUTION','-mword-relocations','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-fcheck-new','-fno-strict-aliasing','-ffp-contract=off','-Wall','-Wno-psabi','-I'+str(kin/'include'),'-I'+str(kin/'source'),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'vendor/citro3d/include'),'-I'+str(dkp/'libctru/include'),'-I'+str(out)]
 # Kinoko alone is built for link-time optimization: its many one-line
 # KartObjectProxy accessors live in .cc files and inline only at link time.
 # Same IEEE semantics (-ffp-contract=off, no fast-math); the app keeps
 # ordinary objects, so the linker --wrap list below is unaffected.
 if a.external_assets:flags+=['-DMKW_EXTERNAL_ASSETS']
 if a.release:flags+=['-DMKW_RELEASE','-DKINOKO_QUIET','-ffile-prefix-map='+str(ROOT)+'/=']
 lto=['-flto=auto']
 codegen=['-O2','-g','-mword-relocations','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-fno-strict-aliasing','-ffp-contract=off']
 sources=[p for p in sorted((kin/'source').rglob('*.cc')) if not p.name.startswith('._') and p.name not in ('main.cc','KReplaySystem.cc','Context.cc')]+[p for p in sorted((ROOT/'src').glob('*.cpp')) if not p.name.startswith('._')]
 # All headers invalidate objects; conservative but reproducible.
 headers=[out/'shader_data.hpp',out/'content_version.hpp']+list(kin.rglob('*.hh'))+list((ROOT/'include').rglob('*.hpp'))+list((ROOT/'vendor/citro3d/include').rglob('*.h'))+list((dkp/'libctru/include').rglob('*.h'))
 signature=hashlib.sha256((' '.join(flags+lto)+subprocess.check_output([str(cxx),'--version'],text=True)).encode()+b''.join(p.read_bytes() for p in sorted(headers) if not p.name.startswith('._'))).hexdigest()
 stamp=out/'compile-signature';changed=not stamp.exists() or stamp.read_text()!=signature
 def compile(src):
  obj=out/'obj'/src.relative_to(ROOT).with_suffix('.o');obj.parent.mkdir(parents=True,exist_ok=True)
  objstamp=obj.with_suffix(".sig")
  if not obj.exists() or not objstamp.exists() or objstamp.read_text()!=signature or src.stat().st_mtime>obj.stat().st_mtime:
   r=subprocess.run([str(cxx),*flags,*(lto if src.is_relative_to(kin) else []),'-c',str(src),'-o',str(obj)],capture_output=True,text=True,errors="replace")
   if r.returncode:raise RuntimeError(str(src)+'\n'+r.stdout+r.stderr)
   objstamp.write_text(signature)
   print(src.relative_to(ROOT),flush=True)
  return obj
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:objs=list(pool.map(compile,sources))
 stamp.write_text(signature)
 elf=out/'mkw-3ds.elf'
 # The background menu asset loader allocates LINEAR memory; src/linear_lock.cpp
 # serializes every LINEAR heap call, including those made inside Citro3D/libctru.
 wraps=['-Wl,--wrap='+name for name in ('linearAlloc','linearMemAlign','linearRealloc','linearGetSize','linearFree','linearSpaceFree','fopen')]
 # The object list goes through a response file: ~240 absolute paths can pass
 # the Windows command-line limit. GCC response files use / and quotes.
 objects_file=out/'objects.rsp';objects_file.write_text(''.join('"'+Path(o).as_posix()+'"\n' for o in objs))
 subprocess.run([str(cxx),*arch,*lto,*codegen,'-specs=3dsx.specs','-Wl,--gc-sections',*wraps,'-Wl,-Map,'+str(out/'mkw-3ds.map'),'@'+str(objects_file),str(citro),'-L'+str(dkp/'libctru/lib'),'-lctru','-lm','-o',str(elf)],check=True)
 subprocess.run([str(tool('3dsxtool')),str(elf),str(out/'mkw-3ds.3dsx')],check=True)
 subprocess.run([str(arm('arm-none-eabi-size')),str(elf)],check=True)
 (out/'build-receipt.json').write_text(json.dumps({'hardware_sdk':json.loads((out/'hardware-sdk/receipt.json').read_text()),'shader_sha256':hashlib.sha256((out/'mkw.shbin').read_bytes()).hexdigest(),'libctru_sha256':hashlib.sha256((dkp/'libctru/lib/libctru.a').read_bytes()).hexdigest(),'kinoko_commit':subprocess.check_output(['git','-C',str(kin),'rev-parse','HEAD'],text=True).strip(),'sha256':hashlib.sha256((out/'mkw-3ds.3dsx').read_bytes()).hexdigest(),'flags':flags,'sources':{s.relative_to(ROOT).as_posix():hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}},indent=2)+'\n')
if __name__=='__main__':main()
