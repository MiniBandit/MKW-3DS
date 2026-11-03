#!/usr/bin/env python3
"""Remove transparent-pixel fringe colours from a banner logo PNG.

The visible RGBA pixels are preserved byte-for-byte.  RGB is propagated from
the nearest visible pixel into alpha-zero texels so bilinear GPU sampling does
not pull a white document background into the logo edge.
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from PIL import Image


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_png", type=Path)
    parser.add_argument("output_png", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source = Image.open(args.input_png).convert("RGBA")
    width, height = source.size
    pixels = list(source.getdata())
    queue: deque[int] = deque()
    visited = bytearray(width * height)

    for index, pixel in enumerate(pixels):
        if pixel[3] != 0:
            visited[index] = 1
            queue.append(index)

    while queue:
        index = queue.popleft()
        x = index % width
        y = index // width
        for neighbour in (
            index - 1 if x else -1,
            index + 1 if x + 1 < width else -1,
            index - width if y else -1,
            index + width if y + 1 < height else -1,
        ):
            if neighbour < 0 or visited[neighbour]:
                continue
            visited[neighbour] = 1
            red, green, blue, _ = pixels[index]
            pixels[neighbour] = (red, green, blue, 0)
            queue.append(neighbour)

    output = Image.new("RGBA", source.size)
    output.putdata(pixels)
    args.output_png.parent.mkdir(parents=True, exist_ok=True)
    output.save(args.output_png, optimize=True)
    print(
        f"prepared {args.output_png.resolve()}: {width}x{height}, "
        "visible RGBA preserved, alpha-zero RGB edge-bled"
    )


if __name__ == "__main__":
    main()
