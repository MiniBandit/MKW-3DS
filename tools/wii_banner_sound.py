#!/usr/bin/env python3
"""Decode the Disc Channel banner sound from a Wii disc's opening.bnr.

opening.bnr is an IMET header followed by a U8 archive; meta/sound.bin is an
IMD5 header (0x20 bytes) and a BNS stream (DSP-ADPCM, optionally LZ77). For
RMCE01 it is 10.94 s of 32 kHz stereo, looping from 4.27 s on the Wii.
Usage: wii_banner_sound.py opening.bnr out.wav
"""
import struct
import sys
import wave


def u8_files(data):
    at = data.find(bytes.fromhex('55AA382D'))
    if at < 0:
        raise ValueError('opening.bnr: no U8 archive')
    u8 = data[at:]
    root = struct.unpack_from('>I', u8, 4)[0]
    total = struct.unpack_from('>I', u8, root + 8)[0]
    names = root + total * 12
    files = {}

    def walk(i, end, path):
        while i < end:
            kind = u8[root + i*12]
            name_at = int.from_bytes(u8[root + i*12 + 1:root + i*12 + 4], 'big')
            offset, size = struct.unpack_from('>II', u8, root + i*12 + 4)
            name = u8[names + name_at:u8.index(b'\0', names + name_at)].decode()
            if kind == 1:
                walk(i + 1, size, path + name + '/')
                i = size
            else:
                files[path + name] = u8[offset:offset + size]
                i += 1
    walk(1, total, '')
    return files


def lz77(data):
    size = struct.unpack_from('<I', data, 4)[0] >> 8
    src, out, i = data[8:], bytearray(), 0
    while len(out) < size:
        flags = src[i]; i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                x = src[i] << 8 | src[i + 1]; i += 2
                for _ in range((x >> 12) + 3):
                    out.append(out[-((x & 0xfff) + 1)])
            else:
                out.append(src[i]); i += 1
    return bytes(out)


def decode(opening):
    """Returns (channels, sample rate, interleaved PCM16 little-endian bytes)."""
    sound = u8_files(opening)['meta/sound.bin']
    if sound[:4] != b'IMD5':
        raise ValueError('sound.bin: no IMD5 header')
    bns = sound[32:]
    if bns[:4] == b'LZ77':
        bns = lz77(bns)
    if bns[:4] != b'BNS ':
        raise ValueError('sound.bin: not a BNS stream')
    info, _, data, _ = struct.unpack_from('>IIII', bns, 0x10)
    if bns[info:info + 4] != b'INFO' or bns[data:data + 4] != b'DATA':
        raise ValueError('BNS: unexpected chunk layout')
    codec, _, channels, _, rate, _, _, count = struct.unpack_from('>BBBBHHII', bns, info + 8)
    if codec != 0:
        raise ValueError(f'BNS: codec {codec} is not DSP-ADPCM')
    table = struct.unpack_from('>I', bns, info + 0x18)[0]
    pcm = []
    for c in range(channels):
        entry = struct.unpack_from('>I', bns, info + 8 + table + c*4)[0]
        start, coef_at = struct.unpack_from('>II', bns, info + 8 + entry)
        coefs = struct.unpack_from('>16h', bns, info + 8 + coef_at)
        stream, at, out, h1, h2 = bns[data + 8 + start:], 0, [], 0, 0
        while len(out) < count:
            header = stream[at]; at += 1
            scale, c1, c2 = 1 << (header & 15), coefs[(header >> 4)*2], coefs[(header >> 4)*2 + 1]
            for k in range(14):
                if len(out) >= count:
                    break
                nibble = stream[at + k//2] >> 4 if k % 2 == 0 else stream[at + k//2] & 15
                if nibble >= 8:
                    nibble -= 16
                value = max(-32768, min(32767, (((nibble*scale) << 11) + 1024 + c1*h1 + c2*h2) >> 11))
                out.append(value); h2, h1 = h1, value
            at += 7
        pcm.append(out)
    frames = b''.join(struct.pack('<' + 'h'*channels, *(pcm[c][i] for c in range(channels))) for i in range(count))
    return channels, rate, frames


if __name__ == '__main__':
    channels, rate, frames = decode(open(sys.argv[1], 'rb').read())
    with wave.open(sys.argv[2], 'wb') as w:
        w.setnchannels(channels); w.setsampwidth(2); w.setframerate(rate); w.writeframes(frames)
