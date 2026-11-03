#!/usr/bin/env python3
"""Cook the two original item shadow volumes from already extracted Common.szs."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from cook_model import cook

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    output = ROOT / 'build/item-shadows'
    (output / 'models').mkdir(parents=True, exist_ok=True)
    entries = []
    for archive, name in [('koura_s', 'item_shadow_shell'), ('banana_s', 'item_shadow_banana')]:
        source = ROOT / 'assets/common' / (archive + '.brres')
        work = ROOT / 'build/item-shadow-work' / name
        work.mkdir(parents=True, exist_ok=True)
        subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(source),
                        str(work), archive], check=True, stdout=subprocess.DEVNULL)
        data = json.loads((work / 'model.json').read_text())
        if not data['models'] or any(m['name'] != archive for m in data['models']):
            raise ValueError('Missing original item shadow: ' + archive)
        shapes = [s for m in data['models'] for s in m['shapes']]
        if any(s['texture'] != -1 or s['secondary'] != -1 for s in shapes):
            raise ValueError('Unexpected textured shadow: ' + archive)
        data['textures'] = []
        (work / 'model.json').write_text(json.dumps(data) + '\n')
        parts = cook(work, output, name)
        triangles = sum(len(s['vertices']) // 3 for s in shapes)
        if len(parts) != 1 or parts[0]['triangles'] != triangles:
            raise ValueError('Original shadow geometry changed: ' + archive)
        target = output / 'models' / (name + '.gmdl')
        shutil.copyfile(output / 'models' / (parts[0]['name'] + '.gmdl'), target)
        entries.append({'source': str(source.relative_to(ROOT)), 'source_sha256': digest(source),
                        'path': str(target.relative_to(output)), 'sha256': digest(target),
                        'triangles': triangles, 'bytes': target.stat().st_size})
    receipt = {'version': 1, 'method': 'original closed item shadow volumes', 'models': entries}
    (output / 'models/item-shadows.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Cooked', sum(e['triangles'] for e in entries), 'original item shadow triangles;',
          sum(e['bytes'] for e in entries), 'bytes; no textures')


if __name__ == '__main__':
    main()
