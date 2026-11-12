#!/usr/bin/env python3
"""Static wave reachability for every sound in the user's revo_kart.brsar.

For a sequence sound it walks the MML from the sound's start offset along
every path (both sides of each A2 "if", every jump, call, loop exit and
opened track), records each NoteOn's program, key and velocity, and maps
them through the sound's bank (BankFileReader::ReadInstInfo) to wave
indices. A program or transpose set from a variable or random argument
counts as "any", so the set is a superset of what can ever play. A wave
sound's set is the notes of its RWSD entry. Streams use no RWAR waves.

The cooker writes these sets so a scene can keep only the waves its sounds
can reach resident (sound.arc "reach" table); tools/test_audio_engine.py
checks them against the C++ sequencer's NoteOns.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nw4r_audio import Rbnk, Rseq, Rwsd, Rsar  # noqa: E402

ANY = None


def _s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Walker:
    """All-paths MML walker (MmlParser::Parse command lengths, MKW retail prefixes).

    Program, transpose and variables are tracked as small sets of possible
    values (None = any): `prg (var 0)` after `var0 = rnd(0, 2) + 58` reaches
    programs 58-60 only. Both sides of every condition are taken."""

    LIMIT = 128

    def __init__(self, data, base):
        self.d = data
        self.base = base
        self.notes = set()
        self.seen = set()
        self.tracks = []

    def run(self, offset):
        # SeqPlayer: local and track variables start at -1, globals are the game's.
        init = tuple([frozenset({-1})] * 16 + [ANY] * 16 + [frozenset({-1})] * 16)
        self.tracks.append((self.base + offset, init))
        opened = set()
        while self.tracks:
            pc, vars_ = self.tracks.pop()
            if (pc, vars_) in opened:
                continue
            opened.add((pc, vars_))
            self._walk((pc, frozenset({0}), frozenset({0}), (), vars_))
        return self.notes

    @classmethod
    def _set(cls, values):
        values = frozenset(values)
        return ANY if len(values) > cls.LIMIT else values

    @classmethod
    def _range(cls, lo, hi):
        return ANY if hi - lo + 1 > cls.LIMIT else frozenset(range(lo, hi + 1))

    def _walk(self, start):
        work = [start]
        d = self.d
        while work:
            state = work.pop()
            if state in self.seen:
                continue
            self.seen.add(state)
            pc, prg, transpose, stack, vars_ = state
            if pc >= len(d):
                continue

            def arg(kind, pc):
                """(value set or ANY, next pc) for ReadArg's argument types."""
                if kind == 'u8':
                    return frozenset({d[pc]}), pc + 1
                if kind == 'var':
                    i = d[pc]
                    return (vars_[i] if i < 48 else frozenset({0})), pc + 1
                if kind == 's16':
                    return frozenset({_s16((d[pc] << 8) | d[pc + 1])}), pc + 2
                if kind == 'rnd':
                    lo, hi = _s16((d[pc] << 8) | d[pc + 1]), _s16((d[pc + 2] << 8) | d[pc + 3])
                    return self._range(min(lo, hi), max(lo, hi)), pc + 4
                v = 0
                while True:
                    b = d[pc]
                    pc += 1
                    v = (v << 7) | (b & 0x7F)
                    if not b & 0x80:
                        return frozenset({v}), pc

            cmd = d[pc]
            pc += 1
            conditional = False
            time_type = None
            override = None
            if cmd == 0xA2:
                cmd = d[pc]
                pc += 1
                conditional = True
            if cmd in (0xA3, 0xA4, 0xA5):
                time_type = {0xA3: 's16', 0xA4: 'rnd', 0xA5: 'var'}[cmd]
                cmd = d[pc]
                pc += 1
            if cmd == 0xA0:
                cmd = d[pc]
                pc += 1
                override = 'rnd'
            elif cmd == 0xA1:
                cmd = d[pc]
                pc += 1
                override = 'var'

            def same(pc):
                return (pc, prg, transpose, stack, vars_)

            if not cmd & 0x80:
                velocity = d[pc]
                _, pc = arg(override or 'vmidi', pc + 1)
                keys = ANY if transpose is ANY else frozenset(max(0, min(127, cmd + t)) for t in transpose)
                self.notes.add((prg, keys, velocity))
                work.append(same(pc))
                continue
            hi = cmd & 0xF0
            succ = []
            if hi == 0x80:
                if cmd == 0x80:
                    _, pc = arg(override or 'vmidi', pc)
                    succ.append(same(pc))
                elif cmd == 0x81:
                    v, pc = arg(override or 'vmidi', pc)
                    new = ANY if v is ANY or prg is ANY else self._set(
                        {x & 0xFFFF for x in v if 0 <= x < 0x10000} | ({p for p in prg} if any(not 0 <= x < 0x10000 for x in v) else set()))
                    succ.append((pc, new, transpose, stack, vars_))
                    if conditional:
                        succ.append(same(pc))
                elif cmd == 0x88:
                    off = (d[pc + 1] << 16) | (d[pc + 2] << 8) | d[pc + 3]
                    pc += 4
                    self.tracks.append((self.base + off, vars_))
                    succ.append(same(pc))
                elif cmd in (0x89, 0x8A):
                    off = (d[pc] << 16) | (d[pc + 1] << 8) | d[pc + 2]
                    pc += 3
                    if cmd == 0x89:
                        succ.append((self.base + off, prg, transpose, stack, vars_))
                    elif len(stack) < 3:
                        succ.append((self.base + off, prg, transpose, stack + ((pc, False),), vars_))
                    else:
                        succ.append(same(pc))
                    if conditional:
                        succ.append(same(pc))
                else:
                    succ.append(same(pc))
            elif hi in (0xB0, 0xC0, 0xD0):
                v, pc = arg(override or 'u8', pc)
                if time_type:
                    _, pc = arg(time_type, pc)
                if cmd == 0xC3:
                    new = ANY if v is ANY else frozenset((x & 0xFF) - 256 if (x & 0xFF) >= 128 else x & 0xFF for x in v)
                    succ.append((pc, prg, new, stack, vars_))
                    if conditional:
                        succ.append(same(pc))
                elif cmd == 0xD4:
                    succ.append((pc, prg, transpose, stack + ((pc, True),) if len(stack) < 3 else stack, vars_))
                    if conditional:
                        succ.append(same(pc))
                else:
                    succ.append(same(pc))
            elif hi == 0x90:
                succ.append(same(pc))
            elif hi == 0xE0:
                _, pc = arg(override or 's16', pc)
                succ.append(same(pc))
            else:  # 0xF0
                if cmd == 0xFE:
                    succ.append(same(pc + 2))
                elif cmd == 0xFF:
                    if conditional:
                        succ.append(same(pc))
                elif cmd == 0xF0:
                    ex = d[pc]
                    pc += 1
                    if ex & 0xF0 == 0xE0:
                        _, pc = arg(override or 's16', pc)
                        succ.append(same(pc))
                    elif ex & 0xF0 in (0x80, 0x90):
                        idx = d[pc]
                        a2, pc = arg(override or 's16', pc + 1)
                        new_vars = vars_
                        if ex & 0xF0 == 0x80 and idx < 48:
                            new_vars = list(vars_)
                            new_vars[idx] = self._op(ex, vars_[idx], a2)
                            new_vars = tuple(new_vars)
                        succ.append((pc, prg, transpose, stack, new_vars))
                        if conditional and new_vars is not vars_:
                            succ.append(same(pc))
                    else:
                        succ.append(same(pc))
                elif cmd == 0xFD:
                    # Return: unwind loops to the innermost call frame.
                    s = list(stack)
                    target = None
                    while s:
                        ret, loop = s.pop()
                        if not loop:
                            target = ret
                            break
                    if target is not None:
                        succ.append((target, prg, transpose, tuple(s), vars_))
                    if conditional or target is None:
                        succ.append(same(pc))
                elif cmd == 0xFC:
                    if stack and stack[-1][1]:
                        succ.append((stack[-1][0], prg, transpose, stack, vars_))
                        succ.append((pc, prg, transpose, stack[:-1], vars_))
                    succ.append(same(pc))
                else:
                    succ.append(same(pc))
            work.extend(succ)

    @classmethod
    def _op(cls, ex, cur, a2):
        """SeqTrack variable commands (0xF080-0xF08B) on value sets; s16 results."""
        if ex == 0x80:
            return a2
        if a2 is ANY or (cur is ANY and ex != 0x86):
            return ANY
        if ex == 0x86:
            out = set()
            for n in a2:
                out |= set(range(n, 1)) if n < 0 else set(range(0, n + 1))
            return cls._set(out)
        def f(v, n):
            if ex == 0x81: return v + n
            if ex == 0x82: return v - n
            if ex == 0x83: return v * n
            if ex == 0x84: return int(v / n) if n else v
            if ex == 0x85: return v << n if n >= 0 else v >> -n
            if ex == 0x87: return v & n
            if ex == 0x88: return v | n
            if ex == 0x89: return v ^ n
            if ex == 0x8A: return ~(n & 0xFFFF)
            if ex == 0x8B: return int(__import__('math').fmod(v, n)) if n else v
            return v
        return cls._set(_s16(f(v, n)) for v in cur for n in a2)


