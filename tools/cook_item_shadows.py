#!/usr/bin/env python3
"""Export the original retail item and item-box shadow volumes, without textures.

Every ItemObj loads a `<name>_s.brres` shadow volume beside its model (fn_1_28FC8C,
PAL 0x807A0040; names at 0x808A5440-0x808A6280): koura_s for shells, Bob-ombs, Fake Item
Boxes, Stars, Mushrooms and the Bullet, banana_s for Bananas, kumo_s for the Thunder Cloud.
The course item box carries `itembox-shadow` (ObjItemBox, PAL 0x80828860). All register
with the shadow-volume manager the karts and course objects use (0x809C1B40).

Like tools/cook_vehicle_shadows.py, the meshes go in a small overlay with new names; the
existing race item assets remain unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from cook_model import cook

SHADOWS = {
    'item_shadow_koura': ('assets/common/koura_s.brres', 'koura_s'),
    'item_shadow_banana': ('assets/common/banana_s.brres', 'banana_s'),
    'item_shadow_kumo': ('assets/common/kumo_s.brres', 'kumo_s'),
    'item_box_shadow': ('assets/luigi/itembox.brres', 'itembox-shadow'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', type=Path, default=ROOT / 'build/export_brres.mjs')
    args = parser.parse_args()
    output = ROOT / 'build/item-shadows'
    work = ROOT / 'build/item-shadow-work'
    (output / 'models').mkdir(parents=True, exist_ok=True)
    manifest = {'version': 1, 'models': []}
    for name, (source, model) in SHADOWS.items():
        archive = ROOT / source
        destination = work / name
        destination.mkdir(parents=True, exist_ok=True)
        with (destination / 'export.log').open('w') as log:
            subprocess.run(['node', str(args.exporter), str(archive), str(destination), model],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        data = json.loads((destination / 'model.json').read_text())
        if not data['models'] or any(m['name'] != model for m in data['models']):
            raise ValueError('Missing original shadow model: ' + model)
        # Shadow volume counting uses only source geometry; the materials have no samplers.
        for m in data['models']:
            for shape in m['shapes']:
                if shape['texture'] != -1 or shape['secondary'] != -1:
                    raise ValueError('Unexpected textured shadow: ' + model)
        data['textures'] = []
        (destination / 'model.json').write_text(json.dumps(data) + '\n')
        parts = cook(destination, output, name)
        if len(parts) != 1:
            raise ValueError('Shadow must fit one native model: ' + name)
        expected = sum(len(s['vertices']) // 3 for m in data['models'] for s in m['shapes'])
        if parts[0]['triangles'] != expected:
            raise ValueError('Original shadow triangles were lost: ' + name)
        target = output / 'models' / (name + '.gmdl')
        shutil.copyfile(output / 'models' / (parts[0]['name'] + '.gmdl'), target)
        manifest['models'].append({'path': 'models/' + target.name, 'source': source, 'model': model,
                                   'source_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                                   'triangles': expected, 'bytes': target.stat().st_size,
                                   'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    (output / 'models/item-shadows.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
