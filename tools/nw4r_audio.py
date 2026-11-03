#!/usr/bin/env python3
"""Readers for the NW4R sound formats in Mario Kart Wii's revo_kart.brsar.

Layouts follow doldecomp/ogws (CC0, commit 27fa94a, src/nw4r/snd/*File*.cpp)
and were checked against the retail archive's own data (RSAR 1.4, RSEQ 1.0,
RBNK 1.2, RWSD 1.3, RWAR 1.0, RWAV, RSTM). Newer than ogws: RBNK 1.2 and
RWSD 1.3 index their waves in the paired RWAR instead of an inline wave block.
Nothing here writes game data; cook_audio.py packs what the 3DS runtime loads.
"""
import struct

SOUND_SEQ, SOUND_STRM, SOUND_WAVE = 1, 2, 3


def u8(d, o): return d[o]
def u16(d, o): return struct.unpack_from('>H', d, o)[0]
def s16(d, o): return struct.unpack_from('>h', d, o)[0]
def u32(d, o): return struct.unpack_from('>I', d, o)[0]
def s32(d, o): return struct.unpack_from('>i', d, o)[0]
def f32(d, o): return struct.unpack_from('>f', d, o)[0]


def nibble_to_sample(nibble):
    """AxVoice::GetSampleByDspAddress for DSP-ADPCM (14 samples per 16 nibbles)."""
    return nibble // 16 * 14 + (nibble % 16 - 2)


class DataRef:
    """Util::DataRef: u8 refType (0 address, 1 offset), u8 dataType, value."""
    __slots__ = ('ref_type', 'data_type', 'value')

    def __init__(self, d, o):
        self.ref_type, self.data_type, self.value = d[o], d[o + 1], u32(d, o + 4)

    def offset(self, base):
        if self.ref_type == 1 and self.value:
            return base + self.value
        return None


