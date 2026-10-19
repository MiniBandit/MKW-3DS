"""Stage already-baked statue portraits as shared two-triangle billboard meshes."""
import argparse,hashlib,json,shutil,struct,subprocess
from pathlib import Path
from course_catalog import ROOT,courses
from devkitpro import tool
BASE=ROOT/'assets/mii/course-portraits'
def inputs(course):
    manifest=BASE/'statue-portraits.json'
    if not manifest.is_file():return None,[]
    data=json.loads(manifest.read_text())
    row=next((r for r in data['statues'] if r['course']==course['key']),None)
    return row,([manifest]+[BASE/r['image'] for r in row['images']] if row else [])
def cook(course,out):
    row,_=inputs(course)
    if row is None:return []
    result=[]
    for image in row['images']:
        source=BASE/image['image']
        if hashlib.sha256(source.read_bytes()).hexdigest()!=image['sha256']:raise ValueError('Prepared statue portrait hash mismatch')
        name=f"mii_statue_{course['id']:02d}_{image['index']}"
        texture=out/'textures'/f'{name}.png';texture.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,texture)
        subprocess.run([str(tool('tex3ds')),'-f','rgba8','-z','none','-o',str(texture.with_suffix('.t3x')),str(texture)],check=True,stdout=subprocess.DEVNULL)
        # Match the original portrait camera at its z=0 plane: 43.2 * 600/500.
        half=43.20000076293945*600/500
        vertices=[]
        for x,y,u,v in [(-half,-half,0,0),(half,-half,1,0),(half,half,1,1),(-half,half,0,1)]:
            vertices.append(struct.pack('<10f4B',x,y,0,0,0,1,u,v,u,v,255,255,255,255))
        # Alpha>0, blend, double-sided, explicit depth test/write. The baked
        # opaque/translucent layers now share one silhouette on a flat plane.
        flags=(1<<31)|(1<<23)|(1<<24)|(4<<26)|1|2|4|8
        blob=struct.pack('<4s5I',b'GMDL',4,4,6,1,1)
        # Retail unlit channel retains baked colour; fog remains course fog 0.
        # Avoid the legacy renderer's additional warm ambient grade.
        blob+=struct.pack('<IIiIiI4fIIII',0,6,0,flags,-1,0xffffffff,0,0,0,0,0,3,0,0)
        blob+=name.encode().ljust(64,b'\0')+b''.join(vertices)+struct.pack('<6H',0,2,1,0,3,2)
        target=out/'models'/f'{name}.gmdl';target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(blob)
        result.append(f"__MiiStatueHeads face_{image['index']} {name} -\n")
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ids',type=int,nargs='+',required=True);a=p.parse_args()
    out=ROOT/'sd-card/3ds/mkw-3ds'
    for c in courses():
        if c['id'] not in a.ids:continue
        path=out/f"models/course_{c['id']:02d}_visuals.list"
        lines=[s+'\n' for s in path.read_text().splitlines() if not s.startswith('__MiiStatueHeads ')]
        added=cook(c,out);path.write_text(''.join(lines+added));print(c['key'],len(added),'prepared heads')
