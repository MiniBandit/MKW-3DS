"""Check that an existing .t3x is what a cook would write now.

Cooks that reuse an earlier .t3x when its PNG is unchanged call this first,
so a file left by an older cook (another size, format or compression) is
encoded again instead of shipping.
"""
from pathlib import Path

# tex3ds format names -> GPU_TEXCOLOR codes in the T3X header.
FORMATS = {'rgba8': 0, 'rgb8': 1, 'rgba5551': 2, 'rgb565': 3, 'rgba4': 4, 'la8': 5,
           'hilo8': 6, 'l8': 7, 'a8': 8, 'la4': 9, 'l4': 10, 'a4': 11, 'etc1': 12, 'etc1a4': 13}


def t3x_matches(path, size, fmt):
    """True if path is an uncompressed T3X of size (width, height) and format fmt."""
    path = Path(path)
    if not path.is_file():
        return False
    data = path.read_bytes()
    if len(data) < 5:
        return False
    count = int.from_bytes(data[0:2], 'little')
    offset = 5 + count * 12
    if len(data) < offset + 4:
        return False
    header = int.from_bytes(data[offset:offset + 4], 'little')
    if header & 255:  # compressed (LZ10/LZ11/...): every cook writes -z none
        return False
    if len(data) - offset - 4 != header >> 8:
        return False
    width = 1 << ((data[2] & 7) + 3)
    height = 1 << (((data[2] >> 3) & 7) + 3)
    return (width, height) == tuple(size) and data[3] == FORMATS[fmt]
