#!/usr/bin/env python3
"""Retail course lighting (EGG posteffect) for every course archive.

Reads posteffect/posteffect.blight (LGHT), posteffect.blmap (LMAP) and
posteffect.bfg from each course .szs, falling back to Common.szs
lightset/default.blight/.blmap like StaticR 0x8055D608. Field meanings follow
the USA main.dol/StaticR decompilation recorded in docs/COURSE_LIGHTING.md:

  LGHT v2   0x10 u16 lights, 0x12 u16 ambients, 0x14 RGBA LightManager colour
            (light-texture clear colour), 0x28 LOBJ[lights] then RGBA8+pad
            ambients                       (EGG::LightManager 0x8022A56C)
  LOBJ v1   0x10 spot fn, 0x11 dist fn, 0x12 coordinate space, 0x13 GX light
            type, 0x14 u16 ambient, 0x16 u16 flags, 0x18 pos, 0x24 aim,
            0x30 f32 intensity, 0x34 RGBA colour, 0x38 RGBA dark colour,
            0x3c cutoff, 0x40 ref distance, 0x44 ref brightness
                                           (EGG::LightObject 0x8022BF30)
  LTEX v0   0x10 u16 entries, 0x14 name, 0x34 type, 0x38 light mask (low 16
            bits), 0x3c {f32 intensity, u8 gradient, pad[3]}[entries]
                                           (EGG::LightTexture 0x8022E150)
  BFG       0x1c-byte fog states {u32 GXFogType, f32 start, f32 end, RGBA,
            u16 enabled, pad, f32 fade per frame, u8, u8, pad}
                                           (StaticR fog 0x805A30C8/0x805A31D0)

Writes include/mkw/course_lighting.hpp and build/course-lighting.json.
"""
import io, json, math, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'vendor/gclib'))
from gclib.yaz0_yay0 import Yaz0
LIGHTS = 16  # StaticR 0x8055CCA4: LightManager(16 lights, 16 ambients)
FOG_STATE = 0x1c


def archive(path):
    b = path.read_bytes()
    if b[:4] == b'Yaz0':
        b = Yaz0.decompress(io.BytesIO(b)).getvalue()
    if b[:4] != b'U\xaa8-':
        raise ValueError('not a U8 archive: ' + str(path))
    root = struct.unpack_from('>I', b, 4)[0]
    count = struct.unpack_from('>I', b, root + 8)[0]
    strings = root + count * 12
    out, stack = {}, [('', count)]
    for i in range(1, count):
        while i >= stack[-1][1]:
            stack.pop()
        info, offset, size = struct.unpack_from('>III', b, root + i * 12)
        start = strings + (info & 0xffffff)
        name = stack[-1][0] + b[start:b.index(0, start)].decode()
        if info >> 24:
            stack.append((name + '/', size))
        else:
            out[name] = b[offset:offset + size]
    return out


def clamp_byte(value):
    return max(0, min(255, int(value)))


