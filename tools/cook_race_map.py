#!/usr/bin/env python3
"""Cook the race bottom screen (docs/BOTTOM_SCREEN.md): 32 course maps and one UI atlas.

Course maps: tools/race_map_bake.py renders each race course top-down from its
cooked GMDL parts and textures (build/course-materials first), removing roofs
over the road with the course KCL, then tex3ds encodes it as ETC1 (at most
1024x1024, 512 KB). One map is resident at a time, in VRAM.

UI atlas (RGBA8): retail Wii art for the map icons (st_*_32x32 faces, the
local player's searchlight), the leaderboard (result-board rows, 1st..12th in
the result font, faces, item icons) and the timer (CtrlRaceTime's digits,
separators and TIME label), plus Mario Kart 7's bottom-screen map markers
for items on the course, read from the user's own MK7 dump (tools/mk7_assets.py).

Outputs: build/race-map/textures/*.t3x (a packaging overlay), build/race-map/
race-map.json (sources, hashes, per-course statistics) and the generated
include/mkw/race_map_assets.hpp. The shared sd-card is never written.
"""
import argparse
import concurrent.futures
import hashlib
import json
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
# MK7 /UI/race.szs map markers used for MKW's items on the course.
MK7_MARKERS = ['map_banana_r90.bclim', 'map_bomb_hei_r90.bclim', 'map_kame_g_r90.bclim', 'map_kame_r_r90.bclim',
               'map_kame_wing_r90.bclim', 'map_exp_r_r90.bclim', 'map_exp_b_r90.bclim', 'map_start.bclim']
# The release ships these eight markers as lossless PNGs (exported with
# --export-mk7-markers), so a build without a Mario Kart 7 dump packs the
# same atlas.
MK7_SHIPPED = ROOT / 'tools/release/mk7_markers'


def marker_key(name):
    return name.split('.')[0].replace('_r90', '')


def mk7_markers(cci):
    """{bclim name: un-rotated RGBA array}, from the MK7 dump when present."""
    if cci and Path(cci).is_file():
        from mk7_assets import race_textures
        return race_textures(cci, MK7_MARKERS)
    return {n: np.asarray(Image.open(MK7_SHIPPED / (marker_key(n) + '.png')).convert('RGBA')) for n in MK7_MARKERS}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def courses():
    text = (ROOT / 'include/mkw/course_catalog.hpp').read_text()
    rows = re.findall(r'\{"([^"]+)",Kinoko::Course::(\w+),"(Race/Course/([^"]+)\.szs)","models/([^"]+)\.list"', text)
    return [dict(name=n, id=i, archive=a, key=k, models=m) for n, i, a, k, m in rows]


def route_length(kmp):
    """Lap length along the checkpoint midpoints, in KMP order."""
    mids = [((r['left'][0] + r['right'][0]) / 2, (r['left'][1] + r['right'][1]) / 2) for r in kmp['CKPT']['records']]
    return sum(np.hypot(mids[i][0] - mids[i - 1][0], mids[i][1] - mids[i - 1][1]) for i in range(len(mids)))


# The detailed view's scale, world units per bottom-screen pixel. Mario Kart 7's
# map shows its roads 45-70 px wide (docs/BOTTOM_SCREEN.md); Wii roads are about
# 2,700-4,500 units wide and karts are the same size on every course, so 64 serves
# every map whose texels allow it. Long courses widen the view instead of
# magnifying their texture more than about 1.2x.
UNITS_PER_PIXEL = 64.0
MAX_UNITS_PER_PIXEL = 110.0
OVERVIEW = 256


