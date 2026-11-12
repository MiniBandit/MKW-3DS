#!/usr/bin/env python3
"""Cook Lakitu (jugemu) for the race: body, hair, rod, signal, lap boards,
reverse board and goal flag, their CHR0 banks, joint tracks for the retail
attachments (CON_ROD, ef_head on the body; ef_hook on the rod; USA 0x8071e0d4),
the signal CLR0 lamp colours (TEV C2) and the lap board PAT0 digit textures.
Output stays in build/vfx (overlaid by tools/resource_manifest.py).
"""
import json, os, shutil, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from cook_model import cook

OUT = ROOT / 'build/vfx'
COMMON = ROOT / 'assets/common'
TMP = ROOT / 'build/vfx/lakitu-src'


def unlit(folder):
    # The retail lightmap materials render Lakitu at full albedo brightness;
    # the runtime's single directional light darkened him to brown and grey.
    path = folder / 'model.json'
    data = json.loads(path.read_text())
    for m in data['models']:
        for shape in m['shapes']:
            shape['flags'] |= 8
    path.write_text(json.dumps(data))
    return data


def export(source, model, name):
    folder = TMP / name
    subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(source), str(folder), model], check=True, stdout=subprocess.DEVNULL,
                   env={**os.environ, 'MKW_SURFACE_ALBEDO': '1'})
    unlit(folder)
    return folder


def static(source, model, name, preserve=False):
    folder = export(source, model, name)
    if model == 'jg_shadow':
        path = folder / 'model.json'
        data = json.loads(path.read_text())
        assert all(s['texture'] == -1 and s['secondary'] == -1 for m in data['models'] for s in m['shapes'])
        data['textures'] = []
        path.write_text(json.dumps(data))
    parts = cook(folder, OUT, name, preserve_vertices=preserve)
    if len(parts) != 1:
        raise ValueError('Lakitu part needs partitioning: ' + name)
    shutil.copyfile(OUT / 'models' / (parts[0]['name'] + '.gmdl'), OUT / 'models' / (name + '.gmdl'))
    return json.loads((OUT / 'models' / (name + '-receipt.json')).read_text())


