#!/usr/bin/env python3
"""Racer index buffers for the post-transform vertex cache (same picture).

The animated racer parts (drivers, animated kart bodies) are cooked with one
vertex per triangle corner (cook_model.py preserve_vertices: their DSKM banks
index per-vertex data), and their index buffers are the identity: every index
shades, 3 vertices a triangle. The PICA200's post-transform cache (on as the
game runs, ~16-entry FIFO keyed by index, console 2026-11-16) can only help
when equal vertices share an index and triangles that share vertices are drawn
close together.

This pass rewrites only those index buffers, in race.pak:
- weld: a corner whose GMDL vertex (all 44 bytes), DSKM source vertex (all 28
  bytes: position, normal, palette matrix) and DSKM shape ranges equal an
  earlier corner's in the same submesh takes that corner's index. Both read the
  same vertex data, through every path (GPU and CPU skinning, visibility and UV
  tracks per shape, per-vertex lighting bakes per submesh), so they shade the
  same;
- reorder: an order-safe submesh (Renderer orderSafe: unblended, depth tested
  and written) takes the Tipsify order (Sander, Nehab and Barczak 2007) that a
  16-entry FIFO shades fewest vertices for (cache size 4 or 16, else the welded
  stored order). Triangles keep their corners' order (winding). Blended
  submeshes keep their triangle order.
Vertices, banks, submesh ranges and every other entry are unchanged byte for
byte, so nothing that indexes racer vertices needs a remap; the unused
duplicate vertices stay in the vertex buffer.

The game's off switch (DIAG no-racer-weld.txt) loads every model whose vertex
count equals its index count with identity indices again: this pass checks that
every such GMDL in the input pack has identity indices, so that is exactly the
old data.

    python3 tools/pack_racer_weld.py race.pak race-weld.pak [--report report.json]
    python3 tools/pack_racer_weld.py --check race.pak    (measure, write nothing)
"""
import argparse, json, shutil, struct, sys
from collections import defaultdict
from pathlib import Path

SUBMESH_BYTES = {3: 44, 4: 56, 5: 92}
# Racer parts with banks (cook_garage.py: the driver, an animated body, animated extras).
PARTS = ('driver', 'body', 'handle', 'f_sus', 'swingarm', 'r_sus', 'arm_fl', 'arm_fr', 'arm_rl', 'arm_rr', 'sus_fr')
FIFO_SIZES = (4, 8, 16)


def read_index(path):
    with path.open('rb') as f:
        header = f.read(12)
        if header[:4] != b'MPK1':
            raise SystemExit(f'{path}: not MPK1')
        count, names_bytes = struct.unpack('<II', header[4:])
        table = f.read(12 * count)
        blob = f.read(names_bytes)
    entries = {}
    for i in range(count):
        name_at, offset, size = struct.unpack_from('<III', table, 12 * i)
        entries[blob[name_at:blob.index(b'\0', name_at)].decode()] = (offset, size)
    return entries


def order_safe(flags):
    """src/renderer.cpp orderSafe: triangle order can't show."""
    return not (flags & 2) and (not (flags & (1 << 31)) or bool((flags & (1 << 23)) and (flags & (1 << 24))))


def gmdl_layout(data):
    magic, version, nv, ni, ns, nt = struct.unpack_from('<4s5I', data)
    if magic != b'GMDL' or version not in SUBMESH_BYTES:
        return None
    record = SUBMESH_BYTES[version]
    subs = [struct.unpack_from('<IIiI', data, 24 + i * record) for i in range(ns)]
    vertices = 24 + ns * record + nt * 64
    indices = vertices + nv * 44
    return nv, ni, subs, vertices, indices


def dskm_layout(data):
    """Source vertices (28 bytes each) and shape ranges, as DriverAnimation::load reads them."""
    if data[:4] != b'DSKM':
        return None
    version, nv, _, _, shapes = struct.unpack_from('<5I', data, 4)
    at = 32 if version >= 5 else 28
    source = data[at:at + nv * 28]
    ranges = [struct.unpack_from('<II', data, at + nv * 28 + 8 * i) for i in range(shapes)]
    return nv, source, ranges


