#!/usr/bin/env python3
"""Add compact opponent animation banks (.dsku) to race.pak.

Each DSKM keyframe stores, per bone, a 3x4 position matrix and a 3x3 normal
matrix (84 bytes). Opponents read their normals once: Model::freezeLighting
bakes the first clip's first frame into vertex colours, then replaces every
normal with a bone index. A .dsku keeps that one frame's normal matrices and
drops the rest (36 of every 84 bytes), so an opponent's bank reads about 43%
fewer bytes. DriverAnimation::load expands it back to the DSKM layout in
memory, filling every frame's normal part with the kept frame's.

Only driver banks get a .dsku; the player and the award ceremony keep loading
the full .dskm. The output pack holds every original entry unchanged plus
models/<name>.dsku next to each models/<name>driver.dskm.

    python3 tools/pack_unlit_banks.py race.pak race-unlit.pak
    python3 tools/pack_unlit_banks.py --check race.pak    (round-trip every bank, write nothing)
"""
import argparse, struct, sys
from pathlib import Path

ALIGN = 32


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def parse(data):
    """DSKM layout as DriverAnimation::load reads it: the sizes needed to re-lay clip frames."""
    if data[:4] != b'DSKM':
        raise ValueError('not DSKM')
    version = u32(data, 4)
    vertices, matrices, clips, shapes, faces = (u32(data, 8 + 4 * i) for i in range(5))
    cursor = 32 if version >= 5 else 28
    cursor += vertices * 28 + shapes * 8
    clip_rows = []
    for _ in range(clips):
        frames = u32(data, cursor + 40)
        clip_rows.append((cursor, frames))
        cursor += 44 + frames * (matrices * 84 + shapes * 4)
    if cursor > len(data):
        raise ValueError('truncated clips')
    return version, matrices, shapes, clip_rows, cursor


def compact(data):
    version, matrices, shapes, clip_rows, clips_end = parse(data)
    if not clip_rows or clip_rows[0][1] == 0:
        raise ValueError('no first frame')
    head_end = clip_rows[0][0]
    first = clip_rows[0][0] + 44
    rest_normals = b''.join(data[first + b * 84 + 48:first + b * 84 + 84] for b in range(matrices))
    out = [b'DSKU', data[4:head_end], rest_normals]
    frame_full = matrices * 84 + shapes * 4
    for start, frames in clip_rows:
        out.append(data[start:start + 44])
        for f in range(frames):
            base = start + 44 + f * frame_full
            out.append(b''.join(data[base + b * 84:base + b * 84 + 48] for b in range(matrices)))
            out.append(data[base + matrices * 84:base + frame_full])
    out.append(data[clips_end:])
    return b''.join(out)


def expand(dsku, matrices_hint=None):
    """The runtime's expansion (DriverAnimation::load), for the round-trip check."""
    if dsku[:4] != b'DSKU':
        raise ValueError('not DSKU')
    version = u32(dsku, 4)
    vertices, matrices, clips, shapes, faces = (u32(dsku, 8 + 4 * i) for i in range(5))
    cursor = (32 if version >= 5 else 28) + vertices * 28 + shapes * 8
    out = [b'DSKM', dsku[4:cursor]]
    rest = dsku[cursor:cursor + matrices * 36]
    cursor += matrices * 36
    for _ in range(clips):
        header = dsku[cursor:cursor + 44]
        frames = u32(dsku, cursor + 40)
        out.append(header)
        cursor += 44
        for _ in range(frames):
            for b in range(matrices):
                out.append(dsku[cursor + b * 48:cursor + b * 48 + 48])
                out.append(rest[b * 36:b * 36 + 36])
            cursor += matrices * 48
            out.append(dsku[cursor:cursor + shapes * 4])
            cursor += shapes * 4
    out.append(dsku[cursor:])
    return b''.join(out)


