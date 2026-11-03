#!/usr/bin/env python3
"""Add mip levels to the source Psea textures whose GX samplers use them.

The original maximum-resolution ETC payload is preserved byte for byte.
Only the small water textures are replaced in a package overlay.
"""
import hashlib,json,struct,subprocess
from pathlib import Path
from devkitpro import tool

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'sd-card/3ds/mkw-3ds'
OUT=ROOT/'build/water-mipmaps'

def digest(data):return hashlib.sha256(data).hexdigest()

def payload(data):
    count=struct.unpack_from('<H',data)[0]
    offset=5+count*12
    size=struct.unpack_from('<I',data,offset)[0]
    if size&255:raise ValueError('Expected an uncompressed T3X')
    raw=data[offset+4:]
    if len(raw)!=size>>8:raise ValueError('Invalid T3X size')
    width=1<<((data[2]&7)+3);height=1<<(((data[2]>>3)&7)+3)
    return width,height,data[3],data[4],raw

def main():
    output=OUT/'textures';output.mkdir(parents=True,exist_ok=True)
    selected={}
    for model in ('psea1sand','psea2dark','psea3nami','psea4tex','psea5spc'):
        data=json.loads((ASSETS/f'models/obj16_psea__{model}-receipt.json').read_text())
        textures={t['source']:t['name'] for t in data['textures']}
        for shape in data['materials']:
            for sampler in shape['sourceMaterial']['samplers']:
                if sampler['minFilter']>=2:
                    selected[textures[sampler['name']]]=sampler['name']
    receipt={'version':1,'textures':{},'extra_gpu_bytes':0}
    for name,source in sorted(selected.items()):
        png=ASSETS/f'textures/{name}.png';old=(ASSETS/f'textures/{name}.t3x').read_bytes()
        width,height,fmt,levels,base=payload(old)
        if fmt not in (12,13) or levels:raise ValueError('Expected original ETC base map')
        target=output/(name+'.t3x')
        subprocess.run([str(tool('tex3ds')),'-f','etc1a4' if fmt==13 else 'etc1','-q','high',
                        '-m','box','-z','none','-o',str(target),str(png)],check=True,stdout=subprocess.DEVNULL)
        encoded=target.read_bytes();w,h,f,n,chain=payload(encoded)
        if (w,h,f)!=(width,height,fmt) or n<1 or chain[:len(base)]!=base:
            raise ValueError('Water mip chain changed source base level: '+name)
        sizes=[len(base)//(4**level) for level in range(n+1)]
        if sum(sizes)!=len(chain) or min(w,h)//(2**n)<8:raise ValueError('Invalid water mip chain')
        extra=len(chain)-len(base);receipt['extra_gpu_bytes']+=extra
        receipt['textures']['textures/'+name+'.t3x']={'source':source,'size':[w,h],'mipLevels':n,
            'levelBytes':sizes,'baseSha256':digest(base),'originalSha256':digest(old),
            'pngSha256':digest(png.read_bytes()),'sha256':digest(encoded),'extraGpuBytes':extra}
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__':main()
