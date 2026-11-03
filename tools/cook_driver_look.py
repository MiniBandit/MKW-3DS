#!/usr/bin/env python3
"""Cook the original per-vehicle/character look parameters, without changing assets."""
from pathlib import Path
import hashlib
import json
import math
import struct

ROOT = Path(__file__).resolve().parents[1]
def f32(x): return struct.unpack('>f', struct.pack('>f', x))[0]
def real(data, at): return struct.unpack_from('>f', data, at)[0]
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def cook():
    common = ROOT/'assets/common/DriverIKParam.bin'
    data = common.read_bytes()
    assert struct.unpack_from('>III', data) == (33, 36, 42)
    assert len(data) == 0x10c + 36*42*33
    garage = ROOT/'build/roster/models/garage.json'
    choices = json.loads(garage.read_text())['choices']
    display_path = ROOT/'assets/common/kartDriverDispParam.bin'
    display = display_path.read_bytes()
    assert struct.unpack_from('>II', display) == (36, 48)
    parameters, entries, evidence = [], [], []
    offsets = (0x64, 0x68, 0x6c, 0x90, 0x70, 0x74, 0x78, 0x7c)
    for choice in choices:
        character, vehicle = choice['character'], choice['vehicle']
        entry = data[0x10c+(vehicle*42+character)*33:0x10c+(vehicle*42+character+1)*33]
        fallback = None
        if entry[0]:
            values = []
            for index in [*range(12, 20),6,7,8,3,4,5,0,1,2,9]:
                high, low = real(data, 0xc+index*4), real(data, 0x8c+index*4)
                ratio = f32(entry[index+1]/255)
                value = high if high == low else f32(low+f32(ratio*f32(high-low)))
                values.append(f32(math.trunc(f32(.5+f32(100*value)))/100))
            values += list(map(f32,(.2,.02,.8,.9)))
        else:
            stem = choice['prefix'].rstrip('_')
            candidates = [ROOT/d/stem/'IKParam.ikp' for d in ('assets/vehicles', 'build/roster-work/vehicles')]
            fallback = next((p for p in candidates if p.is_file()), None)
            if fallback is None: raise ValueError('Missing original vehicle IKParam: '+stem)
            raw = fallback.read_bytes()
            values = [real(raw, at) for at in (*offsets,0x18,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34,0x38,0x3c,0x40,0x44,0x50,0x54)]
        if not all(math.isfinite(v) for v in values): raise ValueError('Nonfinite IK parameters')
        if values not in parameters: parameters.append(values)
        # USA807ba210..254: original Driver+144, not a local-player flag.
        vector = [real(display, 8+(vehicle*48+character)*56+0x14+i*4) for i in range(3)]
        magnitude = f32(f32(f32(vector[0]*vector[0])+f32(vector[1]*vector[1]))+f32(vector[2]*vector[2]))
        enabled = magnitude > 2**-23
        entries.append((character, vehicle, parameters.index(values), enabled))
        evidence.append({'character': character, 'vehicle': vehicle, 'values': values,
                         'vehicleResponseEnabled': enabled, 'responseEnableVector': vector,
                         'fallback': str(fallback.relative_to(ROOT)) if fallback else None,
                         'fallbackSha256': digest(fallback) if fallback else None})
    entries.sort()
    if len({e[:2] for e in entries}) != len(entries): raise ValueError('Duplicate garage pair')
    def literal(v):
        text = format(v, '.9g')
        return text + ('.0f' if '.' not in text and 'e' not in text else 'f')
    out = ['#pragma once', '#include <mkw/driver_look.hpp>', 'namespace mkw {',
           '// Original DriverIKParam.bin, with the selected kart archive IKParam.ikp fallback.',
           'inline bool driverLookParameters(unsigned character,unsigned vehicle,DriverLookParameters& result){',
           '    static constexpr float values[][22]={']
    out += ['        {'+','.join(literal(v) for v in row)+'},' for row in parameters]
    out += ['    };', '    struct Entry{unsigned char character,vehicle;unsigned short row;bool responseEnabled;};',
            '    static constexpr Entry entries[]={']
    out += [f'        {{{c},{v},{r},{str(enabled).lower()}}},' for c,v,r,enabled in entries]
    out += ['    };', '    for(const auto& e:entries)if(e.character==character&&e.vehicle==vehicle){',
            '        const auto& p=values[e.row];',
            '        result.vehicleResponseEnabled=e.responseEnabled;',
            '        result.pitchNegative=p[0];result.pitchPositive=p[1];result.yawBody=p[2];result.yawHead=p[3];',
            '        result.headYawScale=p[4];result.headPitchScale=p[5];result.spineYawScale=p[6];result.rootYawScale=p[7];',
            '        for(unsigned i=0;i<14;++i)result.vehicleResponse[i]=p[8+i];',
            '        return true;', '    }', '    return false;', '}', '}']
    header = ROOT/'include/mkw/driver_look_data.hpp'
    header.write_text('\n'.join(out)+'\n')
    receipt = {'sourceSha256': digest(common), 'displaySha256': digest(display_path), 'garageSha256': digest(garage),
               'headerSha256': digest(header), 'entries': evidence}
    (ROOT/'build/driver-look.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(f'Cooked {len(entries)} original combinations, {len(parameters)} unique parameter rows')

if __name__ == '__main__': cook()
