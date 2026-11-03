#!/usr/bin/env python3
"""Expose original cooked BRLYT font materials for dynamic-label fallback.

Uses the existing source menu receipt, not an extraction or a full UI recook.
No texture is generated: the white BRFNT glyph is remapped by the renderer.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def rgba(values):
    result = 0
    for value in values:
        result = (result << 8) | max(0, min(255, int(value)))
    return '0x%08xu' % result


def main():
    source = ROOT / 'sd-card/3ds/mkw-3ds/models/menu-ui.json'
    styles = json.loads(source.read_text())['textStyles']
    lines = ['#pragma once', '#include <cstdint>', 'namespace mkw {',
             'struct UiTextStyle {std::uint32_t color0,color1,corners[4];};',
             'inline constexpr UiTextStyle UiTextStyles[]={']
    for style in styles:
        top, bottom = style['vertices']
        lines.append('{%s,%s,{%s,%s,%s,%s}},' % (
            rgba(style['colors'][0]), rgba(style['colors'][1]),
            rgba(top), rgba(top), rgba(bottom), rgba(bottom)))
    lines += ['};', '}']
    (ROOT / 'include/mkw/menu_text_styles.hpp').write_text('\n'.join(lines) + '\n')
    receipt = {'styles': len(styles), 'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
               'newTextures': 0}
    (ROOT / 'build/menu-text-styles.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Cooked %d original font materials; no new textures' % len(styles))


if __name__ == '__main__':
    main()
