"""Cook original Wiggler landing-effect attachment matrices."""
from pathlib import Path
import struct
import subprocess
ROOT = Path(__file__).resolve().parents[1]


def read_model(model):
    path = ROOT/'build'/f'{model}-root.djnt'
    subprocess.run(['node', str(ROOT/'build/export_joints.mjs'),
                    str(ROOT/'assets/courses/treehouse_course/hanachan.brres'),
                    str(path), model, 'skl_root'], check=True, stdout=subprocess.DEVNULL)
    data = path.read_bytes()
    magic, version, joints, count = struct.unpack_from('<4s3I', data)
    if (magic, version, joints) != (b'DJNT', 1, 1):
        raise ValueError('Invalid Wiggler attachment export')
    result, offset = {}, 48
    for _ in range(count):
        name = data[offset:offset+32].split(b'\0')[0].decode()
        frames, loop = struct.unpack_from('<2I', data, offset+32)
        offset += 40
        result[name] = (frames, loop, data[offset:offset+frames*48])
        offset += frames*48
    if offset != len(data): raise ValueError('Invalid Wiggler attachment size')
    return result


def main():
    head = read_model('hanachan')
    body = read_model('hanachan_body1')
    for index in range(2, 5):
        other = read_model(f'hanachan_body{index}')
        for name in ('walk', 'wait'):
            if other[name] != body[name]: raise ValueError('Distinct body attachment needs its own table')
    poses = [head[name] for name in ('head_walk', 'head_wait', 'head_wait2')]
    poses += [body[name] for name in ('walk', 'wait')]
    lines = ['#pragma once', '#include <cmath>', '#include <algorithm>', 'namespace mkw {']
    for index, (frames, loop, raw) in enumerate(poses):
        lines.append(f'inline constexpr float HanachanRoot{index}[][12]={{')
        for offset in range(0, len(raw), 48):
            lines.append('{'+','.join(f'{v:.9f}f' for v in struct.unpack_from('<12f', raw, offset))+'},')
        lines.append('};')
    lines += ['struct HanachanRootTrack {const float (*pose)[12];unsigned frames;bool loop;};',
              'inline constexpr HanachanRootTrack HanachanRoots[]={']
    for index, (frames, loop, _) in enumerate(poses):
        lines.append(f'{{HanachanRoot{index},{frames},{"true" if loop else "false"}}},')
    lines += ['};', '''inline void sampleHanachanRoot(unsigned clip,float frame,float (&out)[12]) {
    const auto& track=HanachanRoots[clip];
    frame=std::max(0.f,frame);
    frame=track.loop?std::fmod(frame,float(track.frames)):std::min(frame,float(track.frames-1));
    const unsigned first=unsigned(frame);
    const unsigned second=track.loop?(first+1)%track.frames:std::min(first+1,track.frames-1);
    const float fraction=frame-first;
    for(unsigned k=0;k<12;++k)out[k]=track.pose[first][k]+fraction*(track.pose[second][k]-track.pose[first][k]);
}''', '}']
    (ROOT/'include/mkw/hanachan_joint_assets.hpp').write_text('\n'.join(lines)+'\n')

if __name__ == '__main__': main()
