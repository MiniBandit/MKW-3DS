#!/usr/bin/env python3
"""Cook the retail RKRace BREFF emitters used by kart VFX into one table (REFX v3).

The runtime (src/nw4r_ef*.cpp) is a port of the NW4R EF code in Mario Kart Wii, so the
emitter resources are kept as the game reads them: each record holds the big-endian
EmitterDesc + EmitterDrawSetting and ParticleParameterDesc bytes and the particle and
emitter animation curves verbatim. Only name tables change: their pointer slots (u32
after the count) receive resolved indices, textures for texture curves and emitters
for child and postfield curves (0xffffffff when the archive lacks the name). The
particle descriptor's texture names become three u16 texture indices.

The cooker refuses NW4R features the runtime does not port (other emitter forms,
draw types, fields, postfield shapes) so a new root cannot silently render wrong.
Textures keep their GX precision: I4 -> LA4 and I8/IA8 -> LA8 with alpha equal to
the intensity as on GX, RGB565 -> RGB565, RGB5A3/RGBA8 -> RGBA8.
See docs/VFX_RESEARCH.md.
"""
import argparse, hashlib, io, json, math, struct, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'vendor/gclib'))
from gclib.texture_utils import decode_image
from gclib.gx_enums import ImageFormat, PaletteFormat
from cook_effects import entries
from PIL import Image

U16 = lambda b, p: struct.unpack_from('>H', b, p)[0]
U32 = lambda b, p: struct.unpack_from('>I', b, p)[0]
F = lambda b, p, n=1: list(struct.unpack_from('>%df' % n, b, p))
S8 = lambda b, p: struct.unpack_from('>b', b, p)[0]
# Roots used by the race presentation; children are pulled in automatically.
ROOTS = ['rk_miniTurbo', 'rk_miniTurbo1T', 'rk_wheelie', 'rk_itemBox', 'rk_itemBoxS', 'rk_jumpSeikou', 'rk_koukasen',
         'rk_driftSpark1L_Chip00', 'rk_driftSpark1R_Chip00', 'rk_driftSpark2L_Chip00', 'rk_driftSpark2R_Chip00',
         'rk_driftSpark1LB_Chip00', 'rk_driftSpark1RB_Chip00',
         'rk_kouraTailG', 'rk_kouraTailR', 'rk_kouraTailB', 'rk_killer', 'rk_killer1T',
         # Shell breaks and item blasts (RMCU 0x80689070, 0x806890e8, 0x80689180, 0x806892c0).
         'rk_kouraBrkG', 'rk_kouraBrkR', 'rk_bombHei', 'rk_bombBlue', 'rk_haneKoura',
         'rk_hitA', 'rk_hitAS',
         # Effects::Items::CreateBananaEffect (RMCU 0x806893c0).
         'rk_bananaSmk',
         # HandleFibCollision (RMCU 8056e40c): fragments on a vulnerable
         # racer, smoke on Star/Mega/Bullet. Full and remote variants.
         'rk_niseBox', 'rk_niseBoxS', 'rk_niseBoxVan', 'rk_niseBoxBiri']
# Local bike chassis sparks: USA8068efe0..8068f388, independent of wheel chips.
ROOTS += ['rk_driftSpark1'+side+tail for side in ('LB','RB','CB')
          for tail in ('_Spark00','_Spark01')]
ROOTS += ['rk_driftSpark1'+side+'1T'+tail for side in ('LB','RB','CB')
          for tail in ('_Chip00','_Spark00','_Spark01')]
ROOTS += ['rk_hangOnL','rk_hangOnR','rk_hangOnLTail','rk_hangOnRTail']
# USA8057c140 invokes KartEffect::SSMTCharged (806ae924): local kart
# sustained blue sparks and the one-shot charge trio, not drift input.
ROOTS += ['rk_driftSpark1'+side+tail for side in ('L','R')
          for tail in ('_Spark00','_Spark01','1T_Chip00','1T_Spark00','1T_Spark01')]
INK_ROOTS = ['rk_gessoA_1P', 'rk_gessoB_1P', 'rk_gessoA0_1P', 'rk_gessoB0_1P', 'rk_gessoC0_1P']
SPIN_ROOTS = ['rk_spinHalfL', 'rk_spinHalfLS', 'rk_spinL', 'rk_spinLS',
              'rk_spinHalfR', 'rk_spinHalfRS', 'rk_spinR', 'rk_spinRS']
WATER_ROOTS = ['rk_wave', 'rk_awa', 'rk_spinWaterHalf', 'rk_spinWaterHalfS',
               'rk_spinWater', 'rk_spinWaterS', 'rk_water', 'rk_pochaWaterMini',
               'rk_crash', 'rk_crashS', 'rk_crashWater', 'rk_crashWaterS',
               'rk_uchiage', 'rk_uchiageS']
# KartEffect power-ups (docs/POWERUPS_SOURCE.md), S variants for other racers.
POWERUP_ROOTS = ['rk_star', 'rk_starS', 'rk_killerS', 'rk_killer1TS', 'rk_thunder', 'rk_thunderS',
                 'rk_kaminariGumo', 'rk_kaminariGumoS', 'rk_thunderBiri', 'rk_kyodai', 'rk_kyodaiS',
                 'rk_pow', 'rk_powS', 'rk_pow1', 'rk_pow1S', 'rk_powUp']
