"""Cook the original Pokey separation points for each live CHR0 pose."""
from pathlib import Path
import struct
import subprocess
ROOT = Path(__file__).resolve().parents[1]

def main():
    target = ROOT / 'build/sanbo-joints.djnt'
    subprocess.run(['node', str(ROOT/'build/export_joints.mjs'),
                    str(ROOT/'assets/courses/desert_course/sanbo.brres'), str(target),
                    'sanbo', 'joint4,joint1,joint2,joint3'], check=True)
    data = target.read_bytes()
    magic, version, joints, clips = struct.unpack_from('<4s3I', data)
    if (magic, version, joints) != (b'DJNT', 1, 4): raise ValueError('Invalid Pokey joints')
    offset, poses = 16+32*joints, {}
    for _ in range(clips):
        name = data[offset:offset+32].split(b'\0')[0].decode()
        frames, loop = struct.unpack_from('<2I', data, offset+32)
        offset += 40
        if name in ('move','damage','revive'):
            poses[name] = [[tuple(struct.unpack_from('<12f',data,offset+(i*4+j)*48)[k] for k in (3,7,11)) for j in range(4)] for i in range(frames)]
        offset += frames*4*48
    if offset != len(data) or set(poses) != {'move','damage','revive'}: raise ValueError('Missing Pokey pose')
    lines = ['#pragma once','namespace mkw {']
    for name, frames in poses.items():
        lines += ['inline constexpr float Pokey_'+name+'[][4][3]={']
        lines += ['{'+','.join('{'+','.join(format(v,'.9f')+'f' for v in p)+'}' for p in frame)+'},' for frame in frames]
        lines += ['};']
    lines += ['}']
    (ROOT/'include/mkw/pokey_joint_assets.hpp').write_text('\n'.join(lines)+'\n')
if __name__ == '__main__': main()