class Rsar:
    def __init__(self, data):
        self.data = d = data
        if d[:4] != b'RSAR':
            raise ValueError('not an RSAR')
        self.version = u16(d, 6)
        self.symb, self.symb_size = u32(d, 0x10), u32(d, 0x14)
        self.info, self.info_size = u32(d, 0x18), u32(d, 0x1C)
        self.file_image, self.file_image_size = u32(d, 0x20), u32(d, 0x24)
        self._strings()
        self._info()

    def _strings(self):
        d = self.data
        base = self.symb + 8
        table = base + u32(d, base)
        self.strings = []
        for i in range(u32(d, table)):
            o = base + u32(d, table + 4 + i * 4)
            self.strings.append(d[o:d.index(b'\0', o)].decode())

    def name(self, sid):
        return self.strings[sid] if sid != 0xFFFFFFFF and sid < len(self.strings) else None

    def _table(self, ref_offset):
        d = self.data
        base = self.info + 8
        ref = DataRef(d, ref_offset)
        table = ref.offset(base)
        out = []
        for i in range(u32(d, table)):
            out.append(DataRef(d, table + 4 + i * 8).offset(base))
        return out

    def _info(self):
        d = self.data
        base = self.info + 8
        self.sounds = []
        for sid, e in enumerate(self._table(base)):
            s = dict(id=sid, name=self.name(u32(d, e)), fileId=u32(d, e + 4), playerId=u32(d, e + 8),
                     volume=d[e + 0x14], playerPriority=d[e + 0x15], type=d[e + 0x16],
                     remoteFilter=d[e + 0x17], userParam=(u32(d, e + 0x20), u32(d, e + 0x24)),
                     panMode=d[e + 0x28], panCurve=d[e + 0x29], actorPlayerId=d[e + 0x2A])
            p3 = DataRef(d, e + 0xC).offset(base)
            s['param3d'] = dict(flags=u32(d, p3), decayCurve=d[p3 + 4], decayRatio=d[p3 + 5],
                                dopplerFactor=d[p3 + 6]) if p3 else None
            info = DataRef(d, e + 0x18).offset(base)
            if s['type'] == SOUND_SEQ:
                s['seq'] = dict(dataOffset=u32(d, info), bankId=u32(d, info + 4), allocTrack=u32(d, info + 8),
                                channelPriority=d[info + 12], releasePriorityFix=d[info + 13])
            elif s['type'] == SOUND_WAVE:
                s['wave'] = dict(subNo=s32(d, info), allocTrack=u32(d, info + 4),
                                 channelPriority=d[info + 8], releasePriorityFix=d[info + 9])
            elif s['type'] == SOUND_STRM:
                # RSAR 1.4 StrmSoundInfo: startPosition, allocChannelCount, allocTrackFlag.
                s['strm'] = dict(startPosition=u32(d, info), allocChannelCount=u16(d, info + 4),
                                 allocTrackFlag=u16(d, info + 6))
            self.sounds.append(s)
        self.banks = [dict(id=i, name=self.name(u32(d, e)), fileId=u32(d, e + 4))
                      for i, e in enumerate(self._table(base + 8))]
        self.players = [dict(id=i, name=self.name(u32(d, e)), playableSoundCount=d[e + 4], heapSize=u32(d, e + 8))
                        for i, e in enumerate(self._table(base + 0x10))]
        self.files = []
        for i, e in enumerate(self._table(base + 0x18)):
            ext = DataRef(d, e + 0xC).offset(base)
            pos = DataRef(d, e + 0x14).offset(base)
            positions = []
            if pos:
                for j in range(u32(d, pos)):
                    p = DataRef(d, pos + 4 + j * 8).offset(base)
                    positions.append((u32(d, p), u32(d, p + 4)))
            self.files.append(dict(id=i, fileSize=u32(d, e), waveDataSize=u32(d, e + 4), entryNum=s32(d, e + 8),
                                   extFilePath=d[ext:d.index(b'\0', ext)].decode() if ext else None,
                                   positions=positions))
        self.groups = []
        for i, e in enumerate(self._table(base + 0x20)):
            ext = DataRef(d, e + 8).offset(base)
            items_table = DataRef(d, e + 0x20).offset(base)
            items = []
            for j in range(u32(d, items_table)):
                it = DataRef(d, items_table + 4 + j * 8).offset(base)
                items.append(dict(fileId=u32(d, it), offset=u32(d, it + 4), size=u32(d, it + 8),
                                  waveDataOffset=u32(d, it + 12), waveDataSize=u32(d, it + 16)))
            self.groups.append(dict(id=i, name=self.name(u32(d, e)), entryNum=s32(d, e + 4),
                                    extFilePath=d[ext:d.index(b'\0', ext)].decode() if ext else None,
                                    offset=u32(d, e + 0x10), size=u32(d, e + 0x14),
                                    waveDataOffset=u32(d, e + 0x18), waveDataSize=u32(d, e + 0x1C), items=items))
        pi = DataRef(d, base + 0x28).offset(base)
        self.player_info = dict(seqSoundCount=u16(d, pi), seqTrackCount=u16(d, pi + 2), strmSoundCount=u16(d, pi + 4),
                                strmTrackCount=u16(d, pi + 6), strmChannelCount=u16(d, pi + 8),
                                waveSoundCount=u16(d, pi + 10), waveTrackCount=u16(d, pi + 12)) if pi else None
        self.by_name = {s['name']: s for s in self.sounds if s['name']}

    def file_bytes(self, file_id):
        """Main data and wave data of a file, from the first group that holds it (SoundArchivePlayer)."""
        f = self.files[file_id]
        for group_id, index in f['positions']:
            g = self.groups[group_id]
            if index >= len(g['items']):
                continue
            it = g['items'][index]
            if it['fileId'] != file_id:
                continue
            main = self.data[g['offset'] + it['offset']:g['offset'] + it['offset'] + it['size']]
            wave = self.data[g['waveDataOffset'] + it['waveDataOffset']:
                             g['waveDataOffset'] + it['waveDataOffset'] + it['waveDataSize']]
            return main, wave, group_id
        return None, None, None


class Rwav:
    """One embedded RWAV: DSP-ADPCM (or PCM) channels with loop and decoder state."""

    def __init__(self, d, o):
        if d[o:o + 4] != b'RWAV':
            raise ValueError('not an RWAV')
        self.offset = o
        info = o + u32(d, o + 0x10) + 8
        self.data_base = o + u32(d, o + 0x18) + 8
        self.format, self.loop, self.channels = d[info], d[info + 1], d[info + 2]
        self.rate = (d[info + 3] << 16) + u16(d, info + 4)
        self.loop_start_nibble, self.loop_end_nibble = u32(d, info + 8), u32(d, info + 12)
        table = info + u32(d, info + 0x10)
        self.data_offset = u32(d, info + 0x14)
        self.channel = []
        for c in range(self.channels):
            ci = info + u32(d, table + c * 4)
            ch = dict(dataOffset=u32(d, ci), volume=(u32(d, ci + 8), u32(d, ci + 12), u32(d, ci + 16), u32(d, ci + 20)))
            adpcm = u32(d, ci + 4)
            if adpcm:
                a = info + adpcm
                ch['coefs'] = list(struct.unpack_from('>16h', d, a))
                ch['gain'], ch['ps'], ch['yn1'], ch['yn2'] = u16(d, a + 32), u16(d, a + 34), s16(d, a + 36), s16(d, a + 38)
                ch['loop_ps'], ch['loop_yn1'], ch['loop_yn2'] = u16(d, a + 40), s16(d, a + 42), s16(d, a + 44)
            # WaveInfo.dataOffset points at the DATA block (relative to the info);
            # samples start after its 8-byte header (checked: ps == first frame byte).
            ch['start'] = self.data_base + ch['dataOffset']
            self.channel.append(ch)
        if self.format == 2:
            self.loop_start = nibble_to_sample(self.loop_start_nibble)
            self.samples = nibble_to_sample(self.loop_end_nibble) + 1
        else:
            per = 1 if self.format == 0 else 2
            self.loop_start, self.samples = self.loop_start_nibble, self.loop_end_nibble + 1


