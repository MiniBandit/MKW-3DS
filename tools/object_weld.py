#!/usr/bin/env python3
"""Weld exact duplicate vertices of a course-object GMDL (same picture), and
optionally reorder its triangles for the post-transform vertex cache.

tools/cook_course_objects.py cooks with preserve_vertices=True: one vertex per
triangle corner, so DOBJ banks (tools/object_bank.ts) line up vertex for
vertex. This pass, run after the bank is final (morphs appended), merges the
corners of one submesh whose 44 GMDL bytes are equal and, for a skinned bank,
whose bank vertex (position, normal, matrix) and morph deltas are equal too,
then remaps the bank's per-vertex array, its shape ranges and MORF lists.
Vertices are never shared across submeshes, so everything the game computes
per vertex at load (lighting bakes, environment UVs, bone indices) is the
same value it was for every welded corner.

Welding keeps every triangle, its order and its corners: the GPU shades the
same values and rasterizes the same triangles in the same order, so frames are
identical (Azahar, 2026-11-16). It takes a 16-entry FIFO post-vertex cache
from 3.00 to 1.46 shaded vertices per object triangle.

REORDER (off): Tipsify on top takes that to 1.14, but it is not exact. Two
triangles of one submesh that meet at exactly the same depth on a pixel
(crossed foliage cards, intersecting parts) resolve by draw order, so a
reordered submesh can change such pixels: Azahar showed 4 single pixels in
108 Daisy Circuit frames (up to 88 levels) and 2 in GCN Peach Beach (1-2
levels). Only order-safe
submeshes are reordered (unblended, depth-tested and written: the renderer's
orderSafe), by Tipsify, kept only where a 16-entry FIFO (the console's post
vertex cache, mesh-reduction-plan.md) shades fewer vertices than the stored
order. A rigid bank (every triangle on one matrix) is sorted by matrix first,
as ObjectBank::makeRigid sorts at load, and reordered inside each matrix run,
so the load-time sort keeps this order. Submesh records, textures and every
trailing block (UVM1, samplers, MAN1, WIN2) are per submesh and unchanged.
Vertices are numbered in the order the new index list first uses them.
"""
import struct
from collections import defaultdict

SUBMESH_BYTES = {3: 44, 4: 56, 5: 92}
# Geometry drawn under another model's material at runtime (buildEscalator
# draws the pieces with the strip's escalator_m): keep their order.
KEEP_ORDER = ('obj05_escalator__',)
# Triangle reorder (see above): off, it changes depth-tie pixels.
REORDER = False


def order_safe(flags):
    # Renderer.cpp orderSafe: unblended, and either not a source-GX material
    # or one with depth test and depth write.
    return not (flags & 2) and (not (flags & (1 << 31)) or bool((flags & (1 << 23)) and (flags & (1 << 24))))


def read_gmdl(data):
    magic, version, nv, ni, ns, nt = struct.unpack_from('<4s5I', data)
    if magic != b'GMDL' or version not in SUBMESH_BYTES:
        raise ValueError('not a GMDL')
    size = SUBMESH_BYTES[version]
    subs = [struct.unpack_from('<IIiI', data, 24 + i * size) for i in range(ns)]
    vat = 24 + ns * size + nt * 64
    iat = vat + nv * 44
    vertices = [data[vat + i * 44:vat + i * 44 + 44] for i in range(nv)]
    indices = list(struct.unpack_from('<%dH' % ni, data, iat))
    return {'version': version, 'subs': subs, 'vertices': vertices, 'indices': indices,
            'head': data[:vat], 'tail': data[iat + ni * 2:]}


def write_gmdl(m, vertices, indices):
    head = bytearray(m['head'])
    struct.pack_into('<I', head, 8, len(vertices))
    return bytes(head) + b''.join(vertices) + struct.pack('<%dH' % len(indices), *indices) + m['tail']


def samples(frames, step):
    return 1 if frames <= 1 else (frames - 1 + step - 1) // step + 1


