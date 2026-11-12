"""Read Mario Kart 7 UI textures from the user's own decrypted cartridge dump.

Only the bottom-screen map markers are taken (docs/BOTTOM_SCREEN.md): they
can be decoded into the private build/race-map overlay at cook time. The eight
original-project marker PNGs are also retained as default inputs under
tools/release/mk7_markers. The optional dump is opened read-only.

CCI (NCSD) -> NCCH partition 0 (NoCrypto) -> RomFS level 3 -> /UI/race.szs
(Yaz0 + SARC, names hashed with key 0x65) -> BCLIM (PICA tiled ETC1A4/RGBA).
"""
import struct
from pathlib import Path

import numpy as np

DEFAULT_CCI = Path.home() / 'Downloads/Mario Kart 7 (USA) (En,Fr,Es) (Rev 1)/Mario Kart 7 (USA) (En,Fr,Es) (Rev 1).cci'


class RomFS:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.read(0, 0x200)
        if h[0x100:0x104] != b'NCSD':
            raise ValueError('Not a CCI image: ' + str(path))
        part = struct.unpack_from('<I', h, 0x120)[0] * 0x200
        n = self.read(part, 0x200)
        if n[0x100:0x104] != b'NCCH' or not n[0x18f] & 4:
            raise ValueError('Expected a NoCrypto NCCH (a decrypted dump)')
        romfs = part + struct.unpack_from('<I', n, 0x1b0)[0] * 0x200
        ivfc = self.read(romfs, 0x60)
        if ivfc[:4] != b'IVFC':
            raise ValueError('RomFS IVFC header missing')
        master = struct.unpack_from('<I', ivfc, 8)[0]
        block = 1 << struct.unpack_from('<I', ivfc, 0x4c)[0]
        level3 = romfs + (0x60 + master + block - 1) // block * block
        header = struct.unpack('<10I', self.read(level3, 0x28))
        self.dirs = self.read(level3 + header[3], header[4])
        self.file_meta = self.read(level3 + header[7], header[8])
        self.data = level3 + header[9]
        self.files = {}
        self._walk(0, '')

    def read(self, offset, size):
        self.f.seek(offset)
        return self.f.read(size)

    def _walk(self, d, path):
        parent, sibling, child, first_file, _, length = struct.unpack_from('<6I', self.dirs, d)
        name = self.dirs[d + 0x18:d + 0x18 + length].decode('utf-16le')
        here = path + '/' + name if d else ''
        f = first_file
        while f != 0xffffffff:
            _, next_file, offset, size, _, flen = struct.unpack_from('<IIQQII', self.file_meta, f)
            self.files[here + '/' + self.file_meta[f + 0x20:f + 0x20 + flen].decode('utf-16le')] = (offset, size)
            f = next_file
        c = child
        while c != 0xffffffff:
            self._walk(c, here)
            c = struct.unpack_from('<I', self.dirs, c + 4)[0]

    def get(self, path):
        offset, size = self.files[path]
        return self.read(self.data + offset, size)


