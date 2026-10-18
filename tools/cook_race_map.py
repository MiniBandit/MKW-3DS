#!/usr/bin/env python3
"""Cook the race bottom screen (src/race_bottom_screen.cpp): retail Mario Kart
Wii's minimap of every course and arena, and the screen's UI atlas.

Minimap. Retail draws no map model per frame: Map2DRenderer_createInstance (PAL
0x807DF908, called by RaceScene_createSubsystems) renders the course's
map_model.brres once into a 220x220 capture (Map2DRenderer_init 0x807DFBA4,
Map2DRenderer_render 0x807E0028), and CtrlRace2DMap (0x807EA27C) shows that
capture on its race_null pane. This tool makes the same capture from the same
model: build/export_brres.mjs (tools/export_brres.ts) reads map_model.brres and
bake() rasterises it with Map2DRenderer's frame, camera, depth test, culling,
EFB and copy formats. Races copy GX IA4, which PICA LA4 holds unchanged (4-bit
intensity, 4-bit alpha); battle modes (game modes 3, 9, 10) copy RGB5A3, held
by RGBA5551 because every capture alpha is 0 or 255.

UI atlas (RGBA8): the map icons' retail art at its own resolution (st_*_32x32
faces, tt_map_chara_searchlight, tt_start_line_22), the standings rows (result
board bars, 1st..12th in the result font, faces, item icons) and the timer
(CtrlRaceTime's digits, separators and TIME label at the size the bottom screen
draws them, so their texels map 1:1 to its pixels).

Outputs: build/race-map/textures/*.t3x (a packaging overlay), build/race-map/
race-map.json (sources, hashes, frames, bake statistics) and the generated
include/mkw/race_map_assets.hpp. The shared sd-card is never written.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import re
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from devkitpro import tool

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'vendor/gclib'))

TEX3DS = str(tool('tex3ds'))
UI = ROOT / 'assets/race-ui'
GAME = UI / 'Race/game_image'
GAME_U = UI / 'Race_U/game_image'
RESULT = UI / 'Race/result'
# Retail character id (Kinoko Character) -> st_*_32x32 map icon (character_map_icon_common.brlyt).
FACES = ['mario', 'baby_peach', 'waluigi', 'koopa', 'baby_daisy', 'karon', 'baby_mario', 'luigi',
         'kinopio', 'donky', 'yoshi', 'wario', 'baby_luigi', 'kinopico', 'noko', 'daisy', 'peach',
         'catherine', 'didy', 'teresa', 'koopa_jr', 'hone_koopa', 'fuky', 'roseta']
# Map2DRenderer_init: an EGG::CapTexture of 0xdc x 0xdc texels, in a 256x256 PICA texture.
CAPTURE = 220
TEXTURE = 256
# The bottom screen draws CtrlRaceTime at this scale of its layout units: its
# 32-unit digits are 20 px, so the timer (y 0..20) sits above the 220 px map.
TIMER_SCALE = 0.625


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def courses():
    """Every catalog row (include/mkw/course_catalog.hpp), in catalog order."""
    text = (ROOT / 'include/mkw/course_catalog.hpp').read_text()
    rows = re.findall(r'\{"([^"]+)",Kinoko::Course::(\w+),"(Race/Course/([^"]+)\.szs)","models/[^"]+\.list","[^"]*","(\w+)"', text)
    return [dict(name=n, id=i, archive=a, key=k, arena=cup == 'Arena') for n, i, a, k, cup in rows]


def course_folder(course, data, work):
    """The course's unpacked archive: assets/courses, else unpacked from data."""
    folder = ROOT / 'assets/courses' / course['key']
    if (folder / 'map_model.brres').is_file() and (folder / 'course.kmp').is_file():
        return folder
    out = work / 'courses' / course['key']
    if not (out / 'map_model.brres').is_file():
        from unpack_szs import unpack
        unpack(data / course['archive'], out)
    return out


