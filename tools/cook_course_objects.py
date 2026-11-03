#!/usr/bin/env python3
"""Cook course-object models for the object renderer (race courses by default).

Each MDL0 of each placed-object archive becomes one GMDL with its vertex order
preserved and, when it animates or has several joints, a DOBJ bank (CHR0
palettes, PAT0/SRT0/CLR0 material tracks and the joint table). Textures are
encoded once per source archive. Output per course:
  models/course_NN_visuals.list   lines: resource model gmdl bank|-
Original archives are only read; nothing in them is modified.
"""
import argparse, concurrent.futures, hashlib, json, os, struct, subprocess, sys
from pathlib import Path
from course_catalog import ROOT, courses
from cook_model import cook
from cook_shape_morph import append_parasol_morph, append_wanwan_morph
from course_mii_textures import load_course_mii_textures
from cook_course_mii_heads import inputs as mii_head_inputs, cook as cook_mii_heads

OUT = ROOT / 'sd-card/3ds/mkw-3ds'
EXCLUDED = {'course_model.brres', 'vrcorn_model.brres', 'map_model.brres'}
# Archives whose models share joint names: keep each model's own clips.
CLIP_EXCLUDE = {
    ('hanachan', 'hanachan'): '^(wait|walk)$',
    ('hanachan', 'hanachan_angry'): '^(wait|walk)$',
    ('hanachan', 'hanachan_body1'): '^head_', ('hanachan', 'hanachan_body2'): '^head_',
    ('hanachan', 'hanachan_body3'): '^head_', ('hanachan', 'hanachan_body4'): '^head_',
}


def mdl0_names(path):
    data = path.read_bytes()
    if data[:4] != b'bres':
        raise ValueError('Not a BRRES: ' + str(path))

    def dictionary(offset):
        count = struct.unpack_from('>I', data, offset + 4)[0]
        entries = []
        for i in range(1, count + 1):
            entry = offset + 8 + 16 * i
            name, target = struct.unpack_from('>II', data, entry + 8)
            entries.append((data[offset + name:data.index(b'\0', offset + name)].decode('latin1'), offset + target))
        return entries
    root = struct.unpack_from('>H', data, 12)[0]
    for name, group in dictionary(root + 8):
        if name == '3DModels(NW4R)':
            return [n for n, _ in dictionary(group)]
    return []