def same_except_normals(original, expanded):
    """True when the two differ only in normal parts after the first clip's first frame."""
    if len(original) != len(expanded):
        return False
    _, matrices, shapes, clip_rows, _ = parse(original)
    frame_full = matrices * 84 + shapes * 4
    a, b = bytearray(original), bytearray(expanded)
    for n, (start, frames) in enumerate(clip_rows):
        for f in range(frames):
            if n == 0 and f == 0:
                continue
            base = start + 44 + f * frame_full
            for bone in range(matrices):
                at = base + bone * 84 + 48
                a[at:at + 36] = b[at:at + 36]
    return a == b


def read_index(path):
    with path.open('rb') as f:
        header = f.read(12)
        if header[:4] != b'MPK1':
            raise SystemExit(f'{path}: not MPK1')
        count, names_bytes = struct.unpack('<II', header[4:])
        table = f.read(12 * count)
        blob = f.read(names_bytes)
    entries = []
    for i in range(count):
        name_at, offset, size = struct.unpack_from('<III', table, 12 * i)
        name = blob[name_at:blob.index(b'\0', name_at)].decode()
        entries.append((name, offset, size))
    return entries


def driver_banks(entries):
    return [e for e in entries if e[0].startswith('models/') and e[0].endswith('driver.dskm')]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('pak', type=Path)
    parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--check', action='store_true', help='round-trip every driver bank and write nothing')
    args = parser.parse_args()
    entries = read_index(args.pak)
    names = {e[0] for e in entries}
    banks = driver_banks(entries)
    if any(n.endswith('.dsku') for n in names):
        raise SystemExit('this pack already holds .dsku entries')
    compacted = {}
    full_bytes = small_bytes = 0
    with args.pak.open('rb') as f:
        for name, offset, size in banks:
            f.seek(offset)
            data = f.read(size)
            small = compact(data)
            if not same_except_normals(data, expand(small)):
                raise SystemExit(f'{name}: round trip differs outside unused normals')
            full_bytes += size
            small_bytes += len(small)
            if not args.check:
                compacted[name[:-len('.dskm')] + '.dsku'] = small
    print(f'{len(banks)} driver banks: {full_bytes / 2**20:.1f} MiB -> {small_bytes / 2**20:.1f} MiB as .dsku '
          f'({100 * (1 - small_bytes / full_bytes):.1f}% smaller); round trips exact')
    if args.check:
        return
    if not args.output:
        raise SystemExit('output path required')
    # The runtime finds entries by binary search on the name (strcmp order).
    rows = sorted([(n, 'old', (o, s)) for n, o, s in entries] + [(n, 'new', d) for n, d in compacted.items()],
                  key=lambda r: r[0].encode())
    blob = b''.join(r[0].encode() + b'\0' for r in rows)
    header = 12 + 12 * len(rows) + len(blob)
    offset = (header + ALIGN - 1) & ~(ALIGN - 1)
    table, name_at = [], 0
    for name, kind, payload in rows:
        size = payload[1] if kind == 'old' else len(payload)
        table.append((name_at, offset, size))
        name_at += len(name.encode()) + 1
        offset += (size + ALIGN - 1) & ~(ALIGN - 1)
    if offset >= 1 << 31:
        raise SystemExit('pack would exceed the 2 GiB seek range')
    with args.pak.open('rb') as src, args.output.open('wb') as out:
        out.write(b'MPK1' + struct.pack('<II', len(rows), len(blob)))
        for row in table:
            out.write(struct.pack('<III', *row))
        out.write(blob)
        out.write(b'\0' * (((header + ALIGN - 1) & ~(ALIGN - 1)) - header))
        for (name, kind, payload), (_, at, size) in zip(rows, table):
            if out.tell() != at:
                raise SystemExit('layout error')
            if kind == 'old':
                src.seek(payload[0])
                left = size
                while left:
                    chunk = src.read(min(left, 1 << 22))
                    out.write(chunk)
                    left -= len(chunk)
            else:
                out.write(payload)
            out.write(b'\0' * (((size + ALIGN - 1) & ~(ALIGN - 1)) - size))
    print(f'wrote {args.output} ({len(rows)} entries, {offset / 2**30:.2f} GiB)')


if __name__ == '__main__':
    sys.exit(main())
