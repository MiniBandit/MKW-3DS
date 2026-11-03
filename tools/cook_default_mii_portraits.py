#!/usr/bin/env python3
"""Bake RMCE01's six retail default Miis from its read-only main.dol.

RFL SearchDefaultData (800ca780) scans six 0x4a records at 8024c1b8;
the default pickup at 800ca760 passes them to raw-to-info 800c6d80.
Field extraction follows that conversion's 800c6bc0 bit moves. This is
Wii RFL data, not a lossy native-3DS appearance conversion.
"""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
from PIL import Image
from bake_mii_portrait import bake

def raw_appearance(raw):
    if len(raw)!=0x4a:raise ValueError('RFL raw record must be 74 bytes')
    def bits(byte,start,count):
        n=(start+count+7)//8
        return (int.from_bytes(raw[byte:byte+n],'big')>>(8*n-start-count))&((1<<count)-1)
    fields={
        'favoriteColor':(0,11,4),'faceType':(32,0,3),'skinColor':(32,3,3),'faceFeature':(32,6,4),
        'hairType':(34,0,7),'hairColor':(34,7,3),'hairFlip':(34,10,1),
        'browType':(36,0,5),'browRotation':(36,6,4),'browColor':(38,0,3),'browScale':(38,3,4),
        'browY':(38,7,5),'browX':(38,12,4),
        'eyeType':(40,0,6),'eyeRotation':(41,0,3),'eyeY':(41,3,5),'eyeColor':(42,0,3),
        'eyeScale':(42,4,3),'eyeX':(42,7,4),'noseType':(44,0,4),'noseScale':(44,4,4),'noseY':(45,0,5),
        'mouthType':(46,0,5),'mouthColor':(46,5,2),'mouthScale':(46,7,4),'mouthY':(46,11,5),
        'glassesType':(48,0,4),'glassesColor':(48,4,3),'glassesScale':(49,0,3),'glassesY':(49,3,5),
        'mustacheType':(50,0,2),'beardType':(50,2,2),'beardColor':(50,4,3),'mustacheScale':(50,7,4),'mustacheY':(50,11,5),
        'moleEnabled':(52,0,1),'moleScale':(52,1,4),'moleY':(52,5,5),'moleX':(52,10,5),
    }
    return {name:bits(*location) for name,location in fields.items()}

def read_dol(dol,address,size):
    for i in range(18):
        offset,base,length=[struct.unpack_from('>I',dol,start+4*i)[0] for start in (0,0x48,0x90)]
        if base<=address and address+size<=base+length:
            result=dol[offset+address-base:offset+address-base+size]
            if len(result)==size:return result
    raise ValueError('Default Mii table is outside the DOL sections')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('dol',type=Path);p.add_argument('bank',type=Path);p.add_argument('exporter',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--statue-courses',type=Path,nargs='*',default=[],help='Course folders whose statue materials should be baked into the default head pool')
    a=p.parse_args();dol=a.dol.read_bytes();a.output.mkdir(parents=True,exist_ok=True)
    rows=[];sheet=Image.new('RGBA',(128*6,128),(96,96,96,255));portraits=[]
    for i in range(6):
        raw=read_dol(dol,0x8024c1b8+0x4a*i,0x4a)
        # Reject other game/version tables rather than rendering arbitrary bytes.
        if raw[24:28]!=bytes([0x80,0,0,i]):raise ValueError('Not the supported RMCE01 default table')
        spec=raw_appearance(raw);folder=a.output/f'default-{i}';folder.mkdir(exist_ok=True)
        params=folder/'appearance.txt';params.write_text(''.join(f'{k} {v}\n' for k,v in spec.items()))
        subprocess.run([str(a.exporter.resolve()),str(a.bank.resolve()),str(params),str(folder)],check=True)
        im,triangles=bake(folder);target=a.output/f'default-{i}.png';im.save(target);sheet.alpha_composite(im,(128*i,0))
        small,_=bake(folder,64);portraits.append(small)
        rows.append(dict(index=i,rawSha256=hashlib.sha256(raw).hexdigest(),appearance=spec,image=target.name,pngSha256=hashlib.sha256(target.read_bytes()).hexdigest(),triangles=triangles))
    sheet.save(a.output/'defaults-preview.png')
    # Retail MiiObj::LoadModels requests 4x4 cells and 64x64 captures.
    # Offline fixed default pool intentionally does not sample console/contact
    # Miis at race time. Explicit ordering makes the baked result reproducible.
    slots=[0,3,1,4,2,5,3,0,4,1,5,2,0,4,2,5]
    atlas=Image.new('RGBA',(256,256))
    for cell,index in enumerate(slots):atlas.paste(portraits[index],((cell%4)*64,(cell//4)*64))
    atlas.save(a.output/'default-faces-atlas.png')
    # CarA captures 2x2 at 128px; MiiPoster captures 3x3 at 192px.
    # The latter is resampled as a whole to POT storage, preserving its UVs.
    for count,name,order in [(2,'default-car-atlas.png',[0,1,2,3]),
                             (3,'default-poster-atlas.png',[0,3,1,4,2,5,0,4,2])]:
        small=Image.new('RGBA',(count*64,count*64))
        for cell,index in enumerate(order):small.paste(portraits[index],((cell%count)*64,(cell//count)*64))
        if count==3:small=small.resize((256,256),Image.Resampling.LANCZOS)
        small.save(a.output/name)
    report=dict(sourceSha256=hashlib.sha256(dol).hexdigest(),bankSha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),defaults=rows,
                atlas=dict(image='default-faces-atlas.png',columns=4,rows=4,cellSize=64,slots=slots,policy='offline fixed retail-default pool; not retail runtime contact/random selection'))
    if a.statue_courses:
        from mii_statue_material import StatueMaterial
        report['statues']=[]
        for course in a.statue_courses:
            material=StatueMaterial(course)
            folder=a.output/'statues'/course.name;folder.mkdir(parents=True,exist_ok=True)
            images=[]
            for i in range(6):
                im,_=bake(a.output/f'default-{i}',statue_material=material)
                target=folder/f'default-{i}.png';im.save(target)
                images.append(dict(index=i,image=str(target.relative_to(a.output)),sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
            report['statues'].append(dict(course=course.name,images=images,
                sourceHashes={n:hashlib.sha256((course/n).read_bytes()).hexdigest() for n in ['course.bmm','course.btiMat','course.btiEnv']},
                policy='offline front-view material and reflection; race-view reflections are intentionally fixed'))
    (a.output/'defaults.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Baked {len(rows)} retail default portraits; course assignment remains separate')
if __name__=='__main__':main()
