#!/usr/bin/env python3
"""Cook the user's BREFF drift definitions and lossless REFT art.

Layout reference: BrawlCrate's REFF.cs; emission / dimensions: NW4R EF
(doldecomp/ogws 27fa94a593096eed7442efc4e70981fd52025b9c, CC0).
The audit retains source curves and names, including unsupported interpolation.
"""
import argparse, struct, io, sys, subprocess, json, hashlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'vendor/gclib'))
from gclib.texture_utils import decode_image
from gclib.gx_enums import ImageFormat, PaletteFormat
from devkitpro import tool
U16 = lambda b, p: struct.unpack_from('>H', b, p)[0]
U32 = lambda b, p: struct.unpack_from('>I', b, p)[0]
F = lambda b, p, n=1: list(struct.unpack_from('>' + str(n) + 'f', b, p))


def entries(b):
    if b[:4] not in [b'REFT', b'REFF'] or b[4:6] != b'\xfe\xff':
        raise ValueError('Expected big-endian REF archive')
    table = 0x18 + U32(b, 0x18)
    p = table + 8
    result = {}
    for _ in range(U16(b, table + 4)):
        n = U16(b, p)
        name = b[p + 2:p + 1 + n].decode('ascii')
        off, size = struct.unpack_from('>II', b, p + 2 + n)
        p += 10 + n
        if table + off + size > len(b):
            raise ValueError('REF entry exceeds archive')
        result[name] = (table + off, size)
    return result


def effect(br, idx, name):
    pos, size = idx[name]
    v = br[pos:pos + size]
    pp = 8 + U32(v, 4)
    d = pp + 4
    track = pp + 4 + U32(v, pp)
    n = U16(v, track)
    ec = track + 4 + n * 8
    off = ec + 4 + U16(v, ec) * 8
    curves = []
    for i in range(n):
        length = U32(v, track + 4 + n * 4 + i * 4)
        curves.append(v[off:off + length])
        off += length
    scale = [F(v, d + 24, 2) for _ in range(32)]
    alpha = [v[d + 3] / 255] * 32
    size_xy = F(v, d + 16, 2)
    size_delta = [0., 0.]
    colour_range = [[0., 0., 0.], [0., 0., 0.]]
    primary, secondary = list(v[d:d + 4]), list(v[d + 4:d + 8])
    rotate = [0., 0.]
    drag, alpha_random = 1., 0.
    notes = []
    for c in curves:
        components = [i for i in range(8) if c[3] & (1 << i)]
        if c[0] == 0xAB:
            frames = U16(c, 8)
            if c[1] == 24 and c[2] == 3:
                for age in range(32):
                    values = F(c, 32 + min(age, frames - 1) * len(components) * 4, len(components))
                    for k, value in zip(components, values):
                        scale[age][k] = value
            elif c[1] == 3 and c[2] == 0:
                alpha = [c[32 + min(age, frames - 1)] / 255 for age in range(32)]
        if c[0] != 0xAC:
            continue
        # Float ranges store upper endpoint + an increment for each u16 RNG value,
        # NOT a centre plus a float deviation. For core size: 62.5 -> 37.5.
        r = 32 + U32(c, 12) + 4
        if c[1] == 16 and c[2] == 3 and U32(c, 16) >= 20:
            size_xy = [F(c, r)[0], F(c, r + 8)[0]]
            size_delta = [F(c, r + 4)[0] * 65535, F(c, r + 12)[0] * 65535]
        elif c[1] in (0, 4) and c[2] == 0 and U32(c, 16) > 4:
            slot = 0 if c[1] == 0 else 1
            target = primary if slot == 0 else secondary
            for j, component in enumerate(components):
                if component < 3:
                    target[component] = c[r + j * 2]
                    colour_range[slot][component] = c[r + j * 2 + 1] / 100
        elif c[1] == 3 and c[2] == 0 and U16(c, 32) == 2 and not U32(c, 16):
            # Orange's linear 255 -> 10 fade is keyed, blue's equivalent is baked.
            keys = [(U16(c, 36 + i * 14), c[48 + i * 14]) for i in range(2)]
            assert keys[0][0] == 0 and keys[1][0] > 0
            alpha = [(keys[0][1] + (keys[1][1] - keys[0][1]) * min(age / keys[1][0], 1)) / 255 for age in range(32)]
        elif c[1] == 32 and c[2] == 6 and U32(c, 16) >= 32:
            # Rotation ranges are radians; the smoke track accumulates Z rotation.
            rotate = [F(c, r + 16)[0], F(c, r + 20)[0] * 65535]
        elif c[1] == 1 and c[2] == 7 and U32(c, 28) == 8:
            drag = F(c, 36)[0]
        elif c[1] == 3 and c[2] == 0 and name == 'rk_driftSmoke':
            # Smoke holds its random initial opacity to age 2, then fades to 0 at 6.
            assert U16(c, 32) == 3
            alpha_random = c[r + 1] / 100
            start = c[r] / 255
            alpha = [start * max(0., min(1., (6 - age) / 4)) for age in range(32)]
        elif c[1] == 24 and c[2] == 3:
            # Keyed smoke growth: 0.5 at frame 0 to a random [1.875, 3.125]
            # (expected 2.5) at frame 6, interpolated linearly per frame.
            from cook_race_effects import parse_curve, keyed
            keys = keyed(parse_curve(c))
            for age in range(32):
                if age <= keys[0][0]:
                    values = keys[0][1]
                else:
                    values = keys[-1][1]
                    for (f0, a0), (f1, a1) in zip(keys, keys[1:]):
                        if age <= f1:
                            t = (age - f0) / max(f1 - f0, 1)
                            values = [x + (y - x) * t for x, y in zip(a0, a1)]
                            break
                for k, value in zip(components, values):
                    scale[age][k] = value
    return {'name': name, 'size': size_xy, 'sizeDelta': size_delta, 'life': U16(v, 18),
            'lifeRandom': v[20] / 100, 'inherit': v[34] / 100,
            'primary': primary, 'secondary': secondary, 'colourRange': colour_range,
            'scale': scale, 'alpha': alpha, 'alphaRandom': alpha_random,
            'rate': F(v, 24)[0], 'rateRandom': v[23] / 100,
            'translate': F(v, 128, 3), 'velocity': F(v, 84)[0],
            'volume': F(v, 36, 5), 'specDirection': F(v, 92, 3),
            'rotation': F(v, 116, 3), 'diffusion': F(v, 88)[0],
            'radiation': F(v, 64)[0], 'randomSpeed': F(v, 72)[0],
            'velocityRandom': v[62] / 100, 'formFlags': U32(v, 12),
            'shape': list(v[pp - 12:pp - 4]), 'drawFlags': U16(v, 156),
            'pivot': [x / 100 for x in struct.unpack_from('>bb', v, pp - 15)],
            'rotate': rotate, 'drag': drag, 'notes': notes,
            'curves': [c.hex() for c in curves]}


