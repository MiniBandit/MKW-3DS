#!/usr/bin/env python3
"""Export original FireBPlane inputs and generate the shared phase stream."""
import argparse,os,subprocess
from pathlib import Path
from cook_firebar_indirect import cook
ROOT=Path(__file__).resolve().parents[1]
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--source',type=Path,default=ROOT/'assets/courses/koopa_course/WLfirebarGC.brres')
 parser.add_argument('--output',type=Path,default=ROOT/'build/firebar/textures/firebar-indirect.ftan')
 args=parser.parse_args();source=args.source
 work=args.output.parent.parent/'source';work.mkdir(parents=True,exist_ok=True)
 env={**os.environ,'MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_FREEZE_NONLINEAR_UV':'1'}
 subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work),'fireBPlane'],env=env,check=True)
 cook(work,args.output)
if __name__=='__main__':main()
