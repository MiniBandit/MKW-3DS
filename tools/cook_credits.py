#!/usr/bin/env python3
"""Credits movies: thp/ending/ending_normal.thp and ending_true.thp.

Converted with the 0.6.0 rebuild so the credits can arrive as a quick update:
every source frame as a GPU-native ETC1 GMV stream (the same encoder as the
menu movies, tools/cook_menu_movies.py) and each movie's own DSP ADPCM audio
as PCM16 WAV. The staff roll text (ui-data/menuother_u/message_staffroll.bmg)
and the credits music (sound/strm) are already converted by other steps.
models/credits.json lists it all.
"""
import argparse
import json
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from menu_movies import ThpMovie, extract_movies
from cook_menu_movies import OUT, ROOT, cook_audio, encode, power_of_two
import hashlib

MOVIES = ['ending_normal', 'ending_true']


def spec_for(path, name):
    movie = ThpMovie(path)
    width, height = movie.frame(0).size
    texture_width, texture_height = power_of_two(width), power_of_two(height)
    denominator = 100000
    return dict(name=name, source=path.relative_to(ROOT).as_posix(), sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                sourceSize=[width, height], size=[width, height], textureSize=[texture_width, texture_height],
                frames=movie.count, fps=movie.fps, fpsNumerator=round(movie.fps*denominator), fpsDenominator=denominator,
                frameBytes=texture_width*texture_height//2,
                regions=[dict(key=name, uv=[0, 0, width/texture_width, height/texture_height], crop=[0, 0, width, height])],
                path=f'movies/{name}.gmv')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--quality', choices=['low', 'medium', 'high'], default='medium')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    paths = extract_movies([f'thp/ending/{name}.thp' for name in MOVIES])
    specs = [spec_for(path, name) for path, name in zip(paths, MOVIES)]
    with ProcessPoolExecutor(max_workers=max(1, min(args.jobs, len(specs)))) as pool:
        results = list(pool.map(encode, specs, [args.quality]*len(specs), [args.force]*len(specs)))
    for result in results:
        print(f'{result["name"]}: ready {result["frames"]} frames, {result["bytes"]/1048576:.1f} MiB'+(' (cached)' if result.get('cached') else ''), flush=True)
    audio = [cook_audio(path, name) for path, name in zip(paths, MOVIES)]
    streams = []
    for spec in specs:
        receipt = json.loads((OUT/Path(spec['path']).with_suffix('.json')).read_text())
        streams.append({**spec, 'sha256': receipt['sha256'], 'bytes': receipt['bytes']})
    manifest = dict(version=1, format='GMV1 / GPU_ETC1', everySourceFrame=True, streams=streams, audio=audio,
                    staffRoll='ui-data/menuother_u/message_staffroll.bmg', music='STRM_O_ENDING_PART_A')
    (OUT/'models').mkdir(parents=True, exist_ok=True)
    (OUT/'models/credits.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    main()
