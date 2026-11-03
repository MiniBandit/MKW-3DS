"""Offline top-down render of a cooked course for the bottom-screen map.

Reads exactly what the 3DS draws: the course's GMDL parts (models/course_NN.list,
build/course-materials overrides first), their cooked textures (the PNG each
.t3x was encoded from) and the submesh material flags the renderer interprets
(src/renderer.cpp Renderer::drawNow). Nothing here runs on the console.

The view is orthographic, looking straight down: +x to the right and -z up,
the retail minimap's orientation (Map2DRenderer, posLD below posRU). Depth is
height, so the highest surface wins, as seen from above.

Roofs, tunnels and banners over the road are removed with the course's own
collision: a fragment more than CLEARANCE units above the highest drivable
road surface beneath it (KCL road types) is discarded, so the road shows
through, as on Mario Kart 7's maps. This reads course.kcl as data; the game
itself never queries KCL for presentation.
"""
import math
import struct
from pathlib import Path

from gx_material import GMDL_SUBMESH_BYTES
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SD = ROOT / 'sd-card/3ds/mkw-3ds'
OVERLAY = ROOT / 'build/course-materials'

# KCL collision types (attribute & 0x1f) that are road a kart drives on.
# Off-road (0x02-0x04), walls and triggers do not cut geometry above them.
ROAD_TYPES = {0x00, 0x01, 0x05, 0x06, 0x07, 0x08, 0x09, 0x13, 0x15, 0x16, 0x17, 0x1D}
# Every ground type, for the road-surface statistics (area / route length).
FLOOR_TYPES = ROAD_TYPES | {0x02, 0x03, 0x04, 0x0A, 0x0B}
CLEARANCE = 700.0

SUBMESH = struct.Struct('<IIiIiI4fIIII')


def course_path(relative):
    for base in (OVERLAY, SD):
        p = base / relative
        if p.is_file():
            return p
    raise FileNotFoundError(relative)


class Part:
    """One GMDL v3/v4 part as numpy arrays."""

    def __init__(self, path):
        b = path.read_bytes()
        magic, version, nv, ni, ns, nt = struct.unpack_from('<4s5I', b)
        if magic != b'GMDL' or version not in GMDL_SUBMESH_BYTES:
            raise ValueError(f'Not a GMDL v3/v4 model: {path}')
        record = GMDL_SUBMESH_BYTES[version]
        self.submeshes = []
        for i in range(ns):
            raw = b[24 + i * record:24 + (i + 1) * record]
            if version == 3:
                raw += bytes(12)
            self.submeshes.append(SUBMESH.unpack(raw[:56]))
        at = 24 + ns * record
        self.textures = [b[at + i * 64:at + (i + 1) * 64].split(b'\0')[0].decode() for i in range(nt)]
        at += nt * 64
        v = np.frombuffer(b, dtype=np.dtype([('p', '<f4', 3), ('n', '<f4', 3), ('uv', '<f4', 2),
                                             ('uv1', '<f4', 2), ('c', 'u1', 4)]), count=nv, offset=at)
        self.position = v['p'].astype(np.float64)
        self.normal = v['n'].astype(np.float64)
        self.uv = v['uv'].astype(np.float64)
        self.uv1 = v['uv1'].astype(np.float64)
        self.color = v['c'].astype(np.float64) / 255.0
        at += nv * 44
        self.indices = np.frombuffer(b, dtype='<u2', count=ni, offset=at).astype(np.int64)


