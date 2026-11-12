#!/usr/bin/env python3
"""Prepare original ceremony archives and camera/route data from the local disc."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from cook_roster import data_partition
from extract_disc import Disc
from import_courses import kmp_database
from unpack_szs import unpack

ROOT = Path(__file__).resolve().parents[1]
SCENES = ('winningrun_demo', 'loser_demo', 'draw_demo', 'ending_demo')


def prepare(disc, output):
    inventory = json.loads((ROOT / 'data/extracted/disc-files.json').read_text())
    files = {f['name']: f for f in inventory['files']}
    wanted = sorted(n for n in files if n.startswith('Demo/'))
    wanted += ['Race/Course/' + name + '.szs' for name in SCENES]
    partition = None
    sources = {}
    for name in wanted:
        entry = files[name]
        path = output / name
        if not path.is_file() or path.stat().st_size != entry['size']:
            if partition is None:
                image = Disc(disc)
                if image.read(0, 6) != b'RMCE01':
                    raise ValueError('Ceremony assets require the USA disc')
                partition = data_partition(image)
            data = partition.read(entry['offset'], entry['size'])
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        sources[name] = {'bytes': path.stat().st_size,
                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        if path.suffix == '.szs':
            folder = output / 'unpacked' / path.stem
            unpack(path, folder)
    scenes = {}
    for name in SCENES:
        folder = output / 'unpacked' / name
        scenes[name] = {'archive': 'Race/Course/' + name + '.szs',
                        'kmp': kmp_database(folder / 'course.kmp'),
                        'resources': sorted(p.relative_to(folder).as_posix()
                                            for p in folder.rglob('*') if p.is_file())}
    demo = output / 'unpacked/Award'
    result = {'version': 1, 'sources': sources, 'scenes': scenes,
              'ceremonyResources': sorted(p.relative_to(demo).as_posix()
                                          for p in demo.rglob('*') if p.is_file())}
    # Export scene bindings, camera tables and GP times as separate manual operations.
    (output / 'award-sources.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'Prepared {len(sources)} original archives/models and {len(scenes)} scene descriptions')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disc', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/award-source')
    args = parser.parse_args()
    prepare(args.disc, args.output)


if __name__ == '__main__':
    main()