def node_translations(brres):
    """Local translation (ResNodeData +0x38) of every node of every MDL0, by name:
    what Map2DRenderer_init reads for posLD and posRU (ResMdl node lookup 0x8004E2A0)."""
    data = Path(brres).read_bytes()

    def dictionary(offset):
        count = struct.unpack_from('>I', data, offset + 4)[0]
        out = []
        for i in range(1, count + 1):
            name, target = struct.unpack_from('>ii', data, offset + 16 + 16 * i)
            out.append((data[offset + name:data.index(b'\0', offset + name)].decode('latin1'), offset + target))
        return out
    nodes = {}
    for group, at in dictionary(struct.unpack_from('>H', data, 12)[0] + 8):
        if group != '3DModels(NW4R)':
            continue
        for _, mdl in dictionary(at):
            if data[mdl:mdl + 4] != b'MDL0' or struct.unpack_from('>I', data, mdl + 8)[0] != 11:
                raise ValueError(f'{brres}: expected an MDL0 version 11')
            for name, node in dictionary(mdl + struct.unpack_from('>i', data, mdl + 0x10 + 4)[0]):
                nodes[name] = list(struct.unpack_from('>3f', data, node + 0x38))
    return nodes


def frame(ld, rd):
    """Map2DRenderer_render 0x807E0028: the world square the capture covers,
    (x0, z0, x1, z1) as fn_1_2D0530 0x807E08E4 reads them (+0x90, +0x98, +0x9C,
    +0xA4): the longer of the bones' two extents, centred on their midpoint."""
    f = np.float32
    ld = [f(v) for v in ld]
    ru = [f(v) for v in rd]
    w = f(220)  # DAT_808a7780
    across, down = w / (ru[0] - ld[0]), w / (ld[2] - ru[2])
    if down <= across:
        z0, z1 = ld[2], ru[2]
        centre, half = (ld[0] + ru[0]) * f(.5), ((w * (ld[2] - ru[2])) / w) * f(.5)
        x0, x1 = centre - half, centre + half
    else:
        x0, x1 = ld[0], ru[0]
        centre, half = (ld[2] + ru[2]) * f(.5), ((w * (ru[0] - ld[0])) / w) * f(.5)
        z0, z1 = centre + half, centre - half
    return float(x0), float(z0), float(x1), float(z1)


