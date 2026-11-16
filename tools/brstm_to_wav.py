#!/usr/bin/env python3
"""Decode a PCM16 BRSTM (NW4R stream) to a WAV, every sample included.

    python3 tools/brstm_to_wav.py data/extracted/sound/strm/o_Start2_32_fan.brstm out.wav

Only codec 1 (big-endian PCM16, as Mario Kart Wii's fanfares use) is handled.
"""
import struct, sys, wave


def decode(data):
    if data[:4] != b'RSTM':
        raise SystemExit('not a BRSTM')
    head = struct.unpack_from('>I', data, 0x10)[0]
    body = head + 8 + struct.unpack_from('>I', data, head + 0x0C)[0]  # HEAD part 1
    codec, loop, channels = data[body], data[body + 1], data[body + 2]
    rate = struct.unpack_from('>H', data, body + 4)[0]
    total = struct.unpack_from('>I', data, body + 0x0C)[0]
    data_offset, blocks, block_size, block_samples, last_size, last_samples, last_padded = \
        struct.unpack_from('>7I', data, body + 0x10)
    if codec != 1:
        raise SystemExit(f'codec {codec}: only PCM16 is handled')
    pcm = [[] for _ in range(channels)]
    at = data_offset
    for b in range(blocks):
        size, count = (last_padded, last_samples) if b == blocks - 1 else (block_size, block_samples)
        for c in range(channels):
            pcm[c].append(data[at:at + count * 2])
            at += size
    frames = [b''.join(p) for p in pcm]
    if any(len(f) != total * 2 for f in frames):
        raise SystemExit('sample count mismatch')
    out = bytearray()
    for i in range(total):
        for c in range(channels):
            out += frames[c][2 * i + 1:2 * i + 2] + frames[c][2 * i:2 * i + 1]  # big- to little-endian
    return channels, rate, total, bytes(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    channels, rate, total, pcm = decode(open(src, 'rb').read())
    with wave.open(dst, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    print(f'{dst}: {channels} ch, {rate} Hz, {total} samples ({total / rate:.3f} s)')


if __name__ == '__main__':
    main()
