#!/usr/bin/env python3
"""Complete Mii race/menu resources and their generated source metadata."""
import argparse,json,os,shutil,struct,subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from cook_roster import ROOT,data_partition,sha256
from extract_disc import Disc
from unpack_szs import unpack
from cook_garage import VEHICLES,cook_choice,inspect_kart,inspect_driver
from cook_model import cook


def export(source,work,model,output,prefix,menu=False):
    subprocess.run(['node',str(ROOT/'build/export_brres.mjs'),str(source),str(work),model],check=True,stdout=subprocess.DEVNULL,env={**os.environ,('MKW_MENU_MATERIALS' if menu else 'MKW_RACE_MATERIALS'):'1'})
    parts=cook(work,output,prefix,max_texture=256)
    if len(parts)!=1:raise ValueError(prefix)
    return output/'models'/(parts[0]['name']+'.gmdl')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('disc',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--jobs',type=int,default=3);args=ap.parse_args()
    output=args.output;source=ROOT/'assets/mii-racers';work=ROOT/'assets/mii-racer-extra'
    inventory=json.loads((ROOT/'data/extracted/disc-files.json').read_text());files={r['name']:r for r in inventory['files']}
    audit=json.loads((output/'models/mii-garage.json').read_text());choices=audit['choices'];assert len(choices)==144
    # Every race part retains the original material's independent colour role.
    roles=[]
    for c in choices:
        for part in c['models']:
            p=ROOT/'assets/mii-racer-parts'/part/'model.json'
            if not p.exists():continue # reused rear wheels have no independent export
            shapes=json.loads(p.read_text())['models'][0]['shapes']
            if not any(s['material'] in ('mii_color','mii_skin') for s in shapes):continue
            b=(output/'models'/(part+'.gmdl')).read_bytes();version,_,_,ns,_=struct.unpack_from('<5I',b,4)
            stride={3:44,4:56,5:92}[version];assert ns==len(shapes)
            for i,s in enumerate(shapes):
                if s['material'] not in ('mii_color','mii_skin'):continue
                first,count=struct.unpack_from('<2I',b,24+i*stride)
                roles.append((part,first,count,s['material']=='mii_skin'))
    def shadow(c):
        p=export(source/c['prefix'].rstrip('_')/'kart_model.brres',work/(c['prefix']+'shadow'),'shadow',output,c['prefix']+'shadow')
        shutil.copyfile(p,output/'models'/(c['prefix']+'shadow.gmdl'))
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(shadow,choices))
    disc=Disc(args.disc)
    try:
        partition=data_partition(disc)
        for stem in sorted({c['driverSlug'] for c in choices}):
            name=f'Scene/Model/Kart/{stem}-allkart.szs';r=files[name];packed=source/Path(name).name
            packed.write_bytes(partition.read(r['offset'],r['size']));unpack(packed,source/('menu_'+stem))
        # Battle recolours exist only for the standard kart and standard bike.
        teams=[]
        for c in choices:
            if c['slot'] not in (0,3):continue
            for team in ('red','blue'):
                name=c['archive'].replace('-','_'+team+'-',1);r=files[name];packed=source/Path(name).name
                packed.write_bytes(partition.read(r['offset'],r['size']))
                row=dict(c,prefix=c['prefix']+team+'_',archive=name)
                folder=source/row['prefix'].rstrip('_');unpack(packed,folder);inspect_kart(folder)
                teams.append(row)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(lambda c:cook_choice(c,output,source_root=source,export_root=ROOT/'assets/mii-racer-parts',include_driver=False),teams))
    finally:disc.f.close()
    params=(ROOT/'assets/menu-ui/MenuSingle/parameter/kartDriverDispParam.bin').read_bytes()
    base_y=[20,20,20,47,65,50,45,33,60,31,24,66,68,48,21,40,30,37,45,51,60,60,62,59,35,38,54,51,45,60,60,48,60,48,60,51]
    def menu(c):
        stem=c['driverSlug'];vehicle=c['vehicle'];prefix=c['prefix'];kartstem=VEHICLES['sml'[c['driverIndex']//4]][c['slot']][2]
        export(source/('menu_'+stem)/(kartstem+'.brres'),work/(prefix+'menu'),'menu',output,prefix+'menu',True)
        y,z=struct.unpack_from('>2f',params,8+(vehicle*48+c['character'])*56)
        return (y+base_y[vehicle],z)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:seats=list(pool.map(menu,choices))
    for stem in sorted({c['driverSlug'] for c in choices}):
        subprocess.run(['node',str(ROOT/'build/export_driver.mjs'),str(ROOT/'assets/menu-models/Driver'/(stem+'.brres')),str(output/'models'/('ui_mii_'+stem+'_seated.dskm')),'model'],check=True,stdout=subprocess.DEVNULL,env={**os.environ,'MKW_ANIMATION_SOURCE':str(source/('menu_'+stem)/'driver_anim.brres')})
    lines=['#pragma once','#include <mkw/garage.hpp>','namespace mkw {','inline constexpr std::array<GarageChoice,144> MiiGarageChoices{{']
    for c in choices:
        lines.append(' {%s,%s,%d,%d,%.9ff,%.9ff,%s,%d,%s,%d},'%(json.dumps(c['name']),json.dumps(c['prefix']),c['character'],c['vehicle'],c['seatY'],c['seatZ'],str(c['bike']).lower(),c['extrasMask'],str(c['bodyAnimation']).lower(),c['extraAnimationMask']))
    lines+=['}};','inline constexpr float MiiMenuSeats[144][2]={']+[' {%.9ff,%.9ff},'%s for s in seats]+['};','struct MiiRaceMaterial {const char* model;unsigned first,count;bool skin;};','inline constexpr MiiRaceMaterial MiiRaceMaterials[]={']
    lines+=[' {%s,%d,%d,%s},'%(json.dumps(name),first,count,str(skin).lower()) for name,first,count,skin in roles]
    lines+=['};','inline unsigned miiGarageOffset(unsigned character){const unsigned id=character-24;return (id/6*4+id%6)*VehiclesPerDriver;}','inline const GarageChoice& raceGarageChoice(unsigned index){return index<GarageChoices.size()?GarageChoices[index]:MiiGarageChoices[(index-GarageChoices.size())%MiiGarageChoices.size()];}','}']
    (ROOT/'include/mkw/mii_garage.hpp').write_text('\n'.join(lines)+'\n')
    (output/'models/mii-complete.json').write_text(json.dumps({'races':len(choices),'team_variants':len(teams),'menu_machines':len(seats),'material_roles':len(roles)},indent=2)+'\n')

if __name__=='__main__':main()
