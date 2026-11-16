#!/usr/bin/env python3
"""Decode native textures, sample Funky Kong's cannon loop and read the Flame
Runner's original assembly (BSP, bike parts and driver display parameters)."""
import io
import json
import math
import struct
import sys
import numpy as np

from banner_paths import ROOT, WORK
sys.path.insert(0, str(ROOT / 'vendor/gclib'))
from gclib.texture_utils import decode_image
from gclib.gx_enums import ImageFormat, PaletteFormat
from PIL import Image

CLIP = 'jump'          # the bike cannon pose (driverCannonJump: every bike but 26, 29, 35)
CHARACTER, VEHICLE = 22, 23  # Funky Kong, Flame Runner (la_bike)

for name in ('funky_pose', 'flame_runner'):
    folder = WORK / 'exports' / name
    data = json.loads((folder / 'model.json').read_text())
    for tex in data['textures']:
        raw = (folder / (tex['file'] + '.bin')).read_bytes()
        palette = folder / (tex['file'] + '.pal')
        pal = palette.read_bytes() if palette.exists() else b''
        decode_image(io.BytesIO(raw), io.BytesIO(pal), ImageFormat(tex['format']),
                     PaletteFormat(tex['paletteFormat'] or 0), len(pal)//2,
                     tex['width'], tex['height']).convert('RGBA').save(folder / (tex['file'] + '.png'))

bank = (WORK / 'exports/funky_all_actions.dskm').read_bytes()
magic, version, vertices, palettes, clips, shapes, pat_clips = struct.unpack_from('<4s6I', bank)
assert magic == b'DSKM' and version in (5, 8)
offset = 32
record = np.dtype([('position', '<f4', 3), ('normal', '<f4', 3), ('palette', '<u4')])
local = np.frombuffer(bank, dtype=record, count=vertices, offset=offset)
offset += vertices*28 + shapes*8
positions = normals = None
for i in range(clips):
    name = bank[offset:offset+32].split(b'\0')[0].decode()
    duration, loop, frames = struct.unpack_from('<3I', bank, offset+32)
    offset += 44
    stride = palettes*84 + shapes*4
    if name == CLIP:
        pos, nrm = [], []
        for f in range(frames):
            values = np.frombuffer(bank, dtype='<f4', count=palettes*21, offset=offset+f*stride).reshape(palettes, 21)
            mats = values[:, :12].reshape(palettes, 3, 4)[local['palette']]
            pos.append(np.einsum('nij,nj->ni', mats[:, :, :3], local['position']) + mats[:, :, 3])
            n = np.einsum('nij,nj->ni', values[:, 12:].reshape(palettes, 3, 3)[local['palette']], local['normal'])
            nrm.append(n / np.maximum(1e-12, np.linalg.norm(n, axis=1))[:, None])
        positions, normals, clip_frames, clip_loop = np.array(pos), np.array(nrm), frames, loop
    offset += frames*stride
assert positions is not None and clip_loop == 1, 'cannon clip missing or not a loop'
np.savez_compressed(WORK / 'exports/funky_action_positions.npz', **{CLIP: positions})
np.savez_compressed(WORK / 'exports/funky_action_normals.npz', **{CLIP: normals})
reference = np.array([v[:3] for s in json.loads((WORK/'exports/funky_pose/model.json').read_text())['models'][0]['shapes'] for v in s['vertices']])
error = float(np.max(np.abs(reference - positions[0])))
assert error < 0.0001, error

# Original assembly, read as tools/cook_vehicle_rest.py and tools/build.py do.
common = WORK / 'source/common'
bsp = (common / 'bsp/la_bike.bsp').read_bytes()
display = struct.unpack_from('>22f', (common / 'bikePartsDispParam.bin').read_bytes(), 4 + (VEHICLE - 18) * 0xb0)
seat = struct.unpack_from('>2f', (common / 'kartDriverDispParam.bin').read_bytes(), 8 + (VEHICLE*48 + CHARACTER)*56)
wheels = []
for slot in range(2):
    at = 0x1a4 + slot*0x2c
    wheels.append(dict(position=list(struct.unpack_from('>3f', bsp, at+16)), travel=struct.unpack_from('>f', bsp, at+12)[0],
                       radius=struct.unpack_from('>f', bsp, at+32)[0]))
handle_position, handle_rotation = list(display[3:6]), list(display[6:9])
swing_pivot = list(display[9:12])
# KartBodyBike::wheelMatrix: the front wheel hangs from the handle frame
# (makeRT), the rear from the chassis; airborne in the cannon, both at full travel.
rx = math.radians(handle_rotation[0])
assert handle_rotation[1] == handle_rotation[2] == 0
def handle_point(p):
    x, y, z = p
    return [handle_position[0] + x, handle_position[1] + y*math.cos(rx) - z*math.sin(rx), handle_position[2] + y*math.sin(rx) + z*math.cos(rx)]
front_local = [wheels[0]['position'][0], wheels[0]['position'][1] - wheels[0]['travel'], wheels[0]['position'][2]]
front = handle_point(front_local)
rear = [wheels[1]['position'][0], wheels[1]['position'][1] - wheels[1]['travel'], wheels[1]['position'][2]]
delta = np.array(rear) - np.array(swing_pivot)
recipe = dict(clip=CLIP, sourceFrames=clip_frames, referenceMaxError=error, seat=[0, *seat],
              handlePosition=handle_position, handleRotation=handle_rotation, frontAxle=front, frontAxleHandleLocal=front_local,
              rearAxle=rear, swingPivot=swing_pivot, swingAngle=math.atan2(delta[1], -delta[2]), wheels=wheels)
(WORK/'exports/scene_recipe.json').write_text(json.dumps(recipe, indent=2)+'\n')
print('Cannon clip', clip_frames, 'frames; reference error', error, 'seat', seat, 'front', front, 'rear', rear)
