#!/usr/bin/env python3
"""Cook Mario Kart Wii's sound archive for the 3DS sound engine.

Reads the user's retail data/extracted/sound/revo_kart.brsar and the 111
sound/strm/*.brstm streams (extracted from the local disc by
tools/extract_disc.py machinery) and writes, into the private overlay
build/audio (packaged by tools/resource_manifest.py):
  audio/sound.arc     sound/file/bank/group/player tables, per-wave RWAR
                      entries and per-sound wave reach (tools/audio_reach.py),
                      little endian, read by mkw::snd::Archive
                      (include/mkw/snd_engine.hpp)
  audio/manifest.json source hashes, per-group sizes, wave validation
and generates include/mkw/sound_ids.hpp (every retail SE_/STRM_/SEQ_/VO_/W_
sound, GRP_ group and PL_ player id, named exactly as in the BRSAR symbol
table). The BRSAR and BRSTMs themselves ship unmodified: the runtime reads
file images and wave archives from them by offset, like NW4R's
DvdSoundArchive.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from nw4r_audio import Brstm, Rbnk, Rsar, Rseq, Rwar, Rwsd, SOUND_SEQ, SOUND_STRM, SOUND_WAVE, decode_adpcm
from audio_reach import reach

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'data/extracted/sound'
OUT = ROOT / 'build/audio'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def encode_frame(samples, coefs, yn1, yn2):
    """Best single DSP-ADPCM frame for <=14 samples given decoder history
    (exhaustive predictor/scale search, AX decode arithmetic)."""
    best = None
    for pred in range(8):
        c1, c2 = coefs[pred * 2], coefs[pred * 2 + 1]
        for shift in range(12):
            scale = 1 << shift
            h1, h2, err, nib = yn1, yn2, 0, []
            for x in samples:
                predicted = (c1 * h1 + c2 * h2 + 1024) >> 11
                n = max(-8, min(7, round((x - predicted) / scale)))
                v = ((n * scale << 11) + 1024 + c1 * h1 + c2 * h2) >> 11
                v = max(-32768, min(32767, v))
                err += (v - x) ** 2
                nib.append(n & 15)
                h2, h1 = h1, v
            if best is None or err < best[0]:
                best = (err, pred, shift, nib)
    _, pred, shift, nib = best
    nib += [0] * (14 - len(nib))
    frame = bytes([pred << 4 | shift]) + bytes(nib[i] << 4 | nib[i + 1] for i in range(0, 14, 2))
    return frame


def loop_bridge(data, w, ch):
    """NDSP cannot start a buffer inside an ADPCM frame. For a loop starting
    mid-frame, the loop is played as a re-encoded bridge frame covering
    [loopStart, next frame) followed by the original body from that frame,
    whose decoder state is restored exactly (retail samples, no PCM decode)."""
    L, E = w.loop_start, w.samples
    F = (L + 13) // 14 * 14
    if F >= E:
        return None
    c = w.channel[ch]
    pcm = decode_adpcm(data, c['start'], F, c['coefs'], c['ps'], c['yn1'], c['yn2'])
    frame = encode_frame(pcm[L:F], c['coefs'], c['loop_yn1'], c['loop_yn2'])
    body_ps = data[c['start'] + F // 14 * 8]
    return struct.pack('<IHBBIHhh8s2x', 0, 0, ch, F - L, F, body_ps, pcm[F - 1], pcm[F - 2], frame)


def ident(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=SOURCE)
    p.add_argument('--out', type=Path, default=OUT)
    a = p.parse_args()
    brsar_path = a.source / 'revo_kart.brsar'
    data = brsar_path.read_bytes()
    r = Rsar(data)
    strings = bytearray()
    string_index = {}

    def intern(s):
        if s not in string_index:
            string_index[s] = len(strings)
            strings.extend(s.encode() + b'\0')
        return string_index[s]

    # ---- files: main image and RWAR from the first group that holds each
    files = []
    wave_table = []  # per file in order, per RWAR entry: (offset from the RWAR, bytes)
    bridges = []
    wave_report = {'rwav': 0, 'adpcm': 0, 'pcm': 0, 'misalignedLoops': 0, 'maxBytes': 0}
    for f in r.files:
        main_off = main_size = wave_off = wave_size = wave_count = 0
        path_index = 0xFFFFFFFF
        if f['extFilePath']:
            path_index = intern(f['extFilePath'])
        for group_id, index in f['positions']:
            g = r.groups[group_id]
            if index >= len(g['items']) or g['items'][index]['fileId'] != f['id']:
                continue
            it = g['items'][index]
            main_off, main_size = g['offset'] + it['offset'], it['size']
            if it['waveDataSize']:
                wave_off, wave_size = g['waveDataOffset'] + it['waveDataOffset'], it['waveDataSize']
            break
        if wave_size:
            rw = Rwar(data, wave_off)
            wave_count = len(rw.waves)
            table = wave_off + struct.unpack_from('>I', data, wave_off + 0x10)[0]
            for wi in range(wave_count):
                off, size = struct.unpack_from('>II', data, table + 12 + wi * 12 + 4)
                wave_table.append((struct.unpack_from('>I', data, wave_off + 0x18)[0] + off, size))
            for wi, w in enumerate(rw.waves):
                wave_report['rwav'] += 1
                if w.format == 2 and w.loop and w.loop_start % 14:
                    for ch in range(w.channels):
                        b = loop_bridge(data, w, ch)
                        if b:
                            bridges.append(struct.pack('<IH', f['id'], wi) + b[6:])
                wave_report['adpcm' if w.format == 2 else 'pcm'] += 1
                if w.format == 2 and w.loop and w.loop_start % 14:
                    wave_report['misalignedLoops'] += 1
                wave_report['maxBytes'] = max(wave_report['maxBytes'], sum((w.samples + 13) // 14 * 8 for _ in w.channel))
        if main_size:
            magic = data[main_off:main_off + 4]
            {b'RSEQ': Rseq, b'RBNK': Rbnk, b'RWSD': Rwsd}[magic](data[main_off:main_off + main_size])
        files.append((main_off, main_size, wave_off, wave_size, wave_count, path_index))

    # ---- sounds
    def sound_entry(s):
        p3 = s['param3d'] or {'flags': 0, 'decayCurve': 1, 'decayRatio': 128, 'dopplerFactor': 0}
        channel_priority, release_fix, bank, data_word, alloc, strm_channels = 64, 0, 0xFFFF, 0, 0, 0
        if s['type'] == SOUND_SEQ:
            q = s['seq']
            channel_priority, release_fix, bank, data_word, alloc = q['channelPriority'], q['releasePriorityFix'], q['bankId'], q['dataOffset'], q['allocTrack']
        elif s['type'] == SOUND_WAVE:
            q = s['wave']
            channel_priority, release_fix, data_word, alloc = q['channelPriority'], q['releasePriorityFix'], q['subNo'] & 0xFFFFFFFF, q['allocTrack']
        elif s['type'] == SOUND_STRM:
            q = s['strm']
            data_word, alloc, strm_channels = q['startPosition'], q['allocTrackFlag'], q['allocChannelCount']
        return struct.pack('<12B2H6I2H', s['type'], s['volume'], s['playerPriority'], s['remoteFilter'],
                           s['panMode'], s['panCurve'], s['actorPlayerId'], channel_priority,
                           release_fix, p3['decayCurve'], p3['decayRatio'], p3['dopplerFactor'],
                           s['playerId'], bank, s['fileId'], data_word, alloc, p3['flags'],
                           s['userParam'][0], s['userParam'][1], strm_channels, 0)

    sounds = b''.join(sound_entry(s) for s in r.sounds)
    assert len(sounds) == 44 * len(r.sounds)
    items, groups = [], []
    for g in r.groups:
        groups.append((len(items), len(g['items'])))
        items.extend(it['fileId'] for it in g['items'])
    # ---- reach: per sound, the wave indices it can ever play (tools/audio_reach.py)
    reach_table = reach(r)
    reach_entries, pool = [], []
    for s in r.sounds:
        _, waves = reach_table.get(s['id'], (None, []))
        reach_entries.append((len(pool), len(waves)))
        pool.extend(waves)
    pi = r.player_info
    header = struct.pack('<4sI10I8H', b'MSSA', 3, len(r.sounds), len(files), len(r.banks), len(groups), len(items),
                         len(r.players), len(strings), len(bridges), len(wave_table), len(pool),
                         pi['seqSoundCount'], pi['seqTrackCount'], pi['strmSoundCount'], pi['strmTrackCount'],
                         pi['strmChannelCount'], pi['waveSoundCount'], pi['waveTrackCount'], 0)
    blob = bytearray(header) + sounds
    for f in files:
        blob += struct.pack('<6I', *f)
    blob += b''.join(struct.pack('<I', b['fileId']) for b in r.banks)
    blob += b''.join(struct.pack('<2I', *g) for g in groups)
    blob += b''.join(struct.pack('<I', i) for i in items)
    blob += bytes(p['playableSoundCount'] for p in r.players)
    blob += bytes(strings)
    bridges.sort(key=lambda b: struct.unpack_from('<IHB', b))
    blob += b''.join(bridges)
    blob += b''.join(struct.pack('<2I', *w) for w in wave_table)
    blob += b''.join(struct.pack('<2I', *e) for e in reach_entries)
    blob += b''.join(struct.pack('<H', w) for w in pool)
    blob += bytes(-len(blob) % 4)
    out = a.out / 'audio'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'sound.arc').write_bytes(blob)

    # ---- streams: validate every referenced BRSTM
    streams = {}
    for s in r.sounds:
        if s['type'] != SOUND_STRM:
            continue
        rel = r.files[s['fileId']]['extFilePath']
        if rel in streams:
            continue
        path = a.source / rel
        b = Brstm(path.read_bytes())
        if b.format not in (0, 1, 2) or not 1 <= b.channels <= 8:
            raise SystemExit('unsupported stream ' + rel)
        streams[rel] = {'sha256': digest(path), 'bytes': path.stat().st_size, 'channels': b.channels,
                        'format': ['pcm8', 'pcm16', 'adpcm'][b.format], 'rate': b.rate, 'loop': bool(b.loop), 'loopStart': b.loop_start, 'samples': b.loop_end,
                        'blockSamples': b.block_samples, 'tracks': len(b.tracks)}

    # ---- generated ids
    lines = ['#pragma once',
             '// Generated by tools/cook_audio.py from the retail revo_kart.brsar symbol table.',
             '// Ids are the archive indices MKW passes to nw4r::snd::SoundStartable.',
             '#include <cstdint>',
             'namespace mkw::snd::id {',
             'enum : std::uint32_t {']
    seen = set()
    for s in r.sounds:
        if s['name'] and ident(s['name']) not in seen:
            seen.add(ident(s['name']))
            lines.append(f' {ident(s["name"])}={s["id"]},')
    lines += ['};', 'namespace group { enum : std::uint32_t {']
    lines += [f' {ident(g["name"])}={g["id"]},' for g in r.groups if g['name']]
    lines += ['}; }', 'namespace player { enum : std::uint32_t {']
    lines += [f' {ident(p["name"])}={p["id"]},' for p in r.players if p['name']]
    lines += ['}; }', 'namespace bankfile { enum : std::uint32_t {']
    lines += [f' {ident(b["name"])}={b["fileId"]},' for b in r.banks if b['name']]
    lines += ['}; }', f'inline constexpr std::uint32_t SoundCount={len(r.sounds)};', '}']
    (ROOT / 'include/mkw/sound_ids.hpp').write_text('\n'.join(lines) + '\n')

    group_sizes = {g['name'] or str(g['id']): {'main': g['size'], 'waves': g['waveDataSize']} for g in r.groups}
    manifest = {'version': 1, 'source': str(brsar_path.relative_to(ROOT)), 'sourceSha256': digest(brsar_path),
                'sounds': len(r.sounds), 'files': len(files), 'groups': len(groups), 'banks': len(r.banks),
                'players': [p['name'] for p in r.players], 'playerInfo': pi, 'waves': wave_report,
                'arcSha256': hashlib.sha256(blob).hexdigest(), 'loopBridges': len(bridges), 'reachPool': len(pool), 'streams': streams, 'groupSizes': group_sizes}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    print(json.dumps({k: manifest[k] for k in ('sounds', 'files', 'groups', 'waves')}, indent=1),
          len(streams), 'streams', len(blob), 'arc bytes')


if __name__ == '__main__':
    main()
