#!/usr/bin/env python3
"""Cook the Blue Shell / Bob-omb blast model for its runtime material binder.

The shared race items carry bombCore with frame-zero UVs and fallback materials. This
writes a raw-UV copy (models/bomb_blast.gmdl and textures) into the build overlay
build/vfx, which tools/package_sd.py packages, and regenerates include/mkw/bomb_blast.hpp
with the original SRT0 and bombCoreRed / bombCoreBlue CLR0 tracks (tools/export_blast.ts).
The models/bomb_core.dskm pose bank keeps driving the geometry: the vertex order is the same.
"""
import json, os, subprocess, struct
from pathlib import Path
from cook_model import cook

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/common/bombCore.brres'
OUT = ROOT / 'build/vfx'


def main():
    subprocess.run(['node', 'tools/bundle_cooker.mjs'], cwd=ROOT, check=True)
    work = ROOT / 'build/blast-export'
    work.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'MKW_FREEZE_NONLINEAR_UV': '1', 'MKW_RAW_UV': '1'}
    subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(SOURCE), str(work), 'bombCore', 'bombCore', '0'],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    parts = cook(work, OUT, 'bomb_blast', preserve_vertices=True)
    if len(parts) != 1:
        raise ValueError('Blast geometry unexpectedly needs partitioning')
    model = OUT / 'models/bomb_blast.gmdl'
    (OUT / 'models' / (parts[0]['name'] + '.gmdl')).replace(model)
    shipped = ROOT / 'sd-card/3ds/mkw-3ds/models/bomb_core.gmdl'
    if shipped.is_file():
        a, b = model.read_bytes(), shipped.read_bytes()
        if struct.unpack_from('<II', a, 8) != struct.unpack_from('<II', b, 8):
            raise ValueError('Blast model no longer matches the bomb_core pose bank')
    result = subprocess.run(['node', str(ROOT / 'build/export_blast.mjs'), str(SOURCE), str(ROOT / 'include/mkw/bomb_blast.hpp')],
                            check=True, capture_output=True, text=True)
    print(json.dumps({'model': model.relative_to(ROOT).as_posix(), 'triangles': parts[0]['triangles'], 'tables': json.loads(result.stdout)}))


if __name__ == '__main__':
    main()
