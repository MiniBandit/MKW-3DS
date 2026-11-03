#!/usr/bin/env python3
"""Prepare original RFL parts for Mii renderer integration (no runtime wiring)."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image,ImageDraw
from rfl_resource import Resource,FACIAL_TEXTURE_GROUPS,SHAPE_TEXTURE_GROUPS,SHAPE_GROUPS,shape_data,texture_image,mesh_data

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();raw=a.source.read_bytes();resource=Resource(raw)
    a.output.mkdir(parents=True,exist_ok=True)
    textures=[];samples=[]
    for group in FACIAL_TEXTURE_GROUPS+SHAPE_TEXTURE_GROUPS:
        for index,record in enumerate(resource.groups[group].records):
            im=texture_image(record);name=f'texture-{group:02}-{index:03}.png';im.save(a.output/name)
            textures.append(dict(group=group,index=index,file=name,width=im.width,height=im.height,format=record[0],wrap=list(record[6:8]),rgbaSha256=hashlib.sha256(im.tobytes()).hexdigest()))
            if index<4:samples.append((name,im))
    shapes=[]
    for kind,group in enumerate(SHAPE_GROUPS):
        for index,record in enumerate(resource.groups[group].records):
            part=shape_data(record,kind);part.update(group=group,index=index,shapeType=kind,mesh=mesh_data(part))
            shapes.append(part)
    report=dict(source=str(a.source),sourceSha256=hashlib.sha256(raw).hexdigest(),coordinateSpace='source arrays plus normalized GX position/256 normal/16384 UV/8192; instance transforms not applied',textures=textures,shapes=shapes)
    (a.output/'parts.json').write_text(json.dumps(report,separators=(',',':'))+'\n')
    sheet=Image.new('RGBA',(8*128,((len(samples)+7)//8)*152),(110,110,110,255));draw=ImageDraw.Draw(sheet)
    for i,(name,im) in enumerate(samples):
        x=(i%8)*128;y=(i//8)*152;im=im.copy();im.thumbnail((112,124))
        sheet.alpha_composite(im,(x+(128-im.width)//2,y+18+(124-im.height)//2));draw.text((x+3,y+2),name,fill='white')
    sheet.convert('RGB').save(a.output/'texture-preview.png')
    print(f'{len(shapes)} source shapes; {len(textures)} decoded textures; runtime integration pending')
if __name__=='__main__':main()