# KartEffect slipstream (RMCU 0x8068abbc, 0x806972c0): drafting and the boost, S variants
# for other racers (docs/VFX_RESEARCH.md "Slipstream").
SLIPSTREAM_ROOTS = ['rk_slipStreamPre', 'rk_slipStreamPreS', 'rk_slipStream', 'rk_slipStreamS']
# Ordinary remote racers use the original reduced driving roots, not eleven
# copies of the local kart's full effects (USA constructor 8068d288/dbf4/f110).
OPPONENT_DRIVING_ROOTS = ['rk_miniTurboS', 'rk_driftSpark1L_Spark01S', 'rk_driftSpark1R_Spark01S',
                         'rk_driftSpark2L_Spark01S', 'rk_driftSpark2R_Spark01S',
                         'rk_driftSpark1LB_Spark01S', 'rk_driftSpark1RB_Spark01S', 'rk_driftSpark1CB_Spark01S']
# KartEffect smoke and dust (names from StaticR's KartEffect tables, RMCU 0x8038e24a..):
# start-dash rear-wheel layers (rk_start0/1/2; S for other racers), the start burnout,
# drift tyre smoke, off-road dust and landing puffs.
SMOKE_ROOTS = ['rk_wheelSpin0', 'rk_wheelSpin1', 'rk_wheelSpin2', 'rk_wheelSpinS', 'rk_start0', 'rk_start1', 'rk_start2', 'rk_startS', 'rk_startMiss', 'rk_startMissS',
               'rk_driftSmoke', 'rk_brakeSmk', 'rk_dirtSmokeA', 'rk_jumpSmoke', 'rk_jumpSmokeS', 'rk_jumpSmokeMini', 'rk_jumpSmokeMiniS',
               'rk_jumpSmokeBikeL', 'rk_jumpSmokeBikeLMini', 'rk_jumpSmokeBikeS', 'rk_jumpSmokeBikeL_S', 'rk_jumpSmokeBikeS_S']
# Per-course road materials select these instead of making them globally resident.
SURFACE_ROOTS = ['rk_weed', 'rk_mud', 'rk_snowHDirt', 'rk_snowRoad0', 'rk_snowRoad1', 'rk_flower', 'rk_stone', 'rk_stoneTile', 'rk_stoneVolc']

def surface_roots(root, course):
    data=(root/'assets/courses'/course['key']/'course.kcl').read_bytes()
    prism,octree=U32(data,8),U32(data,12)
    if prism+16>octree or octree>len(data):raise ValueError('Invalid course KCL prism range')
    variants={(U16(data,p+14)&31,(U16(data,p+14)>>5)&7) for p in range(prism+16,octree,16)}
    roots=[]
    if variants & {(2,3),(3,4),(4,4)} or (course['id']==19 and (23,2) in variants):roots.append('rk_weed')
    if variants & {(3,2),(4,2),(22,7)}:roots.append('rk_mud')
    if (4,5) in variants:roots.append('rk_snowHDirt')
    if (0,5) in variants:roots.extend(['rk_snowRoad0','rk_snowRoad1'])
    if (4,3) in variants:roots.append('rk_flower')
    if variants & {(2,6),(2,7),(3,6)}:roots.append('rk_stoneTile' if course['id']==9 else 'rk_stoneVolc' if course['id']==3 else 'rk_stone')
    return roots

VERSION = 3
NO_INDEX = 0xffffffff
# Runtime limits (src/nw4r_ef.cpp, src/race_effects.cpp).
MAX_EMITTERS, MAX_TEXTURES = 512, 160
FORMS = {0: 'disc', 1: 'line', 5: 'cube', 7: 'cylinder', 8: 'sphere', 9: 'point'}
PTCL_TYPES = {3: 'billboard', 4: 'directional', 5: 'stripe', 6: 'smooth stripe'}
FIELDS = {0: 'gravity', 1: 'speed', 3: 'newton', 6: 'spin', 7: 'random'}
TEX_FORMATS = {14: ('rgba8', 'CMPR'), 0: ('la4', 'I4'), 1: ('la8', 'I8'), 3: ('la8', 'IA8'), 4: ('rgb565', 'RGB565'),
               5: ('rgba8', 'RGB5A3'), 6: ('rgba8', 'RGBA8')}


def parse_curve(c):
    """Header fields and the five tables of a curve (also used by tools/cook_effects.py)."""
    h = dict(magic=c[0], target=c[1], kind=c[2], mask=c[3], proc=c[4], loop=c[5], frames=U16(c, 8))
    sizes = [U32(c, 12 + 4 * i) for i in range(5)]
    body = c[32:]
    h['comps'] = [i for i in range(8) if c[3] >> i & 1]
    starts = [0]
    for s in sizes:
        starts.append(starts[-1] + s)
    h['tables'] = [body[starts[i]:starts[i + 1]] for i in range(5)]
    h['starts'] = starts
    return h


def range_values(table, kind, ncomp, flags=None):
    """Expected value of each range entry (tools/cook_effects.py)."""
    if not table:
        return []
    count = U16(table, 0)
    out, p = [], 4
    for _ in range(count):
        vals = []
        for _ in range(ncomp):
            if kind == 0:
                vals.append(float(table[p]))
                p += 2
            else:
                upper, step = F(table, p, 2)
                vals.append(upper + step * 32767.5)
                p += 8
        if kind == 6:
            if flags is not None:
                flags.append(table[p])
            p += 4
        out.append(vals)
    return out


