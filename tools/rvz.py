# SPDX-License-Identifier: LicenseRef-MKW3DS
# Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
"""Read-only RVZ, Dolphin's compressed Wii disc image (format: Dolphin's
docs/WiaAndRvz.md), for extract_disc.Disc.

RVZ keeps a Wii partition's data decrypted and without its hash sectors, so
files are read from it directly: nothing is decrypted, encrypted or hashed
again, and no ISO is written. Groups may be stored or zstd compressed
(Dolphin's RVZ default). Padding that RVZ stores as a junk seed is never part
of a game file; reading it raises (zero seeds are zeros). The builder checks every extracted file's
SHA-256 against an unmodified USA disc (tools/release/usa-disc.json).
"""
import bisect, functools, hashlib, struct

try:
    from compression import zstd  # Python 3.14
    def _zstd(data): return zstd.decompress(data)
except ImportError:
    try:
        import zstandard  # Python 3.11-3.13 (requirements.txt)
        def _zstd(data): return zstandard.ZstdDecompressor().decompressobj().decompress(data)
    except ImportError:
        _zstd = None

SECTOR, DATA = 0x8000, 0x7C00  # Wii sector, and its data after the 0x400 hash block
GROUP_2MIB = 0x200000


class Rvz:
    def __init__(self, f):
        self.f = f
        f.seek(0)
        h1 = f.read(0x48)
        magic, _version, _compatible, size2 = struct.unpack_from('>4sIII', h1)
        if magic != b'RVZ\x01':
            raise ValueError('not an RVZ image')
        if hashlib.sha1(h1[:0x34]).digest() != h1[0x34:0x48]:
            raise ValueError('RVZ file header is damaged')
        h2 = f.read(size2)
        if len(h2) != size2 or hashlib.sha1(h2).digest() != h1[0x10:0x24]:
            raise ValueError('RVZ disc header is damaged')
        disc_type, compression, _level, self.chunk = struct.unpack_from('>IIiI', h2, 0)
        if disc_type != 2:
            raise ValueError('this RVZ is not a Wii disc')
        if compression not in (0, 5):
            raise ValueError('this RVZ uses an older compression (bzip2/LZMA): convert it in Dolphin to an ISO, or to RVZ with zstd')
        if compression == 5 and _zstd is None:
            raise ValueError('reading RVZ needs Python 3.14, or the zstandard package (pip install -r requirements.txt)')
        if self.chunk < SECTOR or self.chunk % SECTOR:
            raise ValueError('unsupported RVZ chunk size')
        self.compressed = compression == 5
        self.head = h2[0x10:0x90]  # the disc's first 0x80 bytes
        parts, part_size, part_off = struct.unpack_from('>IIQ', h2, 0x90)
        raws, raw_off, raw_size = struct.unpack_from('>IQI', h2, 0xB4)
        groups, group_off, group_size = struct.unpack_from('>IQI', h2, 0xC4)
        table = self._at(part_off, parts * part_size)
        if hashlib.sha1(table).digest() != h2[0xA0:0xB4]:
            raise ValueError('RVZ partition table is damaged')
        # Partition data entries: absolute first sector, sector count, first group, group count.
        self.parts = []
        for i in range(parts):
            key = table[i * part_size:i * part_size + 16]
            for e in range(2):
                first, count, group, ngroups = struct.unpack_from('>IIII', table, i * part_size + 16 + e * 16)
                if count:
                    self.parts.append((first, count, group, ngroups, key))
        self.parts.sort()
        raw = self._table(raw_off, raw_size, raws * 0x18)
        # Raw data: Dolphin rounds each entry's start down to a sector.
        self.raws = []
        for i in range(raws):
            offset, size, group, ngroups = struct.unpack_from('>QQII', raw, i * 0x18)
            skipped = offset % SECTOR
            self.raws.append((offset - skipped, size + skipped, group, ngroups))
        self.raws.sort()
        self.groups = self._table(group_off, group_size, groups * 12)
        self.ngroups = groups

    def _at(self, offset, size):
        self.f.seek(offset)
        b = self.f.read(size)
        if len(b) != size:
            raise ValueError('truncated RVZ image')
        return b

    def _table(self, offset, size, expect):
        b = self._at(offset, size)
        if self.compressed:
            b = _zstd(b)
        if len(b) < expect:
            raise ValueError('RVZ table is damaged')
        return b[:expect]

    @functools.lru_cache(maxsize=48)
    def _group(self, index, length, partition):
        """The group's data (partition groups: decrypted, hash exceptions skipped)
        and the (start, end) spans that are junk seeds rather than data."""
        if index >= self.ngroups:
            raise ValueError('RVZ group index out of range')
        offset4, size, packed = struct.unpack_from('>III', self.groups, index * 12)
        compressed, size = bool(size >> 31), size & 0x7FFFFFFF
        if size == 0:
            return bytes(length), ()
        b = self._at(offset4 * 4, size)
        if compressed:
            b = _zstd(b)
        at = 0
        if partition:
            # One hash exception list per 2 MiB of partition data the group spans.
            for _ in range(max(1, -(-self.chunk // GROUP_2MIB))):
                count = struct.unpack_from('>H', b, at)[0]
                at += 2 + count * 22
            if not compressed:
                at = (at + 3) & ~3
        b = b[at:]
        junk = []
        if packed:
            out, p = bytearray(), 0
            b = b[:packed]
            while p < len(b) and len(out) < length:
                (n,) = struct.unpack_from('>I', b, p)
                p += 4
                if n & 0x80000000:
                    # A generator seed (17 words). An all-zero seed generates
                    # zeros: RVZ packs runs of zeros inside files this way.
                    n &= 0x7FFFFFFF
                    if any(b[p:p + 68]):
                        junk.append((len(out), len(out) + n))
                    out += bytes(n)
                    p += 68
                else:
                    out += b[p:p + n]
                    p += n
            b = bytes(out)
        if len(b) < length:
            raise ValueError(f'RVZ group {index} is short')
        return b[:length], tuple(junk)

    def _take(self, index, length, partition, start, n):
        data, junk = self._group(index, length, partition)
        for a, z in junk:
            if a < start + n and start < z:
                raise ValueError('read reaches RVZ junk padding (not game file data)')
        return data[start:start + n]

    def read(self, offset, n):
        """Bytes outside the partitions' data: disc header, partition table,
        partition headers (ticket, TMD)."""
        out = []
        while n:
            if offset < 0x80:
                take = min(n, 0x80 - offset)
                out.append(self.head[offset:offset + take])
            else:
                i = bisect.bisect_right(self.raws, (offset, float('inf'))) - 1
                if i < 0 or offset >= self.raws[i][0] + self.raws[i][1]:
                    raise ValueError(f'RVZ has no raw data at {offset:#x}')
                start, size, group, ngroups = self.raws[i]
                k, within = divmod(offset - start, self.chunk)
                if k >= ngroups:
                    raise ValueError('RVZ raw data group out of range')
                length = min(self.chunk, start + size - (start + k * self.chunk))
                take = min(n, length - within)
                out.append(self._take(group + k, length, False, within, take))
            offset += take
            n -= take
        return b''.join(out)

    def partition_key(self, data_offset):
        """The title key RVZ stored for the partition whose data starts at data_offset."""
        sector = data_offset // SECTOR
        return next((key for first, _, _, _, key in self.parts if first == sector), None)

    def read_partition(self, data_offset, offset, n):
        """Decrypted partition data (0x7C00 per sector) from data_offset, the
        partition's data start on the disc, as Partition.read gives it."""
        base, spg, out = data_offset // SECTOR, self.chunk // SECTOR, []
        while n:
            block, within = divmod(offset, DATA)
            sector = base + block
            i = bisect.bisect_right(self.parts, (sector, float('inf'))) - 1
            if i < 0 or sector >= self.parts[i][0] + self.parts[i][1]:
                raise ValueError(f'RVZ has no partition data at sector {sector:#x}')
            first, count, group, ngroups, _ = self.parts[i]
            k, s = divmod(sector - first, spg)
            if k >= ngroups:
                raise ValueError('RVZ partition group out of range')
            length = min(spg, count - k * spg) * DATA
            start = s * DATA + within
            take = min(n, length - start)
            out.append(self._take(group + k, length, True, start, take))
            offset += take
            n -= take
        return b''.join(out)