def read_bank(data):
    """Offsets of the vertex-numbered parts of a DOBJ v1 (src/object_bank.cpp load)."""
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    if data[:4] != b'DOBJ' or u32(4) != 1:
        raise ValueError('not a DOBJ v1')
    nv, nm, ns, nn, nc, np_, nt, nl = struct.unpack_from('<8I', data, 8)
    at = 40
    source = [data[at + i * 28:at + i * 28 + 28] for i in range(nv)]
    at += nv * 28
    ranges = [struct.unpack_from('<II', data, at + i * 8) for i in range(ns)]
    at += ns * 8
    middle_start = at
    at += nm * 48 + nn * 140
    for _ in range(nc):
        frames, loop, step, animated = struct.unpack_from('<4I', data, at + 32)
        at += 48 + animated * 4 + nm * 48 + samples(frames, step) * animated * 48
    for _ in range(np_):
        frames = u32(at + 32)
        at += 40 + frames * ns * 8
    for _ in range(nt):
        frames, loop, step, count = struct.unpack_from('<4I', data, at + 32)
        at += 48 + count * 4 + samples(frames, step) * count * 48
    for _ in range(nl):
        frames, loop, count = struct.unpack_from('<3I', data, at + 32)
        at += 44 + count * 8 + frames * count * 12
    chunks = []  # (tag, start, end, parsed)
    while at < len(data):
        tag, count = data[at:at + 4], u32(at + 4)
        start = at
        if tag == b'JCB1':
            matrices, clips = struct.unpack_from('<2I', data, at + 8)
            at += 16 + count * 36 + count * 18 * 4
            for _ in range(nm):
                at += 4 + u32(at) * 56
            for _ in range(nc):
                frames, loop, step = struct.unpack_from('<3I', data, at)
                at += 12 + samples(frames, step) * count * 18 * 4
            chunks.append((tag, start, at, None))
        elif tag == b'MORF':
            at += 8
            morphs = []
            for _ in range(count):
                clip, n, frames = struct.unpack_from('<3I', data, at)
                at += 12
                verts = list(struct.unpack_from('<%dI' % n, data, at))
                at += n * 4
                deltas = [data[at + k * 24:at + k * 24 + 24] for k in range(n)]
                at += n * 24
                weights = data[at:at + frames * 4]
                at += frames * 4
                morphs.append((clip, frames, verts, deltas, weights))
            chunks.append((tag, start, at, morphs))
        else:
            raise ValueError('unknown DOBJ chunk %r' % tag)
    if at != len(data):
        raise ValueError('DOBJ size mismatch')
    return {'nv': nv, 'source': source, 'ranges': ranges, 'middle_start': middle_start, 'chunks': chunks, 'data': data}


def write_bank(b, source, ranges, morphs):
    data = b['data']
    head = bytearray(data[:40])
    struct.pack_into('<I', head, 8, len(source) if b['nv'] else 0)
    out = [bytes(head), b''.join(source), b''.join(struct.pack('<II', *r) for r in ranges)]
    first_chunk = b['chunks'][0][1] if b['chunks'] else len(data)
    out.append(data[b['middle_start']:first_chunk])
    for tag, start, end, parsed in b['chunks']:
        if tag != b'MORF':
            out.append(data[start:end])
            continue
        out.append(b'MORF' + struct.pack('<I', len(morphs)))
        for clip, frames, verts, deltas, weights in morphs:
            out.append(struct.pack('<3I', clip, len(verts), frames) + struct.pack('<%dI' % len(verts), *verts)
                       + b''.join(deltas) + weights)
    return b''.join(out)


def fifo_misses(run, size=16):
    cache, members, head, misses = [-1] * size, set(), 0, 0
    for v in run:
        if v in members:
            continue
        misses += 1
        if cache[head] >= 0:
            members.discard(cache[head])
        cache[head] = v
        members.add(v)
        head = (head + 1) % size
    return misses


def tipsify(tris, size=4):
    """Sander, Nehab and Barczak 2007 (analysis-tools/mesh_report.py). Returns
    triangle numbers in the new order; each triangle keeps its corners."""
    adj = defaultdict(list)
    for t, tri in enumerate(tris):
        for v in tri:
            adj[v].append(t)
    verts = list(adj)
    live = {v: len(adj[v]) for v in verts}
    stamp = {v: -10 ** 9 for v in verts}
    done = [False] * len(tris)
    order, dead = [], []
    now, cursor = size + 1, 0
    f = verts[0] if verts else None
    while f is not None:
        cand = []
        for t in adj[f]:
            if done[t]:
                continue
            for v in tris[t]:
                dead.append(v)
                cand.append(v)
                live[v] -= 1
                if now - stamp[v] > size:
                    stamp[v] = now
                    now += 1
            done[t] = True
            order.append(t)
        best, best_p = None, -1
        for v in cand:
            if live[v] > 0:
                p = now - stamp[v] if now - stamp[v] + 2 * live[v] <= size else 0
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
    assert sorted(order) == list(range(len(tris)))
    return order


def reorder(tris, size=4):
    """tris in a better order for a 16-entry FIFO, or unchanged."""
    if len(tris) < 2:
        return tris
    order = tipsify(tris, size)
    new = [tris[t] for t in order]
    flat = lambda ts: [v for t in ts for v in t]
    return new if fifo_misses(flat(new)) < fifo_misses(flat(tris)) else tris