def cook_course(index, course, out, reuse_rendered=False):
    import race_map_bake as rb
    from import_courses import kmp_database
    folder = ROOT / 'assets/courses' / course['key']
    kcl = rb.kcl_triangles(folder / 'course.kcl')
    kmp = kmp_database(folder / 'course.kmp')
    frame, size = rb.course_frame(kmp)
    png = out / 'textures' / f'race_map_{index:02d}.png'
    small = (OVERVIEW, OVERVIEW * size[1] // size[0]) if size[0] >= size[1] else (OVERVIEW * size[0] // size[1], OVERVIEW)
    overview = out / 'textures' / f'race_map_{index:02d}_overview.png'
    if reuse_rendered:
        for image_path,expected in [(png,size),(overview,small)]:
            if Image.open(image_path).size!=tuple(expected) or image_path.with_suffix('.t3x').stat().st_size==0:
                raise ValueError('Incomplete rendered map: '+str(image_path))
        stats={'explicit_reuse_rendered':True,'previous_render_statistics_not_retained':True}
    else:
        image, stats = rb.bake(course['key'], course['models'], course['archive'], frame, size)
        image.save(png)
        subprocess.run([TEX3DS, '-f', 'etc1', '-q', 'high', '-z', 'none', '-o', str(png.with_suffix('.t3x')), str(png)],check=True,stdout=subprocess.DEVNULL)
        image.resize(small, Image.Resampling.LANCZOS).save(overview)
        subprocess.run([TEX3DS, '-f', 'etc1', '-q', 'high', '-z', 'none', '-o', str(overview.with_suffix('.t3x')), str(overview)],check=True,stdout=subprocess.DEVNULL)
    texel = (frame[2] - frame[0]) / size[0]
    view = min(max(UNITS_PER_PIXEL, 0.85 * texel), MAX_UNITS_PER_PIXEL)
    width = rb.road_width(kcl, route_length(kmp))
    # The start line: KMP checkpoint 0, cut to the road it crosses.
    start = kmp['CKPT']['records'][0]
    start_left, start_right = rb.road_span(kcl, start['left'], start['right'], kmp['KTPT']['records'][0]['position'])
    parts = (rb.SD / f"models/{course['models']}.list").read_text().split()
    return {'index': index, 'name': course['name'], 'archive': course['archive'],
            'texture': f'textures/race_map_{index:02d}.t3x', 'frame': list(frame), 'size': list(size),
            'roadWidth': width, 'unitsPerPixel': view, 'overview': f'textures/race_map_{index:02d}_overview.t3x', 'overviewSize': list(small),
            'startLeft': [float(v) for v in start_left], 'startRight': [float(v) for v in start_right],
            'startCheckpoint': [list(start['left']), list(start['right'])],
            't3xBytes': png.with_suffix('.t3x').stat().st_size + overview.with_suffix('.t3x').stat().st_size, 'stats': stats,
            'sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in
                        [folder / 'course.kcl', folder / 'course.kmp'] +
                        [rb.course_path(f'models/{n}.gmdl') for n in parts]}}


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


