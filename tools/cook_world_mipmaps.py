#!/usr/bin/env python3
"""Add trilinear minification levels to static course and world textures.

Retain the existing maximum-resolution ETC payload and T3X UV rectangle.
Animation atlases are excluded globally, since coarse mips across different
frames would bleed. Where the source TEX0 has mip levels (often hand-made
distance art: Rainbow Road's road, Coconut Mall's grass), each of our levels is the original
level of the same size, decoded like tools/cook_model.py's level 0; levels
below the original chain are box-filtered from its last level. Other textures
are box-filtered from level 0. The level count and sizes are the same either
way. Original assets remain untouched; package_sd overlays the result and
pack_race_assets includes the same replacements in race.pak.
"""
import argparse, concurrent.futures, hashlib, json, struct, subprocess, tempfile
import tex3ds_cache
from pathlib import Path
from PIL import Image
import cook_water_mipmaps
from cook_water_mipmaps import payload, textures as original_textures, levels as original_levels, owned as water_owned, BLOCKS
from devkitpro import tool
from gx_material import GMDL_SUBMESH_BYTES

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'sd-card/3ds/mkw-3ds'
OUT=ROOT/'build/world-mipmaps'

def digest(data):return hashlib.sha256(data).hexdigest()

# Reused encodes must come from this cook as it is now: a changed or reverted
# cook (encoder flags, alpha handling) recooks every texture. Line endings
# are ignored so a fresh clone and a working tree agree.
def cook_profile():
    code=b''.join(Path(p).read_bytes().replace(b'\r\n',b'\n') for p in (__file__,cook_water_mipmaps.__file__))
    return digest(code+Path(tool('tex3ds')).name.encode())

def original_sources():
    """Our texture name -> (BRRES files in search order, TEX0 name), from the
    receipts of the course and sky cooks (tools/build.py courses) and the
    object cook (tools/cook_course_objects.py: an object archive may borrow
    textures from the course and sky models). Prepared replacements (Mii
    textures) map to None."""
    out={}
    def add(stem,archives):
        receipt=ASSETS/'models'/(stem+'-receipt.json')
        if not receipt.is_file():return
        for t in json.loads(receipt.read_text())['textures']:
            entry=None if 'preparedImage' in t else (tuple(archives),t['source'])
            if out.setdefault(t['name'],entry)!=entry:raise ValueError('Texture has two sources: '+t['name'])
    catalog=json.loads((ASSETS/'models/course-catalog.json').read_text())
    for course in catalog['courses']:
        folder=ROOT/'assets/courses'/course['key']
        for visual in json.loads((ASSETS/course['receipt']).read_text())['visuals']:
            if visual['parts']:add(visual['parts'][0]['name'].rsplit('_',1)[0],[folder/visual['resource']])
        visuals=ASSETS/f"models/course_{course['id']:02d}_visuals.list"
        if visuals.is_file():
            for row in visuals.read_text().splitlines():
                resource,_,mesh,_=row.split()
                add(mesh.rsplit('_',1)[0],[folder/(resource+'.brres'),folder/'course_model.brres',folder/'vrcorn_model.brres'])
    return out

