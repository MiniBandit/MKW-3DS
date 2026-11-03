#!/usr/bin/env python3
"""Extract a validated retail Mario Kart Wii rksys.dat without changing its source.
Container offsets follow Dolphin's WiiSaveStructs.h. Requires pycryptodome.
The Wii export signatures are not regenerated; output is the game's NAND format.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib


def validate(data):
    if len(data) != 0x2bc000 or data[:8] != b'RKSD0006':
        raise ValueError('Not a retail RKSD0006 save')
    if data[0x23308:0x2330c] != b'RKGD':
        raise ValueError('Missing global data')
    if zlib.crc32(data[:0x27ffc]) != struct.unpack_from('>I', data, 0x27ffc)[0]:
        raise ValueError('Invalid rksys.dat CRC32')
    for slot in range(4):
        if data[8+slot*0x8cc0:12+slot*0x8cc0] not in (b'RKPD',bytes(4)):
            raise ValueError('Invalid license header')
    return data


def extract(data):
    if data[:8] == b'RKSD0006':
        return validate(data)
    from Crypto.Cipher import AES
    if len(data) < 0xf140:
        raise ValueError('Truncated Wii export')
    key = bytes.fromhex('ab01b9d8e1622b08afbad84dbfc2a55d')
    iv = bytes.fromhex('216712e6aa1f689f95c5a22324dc6a98')
    header = bytearray(AES.new(key,AES.MODE_CBC,iv).decrypt(data[:0xf0c0]))
    digest = bytes(header[14:30])
    header[14:30] = bytes.fromhex('0e65378199be4517ab06ec22451a5793')
    if hashlib.md5(header).digest() != digest:
        raise ValueError('Invalid Wii export header checksum')
    if header[4:7] != b'RMC':
        raise ValueError('Not a Mario Kart Wii export')
    backup = data[0xf0c0:0xf140]
    if backup[:8] != bytes.fromhex('00000070426b0001'):
        raise ValueError('Invalid Wii backup header')
    if backup[0x60:0x68] != header[:8]:
        raise ValueError('Wii export title IDs disagree')
    count = struct.unpack_from('>I',backup,12)[0]
    pos = 0xf140
    result = None
    for _ in range(count):
        h = data[pos:pos+128]
        pos += 128
        if len(h) != 128 or h[:4] != bytes.fromhex('03adf17e'):
            raise ValueError('Invalid Wii export file header')
        size = struct.unpack_from('>I',h,4)[0]
        kind = h[10]
        name = h[11:75].split(b'\0',1)[0]
        if kind == 1:
            rounded = (size+63)&~63
            if pos+rounded > len(data):
                raise ValueError('Truncated Wii export file')
            if name == b'rksys.dat':
                if result is not None:
                    raise ValueError('Duplicate rksys.dat')
                result = AES.new(key,AES.MODE_CBC,h[80:96]).decrypt(data[pos:pos+rounded])[:size]
            pos += rounded
        elif kind != 2:
            raise ValueError('Invalid Wii export entry type')
    if result is None:
        raise ValueError('No rksys.dat in Wii export')
    return validate(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('destination',type=Path,help='New rksys.dat path; existing files are never replaced')
    args = parser.parse_args()
    data = extract(args.source.read_bytes())
    args.destination.parent.mkdir(parents=True,exist_ok=True)
    with args.destination.open('xb') as out:
        out.write(data)
    print(f'Wrote {len(data)} bytes: {args.destination}')


if __name__ == '__main__':
    main()