def weld(name, gmdl, bank=None, reorder_triangles=None):
    """(welded gmdl bytes, welded bank bytes or None, stats). bank: DOBJ bytes
    whose vertices line up with the GMDL (cook_course_objects.py)."""
    m = read_gmdl(gmdl)
    V, I = m['vertices'], m['indices']
    if I != list(range(len(V))):
        raise ValueError(name + ': not a preserve_vertices GMDL')
    b = read_bank(bank) if bank is not None else None
    skinned = bool(b and b['nv'])
    if skinned and b['nv'] != len(V):
        raise ValueError(name + ': bank vertices %d != GMDL %d' % (b['nv'], len(V)))
    if b is not None and len(b['ranges']) and sum(r[1] for r in b['ranges']) != len(V):
        raise ValueError(name + ': bank shape ranges do not cover the GMDL')
    morphs = [c[3] for c in b['chunks'] if c[0] == b'MORF'] if b else []
    morphs = morphs[0] if morphs else []
    delta = defaultdict(list)
    for k, (clip, frames, verts, deltas, weights) in enumerate(morphs):
        for v, d in zip(verts, deltas):
            delta[v].append((k, d))
    # Matrix of each source vertex (rigid sort, as makeRigid).
    matrix = [struct.unpack_from('<I', s, 24)[0] for s in b['source']] if skinned else None
    rigid = skinned and not morphs and all(
        matrix[I[s[0] + k]] == matrix[I[s[0] + k - k % 3]] for s in m['subs'] for k in range(s[1]))
    keep = name.startswith(KEEP_ORDER) or not (REORDER if reorder_triangles is None else reorder_triangles)
    # Canonical corner per (submesh, gmdl bytes, bank vertex, morph deltas).
    canon = list(range(len(V)))
    for n, (first, count, _, _) in enumerate(m['subs']):
        seen = {}
        for k in range(first, first + count):
            v = I[k]
            key = (V[v], b['source'][v] if skinned else b'', tuple(delta.get(v, ())))
            canon[v] = seen.setdefault(key, v)
    # New index list, submesh by submesh.
    out = []
    reordered = 0
    for n, (first, count, _, flags) in enumerate(m['subs']):
        tris = [tuple(canon[I[k + j]] for j in range(3)) for k in range(first, first + count - count % 3, 3)]
        if count % 3:
            raise ValueError(name + ': submesh not whole triangles')
        if order_safe(flags) and not keep:
            if rigid:
                groups = defaultdict(list)
                for t in tris:
                    groups[matrix[t[0]]].append(t)
                new = [t for mtx in sorted(groups) for t in reorder(groups[mtx])]
            else:
                new = reorder(tris)
            reordered += new != tris
            tris = new
        out += [v for t in tris for v in t]
    # Number vertices by first use; submeshes stay contiguous vertex blocks.
    number = {}
    for v in out:
        number.setdefault(v, len(number))
    old_of = sorted(number, key=number.get)
    indices = [number[v] for v in out]
    vertices = [V[v] for v in old_of]
    if len(vertices) > 65535:
        raise ValueError(name + ': too many vertices')
    # Self-check: every submesh draws the same corners (as a multiset of
    # triangles; in order where order is kept), every vertex the same bytes.
    for first, count, _, flags in m['subs']:
        before = [tuple(V[I[k + j]] for j in range(3)) for k in range(first, first + count, 3)]
        after = [tuple(vertices[indices[k + j]] for j in range(3)) for k in range(first, first + count, 3)]
        if (after if not order_safe(flags) or keep else sorted(after)) != (before if not order_safe(flags) or keep else sorted(before)):
            raise ValueError(name + ': welded triangles differ')
    new_bank = None
    if b is not None:
        new_of = [number[canon[v]] for v in range(len(V))]
        source = [b['source'][v] for v in old_of] if skinned else []
        if skinned:
            for v in range(len(V)):
                if source[new_of[v]] != b['source'][v]:
                    raise ValueError(name + ': bank vertex differs')
        ranges = []
        for first, count in b['ranges']:
            ids = sorted({new_of[v] for v in range(first, first + count)})
            if ids and ids != list(range(ids[0], ids[0] + len(ids))):
                raise ValueError(name + ': shape range not contiguous after weld')
            start = ids[0] if ids else (new_of[first] if first < len(V) else len(vertices))
            ranges.append((start, len(ids)))
        new_morphs = []
        for k, (clip, frames, verts, deltas, weights) in enumerate(morphs):
            moved = {}
            for v, d in zip(verts, deltas):
                if moved.setdefault(new_of[v], d) != d:
                    raise ValueError(name + ': welded morph deltas differ')
            members = set(verts)
            for v in range(len(V)):
                if (new_of[v] in moved) != (v in members):
                    raise ValueError(name + ': morph membership differs')
            order = sorted(moved)
            new_morphs.append((clip, frames, order, [moved[v] for v in order], weights))
        new_bank = write_bank(b, source, ranges, new_morphs)
        check = read_bank(new_bank)
        if check['ranges'] != ranges:
            raise ValueError(name + ': bank rewrite failed')
    stats = {'vertices_before': len(V), 'vertices_after': len(vertices), 'triangles': len(I) // 3,
             'submeshes': len(m['subs']), 'reordered_submeshes': reordered, 'rigid': rigid,
             'fifo16_before': sum(fifo_misses(I[f:f + c]) for f, c, _, _ in m['subs']),
             'fifo16_after': sum(fifo_misses(indices[f:f + c]) for f, c, _, _ in m['subs'])}
    return write_gmdl(m, vertices, indices), new_bank, stats


if __name__ == '__main__':
    import argparse, json
    from pathlib import Path
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('gmdl', type=Path)
    p.add_argument('--bank', type=Path)
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    g, nb, s = weld(a.gmdl.stem, a.gmdl.read_bytes(), a.bank.read_bytes() if a.bank else None)
    print(json.dumps(s))
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
        (a.out / a.gmdl.name).write_bytes(g)
        if nb is not None:
            (a.out / a.bank.name).write_bytes(nb)