def parse_blight(data):
    if data[:4] != b'LGHT' or data[8] != 2:
        raise ValueError('expected LGHT version 2')
    count, ambients = struct.unpack_from('>HH', data, 0x10)
    clear = data[0x14:0x18]
    lights, at = [], 0x28
    for index in range(count):
        if data[at:at + 4] != b'LOBJ':
            raise ValueError('expected LOBJ')
        size, version = struct.unpack_from('>I', data, at + 4)[0], data[at + 8]
        d = at
        space, light_type = data[d + 0x12], data[d + 0x13]
        ambient, flags = struct.unpack_from('>HH', data, d + 0x14)
        if version == 0:   # 0x8022BF30: version 0 is always directional, world space
            light_type, space, flags = 1, data[d + 0x12], flags | 0x660
        elif version == 1:
            flags &= 0xfffd
        # StaticR 0x8055CD68 marks lights 0-7 as G3D lights (0x40) and 8-15 as
        # light-texture lights (0x20) after building the light sets.
        flags |= 0x40 if index < 8 else 0x20
        pos = struct.unpack_from('>3f', data, d + 0x18)
        aim = struct.unpack_from('>3f', data, d + 0x24)
        intensity = struct.unpack_from('>f', data, d + 0x30)[0]
        color, dark = data[d + 0x34:d + 0x38], data[d + 0x38:d + 0x3c]
        direction = [a - p for a, p in zip(aim, pos)]
        length = math.sqrt(sum(v * v for v in direction))
        direction = [v / length for v in direction] if length > 0 else [0., 0., 0.]
        # LightObject::Calc (0x8022B54C): the GX colour is colour*intensity,
        # each channel saturated at 255; alpha is the authored alpha.
        final = [clamp_byte(min(255., c * intensity)) for c in color[:3]] + [color[3]]
        # LightTexture::addLight (0x8022DC24): konst = dark colour * intensity, byte-truncated.
        dark = [int(c * intensity) & 255 for c in dark[:3]] + [dark[3]]
        lights.append({'index': index, 'flags': flags, 'space': space, 'type': light_type, 'ambient': ambient,
                       'direction': direction, 'distance': length, 'color': final, 'dark': dark,
                       'intensity': intensity, 'spot': data[d + 0x10], 'distanceFunction': data[d + 0x11]})
        at += size
    table = [list(data[at + 8 * i:at + 8 * i + 4]) for i in range(ambients)]
    return {'lights': lights, 'ambients': table, 'clear': list(clear)}


def parse_blmap(data):
    if data[:4] != b'LMAP':
        raise ValueError('expected LMAP')
    textures, at = [], 0x20
    while at + 0x10 <= len(data) and data[at:at + 4] == b'LTEX':
        size, version = struct.unpack_from('>I', data, at + 4)[0], data[at + 8]
        if version != 0:
            raise ValueError('only LTEX version 0 occurs in retail courses')
        count = struct.unpack_from('>H', data, at + 0x10)[0]
        name = data[at + 0x14:at + 0x34].split(b'\0')[0].decode()
        texture_type = data[at + 0x34]
        mask = struct.unpack_from('>I', data, at + 0x38)[0] & 0xffff
        entries = []
        for light in range(min(count, LIGHTS)):
            intensity = struct.unpack_from('>f', data, at + 0x3c + 8 * light)[0]
            gradient = data[at + 0x40 + 8 * light]
            if mask >> light & 1:
                entries.append({'light': light, 'intensity': intensity, 'gradient': gradient})
        textures.append({'name': name, 'type': texture_type, 'entries': entries})
        at += size
    return {'textures': textures}


def parse_bfg(data):
    states = []
    for at in range(0, len(data) - FOG_STATE + 1, FOG_STATE):
        fog_type, start, end = struct.unpack_from('>Iff', data, at)
        enabled = struct.unpack_from('>H', data, at + 0x10)[0]
        fade = struct.unpack_from('>f', data, at + 0x14)[0]
        states.append({'type': fog_type, 'start': start, 'end': end, 'color': list(data[at + 0xc:at + 0x10]),
                       'enabled': bool(enabled), 'fade': fade})
    return states


def course_lighting(source, default_light, default_map):
    files = archive(source)
    light = files.get('posteffect/posteffect.blight') or files.get('./posteffect/posteffect.blight')
    lmap = files.get('posteffect/posteffect.blmap') or files.get('./posteffect/posteffect.blmap')
    fog = files.get('posteffect/posteffect.bfg') or files.get('./posteffect/posteffect.bfg')
    result = parse_blight(light or default_light)
    result['textures'] = parse_blmap(lmap or default_map)['textures']
    # StaticR 0x805A30C8: without a BFG the single fog object is PERSP_EXP
    # 1000..100000, white, and disabled.
    result['fog'] = parse_bfg(fog) if fog else [{'type': 4, 'start': 1000., 'end': 100000., 'color': [255] * 4, 'enabled': False, 'fade': 0.}]
    result['sources'] = {'blight': 'course' if light else 'Common lightset/default.blight',
                         'blmap': 'course' if lmap else 'Common lightset/default.blmap', 'bfg': 'course' if fog else None}
    return result


