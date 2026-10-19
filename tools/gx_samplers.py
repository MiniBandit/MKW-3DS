"""Preserve each GX material's filters and LOD bias independently of textures."""
import math, struct

EMPTY=(0.,0,0,0,0)

def sampler(entry):
    if entry is None:return EMPTY
    bias=float(entry['lodBias']);minimum=int(entry['minFilter']);magnify=int(entry['magFilter'])
    if not math.isfinite(bias) or not -16<=bias<16 or not 0<=minimum<=5 or magnify not in (0,1):
        raise ValueError('Unsupported GX sampler')
    return (bias,minimum,magnify,1,0)

def shape_samplers(shape):
    entries=(shape.get('sourceMaterial') or {}).get('samplers',[])
    def get(key):
        index=shape.get(key,-1)
        return sampler(entries[index]) if 0<=index<len(entries) else EMPTY
    # Three-map translated materials retain their third sampler in map order.
    used={shape.get('primaryMap',-1),shape.get('secondaryMap',-1)}
    third=[i for i in range(len(entries)) if i not in used]
    return (get('primaryMap'),get('secondaryMap'),
            sampler(entries[third[0]]) if (shape.get('reflection') or {}).get('mode')==8 and len(third)==1 else EMPTY)

def block(rows):
    return struct.pack('<4sI',b'SMP1',len(rows))+b''.join(struct.pack('<f4B',*s) for row in rows for s in row)