def cook_atlas(out, cci, manifest):
    from cook_race_items import decode_tpl, ICONS
    sources = []

    def tpl(path):
        sources.append(path)
        return decode_tpl(path)
    images = {}
    for i, name in enumerate(FACES):
        images[('face', i)] = resize(tpl(GAME / f'timg/st_{name}_32x32.tpl'), (28, 28))
        silhouette = Image.new('RGBA', (28,28), (255,255,255,255))
        silhouette.putalpha(images[('face', i)].getchannel('A'))
        images[('face_shadow', i)] = silhouette
    # Item icons appear only in the standings rows (17 px).
    for kind, name in ICONS.items():
        images[('item', kind)] = resize(tpl(GAME / f'timg/{name}.tpl'), (20, 20))
    # Slot 19 (no ItemKind): two Triple Mushrooms left, as the item window shows them.
    images[('item', 19)] = resize(tpl(GAME / 'timg/tt_item_kinoko_2.tpl'), (20, 20))
    images[('fake', 0)] = resize(tpl(GAME / 'timg/tt_item_dummybox.tpl'), (16, 16))
    for d in range(10):
        images[('digit', d)] = resize(tpl(GAME_U / f'timg/tt_d_number_3d_{d:02d}.tpl'), (24, 24))
    for key, name in (('dot', 'tt_d_number_3d_coron'), ('colon', 'tt_d_number_3d_coron_00')):
        images[('sep', key)] = resize(tpl(GAME_U / f'timg/{name}.tpl'), (24, 24))
    images[('label', 'time')] = resize(tpl(GAME_U / 'timg/tt_time_E.tpl'), (120, 20))
    images[('light', 0)] = resize(tpl(GAME / 'timg/tt_map_chara_searchlight.tpl'), (30, 32))
    for name, array in mk7_markers(cci).items():
        images[('mk7', marker_key(name))] = Image.fromarray(array, 'RGBA')
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
    png = out / 'textures/race_bottom_ui.png'
    atlas.save(png)
    subprocess.run([TEX3DS, '-f', 'rgba8', '-z', 'none', '-o', str(png.with_suffix('.t3x')), str(png)], check=True, stdout=subprocess.DEVNULL)
    manifest['atlas'] = {'texture': 'textures/race_bottom_ui.t3x', 'size': list(atlas.size),
                         't3xBytes': png.with_suffix('.t3x').stat().st_size, 'ordinals': texts,
                         'mk7Source': {'cci' if cci and Path(cci).is_file() else 'shipped': str(cci) if cci and Path(cci).is_file() else str(MK7_SHIPPED), 'archive': '/UI/race.szs', 'markers': MK7_MARKERS},
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


def write_header(manifest, atlas_size, rects, timer, timer_source):
    def rect(key):
        x, y, w, h = rects[key]
        return f'{{{x},{y},{w},{h}}}'
    lines = ['// Generated by tools/cook_race_map.py; see docs/BOTTOM_SCREEN.md.', '#pragma once', '#include <array>',
             '#include <cstdint>', 'namespace mkw {',
             '// One course map: an ETC1 texture covering the world rectangle [x0,x1] x [z0,z1]',
             '// (+x right, -z up: the retail minimap orientation), the detailed view scale',
             '// (world units per bottom-screen pixel) and the start line (CKPT 0 cut to its road, XZ).',
             'struct RaceMapCourse {const char* texture;const char* overview;float x0,z0,x1,z1;uint16_t width,height,overviewWidth,overviewHeight;float unitsPerPixel;float startLeft[2],startRight[2];};',
             f'inline constexpr std::array<RaceMapCourse,{len(manifest["courses"])}> RaceMapCourses{{{{']
    for c in manifest['courses']:
        f = c['frame']
        lines.append(f'    {{"{c["texture"]}","{c["overview"]}",{f[0]:.3f}f,{f[1]:.3f}f,{f[2]:.3f}f,{f[3]:.3f}f,{c["size"][0]},{c["size"][1]},'
                     f'{c["overviewSize"][0]},{c["overviewSize"][1]},'
                     f'{c["unitsPerPixel"]:.4f}f,{{{c["startLeft"][0]:.3f}f,{c["startLeft"][1]:.3f}f}},'
                     f'{{{c["startRight"][0]:.3f}f,{c["startRight"][1]:.3f}f}}}}, // {c["name"]}')
    lines += ['}};',
              '// The bottom-screen UI atlas (RGBA8, VRAM): pixel rectangles x,y,w,h.',
              'struct RaceMapRect {uint16_t x,y,w,h;};',
              f'inline constexpr const char* RaceMapAtlasTexture="{manifest["atlas"]["texture"]}";',
              f'inline constexpr uint16_t RaceMapAtlasSize[2]={{{atlas_size[0]},{atlas_size[1]}}};',
              '// st_*_32x32 by retail character id (Kinoko Character 0..23).',
              'inline constexpr std::array<RaceMapRect,24> RaceMapFaces{{' + ','.join(rect(('face', i)) for i in range(24)) + '}};',
              'inline constexpr std::array<RaceMapRect,24> RaceMapFaceShadows{{' + ','.join(rect(('face_shadow', i)) for i in range(24)) + '}};',
              '// Item window icons (tools/cook_race_items.py ICONS) by ItemKind; 19 = two Triple Mushrooms left (kinoko_2).',
              'inline constexpr std::array<RaceMapRect,21> RaceMapItems{{' +
              ','.join(rect(('item', k)) if ('item', k) in rects else '{0,0,0,0}' for k in range(21)) + '}};',
              'inline constexpr std::array<RaceMapRect,10> RaceMapDigits{{' + ','.join(rect(('digit', d)) for d in range(10)) + '}};',
              f'inline constexpr RaceMapRect RaceMapColon{rect(("sep", "colon"))},RaceMapDot{rect(("sep", "dot"))};',
              f'inline constexpr RaceMapRect RaceMapTimeLabel{rect(("label", "time"))};',
              f'inline constexpr RaceMapRect RaceMapSearchlight{rect(("light", 0))};',
              f'inline constexpr RaceMapRect RaceMapFakeBox{rect(("fake", 0))};',
              f'inline constexpr RaceMapRect RaceMapWhite{rect(("white", 0))};',
              f'inline constexpr RaceMapRect RaceMapRowPlayer{rect(("row", "player"))},RaceMapRowOther{rect(("row", "other"))};',
              '// 1st..12th (Race.bmg 0x515..0x520), result board font.',
              'inline constexpr std::array<RaceMapRect,12> RaceMapOrdinals{{' + ','.join(rect(('ordinal', i)) for i in range(12)) + '}};',
              '// Mario Kart 7 bottom-screen map markers (/UI/race.szs).',
              'enum class Mk7Marker : uint8_t {Banana,Bobomb,GreenShell,RedShell,SpinyShell,BlastRed,BlastBlue,StartLine,Count};',
              'inline constexpr std::array<RaceMapRect,8> RaceMapMk7{{' +
              ','.join(rect(('mk7', n.split('.')[0].replace('_r90', ''))) for n in MK7_MARKERS) + '}};',
              '// CtrlRaceTime (Race_U game_image_number.brlyt): pane centre (layout units,',
              '// y up) relative to race_null, and drawn size. Digits: race_null and time_01 the',
              '// minutes, time_02..03 the seconds, time_04..06 the milliseconds (CtrlRaceTime_refresh',
              '// USA 0x807ED9C0); coron01 = coron_00 (between minutes and seconds), coron00 = coron.',
              'struct RaceTimerPane {float x,y,w,h;};',
              f'inline constexpr std::array<RaceTimerPane,{len(timer)}> RaceTimerPanes{{{{' +
              ','.join(f'{{{x:.3f}f,{y:.3f}f,{w:.3f}f,{h:.3f}f}}' for _, x, y, w, h in timer) + '}};',
              '}', '']
    (ROOT / 'include/mkw/race_map_assets.hpp').write_text('\n'.join(lines))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=ROOT / 'build/race-map')
    p.add_argument('--mk7-cci', type=Path, help='Optional owner-supplied MK7 dump; defaults to retained marker PNGs')
    p.add_argument('--mk7-markers',type=Path,help='External directory of the eight original exported MK7 markers')
    p.add_argument('--jobs', type=int, default=6)
    p.add_argument('--reuse-rendered-maps',action='store_true',help='Explicitly reuse PNG/T3X maps from an interrupted local cook')
    p.add_argument('--metadata-only',action='store_true',help='Save map records before external UI inputs are provided')
    p.add_argument('--atlas-only',action='store_true',help='Finish an existing complete map manifest without rendering maps again')
    p.add_argument('--courses', type=str, default='', help='comma-separated catalog indices (default all)')
    p.add_argument('--export-mk7-markers', action='store_true', help='write the eight markers from the MK7 dump to tools/release/mk7_markers and exit')
    a = p.parse_args()
    global MK7_SHIPPED
    if a.mk7_markers:MK7_SHIPPED=a.mk7_markers.resolve()
    if not a.export_mk7_markers and not a.metadata_only and not (a.mk7_cci and a.mk7_cci.is_file()) and not all((MK7_SHIPPED/(marker_key(n)+'.png')).is_file() for n in MK7_MARKERS):
        p.error('Supply --mk7-cci or --mk7-markers; these eight original markers cannot be generated from a Wii disc.')
    if a.export_mk7_markers:
        from mk7_assets import race_textures
        MK7_SHIPPED.mkdir(parents=True, exist_ok=True)
        for name, array in race_textures(a.mk7_cci, MK7_MARKERS).items():
            Image.fromarray(array, 'RGBA').save(MK7_SHIPPED / (marker_key(name) + '.png'))
        return
    (a.output / 'textures').mkdir(parents=True, exist_ok=True)
    catalog = courses()
    # Battle arenas are appended to the runtime course catalog, but have no
    # lap checkpoints and are not part of the 32-course race-map atlas.
    if len(catalog) < 32:
        raise SystemExit(f'Expected at least 32 race courses, found {len(catalog)}')
    catalog = catalog[:32]
    wanted = [] if a.atlas_only else [int(i) for i in a.courses.split(',')] if a.courses else list(range(len(catalog)))
    manifest_path = a.output / 'race-map.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {'version': 1, 'courses': []}
    previous = {c['index']: c for c in manifest.get('courses', [])}
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.jobs) as pool:
        jobs = {pool.submit(cook_course, i, catalog[i], a.output,a.reuse_rendered_maps): i for i in wanted}
        for job in concurrent.futures.as_completed(jobs):
            result = job.result()
            previous[result['index']] = result
            manifest['courses']=[previous[i] for i in sorted(previous)]
            manifest_path.write_text(json.dumps(manifest,indent=1)+'\n')
            print(f"{result['index']:2d} {result['name']:24s} {result['size'][0]}x{result['size'][1]} "
                  f"{result['unitsPerPixel']:.1f} u/px road {result['roadWidth']:.0f}", flush=True)
    manifest['courses'] = [previous[i] for i in sorted(previous)]
    if len(manifest['courses']) != len(catalog):
        raise SystemExit('Cook every course before writing the header (missing maps)')
    if a.metadata_only:return
    atlas_size, rects = cook_atlas(a.output, a.mk7_cci, manifest)
    timer, timer_source = timer_layout()
    manifest['timer'] = {'source': timer_source.relative_to(ROOT).as_posix(), 'sha256': digest(timer_source), 'panes': timer}
    manifest_path.write_text(json.dumps(manifest, indent=1) + '\n')
    write_header(manifest, atlas_size, rects, timer, timer_source)
    print(json.dumps({'maps': len(manifest['courses']), 'atlas': atlas_size,
                      'mapBytes': sum(c['t3xBytes'] for c in manifest['courses'])}))


if __name__ == '__main__':
    main()