def keyed(h):
    """Keys as (frame, values), random keys at their expected value; only
    tools/cook_effects.py uses this (its smoke growth curve)."""
    kind, ncomp = h['kind'], len(h['comps'])
    size = 12 + ncomp * (1 if kind == 0 else 4)
    size += size & 1
    table = h['tables'][0]
    count = U16(table, 0)
    ranges = range_values(h['tables'][1], kind, ncomp)
    def read(t, count, p):
        out = []
        for _ in range(count):
            frame = U16(t, p)
            rnd, index = t[p + 2], t[p + 3]
            vals = [float(t[p + 12 + i]) for i in range(ncomp)] if kind == 0 else F(t, p + 12, ncomp)
            if rnd == 1 and index < len(ranges):
                vals = list(ranges[index])
            out.append((frame, vals))
            p += size
        return out
    keys = read(table, count, 4)
    # Flag 2 keys pull extra keys from the random table (one key record each).
    extra = h['tables'][2]
    if extra and any(table[4 + i * size + 2] == 2 for i in range(count)):
        keys = sorted(keys + read(extra, len(extra) // size, 0), key=lambda k: k[0])
    if any(b[0] < a[0] for a, b in zip(keys, keys[1:])):
        raise ValueError('non-monotonic keys')
    return keys


def names(table):
    if not table:
        return []
    count = U16(table, 0)
    p = 4 + count * 4
    out = []
    for _ in range(count):
        n = U16(table, p)
        out.append(table[p + 2:p + 2 + n].rstrip(b'\0').decode())
        p += 2 + n
    return out


def curve_lists(v, pp):
    """(particle curves, particle init count, emitter curves, emitter init count)."""
    track = pp + 4 + U32(v, pp)
    n, n_init = U16(v, track), U16(v, track + 2)
    ec = track + 4 + n * 8
    ne, ne_init = U16(v, ec), U16(v, ec + 2)
    off = ec + 4 + ne * 8
    sizes = [U32(v, track + 4 + n * 4 + i * 4) for i in range(n)] + [U32(v, ec + 4 + ne * 4 + i * 4) for i in range(ne)]
    out = []
    for length in sizes:
        out.append(v[off:off + length])
        off += length
    return out[:n], n_init, out[n:], ne_init


def particle_textures(v, d):
    out, o = [], d + 0x88
    for _ in range(3):
        n = U16(v, o)
        out.append(v[o + 2:o + 2 + n].rstrip(b'\0').decode())
        o += 2 + n
    return out


class Emitter:
    def __init__(self, br, idx, name):
        pos, size = idx[name]
        v = br[pos:pos + size]
        self.name, self.v = name, v
        head = U32(v, 4)
        if head != 0x14c:
            raise ValueError(f'{name}: EmitterDesc + draw setting is 0x{head:x} bytes, expected 0x14c')
        self.desc = v[8:8 + head]
        self.pp = pp = 8 + head
        self.ptcl = v[pp + 4:pp + 4 + U32(v, pp)]
        self.textures = particle_textures(v, pp + 4)
        self.ptcl_curves, self.ptcl_init, self.emit_curves, self.emit_init = curve_lists(v, pp)

    def references(self):
        """Emitter names referenced by child and postfield curves."""
        out = []
        for c in self.ptcl_curves:
            h = parse_curve(c)
            if h['kind'] in (5, 2):
                out += names(h['tables'][3])
        return out

    def curve_textures(self):
        out = []
        for c in self.ptcl_curves:
            h = parse_curve(c)
            if h['kind'] == 4:
                out += names(h['tables'][3])
        return out


def check(e, notes):
    """Refuse what the runtime does not port."""
    d, name = e.desc, e.name
    common, emit = U32(d, 0), U32(d, 4)
    form = emit & 0xff
    if form not in FORMS:
        raise ValueError(f'{name}: emitter form {form} is not ported')
    if form == 5 and emit & 0x20000:
        raise ValueError(f'{name}: uniform cube lattice is not ported')
    if d[0x84] or d[0x85] or d[0x86]:
        raise ValueError(f'{name}: LOD emission is not ported')
    ds = d[0x94:]
    if ds[0xac] not in PTCL_TYPES:
        raise ValueError(f'{name}: draw type {ds[0xac]} is not ported')
    if ds[0xac] == 6 and (ds[0xad] > 2 or ds[0xae] != 0 or ds[0xb2] & 7 != 1):
        raise ValueError(f'{name}: smooth-stripe connection/direction/tube is not ported')
    if ds[0x76] != 0:
        raise ValueError(f'{name}: particle lighting is not ported')
    # ParticleManager::Initialize (0x80033550) copies the lighting type into the colour
    # modifier; type 1 (ambient) is ported, type 2 (point light) is not.
    if ds[0x77] > 1:
        raise ValueError(f'{name}: point-light colour modifier is not ported')
    flags = U16(ds, 0)
    # InitTev (0x8003c65c) passes DrawInfo's fog (+0x70 type, +0x74.. range, +0x84 colour)
    # to GXSetFog when this flag is set, but every MKW race effect draw (0x80678aec,
    # 0x8067926c, 0x8067966c) leaves the fog type GX_FOG_NONE: the flag changes nothing.
    if flags & (1 << 12):
        notes.append(f'{name}: fog flag (DrawInfo fog type is GX_FOG_NONE in race effect draws)')
    # InitTexture maps enabled layers to texmaps in order; the combiner programs take
    # layer 1 as texmap 0.
    if flags & 0x20 and not flags & 0x10:
        raise ValueError(f'{name}: texture layer 2 without layer 1 is not ported')
    # Indirect texturing (InitTev) only matters when its matrix is non-zero and its
    # target TEV stage is active (rk_miniTurbo1T: zero matrix, stage 1 of 1).
    if flags & 0x40 and ds[7] < ds[5] and any(F(ds, 0x90, 6)):
        raise ValueError(f'{name}: indirect texture warp is not ported')
    # One PICA alpha test: GX's comp0 AND comp1 needs a side that always passes
    # (ALWAYS, LEQUAL 255, GEQUAL 0) with a reference no u8 curve animates.
    animated = set()
    for c in e.ptcl_curves:
        if c[0] in (0xab, 0xac) and c[2] == 0:
            animated |= {c[1] + i for i in range(8) if c[3] >> i & 1}

    def passes(comp, ref_at):
        return comp == 7 or (ref_at not in animated and (comp, e.ptcl[ref_at]) in ((3, 255), (6, 0)))
    if not (passes(ds[2], 0x77) or passes(ds[3], 0x78)) or (ds[4] != 0 and ds[2] != 7 and ds[3] != 7):
        raise ValueError(f'{name}: two-sided alpha compare is not ported')
    for c in e.ptcl_curves + e.emit_curves:
        h = parse_curve(c)
        tag = '%02x%02x%02x%02x' % (c[0], c[1], c[2], c[3])
        if c[0] not in (0xab, 0xac):
            continue
        kind = h['kind']
        if c in e.emit_curves:
            if kind != 11 and kind not in (3,):
                raise ValueError(f'{name}: emitter curve {tag} kind {kind}')
            if c[1] + 4 * len(h['comps']) > 0x94:
                raise ValueError(f'{name}: emitter curve {tag} target out of range')
            continue
        if kind in (0, 3):
            continue
        if kind in (4, 5, 6) and c[0] != 0xac:
            raise ValueError(f'{name}: baked {tag} has no retail executor')
        if kind == 4 and c[1] not in (0x68, 0x6c, 0x70):
            raise ValueError(f'{name}: texture curve target {c[1]:#x}')
        if kind == 5:
            table = h['tables'][0]
            for i in range(U16(table, 0)):
                flag = table[4 + i * 0x18 + 2]
                if flag and not flag & 2:
                    raise ValueError(f'{name}: random child key without a random table')
        if kind == 7:
            if c[1] not in FIELDS and not c[4] & 8:
                raise ValueError(f'{name}: field {c[1]} is not ported')
            info = h['tables'][4]
            # Infos run 8 (speed) to 0x14 bytes; the runtime copies 0x1c.
            if len(info) < 8 or info[0] > 3 or info[1] > 1:
                raise ValueError(f'{name}: field {tag} info')
            if c[1] == 3 and len(info) < 0x18:
                raise ValueError(f'{name}: short Newton field info')
        if kind == 2:
            info = h['tables'][4]
            if len(info) < 0x4c:
                raise ValueError(f'{name}: short postfield info')
            if info[0x29] != 0:
                raise ValueError(f'{name}: postfield shape {info[0x29]} is not ported (plane only)')
            if info[0x48] & 1:
                raise ValueError(f'{name}: postfield wrap box is not ported')
            notes.append(f'{name}: postfield plane option {info[0x2a]} kind {info[0x2b]} flags {U16(info, 0x2c):#x}')
        if kind not in (0, 2, 3, 4, 5, 6, 7):
            raise ValueError(f'{name}: particle curve kind {kind}')


def resolve(c, lookup):
    """The curve with its name-table slots holding resolved big-endian indices."""
    h = parse_curve(c)
    table = h['tables'][3]
    if not table:
        return bytes(c)
    out = bytearray(c)
    base = 32 + h['starts'][3]
    for i, n in enumerate(names(table)):
        struct.pack_into('>I', out, base + 4 + i * 4, lookup(n))
    return bytes(out)


def morton(x, y):
    m = 0
    for i in range(3):
        m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
    return m


def write_t3x(path, im, form, mipmaps=()):
    """A Tex3DS texture retaining supplied source mip levels: 8x8 tiles in
    Morton order, image top row first, texels encoded without rounding."""
    w, h = im.size
    if w < 8 or h < 8 or w & (w - 1) or h & (h - 1) or w > 1024 or h > 1024:
        raise ValueError(f'{path.name}: {w}x{h} is not a PICA texture size')
    code, bpp = {'rgba8': (0, 4), 'rgb565': (3, 2), 'la8': (5, 2), 'la4': (9, 1)}[form]
    def encode_level(im):
        w, h = im.size
        px = im.tobytes()
        data = bytearray(w * h * bpp)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[(y * w + x) * 4:(y * w + x) * 4 + 4]
                o = (((y // 8) * (w // 8) + x // 8) * 64 + morton(x % 8, y % 8)) * bpp
                if form == 'rgba8':
                    data[o:o + 4] = bytes((a, b, g, r))
                elif form == 'rgb565':
                    if (r & 7) != r >> 5 or (g & 3) != g >> 6 or (b & 7) != b >> 5:
                        raise ValueError(f'{path.name}: texel is not a 565 expansion')
                    struct.pack_into('<H', data, o, (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
                elif form == 'la8':
                    if not r == g == b:
                        raise ValueError(f'{path.name}: grey texture with colour')
                    data[o:o + 2] = bytes((a, r))
                else:
                    if not r == g == b or r % 17 or a % 17:
                        raise ValueError(f'{path.name}: texel is not a 4-bit expansion')
                    data[o] = (r // 17) << 4 | a // 17
        return data
    data = encode_level(im)
    for level, mip in enumerate(mipmaps, 1):
        if mip.size != (w >> level, h >> level) or min(mip.size) < 8:
            raise ValueError('Invalid PICA mip dimensions: '+str(mip.size))
        data.extend(encode_level(mip))
    log2 = lambda n: n.bit_length() - 1
    head = struct.pack('<HBBB', 1, (log2(w) - 3) | (log2(h) - 3) << 3, code, len(mipmaps))
    head += struct.pack('<HHHHHH', w, h, 0, 0x400, 0x400, 0)
    head += struct.pack('<I', len(data) << 8)
    path.write_bytes(head + bytes(data))
    return w, h


def texture(reft, tindex, name, output, namespace=''):
    pos, _ = tindex[name]
    width, height, length = struct.unpack_from('>HHI', reft, pos + 4)
    fmt, palfmt, colors, palsize = struct.unpack_from('>BBHI', reft, pos + 12)
    if fmt not in TEX_FORMATS:
        raise ValueError(f'texture {name}: GX format {fmt} is not mapped')
    im = decode_image(io.BytesIO(reft[pos + 32:pos + 32 + length]),
                      io.BytesIO(reft[pos + 32 + length:pos + 32 + length + palsize]),
                      ImageFormat(fmt), PaletteFormat(palfmt), colors, width, height).convert('RGBA')
    safe = 'fx_' + namespace + ''.join(ch if ch.isalnum() or ch == '_' else '_' for ch in name)
    # REFX stores a NUL-terminated name in 32 bytes. Use the same short name
    # for the file and table instead of letting struct.pack truncate only one.
    if len(safe.encode('utf-8')) > 31:
        safe = safe[:20] + '_' + hashlib.sha256(safe.encode('utf-8')).hexdigest()[:10]
    im.save(output / (safe + '.png'))
    # GX takes any size; PICA needs powers of two. The image keeps its top-left corner and
    # its last row and column repeat outward, so clamped sampling matches GX exactly once
    # the runtime scales texture coordinates by width/padded and height/padded.
    pw, ph = max(8, 1 << (width - 1).bit_length()), max(8, 1 << (height - 1).bit_length())
    if (pw, ph) != (width, height):
        padded = Image.new('RGBA', (pw, ph))
        padded.paste(im, (0, 0))
        for x in range(width, pw):
            padded.paste(im.crop((width - 1, 0, width, height)), (x, 0))
        for y in range(height, ph):
            padded.paste(padded.crop((0, height - 1, pw, height)), (0, y))
        im = padded
    mipmaps = []
    from gclib.texture_utils import BLOCK_WIDTHS, BLOCK_HEIGHTS, BLOCK_DATA_SIZES
    image_format = ImageFormat(fmt)
    bw, bh, bs = BLOCK_WIDTHS[image_format], BLOCK_HEIGHTS[image_format], BLOCK_DATA_SIZES[image_format]
    offset = 0
    for level in range(max(1, reft[pos + 0x14])):
        lw, lh = max(1, width >> level), max(1, height >> level)
        size = ((lw+bw-1)//bw)*((lh+bh-1)//bh)*bs
        if offset+size > length: raise ValueError('Truncated GX mip chain: '+name)
        if level and min(pw >> level, ph >> level) >= 8:
            mip = decode_image(io.BytesIO(reft[pos+32+offset:pos+32+offset+size]),
                               io.BytesIO(reft[pos+32+length:pos+32+length+palsize]),
                               image_format, PaletteFormat(palfmt), colors, lw, lh).convert('RGBA')
            mw, mh = pw >> level, ph >> level
            if (mw, mh) != mip.size:
                padded = Image.new('RGBA', (mw, mh));padded.paste(mip, (0, 0))
                for x in range(lw, mw): padded.paste(mip.crop((lw-1, 0, lw, lh)), (x, 0))
                for y in range(lh, mh): padded.paste(padded.crop((0, lh-1, mw, lh)), (0, y))
                mip = padded
            mipmaps.append(mip)
        offset += size
    write_t3x(output / (safe + '.t3x'), im, TEX_FORMATS[fmt][0], mipmaps)
    return safe, width, height, TEX_FORMATS[fmt][1], pw, ph


def exhausts(root):
    """Exhaust pipes from kart/bikePartsDispParam.bin (docs/VFX_RESEARCH.md):
    (x, y, z, pitch, yaw) twice, NaN marking an absent second pipe, then the
    flame scale. A pipe off the centre line is mirrored to both sides."""
    out = []
    for name, stride, first, scale in [('kartPartsDispParam.bin', 336, 63, 74), ('bikePartsDispParam.bin', 176, 23, 34)]:
        data = (root / 'assets/common' / name).read_bytes()
        count = U32(data, 0)
        if count != 18 or len(data) != 4 + count * stride:
            raise ValueError('Unexpected ' + name)
        for i in range(count):
            f = struct.unpack_from('>%df' % (stride // 4), data, 4 + i * stride)
            pipes = [f[first:first + 5]]
            if not math.isnan(f[first + 5]):
                pipes.append(f[first + 6:first + 11])
            points = []
            for x, y, z, pitch, yaw in pipes:
                for side in ((1, -1) if abs(x) > 1e-3 else (1,)):
                    point = (x * side, y, z, pitch, yaw * side)
                    if all(abs(point[0] - q[0]) + abs(point[1] - q[1]) + abs(point[2] - q[2]) > 1e-3 for q in points):
                        points.append(point)
            out.append((points[:4], max(f[scale:scale + 3])))
    lines = ['#pragma once', '#include <array>', '#include <cstdint>', 'namespace mkw {',
             '// Generated by tools/cook_race_effects.py from kart/bikePartsDispParam.bin.',
             '// Kart-local pipe position, pitch and yaw in degrees; Kinoko Vehicle order.',
             'struct VehicleExhaust { uint8_t count; float flameScale; float pipes[4][5]; };',
             'inline constexpr std::array<VehicleExhaust,36> VehicleExhausts{{']
    for points, flame in out:
        rows = ','.join('{' + ','.join(f'{v:.4f}f' for v in pt) + '}' for pt in points + [(0, 0, 0, 0, 0)] * (4 - len(points)))
        lines.append(f'    {{{len(points)},{flame:.4f}f,{{{rows}}}}},')
    lines += ['}};', '}', '']
    (root / 'include/mkw/vehicle_exhaust.hpp').write_text('\n'.join(lines))


def write_bank(order, records, namespaces, texture_archives, output, notes, bank_name='race-effects.bin', texture_cache=None):
    if len(order) > MAX_EMITTERS:
        raise ValueError('Too many emitters')
    for name in order:
        check(records[name], notes)
    output.mkdir(parents=True, exist_ok=True)
    textures, files = {}, []

    def texture_index(t, namespace=''):
        if not t:
            return NO_INDEX
        source, index = texture_archives[namespace]
        if t not in index:
            notes.append(f'texture {t} missing from effect texture archive')
            return NO_INDEX
        key = (namespace, t)
        if key not in textures:
            textures[key] = len(files)
            if texture_cache is not None and key in texture_cache:
                files.append(texture_cache[key])
            else:
                files.append(texture(source, index, t, output, namespace))
                if texture_cache is not None: texture_cache[key] = files[-1]
        return textures[key]
    emitter_index = {n: i for i, n in enumerate(order)}

    def emitter_lookup(n):
        if n not in emitter_index:
            notes.append(f'emitter {n} missing from effect emitter bank')
        return emitter_index.get(n, NO_INDEX)
    blob = bytearray()

    def put(data):
        while len(blob) % 4:
            blob.append(0)
        at = len(blob)
        blob.extend(data)
        return at
    table = []
    for n in order:
        e = records[n]
        namespace = namespaces[n]
        tex = [texture_index(t, namespace) for t in e.textures]
        curves = []
        # Field infos may be shorter than the 0x1c bytes the runtime copies: pad each curve.
        for c in e.ptcl_curves:
            kind = c[2]
            lookup = (lambda x: texture_index(x, namespace)) if kind == 4 else emitter_lookup if kind in (2, 5) else (lambda x: NO_INDEX)
            curves.append(put(resolve(c, lookup) + bytes(0x20)))
        for c in e.emit_curves:
            curves.append(put(bytes(c) + bytes(0x20)))
        # Non-power-of-two textures are padded by edge replication, exact only under clamp.
        wrap = U16(e.ptcl, 0x74)
        layers = [[t] if t else [] for t in e.textures]
        for c in e.ptcl_curves:
            if c[0] in (0xab, 0xac) and c[2] == 4:
                layers[(c[1] - 0x68) // 4] += names(parse_curve(c)['tables'][3])
        for layer, used in enumerate(layers):
            for t in used:
                key = (namespace, t)
                f = files[textures[key]] if key in textures else None
                if f and (f[1], f[2]) != (f[4], f[5]) and wrap >> (layer * 4) & 0xf:
                    raise ValueError(f'{n}: padded texture {t} with wrap on layer {layer}')
        desc_at, ptcl_at = put(e.desc), put(e.ptcl)
        list_at = put(struct.pack('<%dI' % len(curves), *curves)) if curves else 0
        table.append(struct.pack('<32sII3H7HI', n.encode()[:31], desc_at, ptcl_at,
                                 *[t & 0xffff for t in tex], len(e.ptcl_curves), e.ptcl_init, len(e.emit_curves),
                                 e.emit_init, len(e.ptcl), 0, 0, list_at))
    if len(files) > MAX_TEXTURES:
        raise ValueError('Too many effect textures')
    head = struct.pack('<4sIIII', b'REFX', VERSION, len(order), len(files), len(blob))
    tex_table = b''.join(struct.pack('<32sHHHH', f[0].encode(), f[1], f[2], f[4], f[5]) for f in files)
    (output / bank_name).write_bytes(head + tex_table + b''.join(table) + bytes(blob))
    return files, len(blob)


def cannon_attachment(root):
    # USA806bb778: snow is attached to the Wii cannon's taru joint at firing.
    path = root / 'build/cannon-taru.djnt'
    subprocess.run(['node', str(root / 'build/export_joints.mjs'),
                    str(root / 'assets/courses/boardcross_course/DonkyCannon_wii.brres'),
                    str(path), 'DonkyCannon_wii', 'taru'], check=True, capture_output=True)
    data = path.read_bytes()
    if struct.unpack_from('<4s3I', data)[:3] != (b'DJNT', 1, 1):
        raise ValueError('Unexpected cannon attachment format')
    offset, matrix = 48, None
    for _ in range(struct.unpack_from('<I', data, 12)[0]):
        name = data[offset:offset+32].split(b'\0')[0].decode()
        frames, loop = struct.unpack_from('<2I', data, offset+32)
        offset += 40
        if name == 'DonkyCannon_wii': matrix = struct.unpack_from('<12f', data, offset)
        offset += frames * 48
    if matrix is None or offset != len(data):
        raise ValueError('Missing original cannon firing attachment')
    (root / 'include/mkw/cannon_effect_assets.hpp').write_text(
        '#pragma once\nnamespace mkw {\ninline constexpr float CannonSnowJoint[12]={' +
        ','.join(format(x, '.9f')+'f' for x in matrix) + '};\n}\n')


def cook(root, output, course_only=False):
    breff = root / 'assets/common/Effect/RKRace.breff'
    br, reft = breff.read_bytes(), breff.with_suffix('.breft').read_bytes()
    idx, tindex = entries(br), entries(reft)
    order, notes, records = [], [], {}
    namespaces = {}
    for roots in (ROOTS, POWERUP_ROOTS, SLIPSTREAM_ROOTS, SMOKE_ROOTS, SPIN_ROOTS, WATER_ROOTS, INK_ROOTS, OPPONENT_DRIVING_ROOTS, SURFACE_ROOTS):
        if roots is SURFACE_ROOTS: always_common=set(order)
        queue = list(roots)
        while queue:
            name = queue.pop(0)
            if name in records or name not in idx:
                continue
            records[name] = Emitter(br, idx, name)
            namespaces[name] = ''
            order.append(name)
            queue.extend(records[name].references())
    # The original large drowning splash lives in the course's pocha
    # archive, not RKRace. Keep its texture names in a separate namespace.
    pocha = root / 'assets/courses/old_peach_gc/effect/pocha/rk_pocha.breff'
    pb, pt = pocha.read_bytes(), pocha.with_suffix('.breft').read_bytes()
    pi, pti = entries(pb), entries(pt)
    queue = ['rk_pochaWater', 'rk_pochaWaterS']
    while queue:
        name = queue.pop(0)
        if name in records:
            continue
        if name not in pi:
            raise ValueError('Missing original pocha water emitter: '+name)
        records[name] = Emitter(pb, pi, name)
        namespaces[name] = 'pocha_'
        order.append(name)
        queue.extend(records[name].references())
    # Penguin effects are separate course resources. Keep each texture archive
    # explicit rather than treating every non-empty namespace as pocha.
    texture_archives = {'': (reft, tindex), 'pocha_': (pt, pti)}
    creature_sources = []
    course_roots = {'pocha': ['rk_pochaWater', 'rk_pochaWaterS']}
    for course, archive, roots in [('old_sherbet_64', 'penguin_s', ['rk_pgnSpin_s', 'rk_pgnSlider_s']),
                           ('old_sherbet_64', 'penguin_m', ['rk_pgnSpin_m_l']),
                           ('old_heyho_gba', 'crab', ['rk_crabHide', 'rk_crabPocha', 'rk_crab']),
                           ('boardcross_course', 'DonkyCannon_wii', ['rk_dkCannon', 'rk_dkCannonSnw']),
                           ('koopa_course', 'koopaBall', ['rk_koopaBallS']),
                           ('desert_course', 'sanbo', ['rk_sanbo']),
                           ('rainbow_course', 'StarRing', ['rk_StarRing']),
                           ('rainbow_course', 'entry', ['rk_entry']),
                           ('treehouse_course', 'hanachan', ['rk_hanaSmk']),
                           ('treehouse_course', 'leaf_effect', ['rk_leaf_effect', 'rk_leaf_effectDash']),
                           ('treehouse_course', 'karehayama', ['rk_karehayamaS', 'rk_karehayama0S']),
                           ('treehouse_course', 'truckChimSmk', ['rk_truckChimSmkWCalc','rk_truckChimSmkW_A','rk_truckChimSmkW_B','rk_truckChimSmkW1T','rk_truckChimSmkW1TS']),
                           ('venice_battle', 'truckChimSmkW', ['rk_truckChimSmkW_A','rk_truckChimSmkW_B','rk_truckChimSmkW1T','rk_truckChimSmkW1TS']),
                           ('old_donkey_gc', 'DKrockGC', ['rk_DKrockGC']),
                           ('water_course', 'Epropeller', ['rk_epropeller', 'rk_epropellerS']),
                           ('old_koopa_gba', 'pochaYogan', ['rk_pochaYogan', 'rk_pochaYoganS']),
                           ('volcano_course', 'VolcanoBall1', ['rk_volcTail', 'rk_volcFire_L', 'rk_volcErupt', 'rk_volcBrk']),
                           ('factory_course', 'woodbox', ['rk_woodbox']),
                           ('factory_course', 'Block', ['rk_block']),
                           ('factory_course', 'Steam', ['rk_steam']),
                           ('factory_course', 'Alarm', ['rk_alarm']),
                           ('farm_course', 'choropu', ['rk_choropuMoko1', 'rk_choropuMoko0'])]:
        # These four W roots share names with the race archive, but their
        # original descriptors/curves differ. Keep the battle archive distinct.
        alias = (lambda name: 'W_' + name) if archive == 'truckChimSmkW' else (lambda name: name)
        course_roots[archive] = [alias(name) for name in roots]
        effect_name = {'DonkyCannon_wii':'dkCannon', 'hanachan':'hana', 'Steam':'steam', 'Alarm':'alarm', 'choropu':'choropuMoko', 'Epropeller':'epropeller', 'VolcanoBall1':'volc', 'Block':'block'}.get(archive, archive)
        path = root / f'assets/courses/{course}/effect/{archive}/rk_{effect_name}.breff'
        source, pixels = path.read_bytes(), path.with_suffix('.breft').read_bytes()
        index, pixel_index = entries(source), entries(pixels)
        namespace = archive + '_'
        texture_archives[namespace] = (pixels, pixel_index)
        creature_sources.append({'path': path.relative_to(root).as_posix(),
                                 'sha256': hashlib.sha256(source).hexdigest(),
                                 'breft_sha256': hashlib.sha256(pixels).hexdigest()})
        queue = list(roots)
        while queue:
            name = queue.pop(0)
            key = alias(name)
            if key in records: continue
            if name not in index: raise ValueError('Missing original creature emitter: ' + name)
            emitter = Emitter(source, index, name)
            # W's original four records have no child/postfield references. Refuse
            # future dependencies here rather than silently resolving race names.
            if key != name and emitter.references():
                raise ValueError('Namespaced chimney has unexpected dependencies: ' + name)
            records[key] = emitter
            namespaces[key] = namespace
            order.append(key)
            queue.extend(emitter.references())
    texture_cache = {}
    # Course-only reentry must not expand the legacy aggregate resident bank.
    # The race actually loads its course bank; package_sd reads every bank's
    # texture dependencies, including these entry textures.
    aggregate = [name for name in order if namespaces[name] != 'entry_']
    files, blob_size = ([],0) if course_only else write_bank(aggregate, records, namespaces, texture_archives, output, notes, texture_cache=texture_cache)
    from course_catalog import courses
    profiles = []
    header = ['#pragma once', 'namespace mkw {', 'template<class Effects> bool courseEffectRootsPresent(const Effects& effects,unsigned course){', 'switch(course){']
    common = [name for name in order if name in always_common]
    for course in courses():
        folder = root / 'assets/courses' / course['key'] / 'effect'
        roots = [name for archive, names_ in course_roots.items() if (folder/archive).is_dir() for name in names_]
        roots += surface_roots(root,course)
        selected = set(common); queue = list(roots)
        while queue:
            name = queue.pop()
            if name in selected: continue
            if name not in records: raise ValueError('Missing course effect dependency: '+name)
            selected.add(name); queue.extend(records[name].references())
        names_ = [name for name in order if name in selected]
        bank = f"race-effects-{course['id']:02d}.bin"
        textures_, size_ = write_bank(names_, records, namespaces, texture_archives, output, [], bank, texture_cache)
        if len(textures_) > 128: raise ValueError('Course effects exceed texture budget: '+course['key'])
        profiles.append({'id':course['id'],'course':course['key'],'bank':bank,'roots':roots,'emitters':len(names_),'textures':len(textures_),'payload_bytes':size_})
        checks = '&&'.join('effects.find("'+name+'")>=0' for name in roots) or 'true'
        header.append(f"case {course['id']}: return {checks};")
    header += ['default:return false;', '}', '}', '}']
    (root/'include/mkw/course_effect_roots.hpp').write_text('\n'.join(header)+'\n')
    (output/'course-effects.json').write_text(json.dumps(profiles,indent=2)+'\n')
    (output / 'race-effects.json').write_text(json.dumps({
        'source_sha256': hashlib.sha256(br).hexdigest(), 'emitters': aggregate, 'all_course_emitters': order,
        'pocha_source_sha256': hashlib.sha256(pb).hexdigest(),
        'creature_sources': creature_sources,
        'textures': [[f[0], f[1], f[2], f[3], f[4], f[5]] for f in files], 'notes': notes}, indent=1) + '\n')
    exhausts(root)
    cannon_attachment(root)
    print(f'Cooked {len(order)} emitters, {len(files)} textures, {blob_size} bytes of descriptors and curves')
    for n in notes:
        print('  note:', n)
    return len(order)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--output', type=Path)
    p.add_argument('--course-only', action='store_true', help='Build only the bounded banks loaded by races, without a globally resident aggregate')
    a = p.parse_args()
    # Build-local output; tools/package_sd.py overlays it onto the SD assets.
    cook(a.root, a.output or a.root / 'build/vfx/textures', a.course_only)