def animated(source, model, name, clip):
    # cook_race_items.cook_world, but exporting into the build-local TMP.
    folder = TMP / name
    subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(source), str(folder), model, clip, '0'], check=True,
                   stdout=subprocess.DEVNULL, env={**os.environ, 'MKW_FREEZE_NONLINEAR_UV': '1', 'MKW_RACE_MATERIALS': '1', 'MKW_FIRST_SAMPLER': '1'})
    # Preserve jugemu's original GX channels and light maps, as for racers.
    data = json.loads((folder / 'model.json').read_text())
    parts = cook(folder, OUT, name, preserve_vertices=True)
    if len(parts) != 1:
        raise ValueError('Lakitu part needs partitioning: ' + name)
    shutil.copyfile(OUT / 'models' / (parts[0]['name'] + '.gmdl'), OUT / 'models' / (name + '.gmdl'))
    result = subprocess.run(['node', str(ROOT / 'build/export_driver.mjs'), str(source), str(OUT / 'models' / (name + '.dskm')), model],
                            check=True, capture_output=True, text=True)
    vertices = sum(len(s['vertices']) for m in data['models'] for s in m['shapes'])
    return {'clips': json.loads(result.stdout)['clips'], 'triangles': vertices // 3}


def joints(source, model, names, out):
    result = subprocess.run(['node', str(ROOT / 'build/export_joints.mjs'), str(source), str(OUT / 'models' / out), model, ','.join(names)],
                            check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def main():
    (OUT / 'models').mkdir(parents=True, exist_ok=True)
    body = animated(COMMON / 'jugemu.brres', 'jugemu', 'lakitu_body', 'wait')
    rod = animated(COMMON / 'rod.brres', 'rod', 'lakitu_rod', 'wait')
    flag = animated(COMMON / 'jugem_flag.brres', 'jg_flag', 'lakitu_flag', 'jg_wy')
    hair = static(COMMON / 'jugemu.brres', 'jugemu_hair', 'lakitu_hair')
    static(COMMON / 'jugemu.brres', 'jg_shadow', 'lakitu_shadow')
    signal = static(COMMON / 'jugemu_signal.brres', 'jugemu_signal', 'lakitu_signal', preserve=True)
    lap = static(COMMON / 'jugemu_lap.brres', 'jugemu_lap', 'lakitu_lap', preserve=True)
    lapf = static(COMMON / 'jugemu_lapf.brres', 'jugemu_lapf', 'lakitu_lapf')
    reverse = static(COMMON / 'jugem_reverse.brres', 'board_reverse', 'lakitu_reverse')
    body_joints = joints(COMMON / 'jugemu.brres', 'jugemu', ['CON_ROD', 'ef_head'], 'lakitu_body.djnt')
    rod_joints = joints(COMMON / 'rod.brres', 'rod', ['ef_hook'], 'lakitu_rod.djnt')
    # Signal lamps: one submesh per shape (preserve_vertices), CLR0 target 6 = TEV C2.
    inventory = json.loads(subprocess.run(['node', str(ROOT / 'build/export_driver.mjs'), str(COMMON / 'jugemu_signal.brres'), '--inventory', 'jugemu_signal'],
                                          check=True, capture_output=True, text=True).stdout)
    lamps = ['light_1', 'light_2', 'light_3', 'lightb1', 'lightb2', 'lightb3']
    order = [m['material'] for m in signal['materials']]
    lamp_submesh = [order.index(name) for name in lamps]
    clips = ['hg_signal_light1', 'hg_signal_light2', 'hg_signal_light3', 'hg_signal_start']
    colours = []
    for clip in clips:
        c = next(x for x in inventory['clr0'] if x['name'] == clip)
        frames = []
        for f in range(31 if clip != 'hg_signal_start' else 61):
            row = []
            for lamp in lamps:
                anim = next((m for m in c['matAnimations'] if m['materialName'] == lamp), None)
                track = anim['clrAnimations'][6] if anim and len(anim['clrAnimations']) > 6 and anim['clrAnimations'][6] else None
                if track:
                    keys = track['frames']
                    value = keys.get(str(min(f, max(int(k) for k in keys))), 0)
                else:
                    value = 0x28282800
                row.append(value)
            frames.append(row)
        colours.append(frames)
    # Lap board PAT0 'jugemu_lap' (material 'number'): frame n uses jg_lap.n.
    lap_order = [m['material'] for m in lap['materials']]
    number = lap_order.index('number')
    lap_textures = [t['source'] for t in lap['textures']]
    digits = [lap_textures.index(f'jg_lap.{n}') for n in range(8)]
    h = ['#pragma once', '#include <array>', '#include <cstdint>', 'namespace mkw {',
         '// Generated by tools/cook_lakitu.py from common jugemu*.brres, rod.brres,',
         '// jugem_reverse.brres and jugem_flag.brres.',
         'inline constexpr const char* LakituBody="models/lakitu_body.gmdl";',
         'inline constexpr const char* LakituBodyBank="models/lakitu_body.dskm";',
         'inline constexpr const char* LakituBodyJoints="models/lakitu_body.djnt"; // CON_ROD, ef_head',
         'inline constexpr const char* LakituHair="models/lakitu_hair.gmdl";',
         'inline constexpr const char* LakituRod="models/lakitu_rod.gmdl";',
         'inline constexpr const char* LakituRodBank="models/lakitu_rod.dskm";',
         'inline constexpr const char* LakituRodJoints="models/lakitu_rod.djnt"; // ef_hook',
         'inline constexpr const char* LakituFlag="models/lakitu_flag.gmdl";',
         'inline constexpr const char* LakituFlagBank="models/lakitu_flag.dskm";',
         'inline constexpr const char* LakituSignal="models/lakitu_signal.gmdl";',
         'inline constexpr const char* LakituLap="models/lakitu_lap.gmdl";',
         'inline constexpr const char* LakituLapFinal="models/lakitu_lapf.gmdl";',
         'inline constexpr const char* LakituReverse="models/lakitu_reverse.gmdl";',
         '// Signal lamps light_1..3, lightb1..3 (TEV C2 RGBA per CLR0 frame).',
         f'inline constexpr std::array<uint8_t,6> LakituLampSubmesh{{{{{",".join(map(str, lamp_submesh))}}}}};']
    for i, (clip, frames) in enumerate(zip(clips, colours)):
        h.append(f'inline constexpr std::array<std::array<uint32_t,6>,{len(frames)}> LakituSignal{i}{{{{ // {clip}')
        h += ['    {{' + ','.join(f'0x{v & 0xffffffff:08x}u' for v in row) + '}},' for row in frames]
        h.append('}};')
    reverse_inventory = json.loads(subprocess.run(['node', str(ROOT / 'build/export_driver.mjs'), str(COMMON / 'jugem_reverse.brres'), '--inventory', 'board_reverse'], check=True, capture_output=True, text=True).stdout)
    reverse_arrow = [m['material'] for m in reverse['materials']].index('yajirushi')
    reverse_track = next(m for m in reverse_inventory['clr0'][0]['matAnimations'] if m['materialName'] == 'yajirushi')['clrAnimations']
    h.append(f'inline constexpr unsigned LakituReverseArrowSubmesh={reverse_arrow};')
    for target,index in [('Colour',4),('Add',6)]:
        values = [reverse_track[index]['frames'][str(f)] for f in range(60)]
        h.append(f'inline constexpr std::array<uint32_t,60> LakituReverse{target}{{{{'+','.join(f'0x{v:08x}u' for v in values)+'}};')
    h += [f'inline constexpr unsigned LakituLapNumberSubmesh={number};',
          f'inline constexpr std::array<int16_t,8> LakituLapDigit{{{{{",".join(map(str, digits))}}}}}; // PAT0 frame -> GMDL texture',
          '}', '']
    (ROOT / 'include/mkw/lakitu_assets.hpp').write_text('\n'.join(h))
    print(json.dumps({'body': body['clips'], 'rod': rod['clips'], 'flag': flag['clips'], 'body_joints': body_joints['clips'],
                      'lamp_submesh': lamp_submesh, 'number': number, 'digits': digits,
                      'models': {n: r['triangles'] for n, r in [('body', body), ('rod', rod), ('flag', flag)]}}, indent=1))


if __name__ == '__main__':
    main()