class Rwar:
    def __init__(self, d, o=0):
        if d[o:o + 4] != b'RWAR':
            raise ValueError('not an RWAR')
        table = o + u32(d, o + 0x10)
        data = o + u32(d, o + 0x18)
        self.waves = []
        for i in range(u32(d, table + 8)):
            e = table + 12 + i * 12
            self.waves.append(Rwav(d, data + u32(d, e + 4)))


class Rbnk:
    """RBNK 1.2: instrument -> key/velocity split -> InstParam (wave index into the RWAR)."""

    def __init__(self, d):
        if d[:4] != b'RBNK':
            raise ValueError('not an RBNK')
        self.d = d
        self.version = u16(d, 6)
        self.data = u32(d, 0x10)
        self.count = u32(d, self.data + 8)

    def _ref(self, o):
        return DataRef(self.d, o)

    def inst_param(self, ref_offset):
        d = self.d
        base = self.data + 8
        r = self._ref(ref_offset)
        o = r.offset(base)
        return dict(waveIndex=s32(d, o), attack=d[o + 4], decay=d[o + 5], sustain=d[o + 6], release=d[o + 7],
                    originalKey=d[o + 0xC], volume=d[o + 0xD], pan=d[o + 0xE], tune=f32(d, o + 0x10))

    def sub_region(self, ref_offset, split):
        d = self.d
        base = self.data + 8
        r = self._ref(ref_offset)
        if r.data_type == 1:
            return ref_offset
        o = r.offset(base)
        if o is None:
            return None
        if r.data_type == 2:
            size = d[o]
            i = 0
            while split > d[o + 1 + i]:
                i += 1
                if i >= size:
                    return None
            start = (size + 1 + 3) & ~3
            return o + start + i * 8
        if r.data_type == 3:
            lo, hi = d[o], d[o + 1]
            if split < lo or split > hi:
                return None
            return o + 4 + (split - lo) * 8
        return None

    def inst_info(self, prg, key, velocity):
        """BankFileReader::ReadInstInfo."""
        if prg < 0 or prg >= self.count:
            return None
        ref = self.data + 12 + prg * 8
        r = self._ref(ref)
        if r.data_type == 0 and r.value == 0:
            return None
        if r.data_type != 1:
            ref = self.sub_region(ref, key)
            if ref is None:
                return None
        r = self._ref(ref)
        if r.data_type != 1:
            ref = self.sub_region(ref, velocity)
            if ref is None:
                return None
        if self._ref(ref).data_type != 1:
            return None
        p = self.inst_param(ref)
        return p if p['waveIndex'] >= 0 else None

    def wave_indices(self):
        """Every wave index any key/velocity region references (for validation)."""
        out = set()
        for prg in range(self.count):
            for key in range(128):
                for vel in (0, 32, 64, 96, 127):
                    p = self.inst_info(prg, key, vel)
                    if p:
                        out.add(p['waveIndex'])
        return out


class Rwsd:
    """RWSD 1.3 wave sound data: per sound a WsdInfo and note list (waves in the RWAR)."""

    def __init__(self, d):
        if d[:4] != b'RWSD':
            raise ValueError('not an RWSD')
        self.d = d
        self.version = u16(d, 6)
        self.data = u32(d, 0x10)
        self.count = u32(d, self.data + 8)

    def sound(self, index):
        d = self.d
        base = self.data + 8
        wsd = DataRef(d, self.data + 12 + index * 8).offset(base)
        info = DataRef(d, wsd).offset(base)
        out = dict(pitch=f32(d, info), pan=d[info + 4], surroundPan=d[info + 5], fxSendA=d[info + 6],
                   fxSendB=d[info + 7], fxSendC=d[info + 8], mainSend=d[info + 9])
        tracks = DataRef(d, wsd + 8).offset(base)
        notes = DataRef(d, wsd + 16).offset(base)
        out['notes'] = []
        for i in range(u32(d, notes)):
            n = DataRef(d, notes + 4 + i * 8).offset(base)
            out['notes'].append(dict(waveIndex=s32(d, n), attack=d[n + 4], decay=d[n + 5], sustain=d[n + 6],
                                     release=d[n + 7], hold=u16(d, n + 8), originalKey=d[n + 0xC],
                                     volume=d[n + 0xD], pan=d[n + 0xE], surroundPan=d[n + 0xF],
                                     pitch=f32(d, n + 0x10)))
        out['tracks'] = []
        if tracks:
            for t in range(u32(d, tracks)):
                tr = DataRef(d, tracks + 4 + t * 8).offset(base)
                # TrackInfo -> NoteEvent table: {f32 position, f32 length, u32 noteIndex, reserved}.
                events = []
                ev_table = DataRef(d, tr).offset(base) if tr else None
                if ev_table:
                    for k in range(u32(d, ev_table)):
                        e = DataRef(d, ev_table + 4 + k * 8).offset(base)
                        events.append(dict(position=f32(d, e), length=f32(d, e + 4), noteIndex=u32(d, e + 8)))
                out['tracks'].append(events)
        return out


