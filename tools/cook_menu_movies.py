#!/usr/bin/env python3
"""Cook every retail menu THP frame into seekable GPU-native ETC1 GMV atlases.

One original THP atlas stays one stream: all four course/button previews share
one SD read and texture upload per source frame. No frame-rate decimation.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import wave
import array
import sys
import shutil
from PIL import Image
from menu_movies import ThpMovie, extract_movies
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
    source_step = 2 if identifier == 'title' and movie.fps > 30 else 1
    fps_numerator = round(movie.fps/source_step*fps_denominator)
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
                frames=(movie.count+source_step-1)//source_step, fps=movie.fps/source_step, sourceFrameStep=source_step, fpsNumerator=fps_numerator, fpsDenominator=fps_denominator,
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


def encode(spec, quality, force):
    start = time.monotonic()
    destination = OUT/spec['path']
    receipt = destination.with_suffix('.json')
    expected = 64+spec['frameBytes']*spec['frames']
    if not force and destination.exists() and receipt.exists():
        previous = json.loads(receipt.read_text())
        if previous.get('sourceSha256') == spec['sourceSha256'] and previous.get('textureSize') == spec['textureSize'] and previous.get('quality') == quality and previous.get('sourceFrameStep',1)==spec.get('sourceFrameStep',1) and destination.stat().st_size == expected:
            previous.update(spec)
            if 'sha256' not in previous:
                previous['sha256'] = digest_file(destination)
            receipt.write_text(json.dumps(previous, indent=2)+'\n')
            return {**previous, 'cached': True}
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix('.gmv.partial')
    movie = ThpMovie(ROOT/spec['source'])
    with tempfile.TemporaryDirectory(prefix='mkw-gmv-') as directory:
        workers = max(1, min(6, int(spec.get('workers', 6 if spec['name'] == 'title' else 1))))
        chunk_size = (spec['frames']+workers-1)//workers
        def chunk(worker):
            first = worker*chunk_size
            last = min(spec['frames'], first+chunk_size)
            png, raw = Path(directory)/f'frame{worker}.png', Path(directory)/f'frame{worker}.raw'
            encoded = Path(directory)/f'chunk{worker}.etc1'
            with encoded.open('wb') as output:
                for frame in range(first, last):
                    image = movie.frame(frame*spec.get('sourceFrameStep',1)).convert('RGB').resize(tuple(spec['size']), Image.Resampling.LANCZOS)
                    atlas = Image.new('RGB', tuple(spec['textureSize']))
                    atlas.paste(image)
                    w, h = image.size
                    if w < atlas.width: atlas.paste(image.crop((w-1, 0, w, h)).resize((atlas.width-w, h)), (w, 0))
                    if h < atlas.height: atlas.paste(atlas.crop((0, h-1, atlas.width, h)).resize((atlas.width, atlas.height-h)), (0, h))
                    atlas.save(png, compress_level=1)
                    subprocess.run([str(TEX3DS), '-f', 'etc1', '-q', quality, '-r', '-z', 'none', '-o', str(raw), str(png)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                    output.write(strip_tex3ds_header(raw.read_bytes(), spec['frameBytes']))
                    if (frame-first+1) % 240 == 0: print(f'{spec["name"]}: chunk {worker+1}/{workers}, {frame-first+1}/{last-first} source frames', flush=True)
            return encoded
        with ThreadPoolExecutor(max_workers=workers) as pool:
            chunks = list(pool.map(chunk, range(workers)))
        with partial.open('wb') as output:
            output.write(HEADER.pack(b'GMV1', 1, *spec['textureSize'], spec['frames'], spec['fpsNumerator'], spec['fpsDenominator'], spec['frameBytes'], 64))
            for encoded in chunks:
                with encoded.open('rb') as source: shutil.copyfileobj(source, output)
    if partial.stat().st_size != expected: raise ValueError('Wrong GMV output length')
    partial.replace(destination)
    record = {**spec, 'quality': quality, 'format': 'GPU_ETC1', 'everySourceFrame': spec.get('sourceFrameStep',1)==1,
              'sha256': digest_file(destination), 'bytes': expected, 'seconds': round(time.monotonic()-start, 2)}
    receipt.write_text(json.dumps(record, indent=2)+'\n')
    return record


def cook_audio(path, relative_destination='movies/title.wav'):
    """Decode the THP's original Nintendo DSP ADPCM channels to PCM16 WAV."""
    movie = ThpMovie(path)
    data = movie.data
    component = struct.unpack_from('>I', data, 0x20)[0]
    types = list(data[component+4:component+4+movie.components])
    if types != [0, 1]: raise ValueError('Expected THP video plus one audio component')
    channels, rate, samples, tracks = struct.unpack_from('>4I', data, component+32)
    if channels not in (1, 2) or tracks != 1: raise ValueError('Unsupported THP audio layout')
    destination = OUT/relative_destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix('.wav.partial')
    written = 0
    with wave.open(str(partial), 'wb') as output:
        output.setnchannels(channels);output.setsampwidth(2);output.setframerate(rate)
        for offset in movie.offsets:
            image_bytes, audio_bytes = struct.unpack_from('>2I', data, offset+8)
            start = offset+8+movie.components*4+image_bytes
            audio = data[start:start+audio_bytes]
            channel_bytes, count = struct.unpack_from('>2I', audio)
            if 80+channel_bytes*channels > len(audio): raise ValueError('Truncated THP audio packet')
            decoded = []
            for channel in range(channels):
                coefficients = struct.unpack_from('>16h', audio, 8+channel*32)
                previous, previous2 = struct.unpack_from('>2h', audio, 72+channel*4)
                block = audio[80+channel*channel_bytes:80+(channel+1)*channel_bytes]
                values = []
                for position in range(0, len(block), 8):
                    control = block[position];predictor = control >> 4;scale = 1 << (control & 15)
                    if predictor >= 8: raise ValueError('Invalid DSP predictor')
                    c1, c2 = coefficients[predictor*2:predictor*2+2]
                    for packed in block[position+1:position+8]:
                        for nibble in (packed >> 4, packed & 15):
                            signed = nibble-16 if nibble >= 8 else nibble
                            sample = ((signed*scale << 11)+c1*previous+c2*previous2+1024) >> 11
                            sample = max(-32768, min(32767, sample))
                            previous2, previous = previous, sample
                            values.append(sample)
                if len(values) < count: raise ValueError('Truncated DSP sample count')
                decoded.append(values[:count])
            count = min(count, samples-written)
            interleaved = array.array('h', (decoded[channel][i] for i in range(count) for channel in range(channels)))
            if sys.byteorder != 'little': interleaved.byteswap()
            output.writeframesraw(interleaved.tobytes());written += count
    if written != samples: raise ValueError(f'THP sample count mismatch {written} != {samples}')
    partial.replace(destination)
    receipt = dict(source=path.relative_to(ROOT).as_posix(), sourceSha256=hashlib.sha256(data).hexdigest(), path=relative_destination, channels=channels, sampleRate=rate, samples=samples, seconds=samples/rate, codec='retail Nintendo DSP ADPCM decoded to PCM16', sha256=digest_file(destination))
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
    args = parser.parse_args()
    records = json.loads((OUT/'models/menu-ui.json').read_text())['movies']
    for source in ('thp/course/cup_select.thp', 'thp/battle/battle_select.thp',
                   'thp/battle/battle_cup_select.thp'):
        path = extract_movies([source])[0]
        if not any(Path(record['source']).stem == path.stem for record in records):
            records.append(dict(source=path.relative_to(ROOT).as_posix(), regions=[]))
    intro_path = extract_movies(['thp/title/title.thp'])[0]
    audio = cook_audio(intro_path)
    if args.audio_only: return
    intro = ThpMovie(intro_path)
    width, height = intro.frame(0).size
    records.append(dict(source=intro_path.relative_to(ROOT).as_posix(), regions=[dict(key='intro_movie', crop=[0, 0, width, height])]))
    specs = [make_spec(record, Path(record['source']).stem) for record in records]
    write_header(specs)
    chosen = [spec for spec in specs if not args.only or spec['name'] in args.only]
    print(f'Encoding {sum(spec["frames"] for spec in chosen)} complete source frames in {len(chosen)} streams with {args.jobs} workers', flush=True)
    results = []
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(encode, spec, args.quality, args.force): spec['name'] for spec in chosen}
        for future in as_completed(futures):
            result = future.result();results.append(result)
            print(f'{result["name"]}: ready {result["frames"]} frames, {result["bytes"]/1048576:.1f} MiB, {result["seconds"]:.1f}s'+(' (cached)' if result.get('cached') else ''), flush=True)
    for spec in specs:
        receipt = OUT/Path(spec['path']).with_suffix('.json')
        if receipt.exists():
            record = json.loads(receipt.read_text())
            spec['sha256'] = record.get('sha256')
            spec['bytes'] = record['bytes']
    manifest = dict(version=1, format='GMV1 / GPU_ETC1', everySourceFrame=all(s.get('sourceFrameStep',1)==1 for s in specs), streams=specs,
                    introAudio=audio, limitations=[])
    (OUT/'models/menu-movies.json').write_text(json.dumps(manifest, indent=2)+'\n')

if __name__ == '__main__': main()
