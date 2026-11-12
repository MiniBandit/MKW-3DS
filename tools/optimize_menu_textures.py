#!/usr/bin/env python3
"""Fit cooked menu UI textures to the 3DS top screen, then LZ11-compress them.

The Wii layouts are authored on a 608x456 canvas; the 3DS shows them at
400x240, so cooked RGBA8 sprites are sampled down ~0.66x0.53 by bilinear
filtering without mipmaps. This pass resamples each sprite once (premultiplied
alpha, Lanczos) to the largest size the production composer ever draws it at
(measured by tools/menu_sprite_scales.cpp over every page, cursor, selection,
age, transition and OK-glow pulse), never upscaling. Animated border atlases
are re-packed strip by strip with fresh one-texel edge gutters.

Inputs (never modified): the cook's full-resolution PNGs and models/menu-ui.json.
Outputs: textures/ui_*.t3x (LZ11), and the u/v (sprites) and atlas/slice UVs in
include/mkw/menu_assets.hpp. Re-running is idempotent; cook_menu_ui.py runs it
after regenerating menu_assets.hpp.
"""
import argparse, hashlib, json, math, re, subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
from PIL import Image
from devkitpro import tool

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'sd-card/3ds/mkw-3ds'
HEADER = ROOT / 'include/mkw/menu_assets.hpp'
TEX3DS = str(tool('tex3ds'))
WORK = ROOT / 'build/menu-textures'


def pow2(n):
    return max(8, 1 << (max(1, n) - 1).bit_length())


def measure():
    out = ROOT / 'build/host-tools/sprite-scales.json'
    if not out.is_file():
        raise FileNotFoundError('Compile and run tools/menu_sprite_scales.cpp manually to prepare '+str(out))
    return json.loads(out.read_text())


def resample(image, size):
    if image.size == size:
        return image
    # Premultiplied alpha keeps transparent texels from darkening edges.
    return image.convert('RGBa').resize(size, Image.Resampling.LANCZOS).convert('RGBA')


def encode(image, name):
    png = WORK / (name + '.png')
    image.save(png)
    target = ASSETS / 'textures' / (name + '.t3x')
    subprocess.run([TEX3DS, '-f', 'rgba8', '-z', 'lz11', '-o', str(target), str(png)], check=True,
                   stdout=subprocess.DEVNULL)
    return target.stat().st_size


def fit_sprite(key, record, screen):
    source = ASSETS / record['path'].replace('.t3x', '.png')
    width, height = record.get('rasterWidth', record['width']), record.get('rasterHeight', record['height'])
    with Image.open(source) as padded:
        content = padded.convert('RGBA').crop((0, 0, width, height))
    target = (width, height)
    if screen:
        target = (min(width, max(1, math.ceil(screen[0]))), min(height, max(1, math.ceil(screen[1]))))
    content = resample(content, target)
    canvas = Image.new('RGBA', (pow2(target[0]), pow2(target[1])))
    canvas.paste(content, (0, 0))
    size = encode(canvas, record['name'])
    return {'u': target[0] / canvas.width, 'v': target[1] / canvas.height, 'texture': list(canvas.size),
            'content': list(target), 'bytes': canvas.width * canvas.height * 4, 'file': size,
            'sourceBytes': record['padded'][0] * record['padded'][1] * 4}