class Texture:
    """A cooked course texture with box-filtered mip levels (RGBA float)."""

    cache = {}

    @classmethod
    def get(cls, name):
        if name not in cls.cache:
            cls.cache[name] = cls(name)
        return cls.cache[name]

    def __init__(self, name):
        image = Image.open(course_path(f'textures/{name}.png')).convert('RGBA')
        level = np.asarray(image, dtype=np.float32) / 255.0
        self.levels = [level]
        while min(level.shape[0], level.shape[1]) > 1:
            h, w = level.shape[0] // 2 * 2, level.shape[1] // 2 * 2
            level = level[:h, :w].reshape(h // 2, 2, w // 2, 2, 4).mean(axis=(1, 3))
            self.levels.append(level)

    def sample(self, u, v, wrap_s, wrap_t, lod):
        """Bilinear sample at GMDL UVs (V stored as 1-v), GX wrap modes 0 clamp, 1 repeat, 2 mirror."""
        tex = self.levels[min(max(int(lod), 0), len(self.levels) - 1)]
        h, w = tex.shape[:2]
        x = u * w - 0.5
        y = (1.0 - v) * h - 0.5

        def wrap(c, n, mode):
            if mode == 1:
                return np.mod(c, n)
            if mode == 2:
                c = np.mod(c, 2 * n)
                return np.where(c < n, c, 2 * n - 1 - c)
            return np.clip(c, 0, n - 1)
        x0 = np.floor(x)
        y0 = np.floor(y)
        fx = (x - x0)[:, None]
        fy = (y - y0)[:, None]
        x0 = x0.astype(np.int64)
        y0 = y0.astype(np.int64)
        xa, xb = wrap(x0, w, wrap_s), wrap(x0 + 1, w, wrap_s)
        ya, yb = wrap(y0, h, wrap_t), wrap(y0 + 1, h, wrap_t)
        return ((tex[ya, xa] * (1 - fx) + tex[ya, xb] * fx) * (1 - fy) +
                (tex[yb, xa] * (1 - fx) + tex[yb, xb] * fx) * fy)


def kcl_triangles(path):
    """Course collision prisms as (A, B, C) triangles plus their attribute."""
    b = path.read_bytes()
    pos_off, nrm_off, prism_off, block_off = struct.unpack_from('>4I', b, 0)
    positions = np.frombuffer(b, '>f4', count=(nrm_off - pos_off) // 4, offset=pos_off).reshape(-1, 3).astype(np.float64)
    normals = np.frombuffer(b, '>f4', count=(prism_off + 0x10 - nrm_off) // 4, offset=nrm_off)
    normals = normals[:len(normals) // 3 * 3].reshape(-1, 3).astype(np.float64)
    count = (block_off - prism_off - 0x10) // 0x10
    prisms = np.frombuffer(b, np.dtype([('h', '>f4'), ('p', '>u2'), ('f', '>u2'), ('e1', '>u2'), ('e2', '>u2'),
                                        ('e3', '>u2'), ('a', '>u2')]), count=count, offset=prism_off + 0x10)
    a = positions[prisms['p']]
    face = normals[prisms['f']]
    cross_a = np.cross(normals[prisms['e1']], face)
    cross_b = np.cross(normals[prisms['e2']], face)
    e3 = normals[prisms['e3']]
    h = prisms['h'].astype(np.float64)[:, None]
    with np.errstate(divide='ignore', invalid='ignore'):
        bpt = a + cross_b * (h / np.sum(cross_b * e3, axis=1, keepdims=True))
        cpt = a + cross_a * (h / np.sum(cross_a * e3, axis=1, keepdims=True))
    ok = np.isfinite(bpt).all(axis=1) & np.isfinite(cpt).all(axis=1)
    return a[ok], bpt[ok], cpt[ok], (prisms['a'][ok] & 0x1f).astype(np.int64), face[ok]


class Frame:
    """World XZ rectangle -> pixel grid (x right, -z up)."""

    def __init__(self, x0, z0, x1, z1, width, height):
        self.x0, self.z0, self.x1, self.z1 = x0, z0, x1, z1
        self.width, self.height = width, height
        self.sx = width / (x1 - x0)
        self.sz = height / (z1 - z0)

    def to_pixels(self, p):
        return np.stack([(p[..., 0] - self.x0) * self.sx, (p[..., 2] - self.z0) * self.sz], axis=-1)


def raster(tri_xy, width, height):
    """Pixel centres covered by one 2D triangle: (iy, ix, w0, w1, w2) or None."""
    (ax, ay), (bx, by), (cx, cy) = tri_xy
    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
    if abs(area) < 1e-12:
        return None
    x0 = max(int(math.floor(min(ax, bx, cx))), 0)
    x1 = min(int(math.ceil(max(ax, bx, cx))), width - 1)
    y0 = max(int(math.floor(min(ay, by, cy))), 0)
    y1 = min(int(math.ceil(max(ay, by, cy))), height - 1)
    if x0 > x1 or y0 > y1:
        return None
    ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
    px = xs.ravel() + 0.5
    py = ys.ravel() + 0.5
    w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area
    w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -1e-9) & (w1 >= -1e-9) & (w2 >= -1e-9)
    if not inside.any():
        return None
    return ys.ravel()[inside], xs.ravel()[inside], w0[inside], w1[inside], w2[inside]


def floor_maps(kcl, frame):
    """Highest road surface and highest ground of any kind per pixel (-inf where none)."""
    a, b, c, kind, face = kcl
    road = np.full((frame.height, frame.width), -np.inf)
    ground = np.full((frame.height, frame.width), -np.inf)
    upward = face[:, 1] > 0.2
    for i in np.nonzero(upward & np.isin(kind, list(FLOOR_TYPES)))[0]:
        tri = np.stack([a[i], b[i], c[i]])
        cov = raster(frame.to_pixels(tri), frame.width, frame.height)
        if cov is None:
            continue
        iy, ix, w0, w1, w2 = cov
        y = w0 * tri[0, 1] + w1 * tri[1, 1] + w2 * tri[2, 1]
        np.maximum.at(ground, (iy, ix), y)
        if kind[i] in ROAD_TYPES:
            np.maximum.at(road, (iy, ix), y)
    return road, ground


def dilate_max(values, radius):
    out = values.copy()
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy > radius * radius:
                continue
            shifted = np.full_like(values, -np.inf)
            ys = slice(max(dy, 0), values.shape[0] + min(dy, 0))
            yd = slice(max(-dy, 0), values.shape[0] + min(-dy, 0))
            xs = slice(max(dx, 0), values.shape[1] + min(dx, 0))
            xd = slice(max(-dx, 0), values.shape[1] + min(-dx, 0))
            shifted[yd, xd] = values[ys, xs]
            np.maximum(out, shifted, out=out)
    return out


def course_lighting(archive):
    """Lights (flags, space, direction, colour) and ambient colours of one course
    from include/mkw/course_lighting.hpp (tools/cook_course_lighting.py)."""
    import re
    text = (ROOT / 'include/mkw/course_lighting.hpp').read_text()
    start = text.index('{"' + archive + '"')
    body = text[start:text.index('\n', start)]
    lights = [(int(f), int(s), (float(x), float(y), float(z)), int(c, 16))
              for f, s, t, x, y, z, c, d in re.findall(
                  r'\{(\d+),(\d+),(\d+),\{([-\d.e]+)f,([-\d.e]+)f,([-\d.e]+)f\},0x([0-9a-f]+)u,0x([0-9a-f]+)u\}', body)][:16]
    ambient_block = body[body.index('}},{', body.index('{{')) + 4:]
    ambient = [int(v, 16) for v in re.findall(r'0x([0-9a-f]+)u', ambient_block)[:16]]
    return lights, ambient


# The top-down camera: view rows right (+x), up (-z) and back toward the eye (+y).
VIEW = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]])


