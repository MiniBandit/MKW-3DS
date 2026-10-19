"""Preserve the original Grand Prix reference times in course-id order."""
import struct
from pathlib import Path


def cook(source, target):
    data = source.read_bytes()
    magic, factor, count, header = struct.unpack_from('>4sIHH', data)
    if magic != b'RKGT' or count != 32 or header != 12 or len(data) != header + count * 8:
        raise ValueError('Unexpected original ranktimeGP.krt layout')
    rows = [struct.unpack_from('>4H', data, header + i * 8) for i in range(count)]
    if factor != 1000 or any(not all(row[:3]) for row in rows):
        raise ValueError('Missing original GP reference times')
    lines = ['// Generated from original ranktimeGP.krt; retail course-id order.',
             '#pragma once', '#include <cstdint>', 'namespace mkw {',
             'inline constexpr uint16_t GpRankTimes[32][4]={']
    lines += ['{' + ','.join(map(str, row)) + '},' for row in rows]
    lines += ['};', '}']
    target.write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    cook(root / 'assets/common/ranktimeGP.krt', root / 'include/mkw/gp_rank_times.hpp')