def rgba(c):
    return '0x%02x%02x%02x%02xu' % tuple(c)


def header(rows):
    h = ['#pragma once', '#include <mkw/retail_lighting.hpp>', 'namespace mkw {',
         '// Generated by tools/cook_course_lighting.py from posteffect .blight/.blmap/.bfg',
         '// (Common lightset/default.* when a course has none).',
         f'inline constexpr CourseLighting CourseLightings[{len(rows)}]={{']
    for name, r in rows:
        lights = ','.join('{%d,%d,%d,{%.6ff,%.6ff,%.6ff},%s,%s}' % (l['flags'], l['space'], l['type'], *l['direction'], rgba(l['color']), rgba(l['dark']))
                          for l in r['lights'][:LIGHTS])
        ambients = ','.join(rgba(a) for a in r['ambients'][:LIGHTS])
        textures = []
        for t in r['textures'][:2]:
            entries = ','.join('{%d,%d,%.6ff}' % (e['light'], e['gradient'], e['intensity']) for e in t['entries'][:RETAIL_TEXTURE_LIGHTS])
            textures.append('{%d,%d,{%s}}' % (t['type'], min(len(t['entries']), RETAIL_TEXTURE_LIGHTS), entries))
        fogs = ','.join('{%d,%d,%.3ff,%.3ff,%s,%.6ff}' % (f['type'], int(f['enabled']), f['start'], f['end'], rgba(f['color']), f['fade'])
                        for f in r['fog'][:4])
        h.append('    {"%s",{%s},{%s},%s,{%s},%d,{%s}},' % (name, lights, ambients, rgba(r['clear']), ','.join(textures), min(4, len(r['fog'])), fogs))
    h += ['};', 'inline const CourseLighting* courseLighting(const char* archive){',
          '    for(const auto& c:CourseLightings)if(!std::strcmp(c.archive,archive))return &c;', '    return nullptr;', '}', '}', '']
    return '\n'.join(h)


RETAIL_TEXTURE_LIGHTS = 4


def main():
    common = archive(ROOT / 'data/extracted/Race/Common.szs')
    default_light = common.get('lightset/default.blight') or common['./lightset/default.blight']
    default_map = common.get('lightset/default.blmap') or common['./lightset/default.blmap']
    rows = []
    for src in sorted((ROOT / 'data/extracted/Race/Course').glob('*.szs')):
        if src.name.startswith('._') or src.stem.endswith(('_d', '_Dif')):
            continue
        rows.append((f'Race/Course/{src.name}', course_lighting(src, default_light, default_map)))
    if len(rows) != 42:
        raise ValueError(f'expected 42 course archives, found {len(rows)}')
    for name, r in rows:
        if len(r['lights']) < LIGHTS or len(r['ambients']) < LIGHTS:
            raise ValueError('retail courses carry 16 lights and 16 ambients: ' + name)
        if any(l['type'] != 1 for l in r['lights'] if l['flags'] & 1):
            raise ValueError('only directional lights are ported: ' + name)
        if len(r['textures']) != 2 or [t['name'] for t in r['textures']] != ['lm_0', 'lm_1']:
            raise ValueError('expected lm_0 and lm_1: ' + name)
        if any(t['type'] != 0 or len(t['entries']) > RETAIL_TEXTURE_LIGHTS for t in r['textures']):
            raise ValueError('unsupported light texture: ' + name)
    (ROOT / 'include/mkw/course_lighting.hpp').write_text(header(rows))
    (ROOT / 'build').mkdir(exist_ok=True)
    (ROOT / 'build/course-lighting.json').write_text(json.dumps({name: r for name, r in rows}, indent=1) + '\n')
    print(len(rows), 'courses')


if __name__ == '__main__':
    main()