class Rseq:
    def __init__(self, d):
        if d[:4] != b'RSEQ':
            raise ValueError('not an RSEQ')
        self.d = d
        data = u32(d, 0x10)
        self.base = data + u32(d, data + 8)
        self.labels = {}
        labl = u32(d, 0x18)
        if labl and d[labl:labl + 4] == b'LABL':
            n = u32(d, labl + 8)
            for i in range(n):
                o = labl + 8 + u32(d, labl + 12 + i * 4)
                off, length = u32(d, o), u32(d, o + 4)
                self.labels[d[o + 8:o + 8 + length].decode()] = off


class Brstm:
    def __init__(self, d):
        if d[:4] != b'RSTM':
            raise ValueError('not an RSTM')
        self.d = d
        head = u32(d, 0x10)
        base = head + 8
        info = DataRef(d, head + 8).offset(base)
        self.format, self.loop, self.channels = d[info], d[info + 1], d[info + 2]
        self.rate = (d[info + 3] << 16) + u16(d, info + 4)
        self.loop_start, self.loop_end = u32(d, info + 8), u32(d, info + 12)
        self.data_offset = u32(d, info + 0x10)
        self.blocks, self.block_size, self.block_samples = u32(d, info + 0x14), u32(d, info + 0x18), u32(d, info + 0x1C)
        self.last_block_size, self.last_block_samples = u32(d, info + 0x20), u32(d, info + 0x24)
        self.last_block_padded = u32(d, info + 0x28)
        self.adpc_interval, self.adpc_size = u32(d, info + 0x2C), u32(d, info + 0x30)
        tracks = DataRef(d, head + 0x10).offset(base)
        self.track_type = d[tracks + 1]
        self.tracks = []
        for t in range(d[tracks]):
            tr = DataRef(d, tracks + 4 + t * 8).offset(base)
            if self.track_type == 0:
                n = d[tr]
                self.tracks.append(dict(volume=127, pan=64, channels=list(d[tr + 1:tr + 1 + n])))
            else:
                n = d[tr + 8]
                self.tracks.append(dict(volume=d[tr], pan=d[tr + 1], channels=list(d[tr + 9:tr + 9 + n])))
        chans = DataRef(d, head + 0x18).offset(base)
        self.channel = []
        for c in range(d[chans]):
            ci = DataRef(d, chans + 4 + c * 8).offset(base)
            a = DataRef(d, ci).offset(base)
            ch = {}
            if a:
                ch['coefs'] = list(struct.unpack_from('>16h', d, a))
                ch['gain'], ch['ps'], ch['yn1'], ch['yn2'] = u16(d, a + 32), u16(d, a + 34), s16(d, a + 36), s16(d, a + 38)
                ch['loop_ps'], ch['loop_yn1'], ch['loop_yn2'] = u16(d, a + 40), s16(d, a + 42), s16(d, a + 44)
            self.channel.append(ch)
        self.adpc = u32(d, 0x18)
        self.data_block = u32(d, 0x20)
        # First audio byte: DATA block + 8 + its own leading offset (usually 0x18 of padding).
        self.audio = self.data_block + 8 + u32(d, self.data_block + 8)
        self.samples = self.loop_end


def decode_adpcm(data, start, samples, coefs, ps, yn1, yn2):
    """Reference DSP-ADPCM decoder (vgmstream/AX semantics) for tests and validation."""
    out = []
    pos = start
    hist1, hist2 = yn1, yn2
    while len(out) < samples:
        header = data[pos]
        scale = 1 << (header & 15)
        pred = header >> 4
        c1, c2 = coefs[pred * 2], coefs[pred * 2 + 1]
        for b in data[pos + 1:pos + 8]:
            for nib in (b >> 4, b & 15):
                if len(out) >= samples:
                    break
                n = nib - 16 if nib >= 8 else nib
                v = ((n * scale) << 11) + 1024 + c1 * hist1 + c2 * hist2
                v >>= 11
                v = -32768 if v < -32768 else 32767 if v > 32767 else v
                hist2, hist1 = hist1, v
                out.append(v)
        pos += 8
    return out
