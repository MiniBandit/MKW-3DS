#!/usr/bin/env python3
"""Re-cook course and sky meshes into a private overlay (default build/course-materials).

The shared SD tree (sd-card/3ds/mkw-3ds) and the unpacked archives are only read.
Each selected course's course/sky model is exported (into a temporary work
folder) as tools/import_courses.py does, with the course material rules
(MKW_COURSE_MATERIALS), and cooked;
meshes identical to the shared tree are dropped. Textures and part lists must be
unchanged (a mesh-only overlay), otherwise run the full importer instead. The
course receipt's visuals are updated in the overlay copy. tools/resource_manifest.py
overlays the result.

Placed-object meshes are a separate manually invoked operation; the helper uses
tools/cook_course_objects.py does, under the same mesh-only rule (animation
banks are replaced only when their exported material tracks change; textures
stay the shared tree's).
"""
import argparse,contextlib,hashlib,io,json,os,shutil,subprocess
from pathlib import Path
from course_catalog import ROOT,courses
from cook_model import cook
SD=ROOT/'sd-card/3ds/mkw-3ds'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def recook(source,args,env,prefix,output,texture_prefix=None,preserve_vertices=False):
    """Export one model and cook it as its original tool does, then copy every
    part that differs from the shared tree into the overlay. Returns the
    changed part names and the cook receipt."""
    work=output.parent/(output.name+'-work');shutil.rmtree(work,ignore_errors=True)
    exported=work/'export';exported.mkdir(parents=True)
    with (exported/'export.log').open('w') as log:
        subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(exported),*args],env={**os.environ,**env},stdout=log,stderr=subprocess.STDOUT,check=True)
    sources=[t['name'] for t in json.loads((exported/'model.json').read_text())['textures']]
    if sources!=[t['source'] for t in json.loads((SD/'models'/f'{prefix}-receipt.json').read_text())['textures']]:raise ValueError('Texture list changed: '+prefix)
    # Textures are unchanged by construction: skip encoding, keep the audit.
    with contextlib.redirect_stdout(io.StringIO()):
        parts=cook(exported,work,prefix,256,preserve_vertices=preserve_vertices,texture_prefix=texture_prefix,
                   encoded_textures={f'{texture_prefix or prefix}_{i:03d}' for i in range(len(sources))})
    if (work/'models'/f'{prefix}.list').read_text()!=(SD/'models'/f'{prefix}.list').read_text():raise ValueError('Part layout changed: '+prefix)
    changed=[]
    for part in parts:
        mesh=work/'models'/f"{part['name']}.gmdl"
        if mesh.read_bytes()!=(SD/'models'/mesh.name).read_bytes():
            (output/'models').mkdir(parents=True,exist_ok=True);shutil.copyfile(mesh,output/'models'/mesh.name);changed.append(mesh.name)
    detail=json.loads((work/'models'/f'{prefix}-receipt.json').read_text())
    shutil.rmtree(work)
    return changed,detail

def overlay_course(c,output):
    receipt=json.loads((SD/f"models/courses/{c['id']:02d}.json").read_text())
    folder=ROOT/'assets/courses'/c['key']
    if digest(ROOT/'data/extracted'/c['archive'])!=receipt['archive_sha256']:raise ValueError('Source archive changed: '+c['name'])
    visuals={v['kind']:v for v in receipt['visuals']};changed=[]
    for label,resource in [('course','course_model.brres'),('sky','vrcorn_model.brres')]:
        # Coconut Mall's sky is the legacy coconut_sky mesh; the importer reuses it.
        if label not in visuals or (c['id'],label)==(5,'sky'):continue
        if digest(folder/resource)!=receipt['resources'][resource]['sha256']:raise ValueError(f"Unpacked {resource} changed: {c['name']}")
        # Same export settings as tools/import_courses.py: retail lighting state and course TEV rules.
        env={'MKW_FREEZE_NONLINEAR_UV':'1','MKW_COURSE_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_RETAIL_SKY':'1' if label=='sky' else '0'}
        names,detail=recook(folder/resource,[],env,c['prefix']+('_sky' if label=='sky' else ''),output)
        changed+=names
        visuals[label]={**visuals[label],'parts':detail['parts'],'lighting':detail.get('lighting'),'limitations':detail['limitations']}
    updated=[visuals[v['kind']] for v in receipt['visuals']]
    if changed or updated!=receipt['visuals']:
        (output/'models/courses').mkdir(parents=True,exist_ok=True)
        receipt['visuals']=updated
        (output/f"models/courses/{c['id']:02d}.json").write_text(json.dumps(receipt,indent=2)+'\n')
    return changed