def fit_atlas(key, record, slices, screen):
    source = ASSETS / record['path'].replace('.t3x', '.png')
    with Image.open(source) as image:
        atlas = np.asarray(image.convert('RGBA'))
    height, width = atlas.shape[:2]
    sx = min(1.0, screen[0] / width) if screen else 1.0
    sy = min(1.0, screen[1] / height) if screen else 1.0
    strips = []
    for index, s in slices:
        u0, v0, u1, v1 = s['uv']
        x0, y0, x1, y1 = round(u0 * width), round(v0 * height), round(u1 * width), round(v1 * height)
        crop = Image.fromarray(atlas[y0:y1, x0:x1])
        size = (max(1, math.ceil((x1 - x0) * sx)), max(1, math.ceil((y1 - y0) * sy)))
        # One texel of edge replication protects bilinear filtering at strip edges.
        strip = np.pad(np.asarray(resample(crop, size)), ((1, 1), (1, 1), (0, 0)), mode='edge')
        strips.append((index, strip))
    # Shelf packing, tallest first, as the cook does; strips keep their atlas.
    order = sorted(range(len(strips)), key=lambda i: (-strips[i][1].shape[0], -strips[i][1].shape[1]))
    best = None
    for limit in (128, 256, 512, 1024):
        x = y = row = used = 0
        places = {}
        ok = True
        for i in order:
            h, w = strips[i][1].shape[:2]
            if w > limit:
                ok = False
                break
            if x + w > limit:
                x, y, row = 0, y + row, 0
            places[i] = (x, y)
            x += w
            used = max(used, x)
            row = max(row, h)
        if not ok:
            continue
        size = (pow2(used), pow2(y + row))
        if size[1] > 1024:
            continue
        if best is None or size[0] * size[1] < best[0][0] * best[0][1]:
            best = (size, places)
    if best is None:
        raise ValueError('Border strips no longer fit one atlas: ' + key)
    (aw, ah), places = best
    canvas = np.zeros((ah, aw, 4), dtype=np.uint8)
    uvs = {}
    for i, (index, strip) in enumerate(strips):
        x, y = places[i]
        h, w = strip.shape[:2]
        canvas[y:y + h, x:x + w] = strip
        uvs[index] = [(x + 1) / aw, (y + 1) / ah, (x + w - 1) / aw, (y + h - 1) / ah]
    size = encode(Image.fromarray(canvas), record['name'])
    return {'texture': [aw, ah], 'bytes': aw * ah * 4, 'file': size, 'scale': [sx, sy], 'uvs': uvs,
            'sourceBytes': width * height * 4}