def light_toward(light):
    """L (toward the light) in view space, as retailLightViewDirection."""
    flags, space, direction, colour = light
    d = np.array(direction)
    if space == 2:
        v = d
    elif space == 3:
        v = np.array([0.0, 0.0, -1.0])
    elif space == 4:
        v = np.array([0.0, 0.0, 1.0])
    elif space == 1:
        forward = -VIEW[2]
        half = -d / max(np.linalg.norm(d), 1e-9) + -forward
        v = -(VIEW @ (half / max(np.linalg.norm(half), 1e-9)))
    else:
        v = VIEW @ d
    return -v


def unit(colour, i):
    return ((colour >> (24 - 8 * i)) & 255) / 255.0


def primary_colour(sub, part_colors, normals, lights, ambient):
    """GX colour channel 0: vertex colour times the retail channel (or as authored)."""
    first, count, tex, flags, sec, constant, u0, v0, u1, v1, anim, lighting, amb, add = sub
    if not (lighting & 1):
        return part_colors
    channel = (lighting >> 2) & 3
    if channel == 0:
        return part_colors
    light_set = (lighting >> 4) & 7
    alpha_lit = bool(lighting & (1 << 7))
    amb_rgba = [math.floor(0.5 + unit(ambient[0], i) * 255 * unit(amb, i)) / 255.0 for i in range(4)]
    if not alpha_lit:
        amb_rgba[3] = 1.0
    light = lights[light_set]
    on = (light[0] & 0x41) == 0x41
    diffuse = [unit(light[3], i) if on and (light[0] & 0x200) else 0.0 for i in range(3)]
    diffuse.append(unit(light[3], 3) if alpha_lit and on and (light[0] & 0x400) else 0.0)
    toward = light_toward(light)
    fn = (lighting >> 8) & 3
    a, b, c = [(0, 1, 0), (1, 0, -2), (1, 0, 0)][fn if fn < 3 else 2]
    nv = normals @ VIEW.T
    nv /= np.maximum(np.linalg.norm(nv, axis=1, keepdims=True), 1e-6)
    df = np.maximum(nv @ toward * a + b, c)
    scale = np.minimum(np.array(amb_rgba)[None, :] + np.array(diffuse)[None, :] * df[:, None], 1.0)
    return part_colors * scale


