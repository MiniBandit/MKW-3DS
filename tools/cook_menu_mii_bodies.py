#!/usr/bin/env python3
"""Convert original menu Mii bodies, retaining their animated face_1 joint.

MenuDriverModelMgr uses a separate MiiHeadsModel for the chosen identity.
These are body assets only: they must not be shown as a complete Mii until
identity, head attachment, body colour and native sizing are connected.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
from cook_model import cook

ROOT = Path(__file__).resolve().parents[1]
STEMS = tuple(f'{size}{outfit}_mii_{sex}' for size in 'sml'
              for outfit in 'ab' for sex in 'mf')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/menu-mii-bodies')
    args = parser.parse_args()
    output = args.output.resolve()
    (output/'models').mkdir(parents=True, exist_ok=True)
    # Menu and common parameter tables are identical in the original disc.
    params = (ROOT/'assets/common/miiDispParam.bin').read_bytes()
    if params != (ROOT/'assets/menu-models/MiiBody/miiDispParam.bin').read_bytes():
        raise ValueError('Menu/common Mii head tables differ; inspect original binding first')
    if len(params) != 448 or struct.unpack_from('>I', params)[0] != 37:
        raise ValueError('Unexpected original Mii display table')
    records = []
    for stem in STEMS:
        source = ROOT/'assets/menu-models/Driver'/f'{stem}.brres'
        source_hash = sha(source)
        prefix = f'ui_mii_{stem}'
        work = output/'exports'/stem
        env = {**os.environ, 'MKW_MENU_MATERIALS': '1'}
        subprocess.run(['node', str(ROOT/'build/export_brres.mjs'), str(source),
                        str(work), 'model', 'sel_wait', '0'], env=env,
                       check=True, capture_output=True, text=True)
        parts = cook(work, output, prefix, max_texture=256, preserve_vertices=True)
        if len(parts) != 1:
            raise ValueError(f'{stem}: expected one animated body model')
        # Preserve the original independently recoloured material regions.
        # Cooked submesh ranges must match the source shapes before recording
        # roles; never apply a whole-model tint to clothes, shoes and skin.
        model_data = json.loads((work/'model.json').read_text())
        binary = (output/'models'/(parts[0]['name']+'.gmdl')).read_bytes()
        version, _, _, sub_count, _ = struct.unpack_from('<5I', binary, 4)
        stride = {3:44, 4:56, 5:92}[version]
        shapes = model_data['models'][0]['shapes']
        if len(shapes) != sub_count:
            raise ValueError(f'{stem}: merged material regions need an explicit mapping')
        roles = []
        first = 0
        for index, shape in enumerate(shapes):
            offset, count = struct.unpack_from('<2I', binary, 24+index*stride)
            if offset != first or count != len(shape['vertices']):
                raise ValueError(f'{stem}: material range/animation mismatch')
            first += count
            roles.append({'material': shape['material'], 'first': offset, 'count': count,
                          'role': {'mii_color': 'clothing', 'mii_skin': 'skin'}.get(shape['material'], 'fixed')})
            if roles[-1]['role'] != 'fixed' and not shape['retail']['word'] & (1 << 24):
                raise ValueError(f'{stem}: dynamic Mii colour equation was not converted')
        bank = output/'models'/f'{prefix}.dskm' 
        result = subprocess.run(['node', str(ROOT/'build/export_driver.mjs'),
                                 str(source), str(bank), 'model'], check=True,
                                capture_output=True, text=True)
        animation = json.loads(result.stdout)
        inventory = json.loads(subprocess.run(
            ['node', str(ROOT/'build/export_driver.mjs'), str(source), '--inventory', 'model'],
            check=True, capture_output=True, text=True).stdout)
        head = next(n for n in inventory['nodes'] if n['name'] == 'face_1')
        if animation['vertices'] != parts[0]['vertices']:
            raise ValueError(f'{stem}: model/bank vertex order mismatch')
        if not {'sel_wait', 'sel_gut'} <= {c['name'] for c in animation['clips']}:
            raise ValueError(f'{stem}: missing original menu animation')
        if sha(source) != source_hash:
            raise ValueError(f'{stem}: original source changed')
        retail_id = 24 + 'sml'.index(stem[0])*6 + 'ab'.index(stem[1])*2 + 'mf'.index(stem[-1])
        head_scale, head_y, head_x = struct.unpack_from('>3f', params, 4+(retail_id-24)*12)
        record = {'retailCharacter': retail_id,
                  'headTransform': {'scale': head_scale, 'translation': [head_x, -head_y, 0],
                                    'rotation': [0, -1.5707963705062866, 3.1415927410125732]},
                  'stem': stem, 'size': stem[0], 'outfit': stem[1], 'sex': stem[-1],
                  'source': str(source.relative_to(ROOT)), 'sourceSha256': source_hash,
                  'model': parts[0], 'materialRoles': roles, 'animation': animation, 'faceJoint': head,
                  'animationSha256': sha(bank)}
        records.append(record)
        print(f'{stem}: {parts[0]["vertices"]} vertices; original wait/confirm and face_1 retained', flush=True)
    receipt = {'models': records, 'boundary': 'Body resource candidate only. Identity head, colour, sizing, runtime loading/drawing and target validation remain required.'}
    (output/'menu-mii-bodies.json').write_text(json.dumps(receipt, indent=2)+'\n')
    rows = []
    for record in records:
        for role in record['materialRoles']:
            if role['role'] != 'fixed':
                rows.append(' {"%s",%d,%d,%s},' % (record['stem'], role['first'], role['count'], 'true' if role['role'] == 'skin' else 'false'))
    (ROOT/'include/mkw/menu_mii_materials.hpp').write_text(
        '#pragma once\n// Generated from cook_menu_mii_bodies materialRoles; ranges checked against original shapes.\nnamespace mkw {\n'
        'struct MenuMiiMaterial {const char* stem;unsigned first,count;bool skin;};\n'
        'inline constexpr MenuMiiMaterial MenuMiiMaterials[]={\n'+'\n'.join(rows)+'\n};\n}\n')


if __name__ == '__main__':
    main()
