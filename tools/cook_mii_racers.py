#!/usr/bin/env python3
"""Cook Mii A/B race bodies and machines from the owner's original disc.

Does not contain or generate a person's Mii identity. The race picker supplies
that separately on the console. Existing 24-character garage stays untouched.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import struct
from cook_garage import VEHICLES, EXTRA_NAMES, inspect_driver, inspect_kart, cook_choice
from cook_roster import ROOT, data_partition, sha256
from extract_disc import Disc
from unpack_szs import unpack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disc', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--extract-only', action='store_true')
    parser.add_argument('--sample', action='store_true', help='Only medium male outfit A / standard kart')
    args = parser.parse_args()
    inventory = json.loads((ROOT/'data/extracted/disc-files.json').read_text())
    files = {row['name']: row for row in inventory['files']}
    source = ROOT/'assets/mii-racers'
    source.mkdir(parents=True, exist_ok=True)
    display = (ROOT/'assets/common/kartDriverDispParam.bin').read_bytes()
    if struct.unpack_from('>II', display) != (36, 48):
        raise ValueError('Unexpected original display parameter dimensions')
    disc = Disc(args.disc)
    choices = []
    try:
        partition = data_partition(disc)
        for weight, size in enumerate('sml'):
            for outfit in range(2):
                for sex in range(2):
                    code = f"{size}{'ab'[outfit]}_mii_{'mf'[sex]}"
                    character = 24+weight*6+outfit*2+sex
                    for slot, (vehicle, name, stem) in enumerate(VEHICLES[size]):
                        if args.sample and (code != 'ma_mii_m' or slot != 0):
                            continue
                        original = f'Race/Kart/{stem}-{code}.szs'
                        entry = files[original]
                        compressed = source/Path(original).name
                        compressed.write_bytes(partition.read(entry['offset'], entry['size']))
                        prefix = f'mii_{code}_{stem}_'
                        archive = source/prefix.rstrip('_')
                        unpack(compressed, archive)
                        driver = inspect_driver(archive)
                        kart = inspect_kart(archive)
                        seat_y, seat_z = struct.unpack_from('>2f', display, 8+(vehicle*48+character)*56)
                        choices.append(dict(driverIndex=weight*4+outfit*2+sex, slot=slot,
                            driver=code, driverSlug=code, name=name, prefix=prefix,
                            character=character, vehicle=vehicle, bike=vehicle>=18,
                            seatY=seat_y, seatZ=seat_z, archive=original,
                            archive_sha256=sha256(compressed), driver_sha256=sha256(archive/'driver_model.brres'),
                            kart_sha256=sha256(archive/'kart_model.brres'), clips=driver['chr0'],
                            parts=[m['model'] for m in kart['nodes']]))
    finally:
        disc.f.close()
    audit = dict(version=1, identity=inventory['identity'], sector_h0_verified=True,
                 choices=choices, extraNames=EXTRA_NAMES, sample=args.sample)
    (source/('sample-audit.json' if args.sample else 'audit.json')).write_text(json.dumps(audit, indent=2)+'\n')
    if args.extract_only:
        return
    with ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 6))) as workers:
        cooked = list(workers.map(lambda c: cook_choice(c, args.output,
            source_root=source, export_root=ROOT/'assets/mii-racer-parts'), choices))
    target=args.output/'models'/('mii-garage-sample.json' if args.sample else 'mii-garage.json')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(dict(audit, choices=cooked), indent=2)+'\n')

if __name__ == '__main__':
    main()
