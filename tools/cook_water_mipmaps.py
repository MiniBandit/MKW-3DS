#!/usr/bin/env python3
"""Add the original mip levels to the Psea textures whose GX samplers use them.

GCN Peach Beach's sea textures carry hand-made mip levels (the far sea fades
to light cyan through yo_wave_soko's levels; yo_wave and yo_wave_tex change
brightness with distance), so each level is decoded from Psea.brres the way
tools/cook_model.py decodes level 0 and encoded on its own. Box-filtering
level 0 instead loses that distance art. The original maximum-resolution ETC
payload is preserved byte for byte, and the chain keeps the original level
count, so the GPU memory is the same as a filtered chain.
"""
import hashlib,io,json,struct,subprocess,sys,tempfile
import tex3ds_cache
from pathlib import Path
from devkitpro import tool

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'sd-card/3ds/mkw-3ds'
OUT=ROOT/'build/water-mipmaps'
BRRES=ROOT/'assets/courses/old_peach_gc/Psea.brres'

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

# GX block width, height and bits per texel of the unpaletted formats (I4, I8,
# IA4, IA8, RGB565, RGB5A3, RGBA8, CMPR). Course mip chains use no others.
BLOCKS={0:(8,8,4),1:(8,4,8),2:(8,4,8),3:(4,4,16),4:(4,4,16),5:(4,4,16),6:(4,4,32),14:(8,8,4)}

def textures(brres,path=BRRES):
    """name -> (width, height, GX format, image count, data from level 0 on)."""
    if brres[:4]!=b'bres':raise ValueError('Not a BRRES: '+str(path))
    view=memoryview(brres)
    def group(at):
        count=struct.unpack_from('>I',brres,at+4)[0];out={}
        for i in range(1,count+1):
            entry=at+8+16*i;name,data=struct.unpack_from('>II',brres,entry+8)
            text=brres[at+name:brres.index(b'\0',at+name)].decode()
            out[text]=at+data
        return out
    root=struct.unpack_from('>H',brres,12)[0]
    if brres[root:root+4]!=b'root':raise ValueError('BRRES has no root section')
    groups=group(root+8);out={}
    if 'Textures(NW4R)' not in groups:return out
    for name,at in group(groups['Textures(NW4R)']).items():
        if brres[at:at+4]!=b'TEX0':raise ValueError('Not a TEX0: '+name)
        # Version 2 has one more header word (noclip parseTEX0); courses use 3.
        if struct.unpack_from('>I',brres,at+8)[0]!=3:raise ValueError('Unsupported TEX0 version: '+name)
        data=struct.unpack_from('>I',brres,at+0x10)[0]
        width,height=struct.unpack_from('>HH',brres,at+0x1c)
        fmt,images=struct.unpack_from('>II',brres,at+0x20)
        out[name]=(width,height,fmt,images,view[at+data:])
    return out

def levels(width,height,fmt,images,data,count=None):
    """The first count (default all) original levels as RGBA, decoded like
    tools/cook_model.py's level 0."""
    sys.path.insert(0,str(ROOT/'vendor/gclib'))
    from gclib.texture_utils import decode_image
    from gclib.gx_enums import ImageFormat,PaletteFormat
    if fmt not in BLOCKS:raise ValueError('Unsupported TEX0 format %d'%fmt)
    bw,bh,bits=BLOCKS[fmt];out=[];offset=0
    for level in range(images if count is None else min(count,images)):
        w,h=max(1,width>>level),max(1,height>>level)
        size=-(-w//bw)*bw*-(-h//bh)*bh*bits//8
        if offset+size>len(data):raise ValueError('Truncated original mip level')
        out.append(decode_image(io.BytesIO(data[offset:offset+size]),io.BytesIO(b''),ImageFormat(fmt),PaletteFormat(0),0,w,h).convert('RGBA'))
        offset+=size
    return out

def owned():
    """Our Psea texture name -> TEX0 name, for the textures a mip-filtered
    sampler reads. These chains are this cook's, not cook_world_mipmaps.py's."""
    selected={}
    for model in ('psea1sand','psea2dark','psea3nami','psea4tex','psea5spc'):
        data=json.loads((ASSETS/f'models/obj16_psea__{model}-receipt.json').read_text())
        textures_={t['source']:t['name'] for t in data['textures']}
        for shape in data['materials']:
            for sampler in shape['sourceMaterial']['samplers']:
                if sampler['minFilter']>=2:
                    selected[textures_[sampler['name']]]=sampler['name']
    return selected

def main():
    output=OUT/'textures';output.mkdir(parents=True,exist_ok=True)
    selected=owned()
    brres=BRRES.read_bytes();originals=textures(brres)
    receipt={'version':2,'levels':'original','brresSha256':digest(brres),'textures':{},'extra_gpu_bytes':0}
    from PIL import Image
    with tempfile.TemporaryDirectory() as scratch:
        for name,source in sorted(selected.items()):
            png=ASSETS/f'textures/{name}.png';old=(ASSETS/f'textures/{name}.t3x').read_bytes()
            width,height,fmt,count,base=payload(old)
            if fmt not in (12,13) or count:raise ValueError('Expected original ETC base map')
            ow,oh,ofmt,images,data=originals[source]
            if (ow,oh)!=(width,height):raise ValueError('Psea texture was resized: '+name)
            decoded=levels(ow,oh,ofmt,images,data)
            # Level 0 must be the converted image the base payload came from.
            with Image.open(png) as shipped:
                if shipped.convert('RGBA').tobytes()!=decoded[0].tobytes():
                    raise ValueError('Original level 0 decodes differently from '+png.name)
            # PICA levels stop at 8x8 (one ETC tile).
            n=0
            while n+1<images and min(width,height)>>(n+1)>=8:n+=1
            if n<1:raise ValueError('No original mip levels: '+source)
            chain=bytearray(base);sizes=[len(base)]
            for level in range(1,n+1):
                part=Path(scratch)/f'{name}_{level}.png';target=part.with_suffix('.t3x')
                decoded[level].save(part)
                tex3ds_cache.run([str(tool('tex3ds')),'-f','etc1a4' if fmt==13 else 'etc1','-q','high',
                                '-z','none','-o',str(target),str(part)],check=True,stdout=subprocess.DEVNULL)
                w,h,f,m,raw=payload(target.read_bytes())
                if (w,h,f,m)!=(width>>level,height>>level,fmt,0) or len(raw)!=len(base)>>(2*level):
                    raise ValueError('Invalid encoded level %d of %s'%(level,name))
                chain+=raw;sizes.append(len(raw))
            entries=struct.unpack_from('<H',old)[0];header=bytearray(old[:5+entries*12]);header[4]=n
            encoded=bytes(header)+struct.pack('<I',len(chain)<<8)+bytes(chain)
            assert payload(encoded)[4][:len(base)]==base
            (output/(name+'.t3x')).write_bytes(encoded)
            extra=len(chain)-len(base);receipt['extra_gpu_bytes']+=extra
            receipt['textures']['textures/'+name+'.t3x']={'source':source,'size':[width,height],'mipLevels':n,
                'levelBytes':sizes,'baseSha256':digest(base),'originalSha256':digest(old),
                'pngSha256':digest(png.read_bytes()),'sha256':digest(encoded),'extraGpuBytes':extra}
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__':main()
