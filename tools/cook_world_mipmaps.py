#!/usr/bin/env python3
"""Add trilinear minification levels to static course and world textures.

Retain the existing maximum-resolution ETC payload and T3X UV rectangle.
Animation atlases are excluded globally, since coarse mips across different
frames would bleed. Original assets remain untouched; package_sd overlays the
result and pack_race_assets includes the same replacements in race.pak.
"""
import argparse, concurrent.futures, hashlib, json, struct, subprocess
from pathlib import Path
from cook_water_mipmaps import payload
from devkitpro import tool
from gx_material import GMDL_SUBMESH_BYTES

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'sd-card/3ds/mkw-3ds'
OUT=ROOT/'build/world-mipmaps'

def digest(data):return hashlib.sha256(data).hexdigest()

def select():
    used,animated,models,per_course=set(),set(),{},{}
    catalog=json.loads((ASSETS/'models/course-catalog.json').read_text())
    for course in catalog['courses']:
        names=[]
        for relative in course['model_lists']:
            if '_map' not in relative:names+=(ASSETS/relative).read_text().split()
        visuals=ASSETS/f"models/course_{course['id']:02d}_visuals.list"
        if visuals.is_file():names +=[row.split()[2] for row in visuals.read_text().splitlines()]
        course_textures=set()
        for name in names:
            relative='models/'+name+'.gmdl'
            # Match package_sd's final precedence: validate and classify the
            # model that will ship, including later material/animation fixes.
            source=ASSETS/relative
            for overlay in ('course-materials','course-water-feedback','rainbow-final'):
                candidate=ROOT/'build'/overlay/relative
                if candidate.is_file():source=candidate
            data=source.read_bytes();magic,version,nv,ni,ns,nt=struct.unpack_from('<4s5I',data)
            if magic!=b'GMDL' or version not in GMDL_SUBMESH_BYTES:raise ValueError('Unsupported world model: '+name)
            stride=GMDL_SUBMESH_BYTES[version]
            textures=[data[24+ns*stride+i*64:24+ns*stride+(i+1)*64].split(b'\0')[0].decode() for i in range(nt)]
            for i in range(ns):
                at=24+i*stride
                first=struct.unpack_from('<i',data,at+8)[0]
                second=struct.unpack_from('<i',data,at+16)[0]
                frames=struct.unpack_from('<I',data,at+40)[0]&0xffff
                for index in (first,second):
                    if index<0:continue
                    texture=textures[index];used.add(texture);course_textures.add(texture)
                    if frames>1:animated.add(texture)
            models[relative]={'source':str(source.relative_to(ROOT)),'sha256':digest(data)}
        per_course[course['key']]=course_textures
    return used-animated,animated,models,per_course

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jobs',type=int,default=3);args=p.parse_args()
    selected,animated,models,course_sets=select();(OUT/'textures').mkdir(parents=True,exist_ok=True)
    old_receipt=json.loads((OUT/'receipt.json').read_text()) if (OUT/'receipt.json').is_file() else {}
    encoder=str(tool('tex3ds'))
    def cook(name):
        relative='textures/'+name+'.t3x';source=ASSETS/relative
        old=source.read_bytes();w,h,fmt,levels,base=payload(old)
        # Existing water mip chains keep their independently accepted profile.
        if levels or min(w,h)<16:return None
        # Preassembled Mii billboards retain their RGBA pixels. This cooker
        # appends ETC blocks only; do not reinterpret RGBA as compressed ETC.
        if 0<=fmt<12:return None
        if fmt not in (12,13):raise ValueError('Unsupported static world format: '+relative)
        png=source.with_suffix('.png');image=png.read_bytes();target=OUT/relative
        previous=old_receipt.get('textures',{}).get(relative)
        if previous and previous['originalSha256']==digest(old) and previous['pngSha256']==digest(image) and target.is_file() and previous['sha256']==digest(target.read_bytes()):
            return relative,previous
        subprocess.run([encoder,'-f','etc1a4' if fmt==13 else 'etc1','-q','high','-m','box','-z','none','-o',str(target),str(png)],check=True,stdout=subprocess.DEVNULL)
        encoded=target.read_bytes();mw,mh,mfmt,n,chain=payload(encoded)
        if (mw,mh,mfmt)!=(w,h,fmt) or n<1 or min(w,h)//(2**n)<8:raise ValueError('Invalid world mip sizes: '+relative)
        sizes=[len(base)//(4**i) for i in range(n+1)]
        if sum(sizes)!=len(chain):raise ValueError('Invalid world mip payload: '+relative)
        # Keep near detail and original UV coordinates exactly, even if an
        # encoder version differs from the original maximum-resolution one.
        count=struct.unpack_from('<H',old)[0];header_bytes=5+count*12
        header=bytearray(old[:header_bytes]);header[4]=n
        chain=base+chain[len(base):]
        final=bytes(header)+struct.pack('<I',len(chain)<<8)+chain;target.write_bytes(final)
        assert payload(final)[4][:len(base)]==base
        info={'size':[w,h],'mipLevels':n,'format':fmt,'levelBytes':sizes,
              'originalSha256':digest(old),'baseSha256':digest(base),'pngSha256':digest(image),
              'sha256':digest(final),'extraGpuBytes':len(chain)-len(base)}
        return relative,info
    textures={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        for i,result in enumerate(pool.map(cook,sorted(selected))):
            if result:textures[result[0]]=result[1]
            if (i+1)%100==0:print('World mipmaps: %d / %d'%(i+1,len(selected)),flush=True)
    course_bytes={key:sum(textures['textures/'+name+'.t3x']['extraGpuBytes'] for name in names if 'textures/'+name+'.t3x' in textures) for key,names in course_sets.items()}
    receipt={'version':1,'textures':textures,'models':models,'animatedExcluded':sorted(animated),
             'extraGpuBytes':sum(info['extraGpuBytes'] for info in textures.values()),'extraGpuBytesByCourse':course_bytes,
             'maxExtraGpuBytesPerCourse':max(course_bytes.values()),'nearLevelsUnchanged':True,'filter':'box mipmaps / GPU trilinear'}
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'textures':len(textures),'animatedExcluded':len(animated),'extraGpuBytes':receipt['extraGpuBytes'],'maxExtraGpuBytesPerCourse':receipt['maxExtraGpuBytesPerCourse']}))

if __name__=='__main__':main()