def bake(model, square):
    """The capture: Map2DRenderer_render's EFB, before the copy.

    * Clear quad (255,255,255,0) (DAT_808a7774) over the whole capture.
    * No KMP AREA of type 5 (fn_1_2D01C8 0x807E057C's cut-outs, fn_1_66AC
      0x80516A60's bounds) exists on any retail track; cook_course checks it.
    * The model, scaled (s, 0.006, s) and seen from 300 units above its
      centre, looking down with -Z up (up = -ez, 0x802A4148) through a 440x440
      orthographic projection: x right, z down, the higher surface wins (z
      compare LEQUAL), the material's cull mode applies.
    * Map materials are unlit vertex colour (GX channel source VERTEX, one TEV
      stage RASC/RASA), so a pixel is the interpolated vertex colour and alpha.
    """
    x0, z0, x1, z1 = square
    image = np.zeros((CAPTURE, CAPTURE, 4))
    image[..., :3] = 255
    height = np.full((CAPTURE, CAPTURE), -np.inf)
    centres = np.arange(CAPTURE) + .5
    stats = {'triangles': 0, 'culled': 0, 'drawn': 0}
    for m in model['models']:
        for shape in m['shapes']:
            gx = shape['sourceMaterial']['gxMaterial']
            stage = gx['tevStages']
            channel = gx['lightChannels'][0]
            if (shape['texture'] != -1 or len(stage) != 1 or stage[0]['colorInA'] != 10 or stage[0]['colorInD'] != 15
                    or stage[0]['alphaInA'] != 5 or stage[0]['alphaInD'] != 7 or channel['colorChannel']['lightingEnabled']
                    or channel['colorChannel']['matColorSource'] != 1 or channel['alphaChannel']['matColorSource'] != 1
                    or shape['sourceMaterial']['translucent']):
                raise ValueError('Map material is not unlit vertex colour: ' + shape['material'])
            cull = gx['cullMode']
            v = np.asarray(shape['vertices'], dtype=np.float64)
            screen = np.stack([(v[:, 0] - x0) / (x1 - x0) * CAPTURE, (v[:, 2] - z1) / (z0 - z1) * CAPTURE], -1)
            for t in range(0, len(v), 3):
                stats['triangles'] += 1
                a, b, c = v[t:t + 3, :3]
                # GX front faces are clockwise on screen: seen from above, the
                # vertex order's right-handed normal points down. GX_CULL_BACK (2)
                # drops the others, GX_CULL_FRONT (1) these, GX_CULL_ALL (3) both.
                # (The map meshes carry no normals; nearly every triangle is front.)
                front = np.cross(b - a, c - a)[1] < 0
                if cull == 3 or (cull == 2 and not front) or (cull == 1 and front):
                    stats['culled'] += 1
                    continue
                p = screen[t:t + 3]
                area = (p[1, 0] - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[1, 1] - p[0, 1]) * (p[2, 0] - p[0, 0])
                if area == 0:
                    continue
                order = [0, 1, 2] if area > 0 else [0, 2, 1]
                q = p[order]
                left, right = max(0, int(math.floor(q[:, 0].min()))), min(CAPTURE, int(math.ceil(q[:, 0].max())) + 1)
                top, bottom = max(0, int(math.floor(q[:, 1].min()))), min(CAPTURE, int(math.ceil(q[:, 1].max())) + 1)
                if left >= right or top >= bottom:
                    continue
                px, py = np.meshgrid(centres[left:right], centres[top:bottom])
                weights = []
                inside = np.ones(px.shape, bool)
                for i in range(3):
                    s, e = q[(i + 1) % 3], q[(i + 2) % 3]
                    w = (e[0] - s[0]) * (py - s[1]) - (e[1] - s[1]) * (px - s[0])
                    # Top-left rule (y down): a pixel centre on a shared edge belongs to one triangle.
                    edge_top_left = (e[1] == s[1] and e[0] < s[0]) or e[1] < s[1]
                    inside &= (w > 0) | ((w == 0) & edge_top_left)
                    weights.append(w)
                if not inside.any():
                    continue
                total = abs(area)
                bary = [weights[i] / total for i in range(3)]
                vertex = v[t:t + 3][order]
                h = sum(bary[i] * vertex[i, 1] for i in range(3))
                rgba = sum(bary[i][..., None] * vertex[i, 10:14] for i in range(3))
                region = height[top:bottom, left:right]
                passed = inside & (h >= region)
                if not passed.any():
                    continue
                region[passed] = h[passed]
                image[top:bottom, left:right][passed] = np.clip(np.rint(rgba[passed]), 0, 255)
                stats['drawn'] += 1
    # The EFB keeps 6 bits per channel (RGBA6_Z24: the capture copies alpha).
    six = np.floor(image).astype(np.int32) >> 2
    return ((six << 2) | (six >> 4)).astype(np.float64), stats