SCALES = (1.0, 2.0, 4.0, 1.0)


def rgba_const(constant):
    """Submesh constants are 0xAABBGGRR (PICA TEV colour order)."""
    return np.array([(constant >> s & 255) / 255.0 for s in (0, 8, 16, 24)])


def shade(sub, textures, vc, uv0, uv1, lod0, lod1):
    """TEV stages of Renderer::drawNow's default material path; returns RGBA (N,4)."""
    first, count, tex, flags, sec, constant, *_ = sub
    n = len(vc)
    white = np.ones((n, 4))
    t0 = textures[tex].sample(uv0[:, 0], uv0[:, 1], (flags >> 8) & 3, (flags >> 10) & 3, lod0) if tex >= 0 else white
    t1 = textures[sec].sample(uv1[:, 0], uv1[:, 1], (flags >> 16) & 3, (flags >> 18) & 3, lod1) if sec >= 0 else white
    k = rgba_const(constant)
    mode = (flags >> 12) & 3
    bit25 = bool(flags & (1 << 25))
    scale0 = SCALES[(flags >> 4) & 3]
    rgb = t0[:, :3] * vc[:, :3] * scale0
    alpha = t0[:, 3] * vc[:, 3]
    water = bool(flags & (1 << 29)) and not (flags & 1)
    if water:
        rgb = np.broadcast_to(np.array([0x40, 0x5a, 0x88]) / 255.0, (n, 3)).copy()
    if bit25 and mode == 0:
        rgb = vc[:, :3] * t0[:, :3] + k[None, :3] * (1 - t0[:, :3])
    if water:
        pass
    elif bit25 and mode in (1, 2):
        rgb = t0[:, :3] * vc[:, :3] + k[None, :3] * (1 - vc[:, :3])
        if mode == 1:
            rgb = t0[:, :3] * vc[:, :3] + rgb
    elif mode == 1:
        rgb = (vc[:, :3] * t1[:, :3] + k[None, :3] * (1 - t1[:, :3])) * SCALES[(flags >> 14) & 3]
        rgb = np.clip(rgb, 0, 1) * t0[:, :3] * scale0
        alpha = t1[:, 3]
    elif mode == 2:
        rgb = np.clip(rgb, 0, 1) + t1[:, :3] * k[3]
    elif bit25 and mode == 3:
        rgb = t0[:, :3] * k[None, :3]
        rgb = np.clip(np.clip(rgb, 0, 1) + t1[:, :3] - 0.5, 0, 1) * vc[:, :3]
    elif mode == 3:
        rgb = t1[:, :3] * k[None, :3]
        alpha = t1[:, 3] * vc[:, 3]
        rgb = np.clip(rgb, 0, 1) + t0[:, :3]
        alpha = alpha + t0[:, 3]
    am = (flags >> 20) & 7
    if am == 1:
        alpha = t0[:, 3] * vc[:, 3]
    elif am == 2:
        alpha = t1[:, 3] * vc[:, 3]
    elif am in (3, 6):
        alpha = t0[:, 3] * t1[:, 3] * (2 if am == 6 else 1)
        alpha = np.clip(alpha, 0, 1) * vc[:, 3]
    elif am == 4:
        alpha = vc[:, 3]
    elif am == 5:
        alpha = t0[:, 3] * k[3] + t1[:, 3] * (1 - k[3])
    return np.concatenate([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)[:, None]], axis=1)


