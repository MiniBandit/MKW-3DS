#!/usr/bin/env python3
"""Re-cook course, sky and map models and the course objects with retail
lighting state (GMDL v4, MKW_RETAIL_LIGHTING=1) into this tree's
sd-card/3ds/mkw-3ds, then rebuild race.pak.

Unlike tools/import_courses.py this never re-extracts archives: it reads the
already unpacked course folders in assets/courses (read only). Each course's
import receipt (models/courses/NN.json) gets the fresh cooked parts, textures
and limitations exactly as import_courses.py records them; the catalog and the
source identities are left as they are.
"""
import argparse, concurrent.futures, json, os, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from course_catalog import courses
from cook_model import cook, COURSE_TEXELS, LARGE_TEXELS
from cook_course_objects import cook_course

OUT = ROOT / 'sd-card/3ds/mkw-3ds'
# tools/import_courses.py names Coconut Mall's sky after its first cook.
LEGACY = {(5, 'sky'): 'coconut_sky'}


def refresh_receipt(course, prefixes):
    """Record the re-cooked visuals in the course's import receipt."""
    path = OUT / f"models/courses/{course['id']:02d}.json"
    receipt = json.loads(path.read_text())
    for prefix in prefixes:
        model_list = f'models/{prefix}.list'
        detail = json.loads((OUT / f'models/{prefix}-receipt.json').read_text())
        visual = next(v for v in receipt['visuals'] if v['list'] == model_list)
        visual.update(parts=detail['parts'], textures=detail['textures'], lighting=detail.get('lighting'), limitations=detail['limitations'])
    path.write_text(json.dumps(receipt, indent=2) + '\n')


def recook(course, objects):
    folder = ROOT / 'assets/courses' / course['key']
    if not (folder / 'course_model.brres').is_file():
        raise SystemExit('Missing unpacked course: ' + str(folder))
    cooked = []
    for label, resource in (('course', 'course_model.brres'), ('sky', 'vrcorn_model.brres'), ('map', 'map_model.brres')):
        if not (folder / resource).is_file():
            continue
        suffix = '_sky' if label == 'sky' else '_map' if label == 'map' else ''
        prefix = LEGACY.get((course['id'], label), course['prefix'] + suffix)
        exported = ROOT / 'build/course-exports' / course['key'] / label
        exported.mkdir(parents=True, exist_ok=True)
        env = {**os.environ, 'MKW_FREEZE_NONLINEAR_UV': '1', 'MKW_RETAIL_LIGHTING': '1', 'MKW_RETAIL_SKY': '1' if label == 'sky' else '0'}
        # Course/sky TEV rules (tools/export_brres.ts courseRule), as tools/import_courses.py sets them.
        if label in ('course', 'sky'):
            env['MKW_COURSE_MATERIALS'] = '1'
        with (exported / 'export.log').open('w') as log:
            subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(folder / resource), str(exported)],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        # The same texture policy as tools/import_courses.py.
        if label == 'map':
            cook(exported, OUT, prefix, 256)
        else:
            cook(exported, OUT, prefix, texel_budget=LARGE_TEXELS if label == 'sky' else COURSE_TEXELS, brres=[folder / resource])
        cooked.append(prefix)
    refresh_receipt(course, cooked)
    if objects and course['kind'] == 'race':
        cook_course(course, True)
    return course['id'], cooked


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('ids', type=int, nargs='*', help='course ids (default: all 42)')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--no-objects', action='store_true')
    p.add_argument('--no-pack', action='store_true')
    a = p.parse_args()
    subprocess.run(['node', 'tools/bundle_cooker.mjs'], cwd=ROOT, check=True)
    selected = [c for c in courses() if not a.ids or c['id'] in a.ids]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for course_id, cooked in pool.map(lambda c: recook(c, not a.no_objects), selected):
            print(course_id, ' '.join(cooked), flush=True)
    if not a.ids or 5 in a.ids:
        subprocess.run([sys.executable, 'tools/cook_coconut_objects.py'], cwd=ROOT, check=True)
    if not a.no_pack:
        subprocess.run([sys.executable, 'tools/pack_race_assets.py'], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
