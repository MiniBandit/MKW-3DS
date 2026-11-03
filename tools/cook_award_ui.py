"""Original Award UI layouts, animation tracks, variants and texture bank."""
import json
from pathlib import Path


def cook_awards(root, out, manifest, header):
    from cook_race_flow import (parse_layout, controller_clips, controller_variants,
                               texture_file, emit_layout, digest, f, font_glyphs, pack_atlas)
    from cook_race_items import decode_tpl
    from menu_layout import controller_animations, messages
    base = root / 'assets/menu-ui/Award/award'
    sources, textures, slots = [], [], {}
    # Runtime swaps character/cup pictures by their original TPL names; include
    # the full bank, not just the Mario/Mushroom placeholders in the layouts.
    for path in sorted((base / 'timg').glob('*.tpl')):
        slots[path.name] = len(textures)
        textures.append(texture_file(decode_tpl(path), 'flow_award_' + path.stem, out, 'rgba8'))
        sources.append(path)
    layouts = []
    for ctr in sorted((base / 'ctrl').glob('*.brctr')):
        path = base / 'blyt' / (controller_animations(ctr)['layout'] + '.brlyt')
        layout = parse_layout(path)
        clips = controller_clips(ctr, layout, base / 'anim')
        layouts.append((ctr.stem, layout, clips, controller_variants(ctr)))
        sources += [ctr, path] + [clip['source'] for clip in clips]
    bmg = root / 'assets/menu-ui/Award_U/message/Race.bmg'
    original = messages(bmg, icon_glyphs=True)
    ids = set(range(1214, 1227)) | set(range(1415, 1432))
    ids |= {v['message'] for _, _, _, variants in layouts for v in variants.values()}
    labels = {i: original[i] for i in sorted(ids) if i in original and original[i]}
    sources.append(bmg)
    font = root / 'assets/race-ui/Font/kart_kanji_font.brfnt'
    # Names and ordinal/score text share this font with the result board.
    common_path = root / 'assets/menu-ui/Award_U/message/Common.bmg'
    common = messages(common_path)
    text = ''.join(v for k,v in labels.items() if k<1424) + ''.join(common.get(0x2328 + i, '') for i in range(24))
    text += ''.join(original.get(0x515 + i, '') for i in range(12)) + '0123456789+ ABCDEF'
    info, glyphs = font_glyphs(font, text)
    atlas, rects = pack_atlas({c: g['image'] for c, g in glyphs.items()})
    font_slot = len(textures)
    textures.append(texture_file(atlas, 'flow_award_font', out, 'la4'))
    sources += [font, common_path]
    rank_font = root / 'assets/race-ui/Font/tt_kart_extension_font.brfnt'
    rank_info, rank_glyphs = font_glyphs(rank_font, ''.join(labels[i] for i in range(1424,1432)))
    rank_atlas, rank_rects = pack_atlas({c:g['image'] for c,g in rank_glyphs.items()})
    rank_slot = len(textures)
    textures.append(texture_file(rank_atlas, 'flow_award_rank_font', out, 'la4'))
    sources.append(rank_font)
    if len(textures) > 127:
        raise ValueError('Award texture slots exceed FlowMaterial int8 range')
    header.append('// Original Award controllers; texture indices use the separate award bank.')
    header.append('inline constexpr const char* FlowAwardTextures[]={' + ','.join(json.dumps(t) for t in textures) + '};')
    header.append('inline constexpr const char* FlowAwardTextureNames[]={' + ','.join(json.dumps(t) for t in slots) + ',"font","rank_font"};')
    header.append('struct FlowAwardVariant {const char* name;FlowControl pose;uint32_t message;};')
    for name, layout, clips, variants in layouts:
        prefix = 'Flow' + name
        emit_layout(prefix, layout, clips, slots.__getitem__, header)
        header.append(f'inline constexpr FlowAwardVariant {prefix}Variants[]={{')
        for key, v in variants.items():
            pose = ','.join(f(v[k]) for k in ('x', 'y', 'sx', 'sy', 'delay'))
            header.append('{' + json.dumps(key) + ',{' + pose + '},' + str(v['message']) + '},')
        header.append('};')
    header.append('inline constexpr FlowGlyph FlowAwardGlyphs[]={')
    for c in sorted(glyphs):
        x, y, _, _ = rects[c]
        g = glyphs[c]
        header.append(f'{{char16_t({ord(c)}),{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
    header.append('};')
    header.append(f'inline constexpr FlowFont FlowAwardFont{{{font_slot},{atlas.width},{atlas.height},'
                  f'{info["cellH"]},{info["width"]},{info["height"]},{info["linefeed"]},FlowAwardGlyphs,{len(glyphs)}}};')
    header.append('inline constexpr FlowGlyph FlowAwardRankGlyphs[]={')
    for c in sorted(rank_glyphs):
        x,y,_,_ = rank_rects[c]
        g=rank_glyphs[c]
        header.append(f'{{char16_t({ord(c)}),{x},{y},{g["width"]},{g["left"]},{g["advance"]}}},')
    header.append('};')
    header.append(f'inline constexpr FlowFont FlowAwardRankFont{{{rank_slot},{rank_atlas.width},{rank_atlas.height},'
                  f'{rank_info["cellH"]},{rank_info["width"]},{rank_info["height"]},{rank_info["linefeed"]},FlowAwardRankGlyphs,{len(rank_glyphs)}}};')
    header.append('struct FlowAwardMessage {uint32_t id;const char* text;};')
    header.append('inline constexpr FlowAwardMessage FlowAwardMessages[]={' + ','.join(
        '{' + str(i) + ',' + json.dumps(text, ensure_ascii=True) + '}' for i, text in labels.items()) + '};')
    manifest['awards'] = {'textures': textures, 'controls': [x[0] for x in layouts],
                          'sources': {str(p.relative_to(root)): digest(p) for p in sorted(set(sources))}}
