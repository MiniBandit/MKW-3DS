"""Read retail Wii RFL resource groups without modifying source assets.

Layout follows RMCE01 parseOnmemoryRes_ (800bc770) and RFLiLoadTexture
(800bcfc0): group+4 holds count+1 relative offsets, followed by payloads.
Payload formats are deliberately left intact for the model/texture cookers.
"""
from dataclasses import dataclass
from pathlib import Path
import argparse
import hashlib
import json
import struct

@dataclass(frozen=True)
class Group:
    header_value: int
    records: tuple[bytes, ...]

class Resource:
    def __init__(self, data: bytes):
        if len(data) < 76:
            raise ValueError('truncated RFL header')
        count, self.header_value = struct.unpack_from('>HH', data)
        if count != 18:
            raise ValueError('unsupported RFL group count')
        starts = struct.unpack_from('>18I', data, 4)
        if starts[0] < 76 or any(a >= b for a, b in zip(starts, starts[1:])):
            raise ValueError('invalid RFL group order')
        groups = []
        for start, end in zip(starts, (*starts[1:], len(data))):
            if not start + 4 <= end <= len(data):
                raise ValueError('RFL group outside file')
            entries, header_value = struct.unpack_from('>HH', data, start)
            payload = start + 4 + 4 * (entries + 1)
            if payload > end:
                raise ValueError('truncated RFL offset table')
            offsets = struct.unpack_from(f'>{entries+1}I', data, start + 4)
            if any(a > b for a, b in zip(offsets, offsets[1:])) or offsets[-1] > end - payload:
                raise ValueError('RFL record outside group')
            groups.append(Group(header_value, tuple(data[payload+a:payload+b] for a,b in zip(offsets, offsets[1:]))))
        self.groups = tuple(groups)

    def inventory(self):
        return {'headerValue': self.header_value, 'groups': [
            {'group': i, 'headerValue': group.header_value, 'count': len(group.records),
             'records': [{'index': j, 'bytes': len(record), 'sha256': hashlib.sha256(record).hexdigest()}
                         for j, record in enumerate(group.records)]}
            for i, group in enumerate(self.groups)]}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('inventory', type=Path)
    args = parser.parse_args()
    data = args.source.read_bytes()
    report = Resource(data).inventory()
    report.update(source=str(args.source), sourceSha256=hashlib.sha256(data).hexdigest())
    args.inventory.write_text(json.dumps(report, indent=2)+'\n')
    print(f"Read {len(report['groups'])} groups, {sum(g['count'] for g in report['groups'])} records")

# RMCE01 8024b4c0/8024b4e8/8024b508: facial textures, shape textures, shapes.
FACIAL_TEXTURE_GROUPS = (1, 2, 11, 12, 10)
SHAPE_TEXTURE_GROUPS = (4, 17, 15, 7)
SHAPE_GROUPS = (13, 5, 3, 8, 16, 0, 14, 9, 6)

def texture_image(record):
    """Decode the original GX image; preserve source pixels and alpha."""
    import io
    from gclib.texture_utils import decode_image, ImageFormat, PaletteFormat
    if len(record) < 32:
        raise ValueError('truncated RFL texture header')
    fmt = record[0]
    width, height = struct.unpack_from('>HH', record, 2)
    offset = struct.unpack_from('>I', record, 28)[0]
    blocks = {0:(8,8,32), 1:(8,4,32), 2:(8,4,32), 3:(4,4,32),
              4:(4,4,32), 5:(4,4,32), 6:(4,4,64), 14:(8,8,32)}
    if fmt not in blocks or not 0 < width <= 1024 or not 0 < height <= 1024:
        raise ValueError('unsupported RFL texture dimensions/format')
    bw,bh,size=blocks[fmt]
    size *= ((width+bw-1)//bw)*((height+bh-1)//bh)
    if offset < 32 or offset+size > len(record):
        raise ValueError('truncated RFL image payload')
    return decode_image(io.BytesIO(record[offset:offset+size]),io.BytesIO(),
                        ImageFormat(fmt),PaletteFormat(0),0,width,height).convert('RGBA')

def shape_data(record, shape_type):
    """Read source-space fixed-point arrays and GX primitive indices.

    USA800c1da0. Instance transforms and fixed-point scales are applied by
    the renderer, not baked into these source arrays.
    """
    if shape_type not in range(9) or len(record)<6:
        raise ValueError('invalid RFL shape')
    at=4
    def take(fmt):
        nonlocal at
        size=struct.calcsize('>'+fmt)
        if at+size>len(record):raise ValueError('truncated RFL shape')
        value=struct.unpack_from('>'+fmt,record,at);at+=size;return value
    anchors=take('9f') if shape_type==2 else ()
    positions=tuple(take('3h') for _ in range(take('H')[0]))
    if not positions:
        return dict(tag=record[:4].decode('ascii'),anchors=anchors,positions=(),normals=(),uvs=(),primitives=())
    normals=tuple(take('3h') for _ in range(take('H')[0]))
    textured=shape_type not in (0,1,3,5)
    uvs=tuple(take('2h') for _ in range(take('H')[0])) if textured else ()
    primitives=[]
    for _ in range(take('B')[0]):
        count,kind=take('2B')
        indices=tuple(take('3B' if textured else '2B') for _ in range(count))
        for vertex in indices:
            if vertex[0]>=len(positions) or vertex[1]>=len(normals) or (textured and vertex[2]>=len(uvs)):
                raise ValueError('RFL vertex index outside array')
        primitives.append(dict(kind=kind,indices=indices))
    if at!=len(record):raise ValueError('unexpected RFL shape trailing bytes')
    return dict(tag=record[:4].decode('ascii'),anchors=anchors,positions=positions,normals=normals,uvs=uvs,primitives=primitives)

def mesh_data(shape):
    """Normalized source mesh, before instance placement and material setup.

    RFLLoadDrawSetting USA800c0bd0: position S16/8, normal S16/14,
    UV S16/13. Default RFL coordinate map is identity (80270d18).
    """
    vertices=[];indices=[];lookup={}
    def vertex(ref):
        ref=tuple(ref)
        if ref not in lookup:
            position=[x/256 for x in shape['positions'][ref[0]]]
            normal=[x/16384 for x in shape['normals'][ref[1]]]
            uv=[x/8192 for x in shape['uvs'][ref[2]]] if len(ref)==3 else [0.,0.]
            lookup[ref]=len(vertices);vertices.append(position+normal+uv)
        return lookup[ref]
    for primitive in shape['primitives']:
        refs=primitive['indices'];kind=primitive['kind']
        if kind==0x90:
            if len(refs)%3:raise ValueError('incomplete GX triangle list')
            triangles=[refs[i:i+3] for i in range(0,len(refs),3)]
        elif kind==0x98:
            triangles=[(refs[i-1],refs[i-2],refs[i]) if i%2 else (refs[i-2],refs[i-1],refs[i]) for i in range(2,len(refs))]
        else:raise ValueError(f'unsupported RFL primitive {kind:#x}')
        for triangle in triangles:indices.extend(vertex(ref) for ref in triangle)
    return dict(vertices=vertices,indices=indices)
