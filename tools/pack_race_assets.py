#!/usr/bin/env python3
"""Pack race-time models, animation banks (incl. course-object .dobj) and textures into race.pak.

Same MPK1 format and rationale as tools/pack_menu_assets.py: loading a race
opens hundreds of small files (track parts, twelve racers, items and HUD), and
each open/close costs far more than its read. The selection is exactly the
explicit user-supplied JSON mapping. Additional archive merges are separate manual steps.
"""
import argparse, hashlib, json, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'sd-card/3ds/mkw-3ds'


def menu_pak_names(path):
    if not path.is_file():
        raise FileNotFoundError(path)
    data = path.read_bytes()
    count, names_bytes = struct.unpack_from('<II', data, 4)
    blob = data[12 + 12 * count:12 + 12 * count + names_bytes]
    return {n.decode() for n in blob.split(b'\0') if n}


def race_packable(name):
    """Model payloads use pack::read; fopen-scanned .list files stay loose."""
    return ((name.startswith('models/') and name.endswith(('.gmdl', '.dskm', '.dobj')))
            or (name.startswith('textures/') and name.endswith(('.t3x', '.bin'))))


def main():
    parser=argparse.ArgumentParser(description='Pack one explicitly supplied resource map; no automatic merges.')
    parser.add_argument('manifest',type=Path,help='JSON object mapping runtime-relative names to existing source files')
    parser.add_argument('output',type=Path)
    parser.add_argument('--exclude-menu',type=Path,help='Existing menu.pak; must be explicitly supplied')
    args=parser.parse_args()
    listing=json.loads(args.manifest.read_text())
    packed_menu=menu_pak_names(args.exclude_menu) if args.exclude_menu else set()
    names = sorted(rel for rel in listing if race_packable(rel))
    names = [n for n in names if n not in packed_menu]
    blob = b''.join(n.encode() + b'\0' for n in names)
    header = 12 + 12 * len(names) + len(blob)
    offset = (header + 31) & ~31
    entries = []
    output = args.output
    with output.open('wb') as out:
        sizes = [Path(listing[n]).stat().st_size for n in names]
        name_offset = 0
        for n, size in zip(names, sizes):
            entries.append((name_offset, offset, size))
            name_offset += len(n.encode()) + 1
            offset += (size + 31) & ~31
        if offset >= 1 << 31:
            raise SystemExit('race.pak exceeds the 2 GiB seek range')
        out.write(b'MPK1' + struct.pack('<II', len(names), len(blob)))
        for entry in entries:
            out.write(struct.pack('<III', *entry))
        out.write(blob)
        out.write(b'\0' * (((header + 31) & ~31) - header))
        for n, size in zip(names, sizes):
            data = Path(listing[n]).read_bytes()
            out.write(data + b'\0' * (((size + 31) & ~31) - size))
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    receipt = {'version': 1, 'files': len(names), 'bytes': output.stat().st_size, 'sha256': digest}
    output.with_suffix(output.suffix+'.json').write_text(json.dumps(receipt, indent=1) + '\n')
    print(json.dumps(receipt, indent=1))


if __name__ == '__main__':
    main()