def sequence_waves(rseq, offset, bank):
    """Wave indices a sequence starting at `offset` can play through `bank`."""
    notes = Walker(rseq.d, rseq.base).run(offset)
    waves = set()
    for prg, key, velocity in notes:
        prgs = range(bank.count) if prg is ANY else sorted(prg)
        keys = range(128) if key is ANY else sorted(key)
        for p in prgs:
            for k in keys:
                info = bank.inst_info(p, k, velocity)
                if info:
                    waves.add(info['waveIndex'])
    return waves


def reach(rsar):
    """{soundId: (waveFileId, sorted wave indices)} for every sequence and wave sound."""
    files = {}

    def main(fid):
        if fid not in files:
            files[fid] = rsar.file_bytes(fid)[0]
        return files[fid]

    banks = {}
    seqs = {}
    rwsds = {}
    out = {}
    for s in rsar.sounds:
        if s['type'] == 1:
            b = rsar.banks[s['seq']['bankId']]
            if b['fileId'] not in banks:
                banks[b['fileId']] = Rbnk(main(b['fileId']))
            if s['fileId'] not in seqs:
                seqs[s['fileId']] = Rseq(main(s['fileId']))
            out[s['id']] = (b['fileId'], sorted(sequence_waves(seqs[s['fileId']], s['seq']['dataOffset'], banks[b['fileId']])))
        elif s['type'] == 3:
            if s['fileId'] not in rwsds:
                rwsds[s['fileId']] = Rwsd(main(s['fileId']))
            entry = rwsds[s['fileId']].sound(s['wave']['subNo'])
            out[s['id']] = (s['fileId'], sorted({n['waveIndex'] for n in entry['notes'] if n['waveIndex'] >= 0}))
    return out


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    r = Rsar((root / 'data/extracted/sound/revo_kart.brsar').read_bytes())
    table = reach(r)
    for name in sys.argv[1:]:
        s = r.by_name[name]
        print(name, table.get(s['id']))
