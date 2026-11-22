#!/usr/bin/env python3
"""Race loading picture: the top screen while a race loads.

Sources (assets/loading-screen/): background.png (800x480, the top screen's
shape) and loading.png (800x480 transparent: "LOADING" above one period),
both made for the port; the logo is tools/release/artwork/logo.png. Without
the sources nothing is cooked and race loads keep their black top screen.

The game composes the picture straight into the top screen's framebuffer
(src/loading_screen.cpp, RaceLoadingPicture), so every layer is stored the way
the LCD reads it: one column per screen x, bottom row first. Each layer is
resized once, straight to the LCD's grid. The logo and "LOADING..." are
separate layers so that in 3D they can come out of the screen (the race HUD's
layer) while the picture stays on it. The three periods bounce in a wave: a
60 fps loop of their area, drawn at 4x and reduced, so they move by fractions
of a pixel.

One file per top-screen width: loading/race-400.bin (2D and 3D) and
loading/race-800.bin (800px Mode). Format (little endian):
  "MKLQ", u16 width, u16 loop frames, u16 frames per second, u16 0,
  3 x {u16 x, y, w, h}: the logo, "LOADING" and the periods' area (screen
    pixels, y from the top),
  background: width*240 BGR8,
  logo: w*h premultiplied BGRA8,
  "LOADING": w*h premultiplied grey + alpha (2 bytes; the art is black and white),
  the periods: loop frames x w*h premultiplied grey + alpha.
"""
import math, struct
from pathlib import Path
import numpy as np
from PIL import Image
from cook_roster import ROOT

SOURCES = ROOT / 'assets/loading-screen'
LOGO = ROOT / 'tools/release/artwork/logo.png'
OUT = ROOT / 'sd-card/3ds/mkw-3ds/loading'
FPS = 60
# Layout in the 800x480 source: the logo top left over the black band and the
# picture's top edge, "LOADING..." right-aligned in the lower black band.
LOGO_X, LOGO_Y, LOGO_W = 10, 6, 320
TEXT_SCALE, TEXT_RIGHT, TEXT_CENTER_Y = 0.78, 790, 437
PERIOD_SCALE = 1.3           # periods relative to the letters: big enough that their black centre shows
PERIOD_GAP = 1
SPLIT_Y = 280                # loading.png: "LOADING" above this row, the period below
SS = 4                       # supersampling of the period loop
# The wave: each period hops HOP_H source pixels over HOP seconds, HOP_STEP
# after the one before it, lands with a small rebound, and the three rest
# until the loop restarts.
HOP, HOP_STEP, HOP_H = 0.30, 0.10, 10.0
REBOUND, REBOUND_H = 0.12, 0.22
LOOP = 0.90


def resize(im, size):
    size = (max(1, round(size[0])), max(1, round(size[1])))
    return im.convert('RGBa').resize(size, Image.Resampling.LANCZOS).convert('RGBA')


def crop_layer(rows):
    words = Image.open(SOURCES / 'loading.png').convert('RGBA')
    part = words.crop((0, rows[0], words.width, rows[1]))
    return part.crop(part.getbbox())


def layout():
    text, period = crop_layer((0, SPLIT_Y)), crop_layer((SPLIT_Y, 480))
    tw, th = text.width * TEXT_SCALE, text.height * TEXT_SCALE
    pw, ph = period.width * TEXT_SCALE * PERIOD_SCALE, period.height * TEXT_SCALE * PERIOD_SCALE
    pitch = pw + PERIOD_GAP
    tx = TEXT_RIGHT - tw - 3 * pitch
    ty = TEXT_CENTER_Y - th / 2
    periods = [(tx + tw + PERIOD_GAP + i * pitch, ty + th - ph) for i in range(3)]
    return text, period, (tx, ty, tw, th), (pw, ph), periods


def lift(t, i):
    u = t - i * HOP_STEP
    if 0 <= u < HOP:
        return HOP_H * math.sin(math.pi * u / HOP)
    if HOP <= u < HOP + REBOUND:
        return HOP_H * REBOUND_H * math.sin(math.pi * (u - HOP) / REBOUND)
    return 0.0


def columns(rgba):
    """An HxWxC array (top row first) in framebuffer order: per column, bottom row first."""
    return np.ascontiguousarray(rgba[::-1].transpose(1, 0, 2))


def premultiplied(im):
    a = np.array(im.convert('RGBA')).astype(np.float32)
    a[..., :3] *= a[..., 3:] / 255
    return np.clip(a.round(), 0, 255).astype(np.uint8)


def grey_alpha(im):
    a = premultiplied(im)
    return np.stack([a[..., :3].mean(2).round().astype(np.uint8), a[..., 3]], -1)


def cook(width):
    sx, sy = width / 800, 240 / 480
    background = Image.open(SOURCES / 'background.png').convert('RGB').resize((width, 240), Image.Resampling.LANCZOS)
    logo = Image.open(LOGO).convert('RGBA')
    logo = logo.crop(logo.getbbox())
    logo = resize(logo, (LOGO_W * sx, LOGO_W * logo.height / logo.width * sy))
    logo_rect = (round(LOGO_X * sx), round(LOGO_Y * sy), logo.width, logo.height)
    text, period, (tx, ty, tw, th), (pw, ph), places = layout()
    text = resize(text, (tw * sx, th * sy))
    text_rect = (round(tx * sx), round(ty * sy), text.width, text.height)
    x0 = math.floor(places[0][0] * sx) - 2
    x1 = math.ceil((places[2][0] + pw) * sx) + 2
    y0 = math.floor((places[0][1] - HOP_H) * sy) - 2
    y1 = math.ceil((places[0][1] + ph) * sy) + 2
    area = (x0, y0, x1 - x0, y1 - y0)
    sprite = resize(period, (pw * sx * SS, ph * sy * SS))
    frames = []
    for f in range(round(LOOP * FPS)):
        layer = Image.new('RGBA', (area[2] * SS, area[3] * SS), (0, 0, 0, 0))
        for i, (px, py) in enumerate(places):
            x = (px * sx - x0) * SS
            y = ((py - lift(f / FPS, i)) * sy - y0) * SS
            layer.alpha_composite(sprite, (round(x), round(y)))
        frames.append(columns(grey_alpha(resize(layer, area[2:]))))
    for r in (logo_rect, text_rect, area):
        assert r[0] >= 0 and r[1] >= 0 and r[0] + r[2] <= width and r[1] + r[3] <= 240, r
    data = b'MKLQ' + struct.pack('<4H', width, len(frames), FPS, 0)
    data += b''.join(struct.pack('<4H', *r) for r in (logo_rect, text_rect, area))
    data += columns(np.array(background)[..., ::-1]).tobytes()
    data += columns(premultiplied(logo)[..., [2, 1, 0, 3]]).tobytes()
    data += columns(grey_alpha(text)).tobytes()
    data += b''.join(f.tobytes() for f in frames)
    return data


def main():
    if not (SOURCES / 'background.png').is_file() or not (SOURCES / 'loading.png').is_file():
        print('race loading picture: no sources in assets/loading-screen, not cooked')
        for old in OUT.glob('race-*.bin'):
            old.unlink()
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for width in (400, 800):
        data = cook(width)
        target = OUT / f'race-{width}.bin'
        if not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)
        print(f'race loading picture {width}: {len(data)} bytes')


if __name__ == '__main__':
    main()
