"""Precomputed visibility for a course mesh (models/course_NN_000.gmdl).

For camera cells over the track it lists which 128-triangle culling blocks can
be seen from anywhere in the cell, in any direction. The game skips the rest
(Renderer::drawNow). The blocks are the game's own: order-safe submeshes use
the Morton-sorted index copy (include/mkw/visibility.hpp, spatialShadowIndices,
reproduced here step for step in float32), the others their authored indices,
in runs of 128 triangles. The file stores a hash of the sorted copy; the game
ignores the file if its own copy differs.

Conservative by construction:
- only the track's own opaque, depth-written, non-alpha-tested surfaces hide
  anything; one-sided ones only from their front;
- a hiding surface counts only where it covers a whole pixel, at its farthest
  depth across that pixel;
- a block is visible if any pixel it touches might be in front;
- six 90-degree views per sample point, so any camera direction;
- a cell takes the union of every sample point within one spacing of it.
Cells with no sample point in them get no list: the game draws everything.

Needs numpy and numba (an isolated venv works: python -m venv build-pvs/venv;
build-pvs/venv/Scripts/python -m pip install numpy numba).
Use: course_pvs.py <course.gmdl> <out.pvs> [--spacing 500] [--resolution 256]
"""
import argparse
import os
import re
import struct
import sys
import time

import numpy as np
from numba import njit, prange

F32 = np.float32


def order_safe(flags):
    """Must match orderSafe in the renderer: triangle order can't show."""
    return not (flags & 2) and (not (flags & (1 << 31)) or ((flags & (1 << 23)) and (flags & (1 << 24))))


def read_file(path):
    """A file, or `archive.pak:entry` for an MPK1 entry. The game prefers the
    race.pak copy of a model to the loose file, so read the one it reads."""
    if '.pak:' not in path:
        return open(path, 'rb').read()
    archive, entry = path.split('.pak:', 1)
    with open(archive + '.pak', 'rb') as f:
        magic, count, names_size = struct.unpack('<4sII', f.read(12))
        rows = [struct.unpack('<III', f.read(12)) for _ in range(count)]
        names = f.read(names_size)
        for name, offset, size in rows:
            if names[name:names.index(b'\0', name)].decode() == entry:
                f.seek(offset)
                return f.read(size)
    sys.exit(f'{entry} not in {archive}.pak')


def read_gmdl(path):
    data = read_file(path)
    magic, version, vertices, indices, subs, textures = struct.unpack_from('<4s5I', data)
    if magic != b'GMDL' or version not in (3, 4, 5):
        sys.exit(f'{path}: not a GMDL')
    record = {5: 92, 4: 56, 3: 44}[version]
    at = 24
    submeshes = []
    for _ in range(subs):
        first, count, texture, flags = struct.unpack_from('<IIiI', data, at)
        submeshes.append((first, count, flags, texture))
        at += record
    at += textures * 64
    vertex = np.frombuffer(data, dtype=np.uint8, count=vertices * 44, offset=at).reshape(vertices, 44)
    at += vertices * 44
    positions = vertex[:, 0:12].copy().view(F32).reshape(vertices, 3)
    normals = vertex[:, 12:24].copy().view(F32).reshape(vertices, 3)
    index = np.frombuffer(data, dtype=np.uint16, count=indices, offset=at).copy()
    # Each submesh's first sampler (valid, minFilter) from the SMP1 section,
    # which the road sampler split reads; None without one.
    samplers = None
    tag = data.find(b'SMP1' + struct.pack('<I', subs), at + indices * 2)
    if tag >= 0:
        samplers = [(data[tag + 8 + i * 24 + 6], data[tag + 8 + i * 24 + 4]) for i in range(subs)]
    return positions, normals, index, submeshes, samplers


