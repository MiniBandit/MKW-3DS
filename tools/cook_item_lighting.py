#!/usr/bin/env python3
"""Refresh world item GX lighting from extracted archives, without reading the disc."""
import json,struct
from cook_race_items import ROOT,WORLD,cook_world,digest

def main():
    shared=ROOT/'sd-card/3ds/mkw-3ds';out=ROOT/'build/item-lighting'
    manifest=json.loads((shared/'models/race-items.json').read_text())
    receipt={'version':1,'files':{},'items':{}}
    for ident,(archive,model,clip) in WORLD.items():
        # Fake boxes use their separately verified, original GX material binder.
        if ident==3:continue
        original=manifest['items'][str(ident)]
        source=ROOT/original['source']
        if digest(source)!=original['source_sha256']:raise ValueError('Changed item archive: '+archive)
        record=cook_world(source,model,'item_'+str(ident),clip,out)
        exported=json.loads((ROOT/'assets/race-items'/('item_'+str(ident))/'model.json').read_text())
        for m in exported['models']:
            for s in m['shapes']:
                if not s.get('retail'):raise ValueError('Missing GX state: '+s['material'])
        if clip:
            bank=(out/record['animation']).read_bytes();mesh=(out/record['path']).read_bytes()
            if struct.unpack_from('<I',bank,8)!=struct.unpack_from('<I',mesh,8):raise ValueError('Item skin vertex mismatch')
        manifest['items'][str(ident)]=record
        receipt['items'][str(ident)]={'archiveSha256':digest(source),'materials':[
            {'name':s['material'],'retail':s['retail'],'reflection':s.get('reflection')}
            for m in exported['models'] for s in m['shapes']]}
    (out/'models/race-items.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for folder,suffixes in [('models',('.gmdl','.dskm','.dobj','.json')),('textures',('.t3x',))]:
        for p in sorted((out/folder).iterdir()):
            if p.name.startswith('._') or p.suffix not in suffixes or p.stem.endswith('_000'):continue
            if p.suffix=='.json' and p.name!='race-items.json':continue
            rel=p.relative_to(out).as_posix()
            if not (shared/rel).is_file() and rel!='models/item_14.dobj':raise ValueError('Unexpected new item path: '+rel)
            receipt['files'][rel]={'sha256':digest(p),'originalSha256':digest(shared/rel) if (shared/rel).is_file() else None}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Verified lighting for',len(receipt['items']),'world items;',len(receipt['files']),'overlay files')

if __name__=='__main__':main()
