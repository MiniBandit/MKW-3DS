#!/usr/bin/env python3
"""The race loading picture's sources, made from the user's own disc.

tools/cook_loading_picture.py (ethanseagull's 1.2.0) cooks the top screen shown
while a race loads from assets/loading-screen/background.png and loading.png.
His builds ship hand-made sources; this port ships no game art, so both are
made here from the disc:

  background.png  800x480: one frame of the disc's opening movie
                  (thp/title/title.thp, stored 16:9 anamorphic), between a
                  black band at the top (under the logo) and one at the
                  bottom (under "LOADING...").
  loading.png     800x480 transparent: "LOADING" above row SPLIT_Y and one
                  period below it, in the race font (kart_kanji_font.brfnt).

Existing sources are left alone, so hand-made ones (Ethan's layout) win.
"""
import sys
from pathlib import Path
from PIL import Image
from cook_roster import ROOT
from cook_race_hud import font_strings
from menu_movies import ThpMovie, extract_movies
from cook_loading_picture import SOURCES, SPLIT_Y

MOVIE = 'thp/title/title.thp'
FRAME = 4210            # Luigi and Mario jumping past the Mario Kart banner (Luigi Circuit)
TOP_BAND, BOTTOM_BAND = 40, 400   # the picture fills rows TOP_BAND..BOTTOM_BAND
PICTURE_Y = 0.18        # which part of the frame's height stays in view (0 top, 1 bottom)
FONT = ROOT / 'assets/race-ui/Font/kart_kanji_font.brfnt'
TEXT_HEIGHT = 64        # "LOADING" ink height in the 800x480 source, before the cook's scale


def background():
    movie = ThpMovie(extract_movies([MOVIE])[0])
    frame = movie.frame(min(FRAME, movie.count - 1)).convert('RGB')
    height = BOTTOM_BAND - TOP_BAND
    wide = frame.resize((800, round(800 * 9 / 16)), Image.Resampling.LANCZOS)
    top = round((wide.height - height) * PICTURE_Y)
    out = Image.new('RGB', (800, 480), (0, 0, 0))
    out.paste(wide.crop((0, top, 800, top + height)), (0, TOP_BAND))
    return out


def words():
    glyphs = font_strings(FONT, ['LOADING', '.'])
    text, period = (glyphs[k].crop(glyphs[k].getbbox()) for k in ('LOADING', '.'))
    scale = TEXT_HEIGHT / text.height
    big = lambda im: im.resize((round(im.width * scale), round(im.height * scale)), Image.Resampling.LANCZOS)
    text, period = big(text), big(period)
    out = Image.new('RGBA', (800, 480), (0, 0, 0, 0))
    out.alpha_composite(text, ((800 - text.width) // 2, (SPLIT_Y - text.height) // 2))
    out.alpha_composite(period, ((800 - period.width) // 2, SPLIT_Y + (480 - SPLIT_Y - period.height) // 2))
    return out


def main():
    SOURCES.mkdir(parents=True, exist_ok=True)
    made = []
    for name, make in (('background.png', background), ('loading.png', words)):
        target = SOURCES / name
        if not target.is_file():
            make().save(target)
            made.append(name)
    print('race loading sources:', ', '.join(made) if made else 'already present')


if __name__ == '__main__':
    sys.exit(main())