def original_chain(tex0,w,h,fmt,n,png,scratch,encode):
    """Our levels 1..n from the TEX0's levels of the same sizes, as (payload,
    detail), or (None, reason) when it has none to give."""
    width,height,gx,images,data=tex0
    if images<2:return None,'no original mip levels'
    if gx not in BLOCKS:return None,'TEX0 format %d'%gx
    # cook_model.py's texel budget keeps the source size or halves both axes
    # alike (then our level k is original level k+shift); other shapes have
    # no original level of our sizes.
    shift=next((s for s in range(images) if (width>>s,height>>s)==(w,h)),None)
    if shift is None:return None,'resized to %dx%d from %dx%d'%(w,h,width,height)
    if shift+1>=images:return None,'no original level below %dx%d'%(w,h)
    decoded=original_levels(width,height,gx,images,data,shift+n+1)
    # Level 0 must give the shipped base image: decoded as is, or resized as
    # cook_model.py resizes it. That proves the mapping exactly, so original
    # level `shift` (our base size) is not compared: on hand-made chains it
    # differs from the resized level 0 by design.
    level0=decoded[0] if shift==0 else decoded[0].resize((w,h),Image.Resampling.LANCZOS)
    with Image.open(png) as shipped:
        if shipped.convert('RGBA').tobytes()!=level0.tobytes():return None,'level 0 differs from the shipped image'
    last=min(images-1-shift,n);chain=[]
    for k in range(1,last+1):
        part=scratch/f'{k}.png';decoded[k+shift].save(part)
        # The last original level also gives the box-filtered levels below it.
        box=k==last and last<n
        lw,lh,lf,m,raw=encode(part,part.with_suffix('.t3x'),fmt,box)
        if (lw,lh,lf,m)!=(w>>k,h>>k,fmt,n-k if box else 0):raise ValueError('Invalid encoded level %d of %s'%(k,png.name))
        chain.append(raw)
    return b''.join(chain),{'levelShift':shift,'originalLevels':last,'boxLevels':n-last}

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
    profile=cook_profile()
    if old_receipt.get('profile')!=profile:old_receipt={}
    encoder=str(tool('tex3ds'))
    def encode(png,target,fmt,box):
        tex3ds_cache.run([encoder,'-f','etc1a4' if fmt==13 else 'etc1','-q','high',*(('-m','box') if box else ()),'-z','none','-o',str(target),str(png)],check=True,stdout=subprocess.DEVNULL)
        return payload(target.read_bytes())
    # Source TEX0s; Psea's mip-filtered textures keep cook_water_mipmaps.py's chains.
    sources=original_sources();water=set(water_owned());archives={}
    for name in sorted(selected):
        entry=sources.get(name)
        for path in entry[0] if entry else ():
            if path not in archives and path.is_file():
                data=path.read_bytes();archives[path]=(path.relative_to(ROOT).as_posix(),digest(data),original_textures(data,path))
    def original(name):
        if not sources.get(name) or name in water:return None
        paths,tex0=sources[name]
        for path in paths:
            if path in archives and tex0 in archives[path][2]:return archives[path],tex0
        raise ValueError('No source TEX0 %s for %s'%(tex0,name))
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
        found=original(name)
        origin={'brres':found[0][0],'brresSha256':found[0][1],'tex0':found[1]} if found else None
        previous=old_receipt.get('textures',{}).get(relative)
        if previous and previous['originalSha256']==digest(old) and previous['pngSha256']==digest(image) and previous.get('origin')==origin and target.is_file() and previous['sha256']==digest(target.read_bytes()):
            return relative,previous
        # PICA levels stop at 8x8 (one ETC tile), as tex3ds -m box does.
        n=0
        while min(w,h)>>(n+1)>=8:n+=1
        if n<1:raise ValueError('Invalid world mip sizes: '+relative)
        sizes=[len(base)//(4**i) for i in range(n+1)]
        with tempfile.TemporaryDirectory() as scratch:
            chain,detail=original_chain(found[0][2][found[1]],w,h,fmt,n,png,Path(scratch),encode) if found else (None,None)
        if chain is None:
            mw,mh,mfmt,m,chain=encode(png,target,fmt,True)
            if (mw,mh,mfmt,m)!=(w,h,fmt,n):raise ValueError('Invalid world mip sizes: '+relative)
            chain=chain[len(base):]
        if sum(sizes)!=len(base)+len(chain):raise ValueError('Invalid world mip payload: '+relative)
        # Keep near detail and original UV coordinates exactly, even if an
        # encoder version differs from the original maximum-resolution one.
        count=struct.unpack_from('<H',old)[0];header_bytes=5+count*12
        header=bytearray(old[:header_bytes]);header[4]=n
        chain=base+chain
        final=bytes(header)+struct.pack('<I',len(chain)<<8)+chain;target.write_bytes(final)
        assert payload(final)[4][:len(base)]==base
        info={'size':[w,h],'mipLevels':n,'format':fmt,'levelBytes':sizes,'levels':'original' if isinstance(detail,dict) else 'box',
              'originalSha256':digest(old),'baseSha256':digest(base),'pngSha256':digest(image),
              'sha256':digest(final),'extraGpuBytes':len(chain)-len(base)}
        if origin:info.update(origin=origin,**(detail if isinstance(detail,dict) else {'boxReason':detail}))
        return relative,info
    textures={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        for i,result in enumerate(pool.map(cook,sorted(selected))):
            if result:textures[result[0]]=result[1]
            if (i+1)%100==0:print('World mipmaps: %d / %d'%(i+1,len(selected)),flush=True)
    course_bytes={key:sum(textures['textures/'+name+'.t3x']['extraGpuBytes'] for name in names if 'textures/'+name+'.t3x' in textures) for key,names in course_sets.items()}
    original_count=sum(info['levels']=='original' for info in textures.values())
    receipt={'version':2,'profile':profile,'textures':textures,'models':models,'animatedExcluded':sorted(animated),
             'extraGpuBytes':sum(info['extraGpuBytes'] for info in textures.values()),'extraGpuBytesByCourse':course_bytes,
             'maxExtraGpuBytesPerCourse':max(course_bytes.values()),'nearLevelsUnchanged':True,'originalLevelTextures':original_count,
             'filter':'original or box mipmaps / GPU trilinear'}
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'textures':len(textures),'originalLevels':original_count,'animatedExcluded':len(animated),'extraGpuBytes':receipt['extraGpuBytes'],'maxExtraGpuBytesPerCourse':receipt['maxExtraGpuBytesPerCourse']}))

if __name__=='__main__':main()
