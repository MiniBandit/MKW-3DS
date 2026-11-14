#!/usr/bin/env python3
"""Cook both original Staff THPs at the full source rate (59.94 fps) with their original DSP ADPCM sound, no downloads."""
import argparse,json
from pathlib import Path
import cook_menu_movies as movie

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--output',type=Path,default=movie.OUT)
 p.add_argument('--only',choices=['normal','true'])
 p.add_argument('--jobs',type=int,default=6)
 args=p.parse_args();movie.OUT=args.output.resolve()
 variants=[variant for variant in ('normal','true') if not args.only or args.only==variant]
 sources=[movie.ROOT/f'data/extracted/thp/ending/ending_{variant}.thp' for variant in variants]
 specs=[]
 for variant,source in zip(variants,sources):
  spec=movie.make_spec(dict(source=source.relative_to(movie.ROOT).as_posix(),regions=[]),f'staff_{variant}')
  spec['workers']=6 # unused now; kept so the receipts stay the same
  print(json.dumps(spec),flush=True);specs.append(spec)
 # Both movies' frames share one pool.
 for variant,source,video in zip(variants,sources,movie.encode(specs,'medium',False,args.jobs)):
  audio=movie.cook_audio(source,f'movies/staff_{variant}.adp')
  receipt=movie.OUT/f'movies/staff_{variant}.source.json'
  receipt.write_text(json.dumps(dict(video=video,audio=audio),indent=2)+'\n')
if __name__=='__main__':main()