def rewrite_header(sprites, atlases, slice_uvs):
    text = HEADER.read_text()
    head, rest = text.split('inline constexpr UiSpriteInfo UiSprites[]={\n', 1)
    body, tail = rest.split('\n};', 1)
    lines = body.split('\n')
    pattern = re.compile(r'^\{"([^"]+)","(textures/[^"]+)",([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,(.*)$')
    for n, line in enumerate(lines):
        # Variable-name glyphs are independently cooked at native font size.
        if line in ("#include <mkw/menu_name_glyphs.hpp>","#include <mkw/battle_menu_text.inc>","#include <mkw/menu_record_text.inc>","#include <mkw/menu_footer_text.inc>"):
            continue
        m = pattern.match(line)
        if not m:
            raise ValueError('Unexpected UiSprites row: ' + line[:80])
        key = m.group(1)
        trailing = m.group(7)
        if key in sprites or key in atlases:
            # Metadata follows the actual allocation, including power-of-two
            # padding; compressed file size is not GPU residency.
            r = sprites.get(key) or atlases[key]
            trailing = re.sub(r',\d+\},$', ',%d},' % r['bytes'], trailing)
        if key in sprites:
            r = sprites[key]
            lines[n] = '{"%s","%s",%sf,%sf,%.8ff,%.8ff,%s' % (key, m.group(2), m.group(3), m.group(4), r['u'], r['v'], trailing)
        elif key in atlases:
            w, h = atlases[key]['texture']
            lines[n] = '{"%s","%s",%.8ff,%.8ff,%sf,%sf,%s' % (key, m.group(2), w, h, m.group(5), m.group(6), trailing)
    text = head + 'inline constexpr UiSpriteInfo UiSprites[]={\n' + '\n'.join(lines) + '\n};' + tail
    head, rest = text.split('inline constexpr UiBorderSlice UiBorderSlices[]={\n', 1)
    body, tail = rest.split('\n};', 1)
    lines = body.split('\n')
    slice_pattern = re.compile(r'^\{(\d+),"([^"]+)",([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,([-\d.]+)f,([-\d.]+)f\},$')
    for n, line in enumerate(lines):
        m = slice_pattern.match(line)
        if not m:
            raise ValueError('Unexpected UiBorderSlices row: ' + line[:80])
        if n in slice_uvs:
            u0, v0, u1, v1 = slice_uvs[n]
            lines[n] = '{%s,"%s",%sf,%sf,%sf,%sf,%.8ff,%.8ff,%.8ff,%.8ff},' % (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(6), u0, v0, u1, v1)
    HEADER.write_text(head + 'inline constexpr UiBorderSlice UiBorderSlices[]={\n' + '\n'.join(lines) + '\n};' + tail)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jobs', type=int, default=8)
    a = p.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ASSETS / 'models/menu-ui.json').read_text())
    records = manifest['sprites']
    slices = manifest['borderSlices']
    atlas_keys = sorted({s['key'] for s in slices})
    # Measurement must use the original PNG atlas coordinates. The header
    # may already contain a fitted atlas from a previous pass, whose UV
    # footprint would otherwise be mistaken for the original atlas's size.
    raw_sprites = {k: {'u': r['u'], 'v': r['v'], 'bytes': r['padded'][0]*r['padded'][1]*4}
                   for k, r in records.items() if k not in atlas_keys}
    raw_atlases = {k: {'texture': records[k]['padded'], 'bytes': records[k]['padded'][0]*records[k]['padded'][1]*4}
                   for k in atlas_keys}
    rewrite_header(raw_sprites, raw_atlases, {i: s['uv'] for i, s in enumerate(slices)})
    screen = measure()
    by_atlas = {key: [] for key in atlas_keys}
    for index, s in enumerate(slices):
        by_atlas[s['key']].append((index, s))
    used = [k for k in screen if k in records and k not in by_atlas]
    # Identical style variants can share one texture path. Fit the shared
    # image once at the largest required size and propagate its UVs to every
    # alias, including aliases selected only by animated font materials.
    aliases = {}
    for key, record in records.items():
        if key not in by_atlas:
            aliases.setdefault(record['path'], []).append(key)
    shared = {}
    for keys in aliases.values():
        requested = [k for k in keys if k in used]
        if not requested:
            continue
        leader = requested[0]
        shared[leader] = keys
        screen[leader] = [max(screen[k][axis] for k in requested) for axis in range(2)]
    used = list(shared)
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        sprite_results = dict(zip(used, pool.map(lambda k: fit_sprite(k, records[k], screen[k]), used)))
        atlas_results = dict(zip(atlas_keys, pool.map(lambda k: fit_atlas(k, records[k], by_atlas[k], screen.get(k)), atlas_keys)))
    for leader, keys in shared.items():
        for key in keys:
            sprite_results[key] = sprite_results[leader]
    slice_uvs = {}
    for result in atlas_results.values():
        slice_uvs.update(result.pop('uvs'))
    rewrite_header(sprite_results, atlas_results, slice_uvs)
    before = sum(r['sourceBytes'] for r in [*sprite_results.values(), *atlas_results.values()])
    after = sum(r['bytes'] for r in [*sprite_results.values(), *atlas_results.values()])
    files = sum(r['file'] for r in [*sprite_results.values(), *atlas_results.values()])
    receipt = {'version': 1, 'sprites': sprite_results, 'atlases': atlas_results,
               'gpuBytesBefore': before, 'gpuBytesAfter': after, 'fileBytes': files,
               'header_sha256': hashlib.sha256(HEADER.read_bytes()).hexdigest()}
    (ASSETS / 'models/menu-texture-fit.json').write_text(json.dumps(receipt, indent=1) + '\n')
    print(json.dumps({'sprites': len(sprite_results), 'atlases': len(atlas_results), 'slices': len(slice_uvs),
                      'gpuMBBefore': round(before / 2**20, 1), 'gpuMBAfter': round(after / 2**20, 1),
                      'fileMB': round(files / 2**20, 1)}, indent=1))


if __name__ == '__main__':
    main()