def shape(s, rotation=None):
    return struct.pack('<16fI', *s['volume'], *s['specDirection'], *(rotation or s['rotation']),
                       s['velocity'], s['diffusion'], s['radiation'], s['randomSpeed'],
                       s['velocityRandom'], s['formFlags'])


def record(s, parents, layer, texture):
    raw = struct.pack('<4f3I8B6f2f64f32f', *s['size'], s['inherit'], parents[0]['rate'],
                      s['life'], texture, layer, *s['primary'], *s['secondary'],
                      *parents[0]['translate'], *parents[1]['translate'],
                      parents[0]['velocity'], parents[1]['velocity'],
                      *[x for pair in s['scale'] for x in pair], *s['alpha'])
    raw += b''.join(shape(s, parent['rotation']) if layer == 0 else shape(parent) for parent in parents)
    raw += struct.pack('<16fI4B', *s['sizeDelta'], *sum(s['colourRange'], []), *s['pivot'],
                       *s['rotate'], s['drag'], s['lifeRandom'], parents[0]['rateRandom'],
                       s['alphaRandom'], s['drawFlags'], s['shape'][0], s['shape'][1], s['shape'][2], s['shape'][6])
    assert len(raw) == 660
    return raw


def cook(archive, output, bike=False):
    b = archive.read_bytes()
    index = entries(b)
    output.mkdir(parents=True, exist_ok=True)
    for name in ['sparkTex2', 'flash2', 'firechip2', 'rk_smoke2']:
        pos, size = index[name]
        width, height, length = struct.unpack_from('>HHI', b, pos + 4)
        fmt, palfmt, colors, palsize = struct.unpack_from('>BBHI', b, pos + 12)
        if pos + 32 + length + palsize > len(b):
            raise ValueError('Invalid texture bounds')
        im = decode_image(io.BytesIO(b[pos + 32:pos + 32 + length]),
                          io.BytesIO(b[pos + 32 + length:pos + 32 + length + palsize]),
                          ImageFormat(fmt), PaletteFormat(palfmt), colors, width, height).convert('RGBA')
        png = output / ('drift_' + name + '.png')
        im.save(png)
        # Original colour and 8-bit alpha matter for these small bright textures.
        subprocess.run([str(tool('tex3ds')), '-f', 'rgba8', '-z', 'none',
                        '-o', str(png.with_suffix('.t3x')), str(png)], check=True, stdout=subprocess.DEVNULL)
    br = archive.with_suffix('.breff').read_bytes()
    idx = entries(br)
    records, audit = [], []
    for colour, level in [('blue', 1), ('orange', 2)]:
        for k in range(3):
            spec = effect(br, idx, f'rk_{colour}Spark{k:02d}')
            parents = [effect(br, idx, f'rk_driftSpark{level}{side}_Spark{0 if k == 0 else 1:02d}')
                       for side in (['CB', 'CB'] if bike and level == 1 else ['L', 'R'])]
            if not 0 < spec['life'] <= 32:
                raise ValueError('Particle lifetime exceeds native bound')
            # Both flash child keys occur at parent age 0. They share one emission
            # cadence and one inherited velocity; neither rate is divided in half.
            records.append(record(spec, parents, k, 0 if k == 0 else 1))
            audit.append({'child': spec, 'parents': parents})
    smoke = effect(br, idx, 'rk_driftSmoke')
    records.append(record(smoke, [smoke, smoke], 3, 2))
    audit.append({'child': smoke})
    stem = 'drift-effects-bike' if bike else 'drift-effects'
    (output / (stem + '.bin')).write_bytes(struct.pack('<4sII', b'DEFX', 3, len(records)) + b''.join(records))
    (output / (stem + '.json')).write_text(json.dumps({
        'source_sha256': hashlib.sha256(br).hexdigest(), 'format_version': 3, 'record_size': 660,
        'definitions': audit, 'limitations': [
            'Directional cross-plane basis is reconstructed; original game reference captures are still needed for visual parity.',
            'The full BREFF child/field interpreter is not implemented; selected drift children and speed fields are specialized.',
            'Smoke growth uses the expected value of its random scale key.',
            'Emitter RNG uses the source generator but does not share the original game effect-system seed sequence.'
        ]}, indent=2) + '\n')
    print(f'Cooked {stem}: source extents, pivots, ranges, fade, cadence, drag and lossless particle textures.')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('archive', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--bike', action='store_true')
    a = p.parse_args()
    cook(a.archive, a.output, a.bike)
