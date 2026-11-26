#!/usr/bin/env python3
"""Cook every retail menu THP frame into seekable GPU-native ETC1 GMV atlases.

One original THP atlas stays one stream: all four course/button previews share
one SD read and texture upload per source frame. Every source frame is kept
(59.94 fps, as on the Wii). Frames are stored as GMV2: LZ4 bands, lossless
(tools/pakpack.c --gmv). The movies' sound is their original DSP ADPCM (MADP).
"""
import argparse
from bisect import bisect_right
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor, as_completed
import multiprocessing
SPAWN = multiprocessing.get_context('spawn')  # the same on every OS; fork with threads alive can hang
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import shutil
import numpy as np
from PIL import Image
from menu_movies import ThpMovie, extract_movies, thp_image
from cook_roster import ROOT
from devkitpro import tool

OUT = ROOT/'sd-card/3ds/mkw-3ds'
TEX3DS = tool('tex3ds')
HEADER = struct.Struct('<4sIHHIIIII32x')


def digest_file(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024*1024), b''): digest.update(block)
    return digest.hexdigest()


def power_of_two(value):
    return max(8, 1 << (value-1).bit_length())


def strip_tex3ds_header(data, expected):
    # tex3ds --raw retains the documented uncompressed Nintendo header.
    if len(data) == expected+4 and data[0] == 0 and int.from_bytes(data[1:4], 'little') == expected:
        return data[4:]
    if len(data) == expected+8 and data[0] == 0x80 and int.from_bytes(data[1:5], 'little') == expected:
        return data[8:]
    raise ValueError(f'Invalid raw ETC1 payload ({len(data)} bytes, expected {expected})')


