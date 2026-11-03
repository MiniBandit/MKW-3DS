#!/usr/bin/env python3
"""Reject game resources, generated game headers and nested Git repositories."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATED = set(json.loads((ROOT / 'config/generated-inputs.json').read_text())['files'])
SUFFIXES = set('iso wbfs rvz nkit cia cci cxi 3dsx elf o a pak szs brres tpl brfnt bmg brsar breff breft thp t3x gmdl dskm djnt bnr icn cgfx shbin rkg krkg sav png jpg jpeg bmp gif wav mp3 mp4 ogg bin dat kmp kcl ttf otf woff woff2'.split())
SKIP = {'.git', '.local', '__pycache__', '.venv', 'node_modules'}


def check(folder=ROOT):
    errors = []
    for path in folder.iterdir():
        relative = path.relative_to(ROOT)
        if path.is_symlink():
            errors.append(f'Symlink is not source: {relative}')
        elif path.name == '.git':
            if path.parent != ROOT:
                errors.append(f'Nested Git repository: {relative}')
        elif path.is_dir():
            if path.name not in SKIP:
                errors.extend(check(path))
        elif (relative.parts[:3] == ('tools', 'release', 'mk7_markers') and path.name in {'map_bomb_hei.png','map_banana.png','map_exp_b.png','map_exp_r.png','map_start.png','map_kame_g.png','map_kame_wing.png','map_kame_r.png'}):
            continue
        elif (path.suffix.lower().lstrip('.') in SUFFIXES
              or relative.as_posix().removeprefix('include/') in GENERATED
              or relative.as_posix() == 'tools/course_startline_faces.json'):
            errors.append(f'Generated/game/build data is not source: {relative}')
    return errors


if __name__ == '__main__':
    errors = check()
    if errors:
        raise SystemExit('\n'.join(errors))
    print('Source tree contains no listed game/resource/build files, generated game headers or nested Git repositories.')
