#!/usr/bin/env python3
"""Cook the retail lens flare textures (assets/common/lensflare.brres).

Four 64x64 TEX0 images, each one quadrant of its element (the circle's centre
is texel 63,63); retail draws them with texture coordinates 0..2 and mirrored
wrap (src/lens_flare.cpp). rk_lens01/02/04 are I8: retail's TEV takes the
element colour from its table and the alpha from the texel (GX TEXA of I8 is
the intensity), so they become LA8 with L = A = I. rk_lens03 is RGB565 and
keeps its own colours (opaque).
Output: build/lens-flare/textures/lensflare_0N.t3x (N = retail texture index,
the brres order) and receipt.json; tools/resource_manifest.py packs them.
"""
import json,struct
from pathlib import Path
from PIL import Image
from cook_race_items import ROOT,digest
from cook_race_effects import write_t3x

SOURCE=ROOT/'assets/common/lensflare.brres'
NAMES=['rk_lens01','rk_lens02','rk_lens03','rk_lens04']

def tex0s(data):
    found={}
    at=data.find(b'TEX0')
    while at>=0:
        name_off=struct.unpack_from('>i',data,at+0x14)[0]
        name=data[at+name_off:data.index(b'\0',at+name_off)].decode()
        found[name]=at
        at=data.find(b'TEX0',at+4)
    return found

def decode(data,at):
    data_off=struct.unpack_from('>i',data,at+0x10)[0]
    w,h,fmt=struct.unpack_from('>HHI',data,at+0x1c)
    p=at+data_off;im=Image.new('RGBA',(w,h));px=im.load()
    if fmt==1:   # I8: 8x4 tiles
        for ty in range(0,h,4):
            for tx in range(0,w,8):
                for y in range(4):
                    for x in range(8):
                        i=data[p];p+=1;px[tx+x,ty+y]=(i,i,i,i)
        return im,'la8'
    if fmt==4:   # RGB565: 4x4 tiles
        for ty in range(0,h,4):
            for tx in range(0,w,4):
                for y in range(4):
                    for x in range(4):
                        v=struct.unpack_from('>H',data,p)[0];p+=2
                        r,g,b=v>>11,(v>>5)&63,v&31
                        px[tx+x,ty+y]=(r<<3|r>>2,g<<2|g>>4,b<<3|b>>2,255)
        return im,'rgb565'
    raise ValueError(f'unexpected TEX0 format {fmt}')

def main():
    data=SOURCE.read_bytes();found=tex0s(data)
    if sorted(found)!=NAMES:raise ValueError('lensflare.brres textures changed: '+str(sorted(found)))
    out=ROOT/'build/lens-flare/textures';out.mkdir(parents=True,exist_ok=True)
    written=[]
    for index,name in enumerate(NAMES):
        im,form=decode(data,found[name])
        if im.size!=(64,64):raise ValueError(name+' is not 64x64')
        write_t3x(out/f'lensflare_{index:02d}.t3x',im,form)
        written.append({'index':index,'source':name,'format':form})
    (out.parent/'receipt.json').write_text(json.dumps({'source':str(SOURCE.relative_to(ROOT)),'source_sha256':digest(SOURCE),'textures':written},indent=2)+'\n')
    print(json.dumps(written))

if __name__=='__main__':main()
