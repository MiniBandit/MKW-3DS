"""Cook original unloaded-suspension poses for scenes without race physics."""
import struct
from pathlib import Path
from cook_garage import VEHICLES
from cook_race_flow import f
ROOT=Path(__file__).resolve().parents[1]

def cook_vehicle_rest():
    source=ROOT/'assets/common'
    stats=(source/'kartParam.bin').read_bytes()
    kart=(source/'kartPartsDispParam.bin').read_bytes()
    bike=(source/'bikePartsDispParam.bin').read_bytes()
    if struct.unpack_from('>I',stats)[0]!=36:raise ValueError('Expected 36 original vehicle rows')
    names={row[0]:row[2] for group in VEHICLES.values() for row in group}
    lines=['// Generated from original BSP, kartParam and parts display parameters.',
           '#pragma once','#include <mkw/math.hpp>','namespace mkw {',
           'struct VehicleRestWheel {Vec3 position;float travel,radius;bool enabled;};',
           'struct VehicleRestParameters {unsigned body;float initialHeight;VehicleRestWheel wheels[4];float display[84];};',
           'inline constexpr VehicleRestParameters VehicleRestData[]={']
    for vehicle in range(36):
        bsp=(source/'bsp'/(names[vehicle]+'.bsp')).read_bytes()
        if len(bsp)!=0x25c:raise ValueError('Invalid BSP '+names[vehicle])
        body=struct.unpack_from('>I',stats,4+vehicle*0x18c)[0]
        if body>3:raise ValueError('Unknown vehicle body type')
        wheels=[]
        for i in range(4):
            off=0x1a4+i*0x2c
            pos=struct.unpack_from('>3f',bsp,off+16)
            travel=struct.unpack_from('>f',bsp,off+12)[0]
            radius=struct.unpack_from('>f',bsp,off+32)[0]
            enabled=bool(struct.unpack_from('>H',bsp,off)[0])
            wheels.append('{{'+','.join(f(v) for v in pos)+'},'+f(travel)+','+f(radius)+','+str(enabled).lower()+'}')
        isbike=vehicle>=18;stride=0xb0 if isbike else 0x150
        data=bike if isbike else kart;index=vehicle-18 if isbike else vehicle
        # Only the float prefix consumed by VehicleAnimation. Later fields
        # include integer sentinels (0xffffffff), not floating-point values.
        used=0x58 if isbike else 0xf8
        display=struct.unpack_from('>'+str(used//4)+'f',data,4+index*stride)
        lines.append('{'+str(body)+','+f(struct.unpack_from('>f',bsp)[0])+',{'+','.join(wheels)+'},{'+','.join(f(v) for v in display)+'}},')
    lines+=['};','}']
    (ROOT/'include/mkw/vehicle_rest_data.hpp').write_text('\n'.join(lines)+'\n')

if __name__=='__main__':cook_vehicle_rest()