def copy_ia4(efb):
    """EFB copy to GX_TF_IA4: intensity 0.257R+0.504G+0.098B+16, alpha, 4 bits each
    (floor(v*255/16), as Dolphin's encoder), expanded to 8 bits as GX samples them."""
    y = efb[..., 0] * .257 + efb[..., 1] * .504 + efb[..., 2] * .098 + 16
    i4, a4 = np.floor(y / 16).astype(np.int32), np.floor(efb[..., 3] / 16).astype(np.int32)
    out = np.empty(efb.shape[:2] + (4,), np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = i4 * 17
    out[..., 3] = a4 * 17
    return out


def copy_rgb5a3(efb):
    """EFB copy to GX_TF_RGB5A3: RGB555 where alpha > 224, else RGB444 + A3."""
    opaque = efb[..., 3] > 224
    out = np.empty(efb.shape[:2] + (4,), np.uint8)
    five = np.floor(efb[..., :3] / 8).astype(np.int32)
    four = np.floor(efb[..., :3] / 16).astype(np.int32)
    three = np.floor(efb[..., 3] / 32).astype(np.int32)
    out[..., :3] = np.where(opaque[..., None], (five << 3) | (five >> 2), four * 17)
    out[..., 3] = np.where(opaque, 255, (three << 5) | (three << 2) | (three >> 1))
    return out


def cook_course(index, course, out, data, exporter):
    from import_courses import kmp_database
    folder = course_folder(course, data, out)
    kmp = kmp_database(folder / 'course.kmp')
    areas = [r for r in kmp.get('AREA', {}).get('records', []) if bytes.fromhex(r['raw_hex'])[1] == 5]
    if areas:
        raise ValueError(f"{course['name']}: KMP AREA type 5 (minimap control) is not supported")
    bones = node_translations(folder / 'map_model.brres')
    if 'posLD' not in bones or 'posRU' not in bones:
        raise ValueError(f"{course['name']}: map_model.brres has no posLD/posRU")
    work = out / 'exports' / course['key']
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run(['node', str(exporter), str(folder / 'map_model.brres'), str(work)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    model = json.loads((work / 'model.json').read_text())
    square = frame(bones['posLD'], bones['posRU'])
    efb, stats = bake(model, square)
    # Battle modes (gameMode 3, 9, 10: Map2DRenderer_init) copy RGB5A3; races IA4.
    encode = copy_rgb5a3 if course['arena'] else copy_ia4
    # Outside the capture: the clear colour, never sampled (the bottom screen
    # draws the capture's texels 1:1).
    texture = np.empty((TEXTURE, TEXTURE, 4), np.uint8)
    texture[...] = encode(np.array([[[255., 255., 255., 0.]]]))[0, 0]
    texture[:CAPTURE, :CAPTURE] = encode(efb)
    name = f'race_minimap_{index:02d}'
    png = out / 'textures' / (name + '.png')
    Image.fromarray(texture, 'RGBA').save(png)
    fmt = 'rgba5551' if course['arena'] else 'la4'
    subprocess.run([TEX3DS, '-f', fmt, '-z', 'none', '-o', str(png.with_suffix('.t3x')), str(png)], check=True, stdout=subprocess.DEVNULL)
    start = kmp['KTPT']['records'][0]
    return {'index': index, 'name': course['name'], 'archive': course['archive'], 'arena': course['arena'],
            'texture': f'textures/{name}.t3x', 'format': fmt, 'posLD': bones['posLD'], 'posRU': bones['posRU'],
            'square': list(square), 'start': [start['position'][0], start['position'][2], start['rotation'][1]],
            't3xBytes': png.with_suffix('.t3x').stat().st_size, 'stats': stats,
            'sources': {p.relative_to(p.parents[2]).as_posix(): digest(p) for p in [folder / 'map_model.brres', folder / 'course.kmp']}}


def resize(image, scale_or_size):
    if isinstance(scale_or_size, tuple):
        size = scale_or_size
    else:
        size = (max(1, round(image.width * scale_or_size)), max(1, round(image.height * scale_or_size)))
    # Premultiplied downscale keeps transparent borders from bleeding dark fringes.
    a = np.asarray(image.convert('RGBA'), dtype=np.float32) / 255
    a[..., :3] *= a[..., 3:]
    planes = [Image.fromarray(a[..., c]).resize(size, Image.Resampling.LANCZOS) for c in range(4)]
    b = np.stack([np.asarray(p) for p in planes], axis=-1).clip(0, 1)
    b[..., :3] = np.where(b[..., 3:] > 1e-4, b[..., :3] / np.maximum(b[..., 3:], 1e-4), 0)
    return Image.fromarray((b.clip(0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA')


def trim(image):
    box = image.getbbox()
    return image.crop(box) if box else image


def result_rows(height):
    """Compact leaderboard bars from result/position_12players.brlyt: the player's
    (1P colour) and everyone else's (c_off), cut to [end cap | body | fade]."""
    from menu_layout import render_layout
    rows = {}
    for key, anim in (('player', 'position_12players_1p_color.brlan'), ('other', 'position_12players_c_off.brlan')):
        image, _, _ = render_layout(RESULT / 'blyt/position_12players.brlyt', lambda p: None,
                                    animations=[RESULT / 'anim' / anim, RESULT / 'anim/position_12players_icon_wait_position.brlan'],
                                    frame=0, aspect_scale=(1.0, 1.0),
                                    exclude=('get_point', 'total_score', 'total_point', 'handle_text', 'time',
                                             'player_name', 'position', 'chara_icon_null', 'chara_icon', 'chara_icon_sha'))
        image = image.convert('RGBA')
        scale = height / image.height
        rows[key] = resize(image, scale)
    return rows


def compact_bar(bar, width, cap=4):
    """The left end of the full result row (where retail draws the position and
    the face), closed by its own left edge mirrored: no rescaling, so the
    board's soft checker pattern keeps its authored period."""
    out = Image.new('RGBA', (width, bar.height))
    out.paste(bar.crop((0, 0, width - cap, bar.height)), (0, 0))
    out.paste(bar.crop((0, 0, cap, bar.height)).transpose(Image.Transpose.FLIP_LEFT_RIGHT), (width - cap, 0))
    return out


def plated(gloss, silhouette, colour=(52, 62, 88)):
    """The c_off row is a translucent gloss whose left end leaves its lower half
    open (retail lays it over the scene): put it on a solid plate cut to the
    player bar's own silhouette, so every row reads as a full-height box."""
    body = np.zeros((gloss.height, gloss.width, 4), np.uint8)
    body[..., :3] = colour
    body[..., 3] = np.asarray(silhouette.convert('RGBA'))[..., 3]
    out = Image.fromarray(body, 'RGBA')
    out.alpha_composite(gloss)
    return out


def ordinal_images(height):
    """1st..12th (Race.bmg 0x515..) in the result board's kart_kanji_font:
    lerp(black, white, intensity), the board's material colours."""
    from cook_race_flow import font_glyphs
    from menu_layout import messages
    race = messages(UI / 'Race_U/message/Race.bmg')
    texts = [race[0x515 + i] for i in range(12)]
    font = UI / 'Font/kart_kanji_font.brfnt'
    info, glyphs = font_glyphs(font, ''.join(texts))
    out = []
    for text in texts:
        width = sum(glyphs[c]['advance'] for c in text) + 4
        canvas = Image.new('RGBA', (width, info['cellH']))
        x = 2
        for c in text:
            g = glyphs[c]
            a = np.asarray(g['image'].convert('RGBA'), dtype=np.float32)
            rgba = np.zeros_like(a)
            rgba[..., :3] = a[..., :1]  # intensity selects black (0) .. white (1)
            rgba[..., 3] = a[..., 3]
            canvas.alpha_composite(Image.fromarray(rgba.astype(np.uint8), 'RGBA'), (x + g['left'], 0))
            x += g['advance']
        out.append(resize(trim(canvas), height / info['cellH']))
    return texts, out


def pack(images, width, padding=1):
    """Shelf packing; returns (atlas, {key: (x, y, w, h)}) or None when it does not fit."""
    order = sorted(images, key=lambda k: (-images[k].height, -images[k].width, str(k)))
    x = y = row = 0
    rects = {}
    for key in order:
        w, h = images[key].width + 2 * padding, images[key].height + 2 * padding
        if x + w > width:
            x, y, row = 0, y + row, 0
        rects[key] = (x + padding, y + padding, images[key].width, images[key].height)
        x, row = x + w, max(row, h)
    height = max(8, 1 << (y + row - 1).bit_length())
    if height > 512:
        return None
    atlas = Image.new('RGBA', (width, height))
    for key, (px, py, _, _) in rects.items():
        im = images[key]
        atlas.paste(im, (px, py))
        # Edge texels repeat into the padding so bilinear sampling never reaches a neighbour.
        atlas.paste(im.crop((0, 0, im.width, 1)), (px, py - 1))
        atlas.paste(im.crop((0, im.height - 1, im.width, im.height)), (px, py + im.height))
        atlas.paste(atlas.crop((px, py - 1, px + 1, py + im.height + 1)), (px - 1, py - 1))
        atlas.paste(atlas.crop((px + im.width - 1, py - 1, px + im.width, py + im.height + 1)), (px + im.width, py - 1))
    return atlas, rects


def cook_atlas(out, manifest):
    from cook_race_items import decode_tpl, ICONS
    sources = []

    def tpl(path):
        sources.append(path)
        return decode_tpl(path)
    images = {}
    for i, name in enumerate(FACES):
        face = tpl(GAME / f'timg/st_{name}_32x32.tpl')
        # The map icons sample the retail 32x32 texels; the standings rows draw 17 px faces.
        images[('face', i)] = face.convert('RGBA')
        images[('rowface', i)] = resize(face, (28, 28))
    for kind, name in ICONS.items():
        images[('item', kind)] = resize(tpl(GAME / f'timg/{name}.tpl'), (20, 20))
    # Slot 19 (no ItemKind): two Triple Mushrooms left, as the item window shows them.
    images[('item', 19)] = resize(tpl(GAME / 'timg/tt_item_kinoko_2.tpl'), (20, 20))
    # CtrlRaceTime panes at TIMER_SCALE: 32-unit digits, TIME 240x40 at pane scale 0.7.
    digit = round(32 * TIMER_SCALE)
    for d in range(10):
        images[('digit', d)] = resize(tpl(GAME_U / f'timg/tt_d_number_3d_{d:02d}.tpl'), (digit, digit))
    for key, name in (('dot', 'tt_d_number_3d_coron'), ('colon', 'tt_d_number_3d_coron_00')):
        images[('sep', key)] = resize(tpl(GAME_U / f'timg/{name}.tpl'), (digit, digit))
    images[('label', 'time')] = resize(tpl(GAME_U / 'timg/tt_time_E.tpl'), (round(240 * .7 * TIMER_SCALE), round(40 * .7 * TIMER_SCALE)))
    # The light pane's colour is its material's (C0 = C1 = yellow): keep only the texture's alpha.
    light = np.asarray(tpl(GAME / 'timg/tt_map_chara_searchlight.tpl').convert('RGBA')).copy()
    light[..., :3] = 255
    images[('light', 0)] = Image.fromarray(light, 'RGBA')
    images[('start', 0)] = tpl(GAME / 'timg/tt_start_line_22.tpl').convert('RGBA')
    # Twelve standings rows fill the lower screen's 240 px: 19 px bars, 1 px apart.
    rows = {key: compact_bar(bar, 78) for key, bar in result_rows(19).items()}
    images[('row', 'player')] = rows['player']
    images[('row', 'other')] = plated(rows['other'], rows['player'])
    sources += [RESULT / 'blyt/position_12players.brlyt', RESULT / 'anim/position_12players_1p_color.brlan',
                RESULT / 'anim/position_12players_c_off.brlan', RESULT / 'anim/position_12players_icon_wait_position.brlan',
                RESULT / 'timg/tt_pattern_chek_bokashi_32x32.tpl', RESULT / 'timg/tt_pattern_chek_32x32_alpha.tpl']
    texts, ordinals = ordinal_images(16)
    for i, im in enumerate(ordinals):
        images[('ordinal', i)] = im
    sources += [UI / 'Race_U/message/Race.bmg', UI / 'Font/kart_kanji_font.brfnt']
    images[('white', 0)] = Image.new('RGBA', (4, 4), (255, 255, 255, 255))
    for width in (256, 512):
        packed = pack(images, width)
        if packed:
            break
    atlas, rects = packed
    png = out / 'textures/race_minimap_ui.png'
    atlas.save(png)
    subprocess.run([TEX3DS, '-f', 'rgba8', '-z', 'none', '-o', str(png.with_suffix('.t3x')), str(png)], check=True, stdout=subprocess.DEVNULL)
    manifest['atlas'] = {'texture': 'textures/race_minimap_ui.t3x', 'size': list(atlas.size),
                         't3xBytes': png.with_suffix('.t3x').stat().st_size, 'ordinals': texts,
                         'sources': {Path(p).relative_to(ROOT).as_posix(): digest(p) for p in sorted(set(sources))}}
    return atlas.size, rects


def timer_layout():
    """CtrlRaceTime (game_image_number.brlyt): pane centres relative to race_null,
    sizes and the TIME label's scale."""
    from menu_layout import pane_nodes
    path = GAME_U / 'blyt/game_image_number.brlyt'
    panes = {n['name']: n for n in pane_nodes(path)}
    names = ['race_null', 'time_01', 'coron01', 'time_02', 'time_03', 'coron00', 'time_04', 'time_05', 'time_06', 'set_p']
    out = []
    for name in names:
        v = panes[name]['values']
        x, y = (0.0, 0.0) if name == 'race_null' else (v[0], v[1])
        out.append((name, x, y, v[8] * v[6], v[9] * v[7]))
    return out, path


def write_header(manifest, atlas_size, rects, timer):
    def rect(key):
        x, y, w, h = rects[key]
        return f'{{{x},{y},{w},{h}}}'
    lines = ['// Generated by tools/cook_race_map.py: retail Mario Kart Wii\'s minimap and the race',
             '// bottom screen\'s UI atlas (src/race_bottom_screen.cpp).', '#pragma once', '#include <array>',
             '#include <cstdint>', 'namespace mkw {',
             '// Map2DRenderer\'s capture (Map2DRenderer_init PAL 0x807DFBA4: 220x220 texels) at the',
             '// top-left of a 256x256 texture: LA4 for races (GX IA4), RGBA5551 for arenas',
             '// (GX RGB5A3, battle modes).',
             f'inline constexpr unsigned RaceMapCapture={CAPTURE},RaceMapTextureSize={TEXTURE};',
             '// One course\'s capture and the world square it covers (Map2DRenderer_render',
             '// 0x807E0028): u = (x-x0)/(x1-x0), v = (z-z0)/(z1-z0) (fn_1_2D0530 0x807E08E4), z0',
             '// the posLD side (the bottom); and its start line, KTPT 0 (CtrlRace2DMap_load',
             '// 0x807EA27C): position XZ and yaw (degrees).',
             'struct RaceMapCourse {const char* texture;float x0,z0,x1,z1;float startX,startZ,startYaw;};',
             f'inline constexpr std::array<RaceMapCourse,{len(manifest["courses"])}> RaceMapCourses{{{{']
    for c in manifest['courses']:
        x0, z0, x1, z1 = c['square']
        sx, sz, yaw = c['start']
        lines.append(f'    {{"{c["texture"]}",{x0:.3f}f,{z0:.3f}f,{x1:.3f}f,{z1:.3f}f,{sx:.3f}f,{sz:.3f}f,{yaw:.3f}f}}, // {c["name"]}')
    lines += ['}};',
              '// The bottom-screen UI atlas (RGBA8, VRAM): pixel rectangles x,y,w,h.',
              'struct RaceMapRect {uint16_t x,y,w,h;};',
              f'inline constexpr const char* RaceMapAtlasTexture="{manifest["atlas"]["texture"]}";',
              f'inline constexpr uint16_t RaceMapAtlasSize[2]={{{atlas_size[0]},{atlas_size[1]}}};',
              '// st_*_32x32 by retail character id (Kinoko Character 0..23): the map icons\' 32x32',
              '// texels, and 28x28 copies for the standings rows.',
              'inline constexpr std::array<RaceMapRect,24> RaceMapFaces{{' + ','.join(rect(('face', i)) for i in range(24)) + '}};',
              'inline constexpr std::array<RaceMapRect,24> RaceMapRowFaces{{' + ','.join(rect(('rowface', i)) for i in range(24)) + '}};',
              '// Item window icons (tools/cook_race_items.py ICONS) by ItemKind; 19 = two Triple Mushrooms left (kinoko_2).',
              'inline constexpr std::array<RaceMapRect,21> RaceMapItems{{' +
              ','.join(rect(('item', k)) if ('item', k) in rects else '{0,0,0,0}' for k in range(21)) + '}};',
              '// CtrlRaceTime\'s art at RaceTimerScale: one atlas texel per bottom-screen pixel.',
              'inline constexpr std::array<RaceMapRect,10> RaceMapDigits{{' + ','.join(rect(('digit', d)) for d in range(10)) + '}};',
              f'inline constexpr RaceMapRect RaceMapColon{rect(("sep", "colon"))},RaceMapDot{rect(("sep", "dot"))};',
              f'inline constexpr RaceMapRect RaceMapTimeLabel{rect(("label", "time"))};',
              '// tt_map_chara_searchlight (60x64, its alpha; the light pane\'s material is yellow)',
              '// and tt_start_line_22 (22x4), retail texels.',
              f'inline constexpr RaceMapRect RaceMapSearchlight{rect(("light", 0))};',
              f'inline constexpr RaceMapRect RaceMapStartLine{rect(("start", 0))};',
              f'inline constexpr RaceMapRect RaceMapWhite{rect(("white", 0))};',
              f'inline constexpr RaceMapRect RaceMapRowPlayer{rect(("row", "player"))},RaceMapRowOther{rect(("row", "other"))};',
              '// 1st..12th (Race.bmg 0x515..0x520), result board font.',
              'inline constexpr std::array<RaceMapRect,12> RaceMapOrdinals{{' + ','.join(rect(('ordinal', i)) for i in range(12)) + '}};',
              '// CtrlRaceTime (Race_U game_image_number.brlyt): pane centre (layout units,',
              '// y up) relative to race_null, and drawn size. Digits: race_null and time_01 the',
              '// minutes, time_02..03 the seconds, time_04..06 the milliseconds (CtrlRaceTime_refresh',
              '// PAL 0x807F84C0, time_number.brctr groups Group_00..06); coron01 = coron_00',
              '// (between minutes and seconds), coron00 = coron. Drawn at RaceTimerScale.',
              f'inline constexpr float RaceTimerScale={TIMER_SCALE}f;',
              'struct RaceTimerPane {float x,y,w,h;};',
              f'inline constexpr std::array<RaceTimerPane,{len(timer)}> RaceTimerPanes{{{{' +
              ','.join(f'{{{x:.3f}f,{y:.3f}f,{w:.3f}f,{h:.3f}f}}' for _, x, y, w, h in timer) + '}};',
              '}', '']
    (ROOT / 'include/mkw/race_map_assets.hpp').write_text('\n'.join(lines))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=ROOT / 'build/race-map')
    p.add_argument('--exporter', type=Path, default=ROOT / 'build/export_brres.mjs',
                   help='the bundled tools/export_brres.ts (node tools/bundle_cooker.mjs)')
    p.add_argument('--data', type=Path, default=ROOT / 'data/extracted',
                   help='extracted disc files, for courses without an assets/courses folder')
    p.add_argument('--jobs', type=int, default=6)
    a = p.parse_args()
    (a.output / 'textures').mkdir(parents=True, exist_ok=True)
    catalog = courses()
    manifest = {'version': 2, 'courses': []}
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.jobs) as pool:
        jobs = [pool.submit(cook_course, i, c, a.output, a.data, a.exporter) for i, c in enumerate(catalog)]
        for job in jobs:
            r = job.result()
            manifest['courses'].append(r)
            s = r['stats']
            print(f"{r['index']:2d} {r['name']:24s} {r['format']:8s} square {r['square'][2] - r['square'][0]:8.0f} "
                  f"tris {s['triangles']} drawn {s['drawn']} culled {s['culled']}", flush=True)
    atlas_size, rects = cook_atlas(a.output, manifest)
    timer, timer_source = timer_layout()
    manifest['timer'] = {'source': timer_source.relative_to(ROOT).as_posix(), 'sha256': digest(timer_source), 'scale': TIMER_SCALE, 'panes': timer}
    (a.output / 'race-map.json').write_text(json.dumps(manifest, indent=1) + '\n')
    write_header(manifest, atlas_size, rects, timer)
    print(json.dumps({'maps': len(manifest['courses']), 'atlas': atlas_size, 'atlasBytes': manifest['atlas']['t3xBytes'],
                      'mapBytes': sum(c['t3xBytes'] for c in manifest['courses'])}))


if __name__ == '__main__':
    main()
