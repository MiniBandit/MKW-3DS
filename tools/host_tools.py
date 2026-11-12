#!/usr/bin/env python3
"""Build the desktop helper programs the converters run (not the game).

    host_tools.py mii-head       build/host-tools/export_mii_head
    host_tools.py sprite-scales  build/host-tools/sprite-scales.json

Both reuse the game's own C++ sources, compiled for this computer: the Mii head
exporter assembles the retail default Miis' heads for their portraits, and the
sprite-scale measurer composes every menu page to size the menu textures.
"""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/host-tools'
PROGRAMS={
 'mii-head':('export_mii_head',['tools/export_mii_head.cpp','src/mii_head.cpp','src/mii_part_bank.cpp']),
 'sprite-scales':('menu_sprite_scales',['tools/menu_sprite_scales.cpp','src/menu_ui.cpp','src/menu_state.cpp','src/menu_transition.cpp','src/menu_focus.cpp','src/menu_index.cpp','src/menu_text_motion.cpp']),
}
def compiler():
 for name in ('clang++','g++','c++'):
  found=shutil.which(name)
  if found:return found
 hint=('install the Xcode Command Line Tools (xcode-select --install)' if sys.platform=='darwin' else
       'install one in a terminal (winget install MartinStorsjo.LLVM-MinGW.UCRT), then open a new window' if os.name=='nt' else
       'install g++ or clang++')
 raise SystemExit('No C++ compiler for this computer: '+hint)
def build(kind):
 name,sources=PROGRAMS[kind];OUT.mkdir(parents=True,exist_ok=True)
 program=OUT/(name+('.exe' if os.name=='nt' else ''))
 cxx=compiler();flags=['-std=c++2b','-O2','-I'+str(ROOT/'include')]
 digest=hashlib.sha256(' '.join([cxx,*flags]).encode())
 for source in sources:digest.update((ROOT/source).read_bytes())
 for header in sorted((ROOT/'include').rglob('*.h*')):
  if not header.name.startswith('._'):digest.update(header.read_bytes())
 stamp=program.with_suffix('.sig')
 if program.is_file() and stamp.is_file() and stamp.read_text()==digest.hexdigest():return program
 subprocess.run([cxx,*flags,*[str(ROOT/s) for s in sources],'-o',str(program)],check=True)
 stamp.write_text(digest.hexdigest());return program
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('program',choices=sorted(PROGRAMS));a=p.parse_args()
 program=build(a.program)
 if a.program=='sprite-scales':
  target=OUT/'sprite-scales.json'
  subprocess.run([str(program),str(target)],check=True)
  json.loads(target.read_text())
 print(program)
if __name__=='__main__':main()