def fifo_misses(run, size):
    cache = [-1] * size
    members = set()
    head = misses = 0
    for v in run:
        if v in members:
            continue
        misses += 1
        old = cache[head]
        if old >= 0:
            members.discard(old)
        cache[head] = v
        members.add(v)
        head = (head + 1) % size
    return misses


def tipsify(run, size):
    """analysis-tools/mesh_report.py tipsify(); corners keep their order."""
    tris = [tuple(run[i:i + 3]) for i in range(0, len(run), 3)]
    adj = defaultdict(list)
    for t, (a, b, c) in enumerate(tris):
        adj[a].append(t)
        adj[b].append(t)
        adj[c].append(t)
    verts = list(adj)
    live = {v: len(adj[v]) for v in verts}
    stamp = {v: -10 ** 9 for v in verts}
    done = [False] * len(tris)
    out, dead = [], []
    now = size + 1
    cursor = 0
    f = verts[0] if verts else None
    while f is not None:
        cand = []
        for t in adj[f]:
            if done[t]:
                continue
            for v in tris[t]:
                out.append(v)
                dead.append(v)
                cand.append(v)
                live[v] -= 1
                if now - stamp[v] > size:
                    stamp[v] = now
                    now += 1
            done[t] = True
        best, best_p = None, -1
        for v in cand:
            if live[v] > 0:
                p = 0
                if now - stamp[v] + 2 * live[v] <= size:
                    p = now - stamp[v]
                if p > best_p:
                    best, best_p = v, p
        if best is None:
            while dead:
                v = dead.pop()
                if live[v] > 0:
                    best = v
                    break
        if best is None:
            while cursor < len(verts) and live[verts[cursor]] <= 0:
                cursor += 1
            best = verts[cursor] if cursor < len(verts) else None
        f = best
    return out


def triangle_multiset(run):
    return sorted(tuple(run[i:i + 3]) for i in range(0, len(run), 3))


def weld_model(gmdl, bank):
    """New index list for an identity-indexed racer part, and per-run statistics."""
    nv, ni, subs, vat, iat = gmdl_layout(gmdl)
    bnv, source, ranges = dskm_layout(bank)
    if bnv != nv:
        raise ValueError('bank vertex count differs from the model')
    shape_mask = [0] * nv
    for s, (first, count) in enumerate(ranges):
        for v in range(first, first + count):
            shape_mask[v] |= 1 << s
    covered = sorted((first, count) for first, count, _, _ in subs if count)
    end = 0
    for first, count in covered:
        if first < end:
            raise ValueError('overlapping submeshes')
        end = first + count
    indices = list(range(ni))
    stats = {'triangles': 0, 'vertices': nv, 'stored': [0] * len(FIFO_SIZES), 'welded': [0] * len(FIFO_SIZES),
             'final': [0] * len(FIFO_SIZES), 'unique': 0, 'reordered_tris': 0, 'kept_order_tris': 0}
    for first, count, _, flags in subs:
        if not count:
            continue
        seen = {}
        run = []
        for i in range(first, first + count):
            key = (gmdl[vat + i * 44:vat + i * 44 + 44], source[i * 28:i * 28 + 28], shape_mask[i])
            run.append(seen.setdefault(key, i))
        stats['unique'] += len(seen)
        stats['triangles'] += count // 3
        stored = list(range(first, first + count))
        best = run
        if order_safe(flags):
            score = lambda r: (fifo_misses(r, 16), fifo_misses(r, 4))
            best_score = score(run)
            for size in (4, 16):
                candidate = tipsify(run, size)
                if triangle_multiset(candidate) != triangle_multiset(run):
                    raise ValueError('Tipsify lost or changed a triangle')
                if score(candidate) < best_score:
                    best, best_score = candidate, score(candidate)
            stats['reordered_tris'] += count // 3
        else:
            stats['kept_order_tris'] += count // 3
        for k, size in enumerate(FIFO_SIZES):
            stats['stored'][k] += fifo_misses(stored, size)
            stats['welded'][k] += fifo_misses(run, size)
            stats['final'][k] += fifo_misses(best, size)
        indices[first:first + count] = best
    return struct.pack(f'<{ni}H', *indices), iat, stats


