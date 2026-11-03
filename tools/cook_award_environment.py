"""Bind original ceremony KMP placements to the cooked model catalog."""
import json
import struct
from pathlib import Path
from cook_race_flow import f

ROOT=Path(__file__).resolve().parents[1]

def cook_environment(source, models):
    from cook_award_drive import cook_drive
    cook_drive(source)
    from cook_vehicle_rest import cook_vehicle_rest
    cook_vehicle_rest()
    from cook_award_lighting import cook_lighting
    cook_lighting(source)
    sources=json.loads((source/'award-sources.json').read_text())
    archives=json.loads((source/'scene-bindings.json').read_text())['archives']
    flow=(ROOT/'assets/common/ObjFlow.bin').read_bytes()
    resources={}
    for i in range(struct.unpack_from('>H',flow)[0]):
        offset=2+i*116
        resources[struct.unpack_from('>H',flow,offset)[0]]=flow[offset+34:offset+98].split(b'\0')[0].decode()
    lookup={(m['resource'],m['model']):i for i,m in enumerate(models)}
    header=['// Generated from original ceremony KMP and ObjFlow.bin.', '#pragma once',
            '#include <mkw/award_environment.hpp>','namespace mkw {']
    scenes=[]
    for name in ('winningrun_demo','loser_demo','draw_demo','ending_demo'):
        prefix='unpacked/'+name+'/'
        records=sources['scenes'][name]['kmp']['GOBJ']['records']
        header.append('inline constexpr AwardEnvironmentObject AwardObjects_'+name+'[]={')
        for row in records:
            resource=resources[row['id']]
            if ';' in resource:raise ValueError('Multi-resource ceremony object '+resource)
            archive=prefix+resource+'.brres'
            index=lookup.get((archive,resource),-1)
            clip=None
            if index>=0 and models[index]['animation']:
                names={a['name'] for a in archives[archive]['skeletal']}
                clip=resource if resource in names else 'o_jg-demowin' if resource=='DemoJugemu' else None
                if clip not in names:raise ValueError('Unbound animation '+archive)
            # Effect and helper records have no mesh; preserve them for their
            # own controller, never replace them with a guessed model.
            if index<0 and resource not in ('-','DemoEf','Kamifubuki'):
                raise ValueError('Missing ceremony model '+archive)
            vec=lambda key:'{'+','.join(f(v) for v in row[key])+'}'
            header.append('{'+f'{row["id"]},{row["index"]},{index},{row["route"]},{row["presence"]},'+
                          (json.dumps(clip) if clip else 'nullptr')+','+
                          ','.join(vec(k) for k in ('position','rotation','scale'))+',{'+
                          ','.join(str(v) for v in row['settings'])+'}},')
        header.append('};')
        routes=sources['scenes'][name]['kmp']['POTI']['records']
        for route in routes:
            raw=struct.pack('>HBB',len(route['points']),route['smooth'],route['loop'])
            for point in route['points']:
                raw+=struct.pack('>3f2H',*point['position'],*point['settings'])
            header.append('alignas(4) inline constexpr unsigned char AwardRoute_'+name+'_'+str(route['index'])+'[]={'+','.join(str(v) for v in raw)+'};')
        if routes:
            header.append('inline constexpr AwardEnvironmentRoute AwardRoutes_'+name+'[]={'+','.join('{AwardRoute_'+name+'_'+str(r['index'])+'}' for r in routes)+'};')
        starts=sources['scenes'][name]['kmp']['KTPT']['records']
        if starts:
            header.append('inline constexpr AwardEnvironmentStart AwardStarts_'+name+'[]={')
            for start in starts:
                vec=lambda key:'{'+','.join(f(v) for v in start[key])+'}'
                header.append('{'+vec('position')+','+vec('rotation')+','+str(start['settings'][0])+'},')
            header.append('};')
        course=lookup[(prefix+'course_model.brres','course')]
        sky=lookup[(prefix+'vrcorn_model.brres','vrcorn')]
        scenes.append('{'+json.dumps(name)+f',{course},{sky},AwardObjects_{name},{len(records)},'+('AwardRoutes_'+name if routes else 'nullptr')+f',{len(routes)},'+('AwardStarts_'+name if starts else 'nullptr')+f',{len(starts)}'+'}')
    header.append('inline constexpr AwardEnvironmentDef AwardEnvironments[]={'+','.join(scenes)+'};')
    header.append('}')
    (ROOT/'include/mkw/award_environment_data.hpp').write_text('\n'.join(header)+'\n')

if __name__=='__main__':
    cook_environment(ROOT/'build/award-source',json.loads((ROOT/'build/award-models/award-models.json').read_text())['models'])
