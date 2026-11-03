#!/usr/bin/env python3
"""Cook original Coconut Mall palm and escalator meshes, excluding duplicate LODs."""
import json,os,subprocess,sys
from pathlib import Path
from cook_model import cook
ROOT=Path(__file__).resolve().parents[1]
def main():
    out=ROOT/'sd-card/3ds/mkw-3ds'
    recipes=[('PalmTree','PalmTree','coconut_palm'),('escalator','escalator','coconut_escalator')]
    for archive,model,prefix in recipes:
        # Exports stay in build/; assets/ holds only the extracted source archives.
        exported=ROOT/'build/coconut-exports'/prefix
        subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(ROOT/'assets/coconut-mall'/(archive+'.brres')),str(exported),model],check=True,
                       env={**os.environ,'MKW_RETAIL_LIGHTING':'1'})
        if model=='escalator':
            # The visible strip must extend into the fixed upper landing through
            # the full two-tread (200Y/-600Z) collision animation period.
            path=exported/'model.json';data=json.loads(path.read_text())
            for mesh in data['models']:
                for shape in mesh['shapes']:
                    original=shape['vertices'].copy();end=[];bottom=[]
                    for i in range(0,len(original),3):
                        tri=original[i:i+3]
                        if min(v[1] for v in tri)>=1299.9:end.extend(tri)
                        if max(v[1] for v in tri)<=0.1:bottom.extend(tri)
                    if len(bottom)!=24:raise ValueError('Unexpected source escalator start topology')
                    for vertex in bottom:
                        v=vertex.copy();v[1]-=200;v[2]+=600;shape['vertices'].append(v)
                    if len(end)!=24:raise ValueError('Unexpected source escalator end topology')
                    for offset in (200,400):
                        for vertex in end:
                            v=vertex.copy();v[1]+=offset;v[2]-=3*offset;shape['vertices'].append(v)
            path.write_text(json.dumps(data))
        parts=cook(exported,out,prefix,256)
        if len(parts)!=1:raise ValueError('Object runtime expects one partition')
    print('Coconut palms and escalator treads cooked from local source.')
if __name__=='__main__':main()
