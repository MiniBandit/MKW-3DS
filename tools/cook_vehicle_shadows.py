#!/usr/bin/env python3
"""Export the original retail vehicle shadow volumes, without texture baking.

Reads the already extracted kart_model.brres archives. Meshes go in a small
overlay; the existing garage and race assets remain unchanged.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from cook_model import cook
from cook_garage import VEHICLES_PER_DRIVER
from cook_roster import ROSTER


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    garage = ROOT / 'build/roster/models/garage.json'
    rows = json.loads(garage.read_text())['choices']
    expected_count = len(ROSTER) * VEHICLES_PER_DRIVER
    if len(rows) != expected_count or len({r['prefix'] for r in rows}) != expected_count:
        raise ValueError(f'Expected all {expected_count} supported retail driver/vehicle combinations')
    output = ROOT / 'build/vehicle-shadows'
    work = ROOT / 'build/vehicle-shadow-work'
    (output / 'models').mkdir(parents=True, exist_ok=True)

    def export(row):
        name = row['prefix'] + 'shadow'
        if not row['prefix'] or any(c not in 'abcdefghijklmnopqrstuvwxyz_' for c in row['prefix']):
            raise ValueError('Invalid garage prefix')
        archive = None
        for base in (ROOT / 'assets/vehicles', ROOT / 'build/roster-work/vehicles'):
            candidate = base / row['prefix'].rstrip('_') / 'kart_model.brres'
            if candidate.is_file():
                archive = candidate
                break
        if archive is None:
            raise FileNotFoundError('Missing extracted vehicle: ' + row['prefix'])
        source_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        if source_hash != row['kart_sha256']:
            raise ValueError('Vehicle source changed: ' + str(archive))
        destination = work / name
        destination.mkdir(parents=True, exist_ok=True)
        with (destination / 'export.log').open('w') as log:
            subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'),
                            str(archive), str(destination), 'shadow'],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        data = json.loads((destination / 'model.json').read_text())
        if not data['models'] or any(m['name'] != 'shadow' for m in data['models']):
            raise ValueError('Missing original shadow model: ' + row['prefix'])
        # Shadow volume counting uses only source geometry. Its material has
        # no samplers; unrelated kart textures must not enter this overlay.
        for model in data['models']:
            for shape in model['shapes']:
                if shape['texture'] != -1 or shape['secondary'] != -1:
                    raise ValueError('Unexpected textured shadow: ' + row['prefix'])
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
        result = {'prefix': row['prefix'], 'vehicle': row['vehicle'],
                  'source': str(archive.relative_to(ROOT)), 'source_sha256': source_hash,
                  'triangles': expected, 'bytes': target.stat().st_size,
                  'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
        print(row['prefix'], expected, 'original shadow triangles', flush=True)
        return result

    start = time.monotonic()
    with ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 3))) as pool:
        results = list(pool.map(export, rows))
    manifest = {'version': 1, 'method': 'original retail shadow volume geometry',
                'elapsed_seconds': round(time.monotonic() - start, 2), 'choices': results}
    (output / 'models/vehicle-shadows.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
