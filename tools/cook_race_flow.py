#!/usr/bin/env python3
"""Cook the retail race-flow UI (out-of-bounds wipe, result board, end-of-race
menu) from the locally extracted Race/Race_U/Font archives.

Textures go to build/race-flow/textures (an overlay that tools/package_sd.py
adds to the package); the layout geometry and animation keys go to
include/mkw/race_flow_layout.hpp. Nothing in the shared sd-card is written.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from cook_race_hud import sections, string, sample
from cook_menu_animations import animation as brlan
from cook_race_items import decode_tpl, cook_image_sprite
from devkitpro import tool

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'assets/race-ui'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def f(value):
    return format(float(value), '.9g') + ('.f' if float(value).is_integer() else 'f')


def material_texture_srt(layout, material):
    """Texture SRT (tx, ty, rotate, sx, sy) and sampler of one BRLYT material."""
    d = layout.read_bytes()
    for tag, block, s in sections(d):
        if tag != b'mat1':
            continue
        for i in range(struct.unpack_from('>H', d, s)[0]):
            o = block + struct.unpack_from('>I', d, s + 4 + i * 4)[0]
            if string(d, o) != material:
                continue
            flags = struct.unpack_from('>I', d, o + 60)[0]
            sampler = struct.unpack_from('>HH', d, o + 64)
            off = o + 64 + (flags & 15) * 4
            srt = list(struct.unpack_from('>5f', d, off)) if (flags >> 4) & 15 else [0, 0, 0, 1, 1]
            return srt, sampler
    raise ValueError(f'Missing material {material} in {layout}')


def cook_wipe(out, manifest, header):
    """game_image/wipe.brlyt 'Normal': a black pane whose clamped texture T
    translation follows wipe_Normal.brlan (Kart::Link::StartOobWipe)."""
    ui = UI / 'Race/game_image'
    layout, anim, tpl = ui / 'blyt/wipe.brlyt', ui / 'anim/wipe_Normal.brlan', ui / 'timg/ij_wipe_a.tpl'
    srt, sampler = material_texture_srt(layout, 'Normal')
    if sampler[1] & 0x0303:
        raise ValueError('Wipe texture is expected to clamp in both directions')
    duration, _, tracks = brlan(anim)
    keys = next(t['keys'] for t in tracks if t['element'] == 'Normal' and t['kind'] == 'RLTS' and t['target'] == 1)
    translate = [sample(keys, frame) for frame in range(duration)]
    record = cook_image_sprite(decode_tpl(tpl), 'flow_wipe', out)
    manifest['wipe'] = {'texture': record, 'frames': duration, 'scaleT': srt[4], 'translateT': translate,
                        'sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in (layout, anim, tpl)}}
    header += [
        '// game_image/wipe.brlyt pane "Normal" (832x456, colour 0 black) with',
        '// ij_wipe_a clamped; wipe_Normal.brlan translates the texture T axis.',
        f'inline constexpr float FlowWipeScaleT={f(srt[4])};',
        f'inline constexpr float FlowWipeTextureV={f(record["v"])};',
        f'inline constexpr std::array<float,{duration}> FlowWipeTranslateT{{{{',
        ','.join(f(v) for v in translate),
        '}};',
    ]



TRACK_KINDS = {'RLPA': 0, 'RLVC': 1, 'RLMC': 2, 'RLTS': 3, 'RLVI': 4}
# Kinoko/retail character id -> result icon (GetCharacterIconPaneName, USA
# string table near 0x8088C150) and Common.bmg name (0x2328 + id).
ICONS = ['mario', 'baby_peach', 'waluigi', 'koopa', 'baby_daisy', 'karon', 'baby_mario', 'luigi',
         'kinopio', 'donky', 'yoshi', 'wario', 'baby_luigi', 'kinopico', 'noko', 'daisy', 'peach',
         'catherine', 'didy', 'teresa', 'koopa_jr', 'hone_koopa', 'funky', 'roseta']
# Every driver the race runtime can field: all 24 Nintendo characters
# (include/mkw/roster.hpp). The Mii outfits keep the "?" icon.
RACE_CHARACTERS = list(range(24))


def rgba(values):
    r, g, b, a = (max(0, min(255, int(round(v)))) for v in values)
    return (r << 24) | (g << 16) | (b << 8) | a


def parse_layout(path):
    """Pane tree, materials and groups of one BRLYT, in file (draw) order."""
    d = path.read_bytes()
    textures, fonts, materials, panes, groups, stack, last = [], [], [], [], {}, [], -1
    for tag, block, s in sections(d):
        if tag == b'txl1':
            textures = [string(d, s + 4 + struct.unpack_from('>I', d, s + 4 + i * 8)[0])
                        for i in range(struct.unpack_from('>H', d, s)[0])]
        elif tag == b'fnl1':
            fonts = [string(d, s + 4 + struct.unpack_from('>I', d, s + 4 + i * 8)[0])
                     for i in range(struct.unpack_from('>H', d, s)[0])]
        elif tag == b'mat1':
            for i in range(struct.unpack_from('>H', d, s)[0]):
                o = block + struct.unpack_from('>I', d, s + 4 + i * 4)[0]
                flags = struct.unpack_from('>I', d, o + 60)[0]
                samplers = [struct.unpack_from('>HH', d, o + 64 + j * 4) for j in range(flags & 15)]
                off = o + 64 + len(samplers) * 4
                srt = list(struct.unpack_from('>5f', d, off)) if (flags >> 4) & 15 else [0, 0, 0, 1, 1]
                if len(samplers) > 1 or (flags >> 4) & 15 > 1:
                    raise ValueError(f'Multi-texture material in {path}')
                regs = struct.unpack_from('>12h', d, o + 20)
                materials.append({'name': string(d, o), 'c0': list(regs[0:4]), 'c1': list(regs[4:8]),
                                  'texture': textures[samplers[0][0]] if samplers else None,
                                  'wrap': ((samplers[0][1] >> 8) & 3, samplers[0][1] & 3) if samplers else (0, 0),
                                  'srt': srt, 'flags': flags})
        elif tag in (b'pan1', b'pic1', b'txt1', b'bnd1'):
            values = list(struct.unpack_from('>10f', d, s + 28))
            pane = {'name': string(d, s + 4), 'parent': stack[-1] if stack else -1,
                    'kind': {b'pic1': 1, b'txt1': 2}.get(tag, 0), 'flags': d[s], 'origin': d[s + 1],
                    'alpha': d[s + 2], 'values': values, 'material': -1,
                    'vertex': [[255] * 4] * 4, 'uv': [0, 0, 1, 0, 0, 1, 1, 1],
                    'textPosition': 0, 'textAlign': 0, 'font': [0, 0], 'charSpace': 0}
            if tag == b'pic1':
                c = s + 68
                pane['vertex'] = [list(d[c + j * 4:c + j * 4 + 4]) for j in range(4)]
                pane['material'] = struct.unpack_from('>H', d, c + 16)[0]
                if d[c + 18]:
                    pane['uv'] = list(struct.unpack_from('>8f', d, c + 20))
            elif tag == b'txt1':
                pane['material'] = struct.unpack_from('>H', d, s + 72)[0]
                position, align = d[s + 76], d[s + 77]
                pane['textPosition'] = position
                pane['textAlign'] = position % 3 if align == 0 else align - 1
                top, bottom = list(d[s + 84:s + 88]), list(d[s + 88:s + 92])
                pane['vertex'] = [top, top, bottom, bottom]
                pane['font'] = list(struct.unpack_from('>2f', d, s + 92))
                pane['charSpace'] = struct.unpack_from('>f', d, s + 100)[0]
                pane['fontName'] = fonts[struct.unpack_from('>H', d, s + 74)[0]]
            panes.append(pane)
            last = len(panes) - 1
        elif tag == b'pas1':
            stack.append(last)
        elif tag == b'pae1':
            stack.pop()
        elif tag == b'grp1':
            groups[string(d, s)] = [string(d, s + 20 + i * 16) for i in range(struct.unpack_from('>H', d, s + 16)[0])]
    return {'textures': textures, 'fonts': fonts, 'materials': materials, 'panes': panes, 'groups': groups}


def controller_clips(control, layout, anim_dir):
    """BRCTR animation groups; each clip keeps only the tracks of its group's
    panes and their materials (UIAnimator binding)."""
    d = control.read_bytes()
    offset, _, strings = struct.unpack_from('>3H', d, 12)
    count = struct.unpack_from('>H', d, offset + 2)[0]
    table = offset + 8 + count * 8
    pane_index = {p['name']: i for i, p in enumerate(layout['panes'])}
    material_index = {m['name']: i for i, m in enumerate(layout['materials'])}
    clips = []
    for g in range(count):
        group, panes, first, length = struct.unpack_from('>4H', d, offset + 8 + g * 8)
        group, panes = string(d, strings + group), string(d, strings + panes)
        members = set(layout['groups'][panes])
        materials = {layout['materials'][layout['panes'][pane_index[m]]['material']]['name']
                     for m in members if layout['panes'][pane_index[m]]['material'] >= 0}
        for j in range(first, first + length):
            name, file, next_name, _, rate = struct.unpack_from('>4Hf', d, table + j * 12)
            source = anim_dir / string(d, strings + file)
            duration, loop, tracks = brlan(source)
            bound = []
            for t in tracks:
                kind = TRACK_KINDS.get(t['kind'])
                if kind is None:
                    raise ValueError(f'Unsupported animation {t["kind"]} in {source}')
                if kind in (2, 3):
                    if t['element'] not in materials:
                        continue
                    target = material_index[t['element']]
                elif t['element'] in members:
                    target = pane_index[t['element']]
                else:
                    continue
                bound.append({'target': target, 'kind': kind, 'index': t['target'],
                              'step': t['curve'] == 1 or kind == 4, 'keys': t['keys']})
            clips.append({'group': group, 'name': string(d, strings + name), 'next': string(d, strings + next_name),
                          'frames': duration, 'loop': loop, 'rate': rate, 'tracks': bound, 'source': source})
    names = [(c['group'], c['name']) for c in clips]
    for c in clips:
        c['nextIndex'] = names.index((c['group'], c['next'])) if c['next'] else -1
    return clips


def controller_variants(control):
    """Control variants: name, delay, 16:9 transform and message id."""
    d = control.read_bytes()
    table, strings = struct.unpack_from('>2H', d, 14)
    count = struct.unpack_from('>H', d, table + 2)[0]
    result = {}
    for i in range(count):
        at = table + 12 + i * 60
        name, _, _, _, delay = struct.unpack_from('>4Hf', d, at)
        x, y, _, sx, sy = struct.unpack_from('>5f', d, at + 32)
        message = struct.unpack_from('>I', d, table + 12 + count * 60 + i * 8 + 4)[0]
        result[string(d, strings + name)] = {'delay': delay, 'x': x, 'y': y, 'sx': sx, 'sy': sy, 'message': message}
    return result


def font_glyphs(path, characters):
    """Glyph cells (BRFNT TGLP/CWDH/CMAP) for `characters`, plus FINF metrics."""
    from io import BytesIO
    from PIL import Image
    from gclib.gx_enums import ImageFormat, PaletteFormat
    from gclib.texture_utils import decode_image
    d = path.read_bytes()
    sheets, widths, mapping, info = [], {}, {}, None
    for tag, _, s in sections(d):
        if tag == b'FINF':
            info = {'linefeed': struct.unpack_from('>b', d, s + 1)[0], 'height': d[s + 20], 'width': d[s + 21], 'ascent': d[s + 22]}
        elif tag == b'TGLP':
            cw, ch, baseline, _, size, count, fmt, nx, ny, w, h, pixels = struct.unpack_from('>4BI6HI', d, s)
            info.update(cellW=cw, cellH=ch, baseline=baseline)
            for i in range(count):
                sheets.append(decode_image(BytesIO(d[pixels + i * size:pixels + (i + 1) * size]), BytesIO(),
                                           ImageFormat(fmt), PaletteFormat(0), 0, w, h).convert('RGBA'))
            per_sheet = nx * ny
        elif tag == b'CWDH':
            first, last = struct.unpack_from('>2H', d, s)
            for i in range(first, last + 1):
                widths[i] = struct.unpack_from('>bBb', d, s + 8 + (i - first) * 3)
        elif tag == b'CMAP':
            first, last, kind = struct.unpack_from('>3H', d, s)
            if kind == 0:
                begin = struct.unpack_from('>H', d, s + 12)[0]
                mapping.update({i: begin + i - first for i in range(first, last + 1)})
            elif kind == 1:
                mapping.update({i: struct.unpack_from('>H', d, s + 12 + 2 * (i - first))[0] for i in range(first, last + 1)})
            else:
                n = struct.unpack_from('>H', d, s + 12)[0]
                mapping.update(struct.unpack_from('>2H', d, s + 14 + 4 * i) for i in range(n))
    glyphs = {}
    for c in sorted(set(characters)):
        index = mapping[ord(c)]
        sheet, cell = divmod(index, per_sheet)
        y, x = divmod(cell, nx)
        left, width, advance = widths[index]
        image = sheets[sheet].crop((x * (info['cellW'] + 1), y * (info['cellH'] + 1),
                                    x * (info['cellW'] + 1) + max(width, 1), y * (info['cellH'] + 1) + info['cellH']))
        glyphs[c] = {'image': image, 'left': left, 'width': width, 'advance': advance}
    return info, glyphs


def pack_atlas(images, padding=1):
    """Shelf-pack images (name -> PIL image); returns size and pixel rects."""
    from PIL import Image
    order = sorted(images, key=lambda k: (-images[k].height, -images[k].width, k))
    for width in (64, 128, 256, 512, 1024):
        x = y = row = 0
        rects = {}
        for key in order:
            w, h = images[key].width + 2 * padding, images[key].height + 2 * padding
            if x + w > width:
                x, y, row = 0, y + row, 0
            rects[key] = (x + padding, y + padding, images[key].width, images[key].height)
            x, row = x + w, max(row, h)
        height = max(8, 1 << (y + row - 1).bit_length())
        if height <= width:
            atlas = Image.new('RGBA', (width, height))
            for key, (px, py, _, _) in rects.items():
                atlas.paste(images[key], (px, py))
            return atlas, rects
    raise ValueError('Atlas exceeds 1024x1024')


def texture_file(image, name, out, fmt):
    import subprocess
    png = out / 'textures' / (name + '.png')
    image.save(png)
    subprocess.run([str(tool('tex3ds')), '-f', fmt, '-z', 'none', '-o', str(png.with_suffix('.t3x')), str(png)],
                   check=True, stdout=subprocess.DEVNULL)
    return 'textures/' + name + '.t3x'


def emit_layout(prefix, layout, clips, texture_slot, header):
    """C++ tables for one layout: panes, materials, clips, tracks and keys."""
    keys, tracks = [], []
    clip_lines = []
    for c in clips:
        first = len(tracks)
        for t in c['tracks']:
            tracks.append((t['target'], t['kind'], t['index'], t['step'], len(keys), len(t['keys'])))
            keys.extend(t['keys'])
        clip_lines.append(f'    {{"{c["group"]}","{c["name"]}",{f(c["frames"])},{f(c["rate"])},{str(c["loop"]).lower()},'
                          f'{c["nextIndex"]},{first},{len(tracks) - first}}}, // {c["source"].name}')
    header.append(f'inline constexpr FlowPane {prefix}Panes[]={{')
    for p in layout['panes']:
        v = p['values']
        vertex = ','.join(f'0x{rgba(c):08x}u' for c in p['vertex'])
        header.append(f'    {{"{p["name"]}",{p["parent"]},{p["kind"]},{p["flags"]},{p["origin"]},{p["alpha"]},{p["material"]},'
                      f'{f(v[0])},{f(v[1])},{f(v[2])},{f(v[5])},{f(v[6])},{f(v[7])},{f(v[8])},{f(v[9])},{{{{{vertex}}}}},'
                      f'{{{{{",".join(f(u) for u in p["uv"])}}}}},{p["textPosition"]},{p["textAlign"]},'
                      f'{f(p["font"][0])},{f(p["font"][1])},{f(p["charSpace"])}}},')
    header.append('};')
    header.append(f'inline constexpr FlowMaterial {prefix}Materials[]={{')
    for m in layout['materials']:
        slot = texture_slot(m['texture']) if m['texture'] else -1
        header.append(f'    {{"{m["name"]}",{{{{{",".join(str(c) for c in m["c0"])}}}}},{{{{{",".join(str(c) for c in m["c1"])}}}}},'
                      f'{slot},{m["wrap"][0]},{m["wrap"][1]},{{{{{",".join(f(x) for x in m["srt"])}}}}}}},')
    header.append('};')
    if not clips:
        # A static layout: no clip, track or key tables.
        header.append(f'inline constexpr FlowLayoutDef {prefix}Layout{{{prefix}Panes,{len(layout["panes"])},{prefix}Materials,'
                      f'{len(layout["materials"])},nullptr,0,nullptr,nullptr}};')
        return
    header.append(f'inline constexpr FlowClip {prefix}Clips[]={{')
    header += clip_lines
    header.append('};')
    header.append(f'inline constexpr FlowTrack {prefix}Tracks[]={{')
    header += [f'    {{{t[0]},{t[1]},{t[2]},{str(t[3]).lower()},{t[4]},{t[5]}}},' for t in tracks]
    header.append('};')
    header.append(f'inline constexpr FlowKey {prefix}Keys[]={{')
    header += ['    ' + ','.join(f'{{{f(k[0])},{f(k[1])},{f(k[2])}}}' for k in keys[i:i + 4]) + ',' for i in range(0, len(keys), 4)]
    header.append('};')
    header.append(f'inline constexpr FlowLayoutDef {prefix}Layout{{{prefix}Panes,{len(layout["panes"])},{prefix}Materials,'
                  f'{len(layout["materials"])},{prefix}Clips,{len(clips)},{prefix}Tracks,{prefix}Keys}};')


def cook_results(out, manifest, header):
    """Result board rows (result/position_12players.brlyt, ResultVS.brctr) and
    the VS end-of-race buttons (button/common_w004_menu.brlyt, AfterMenuVS and
    AfterMenuVSLast.brctr), drawn pane by pane at runtime."""
    from menu_layout import messages
    sources = []
    result_dir, button_dir = UI / 'Race/result', UI / 'Race/button'
    row = parse_layout(result_dir / 'blyt/position_12players.brlyt')
    row_clips = controller_clips(result_dir / 'ctrl/ResultVS.brctr', row, result_dir / 'anim')
    button = parse_layout(button_dir / 'blyt/common_w004_menu.brlyt')
    button_clips = controller_clips(button_dir / 'ctrl/AfterMenuVS.brctr', button, button_dir / 'anim')
    last_clips = controller_clips(button_dir / 'ctrl/AfterMenuVSLast.brctr', button, button_dir / 'anim')
    if [(c['group'], c['name'], c['source'].name) for c in last_clips] != [(c['group'], c['name'], c['source'].name) for c in button_clips]:
        raise ValueError('AfterMenuVSLast binds different button animations')
    sources += [result_dir / 'blyt/position_12players.brlyt', result_dir / 'ctrl/ResultVS.brctr',
                button_dir / 'blyt/common_w004_menu.brlyt', button_dir / 'ctrl/AfterMenuVS.brctr',
                button_dir / 'ctrl/AfterMenuVSLast.brctr']
    sources += sorted({c['source'] for c in row_clips + button_clips})
    race = messages(UI / 'Race_U/message/Race.bmg')
    common = messages(UI / 'Race_U/message/Common.bmg')
    sources += [UI / 'Race_U/message/Race.bmg', UI / 'Race_U/message/Common.bmg']

    textures = []
    def add(name, image, fmt):
        textures.append(texture_file(image, name, out, fmt))
        return len(textures) - 1
    row_pattern = add('flow_result_pattern', decode_tpl(result_dir / 'timg/tt_pattern_chek_bokashi_32x32.tpl'), 'rgba8')
    row_alpha = add('flow_result_alpha', decode_tpl(result_dir / 'timg/tt_pattern_chek_32x32_alpha.tpl'), 'rgba8')
    button_pattern = add('flow_button_pattern', decode_tpl(button_dir / 'timg/tt_pattern_chek_bokashi_32x32.tpl'), 'rgba8')
    sources += [result_dir / 'timg/tt_pattern_chek_bokashi_32x32.tpl', result_dir / 'timg/tt_pattern_chek_32x32_alpha.tpl',
                button_dir / 'timg/tt_pattern_chek_bokashi_32x32.tpl']
    icon_images = {i: decode_tpl(result_dir / f'timg/tt_{ICONS[i]}_64x64.tpl') for i in RACE_CHARACTERS}
    icon_images[-1] = decode_tpl(result_dir / 'timg/tt_hatena_64x64.tpl')
    sources += [result_dir / f'timg/tt_{ICONS[i]}_64x64.tpl' for i in RACE_CHARACTERS] + [result_dir / 'timg/tt_hatena_64x64.tpl']
    # 64x64 icons tile a 256x256 atlas exactly; their borders are transparent.
    icon_atlas, icon_rects = pack_atlas(icon_images, padding=0)
    icons = add('flow_result_icons', icon_atlas, 'rgba8')

    # Row text: kart_kanji_font. Positions 1st..12th, names, numbers, pt/pts.
    names = [common[0x2328 + i] for i in range(24)]
    positions = [race[0x515 + i] for i in range(12)]
    unit = [race[0x524], race[0x525]]
    labels = {'next': race[0x3f1], 'quit': race[0x3ec], 'last': race[0x3f2]}
    # These messages carry BMG font attribute 1 (BMGHolder::GetFontIndex), the
    # kart_kanji_font message font, in both layouts; the layouts' own
    # tt_kart_font_rodan subset only covers 30 capitals.
    kanji_text = ''.join(names + positions + unit + list(labels.values())) + '0123456789+'
    fonts = []
    for key, font_name, text in (('Kanji', 'kart_kanji_font', kanji_text),):
        font_path = UI / f'Font/{font_name}.brfnt'
        sources.append(font_path)
        info, glyphs = font_glyphs(font_path, text.replace(' ', '') + ' ')
        atlas, rects = pack_atlas({c: g['image'] for c, g in glyphs.items()})
        slot = add(f'flow_font_{key.lower()}', atlas, 'la4')
        fonts.append((key, info, glyphs, rects, atlas.size, slot))

    def texture_slot(name):
        table = {('position_12players', 'tt_pattern_chek_bokashi_32x32.tpl'): row_pattern,
                 ('position_12players', 'tt_pattern_chek_32x32_alpha.tpl'): row_alpha,
                 ('position_12players', 'tt_mario_64x64.tpl'): icons,
                 ('common_w004_menu', 'tt_pattern_chek_bokashi_32x32.tpl'): button_pattern}
        return table[(texture_slot.layout, name)]

    if any(p['values'][2] for p in row['panes']):
        raise ValueError('Result rows are drawn pane-major across rows, which needs equal pane depths')
    variants = controller_variants(result_dir / 'ctrl/ResultVS.brctr')
    buttons = controller_variants(button_dir / 'ctrl/AfterMenuVS.brctr')
    last = controller_variants(button_dir / 'ctrl/AfterMenuVSLast.brctr')
    if buttons['ButtonNext']['message'] != 0x3f1 or buttons['ButtonQuit']['message'] != 0x3ec or last['ButtonNext']['message'] != 0x3f2:
        raise ValueError('Unexpected VS end-of-race button messages')

    header += [
        '// Result board and VS end-of-race menu (tools/cook_race_flow.py cook_results).',
        '// NW4R panes in draw order; materials reduce to lerp(C0,C1,tex) x vertex colour.',
        '// Section::Draw keeps LEQUAL depth with writes on: a pane with a larger z hides',
        '// later panes with smaller z (the button plate over its border pattern).',
        'struct FlowPane {const char* name;int16_t parent;uint8_t kind,flags,origin,alpha;int16_t material;',
        '    float x,y,z,rotation,sx,sy,w,h;std::array<uint32_t,4> vertex;std::array<float,8> uv;',
        '    uint8_t textPosition,textAlign;float fontW,fontH,charSpace;};',
        'struct FlowMaterial {const char* name;std::array<int16_t,4> c0,c1;int8_t texture;uint8_t wrapS,wrapT;std::array<float,5> srt;};',
        '// kind: 0 RLPA pane SRT, 1 RLVC pane colour/alpha, 2 RLMC material colour, 3 RLTS texture SRT, 4 RLVI visibility.',
        'struct FlowKey {float frame,value,slope;};',
        'struct FlowTrack {uint16_t target;uint8_t kind,index;bool step;uint16_t first,count;};',
        'struct FlowClip {const char* group;const char* name;float frames,rate;bool loop;int16_t next;uint16_t firstTrack,trackCount;};',
        'struct FlowLayoutDef {const FlowPane* panes;uint16_t paneCount;const FlowMaterial* materials;uint16_t materialCount;',
        '    const FlowClip* clips;uint16_t clipCount;const FlowTrack* tracks;const FlowKey* keys;};',
        'struct FlowGlyph {char16_t code;uint16_t x,y,w;int8_t left;uint8_t advance;};',
        'struct FlowFont {int8_t texture;uint16_t atlasW,atlasH;uint8_t cellH,width,height,linefeed;const FlowGlyph* glyphs;uint16_t glyphCount;};',
        'struct FlowControl {float x,y,sx,sy,delay;};',
        '// Textures are loaded when the player finishes and released with the race.',
        f'inline constexpr std::array<const char*,{len(textures)}> FlowResultTextures{{{{{",".join(chr(34) + t + chr(34) for t in textures)}}}}};',
    ]
    for key, info, glyphs, rects, size, slot in fonts:
        header.append(f'inline constexpr FlowGlyph Flow{key}Glyphs[]={{')
        for c in sorted(glyphs):
            x, y, w, _ = rects[c]
            g = glyphs[c]
            header.append(f'    {{u\'{c if c not in chr(39) + chr(92) else chr(92) + c}\',{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
        header.append('};')
        header.append(f'inline constexpr FlowFont Flow{key}Font{{{slot},{size[0]},{size[1]},{info["cellH"]},{info["width"]},'
                      f'{info["height"]},{info["linefeed"]},Flow{key}Glyphs,{len(glyphs)}}}; // {f"{key}"}')
    header.append(f'inline constexpr int8_t FlowIconTexture={icons};')
    header.append('// Character icon atlas rects (pixels: x,y,w,h) by retail character id; -1 rows use "?".')
    header.append('inline constexpr std::array<std::array<int16_t,4>,25> FlowIconRects{{')
    for i in list(range(24)) + [-1]:
        r = icon_rects.get(i, icon_rects[-1])
        header.append(f'    {{{{{r[0]},{r[1]},{r[2]},{r[3]}}}}}, // {ICONS[i] if i >= 0 else "hatena"}')
    header.append('}};')
    header.append(f'inline constexpr uint16_t FlowIconAtlas[2]={{{icon_atlas.size[0]},{icon_atlas.size[1]}}};')
    header.append('inline constexpr std::array<const char*,24> FlowCharacterNames{{' + ','.join(f'"{n}"' for n in names) + '}};')
    header.append('inline constexpr std::array<const char*,12> FlowPositionText{{' + ','.join(f'"{p}"' for p in positions) + '}};')
    header.append(f'inline constexpr const char* FlowPointText[2]={{"{unit[0]}","{unit[1]}"}}; // Race.bmg 0x524/0x525')
    header.append(f'inline constexpr const char* FlowButtonText[3]={{"{labels["next"]}","{labels["quit"]}","{labels["last"]}"}}; // 0x3F1, 0x3EC, 0x3F2')
    header.append('// ResultVS.brctr rank1..rank12 (16:9 transform, page-slide delay).')
    header.append('inline constexpr std::array<FlowControl,12> FlowResultRows{{')
    for i in range(12):
        v = variants[f'rank{i + 1}']
        header.append(f'    {{{f(v["x"])},{f(v["y"])},{f(v["sx"])},{f(v["sy"])},{f(v["delay"])}}},')
    header.append('}};')
    header.append('// AfterMenuVS ButtonNext/ButtonQuit and AfterMenuVSLast ButtonNext.')
    header.append('inline constexpr std::array<FlowControl,3> FlowButtons{{')
    for v in (buttons['ButtonNext'], buttons['ButtonQuit'], last['ButtonNext']):
        header.append(f'    {{{f(v["x"])},{f(v["y"])},{f(v["sx"])},{f(v["sy"])},{f(v["delay"])}}},')
    header.append('}};')
    # PushButton palette (USA 0x8088FD18, read from the user's StaticR): slot 1
    # is the first player's selection, slot 5 an unselected button; C0/C1 tint
    # fuchi_pattern and the third colour fills color_base.
    rel = ROOT / 'build/itembox-retail.rel'
    raw = rel.read_bytes()
    base = 0x8050BF60
    palette = [[list(struct.unpack_from('>4h', raw, 0x8088FD18 - base + slot * 24 + i * 8)) for i in range(3)] for slot in (1, 5)]
    sources.append(rel)
    header.append('inline constexpr int16_t FlowButtonPalette[2][3][4]={' + ','.join(
        '{' + ','.join('{' + ','.join(str(v) for v in colour) + '}' for colour in slot) + '}' for slot in palette) + '}; // selected, free')
    texture_slot.layout = 'position_12players'
    emit_layout('FlowResult', row, row_clips, texture_slot, header)
    texture_slot.layout = 'common_w004_menu'
    emit_layout('FlowButton', button, button_clips, texture_slot, header)
    manifest['results'] = {'textures': textures, 'sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}}


def bmg_formats(path):
    """BMG messages with their argument commands as printf conversions: a
    message argument (control group 2, type 0x11) becomes %s and a number
    (type 0x10) %u, in argument order. Returns {id: text}."""
    d = path.read_bytes()
    offset, blocks = 32, {}
    for _ in range(struct.unpack_from('>I', d, 12)[0]):
        blocks[d[offset:offset + 4]] = offset
        offset += struct.unpack_from('>I', d, offset + 4)[0]
    info, strings = blocks[b'INF1'], blocks[b'DAT1'] + 8
    count, stride = struct.unpack_from('>HH', d, info + 8)
    result = {}
    for index in range(count):
        at = strings + struct.unpack_from('>I', d, info + 16 + index * stride)[0]
        text, arguments = '', [[], []]
        while d[at:at + 2] != b'\0\0':
            if d[at:at + 2] == b'\0\x1a':
                size, group, kind = d[at + 2], d[at + 3], struct.unpack_from('>H', d, at + 4)[0]
                if group == 2 and kind in (0x10, 0x11):
                    arguments[kind & 1].append(struct.unpack_from('>H', d, at + 6)[0])
                    text += '%s' if kind == 0x11 else '%u'
                at += size
                continue
            c = d[at:at + 2].decode('utf-16be')
            if c == '%':
                raise ValueError('Literal % in a title-card message')
            text += c
            at += 2
        # None: arguments out of order, which a printf format cannot express.
        ordered = all(a == list(range(len(a))) for a in arguments)
        result[struct.unpack_from('>I', d, blocks[b'MID1'] + 16 + index * 4)[0]] = text if ordered else None
    return result


# DemoPage_onInit (PAL 0x80855200) helpers: course -> cup (fn_1_350000,
# 0x808603B4) and cup -> cup_icon_64x64_common pane (fn_1_3507A0 table at
# 0x808AF960: icon_03_kinoko, icon_02_flower, icon_07_star, icon_06_oukan,
# icon_05_koura, icon_01_banana, icon_04_konoha, icon_00_thunder).
COURSE_CUP = {0: 1, 5: 1, 6: 1, 7: 1, 1: 0, 2: 0, 4: 0, 8: 0, 3: 2, 9: 2, 0xb: 2, 0xf: 2, 10: 3, 0xc: 3, 0xd: 3, 0xe: 3,
              0x10: 4, 0x14: 4, 0x19: 4, 0x1a: 4, 0x11: 6, 0x15: 6, 0x1d: 6, 0x1e: 6, 0x12: 5, 0x17: 5, 0x1b: 5, 0x1f: 5,
              0x13: 7, 0x16: 7, 0x18: 7, 0x1c: 7}
CUP_ICON = [3, 2, 7, 6, 5, 1, 4, 0]


def cook_demo(out, manifest, header):
    """The course preview's title card (DemoPage): demo/course_name and
    demo/cup_name with the cup_icon_64x64_common icons. Both controls are
    static: their BRCTR files bind no animation and the variant's animate
    flag is 0, so the card shows unchanged for the whole Demo section."""
    sources = []
    demo = UI / 'Race/demo'
    course = parse_layout(demo / 'blyt/course_name.brlyt')
    cup = parse_layout(demo / 'blyt/cup_name.brlyt')
    icons = parse_layout(demo / 'blyt/cup_icon_64x64_common.brlyt')
    sources += [demo / 'blyt/course_name.brlyt', demo / 'blyt/cup_name.brlyt', demo / 'blyt/cup_icon_64x64_common.brlyt',
                demo / 'ctrl/course_name.brctr', demo / 'ctrl/cup_name.brctr']
    for name, layout in (('course_name', course), ('cup_name', cup)):
        if controller_clips(demo / f'ctrl/{name}.brctr', layout, demo / 'anim'):
            raise ValueError(f'{name}.brctr binds animations; the card is drawn static')
    course_control = controller_variants(demo / 'ctrl/course_name.brctr')['course_name']
    cup_control = controller_variants(demo / 'ctrl/cup_name.brctr')['cup_name']

    race = bmg_formats(UI / 'Race_U/message/Race.bmg')
    common = bmg_formats(UI / 'Race_U/message/Common.bmg')
    sources += [UI / 'Race_U/message/Race.bmg', UI / 'Race_U/message/Common.bmg']
    # fn_1_3232B4: courses 0x2454 + id, arenas 0x2498 + id.
    course_names = [common[0x2454 + i] if i < 0x20 else common[0x2498 + i] for i in range(0x2a)]
    cup_names = [common[0x23f0 + i] for i in range(8)]
    classes = [race[0x589 + i] for i in range(4)]       # 50cc, 100cc, 150cc, Mirror (0x58C)
    gp_races = [race[0x582 + i] for i in range(4)]       # "%s %s 1st Race" .. "Final Race"
    vs_race, battles = race[0x586], [race[0x587], race[0x588]]
    texts = course_names + cup_names + classes + gp_races + [vs_race] + battles
    if None in texts:
        raise ValueError('A title-card message has its arguments out of order')
    if any(t.count('%s') != 2 or '%u' in t for t in gp_races) or vs_race.count('%s') != 1 or vs_race.count('%u') != 2:
        raise ValueError('Unexpected title-card message arguments')
    if any(ord(c) > 127 for t in texts for c in t):
        raise ValueError('Title-card text outside ASCII')

    textures = []
    def add(name, image, fmt):
        textures.append(texture_file(image, name, out, fmt))
        return len(textures) - 1
    # Message font attribute 1 (BMGHolder::GetFontIndex): kart_kanji_font, as on the result board.
    font_path = UI / 'Font/kart_kanji_font.brfnt'
    sources.append(font_path)
    characters = ''.join(t.replace('%s', '').replace('%u', '') for t in texts) + '0123456789'
    info, glyphs = font_glyphs(font_path, characters.replace(' ', '') + ' ')
    atlas, rects = pack_atlas({c: g['image'] for c, g in glyphs.items()})
    font_slot = add('flow_demo_font', atlas, 'la4')
    # One texture per icon pane, in pane order; the card loads only its own.
    icon_panes = [p for p in icons['panes'] if p['kind'] == 1]
    if [p['name'][:7] for p in icon_panes] != [f'icon_{i:02d}' for i in range(13)]:
        raise ValueError('Unexpected cup_icon_64x64_common panes')
    for i, p in enumerate(icon_panes):
        tpl = demo / 'timg' / icons['materials'][p['material']]['texture']
        sources.append(tpl)
        add(f'flow_demo_icon_{i:02d}', decode_tpl(tpl), 'rgba8')

    def texture_slot(name):
        if name != 'tt_cup_icon_kinoko_00.tpl':
            raise ValueError(f'Unexpected title-card texture {name}')
        return 1   # FlowDemoIconSlot: the card's own icon
    header += [
        '// Course preview title card (DemoPage_onInit, PAL 0x80855200; tools/cook_race_flow.py cook_demo):',
        '// demo/course_name and demo/cup_name, static, shown for the whole Demo section and faded with it.',
        '// FlowDemoTextures: the card font, then the 13 cup_icon_64x64_common icons in pane order.',
        f'inline constexpr std::array<const char*,{len(textures)}> FlowDemoTextures{{{{{",".join(chr(34) + t + chr(34) for t in textures)}}}}};',
        'inline constexpr int8_t FlowDemoIconSlot=1; // material texture slot of cup_icon: the loaded icon',
        f'inline constexpr FlowGlyph FlowDemoGlyphs[]={{',
    ]
    for c in sorted(glyphs):
        x, y, w, _ = rects[c]
        g = glyphs[c]
        header.append(f'    {{u\'{c if c not in chr(39) + chr(92) else chr(92) + c}\',{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
    header.append('};')
    header.append(f'inline constexpr FlowFont FlowDemoFont{{{font_slot},{atlas.size[0]},{atlas.size[1]},{info["cellH"]},{info["width"]},'
                  f'{info["height"]},{info["linefeed"]},FlowDemoGlyphs,{len(glyphs)}}}; // kart_kanji_font')
    emit_layout('FlowCourseName', course, [], texture_slot, header)
    emit_layout('FlowCupName', cup, [], texture_slot, header)
    header.append('// course_name.brctr / cup_name.brctr variants (16:9 transform).')
    for key, v in (('FlowCourseNameControl', course_control), ('FlowCupNameControl', cup_control)):
        header.append(f'inline constexpr FlowControl {key}{{{f(v["x"])},{f(v["y"])},{f(v["sx"])},{f(v["sy"])},{f(v["delay"])}}};')
    def strings(values):
        return ','.join('"' + v.replace('\\', '\\\\').replace('"', '\\"') + '"' for v in values)
    header.append(f'inline constexpr std::array<const char*,{len(course_names)}> FlowCourseNames{{{{{strings(course_names)}}}}}; // by retail course id')
    header.append(f'inline constexpr std::array<const char*,8> FlowCupNames{{{{{strings(cup_names)}}}}}; // Common.bmg 0x23F0')
    header.append(f'inline constexpr std::array<const char*,4> FlowEngineClassText{{{{{strings(classes)}}}}}; // Race.bmg 0x589-0x58C')
    header.append(f'inline constexpr std::array<const char*,4> FlowGpRaceText{{{{{strings(gp_races)}}}}}; // 0x582-0x585: class, cup')
    header.append(f'inline constexpr const char* FlowVsRaceText={strings([vs_race])}; // 0x586: class, race, races')
    header.append(f'inline constexpr std::array<const char*,2> FlowBattleText{{{{{strings(battles)}}}}}; // 0x587, 0x588')
    header.append('inline constexpr std::array<int8_t,0x20> FlowCourseCup{{' + ','.join(str(COURSE_CUP[i]) for i in range(0x20)) + '}}; // fn_1_350000')
    header.append('inline constexpr std::array<int8_t,8> FlowCupIcon{{' + ','.join(str(i) for i in CUP_ICON) + '}}; // fn_1_3507A0')
    header.append('inline constexpr int8_t FlowIconHatena=8,FlowIconBalloon=9,FlowIconCoin=10,FlowIconFlag=11;')
    manifest['demo'] = {'textures': textures, 'sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/race-flow')
    args = parser.parse_args()
    out = args.output
    (out / 'textures').mkdir(parents=True, exist_ok=True)
    manifest = {'version': 1}
    header = ['// Generated by tools/cook_race_flow.py from the retail Race UI archives.',
              '#pragma once', '#include <array>', '#include <cstdint>', 'namespace mkw {']
    cook_wipe(out, manifest, header)
    cook_results(out, manifest, header)
    cook_demo(out, manifest, header)
    header.append('}')
    (ROOT / 'include/mkw/race_flow_layout.hpp').write_text('\n'.join(header) + '\n')
    (out / 'race-flow.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'textures': sorted(p.name for p in (out / 'textures').glob('*.t3x'))}))


if __name__ == '__main__':
    main()
