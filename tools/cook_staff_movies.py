#!/usr/bin/env python3
"""Cook both original Staff THPs at half source rate (29.97fps) with original PCM, no downloads."""
import argparse,json
from pathlib import Path
import cook_menu_movies as movie

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--output',type=Path,default=movie.OUT)
 p.add_argument('--only',choices=['normal','true'])
 args=p.parse_args();movie.OUT=args.output.resolve()
 for variant in ('normal','true'):
  if args.only and args.only!=variant:continue
  source=movie.ROOT/f'data/extracted/thp/ending/ending_{variant}.thp'
  spec=movie.make_spec(dict(source=source.relative_to(movie.ROOT).as_posix(),regions=[]),f'staff_{variant}')
  spec['sourceFrames']=spec['frames'];spec['sourceFrameStep']=2
  spec['frames']=(spec['frames']+1)//2;spec['fps']/=2;spec['fpsDenominator']*=2
  spec['workers']=6
  print(json.dumps(spec),flush=True)
  video=movie.encode(spec,'medium',False)
  audio=movie.cook_audio(source,f'movies/staff_{variant}.wav')
  receipt=movie.OUT/f'movies/staff_{variant}.source.json'
  receipt.write_text(json.dumps(dict(video=video,audio=audio),indent=2)+'\n')
if __name__=='__main__':main()