def road_sampling(name, vertices, indices, submeshes, samplers, ranges_path):
    """roadSampling::apply (include/mkw/road_sampler_policy.hpp): the game cuts
    some road draws at these ranges when it loads the model, so its submesh
    list, and with it the culling blocks, has more entries than the file."""
    if not samplers or not os.path.exists(ranges_path):
        return submeshes
    text = open(ranges_path, encoding='utf-8').read()
    tables = {m[0]: [tuple(map(int, r)) for r in re.findall(r'\{(\d+),(\d+),(-?\d+)\}', m[1])]
              for m in re.findall(r'Range (R\d+)\[\]=\{(.*?)\};', text)}
    mesh = None
    for m in re.findall(r'\{"([^"]+)",(\d+),(\d+),(\d+),(R\d+),(\d+) ?\}', text):
        if m[0] == name and int(m[1]) == vertices and int(m[2]) == indices and int(m[3]) == len(submeshes):
            mesh = tables[m[4]][:int(m[5])]
    if mesh is None:
        return submeshes
    out, changed = [], False
    for i, (first, count, flags, texture) in enumerate(submeshes):
        cuts = {first, first + count}
        for r_first, r_count, r_texture in mesh:
            if r_texture != texture or r_first >= first + count or r_first + r_count <= first:
                continue
            cuts.add(max(first, r_first))
            cuts.add(min(first + count, r_first + r_count))
        cuts = sorted(cuts)
        if len(cuts) == 1:
            cuts.append(cuts[0])
        for a, b in zip(cuts, cuts[1:]):
            valid, min_filter = samplers[i]
            for r_first, r_count, r_texture in mesh:
                if r_texture == texture and r_first <= a < r_first + r_count and valid and min_filter >= 2:
                    changed = True
                    break
            out.append((a, b - a, flags, texture))
    return out if changed else submeshes


def spread(n):
    n = n & np.uint32(1023)
    n = (n | (n << np.uint32(16))) & np.uint32(0x030000ff)
    n = (n | (n << np.uint32(8))) & np.uint32(0x0300f00f)
    n = (n | (n << np.uint32(4))) & np.uint32(0x030c30c3)
    return (n | (n << np.uint32(2))) & np.uint32(0x09249249)