ALPHA_COMPARE = [lambda a, r: np.zeros_like(a, bool), lambda a, r: a < r, lambda a, r: a == r, lambda a, r: a <= r,
                 lambda a, r: a > r, lambda a, r: a != r, lambda a, r: a >= r, lambda a, r: np.ones_like(a, bool)]


def alpha_pass(sub, alpha8):
    """Alpha test of drawNow (sourceState: exact GX compare/reference)."""
    first, count, tex, flags, sec, constant, u0, v0, u1, v1, anim, *_ = sub
    if not flags & 1:
        return np.ones_like(alpha8, bool)
    source = bool(flags & (1 << 31))
    if source:
        extended = anim >> 16
        ref = (extended & 255) or (0, 128, 165, 255)[(flags >> 29) & 3]
        return ALPHA_COMPARE[(flags >> 26) & 7](alpha8, ref)
    return alpha8 > 127


def blend_mode(sub):
    """(blended, additive) of drawNow's alpha blend state."""
    flags = sub[3]
    blended = bool(flags & 2)
    source = bool(flags & (1 << 31))
    return blended, blended and source and bool(flags & 64)


def render(parts, frame, ceiling, lights, ambient, background):
    """Top-down render: RGBA float image, height buffer (-inf empty)."""
    H, W = frame.height, frame.width
    colour = np.empty((H, W, 3))
    colour[:] = background
    depth = np.full((H, W), -np.inf)
    blended = []
    texel_world = None
    for part in parts:
        textures = [Texture.get(name) for name in part.textures]
        pix = frame.to_pixels(part.position)
        lit = None
        for sub in part.submeshes:
            first, count, tex, flags, sec = sub[:5]
            if count == 0:
                continue
            tris = part.indices[first:first + count].reshape(-1, 3)
            if blend_mode(sub)[0]:
                for t in tris:
                    blended.append((part.position[t, 1].mean(), part, sub, t, textures, pix))
                continue
            vc_all = None
            for t in tris:
                draw_triangle(part, sub, t, textures, pix, frame, ceiling, lights, ambient, colour, depth, False)
    blended.sort(key=lambda e: e[0])
    for _, part, sub, t, textures, pix in blended:
        draw_triangle(part, sub, t, textures, pix, frame, ceiling, lights, ambient, colour, depth, True)
    return colour, depth


def lod_for(uv, xy, texture):
    """Mip level from the triangle's texel/pixel area ratio."""
    if texture is None:
        return 0
    base = texture.levels[0]
    (a, b, c) = xy
    pixel_area = abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]))
    ua, ub, uc = uv
    texel_area = abs((ub[0] - ua[0]) * (uc[1] - ua[1]) - (ub[1] - ua[1]) * (uc[0] - ua[0])) * base.shape[0] * base.shape[1]
    if pixel_area <= 1e-9:
        return len(texture.levels) - 1
    return max(0.0, 0.5 * math.log2(max(texel_area / pixel_area, 1e-9)))


def draw_triangle(part, sub, t, textures, pix, frame, ceiling, lights, ambient, colour, depth, blend):
    first, count, tex, flags, sec = sub[:5]
    xy = pix[t]
    cov = raster(xy, frame.width, frame.height)
    if cov is None:
        return
    iy, ix, w0, w1, w2 = cov
    p = part.position[t]
    y = w0 * p[0, 1] + w1 * p[1, 1] + w2 * p[2, 1]
    keep = y >= depth[iy, ix]
    keep &= y <= ceiling[iy, ix]
    if not keep.any():
        return
    iy, ix, w0, w1, w2, y = iy[keep], ix[keep], w0[keep], w1[keep], w2[keep], y[keep]
    w = np.stack([w0, w1, w2], axis=1)
    vc = w @ primary_colour(sub, part.color[t], part.normal[t], lights, ambient)
    uv0 = w @ part.uv[t]
    uv1 = w @ part.uv1[t]
    t0 = textures[tex] if tex >= 0 else None
    t1 = textures[sec] if sec >= 0 else None
    lod0 = lod_for(part.uv[t], xy, t0)
    lod1 = lod_for(part.uv1[t], xy, t1)
    rgba = shade(sub, textures, vc, uv0, uv1, lod0, lod1)
    ok = alpha_pass(sub, np.round(rgba[:, 3] * 255))
    if not ok.any():
        return
    iy, ix, y, rgba = iy[ok], ix[ok], y[ok], rgba[ok]
    if blend:
        blended, additive = blend_mode(sub)
        a = rgba[:, 3:4]
        dst = colour[iy, ix]
        colour[iy, ix] = np.clip(rgba[:, :3] * a + dst * (1 if additive else (1 - a)), 0, 1)
        return
    # Several fragments of one triangle never share a pixel: plain assignment.
    colour[iy, ix] = rgba[:, :3]
    depth[iy, ix] = y


