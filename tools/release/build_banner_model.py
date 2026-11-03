#!/usr/bin/env python3
"""Build Mario/Standard Kart M/good banner geometry from extracted Wii inputs.

Run each stage explicitly. Generated assets stay in the external work directory.
This script neither compiles the game nor builds a CIA.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT/'tools/release/banner'
PYCGFX_COMMIT = '1f78850086f3a77c41e07162e842f97a5bf3c18a'


def unique(folder, name):
    matches = sorted(folder.rglob(name))
    if len(matches) != 1:
        raise ValueError(f'Expected exactly one {name} below {folder}; found {len(matches)}')
    return matches[0]


def run(argv, env):
    subprocess.run([str(a) for a in argv], check=True, cwd=ROOT, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['export', 'scene', 'cgfx'])
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--pycgfx-root', type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    work = args.work.resolve()
    if work == ROOT or ROOT in work.parents:
        parser.error('Use a work directory outside the publication repository.')
    env = os.environ.copy()
    env['MKW_BANNER_WORK'] = str(work)
    env['PYTHONHASHSEED'] = '0'
    # Do not inherit game-cook material policies in this fixed banner recipe.
    for key in list(env):
        if key.startswith('MKW_') and key != 'MKW_BANNER_WORK':
            del env[key]
    if args.stage == 'export':
        unpacked = work/'unpacked'
        driver = unique(unpacked/'mario-standard', 'driver_model.brres')
        kart = unique(unpacked/'mario-standard', 'kart_model.brres')
        tools = work/'tools';tools.mkdir(parents=True, exist_ok=True)
        exports = work/'exports';exports.mkdir(exist_ok=True)
        env['MKW_EXPORTER_OUTPUT'] = str(tools)
        for name in ['export_brres', 'export_driver']:
            run(['node','tools/bundle_cooker.mjs',f'tools/{name}.ts'],env)
        run(['node',tools/'export_brres.mjs',driver,exports/'mario_1st','model','1st','20'],env)
        run(['node',tools/'export_brres.mjs',kart,exports/'standard_kart'],env)
        run(['node',tools/'export_driver.mjs',driver,exports/'mario_all_actions.dskm'],env)
        source = work/'source';(source/'mario_standard').mkdir(parents=True,exist_ok=True)
        inventory = subprocess.check_output(['node',str(tools/'export_driver.mjs'),str(driver),'--inventory'],cwd=ROOT,env=env)
        json.loads(inventory)
        (source/'mario_standard/driver-inventory.json').write_bytes(inventory)
        common = source/'common';(common/'bsp').mkdir(parents=True,exist_ok=True)
        for name in ['kartPartsDispParam.bin','kartDriverDispParam.bin','mdf_kart.bsp']:
            dst = common/('bsp' if name.endswith('.bsp') else '')/name
            shutil.copyfile(unique(unpacked/'common',name),dst)
        lighting = source/'menu_lighting';lighting.mkdir(exist_ok=True)
        for name in ['default.blight','default.blmap']:
            shutil.copyfile(unique(unpacked/'menu-lighting',name),lighting/name)
        inputs = [driver,kart,*common.rglob('*.bin'),*common.rglob('*.bsp'),*lighting.iterdir()]
        (work/'banner-model-inputs.json').write_text(json.dumps({str(p.relative_to(work)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},indent=2)+'\n')
    elif args.stage == 'scene':
        sys.path.insert(0,str(ROOT/'tools'))
        from cook_course_lighting import parse_blight, parse_blmap, header
        lighting = work/'source/menu_lighting'
        data = parse_blight((lighting/'default.blight').read_bytes())
        data['textures'] = parse_blmap((lighting/'default.blmap').read_bytes())['textures']
        data['fog'] = []
        include = work/'include/mkw';include.mkdir(parents=True,exist_ok=True)
        (include/'menu_lighting.hpp').write_text(header([('Scene/UI/MenuSingle',data)]).replace('CourseLightings','MenuLightings').replace('courseLighting(','menuLighting('))
        run([args.cxx,'-std=c++17','-O2','-I'+str(work/'include'),'-I'+str(ROOT/'include'),SCRIPTS/'export_menu_light_textures.cpp',ROOT/'src/retail_lighting.cpp','-o',work/'tools/export_menu_light_textures'],env)
        run([sys.executable,SCRIPTS/'prepare_scene_assets.py'],env)
        run([sys.executable,SCRIPTS/'prepare_home_banner.py'],env)
    else:
        if not args.pycgfx_root:
            parser.error('cgfx requires --pycgfx-root pointing to the documented pycgfx checkout.')
        dep = args.pycgfx_root.resolve()
        revision = subprocess.check_output(['git','-C',str(dep),'rev-parse','HEAD'],text=True).strip()
        if revision != PYCGFX_COMMIT:
            parser.error(f'Expected pycgfx {PYCGFX_COMMIT}; found {revision}.')
        env['MKW_PYCGFX_ROOT'] = str(dep)
        run([sys.executable,SCRIPTS/'convert_home_banner.py'],env)
    print(f'Banner {args.stage} stage complete: {work}')


if __name__ == '__main__':
    main()
