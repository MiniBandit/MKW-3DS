#!/usr/bin/env python3
"""Race loading picture: the top screen while a race loads.

Sources (assets/loading-screen/): background.png (800x480, the top screen's
shape) and loading.png (800x480 transparent: "LOADING" above one period),
both made for the port; the logo is tools/release/artwork/logo.png. Without
the sources nothing is cooked and race loads keep their black top screen.

The game copies the picture straight into the top screen's framebuffer
(src/loading_screen.cpp, RaceLoadingPicture), so it is stored the way the
LCD reads it: BGR8, one 240-pixel column per screen x, bottom row first. One
file per top-screen width: loading/race-400.bin (2D and 3D) and
loading/race-800.bin (800px Mode). Each holds the picture with "LOADING" and
no periods, then each period's rectangle with the period off and on, so the
game animates them by copying a few columns.

Format (little endian): "MKLP", u16 width, u16 periods, periods x {u16 x, y,
w, h} (screen pixels, y from the top), width*240*3 picture bytes, then per
period w*h*3 bytes off and w*h*3 bytes on (each column bottom row first).
"""
import struct
from pathlib import Path
import numpy as np
from PIL import Image
from cook_roster import ROOT

SOURCES = ROOT / 'assets/loading-screen'
LOGO = ROOT / 'tools/release/artwork/logo.png'
OUT = ROOT / 'sd-card/3ds/mkw-3ds/loading'
# Layout in the 800x480 source: the logo top left over the black band and the
# picture's top edge, "LOADING..." right-aligned in the lower black band.
LOGO_X, LOGO_Y, LOGO_W = 10, 6, 320
TEXT_SCALE, TEXT_RIGHT, TEXT_CENTER_Y = 0.6, 790, 438
PERIOD_GAP, PERIODS = 4, 3
SPLIT_Y = 280                # loading.png: "LOADING" above this row, the period below


def premultiplied_resize(im, size):
    return im.convert('RGBa').resize(size, Image.Resampling.LANCZOS).convert('RGBA')


def layer(path_image, rows):
    part = path_image.crop((0, rows[0], path_image.width, rows[1]))
    part = part.crop(part.getbbox())
    return premultiplied_resize(part, (round(part.width * TEXT_SCALE), round(part.height * TEXT_SCALE)))


def compose():
    picture = Image.open(SOURCES / 'background.png').convert('RGBA')
    logo = Image.open(LOGO).convert('RGBA')
    logo = logo.crop(logo.getbbox())
    logo = premultiplied_resize(logo, (LOGO_W, round(LOGO_W * logo.height / logo.width)))
    picture.alpha_composite(logo, (LOGO_X, LOGO_Y))
    words = Image.open(SOURCES / 'loading.png').convert('RGBA')
    text, period = layer(words, (0, SPLIT_Y)), layer(words, (SPLIT_Y, words.height))
    pitch = period.width + PERIOD_GAP
    x = TEXT_RIGHT - text.width - PERIODS * pitch
    y = round(TEXT_CENTER_Y - text.height / 2)
    picture.alpha_composite(text, (x, y))
    bottom = y + text.height
    places = [(x + text.width + PERIOD_GAP + i * pitch, bottom - period.height) for i in range(PERIODS)]
    return picture, period, places


def halve(rgb, width):
    """800x480 -> width x 240 by averaging (2x1 rows, and 2 columns for 400)."""
    a = rgb.astype(np.float32)
    a = (a[0::2] + a[1::2]) / 2
    if width == 400:
        a = (a[:, 0::2] + a[:, 1::2]) / 2
    return np.clip(a.round(), 0, 255).astype(np.uint8)


def columns(rgb, x, y, w, h):
    """BGR bytes of a rectangle in framebuffer order: per column, bottom row first."""
    block = rgb[y:y + h, x:x + w][::-1, :, ::-1]       # bottom row first, BGR
    return np.ascontiguousarray(block.transpose(1, 0, 2)).tobytes()


def cook(width, picture, period, places):
    base = np.array(picture.convert('RGB'))
    off = halve(base, width)
    rects, patches = [], []
    for px, py in places:
        lit = picture.copy()
        lit.alpha_composite(period, (px, py))
        on = halve(np.array(lit.convert('RGB')), width)
        ys, xs = np.where((on != off).any(2))
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        rects.append((x0, y0, x1 - x0, y1 - y0))
        patches.append(columns(off, x0, y0, x1 - x0, y1 - y0) + columns(on, x0, y0, x1 - x0, y1 - y0))
    for a, b in zip(rects, rects[1:]):
        assert a[0] + a[2] <= b[0], 'periods must not share a column'
    data = b'MKLP' + struct.pack('<HH', width, len(rects))
    data += b''.join(struct.pack('<4H', *r) for r in rects)
    data += columns(off, 0, 0, width, 240) + b''.join(patches)
    return data, off


def main():
    if not (SOURCES / 'background.png').is_file() or not (SOURCES / 'loading.png').is_file():
        print('race loading picture: no sources in assets/loading-screen, not cooked')
        for old in OUT.glob('race-*.bin'):
            old.unlink()
        return
    picture, period, places = compose()
    OUT.mkdir(parents=True, exist_ok=True)
    for width in (400, 800):
        data, _ = cook(width, picture, period, places)
        target = OUT / f'race-{width}.bin'
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)
        print(f'race loading picture {width}: {len(data)} bytes')


if __name__ == '__main__':
    main()
