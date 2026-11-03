"""Cook the original DemoJugemu tail and its animated root attachment."""
import json
import struct
import subprocess
from pathlib import Path
from cook_race_effects import Emitter, entries, write_bank
from cook_race_flow import f

ROOT = Path(__file__).resolve().parents[1]

def cook_effects(source, output):
    scene = source / 'unpacked/winningrun_demo'
    order, records, notes, namespaces, archives = [], {}, [], {}, {}
    for resource, roots, namespace in (
            ('DemoJugemu/rk_DemoJugemu', ['rk_JugemTail'], 'award_'),
            ('DemoEf/rk_demo', ['rk_demoHanabi'], 'awfire_'),
            ('Kamifubuki/rk_kamifubuki', ['rk_kamifubuki', 'rk_kamifubukiLast'], 'awpaper_')):
        archive = scene / 'effect' / (resource + '.breff')
        data, pixels = archive.read_bytes(), archive.with_suffix('.breft').read_bytes()
        index = entries(data)
        archives[namespace] = (pixels, entries(pixels))
        queue = list(roots)
        while queue:
            name = queue.pop(0)
            if name in records:
                continue
            if name not in index:
                raise ValueError('Missing ceremony emitter ' + name)
            records[name] = Emitter(data, index, name)
            namespaces[name] = namespace
            order.append(name)
            queue.extend(records[name].references())
    write_bank(order, records, namespaces, archives, output / 'textures', notes,
               'award-effects.bin')
    if notes:
        raise ValueError('Incomplete ceremony effects: ' + '; '.join(notes))
    archive = source / 'unpacked/Award/Effect/rk_trophy.breff'
    data, pixels = archive.read_bytes(), archive.with_suffix('.breft').read_bytes()
    index = entries(data)
    trophy_order, trophy_records, trophy_notes = [], {}, []
    queue = ['rk_trophyGold', 'rk_trophySilver', 'rk_trophyCopper']
    while queue:
        name = queue.pop(0)
        if name in trophy_records:
            continue
        trophy_records[name] = Emitter(data, index, name)
        trophy_order.append(name)
        queue.extend(trophy_records[name].references())
    write_bank(trophy_order, trophy_records, {name: 'awcup_' for name in trophy_order},
               {'awcup_': (pixels, entries(pixels))}, output / 'textures', trophy_notes,
               'award-trophy-effects.bin')
    if trophy_notes:
        raise ValueError('Incomplete trophy effects: ' + '; '.join(trophy_notes))
    # Retail DemoJugemu uses root translation, not the head attachment of the
    # in-race Lakitu. Preserve the authored CHR0 flight in model space.
    joint_file = source / 'demojugemu-root.djnt'
    subprocess.run(['node', str(ROOT / 'build/export_joints.mjs'),
                    str(scene / 'DemoJugemu.brres'), str(joint_file),
                    'DemoJugemu', 'root'], check=True, capture_output=True)
    joints = joint_file.read_bytes()
    magic, version, count, clips = struct.unpack_from('<4s3I', joints)
    if (magic, version, count) != (b'DJNT', 1, 1):
        raise ValueError('Unexpected DemoJugemu joints')
    offset, positions = 48, None
    for _ in range(clips):
        name = joints[offset:offset+32].split(b'\0')[0].decode()
        duration, loop = struct.unpack_from('<2I', joints, offset+32)
        offset += 40
        if name == 'o_jg-demowin':
            if loop or duration != 401:
                raise ValueError('Unexpected DemoJugemu flight')
            positions = [struct.unpack_from('<12f', joints, offset+i*48) for i in range(duration)]
        offset += duration*48
    if positions is None or offset != len(joints):
        raise ValueError('Missing DemoJugemu flight root')
    header = ['// Original DemoJugemu o_jg-demowin root translations.',
              '#pragma once', '#include <mkw/math.hpp>', 'namespace mkw {',
              'inline constexpr Vec3 AwardJugemuRoots[]={']
    header += ['{' + ','.join(f(matrix[i]) for i in (3, 7, 11)) + '},' for matrix in positions]
    header += ['};', '}']
    (ROOT / 'include/mkw/award_effect_anchors.hpp').write_text('\n'.join(header)+'\n')
    print(f'Cooked ceremony effects: {len(order)} emitters, {len(positions)} root frames')

if __name__ == '__main__':
    output = ROOT / 'build/award-models'
    cook_effects(ROOT / 'build/award-source', output)
    # Standalone incremental cook updates the existing package receipt too.
    import hashlib
    path = output / 'award-models.json'
    receipt = json.loads(path.read_text())
    receipt['files'] = {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                        for folder in ('models', 'textures') for p in sorted((output/folder).rglob('*')) if p.is_file()}
    path.write_text(json.dumps(receipt, indent=2)+'\n')