def make_spec(record, identifier):
    path = ROOT/record['source']
    movie = ThpMovie(path)
    first = movie.frame(0)
    width, height = first.size
    target_width = min(width, 512 if identifier == 'title' else 256)
    target_height = round(height*target_width/width)
    if identifier == 'title': target_width, target_height = 400, 225 # Retail anamorphic image, native top-screen display rectangle.
    if identifier == 'cup_select': target_width, target_height = 400, round(height*400/width)
    texture_width, texture_height = power_of_two(target_width), power_of_two(target_height)
    fps_denominator = 100000
    fps_numerator = round(movie.fps*fps_denominator)
    regions = []
    if identifier == 'top_menu':
        # CropMovie's PPC f1..f4 are v0,v1,u0,u1, confirmed against
        # MoviePaneHandler::Draw GX texcoord writes (USA 805dc084).
        # Multi/WFC split their quadrant into vertical columns.
        cuts = [(0,.5,0,.5),(.5,2/3,0,.5),(2/3,5/6,0,.5),(5/6,1,0,.5),
                (0,.25,.5,1),(.25,.5,.5,1),(.5,1,.5,1)]
        record = {**record, 'regions': [dict(key=f'main_movie_{i}', crop=[l*width,t*height,r*width,b*height]) for i,(l,r,t,b) in enumerate(cuts)]}
    elif identifier == 'single_top':
        # OnExternalButtonSelect 80830074: selected%2 chooses the Y half;
        # selected/2 chooses X. Each quadrant is then spread over four rows.
        record = {**record, 'regions': [dict(key=f'mode_movie_{i}', crop=[
            (i//2)*width/2,(i%2)*height/2,(i//2+1)*width/2,(i%2+1)*height/2]) for i in range(4)]}
    elif identifier == 'cup_select':
        # CtrlMenuCupSelectCup::OnCupButtonSelect USA 807dfbe8 uses 180/736
        # vertically, not quarters: the last 16 source scanlines are padding.
        record = {**record, 'regions': [dict(key=f'cup_movie_{i}', crop=[
            (i//4)*width/2,(i%4)*180,(i//4+1)*width/2,(i%4+1)*180]) for i in range(8)]}
    elif identifier in ('battle_select', 'battle_cup_select'):
        # Battle selection source crops are vertical strips. Cup previews
        # occupy 250 source lines each; the final 28 lines are padding.
        prefix = 'battle_mode_movie_' if identifier == 'battle_select' else 'battle_cup_movie_'
        stride = height/2 if identifier == 'battle_select' else 250
        record = {**record, 'regions': [dict(key=prefix+str(i), crop=[
            0, i*stride, width, (i+1)*stride]) for i in range(2)]}
    elif identifier in ('battle_wii', 'battle_retro'):
        # BattleStage_ControlStage_OnSelect 8083e0c0 uses the exact float
        # 1/3 for the V stride, not 250 lines. All 752 source lines are used.
        first = 0 if identifier == 'battle_wii' else 5
        record = {**record, 'regions': [dict(key='battle_preview_'+str(first+i), crop=[
            (i//3)*width/2, (i%3)*height/3,
            (i//3+1)*width/2, (i%3+1)*height/3]) for i in range(5)]}
    for region in record['regions']:
        left, top, right, bottom = region['crop']
        regions.append(dict(key=region['key'], uv=[
            left/width*target_width/texture_width,
            top/height*target_height/texture_height,
            right/width*target_width/texture_width,
            bottom/height*target_height/texture_height,
        ], crop=region['crop']))
    return dict(name=identifier, source=record['source'], sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                sourceSize=[width, height], size=[target_width, target_height], textureSize=[texture_width, texture_height],
                frames=movie.count, fps=movie.fps, sourceFrameStep=1, fpsNumerator=fps_numerator, fpsDenominator=fps_denominator,
                frameBytes=texture_width*texture_height//2, regions=regions, path='movies/'+identifier+'.gmv')


def write_header(specs):
    from cook_menu_movie_chapters import cook as cook_chapters
    cook_chapters()
    lines = ['#pragma once', '#include <cstdint>', 'namespace mkw {',
             '// Generated by cook_menu_movies.py from the complete retail THP atlases.',
             'struct UiMovieStreamInfo {const char* path;unsigned width,height,frames;float fps;};',
             'inline constexpr UiMovieStreamInfo UiMovieStreams[]={']
    for spec in specs:
        lines.append('{%s,%u,%u,%u,%.8ff},' % (json.dumps(spec['path']), *spec['textureSize'], spec['frames'], spec['fps']))
    lines += ['};', 'struct UiMovieRegionInfo {const char* key;unsigned stream;float u0,v0,u1,v1;};',
              'inline constexpr UiMovieRegionInfo UiMovieRegions[]={']
    for stream, spec in enumerate(specs):
        for region in spec['regions']:
            lines.append('{%s,%u,%s},' % (json.dumps(region['key']), stream, ','.join(f'{value:.9f}f' for value in region['uv'])))
    lines += ['};', '}']
    (ROOT/'include/mkw/menu_movie_assets.hpp').write_text('\n'.join(lines)+'\n')


def pakpack():
    """tools/pakpack.c built for this machine (it writes GMV2)."""
    exe = ROOT/'build/host-tools'/('pakpack.exe' if os.name == 'nt' else 'pakpack')
    source = ROOT/'tools/pakpack.c'
    fresh = lambda: exe.is_file() and exe.stat().st_mtime >= source.stat().st_mtime
    if not fresh():
        exe.parent.mkdir(parents=True, exist_ok=True)
        compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('c++')
        if not compiler: raise RuntimeError('No host C compiler for tools/pakpack.c')
        # Own name, then a rename: menu and staff movies may build it at once.
        partial = exe.with_name(f'{exe.stem}-{os.getpid()}.partial{exe.suffix}')
        try:
            subprocess.run([compiler, '-x', 'c', '-O2', '-pthread', '-o', str(partial), str(source)], check=True)
            try: os.replace(partial, exe)
            except OSError:
                if not fresh(): raise # Windows: the other stage's fresh copy is running.
        finally: partial.unlink(missing_ok=True)
    return exe


def to_gmv2(packer, raw, destination):
    """GMV1 (raw ETC1 frames) to GMV2 (LZ4 bands, lossless; pakpack checks every block)."""
    partial = destination.with_suffix('.gmv2.partial')
    retry_locked(lambda: subprocess.run([str(packer), '--gmv', str(raw), str(partial)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, errors='replace'))
    retry_locked(lambda: partial.replace(destination))


def reuse(spec, quality, packer):
    """The stream's receipt when the GMV on disk is already this cook, else None."""
    destination = OUT/spec['path']
    receipt = destination.with_suffix('.json')
    expected = 64+spec['frameBytes']*spec['frames']
    if not destination.exists() or not receipt.exists(): return None
    previous = json.loads(receipt.read_text())
    if previous.get('sourceSha256') == spec['sourceSha256'] and previous.get('textureSize') == spec['textureSize'] and previous.get('quality') == quality and previous.get('sourceFrameStep',1)==spec.get('sourceFrameStep',1):
        if previous.get('container') != 'GMV2' and destination.stat().st_size == expected:
            # The same frames cooked as GMV1: only the container changes.
            to_gmv2(packer, destination, destination)
            previous.update(container='GMV2', bytes=destination.stat().st_size, rawBytes=expected, sha256=digest_file(destination))
        if previous.get('container') == 'GMV2' and destination.stat().st_size == previous.get('bytes'):
            previous.update(spec)
            receipt.write_text(json.dumps(previous, indent=2)+'\n')
            return {**previous, 'cached': True}
    return None


def plan(spec):
    """Each distinct frame once, keyed on its THP JPEG bytes: the distinct frames'
    byte ranges in first-use order, and each output frame's index into them."""
    movie = ThpMovie(ROOT/spec['source']);data = memoryview(movie.data)
    spans, order, seen = [], [], {}
    for frame in range(spec['frames']):
        at = movie.offsets[frame*spec.get('sourceFrameStep',1) % movie.count]
        start = at+8+movie.components*4;end = start+struct.unpack_from('>I', data, at+8)[0]
        index = seen.setdefault(hashlib.sha256(data[start:end]).digest(), len(spans))
        if index == len(spans) or data[slice(*spans[index])] != data[start:end]:
            index = len(spans);spans.append((start, end))
        order.append(index)
    return spans, order


def stacks(count, most):
    """Runs of at most most frames. A short tail splits into powers of two:
    tex3ds pads a height up to the next one."""
    first = 0
    while first < count:
        size = min(most, 1 << (count-first).bit_length()-1)
        yield first, size;first += size


def retry_locked(action, attempts=40):
    """Runs action again while Windows reports a file locked (EACCES/EINVAL):
    virus scanners briefly hold files just written, which failed this step at
    random on Windows (#46)."""
    for attempt in range(attempts):
        try: return action()
        except (OSError, subprocess.CalledProcessError) as error:
            locked = isinstance(error, (PermissionError, subprocess.CalledProcessError)) or getattr(error, 'errno', None) in (13, 22)
            if attempt == attempts-1 or not locked:
                if isinstance(error, subprocess.CalledProcessError) and error.stderr:
                    raise RuntimeError(f'{error.cmd[0]} failed with code {error.returncode}: {error.stderr.strip()}') from error
                raise
            time.sleep(0.05*(attempt+1))


def unlink_quietly(path):
    """Removes a scratch file; one a scanner still holds is left for cleanup."""
    try: retry_locked(lambda: path.unlink(missing_ok=True), 10)
    except OSError: pass


def encode_stack(source, spans, size, texture_size, frame_bytes, quality, out):
    """Frames stacked vertically, one tex3ds call. ETC1 blocks are coded on their
    own, so the output is each frame's own payload, in order."""
    width, height = texture_size
    stack = Image.new('RGB', (width, height*len(spans)))
    with source.open('rb') as thp:
        for i, (start, end) in enumerate(spans):
            thp.seek(start)
            image = thp_image(thp.read(end-start)).convert('RGB').resize(size, Image.Resampling.LANCZOS)
            atlas = Image.new('RGB', texture_size)
            atlas.paste(image)
            w, h = image.size
            if w < atlas.width: atlas.paste(image.crop((w-1, 0, w, h)).resize((atlas.width-w, h)), (w, 0))
            if h < atlas.height: atlas.paste(atlas.crop((0, h-1, atlas.width, h)).resize((atlas.width, atlas.height-h)), (0, h))
            stack.paste(atlas, (0, i*height))
    png, raw = out.with_suffix('.png'), out.with_suffix('.raw')
    retry_locked(lambda: stack.save(png, compress_level=1))
    retry_locked(lambda: subprocess.run([str(TEX3DS), '-f', 'etc1', '-q', quality, '-r', '-z', 'none', '-o', str(raw), str(png)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, errors='replace'))
    data = strip_tex3ds_header(retry_locked(raw.read_bytes), frame_bytes*len(spans))
    retry_locked(lambda: out.write_bytes(data))
    unlink_quietly(png);unlink_quietly(raw)


def join(spec, order, chunks, quality, packer, start):
    """The stream's GMV1 in frame order (repeats copied), then GMV2 and its receipt."""
    destination = OUT/spec['path']
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix('.gmv.partial')
    expected = 64+spec['frameBytes']*spec['frames']
    firsts, size = [first for first, _ in chunks], spec['frameBytes']
    read = lru_cache(8)(lambda n: memoryview(retry_locked(chunks[n][1].read_bytes)))
    with partial.open('wb') as output:
        output.write(HEADER.pack(b'GMV1', 1, *spec['textureSize'], spec['frames'], spec['fpsNumerator'], spec['fpsDenominator'], spec['frameBytes'], 64))
        for index in order:
            n = bisect_right(firsts, index)-1;at = (index-firsts[n])*size
            output.write(read(n)[at:at+size])
    read.cache_clear()
    for _, path in chunks: unlink_quietly(path)
    if partial.stat().st_size != expected: raise ValueError('Wrong GMV output length')
    to_gmv2(packer, partial, destination)
    unlink_quietly(partial)
    record = {**spec, 'quality': quality, 'format': 'GPU_ETC1', 'container': 'GMV2', 'everySourceFrame': spec.get('sourceFrameStep',1)==1,
              'sha256': digest_file(destination), 'bytes': destination.stat().st_size, 'rawBytes': expected, 'seconds': round(time.monotonic()-start, 2)}
    destination.with_suffix('.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


def ready(record):
    print(f'{record["name"]}: ready {record["frames"]} frames, {record["bytes"]/1048576:.1f} MiB, {record["seconds"]:.1f}s'+(' (cached)' if record.get('cached') else ''), flush=True)


def encode(specs, quality, force, jobs, packer=None):
    """Cook streams: stacks of distinct frames from every stream share one pool of
    jobs processes; each stream is joined once its stacks are done. Records in specs order."""
    start = time.monotonic()
    packer = packer or pakpack()
    records = {}
    for spec in specs:
        record = None if force else reuse(spec, quality, packer)
        if record: records[spec['name']] = record;ready(record)
    todo = [spec for spec in specs if spec['name'] not in records]
    with tempfile.TemporaryDirectory(prefix='mkw-gmv-', ignore_cleanup_errors=True) as directory, ProcessPoolExecutor(max_workers=jobs, mp_context=SPAWN) as pool, ThreadPoolExecutor(max_workers=2) as joiner:
        try:
            owner, left, joins = {}, {}, []
            # Longest first, so its join overlaps the other streams' stacks.
            for spec in sorted(todo, key=lambda spec: -spec['frames']):
                spans, order = plan(spec)
                chunks = []
                for n, (first, count) in enumerate(stacks(len(spans), max(1, 1024//spec['textureSize'][1]))):
                    out = Path(directory)/f'chunk-{spec["name"]}-{n}.etc1'
                    owner[pool.submit(encode_stack, ROOT/spec['source'], spans[first:first+count], tuple(spec['size']), tuple(spec['textureSize']), spec['frameBytes'], quality, out)] = spec['name']
                    chunks.append((first, out))
                left[spec['name']] = [spec, order, chunks, len(chunks)]
                print(f'{spec["name"]}: {spec["frames"]} frames, {len(spans)} distinct, {len(chunks)} stacks', flush=True)
            done = 0
            for future in as_completed(owner):
                future.result();done += 1
                stream = left[owner[future]];stream[3] -= 1
                if not stream[3]: joins.append(joiner.submit(join, *stream[:3], quality, packer, start))
                if done % 500 == 0: print(f'{done}/{len(owner)} stacks', flush=True)
            for job in joins:
                record = job.result();records[record['name']] = record;ready(record)
        except BaseException:
            pool.shutdown(cancel_futures=True)
            raise
    return [records[spec['name']] for spec in specs]


def decode_dsp(blocks, coefficients, history):
    """Nintendo DSP ADPCM blocks, each decoded on its own from its stored history
    (prev, prev2). Every sample step runs over all blocks at once. Returns the
    samples (one row per block, int16) and each block's sample count."""
    lengths = np.array([len(block) for block in blocks])
    groups = (int(lengths.max(initial=0))+7)//8
    raw = np.zeros((len(blocks), groups*8), np.uint8)
    for row, block in zip(raw, blocks): row[:len(block)] = np.frombuffer(block, np.uint8)
    raw = raw.reshape(len(blocks), groups, 8)
    predictor = raw[:, :, 0] >> 4
    if (predictor[np.arange(groups)*8 < lengths[:, None]] >= 8).any(): raise ValueError('Invalid DSP predictor')
    nibbles = np.stack((raw[:, :, 1:] >> 4, raw[:, :, 1:] & 15), -1).reshape(len(blocks), groups, 14).astype(np.int64)
    nibbles = np.where(nibbles >= 8, nibbles-16, nibbles) << (raw[:, :, :1] & 15).astype(np.int64) << 11
    terms = np.ascontiguousarray(nibbles.reshape(len(blocks), -1).T)
    pairs = np.take_along_axis(np.asarray(coefficients, np.int64).reshape(len(blocks), 8, 2), np.minimum(predictor, 7)[:, :, None].astype(np.intp), 1)
    c1, c2 = np.ascontiguousarray(pairs[:, :, 0].T), np.ascontiguousarray(pairs[:, :, 1].T)
    history = np.asarray(history, np.int64);previous, previous2 = history[:, 0], history[:, 1]
    samples = np.empty((groups*14, len(blocks)), np.int16)
    for step in range(groups*14):
        sample = (terms[step]+c1[step//14]*previous+c2[step//14]*previous2+1024) >> 11
        np.clip(sample, -32768, 32767, out=sample)
        previous2, previous = previous, sample
        samples[step] = sample
    # A short last group: its control byte, then two samples per byte.
    counts = lengths//8*14+np.where(lengths % 8, 2*(lengths % 8-1), 0)
    return samples.T, counts


def cook_audio(path, relative_destination='movies/title.adp'):
    """The THP's original Nintendo DSP ADPCM packets as MADP
    (include/mkw/dsp_adpcm.hpp), which the game decodes as it plays. They are
    decoded here too, to check them and to record the PCM the game will play."""
    movie = ThpMovie(path)
    data = movie.data
    component = struct.unpack_from('>I', data, 0x20)[0]
    types = list(data[component+4:component+4+movie.components])
    if types != [0, 1]: raise ValueError('Expected THP video plus one audio component')
    channels, rate, samples, tracks = struct.unpack_from('>4I', data, component+32)
    if channels not in (1, 2) or tracks != 1: raise ValueError('Unsupported THP audio layout')
    destination = OUT/relative_destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix('.adp.partial')
    rows = []
    for offset in movie.offsets:
        image_bytes, audio_bytes = struct.unpack_from('>2I', data, offset+8)
        start = offset+8+movie.components*4+image_bytes
        audio = data[start:start+audio_bytes]
        channel_bytes, count = struct.unpack_from('>2I', audio)
        if 80+channel_bytes*channels > len(audio): raise ValueError('Truncated THP audio packet')
        rows.append((audio, channel_bytes, count))
    lanes = [(audio, channel_bytes, channel) for audio, channel_bytes, _ in rows for channel in range(channels)]
    decoded, counts = decode_dsp([audio[80+channel*channel_bytes:80+(channel+1)*channel_bytes] for audio, channel_bytes, channel in lanes],
                                 [struct.unpack_from('>16h', audio, 8+channel*32) for audio, _, channel in lanes],
                                 [struct.unpack_from('>2h', audio, 72+channel*4) for audio, _, channel in lanes])
    written = 0;packets = [];pcm = hashlib.sha256()
    for row, (audio, channel_bytes, count) in enumerate(rows):
        if counts[row*channels] < count: raise ValueError('Truncated DSP sample count')
        count = min(count, samples-written)
        if count <= 0: continue
        pcm.update(decoded[row*channels:(row+1)*channels, :count].T.astype('<i2').tobytes());written += count
        # The packet as stored: what decoding count samples reads.
        used = (count+13)//14*8
        if used > channel_bytes or count > 4096: raise ValueError('Unexpected THP audio packet')
        packet = [struct.pack('<HH', count, used)]
        for channel in range(channels):
            packet.append(struct.pack('<16h', *struct.unpack_from('>16h', audio, 8+channel*32)))
            packet.append(struct.pack('<2h', *struct.unpack_from('>2h', audio, 72+channel*4)))
        for channel in range(channels):
            packet.append(audio[80+channel*channel_bytes:80+channel*channel_bytes+used])
        packets.append(b''.join(packet))
    if written != samples: raise ValueError(f'THP sample count mismatch {written} != {samples}')
    with partial.open('wb') as output:
        output.write(struct.pack('<4sIHHIIIII', b'MADP', 1, channels, 0, rate, samples, len(packets), 32, 0))
        for packet in packets: output.write(packet)
    partial.replace(destination)
    receipt = dict(source=path.relative_to(ROOT).as_posix(), sourceSha256=hashlib.sha256(data).hexdigest(), path=relative_destination, channels=channels, sampleRate=rate, samples=samples, seconds=samples/rate,
                   codec='retail Nintendo DSP ADPCM (THP audio packets) in MADP, decoded by the game to PCM16', pcmSha256=pcm.hexdigest(), sha256=digest_file(destination))
    destination.with_suffix('.audio.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(f'Original intro audio ready: {channels}ch {rate}Hz, {written} samples, {samples/rate:.3f}s', flush=True)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--quality', choices=['low', 'medium', 'high'], default='medium')
    parser.add_argument('--only', nargs='*', help='Stream names for an incremental cook; header still describes all streams')
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--audio-only', action='store_true')
    split = parser.add_mutually_exclusive_group()
    split.add_argument('--header-only', action='store_true', help='Write only the generated headers; encode nothing')
    split.add_argument('--skip-header', action='store_true', help='Encode and write everything but the generated headers')
    args = parser.parse_args()
    records = json.loads((OUT/'models/menu-ui.json').read_text())['movies']
    for source in ('thp/course/cup_select.thp', 'thp/battle/battle_select.thp',
                   'thp/battle/battle_cup_select.thp'):
        path = extract_movies([source])[0]
        if not any(Path(record['source']).stem == path.stem for record in records):
            records.append(dict(source=path.relative_to(ROOT).as_posix(), regions=[]))
    intro_path = extract_movies(['thp/title/title.thp'])[0]
    if not args.header_only:
        audio = cook_audio(intro_path)
        if args.audio_only: return
    intro = ThpMovie(intro_path)
    width, height = intro.frame(0).size
    records.append(dict(source=intro_path.relative_to(ROOT).as_posix(), regions=[dict(key='intro_movie', crop=[0, 0, width, height])]))
    specs = [make_spec(record, Path(record['source']).stem) for record in records]
    if not args.skip_header: write_header(specs)
    if args.header_only: return
    chosen = [spec for spec in specs if not args.only or spec['name'] in args.only]
    print(f'Encoding {sum(spec["frames"] for spec in chosen)} complete source frames in {len(chosen)} streams with {args.jobs} workers', flush=True)
    encode(chosen, args.quality, args.force, args.jobs)
    for spec in specs:
        receipt = OUT/Path(spec['path']).with_suffix('.json')
        if receipt.exists():
            record = json.loads(receipt.read_text())
            spec['sha256'] = record.get('sha256')
            spec['bytes'] = record['bytes']
    manifest = dict(version=1, format='GMV2 / GPU_ETC1', everySourceFrame=all(s.get('sourceFrameStep',1)==1 for s in specs), streams=specs,
                    introAudio=audio, limitations=[])
    (OUT/'models/menu-movies.json').write_text(json.dumps(manifest, indent=2)+'\n')

if __name__ == '__main__': main()
