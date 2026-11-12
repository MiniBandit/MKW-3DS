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


# HW58: Dad requested a 3DS-specific base-level-only workaround for these
# reported road surfaces / DS Delfino advertising plaques. This intentionally
# differs from retail GX mip sampling. Never key by shared texture allocation:
# the same texture can still need mips on another material.
COURSE_BASE_LEVEL_MATERIALS = {
    # HW108: Gold Mine flags only, both original FlagB near/far model LODs.
    'obj07_flaga1__flaga1': frozenset(('M_FlagA',)),
    'obj07_flagb1__flagb1': frozenset(('M_FlagB',)),
    'obj07_flagb1__flagb1_low': frozenset(('M_FlagB',)),
    # HW62: SNES Battle Course 4 vertical block faces; retain top/floor mips.
    'course_39': frozenset(('a_block', 'a_block2')),
    # HW60: dirt road, dirt clearing and tyre marks only; roadside grass stays mipmapped.
    'course_01': frozenset(('tuti', 'hiroba_tuti', 'tuti_tire')),
    'course_24': frozenset(('a_road', 'a_road_lt')),
    'course_02': frozenset(('road', 'road_doukutu', 'road_doukutu_a', 'union_01', 'taiya')),
    'course_29': frozenset(('a_road2', 'a_tuchimichi', 'c_michi', 'c_tire', 'z_doro')),
    'course_23': frozenset(('a_tw_kanban1', 'b_tw_kanban2')),
}

def course_sampler_policy(prefix, material, rows):
    if material not in COURSE_BASE_LEVEL_MATERIALS.get(prefix, ()):
        return rows
    # Only the primary colour map; keep shadow / secondary-map sampling intact.
    s = rows[0]
    return ((s[0], s[1] & 1, s[2], s[3], s[4]), *rows[1:])


# The inspected finish-line faces can share an atlas/material with walls.
# Give only those exact original triangles their own sampler draw range.
from functools import lru_cache
from pathlib import Path
import json
@lru_cache(maxsize=1)
def _startline_faces():
    records=json.loads(Path(__file__).with_name('course_startline_faces.json').read_text())
    return {k:(set(v['materials']),set(v['faces']))for k,v in records.items()}

def course_triangle_sampler_policy(prefix,material,points,rows):
    record=_startline_faces().get(prefix)
    if record is None or material not in record[0]:return rows
    key=','.join(str(round(v*16))for p in sorted(tuple(v[:3])for v in points)for v in p)
    if key not in record[1]:return rows
    s=rows[0]
    return ((s[0],s[1]&1,s[2],s[3],s[4]),*rows[1:])
