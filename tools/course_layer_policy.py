"""Small source-geometry exceptions for the two reported course surfaces.

The layer bit only changes the existing bounded PICA depth-map offset. It
does not change source positions, UVs, mip filters, alpha or draw order.
"""
import math
from coplanar_layers import overlapping_area


def face(vertices):
    p, q, r = [v[:3] for v in vertices]
    a = [q[i] - p[i] for i in range(3)]
    b = [r[i] - p[i] for i in range(3)]
    n = [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
    length = math.sqrt(sum(x*x for x in n))
    if length < 1e-7:
        return None
    n = [x/length for x in n]
    axis = max(range(3), key=lambda i: abs(n[i]))
    axes = [i for i in range(3) if i != axis]
    projected = [tuple(v[i] for i in axes) for v in (p, q, r)]
    if ((projected[1][0]-projected[0][0])*(projected[2][1]-projected[0][1]) -
            (projected[1][1]-projected[0][1])*(projected[2][0]-projected[0][0])) < 0:
        projected.reverse()
    return n, sum(n[i]*p[i] for i in range(3)), axis, projected


def triangle_layers(models, prefix):
    selected = {}
    for model in models:
        for shape in model['shapes']:
            material = shape['material']
            if prefix == 'course_02' and material in ('union_01', 'taiya'):
                # Original raised road detail and transparent tyre marks.
                # The authored order follows road; some triangles are sloped,
                # which the general horizontal decal classifier cannot see.
                selected[id(shape)] = set(range(len(shape['vertices'])//3))
            elif prefix == 'course_29' and material in ('c_michi','c_tire'):
                # Native translucent road joins/tyre marks follow the road,
                # including sloped pieces not found by a horizontal classifier.
                selected[id(shape)] = set(range(len(shape['vertices'])//3))
            elif prefix == 'course_23' and material == 'b_tw_kanban2':
                stream = shape['vertices']
                faces = [face(stream[i:i+3]) for i in range(0, len(stream), 3)]
                layers = set()
                for i, a in enumerate(faces):
                    if a is None:
                        continue
                    for j in range(i+1, len(faces)):
                        b = faces[j]
                        if b is None or a[2] != b[2] or sum(x*y for x,y in zip(a[0],b[0])) < .9998:
                            continue
                        gaps = [sum(a[0][k]*v[k] for k in range(3))-a[1]
                                for v in stream[j*3:j*3+3]]
                        # Keep actual intersections and opposing front/back
                        # faces intact. Only separated, overlapping plaques.
                        if not (all(.1 < x <= 32 for x in gaps) or
                                all(-32 <= x < -.1 for x in gaps)):
                            continue
                        if any(max(p[k] for p in a[3]) <= min(p[k] for p in b[3]) or
                               max(p[k] for p in b[3]) <= min(p[k] for p in a[3]) for k in (0,1)):
                            continue
                        if overlapping_area(a[3], b[3]) <= 1:
                            continue
                        # Imported triangles are clockwise from their visible
                        # side: negative signed distance is toward that eye.
                        layers.add(j if gaps[0] < 0 else i)
                selected[id(shape)] = layers
    return selected
