#!/usr/bin/env python3
"""Expose the original point events used by the current animal sound bridge."""
from pathlib import Path
import hashlib
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [("sanbo", "desert_course", "revive"),("DonkyCannon_wii", "boardcross_course", "DonkyCannon_wii"),
           ("DonkyCannonGC", "old_donkey_gc", "DonkyCannonGC"),
           ("tree_cannon", "treehouse_course", "tree_cannon"),
           *[('koopaFigure', 'koopa_course', clip) for clip in ('wait', 'vomit')], ('kuribo', 'old_mario_gc', 'walk_l'), ('kuribo', 'old_mario_gc', 'walk_r'),
           *[('cow', 'farm_course', clip) for clip in ('walk', 'run', 'eat', 'eat_ed', 'surprise')],
           *[('penguin_s', 'old_sherbet_64', clip) for clip in ('walk', 'dive', 'stand_up')],
           ('penguin_m', 'old_sherbet_64', 'walk'),
           *[('crab', 'old_heyho_gba', clip) for clip in ('walk_l', 'walk_r', 'wait')],
           *[('hanachan', 'treehouse_course', clip) for clip in ('walk', 'head_wait2')],
           ('puchi_pakkun', 'old_mario_64', 'puchi_pakkun'),
           *[('pakkun_f', 'old_mario_gc', clip) for clip in ('wait', 'attack')],
           *[('poihana', 'old_peach_gc', clip) for clip in ('walk', 'throw')]]


def main():
    ids = dict((name, int(value)) for name, value in re.findall(r'^\s*(\w+)=(\d+),', (ROOT/'include/mkw/sound_ids.hpp').read_text(), re.M))
    events, clips, receipt = [], [], []
    for resource, course, clip in SOURCES:
        path = ROOT/f'assets/courses/{course}/brasd/{resource}/{clip}.brasd'
        data = path.read_bytes()
        if data[:8] != b'RASD\xfe\xff\x01\x00':
            raise ValueError('Unsupported RASD: ' + str(path))
        block = struct.unpack_from('>I', data, 16)[0]
        if data[block:block+4] != b'DATA':
            raise ValueError('Missing RASD DATA')
        base = block+8
        frames, _, table_offset = struct.unpack_from('>III', data, base)
        table = base+table_offset
        count = struct.unpack_from('>I', data, table)[0]
        first = len(events)
        for i in range(count):
            at = table+4+i*20
            frame, end = struct.unpack_from('>2i', data, at)
            if end != -1 or data[at+8:at+16] != b'\x03\x00\x01\x00\x01\x00\x00\x00':
                raise ValueError('Bridge supports original forward point events only')
            info = base+struct.unpack_from('>I', data, at+16)[0]
            text = info+struct.unpack_from('>I', data, info+12)[0]
            name = data[text:].split(b'\0', 1)[0].decode('ascii')
            volume = data[info+16]
            pitch = struct.unpack_from('>f', data, info+20)[0]
            if data[info+17:info+20] != b'\x00\x00\x00' or not 0 < pitch < 16:
                raise ValueError('Unsupported animal event flags/pitch')
            events.append((frame, ids[name], volume/128, pitch))
        clips.append((resource, clip, frames, first, count))
        receipt.append({'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(data).hexdigest(), 'events': count})
    lines = ['#pragma once', 'namespace mkw {', 'struct ObjectSoundEvent {float frame;unsigned short sound;float volume,pitch;};',
             'inline constexpr ObjectSoundEvent ObjectSoundEvents[]={']
    lines += ['{%s.f,%s,%.8ff,%.8ff},' % (frame, sound, volume, pitch) for frame,sound,volume,pitch in events]
    lines += ['};', 'struct ObjectSoundClip {const char* resource;const char* name;unsigned frames,first,count;};',
              'inline constexpr ObjectSoundClip ObjectSoundClips[]={']
    lines += ['{%s,%s,%s,%s,%s},' % (json.dumps(r), json.dumps(c), n, first, count) for r,c,n,first,count in clips]
    lines += ['};', '}']
    (ROOT/'include/mkw/object_sound_assets.hpp').write_text('\n'.join(lines)+'\n')
    (ROOT/'build/object-sounds.json').write_text(json.dumps({'sources': receipt, 'clips': len(clips), 'events': len(events)}, indent=2)+'\n')
    print(f'Cooked {len(clips)} animal sound clips / {len(events)} point events; no new audio files')


if __name__ == '__main__':
    main()