def overlay_objects(c,output,banks_only=False):
    from cook_course_objects import CLIP_EXCLUDE,EXCLUDED,mdl0_names,safe
    folder=ROOT/'assets/courses'/c['key'];changed=[]
    # Only the packaged meshes: the course's visuals list (resource model mesh bank).
    listed={tuple(line.split()[:3]) for line in (SD/f"models/course_{c['id']:02d}_visuals.list").read_text().splitlines()}
    for archive in sorted(p for p in folder.glob('*.brres') if not p.name.startswith('._') and p.name not in EXCLUDED):
        resource=archive.stem
        for model in mdl0_names(archive):
            stem=f"obj{c['id']:02d}_{safe(resource)}__{safe(model)}"
            if (resource,model,stem+'_000') not in listed:continue
            if banks_only and not (output/'models'/f'{stem}_000.gmdl').is_file():continue
            # Same export settings as tools/cook_course_objects.py; its animation bank is discarded.
            env={'MKW_FREEZE_NONLINEAR_UV':'1','MKW_OBJECT_MATERIALS':'1','MKW_RETAIL_LIGHTING':'1','MKW_OBJECT_BANK':str(output.parent/(output.name+'-bank.dobj')),
                 'MKW_TEXTURE_SOURCES':os.pathsep.join(str(folder/n) for n in ('course_model.brres','vrcorn_model.brres') if (folder/n).is_file())}
            for (res,pattern),regex in CLIP_EXCLUDE.items():
                if res==resource and pattern==model:env['MKW_OBJECT_CLIP_EXCLUDE']=regex
            names,_=recook(archive,[model],env,stem,output,texture_prefix=f"obj{c['id']:02d}_{safe(resource)}",preserve_vertices=True)
            changed+=names
            bank=output.parent/(output.name+'-bank.dobj');original=SD/'models'/f'{stem}.dobj'
            if original.is_file() and bank.read_bytes()!=original.read_bytes():
                target=output/'models'/original.name;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(bank,target);changed.append(target.name)
    (output.parent/(output.name+'-bank.dobj')).unlink(missing_ok=True)
    return changed

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ids',type=int,nargs='+',required=True);p.add_argument('--output',type=Path,default=ROOT/'build/course-materials')
    p.add_argument('--banks-only',action='store_true',help="update animation banks for object meshes already present in the overlay")
    a=p.parse_args()
    if not (ROOT/'build/export_brres.mjs').is_file():raise FileNotFoundError('Prepare the individual BRRES exporter manually; see docs/MANUAL-BUILD.md')
    for c in courses():
        if a.ids is not None and c['id'] not in a.ids:continue
        if a.banks_only:
            if c['kind']=='race':
                changed=overlay_objects(c,a.output,True)
                print(c['id'],c['name'],' '.join(changed) or 'banks unchanged',flush=True)
            continue
        # Rebuild this course's overlay entries from scratch.
        for old in [*a.output.glob(f"models/{c['prefix']}_*.gmdl"),*a.output.glob(f"models/{c['prefix']}_sky_*.gmdl"),a.output/f"models/courses/{c['id']:02d}.json"]:
            old.unlink(missing_ok=True)
        changed=overlay_course(c,a.output)
        print(c['id'],c['name'],' '.join(changed) or 'unchanged',flush=True)

if __name__=='__main__':main()