def safe(name):
    return ''.join(ch if ch.isalnum() else '_' for ch in name.lower())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cook_course(course, force, mii_manifest=None):
    if mii_manifest is None: mii_manifest = ROOT / 'assets/mii/course-portraits/course-textures.json'
    folder = ROOT / 'assets/courses' / course['key']
    manifest = OUT / f"models/course_{course['id']:02d}_visuals.list"
    stamp = ROOT / 'build/object-exports' / course['key'] / 'stamp.json'
    tools = [ROOT / 'build/export_brres.mjs', ROOT / 'tools/cook_model.py', ROOT / 'tools/gx_material.py', ROOT / 'tools/coplanar_layers.py', ROOT / 'tools/world_uv.py', ROOT / 'tools/cook_shape_morph.py', Path(__file__)]
    archives = sorted(p for p in folder.glob('*.brres') if not p.name.startswith('._') and p.name not in EXCLUDED)
    mii_textures, mii_inputs = load_course_mii_textures(mii_manifest, course['key'])
    missing = set(mii_textures) - {p.stem for p in archives}
    if missing: raise ValueError(f'Unknown Mii resources in {course["key"]}: {sorted(missing)}')
    tools.append(ROOT / 'tools/course_mii_textures.py')
    tools.append(ROOT / 'tools/cook_course_mii_heads.py')
    _, head_inputs = mii_head_inputs(course)
    mii_inputs += head_inputs
    signature = hashlib.sha256(''.join(digest(p) for p in archives + tools + mii_inputs).encode()).hexdigest()
    if not force and manifest.is_file() and stamp.is_file() and json.loads(stamp.read_text()).get('signature') == signature:
        return json.loads(stamp.read_text())['report']
    lines, report, encoded = [], [], set()
    for archive in archives:
        resource = archive.stem
        texture_prefix = f"obj{course['id']:02d}_{safe(resource)}"
        for model in mdl0_names(archive):
            stem = f"obj{course['id']:02d}_{safe(resource)}__{safe(model)}"
            work = ROOT / 'build/object-exports' / course['key'] / safe(resource) / safe(model)
            work.mkdir(parents=True, exist_ok=True)
            bank = work / 'bank.dobj'
            bank.unlink(missing_ok=True)
            env = {**os.environ, 'MKW_FREEZE_NONLINEAR_UV': '1', 'MKW_OBJECT_MATERIALS': '1', 'MKW_OBJECT_BANK': str(bank), 'MKW_RETAIL_LIGHTING': '1',
                   'MKW_TEXTURE_SOURCES': os.pathsep.join(str(folder / n) for n in ('course_model.brres', 'vrcorn_model.brres') if (folder / n).is_file())}
            morph = resource == 'r_parasol' and model == 'r_parasol'
            if morph: env['MKW_CAPTURE_MORPH'] = '1'
            for (res, pattern), regex in CLIP_EXCLUDE.items():
                if res == resource and pattern == model:
                    env['MKW_OBJECT_CLIP_EXCLUDE'] = regex
            with (work / 'export.log').open('w') as log:
                subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(archive), str(work), model],
                               env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            parts = cook(work, OUT, stem, 256, preserve_vertices=True, texture_prefix=texture_prefix, encoded_textures=encoded, texture_overrides=mii_textures.get(resource, {}))
            if len(parts) != 1:
                raise ValueError(f'{course["name"]}: {resource}/{model} needs {len(parts)} mesh parts')
            info = json.loads((work / 'bank.json').read_text())
            if morph:
                info['morph'] = append_parasol_morph(archive, bank)
                info['chr'] = info['morph']
                (work / 'bank.json').write_text(json.dumps(info, indent=2)+'\n')
            if resource == 'wanwan' and model == 'wanwan':
                info['morph'] = append_wanwan_morph(archive, bank)
                (work / 'bank.json').write_text(json.dumps(info, indent=2)+'\n')
            animated = info['chr'] or info['pat'] or info['srt'] or info['clr'] or info['palette'] > 1
            bank_name = '-'
            if animated:
                bank_name = stem
                target = OUT / 'models' / (stem + '.dobj')
                target.write_bytes(bank.read_bytes())
            else:
                (OUT / 'models' / (stem + '.dobj')).unlink(missing_ok=True)
            lines.append(f"{resource} {model} {parts[0]['name']} {bank_name}\n")
            report.append({'resource': resource, 'model': model, 'gmdl': parts[0]['name'], 'bank': bank_name,
                           'bank_bytes': bank.stat().st_size if animated else 0, 'gmdl_bytes': parts[0]['bytes'],
                           'triangles': parts[0]['triangles'], **{k: info[k] for k in ('palette', 'nodes', 'weighted', 'chr', 'pat', 'srt', 'clr')}})
    heads = cook_mii_heads(course, OUT)
    lines += heads
    for line in heads:
        resource, model, mesh, bank_name = line.split()
        report.append({'resource': resource, 'model': model, 'gmdl': mesh, 'bank': bank_name,
                       'bank_bytes': 0, 'gmdl_bytes': (OUT / 'models' / (mesh + '.gmdl')).stat().st_size,
                       'triangles': 2, 'palette': 0, 'nodes': 0, 'weighted': False,
                       'chr': [], 'pat': [], 'srt': [], 'clr': []})
    manifest.write_text(''.join(lines))
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps({'signature': signature, 'report': report}) + '\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--force', action='store_true')
    p.add_argument('--mii-heads', type=Path, help='Offline Mii billboard texture manifest; no runtime head assembly')
    p.add_argument('--ids', type=int, nargs='+', required=True)
    a = p.parse_args()
    catalog = courses()
    if a.ids is not None:
        unknown = set(a.ids) - {c['id'] for c in catalog}
        if unknown:
            p.error(f'Unknown course IDs: {sorted(unknown)}')
    # An explicit arena ID must not silently succeed without exporting it.
    # Keep the ordinary default race-only batch; arenas use the same pipeline.
    selected = [c for c in catalog if
                (c['kind'] == 'race' if a.ids is None else c['id'] in a.ids)]
    if not (ROOT/'build/export_brres.mjs').is_file():raise FileNotFoundError('Prepare the individual BRRES exporter manually; see docs/MANUAL-BUILD.md')

    def run(course):
        report = cook_course(course, a.force, a.mii_heads)
        banks = sum(r['bank_bytes'] for r in report)
        print(f"{course['id']:02d} {course['name']}: {len(report)} models, banks {banks/1024:.0f} KiB", flush=True)
        return course['id'], report
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        results = dict(pool.map(run, selected))
    summary = ROOT / 'build/course-visuals-receipt.json'
    previous = json.loads(summary.read_text()) if summary.is_file() else {}
    previous.update({f'{k:02d}': v for k, v in results.items()})
    summary.write_text(json.dumps(previous, indent=1) + '\n')


if __name__ == '__main__':
    main()
