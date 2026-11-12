#!/usr/bin/env python3
"""Render Markdown heading sections as 1920x1080 video-overlay PNGs."""
import argparse
import json
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 1920, 1080
SCALE = 2
MARGIN, TOP, BOTTOM, GAP = 80, 200, 80, 56


def sections(text, level):
    result = []
    current = None
    for line in text.splitlines():
        heading = re.match(r'^(#{1,6})\s+(.+?)\s*#*$', line)
        if heading:
            depth, title = len(heading[1]), heading[2]
            if depth <= level:
                current = None
            if depth == level:
                current = {'title': title, 'items': []}
                result.append(current)
        elif current is not None and line.startswith('- '):
            current['items'].append(line[2:].strip())
        elif current is not None and line.startswith('  ') and current['items']:
            current['items'][-1] += ' ' + line.strip()
    return [s for s in result if s['items']]


def find_font(explicit, bold=False):
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise ValueError(f'Font not found: {path}')
        return str(path)
    candidates = ([
        '/System/Library/Fonts/Supplemental/Arial Bold.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
        'C:/Windows/Fonts/arialbd.ttf',
    ] if bold else [
        '/System/Library/Fonts/Supplemental/Arial.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
        'C:/Windows/Fonts/arial.ttf',
    ])
    for path in candidates:
        if Path(path).is_file():
            return path
    raise ValueError('No suitable font found; pass --font and --bold-font.')


def font(path, size):
    return ImageFont.truetype(path, size * SCALE)


def wrap(text, face, width):
    lines, line = [], ''
    for word in text.split():
        if face.getlength(word) > width * SCALE:
            raise ValueError(f'Word too wide at this font size: {word}')
        candidate = (line + ' ' + word).strip()
        if line and face.getlength(candidate) > width * SCALE:
            lines.append(line)
            line = word
        else:
            line = candidate
    return lines + ([line] if line else [])


def layout(items, path, max_size, min_size):
    available = HEIGHT - TOP - BOTTOM
    for size in range(max_size, min_size - 1, -1):
        for count in (3,):
            width = (WIDTH - MARGIN * 2 - GAP * (count - 1)) / count
            face = font(path, size)
            try:
                blocks = [wrap(item, face, width - 30) for item in items]
            except ValueError:
                continue
            line_height, spacing = round(size * 1.25), round(size * .48)
            heights = [len(lines) * line_height + spacing for lines in blocks]
            # Find the smallest column height that keeps each bullet intact.
            def pack(limit):
                columns, column, used = [], [], 0
                for lines, height in zip(blocks, heights):
                    if height > limit:
                        return None
                    if column and used + height > limit:
                        columns.append(column)
                        column, used = [], 0
                    column.append(lines)
                    used += height
                return columns + [column]
            trial = pack(available)
            if trial is None or len(trial) > count:
                continue
            lo, hi = max(heights), available
            while lo < hi:
                middle = (lo + hi) // 2
                trial = pack(middle)
                if trial is not None and len(trial) <= count:
                    hi = middle
                else:
                    lo = middle + 1
            return size, width, line_height, spacing, pack(hi)
    raise ValueError('Section cannot fit without clipping; lower --min-font-size.')


def render(section, index, total, args, regular, bold):
    size, width, leading, spacing, columns = layout(
        section['items'], regular, args.max_font_size, args.min_font_size)
    bg = (0, 0, 0, 0) if args.background == 'transparent' else (0, 0, 0, 255)
    image = Image.new('RGBA', (WIDTH * SCALE, HEIGHT * SCALE), bg)
    draw = ImageDraw.Draw(image)
    def text(x, y, value, face, fill=(255, 255, 255, 255)):
        draw.text((round(x*SCALE), round(y*SCALE)), value, font=face,
                  fill=fill, anchor='lt')
    title_size = args.title_size
    while font(bold, title_size).getlength(section['title']) > (WIDTH - 2*MARGIN)*SCALE:
        title_size -= 1
    text(MARGIN, 65, section['title'], font(bold, title_size))
    face = font(regular, size)
    ends = []
    for column, blocks in enumerate(columns):
        x, y = MARGIN + column*(width+GAP), TOP
        for lines in blocks:
            text(x, y, '\u2022', face)
            for line in lines:
                text(x+30, y, line, face)
                y += leading
            y += spacing
        ends.append(y)
    assert max(ends) <= HEIGHT - BOTTOM
    image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    slug = re.sub(r'[^a-z0-9]+', '-', section['title'].lower()).strip('-')
    target = args.output / f'{index:02d}-{slug}.png'
    image.save(target)
    return {'file': target.name, 'title': section['title'], 'items': len(section['items']),
            'font_size': size, 'columns': len(columns), 'bottom': max(ends)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--heading-level', type=int, choices=range(1, 7), default=1,
                        help='Markdown heading level; defaults to # categories.')
    parser.add_argument('--background', choices=['transparent', 'dark'], default='transparent')
    parser.add_argument('--font')
    parser.add_argument('--bold-font')
    parser.add_argument('--max-font-size', type=int, default=36)
    parser.add_argument('--min-font-size', type=int, default=18)
    args = parser.parse_args()
    if not 8 <= args.min_font_size <= args.max_font_size <= 80:
        parser.error('Font sizes must satisfy 8 <= min <= max <= 80.')
    entries = sections(args.input.read_text(encoding='utf-8'), args.heading_level)
    if not entries:
        parser.error('No bullet sections found at the selected heading level.')
    regular, bold = find_font(args.font), find_font(args.bold_font, True)
    # Fit every section first, then use the most restrictive size everywhere.
    sizes = [layout(s['items'], regular, args.max_font_size, args.min_font_size)[0] for s in entries]
    args.max_font_size = args.min_font_size = min(sizes)
    args.title_size = 60
    while any(font(bold, args.title_size).getlength(s['title']) > (WIDTH-2*MARGIN)*SCALE for s in entries):
        args.title_size -= 1
    args.output.mkdir(parents=True, exist_ok=True)
    rows = [render(s, i, len(entries), args, regular, bold) for i, s in enumerate(entries, 1)]
    manifest = {'source': str(args.input.resolve()), 'size': [WIDTH, HEIGHT],
                'background': args.background, 'body_font_size': args.min_font_size,
                'title_font_size': args.title_size, 'cards': rows}
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(rows, indent=2))


if __name__ == '__main__':
    main()