def yaz0(b):
    if b[:4] != b'Yaz0':
        return b
    size = struct.unpack_from('>I', b, 4)[0]
    out = bytearray()
    i = 16
    while len(out) < size:
        code = b[i]
        i += 1
        for k in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> k):
                out.append(b[i])
                i += 1
            else:
                b1, b2 = b[i], b[i + 1]
                i += 2
                dist = ((b1 & 15) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = b[i] + 0x12
                    i += 1
                else:
                    n += 2
                start = len(out) - dist
                for j in range(n):
                    out.append(out[start + j])
    return bytes(out)


def sarc(b):
    """{hash: bytes} of one SARC archive."""
    b = yaz0(b)
    if b[:4] != b'SARC':
        raise ValueError('Not a SARC archive')
    e = '<' if b[6:8] == b'\xff\xfe' else '>'
    header, _, _, data, _ = struct.unpack_from(e + 'HHIIH', b, 4)
    _, count, _ = struct.unpack_from(e + 'HHI', b, header + 4)
    files = {}
    for i in range(count):
        name_hash, attr, start, end = struct.unpack_from(e + 'IIII', b, header + 12 + 16 * i)
        files[name_hash] = b[data + start:data + end]
    return files


def name_hash(name, key=0x65):
    value = 0
    for c in name.encode():
        value = (value * key + c) & 0xffffffff
    return value


ETC1_MODIFIERS = [[2, 8, -2, -8], [5, 17, -5, -17], [9, 29, -9, -29], [13, 42, -13, -42],
                  [18, 60, -18, -60], [24, 80, -24, -80], [33, 106, -33, -106], [47, 183, -47, -183]]


def etc1_block(v):
    """One ETC1 4x4 block (64-bit, 3DS little-endian word) as [y][x] RGB."""
    flip, diff = (v >> 32) & 1, (v >> 33) & 1
    table1, table2 = (v >> 37) & 7, (v >> 34) & 7
    if diff:
        base = [(v >> s) & 31 for s in (59, 51, 43)]
        delta = [(v >> s) & 7 for s in (56, 48, 40)]
        delta = [d - 8 if d > 3 else d for d in delta]
        c1 = [(c << 3) | (c >> 2) for c in base]
        c2 = [((c + d) << 3) | ((c + d) >> 2) for c, d in zip(base, delta)]
    else:
        c1 = [((v >> s) & 15) * 17 for s in (60, 52, 44)]
        c2 = [((v >> s) & 15) * 17 for s in (56, 48, 40)]
    out = np.zeros((4, 4, 3), np.uint8)
    for x in range(4):
        for y in range(4):
            i = x * 4 + y
            index = ((v >> (16 + i)) & 1) << 1 | ((v >> i) & 1)
            second = y >= 2 if flip else x >= 2
            mod = ETC1_MODIFIERS[table2 if second else table1][index]
            out[y, x] = [max(0, min(255, c + mod)) for c in (c2 if second else c1)]
    return out


def bclim(b):
    """Decode a BCLIM to an RGBA uint8 array (rows top to bottom)."""
    if b[-0x28:-0x24] != b'CLIM':
        raise ValueError('Not a BCLIM')
    imag = len(b) - 0x14
    width, height, fmt = struct.unpack_from('<HHI', b, imag + 8)
    data = b[:len(b) - 0x28]
    tw, th = max(8, 1 << (width - 1).bit_length()), max(8, 1 << (height - 1).bit_length())
    img = np.zeros((th, tw, 4), np.uint8)
    if fmt in (10, 11):
        o = 0
        for ty in range(0, th, 8):
            for tx in range(0, tw, 8):
                for sub in range(4):
                    bx, by = tx + (sub & 1) * 4, ty + (sub >> 1) * 4
                    alpha = None
                    if fmt == 11:
                        alpha = struct.unpack_from('<Q', data, o)[0]
                        o += 8
                    img[by:by + 4, bx:bx + 4, :3] = etc1_block(struct.unpack_from('<Q', data, o)[0])
                    o += 8
                    for x in range(4):
                        for y in range(4):
                            img[by + y, bx + x, 3] = ((alpha >> (4 * (x * 4 + y))) & 15) * 17 if alpha is not None else 255
    else:
        bpp = {9: 4, 8: 2, 5: 2, 3: 2, 0: 1}[fmt]
        o = 0
        for ty in range(0, th, 8):
            for tx in range(0, tw, 8):
                for i in range(64):
                    x = sum(((i >> (2 * k)) & 1) << k for k in range(3))
                    y = sum(((i >> (2 * k + 1)) & 1) << k for k in range(3))
                    p = data[o:o + bpp]
                    o += bpp
                    if fmt == 9:
                        img[ty + y, tx + x] = [p[3], p[2], p[1], p[0]]
                    elif fmt == 8:
                        v = p[0] | p[1] << 8
                        img[ty + y, tx + x] = [(v >> 12) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17]
                    elif fmt == 5:
                        v = p[0] | p[1] << 8
                        img[ty + y, tx + x] = [(v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255]
                    elif fmt == 3:
                        img[ty + y, tx + x] = [p[1], p[1], p[1], p[0]]
                    else:
                        img[ty + y, tx + x] = [p[0], p[0], p[0], 255]
    # PICA textures are stored bottom row first (every map marker is a power of two).
    return img[::-1][:height, :width]


def race_textures(cci=DEFAULT_CCI, names=()):
    """{name: RGBA array} for the named /UI/race.szs textures (un-rotated from _r90)."""
    archive = sarc(RomFS(cci).get('/UI/race.szs'))
    out = {}
    for name in names:
        blob = archive.get(name_hash(name))
        if blob is None:
            raise KeyError('MK7 race.szs has no ' + name)
        image = bclim(blob)
        # Bottom-screen layouts store their art rotated 90 degrees for the LCD.
        if name.endswith('_r90.bclim'):
            image = np.rot90(image, k=-1)
        out[name] = np.ascontiguousarray(image)
    return out
