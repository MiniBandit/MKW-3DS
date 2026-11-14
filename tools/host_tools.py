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
def windows_compilers():
 """Usual Windows install folders, for a compiler that is not on PATH (winget
 and MSYS2 often leave it off PATH until a new window, or for good). Every
 match, most likely first: compiler() tries each until one builds C++."""
 import glob
 local=os.environ.get('LOCALAPPDATA','');programs=[os.environ.get(k,'') for k in ('ProgramFiles','ProgramFiles(x86)')]
 # A portable LLVM-MinGW unpacked into the builder (.toolchain/llvm-mingw, PR #127).
 patterns=[str(ROOT/'.toolchain'/'llvm-mingw'/'bin'/'clang++.exe')]
 for base in [local,*programs]:
  if not base:continue
  # winget: per-user installs under LOCALAPPDATA, machine-wide ones under Program Files.
  patterns+=[os.path.join(base,'Microsoft','WinGet','Links','clang++.exe'),os.path.join(base,'Microsoft','WinGet','Links','g++.exe'),
   os.path.join(base,'WinGet','Links','clang++.exe'),os.path.join(base,'WinGet','Links','g++.exe'),
   os.path.join(base,'Microsoft','WinGet','Packages','MartinStorsjo.LLVM-MinGW*','*','bin','clang++.exe'),
   os.path.join(base,'WinGet','Packages','MartinStorsjo.LLVM-MinGW*','*','bin','clang++.exe'),
   os.path.join(base,'Microsoft','WinGet','Packages','BrechtSanders.WinLibs*','mingw64','bin','g++.exe'),
   os.path.join(base,'WinGet','Packages','BrechtSanders.WinLibs*','mingw64','bin','g++.exe')]
 for drive in ('C:\\','D:\\'):
  for root in ('msys64','msys2','devkitPro\\msys2'):
   for env in ('ucrt64','mingw64','clang64'):
    patterns+=[os.path.join(drive,root,env,'bin','g++.exe'),os.path.join(drive,root,env,'bin','clang++.exe')]
  patterns+=[os.path.join(drive,'llvm-mingw*','bin','clang++.exe'),os.path.join(drive,'w64devkit','bin','g++.exe'),
   os.path.join(drive,'mingw64','bin','g++.exe'),os.path.join(drive,'MinGW','bin','g++.exe'),os.path.join(drive,'Strawberry','c','bin','g++.exe')]
 for program in programs:
  if program:patterns+=[os.path.join(program,'llvm-mingw*','bin','clang++.exe'),os.path.join(program,'mingw-w64','*','mingw64','bin','g++.exe')]
 found=[]
 for pattern in patterns:
  for match in sorted(glob.glob(pattern)):
   if os.path.isfile(match) and match not in found:found.append(match)
 return found
def builds_cpp(cxx):
 """True when cxx builds a small C++ program with its standard library. The
 clang++ from LLVM's own Windows installer has none unless Visual Studio is
 installed ('array' file not found, #125), so finding a compiler is not enough."""
 import tempfile
 env=dict(os.environ);env['PATH']=os.path.dirname(cxx)+os.pathsep+env.get('PATH','')
 with tempfile.TemporaryDirectory(prefix='mkw-cxx-') as d:
  source=os.path.join(d,'probe.cpp');program=os.path.join(d,'probe'+('.exe' if os.name=='nt' else ''))
  with open(source,'w') as f:f.write('#include <array>\n#include <string>\nint main(){std::array<int,1> a{};return int(std::string("x").size())-1+a[0];}\n')
  try:
   if subprocess.run([cxx,'-std=c++2b',source,'-o',program],capture_output=True,timeout=300,env=env).returncode:return False
   return subprocess.run([program],capture_output=True,timeout=60,env=env).returncode==0
  except (OSError,subprocess.SubprocessError):return False
_chosen=None
def compiler():
 global _chosen
 if _chosen:return _chosen
 candidates=[shutil.which(name) for name in ('clang++','g++','c++')]
 if os.name=='nt':candidates+=windows_compilers()
 tried=[]
 for found in dict.fromkeys(c for c in candidates if c):
  tried.append(found)
  if builds_cpp(found):
   # Its own DLLs (libstdc++, libwinpthread) beside it, for the programs it builds too.
   if os.name=='nt':os.environ['PATH']=os.path.dirname(found)+os.pathsep+os.environ.get('PATH','')
   _chosen=found;return found
 if tried:print('These C++ compilers could not build a test program: '+', '.join(tried)+
  ('. The clang++ from LLVM\'s own installer needs Visual Studio; LLVM-MinGW works on its own.' if os.name=='nt' else ''),file=sys.stderr)
 hint=('install the Xcode Command Line Tools (xcode-select --install)' if sys.platform=='darwin' else
       'none on PATH or in the usual folders (WinGet, MSYS2, llvm-mingw, w64devkit). Install one in a terminal: winget install MartinStorsjo.LLVM-MinGW.UCRT, then close this window and run the builder again in a new one' if os.name=='nt' else
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
