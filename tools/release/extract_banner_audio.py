#!/usr/bin/env python3
"""Extract banner CWAV bytes from a decrypted CIA or CBMD without re-encoding.

Only the sound is written; no banner model is copied to the repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def u32(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def align64(value):
    return (value+63)&~63


def extract(path):
    with path.open('rb') as f:
        size = path.stat().st_size
        def read(offset, count):
            if offset < 0 or count < 0 or offset+count > size:
                raise ValueError('Truncated input or invalid section bounds')
            f.seek(offset)
            data = f.read(count)
            if len(data) != count:
                raise ValueError('Short read')
            return data
        head = read(0, min(size, 0x2020))
        info = {'source_name':path.name}
        if head[:4] == b'CBMD':
            banner = read(0,size)
        else:
            if len(head)<0x2020 or u32(head)!=0x2020:
                raise ValueError('Expected a CIA or CBMD banner')
            cert,ticket,tmd = struct.unpack_from('<III',head,8)
            tmd_at = align64(align64(u32(head))+cert)
            tmd_at = align64(tmd_at+ticket)
            content_at = align64(tmd_at+tmd)
            signed = read(tmd_at,tmd)
            sig = struct.unpack_from('>I',signed)[0]
            body_at = {0x10000:0x240,0x10001:0x140,0x10002:0x80,0x10003:0x240,0x10004:0x140,0x10005:0x80}.get(sig)
            if body_at is None or len(signed)<body_at+0xc4:
                raise ValueError('Unsupported TMD')
            body = signed[body_at:]
            info['title_id'] = body[0x4c:0x54].hex()
            info['title_version'] = struct.unpack_from('>H',body,0x9c)[0]
            count = struct.unpack_from('>H',body,0x9e)[0]
            ncch = None
            for i in range(count):
                entry = body[0x9c4+i*48:0x9c4+(i+1)*48]
                if len(entry)!=48:raise ValueError('Truncated content records')
                _,index,flags,content_size = struct.unpack_from('>IHHQ',entry)
                if not head[0x20+index//8] & (0x80>>(index%8)):continue
                if index == 0:
                    if flags&1:raise ValueError('CIA content is encrypted; supply a decrypted package')
                    ncch = read(content_at,0x200)
                    break
                content_at += content_size
            if ncch is None or ncch[0x100:0x104]!=b'NCCH':
                raise ValueError('No main NCCH content')
            if not ncch[0x18f]&4:
                raise ValueError('NCCH is encrypted; supply a decrypted package')
            unit = 0x200 << ncch[0x18e]
            exefs_at = content_at + u32(ncch,0x1a0)*unit
            exefs_size = u32(ncch,0x1a4)*unit
            exefs = read(exefs_at,0x200)
            banner = None
            for i in range(10):
                name,offset,length=struct.unpack_from('<8sII',exefs,i*16)
                if name.rstrip(b'\0')==b'banner':
                    if offset+length+0x200>exefs_size:raise ValueError('Banner exceeds ExeFS')
                    banner=read(exefs_at+0x200+offset,length)
                    expected=exefs[0x1e0-i*32:0x200-i*32]
                    if hashlib.sha256(banner).digest()!=expected:raise ValueError('Banner ExeFS hash mismatch')
                    break
            if banner is None:raise ValueError('No banner entry in ExeFS')
        if len(banner)<0x88 or banner[:4]!=b'CBMD':raise ValueError('Invalid banner header')
        offset=u32(banner,0x84)
        if offset<0x88 or offset+16>len(banner) or banner[offset:offset+4]!=b'CWAV':raise ValueError('Missing CWAV')
        length=u32(banner,offset+12)
        if length<16 or offset+length>len(banner):raise ValueError('Invalid CWAV size')
        audio=banner[offset:offset+length]
        info.update(bytes=len(audio),sha256=hashlib.sha256(audio).hexdigest(),encoding='Original CWAV bytes; no resampling or normalization')
        return audio,info


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    try:
        audio,info=extract(args.input)
        with args.output.open('xb') as f:f.write(audio)
        args.output.with_suffix(args.output.suffix+'.json').write_text(json.dumps(info,indent=2)+'\n')
    except (ValueError,OSError,struct.error) as e:parser.error(str(e))
    print(json.dumps(info,indent=2))


if __name__=='__main__':main()
