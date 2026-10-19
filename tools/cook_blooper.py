#!/usr/bin/env python3
"""Cook the original Gesso model and its three CHR0 clips without disc extraction."""
import json
from pathlib import Path
from cook_race_items import ROOT,cook_world,digest

def main():
 output=ROOT/'build/blooper';output.mkdir(parents=True,exist_ok=True)
 source=ROOT/'assets/common/gesso.brres'
 record=cook_world(source,'gesso','item_12','start',output)
 clips={c['name']:c for c in record['clips']}
 for name,duration in [('start',70),('attack',176),('attack_end',80)]:
  assert clips[name]['duration']==duration,(name,clips[name])
 (output/'receipt.json').write_text(json.dumps({'source':str(source.relative_to(ROOT)),'source_sha256':digest(source),'model':record},indent=2)+'\n')
 print(json.dumps(record,indent=2))
if __name__=='__main__':main()
