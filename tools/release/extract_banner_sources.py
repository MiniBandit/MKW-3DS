#!/usr/bin/env python3
"""Extract original Mario/Standard Kart banner inputs from an owner's RMCE01 disc.

This explicit banner-only stage does not compile the game or consume CGFX.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    'mario-standard': 'Race/Kart/mdf_kart-mr.szs',
    'menu-driver': 'Scene/Model/Driver.szs',
    'menu-kart': 'Scene/Model/Kart/mr-allkart.szs',
    'common': 'Race/Common.szs',
    'menu-lighting': 'Scene/UI/MenuSingle.szs',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Use an output directory outside the publication repository.')
    if not args.disc.is_file():
        parser.error('The supplied disc image does not exist.')
    # Import the retained, read-only disc parser only after validating arguments.
    sys.path.insert(0, str(ROOT / 'tools'))
    from extract_disc import Disc
    image = Disc(args.disc)
    try:
        if image.read(0, 6) != b'RMCE01':
            parser.error('The banner recipe requires a USA RMCE01 disc.')
    finally:
        image.f.close()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {'disc_id': 'RMCE01', 'sources': {}}
    for name, source in SOURCES.items():
        archive = output / 'archives' / (name + '.szs')
        archive.parent.mkdir(exist_ok=True)
        subprocess.run([sys.executable, str(ROOT/'tools/extract_disc.py'),
                        str(args.disc.resolve()), str(output/'inventory'),
                        '--file', source, '--to', str(archive)], check=True)
        subprocess.run([sys.executable, str(ROOT/'tools/unpack_szs.py'),
                        str(archive), str(output/'unpacked'/name)], check=True)
        receipt['sources'][source] = {'archive': str(archive.relative_to(output)),
                                     'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}
    (output/'banner-source-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('Extracted original banner model, animation, assembly and lighting inputs.')
    print('CGFX conversion and HOME packaging are separate steps.')


if __name__ == '__main__':
    main()