def spatial_sort(positions, source, first, count):
    """spatialShadowIndices: the same float32 steps and the same total order."""
    if not count:
        return source[first:first]
    run = source[first:first + count]
    p = positions[run]
    lo, hi = p.min(axis=0), p.max(axis=0)
    span = (hi - lo).astype(F32)
    tri = run[: (count // 3) * 3].reshape(-1, 3)
    centre = ((positions[tri[:, 0]] + positions[tri[:, 1]]) + positions[tri[:, 2]]) * (F32(1) / F32(3))

    def cell(x, low, size):
        v = (x - low) / np.maximum(size, F32(1)) * F32(1023)
        return np.clip(v, F32(0), F32(1023)).astype(np.uint32)

    key = (spread(cell(centre[:, 0], lo[0], span[0])) | (spread(cell(centre[:, 1], lo[1], span[1])) << np.uint32(1))
           | (spread(cell(centre[:, 2], lo[2], span[2])) << np.uint32(2)))
    order = np.lexsort((np.arange(len(key)), key))
    return tri[order].reshape(-1)


@njit(cache=True)
def raster(depth, R, x0, y0, w0, x1, y1, w1, x2, y2, w2, inner):
    """inner: write pixels the triangle wholly covers, at its farthest 1/z over
    the pixel. Otherwise: True if any touched pixel's nearest 1/z over the pixel
    is at least the buffer's."""
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    xmin, xmax = min(x0, x1, x2), max(x0, x1, x2)
    ymin, ymax = min(y0, y1, y2), max(y0, y1, y2)
    if xmax < 0 or ymax < 0 or xmin > R or ymin > R:
        return False
    e = 0 if inner else 1
    px0, px1 = max(0, int(np.floor(xmin)) - e), min(R - 1, int(np.floor(xmax)) + e)
    py0, py1 = max(0, int(np.floor(ymin)) - e), min(R - 1, int(np.floor(ymax)) + e)
    if abs(area) < 1e-4:
        if inner:
            return False
        w = max(w0, w1, w2)
        for y in range(py0, py1 + 1):
            for x in range(px0, px1 + 1):
                if w >= depth[y, x]:
                    return True
        return False
    s = 1.0 if area > 0 else -1.0
    a0, b0, c0 = s * (y0 - y1), s * (x1 - x0), s * (x0 * y1 - y0 * x1)
    a1, b1, c1 = s * (y1 - y2), s * (x2 - x1), s * (x1 * y2 - y1 * x2)
    a2, b2, c2 = s * (y2 - y0), s * (x0 - x2), s * (x2 * y0 - y2 * x0)
    pad0, pad1, pad2 = .5 * (abs(a0) + abs(b0)), .5 * (abs(a1) + abs(b1)), .5 * (abs(a2) + abs(b2))
    if inner:
        pad0, pad1, pad2 = -pad0, -pad1, -pad2
    A = ((w1 - w0) * (y2 - y0) - (w2 - w0) * (y1 - y0)) / area
    B = ((w2 - w0) * (x1 - x0) - (w1 - w0) * (x2 - x0)) / area
    C = w0 - A * x0 - B * y0
    spanW = .5 * (abs(A) + abs(B))
    for y in range(py0, py1 + 1):
        cy = y + .5
        for x in range(px0, px1 + 1):
            cx = x + .5
            if a0 * cx + b0 * cy + c0 + pad0 < 0 or a1 * cx + b1 * cy + c1 + pad1 < 0 or a2 * cx + b2 * cy + c2 + pad2 < 0:
                continue
            wc = A * cx + B * cy + C
            if inner:
                if wc - spanW > depth[y, x]:
                    depth[y, x] = wc - spanW
            elif wc + spanW >= depth[y, x]:
                return True
    return False


@njit(cache=True)
def draw(depth, R, V, i, poly, inner):
    """Clips view-space triangle i to z >= 1, projects it, rasterizes the result."""
    n = 0
    for k in range(3):
        a, b = V[i, k], V[i, (k + 1) % 3]
        if a[2] >= 1.0:
            poly[n] = a
            n += 1
        if (a[2] >= 1.0) != (b[2] >= 1.0):
            t = (1.0 - a[2]) / (b[2] - a[2])
            poly[n] = a + (b - a) * t
            n += 1
    if n < 3:
        return False
    for k in range(n):
        w = 1.0 / poly[k, 2]
        poly[k, 0], poly[k, 1], poly[k, 2] = (poly[k, 0] * w * .5 + .5) * R, (poly[k, 1] * w * .5 + .5) * R, w
    if raster(depth, R, poly[0, 0], poly[0, 1], poly[0, 2], poly[1, 0], poly[1, 1], poly[1, 2], poly[2, 0], poly[2, 1], poly[2, 2], inner):
        return True
    if n == 4:
        return raster(depth, R, poly[0, 0], poly[0, 1], poly[0, 2], poly[2, 0], poly[2, 1], poly[2, 2], poly[3, 0], poly[3, 1], poly[3, 2], inner)
    return False


@njit(parallel=True, cache=True)
def visibility(eyes, P, N, occluder, two_sided, block_first, block_count, R, chunks):
    axes = np.array([[[1, 0, 0], [0, 0, -1], [0, 1, 0]], [[-1, 0, 0], [0, 0, 1], [0, 1, 0]],
                     [[0, 1, 0], [1, 0, 0], [0, 0, -1]], [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
                     [[0, 0, 1], [1, 0, 0], [0, 1, 0]], [[0, 0, -1], [-1, 0, 0], [0, 1, 0]]], dtype=np.float64)
    T, B = P.shape[0], block_first.shape[0]
    out = np.zeros((eyes.shape[0], B), dtype=np.uint8)
    for c in prange(chunks):
        depth = np.empty((R, R))
        V = np.empty((T, 3, 3))
        poly = np.empty((4, 3))
        for e in range(c, eyes.shape[0], chunks):
            eye = eyes[e]
            for f in range(6):
                depth[:, :] = 0.0
                for i in range(T):
                    for k in range(3):
                        d = P[i, k] - eye
                        V[i, k, 0] = d[0] * axes[f, 1, 0] + d[1] * axes[f, 1, 1] + d[2] * axes[f, 1, 2]
                        V[i, k, 1] = d[0] * axes[f, 2, 0] + d[1] * axes[f, 2, 1] + d[2] * axes[f, 2, 2]
                        V[i, k, 2] = d[0] * axes[f, 0, 0] + d[1] * axes[f, 0, 1] + d[2] * axes[f, 0, 2]
                for i in range(T):
                    if not occluder[i]:
                        continue
                    if not two_sided[i]:
                        d = eye - P[i, 0]
                        if N[i, 0] * d[0] + N[i, 1] * d[1] + N[i, 2] * d[2] <= 0:
                            continue
                    draw(depth, R, V, i, poly, True)
                for b in range(B):
                    if out[e, b]:
                        continue
                    for i in range(block_first[b], block_first[b] + block_count[b]):
                        if draw(depth, R, V, i, poly, False):
                            out[e, b] = 1
                            break
    return out


@njit(cache=True)
def air_nodes(P, N, two_sided, origin, spacing, nx, ny, nz, max_height):
    """Grid nodes over the track: the nearest surface below faces up (or is
    two-sided) within max_height, and the nearest above isn't a floor seen
    from beneath."""
    air = np.zeros((nx, ny, nz), dtype=np.uint8)
    T = P.shape[0]
    xmin = np.empty(T); xmax = np.empty(T); zmin = np.empty(T); zmax = np.empty(T)
    for i in range(T):
        xmin[i] = min(P[i, 0, 0], P[i, 1, 0], P[i, 2, 0]); xmax[i] = max(P[i, 0, 0], P[i, 1, 0], P[i, 2, 0])
        zmin[i] = min(P[i, 0, 2], P[i, 1, 2], P[i, 2, 2]); zmax[i] = max(P[i, 0, 2], P[i, 1, 2], P[i, 2, 2])
    hy = np.empty(T); hup = np.empty(T, dtype=np.uint8); htwo = np.empty(T, dtype=np.uint8)
    for ix in range(nx):
        x = origin[0] + ix * spacing
        for iz in range(nz):
            z = origin[2] + iz * spacing
            n = 0
            for i in range(T):
                if x < xmin[i] or x > xmax[i] or z < zmin[i] or z > zmax[i]:
                    continue
                p0, p1, p2 = P[i, 0], P[i, 1], P[i, 2]
                d = (p1[0] - p0[0]) * (p2[2] - p0[2]) - (p2[0] - p0[0]) * (p1[2] - p0[2])
                if abs(d) < 1e-6:
                    continue
                u = ((x - p0[0]) * (p2[2] - p0[2]) - (p2[0] - p0[0]) * (z - p0[2])) / d
                v = ((p1[0] - p0[0]) * (z - p0[2]) - (x - p0[0]) * (p1[2] - p0[2])) / d
                if u < 0 or v < 0 or u + v > 1:
                    continue
                hy[n] = p0[1] + u * (p1[1] - p0[1]) + v * (p2[1] - p0[1])
                hup[n] = N[i, 1] > 0
                htwo[n] = two_sided[i]
                n += 1
            for iy in range(ny):
                y = origin[1] + iy * spacing
                below = -1; above = -1
                for k in range(n):
                    if hy[k] <= y and (below < 0 or hy[k] > hy[below]):
                        below = k
                    if hy[k] > y and (above < 0 or hy[k] < hy[above]):
                        above = k
                if below < 0 or y - hy[below] > max_height or not (hup[below] or htwo[below]):
                    continue
                if above >= 0 and hup[above] and not htwo[above]:
                    continue
                air[ix, iy, iz] = 1
    return air


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('gmdl')
    parser.add_argument('out')
    parser.add_argument('--spacing', type=float, default=500.0)
    parser.add_argument('--resolution', type=int, default=256)
    parser.add_argument('--max-height', type=float, default=3000.0)
    parser.add_argument('--ranges', default=os.path.join(os.path.dirname(__file__), '../../include/mkw/road_sampler_ranges.hpp'),
                        help='road_sampler_ranges.hpp the game was built with (generated; in the clean build)')
    a = parser.parse_args()
    start = time.time()
    positions, normals, index, submeshes, samplers = read_gmdl(a.gmdl)
    name = os.path.basename(a.gmdl.split('.pak:')[-1])
    split = road_sampling(name, len(positions), len(index), submeshes, samplers, a.ranges)
    if len(split) != len(submeshes):
        print(f'road sampler split: {len(submeshes)} -> {len(split)} submeshes', flush=True)
    submeshes = split

    # The game's contactIndices: every submesh sorted. Its hash checks this
    # tool against the game; order-safe submeshes draw from it.
    contact = index.copy()
    for first, count, _, _ in submeshes:
        contact[first:first + count] = spatial_sort(positions, index, first, count)
    hash_ = 2166136261
    for value in contact.tobytes():
        hash_ = ((hash_ ^ value) * 16777619) & 0xffffffff

    tri_index, occluder, two_sided, block_first, block_count = [], [], [], [], []
    for first, count, flags, _ in submeshes:
        safe = order_safe(flags)
        source = contact if safe else index
        for at in range(first, first + count, 384):
            end = min(at + 384, first + count)
            block_first.append(len(tri_index) // 3)
            tri_index.extend(source[at:end - (end - at) % 3])
            block_count.append(len(tri_index) // 3 - block_first[-1])
            n = (end - at) // 3
            occluder.extend([safe and not (flags & 1)] * n)
            two_sided.extend([bool(flags & 4)] * n)
    tri_index = np.array(tri_index, dtype=np.int64).reshape(-1, 3)
    P = positions[tri_index].astype(np.float64)
    N = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    # Front faces: the winding that agrees with the authored vertex normals.
    authored = index[: (len(index) // 3) * 3].reshape(-1, 3)
    geometric = np.cross(positions[authored[:, 1]] - positions[authored[:, 0]], positions[authored[:, 2]] - positions[authored[:, 0]])
    agree = np.sign(np.einsum('ij,ij->i', geometric, normals[authored].sum(axis=1))).sum()
    if agree < 0:
        N = -N
    occluder = np.array(occluder, dtype=np.uint8)
    two_sided = np.array(two_sided, dtype=np.uint8)
    block_first = np.array(block_first, dtype=np.int64)
    block_count = np.array(block_count, dtype=np.int64)
    B = len(block_first)
    print(f'{a.gmdl}: {len(P)} triangles, {B} blocks, winding {"+" if agree >= 0 else "-"} (agreement {agree:.0f})', flush=True)

    s = a.spacing
    lo, hi = positions.min(axis=0).astype(np.float64), positions.max(axis=0).astype(np.float64)
    origin = np.floor(lo / s) * s
    nx = int(np.ceil((hi[0] - origin[0]) / s)) + 1
    ny = int(np.ceil((hi[1] + a.max_height - origin[1]) / s)) + 1
    nz = int(np.ceil((hi[2] - origin[2]) / s)) + 1
    air = air_nodes(P, N, two_sided, origin, s, nx, ny, nz, a.max_height)
    nodes = np.argwhere(air)
    print(f'grid {nx}x{ny}x{nz} at {s:.0f}, {len(nodes)} sample points in the air ({time.time() - start:.0f} s)', flush=True)

    eyes = origin + nodes.astype(np.float64) * s
    masks = visibility(eyes, P, N, occluder, two_sided, block_first, block_count, a.resolution, 160)
    print(f'visibility done ({time.time() - start:.0f} s)', flush=True)

    # Cells: 2x2x2 spacing; cell (i,j,k) spans nodes 2i..2i+2. Its list is the
    # union over nodes 2i-1..2i+3 (one spacing past each face); it has one
    # only if a node inside it is in the air. Identical lists share one entry.
    words = (B + 31) // 32
    packed = np.packbits(masks, axis=1, bitorder='little')
    packed = np.pad(packed, ((0, 0), (0, words * 4 - packed.shape[1])))
    grid = np.zeros((nx + 4, ny + 4, nz + 4, words * 4), dtype=np.uint8)
    inside = np.zeros((nx + 4, ny + 4, nz + 4), dtype=np.uint8)
    grid[nodes[:, 0] + 1, nodes[:, 1] + 1, nodes[:, 2] + 1] = packed
    inside[nodes[:, 0] + 1, nodes[:, 1] + 1, nodes[:, 2] + 1] = 1
    cx, cy, cz = (nx + 1) // 2, (ny + 1) // 2, (nz + 1) // 2
    union = np.zeros((cx, cy, cz, words * 4), dtype=np.uint8)
    has = np.zeros((cx, cy, cz), dtype=np.uint8)
    for dx in range(5):
        for dy in range(5):
            for dz in range(5):
                # padded node 2i+dx is node 2i+dx-1, i.e. 2i-1..2i+3
                union |= grid[dx:dx + 2 * cx:2, dy:dy + 2 * cy:2, dz:dz + 2 * cz:2]
                if 1 <= dx <= 3 and 1 <= dy <= 3 and 1 <= dz <= 3:
                    has |= inside[dx:dx + 2 * cx:2, dy:dy + 2 * cy:2, dz:dz + 2 * cz:2]
    cells = np.full((cx, cy, cz), 0xffff, dtype=np.uint16)
    valid = has.astype(bool)
    lists, inverse = np.unique(union[valid], axis=0, return_inverse=True)
    if len(lists) >= 0xffff:
        sys.exit('too many distinct lists')
    cells[valid] = inverse.reshape(-1).astype(np.uint16)
    bits = np.unpackbits(union[valid], axis=1, bitorder='little')[:, :B].astype(bool)
    visible = (bits * block_count[None, :]).sum(axis=1)
    print(f'cells {cx}x{cy}x{cz}, {valid.sum()} with a list, {len(lists)} distinct lists, '
          f'mean visible {100 * visible.mean() / len(P):.1f}% of triangles (min {100 * visible.min() / len(P):.1f}%, '
          f'max {100 * visible.max() / len(P):.1f}%) ({time.time() - start:.0f} s)', flush=True)

    # PVS1: u32 magic, vertices, indices, sorted-index hash, blocks, cells
    # x/y/z, list count; f32 origin x/y/z, cell size; the lists (blocks/32
    # u32 each, bit b = block b); one u16 list index per cell, x-major then y
    # then z (0xffff: none).
    with open(a.out, 'wb') as f:
        f.write(struct.pack('<9I', 0x31535650, len(positions), len(index), hash_, B, cx, cy, cz, len(lists)))
        f.write(struct.pack('<4f', origin[0], origin[1], origin[2], 2 * s))
        f.write(lists.tobytes())
        f.write(cells.tobytes())


if __name__ == '__main__':
    main()
