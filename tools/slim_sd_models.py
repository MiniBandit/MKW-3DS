#!/usr/bin/env python3
"""Move loose files that the SD package's packs already hold out of it.

The game reads every packed file from its pack (src/asset_pack.cpp: pack::read,
and fopen through openFile), so a loose copy of a packed entry is never opened.
Leaving them on the card still costs time: the 3DS looks a file up by scanning
its folder entry by entry, so every remaining loose open in that folder gets
slower with each extra file. An SD folder copied over an older one keeps the
older one's loose files; this finds them. Saves and logs are never packed, so
they always stay.

Run it after the packs are final. Without --move-to it only reports. Moved
files keep their paths, so moving them back restores the package exactly.

    python3 tools/slim_sd_models.py D:/3ds/MKWiiDS
    python3 tools/slim_sd_models.py D:/3ds/MKWiiDS --move-to C:/backup/mkw-duplicates
"""
import argparse, shutil, struct
from pathlib import Path

def pack_names(path):
    with path.open('rb') as f:
        header = f.read(12)
        if header[:4] not in (b'MPK1', b'MPK2'):
            raise SystemExit(f'{path}: not an MPK1 or MPK2 archive')
        count, names_bytes = struct.unpack('<II', header[4:])
        f.seek(12 + (16 if header[:4] == b'MPK2' else 12) * count)
        blob = f.read(names_bytes)
    return {n.decode() for n in blob.split(b'\0') if n}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('package', type=Path, help='the SD folder holding menu.pak, race.pak and stream.pak')
    parser.add_argument('--move-to', type=Path, help='move the duplicates into this folder (outside the package)')
    args = parser.parse_args()
    packed = set()
    for archive in ('menu.pak', 'race.pak', 'stream.pak'):
        path = args.package / archive
        if path.is_file():
            packed |= pack_names(path)
        elif archive != 'stream.pak':
            raise SystemExit(f'{path} is missing: the packs must be in place first')
    loose = sorted(p for p in args.package.rglob('*') if p.is_file())
    duplicates = [p for p in loose if p.relative_to(args.package).as_posix() in packed]
    kept = len(loose) - len(duplicates)
    size = sum(p.stat().st_size for p in duplicates)
    print(f'{len(duplicates)} packed duplicates ({size / 2**20:.1f} MiB); {kept} files stay')
    if not args.move_to:
        print('report only; pass --move-to to move them')
        return
    target = args.move_to.resolve()
    if args.package.resolve() in target.parents or target == args.package.resolve():
        raise SystemExit('--move-to must be outside the package')
    for p in duplicates:
        if (target / p.relative_to(args.package)).exists():
            raise SystemExit(f'{target / p.relative_to(args.package)} already exists')
    for p in duplicates:
        destination = target / p.relative_to(args.package)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(p), str(destination))
    print(f'moved to {target}')


if __name__ == '__main__':
    main()
