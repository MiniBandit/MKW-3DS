#!/usr/bin/env python3
"""Import all local retail maps through one pipeline; never modifies gameplay data.

Per-map receipts retain archive/resource identities, original KMP sections and
KCL surface flags. A rough visual import does not imply object or mode parity.
"""
import argparse,collections,concurrent.futures,hashlib,json,os,struct,subprocess,sys
from pathlib import Path
from course_catalog import ROOT,courses
sys.path.insert(0,str(ROOT/'vendor/gclib'))
from unpack_szs import unpack
from cook_model import cook
OUT=ROOT/'sd-card/3ds/mkw-3ds'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,data):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(data,indent=2)+'\n')
def kmp_database(path):
    b=path.read_bytes()
    if b[:4]!=b'RKMD' or len(b)<16:raise ValueError('Invalid KMP')
    size,n,h=struct.unpack_from('>IHH',b,4)
    if size!=len(b) or h<16+4*n:raise ValueError('Invalid KMP header')
    offsets=[h+struct.unpack_from('>I',b,16+4*i)[0] for i in range(n)]+[size]
    if offsets!=sorted(offsets) or offsets[-2]+8>size:raise ValueError('Invalid KMP offsets')
    result={}
    for start,end in zip(offsets,offsets[1:]):
        tag=b[start:start+4].decode('ascii');count,extra=struct.unpack_from('>HH',b,start+4);body=b[start+8:end]
        section={'count':count,'header_extra':extra,'offset':start,'bytes':end-start,'sha256':hashlib.sha256(b[start:end]).hexdigest(),'raw_hex':body.hex()}
        records=[]
        strides={'KTPT':28,'ENPT':20,'ENPH':16,'ITPT':20,'ITPH':16,'CKPT':20,'CKPH':16,'GOBJ':60,'AREA':48,'CAME':72,'JGPT':28,'CNPT':28,'MSPT':28,'STGI':12}
        if tag in strides:
            stride=strides[tag]
            # Original draw_demo uses the older 8-byte STGI record; retain
            # that record as-is instead of inventing the later four bytes.
            if tag=='STGI' and count and len(body)==count*8:stride=8
            if len(body)<count*stride:raise ValueError('Truncated '+tag)
            for i in range(count):
                raw=body[i*stride:(i+1)*stride];r={'index':i,'raw_hex':raw.hex()}
                if tag in ('ENPT','ITPT'):r.update(position=struct.unpack_from('>3f',raw),width=struct.unpack_from('>f',raw,12)[0],settings=struct.unpack_from('>HH',raw,16))
                elif tag in ('ENPH','ITPH','CKPH'):r.update(start=raw[0],length=raw[1],previous=list(raw[2:8]),next=list(raw[8:14]),settings=list(raw[14:16]))
                elif tag=='GOBJ':r.update(id=struct.unpack_from('>H',raw)[0],position=struct.unpack_from('>3f',raw,4),rotation=struct.unpack_from('>3f',raw,16),scale=struct.unpack_from('>3f',raw,28),route=struct.unpack_from('>h',raw,40)[0],settings=struct.unpack_from('>8H',raw,42),presence=struct.unpack_from('>H',raw,58)[0])
                elif tag in ('KTPT','JGPT','CNPT','MSPT'):r.update(position=struct.unpack_from('>3f',raw),rotation=struct.unpack_from('>3f',raw,12),settings=struct.unpack_from('>hh',raw,24))
                elif tag=='CKPT':r.update(left=struct.unpack_from('>2f',raw),right=struct.unpack_from('>2f',raw,8),respawn=raw[16],type=raw[17],previous=raw[18],next=raw[19])
                records.append(r)
        elif tag=='POTI':
            at=0
            for i in range(count):
                points,smooth,loop=struct.unpack_from('>HBB',body,at);at+=4;route={'index':i,'smooth':smooth,'loop':loop,'points':[]}
                for _ in range(points):
                    x,y,z,a,c=struct.unpack_from('>3fHH',body,at);at+=16;route['points'].append({'position':[x,y,z],'settings':[a,c]})
                records.append(route)
        section['records']=records;result[tag]=section
    return result

