#!/usr/bin/env python3
"""Pause screen controls diagram, after the Wii PauseMenu's controller page.

One RGBA8 texture per console (Old 3DS, New 3DS): the console photo, the
label bands, button icons and leader lines, in top-screen pixels (rows Y0 to
Y0+128). The labels and icon letters are drawn at runtime with the game font
(menu_ui.cpp, pauseControls), at the same screen positions as below.

Sources: assets/pause-controls/old3ds.png and new3ds.png, front photos cut
out on a transparent background. They are not in the repository; without
them nothing is cooked and the pause screen shows no diagram.
"""
import subprocess, sys
import tex3ds_cache
from pathlib import Path
from PIL import Image, ImageDraw
from cook_roster import ROOT
from devkitpro import tool

SOURCES = ROOT / 'assets/pause-controls'
OUT = ROOT / 'sd-card/3ds/mkw-3ds/textures'
S = 4                       # supersampling
Y0, ROWS = 8, 128           # screen rows held by the texture
DEVICE_X, DEVICE_Y, DEVICE_W = 158, 52, 84
TOP_Y, BOTTOM_Y, BAND_H = 32, 104, 13
# Icon centres (screen px). The runtime labels sit beside these.
L_X, R_X, PAD_X, A_X = 163, 237, 136, 264
# Points on each photo (source pixels): Circle Pad, A, L and R shoulders.
FEATURES = {
    'old3ds': {'pad': (62.3, 102), 'a': (370, 109), 'l': (24, 31), 'r': (387, 31), 'a_color': (70, 175, 205)},
    'new3ds': {'pad': (57.5, 101.8), 'a': (380.3, 126), 'l': (26, 29), 'r': (391, 29), 'a_color': (214, 44, 56)},
}
LINE = (40, 110, 255, 255)


def px(x, y):
    return (x * S, (y - Y0) * S)


def band(img, x0, x1, y, fade_left):
    """Blue checkered band, fading out toward the screen edge."""
    w, h = (x1 - x0) * S, BAND_H * S
    layer = Image.new('RGBA', (w, h))
    pixels = layer.load()
    for yy in range(h):
        edge = yy < S or yy >= h - S
        for xx in range(w):
            t = xx / w if fade_left else 1 - xx / w   # 0 at the screen edge
            alpha = min(1.0, t / 0.7) ** 1.5
            if edge:
                color = (90, 150, 255)
            else:
                color = (26, 66, 210) if ((xx // S) + (yy // S)) % 2 else (12, 28, 120)
            pixels[xx, yy] = (*color, int(235 * alpha))
    img.alpha_composite(layer, (x0 * S, (y - BAND_H // 2 - Y0) * S))


def shoulder(draw, cx, cy):
    """L/R button: a grey dome with a flat base."""
    w, h = 24 * S, 14 * S
    box = [cx * S - w // 2, (cy - Y0) * S - h // 2, cx * S + w // 2, (cy - Y0) * S + h // 2]
    draw.chord([box[0], box[1], box[2], box[3] + h], 180, 360, fill=(36, 36, 40, 255))
    inner = [box[0] + S, box[1] + S, box[2] - S, box[3] + h - S]
    draw.chord(inner, 180, 360, fill=(104, 106, 116, 255))
    draw.chord([inner[0] + 2 * S, inner[1] + S, inner[2] - 2 * S, inner[1] + h], 180, 360, fill=(138, 140, 150, 255))
    draw.rectangle([box[0], box[3] - S, box[2], box[3]], fill=(36, 36, 40, 255))


def disc(draw, cx, cy, r, fill):
    x, y = px(cx, cy)
    draw.ellipse([x - r * S, y - r * S, x + r * S, y + r * S], fill=fill)


def circle_pad(draw, cx, cy):
    disc(draw, cx, cy, 8.5, (50, 50, 54, 255))
    disc(draw, cx, cy, 7.5, (200, 202, 208, 255))
    disc(draw, cx, cy, 5.5, (140, 142, 150, 255))
    disc(draw, cx, cy, 4.5, (232, 234, 238, 255))


def button(draw, cx, cy, color):
    disc(draw, cx, cy, 8.5, (40, 40, 44, 255))
    disc(draw, cx, cy, 7.5, color)
    light = tuple(min(255, c + 40) for c in color)
    x, y = px(cx, cy)
    draw.ellipse([x - 5 * S, y - 6.5 * S, x + 5 * S, y - 1 * S], fill=(*light, 255))
    disc(draw, cx, cy + 0.8, 5.5, color)


def leader(draw, start, end):
    draw.line([px(*start), px(*end)], fill=LINE, width=int(1.5 * S))
    x, y = px(*end)
    r = 1.6 * S
    draw.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, 255))
    r = 1.0 * S
    draw.ellipse([x - r, y - r, x + r, y + r], fill=LINE)


def cook(name):
    photo = Image.open(SOURCES / (name + '.png')).convert('RGBA')
    photo = photo.crop(photo.getbbox())
    scale = DEVICE_W / photo.width
    f = FEATURES[name]
    img = Image.new('RGBA', (400 * S, ROWS * S))
    band(img, 0, L_X, TOP_Y, True)
    band(img, R_X, 400, TOP_Y, False)
    band(img, 0, PAD_X, BOTTOM_Y, True)
    band(img, A_X, 400, BOTTOM_Y, False)
    device = photo.resize((DEVICE_W * S, round(photo.height * scale * S)), Image.LANCZOS)
    scale_y = device.height / (photo.height * S)
    img.alpha_composite(device, px(DEVICE_X, DEVICE_Y))
    # FEATURES are continuous source coordinates (pixel edges), measured on the photos.
    point = {k: (DEVICE_X + f[k][0] * scale, DEVICE_Y + f[k][1] * scale_y) for k in ('pad', 'a', 'l', 'r')}
    draw = ImageDraw.Draw(img)
    leader(draw, (L_X, TOP_Y + 5), point['l'])
    leader(draw, (R_X, TOP_Y + 5), point['r'])
    leader(draw, (PAD_X, BOTTOM_Y), point['pad'])
    leader(draw, (A_X, BOTTOM_Y), point['a'])
    shoulder(draw, L_X, TOP_Y)
    shoulder(draw, R_X, TOP_Y)
    circle_pad(draw, PAD_X, BOTTOM_Y)
    button(draw, A_X, BOTTOM_Y, f['a_color'])
    small = img.resize((400, ROWS), Image.LANCZOS)
    texture = Image.new('RGBA', (512, ROWS))
    texture.paste(small, (0, 0))
    OUT.mkdir(parents=True, exist_ok=True)
    png = OUT / f'pause_controls_{name}.png'
    texture.save(png)
    tex3ds_cache.run([str(tool('tex3ds')), '-f', 'rgba8', '-z', 'none', '-o', str(png.with_suffix('.t3x')), str(png)],
                   check=True, stdout=subprocess.DEVNULL)
    return png.with_suffix('.t3x')


def main():
    if not all((SOURCES / (n + '.png')).is_file() for n in FEATURES):
        print('pause controls: no source photos in ' + str(SOURCES) + ', skipped')
        return
    for name in FEATURES:
        print(cook(name))


if __name__ == '__main__':
    sys.exit(main())