def blur(values, radius):
    """Separable box blur, approximately Gaussian after three passes (edges clamped)."""
    out = values.astype(np.float64)
    if radius < 1:
        return out
    k = 2 * radius + 1
    for _ in range(3):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            c = np.cumsum(np.pad(out, pad, mode='edge'), axis=axis)
            if axis == 0:
                out = (c[k:] - c[:-k]) / k
            else:
                out = (c[:, k:] - c[:, :-k]) / k
    return out


def finish(colour, depth, frame, background, ground_height):
    """Map look: occlusion and relief from the height buffer, soft shadows under
    raised pieces, and a dark surround where the course model has no geometry.

    colour/depth come from render(); units_per_pixel converts world heights."""
    upp = (frame.x1 - frame.x0) / frame.width
    covered = np.isfinite(depth)
    h = np.where(covered, depth, ground_height)
    # Ambient occlusion: how far a pixel lies below its neighbourhood, at two radii.
    ao = np.ones_like(h)
    for radius_world, strength in ((350.0, 0.45), (1400.0, 0.30)):
        r = max(1, int(round(radius_world / upp)))
        lower = np.clip(blur(h, r) - h, 0, None)
        ao -= strength * np.clip(lower / (radius_world * 1.2), 0, 1)
    # Relief: light from the upper left (north-west, above) on the height field.
    gy, gx = np.gradient(blur(h, max(1, int(round(120.0 / upp)))), upp)
    n = np.stack([-gx, np.ones_like(h), -gy], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    light = np.array([-0.45, 0.80, -0.40])
    light /= np.linalg.norm(light)
    relief = 0.82 + 0.18 * np.clip(n @ light / light[1], 0, 1.25)
    # Soft drop shadow toward the lower right of anything raised above its surroundings.
    shift = max(1, int(round(220.0 / upp)))
    raised = np.full_like(h, -np.inf)
    raised[shift:, shift:] = h[:-shift, :-shift]
    shadow = np.clip((raised - h - 250.0) / 900.0, 0, 1)
    shadow = blur(shadow, max(1, int(round(180.0 / upp))))
    shade = np.clip(ao, 0.35, 1) * relief * (1 - 0.35 * shadow)
    out = colour * shade[..., None]
    # Gentle lift toward MK7's brighter, saturated map look.
    mean = out.mean(axis=-1, keepdims=True)
    out = np.clip(mean + (out - mean) * 1.12, 0, 1) ** 0.92
    # The surround takes the course's own edge colour, darkened, so the model's
    # border fades into it instead of ending at a hard line.
    border = covered & (blur(covered.astype(float), max(2, frame.width // 128)) < 0.9)
    if border.any():
        background = 0.42 * np.median(out[border], axis=0)
    surround = background[None, None, :] * (0.8 + 0.2 * blur(covered.astype(float), max(2, frame.width // 64)))[..., None]
    edge = blur(covered.astype(float), 2)[..., None]
    return out * edge + surround * (1 - edge)


def ceiling_map(road, ground, frame):
    """Highest visible height per pixel: CLEARANCE over the drivable road beneath,
    the course's highest road where no ground lies within ~3000 units (floating
    decorations over the void), and unbounded elsewhere."""
    upp = (frame.x1 - frame.x0) / frame.width
    top = np.max(road[np.isfinite(road)])
    ceiling = dilate_max(road, max(1, int(round(150.0 / upp)))) + CLEARANCE
    near_ground = blur(np.isfinite(ground).astype(float), max(1, int(round(1000.0 / upp)))) > 1e-4
    ceiling[~np.isfinite(ceiling)] = np.inf
    ceiling[~near_ground] = np.minimum(ceiling[~near_ground], top)
    return ceiling


def course_frame(kmp, margin=3000.0):
    """World rectangle around everything a racer can reach (KMP checkpoints, enemy and
    item routes, respawn and start points) plus a margin, and a texture size (512 or
    1024 a side, at most 1024x1024 = 512 KB of ETC1) with square texels."""
    pts = []
    for r in kmp['CKPT']['records']:
        pts += [r['left'], r['right']]
    for section in ('ENPT', 'ITPT', 'JGPT', 'KTPT'):
        pts += [(q['position'][0], q['position'][2]) for q in kmp.get(section, {}).get('records', [])]
    pts = np.array(pts)
    x0, x1 = pts[:, 0].min() - margin, pts[:, 0].max() + margin
    z0, z1 = pts[:, 1].min() - margin, pts[:, 1].max() + margin
    w, h = x1 - x0, z1 - z0
    size = [1024, 1024]
    if w >= h * 1.6:
        size[1] = 512
    elif h >= w * 1.6:
        size[0] = 512
    # Grow the shorter side so both axes share one texel size.
    texel = max(w / size[0], h / size[1])
    cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
    return (cx - texel * size[0] / 2, cz - texel * size[1] / 2, cx + texel * size[0] / 2, cz + texel * size[1] / 2), tuple(size)


def road_width(kcl, route_length):
    """Mean drivable road width: road surface area seen from above over the lap length."""
    a, b, c, kind, face = kcl
    road = np.isin(kind, list(ROAD_TYPES)) & (face[:, 1] > 0.2)
    ab, ac = b[road] - a[road], c[road] - a[road]
    area = 0.5 * np.abs(ab[:, 0] * ac[:, 2] - ab[:, 2] * ac[:, 0]).sum()
    return area / max(route_length, 1.0)


# KCL walls (attribute & 0x1f): wall, invisible, item, wall 2, player-only,
# weak, special and second invisible walls.
WALL_TYPES = {0x0C, 0x0D, 0x0E, 0x0F, 0x14, 0x19, 0x1E, 0x1F}


def road_span(kcl, left, right, anchor, step=25.0, gap=300.0):
    """The part of a KMP checkpoint segment (left, right; world XZ) that crosses
    drivable road, seen from above: the run of road containing the sample nearest
    `anchor` (the first start position: x, y, z), bridging gaps shorter than `gap`
    units and ending at the first wall either side. Checkpoints reach well past
    the road's edges; Mario Kart 7 draws its start line across the road only."""
    a, b, c, kind, face = kcl
    road = np.isin(kind, list(ROAD_TYPES)) & (face[:, 1] > 0.2)
    p0, p1 = np.asarray(left, float), np.asarray(right, float)
    lo_xz, hi_xz = np.minimum(p0, p1), np.maximum(p0, p1)
    xs = np.stack([a[:, 0], b[:, 0], c[:, 0]], axis=1)
    zs = np.stack([a[:, 2], b[:, 2], c[:, 2]], axis=1)
    near = road & (xs.max(axis=1) >= lo_xz[0]) & (xs.min(axis=1) <= hi_xz[0]) & (zs.max(axis=1) >= lo_xz[1]) & (zs.min(axis=1) <= hi_xz[1])
    ta, tb, tc = a[near][:, [0, 2]], b[near][:, [0, 2]], c[near][:, [0, 2]]
    length = float(np.hypot(*(p1 - p0)))
    n = max(2, int(length / step) + 1)
    t = np.linspace(0.0, 1.0, n)
    pts = p0[None, :] + t[:, None] * (p1 - p0)[None, :]

    def side(p, u, v):
        return (u[None, :, 0] - p[:, None, 0]) * (v[None, :, 1] - p[:, None, 1]) - (u[None, :, 1] - p[:, None, 1]) * (v[None, :, 0] - p[:, None, 0])
    s0, s1, s2 = side(pts, ta, tb), side(pts, tb, tc), side(pts, tc, ta)
    on = (((s0 >= 0) & (s1 >= 0) & (s2 >= 0)) | ((s0 <= 0) & (s1 <= 0) & (s2 <= 0))).any(axis=1)
    if not on.any():
        return tuple(p0), tuple(p1)
    # Bridge short gaps (seams, a narrow strip of another surface).
    idx = np.nonzero(on)[0]
    bridge = int(gap / (length / (n - 1)))
    for i, j in zip(idx[:-1], idx[1:]):
        if j - i <= bridge:
            on[i:j] = True
    d = (p1 - p0) / max(length, 1e-9)
    anchor_xz = np.asarray([anchor[0], anchor[2]], float)
    k = int(np.clip(round(float(np.dot(anchor_xz - p0, d)) / length * (n - 1)), 0, n - 1))
    if not on[k]:
        k = int(idx[np.argmin(np.abs(idx - k))])
    # Walls at the start's height that cross the segment end the run.
    wall = np.isin(kind, list(WALL_TYPES)) & (np.abs(face[:, 1]) < 0.7)
    ys = np.stack([a[:, 1], b[:, 1], c[:, 1]], axis=1)
    wall &= (ys.max(axis=1) > anchor[1] - 300.0) & (ys.min(axis=1) < anchor[1] + 1500.0)
    wall &= (xs.max(axis=1) >= lo_xz[0]) & (xs.min(axis=1) <= hi_xz[0]) & (zs.max(axis=1) >= lo_xz[1]) & (zs.min(axis=1) <= hi_xz[1])
    cuts = []
    r = p1 - p0
    for u, v in ((a, b), (b, c), (c, a)):
        q0, q1 = u[wall][:, [0, 2]], v[wall][:, [0, 2]]
        s = q1 - q0
        den = r[0] * s[:, 1] - r[1] * s[:, 0]
        with np.errstate(divide='ignore', invalid='ignore'):
            tt = ((q0[:, 0] - p0[0]) * s[:, 1] - (q0[:, 1] - p0[1]) * s[:, 0]) / den
            uu = ((q0[:, 0] - p0[0]) * r[1] - (q0[:, 1] - p0[1]) * r[0]) / den
        hit = np.isfinite(tt) & (tt >= 0) & (tt <= 1) & (uu >= 0) & (uu <= 1)
        cuts += list(tt[hit])
    for x in cuts:
        i = int(round(x * (n - 1)))
        if i != k:
            on[np.clip(i, 0, n - 1)] = False
    lo = k
    while lo > 0 and on[lo - 1]:
        lo -= 1
    hi = k
    while hi < n - 1 and on[hi + 1]:
        hi += 1
    half = 0.5 / (n - 1)
    ends = np.clip([t[lo] - half, t[hi] + half], 0.0, 1.0)
    return tuple(p0 + ends[0] * (p1 - p0)), tuple(p0 + ends[1] * (p1 - p0))


def bake(archive_key, model_list, lighting_archive, frame_rect, size, supersample=2):
    """The finished map image (RGB uint8, rows -z first) and render statistics."""
    kcl = kcl_triangles(ROOT / 'assets/courses' / archive_key / 'course.kcl')
    parts = [Part(course_path(f'models/{n}.gmdl')) for n in (SD / f'models/{model_list}.list').read_text().split()]
    x0, z0, x1, z1 = frame_rect
    frame = Frame(x0, z0, x1, z1, size[0] * supersample, size[1] * supersample)
    road, ground = floor_maps(kcl, frame)
    ceiling = ceiling_map(road, ground, frame)
    lights, ambient = course_lighting(lighting_archive)
    background = np.array([0.10, 0.13, 0.18])
    colour, depth = render(parts, frame, ceiling, lights, ambient, background)
    ground_height = np.median(ground[np.isfinite(ground)])
    image = finish(colour, depth, frame, background, ground_height)
    full = Image.fromarray((np.clip(image, 0, 1) * 255 + 0.5).astype(np.uint8))
    return full.resize(size, Image.Resampling.BOX), {
        'parts': len(parts), 'triangles': int(sum(len(p.indices) for p in parts) // 3),
        'coverage': float(np.isfinite(depth).mean()), 'cutPixels': float((np.isfinite(road)).mean())}
