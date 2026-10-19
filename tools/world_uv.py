"""Keep static repeating world UVs local without changing the sampled surface.

PICA texture coordinates have limited mantissa precision. The Wii courses use
large tiled UVs (Mario Circuit curbs reach hundreds), so remove a common period
from each triangle. Mirror repeat has period two; clamp coordinates stay intact.
Do not apply this to animated SRTs or object banks that replace the baked UVs.
"""
import math

def local_triangle(vertices,flags,animated=False):
    out=[v.copy() for v in vertices]
    if animated:return out
    for at,shift in ((6,8),(7,10),(8,16),(9,18)):
        wrap=(flags>>shift)&3
        period=1 if wrap==1 else 2 if wrap==2 else 0
        if not period:continue
        offset=math.floor(min(v[at] for v in out)/period)*period
        for v in out:v[at]-=offset
    return out
