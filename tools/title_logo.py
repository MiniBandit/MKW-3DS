"""The title screen's logo: Mario Kart WiiDS in place of the Wii's "Wii".

The Wii draws its title logo in two parts: the wordmark and the top of the
ring are their own image (rows 0-140 of the 832x456 title art), the lower ring
and "Wii" are painted into each character picture, some of them behind a
character (Peach's hair crosses the ring). Both logos share the wordmark and
ring, so only the inside of the ring below the wordmark is replaced, and only
where the logo shows: a pixel that differs from the six pictures' median there
is a character in front and stays. The blurred pictures (license and main menu
backgrounds) get the same change, blurred like the Wii's own blurred logo.
"""
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
LOGO = ROOT / 'tools/release/artwork/logo.png'
# The logo artwork in title-art pixels: scale and offset fitted to the Wii's
# wordmark (title rows 0-140), and the ring's inside that it covers.
SCALE, OFFSET = 0.2725, (143.25, 20.75)
RING_CENTER, RING_INNER = (415.0, 115.5), 74.0
WORDMARK_BOTTOM = 111.0
# Gaussian sigma that turns the Wii's sharp title logo into its blurred one
# (tt_title_screen_title_rogo_bokeboke: mean error 1.2 levels).
BLUR_SIGMA = 5.75
SIZE = (832, 456)


def logo_in_title():
    """The new logo over white, in title-art pixels (float RGB)."""
    logo = Image.open(LOGO).convert('RGBA')
    pad = 1000
    big = Image.new('RGBA', (logo.width + 2 * pad, logo.height + 2 * pad), (255, 255, 255, 0))
    big.paste(logo, (pad, pad))
    dx, dy = OFFSET
    box = (pad - dx / SCALE, pad - dy / SCALE, pad + (SIZE[0] - dx) / SCALE, pad + (SIZE[1] - dy) / SCALE)
    placed = np.array(big.convert('RGBa').resize(SIZE, Image.Resampling.LANCZOS, box=box).convert('RGBA'), np.float32)
    alpha = placed[..., 3:] / 255
    return placed[..., :3] * alpha + 255 * (1 - alpha)


def blur(values, sigma):
    radius = int(sigma * 3 + 1)
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    values = np.pad(values, ((radius, radius), (radius, radius), (0, 0)), mode='edge')
    values = np.apply_along_axis(lambda v: np.convolve(v, kernel, 'valid'), 0, values)
    return np.apply_along_axis(lambda v: np.convolve(v, kernel, 'valid'), 1, values)


def replace(sharp, blurred):
    """New sharp and blurred title pictures (opaque 832x456 RGBA images, the
    six title arts in order) with the new logo."""
    new = logo_in_title()
    y, x = np.mgrid[0:SIZE[1], 0:SIZE[0]].astype(np.float32)
    radius = np.hypot(x - RING_CENTER[0], y - RING_CENTER[1])
    region = np.clip((RING_INNER - radius) / 2.5, 0, 1) * np.clip((y - WORDMARK_BOTTOM) / 3, 0, 1)
    sharp = [np.array(im.convert('RGB'), np.float32) for im in sharp]
    median = np.median(np.stack(sharp), 0)
    out_sharp, out_blurred = [], []
    for picture, soft in zip(sharp, blurred):
        shown = np.clip((60 - np.abs(picture - median).max(2)) / 35, 0, 1)
        change = (region * shown)[..., None] * (new - picture)
        soft = np.array(soft.convert('RGB'), np.float32)
        out_sharp.append(Image.fromarray(np.clip(picture + change, 0, 255).round().astype(np.uint8)).convert('RGBA'))
        out_blurred.append(Image.fromarray(np.clip(soft + blur(change, BLUR_SIGMA), 0, 255).round().astype(np.uint8)).convert('RGBA'))
    return out_sharp, out_blurred
