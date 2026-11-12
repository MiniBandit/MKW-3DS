#!/usr/bin/env python3
"""Decode native textures and sample source animation palettes for Blender."""
import io
import json
import struct
import sys
from pathlib import Path
import numpy as np

from banner_paths import ROOT, WORK
sys.path.insert(0, str(ROOT / 'vendor/gclib'))
from gclib.texture_utils import decode_image
from gclib.gx_enums import ImageFormat, PaletteFormat
from PIL import Image, ImageOps

for name in ('mario_1st', 'standard_kart'):
    folder = WORK / 'exports' / name
    data = json.loads((folder / 'model.json').read_text())
    for tex in data['textures']:
        raw = (folder / (tex['file'] + '.bin')).read_bytes()
        palette = folder / (tex['file'] + '.pal')
        pal = palette.read_bytes() if palette.exists() else b''
        image = decode_image(io.BytesIO(raw), io.BytesIO(pal), ImageFormat(tex['format']),
                             PaletteFormat(tex['paletteFormat'] or 0), len(pal)//2,
                             tex['width'], tex['height']).convert('RGBA')
        image.save(folder / (tex['file'] + '.png'))
    # A period-two reflected atlas + halved UVs reproduces MIRROR exactly
    # with ordinary REPEAT sampling in Blender and glTF, including negative UVs.
    for model in data['models']:
        for shape in model['shapes']:
            if shape['texture'] < 0: continue
            sampler = shape['sourceMaterial']['samplers'][shape['primaryMap']]
            ws, wt = sampler['wrapS'], sampler['wrapT']
            if 2 not in (ws, wt): continue
            tex = data['textures'][shape['texture']]
            if name=='mario_1st' and shape['material']=='mario_eye_tx':
                tex=next(t for t in data['textures'] if t['name']=='mario_eye.3')
            im = Image.open(folder/(tex['file']+'.png')).convert('RGBA')
            if ws == 2:
                row = Image.new('RGBA', (im.width*2,im.height))
                row.paste(im,(0,0));row.paste(ImageOps.mirror(im),(im.width,0));im=row
            if wt == 2:
                atlas = Image.new('RGBA', (im.width,im.height*2))
                atlas.paste(ImageOps.flip(im),(0,0));atlas.paste(im,(0,im.height));im=atlas
            im.save(folder/f'{tex["file"]}_mirror_{ws}_{wt}.png')

bank = (WORK / 'exports/mario_all_actions.dskm').read_bytes()
magic, version, vertices, palettes, clips, shapes, pat_clips = struct.unpack_from('<4s6I', bank)
assert magic == b'DSKM' and version in (5, 8)
offset = 32
record = np.dtype([('position', '<f4', 3), ('normal', '<f4', 3), ('palette', '<u4')])
local = np.frombuffer(bank, dtype=record, count=vertices, offset=offset)
offset += vertices*28
ranges = np.frombuffer(bank, dtype='<u4', count=shapes*2, offset=offset).reshape(shapes, 2)
offset += shapes*8
selected = {}
selected_normals = {}
metadata = []
inventory = json.loads((WORK / 'source/mario_standard/driver-inventory.json').read_text())
palette_ids = inventory['paletteIds']
bone_map = {n['name']:palette_ids.index(n['matrix']) for n in inventory['nodes'] if n['matrix'] in palette_ids}
for i in range(clips):
    name = bank[offset:offset+32].split(b'\0')[0].decode()
    duration, loop, frames = struct.unpack_from('<3I', bank, offset+32)
    offset += 44
    frame_stride = palettes*84+shapes*4
    if name in ('1st', 'good', 'gut'):
        positions = []
        normals = []
        hands = []
        for f in range(frames):
            values = np.frombuffer(bank, dtype='<f4', count=palettes*21, offset=offset+f*frame_stride).reshape(palettes, 21)
            mats = values[:, :12].reshape(palettes, 3, 4)
            transforms = mats[local['palette']]
            points = np.einsum('nij,nj->ni', transforms[:, :, :3], local['position']) + transforms[:, :, 3]
            assert np.isfinite(points).all()
            positions.append(points)
            transformed=np.einsum('nij,nj->ni',values[:,12:].reshape(palettes,3,3)[local['palette']],local['normal'])
            transformed/=np.maximum(1e-12,np.linalg.norm(transformed,axis=1))[:,None]
            normals.append(transformed)
            hands.append({b:mats[bone_map[b], :, 3].tolist() for b in ('wrist_l1','wrist_r1','face_1')})
        selected[name] = np.array(positions)
        selected_normals[name] = np.array(normals)
        metadata.append(dict(name=name, sourceDuration=duration, sourceLoopMode=loop, frames=frames, joints=hands))
    offset += frames*frame_stride
np.savez_compressed(WORK / 'exports/mario_action_positions.npz', **selected)
np.savez_compressed(WORK / 'exports/mario_action_normals.npz', **selected_normals)
reference = np.array([v[:3] for s in json.loads((WORK/'exports/mario_1st/model.json').read_text())['models'][0]['shapes'] for v in s['vertices']])
error = float(np.max(np.abs(reference-selected['1st'][20])))
assert error < 0.0001, error
common = WORK / 'source/common'
bsp = (common/'bsp/mdf_kart.bsp').read_bytes()
height = struct.unpack_from('>f', bsp)[0]
params = (common/'kartPartsDispParam.bin').read_bytes()
display = struct.unpack_from('>62f', params, 4+0x150)
seat = struct.unpack_from('>2f', (common/'kartDriverDispParam.bin').read_bytes(), 8+48*56)
wheels = []
for slot in range(4):
    at = 0x1a4 + (slot//2)*0x2c
    p = list(struct.unpack_from('>3f', bsp, at+16))
    travel = struct.unpack_from('>f', bsp, at+12)[0]
    if slot%2: p[0] = -p[0]
    p[1] += height-travel
    wheels.append(dict(name=['tire_fl','tire_fr','tire_rl','tire_rr'][slot],
                       sourceModel='tire_fl' if slot%2==0 else 'tire_fr', position=p))
receipt = dict(selectedClip='good', previewFPS=60, sourceFrames=49,
               sampledFrame20MaxError=error, sourceRanges=ranges.tolist(),
               chassisHeight=height, seatOffset=[0,*seat],
               wheelScale=list(display[15:18]), wheels=wheels, candidates=metadata,
               selectedFaceTexture='mario_eye.3',sourceFacePAT0='good',
               bumperAppearance='Original material and UVs restored, including original red/white upper lip. ',
               previewMaterial='Original albedo and mesh, simplified Blender lighting; Wii GX shader is retained in source JSON but not reproduced by this preview.')
(WORK/'exports/scene_recipe.json').write_text(json.dumps(receipt, indent=2)+'\n')
for m in metadata:
    for frame in (0,12,20,30):
        j=m['joints'][min(frame,m['frames']-1)]
        print(m['name'],frame,'hand heights',round(j['wrist_l1'][1],1),round(j['wrist_r1'][1],1),'head',round(j['face_1'][1],1))
print('Reference sample max error:',error,'seat:',seat,'wheelScale:',display[15:18])
