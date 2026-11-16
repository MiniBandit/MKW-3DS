#!/usr/bin/env python3
"""Entry order for tools/pakpack.c from DIAG pack traces (pack-trace.txt).

Entries the traces read come first, in the order the game first read them;
the rest follow by name without the models/ or textures/ prefix, which puts
each model beside its textures. One name per line on stdout.

usage: pak_order.py PACK.pak ARCHIVE TRACE [TRACE ...] > order.txt
ARCHIVE: 0 for menu.pak, 1 for race.pak (the trace's first column). The
traces must come from the MPK1 pack given (offsets are matched to entries).
A TRACE ending in .txt is a list of entry names already in read order, such
as tools/pak-order/race.txt (the traced part of an earlier order.txt, which
tools/build.py uses); names the pack doesn't hold are skipped.
"""
import bisect, re, struct, sys


def index(path):
    with open(path, 'rb') as f:
        magic, count, names_bytes = struct.unpack('<4sII', f.read(12))
        if magic != b'MPK1':
            raise SystemExit(f'{path}: traces map onto an MPK1 pack, not {magic!r}')
        table = f.read(12 * count)
        names = f.read(names_bytes)
    entries = []
    for i in range(count):
        name, offset, size = struct.unpack_from('<III', table, 12 * i)
        entries.append((offset, size, names[name:names.index(b'\0', name)].decode()))
    entries.sort()
    return entries


def stem_key(name):
    flat = name.split('/', 1)[-1]
    return (re.sub(r'(_\d{3})?\.\w+$', '', flat), not name.startswith('models/'), flat)


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    entries = index(sys.argv[1])
    archive = sys.argv[2]
    starts = [e[0] for e in entries]
    seen, order = set(), []
    held = {e[2] for e in entries}
    for trace in sys.argv[3:]:
        if trace.endswith('.txt'):
            for name in open(trace).read().splitlines():
                if name in held and name not in seen:
                    seen.add(name)
                    order.append(name)
            continue
        for line in open(trace):
            parts = line.strip().split(',')
            if len(parts) < 3 or parts[0] != archive:
                continue
            offset = int(parts[1])
            i = bisect.bisect_right(starts, offset) - 1
            if i < 0 or offset >= entries[i][0] + max(entries[i][1], 1):
                continue
            name = entries[i][2]
            if name not in seen:
                seen.add(name)
                order.append(name)
    rest = sorted((e[2] for e in entries if e[2] not in seen), key=stem_key)
    sys.stdout.write('\n'.join(order + rest) + '\n')
    print(f'{len(order)} traced, {len(rest)} by name', file=sys.stderr)


if __name__ == '__main__':
    main()