def part_kind(name):
    stem = name[len('models/'):-len('.gmdl')]
    part = next((p for p in PARTS if stem.endswith('_' + p)), None)
    if part is None:
        return None
    return ('mii' if stem.startswith('mii_') else 'roster') + '-' + ('driver' if part == 'driver' else 'body' if part == 'body' else 'extra')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('pak', type=Path)
    parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--check', action='store_true', help='measure only, write nothing')
    parser.add_argument('--report', type=Path, help='per-model statistics (JSON)')
    args = parser.parse_args()
    entries = read_index(args.pak)
    patches, report = [], {}
    totals = defaultdict(lambda: {'models': 0, 'triangles': 0, 'vertices': 0, 'unique': 0, 'stored': [0] * 3, 'welded': [0] * 3,
                                  'final': [0] * 3, 'reordered_tris': 0, 'kept_order_tris': 0})
    identity_models = 0
    with args.pak.open('rb') as f:
        for name, (offset, size) in sorted(entries.items()):
            if not (name.startswith('models/') and name.endswith('.gmdl')):
                continue
            f.seek(offset)
            data = f.read(size)
            layout = gmdl_layout(data)
            if layout is None:
                continue
            nv, ni, _, _, iat = layout
            if nv != ni:
                continue
            # The off switch's premise: vertex count == index count means identity indices.
            if data[iat:iat + 2 * ni] != struct.pack(f'<{ni}H', *range(ni)):
                raise SystemExit(f'{name}: as many indices as vertices but not the identity (the off switch would change it)')
            identity_models += 1
            kind = part_kind(name)
            bank = name[:-len('.gmdl')] + '.dskm'
            if kind is None or bank not in entries:
                continue
            f.seek(entries[bank][0])
            indices, at, stats = weld_model(data, f.read(entries[bank][1]))
            patches.append((name, offset + at, indices))
            report[name] = stats
            t = totals[kind]
            t['models'] += 1
            for key in ('triangles', 'vertices', 'unique', 'reordered_tris', 'kept_order_tris'):
                t[key] += stats[key]
            for key in ('stored', 'welded', 'final'):
                t[key] = [a + b for a, b in zip(t[key], stats[key])]
    print(f'{identity_models} identity-indexed models in the pack; {len(patches)} racer parts rewritten')
    print('kind                models triangles  unique/vertices  shaded/tri FIFO-4/8/16: stored -> welded -> welded+reordered')
    for kind, t in sorted(totals.items()):
        n = t['triangles']
        fmt = lambda xs: '/'.join(f'{x / n:.2f}' for x in xs)
        print(f'{kind:18s} {t["models"]:6d} {n:9d}  {t["unique"]:7d}/{t["vertices"]:7d}  '
              f'{fmt(t["stored"])} -> {fmt(t["welded"])} -> {fmt(t["final"])}  (order kept on {t["kept_order_tris"]} tris)')
    if args.report:
        args.report.write_text(json.dumps({'totals': totals, 'models': report}, indent=1) + '\n')
    if args.check:
        return
    if not args.output:
        raise SystemExit('output path required')
    temp = args.output.with_suffix(args.output.suffix + '.tmp')
    shutil.copyfile(args.pak, temp)
    with temp.open('r+b') as out:
        for name, at, indices in patches:
            out.seek(at)
            out.write(indices)
    temp.replace(args.output)
    print(f'wrote {args.output}')


if __name__ == '__main__':
    sys.exit(main())
