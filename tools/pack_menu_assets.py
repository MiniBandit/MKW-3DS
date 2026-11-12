#!/usr/bin/env python3
"""Pack the menu's runtime assets into one indexed archive, menu.pak.

Opening a file costs far more than reading a small one (on 3DS SD access,
and much more under emulators that charge a fixed open delay). The menu
streams hundreds of small textures and models, so the runtime keeps this one
archive open and reads each asset with a single seek+read. Loose files stay
in the package; anything missing from the archive is still loaded from disk.

Contents: every UI sprite texture the production composer can show (from
build/host-tools/sprite-scales.json plus all border atlases), all ui_driver /
ui_back / ui_machine models and animation banks, and every texture they use.

Format (little endian): "MPK1", u32 count, u32 namesBytes, count entries of
{u32 nameOffset, u32 offset, u32 size} sorted by name, NUL-terminated names,
then 32-byte aligned file data.
"""
import argparse, hashlib, json, struct
from gx_material import GMDL_SUBMESH_BYTES
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'sd-card/3ds/mkw-3ds'


def model_textures(path):
    data = path.read_bytes()
    magic, version, vertices, indices, submeshes, textures = struct.unpack_from('<4s5I', data)
    if magic != b'GMDL' or version not in GMDL_SUBMESH_BYTES:
        raise ValueError('Unsupported GMDL model: ' + str(path))
    names = []
    for i in range(textures):
        start = 24 + submeshes * GMDL_SUBMESH_BYTES[version] + i * 64
        names.append(data[start:start + 64].split(b'\0')[0].decode())
    return names


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=ASSETS / 'menu.pak')
    a = p.parse_args()
    manifest = json.loads((ASSETS / 'models/menu-ui.json').read_text())
    # Supply the host measurement output explicitly; no compiler is invoked.
    # The historical fallback is an existing metadata file, not a build step.
    measured = ROOT / 'build/host-tools/sprite-scales.json'
    used = json.loads((measured if measured.is_file() else ROOT / 'tools/menu_sprite_scales.json').read_text())
    atlases = {s['key'] for s in manifest['borderSlices']}
    files = set()
    names_manifest = ASSETS / 'models/menu-name-glyphs.json'
    if names_manifest.exists():
        files.update(r['path'] for r in json.loads(names_manifest.read_text())['glyphs'])
    for key, record in manifest['sprites'].items():
        if key in used or key in atlases:
            files.add(record['path'])
    models = sorted(ASSETS.glob('models/ui_driver_*')) + sorted(ASSETS.glob('models/ui_back_*')) + sorted(ASSETS.glob('models/ui_machine_*'))
    for path in models:
        if path.name.startswith('._') or path.suffix not in ('.gmdl', '.dskm'):
            continue
        files.add('models/' + path.name)
        if path.suffix == '.gmdl':
            for texture in model_textures(path):
                files.add('textures/' + texture + '.t3x')
    names = sorted(files)
    missing = [n for n in names if not (ASSETS / n).is_file()]
    if missing:
        raise SystemExit('Missing packed assets: ' + ', '.join(missing[:8]))
    blob = b''.join(n.encode() + b'\0' for n in names)
    header = 12 + 12 * len(names) + len(blob)
    offset = (header + 31) & ~31
    entries, payloads, name_offset = [], [], 0
    for n in names:
        data = (ASSETS / n).read_bytes()
        entries.append((name_offset, offset, len(data)))
        name_offset += len(n.encode()) + 1
        padded = data + b'\0' * (((len(data) + 31) & ~31) - len(data))
        payloads.append(padded)
        offset += len(padded)
    with a.output.open('wb') as out:
        out.write(b'MPK1' + struct.pack('<II', len(names), len(blob)))
        for entry in entries:
            out.write(struct.pack('<III', *entry))
        out.write(blob)
        out.write(b'\0' * (((header + 31) & ~31) - header))
        for payload in payloads:
            out.write(payload)
    digest = hashlib.sha256(a.output.read_bytes()).hexdigest()
    receipt = {'version': 1, 'files': len(names), 'bytes': a.output.stat().st_size, 'sha256': digest}
    (ASSETS / 'models/menu-pak.json').write_text(json.dumps(receipt, indent=1) + '\n')
    print(json.dumps(receipt, indent=1))


if __name__ == '__main__':
    main()
