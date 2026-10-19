"""Resolve pre-rendered course Mii images before encoding the object bank.

Manifest: {"version":1,"textures":[{"course":"ridgehighway_course",
"resource":"ridgemii00","texture":"Mii_Faces_dummy","image":"head.png"}]}.
Resource and texture names are exact BRRES names, not case-folded filenames.
Only selected-course images are opened; original archives remain read-only.
"""
import json
from pathlib import Path
from PIL import Image


def load_course_mii_textures(manifest, course):
    if manifest is None:
        return {}, []
    manifest = Path(manifest).resolve()
    data = json.loads(manifest.read_text())
    if data.get('version') != 1 or not isinstance(data.get('textures'), list):
        raise ValueError('Invalid course Mii texture manifest')
    result, inputs = {}, [manifest]
    for row in data['textures']:
        if row['course'] != course:
            continue
        resource, texture = row['resource'], row['texture']
        image = (manifest.parent / row['image']).resolve()
        textures = result.setdefault(resource, {})
        if texture in textures:
            raise ValueError(f'Duplicate Mii texture: {course}/{resource}/{texture}')
        with Image.open(image) as im:
            if im.mode != 'RGBA' or not all(8 <= n <= 256 and n & (n-1) == 0 for n in im.size):
                raise ValueError(f'Mii image requires power-of-two RGBA dimensions 8..256: {image}')
            im.load()
        textures[texture] = image
        inputs.append(image)
    return result, inputs