def import_one(c,force=False):
    source=ROOT/'data/extracted'/c['archive'];receipt_path=OUT/'models/courses'/f"{c['id']:02d}.json"
    receipt={**c,'status':'failed','battle_modes_implemented':False,'runtime_validation':'pending','limitations':[]}
    try:
        source_hash=digest(source)
        if not force and receipt_path.exists():
            old=json.loads(receipt_path.read_text())
            if old.get('status')=='imported' and old.get('archive_sha256')==source_hash and all((OUT/p).exists() for p in old.get('model_lists',[])):
                return old
        receipt.update(archive_sha256=source_hash,archive_bytes=source.stat().st_size)
        folder=ROOT/'assets/courses'/c['key'];unpack(source,folder)
        receipt['resources']={p.relative_to(folder).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(folder.rglob('*')) if p.is_file() and not p.name.startswith('._')}
        for required in ('course.kmp','course.kcl','course_model.brres'):
            if required not in receipt['resources']:raise ValueError('Missing '+required)
        receipt['kmp']=kmp_database(folder/'course.kmp')
        kcl=(folder/'course.kcl').read_bytes();prisms,octree=struct.unpack_from('>II',kcl,8)
        if prisms+16>octree or octree>len(kcl) or (octree-prisms)%16:raise ValueError('Invalid KCL prism bounds')
        flags=collections.Counter(struct.unpack_from('>H',kcl,p+14)[0] for p in range(prisms+16,octree,16))
        receipt['kcl']={'prisms':sum(flags.values()),'surface_flags':{f'0x{f:04x}':n for f,n in sorted(flags.items())}}
        objects=collections.Counter(r['id'] for r in receipt['kmp'].get('GOBJ',{}).get('records',[]))
        receipt['object_requirements']=[{'id':i,'instances':n,'visual_implementation':'item renderer' if i in (0x65,0x76,0xc9,0xd4,0xee) else 'Coconut object renderer' if c['id']==5 and i in (0x145,0x25e,0x260) else 'course object renderer'} for i,n in sorted(objects.items())]
        receipt['model_lists']=[];receipt['visuals']=[]
        # Existing validated geometry/object assets remain byte-for-byte unchanged.
        for label,resource in [('course','course_model.brres'),('sky','vrcorn_model.brres'),('map','map_model.brres')]:
            if not (folder/resource).exists():
                if label in ('sky','map'):
                    if label=='sky':receipt['limitations'].append('No source sky model')
                    continue
                raise ValueError('Missing '+resource)
            legacy={(5,'sky'):'coconut_sky'}
            suffix='_sky' if label=='sky' else '_map' if label=='map' else ''
            prefix=legacy.get((c['id'],label),c['prefix']+suffix)
            exported=ROOT/'build/course-exports'/c['key']/label
            if (c['id'],label) not in legacy:
                exported.mkdir(parents=True,exist_ok=True)
                # Retail course lighting state per submesh (GMDL v4); the course's
                # lights and fog are cooked separately (tools/cook_course_lighting.py).
                export_env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_RETAIL_LIGHTING':'1','MKW_RETAIL_SKY':'1' if label=='sky' else '0'}
                # Course/sky TEV rules (tools/export_brres.ts courseRule); the minimap keeps its fallbacks.
                if label in ('course','sky'):export_env['MKW_COURSE_MATERIALS']='1'
                with (exported/'export.log').open('w') as log:
                    subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(folder/resource),str(exported)],env=export_env,stdout=log,stderr=subprocess.STDOUT,check=True)
                cook(exported,OUT,prefix,256)
            model_list=f'models/{prefix}.list'
            if not (OUT/model_list).is_file():raise ValueError('Missing cooked model list '+model_list)
            parts=(OUT/model_list).read_text().split()
            if not parts:raise ValueError('Empty '+model_list)
            receipt['model_lists'].append(model_list)
            detail=json.loads((OUT/f'models/{prefix}-receipt.json').read_text())
            receipt['visuals'].append({'kind':label,'list':model_list,'parts':detail['parts'],'textures':detail['textures'],'lighting':detail.get('lighting'),'limitations':detail['limitations']})
        # Placed objects: one mesh (+ DOBJ animation bank) per model of every
        # object archive (tools/cook_course_objects.py), listed per course in
        # models/course_NN_visuals.list. Arenas too (0.5.0): their objects and
        # Coin Runners coins are converted ahead of the battle runtime.
        if c['kind'] in ('race','arena'):
            from cook_course_objects import cook_course
            report=cook_course(c,False)
            receipt['object_visuals']=f'models/course_{c["id"]:02d}_visuals.list'
            receipt['object_models']=len(report)
        receipt['limitations']+=['Rough course/sky import; course materials without a runtime equivalent use documented fallbacks (docs/COURSE_IMPORT.md)','Retail course lighting: GX channels, EGG light textures and fog per docs/COURSE_LIGHTING.md; shadows remain unsupported','Fog, indirect effects, cameras, audio and non-linear animations are not fully implemented','Race availability requires native host smoke check; complete-lap and hardware performance parity are unverified']
        if c['kind']=='arena':receipt['limitations'].append('Asset import only: Balloon Battle and Coin Runners are not implemented')
        receipt['status']='imported'
    except Exception as e:receipt['error']=str(e)
    write(receipt_path,receipt);print(c['id'],c['name'],receipt['status'],receipt.get('error',''),flush=True);return receipt

def publish(receipts):
    # The runtime catalog remains small; verbose extraction evidence is separate.
    compact=[]
    for r in receipts:
        v={k:r[k] for k in ('id','enum','name','kind','cup','slot','archive','key','status')}
        v.update(archive_sha256=r.get('archive_sha256'),model_lists=r.get('model_lists',[]),receipt=f"models/courses/{r['id']:02d}.json",runtime_validation=r.get('runtime_validation','pending'))
        compact.append(v)
    write(OUT/'models/course-catalog.json',{'version':1,'courses':compact,'battle_modes_implemented':False})
    write(ROOT/'build/course-import-summary.json',{'version':1,'imported_races':sum(r['status']=='imported' and r['kind']=='race' for r in receipts),'imported_arenas':sum(r['status']=='imported' and r['kind']=='arena' for r in receipts),'courses':compact})

def main():
    p=argparse.ArgumentParser();p.add_argument('--jobs',type=int,default=3);p.add_argument('--force',action='store_true');p.add_argument('--ids',type=int,nargs='*');a=p.parse_args()
    subprocess.run(['node','tools/bundle_cooker.mjs'],cwd=ROOT,check=True)
    selected=[c for c in courses() if a.ids is None or c['id'] in a.ids]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:receipts=list(pool.map(lambda c:import_one(c,a.force),selected))
    if a.ids is not None:
        receipts=[json.loads((OUT/f"models/courses/{c['id']:02d}.json").read_text()) for c in courses()]
    publish(receipts)
    if any(r['status']!='imported' for r in receipts):raise SystemExit(1)
if __name__=='__main__':main()
