"""Generate the authored firebar phase stream from this disc's WLfirebarGC."""
from pathlib import Path
import os,subprocess,sys
from cook_firebar_indirect import cook
ROOT=Path(__file__).resolve().parents[1]
def main():
 source=ROOT/'assets/courses/koopa_course/WLfirebarGC.brres'
 work=ROOT/'build/firebar-source'
 subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work)],env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1'},check=True)
 cook(work,ROOT/'build/course-water-feedback/textures/firebar-indirect.ftan')
if __name__=='__main__':main()
