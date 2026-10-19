"""Identify authored translucent course layers sharing a previously drawn plane.

Also retain the draw order of raised floor decals. Wii courses place some
opaque atlas decals only five units above the road; the distant PICA depth
interpolator can merge their depths even though their source planes differ.
"""
import math

def horizontal_triangle(tri):
    if max(v[1] for v in tri)-min(v[1] for v in tri)>0.01:return None
    points=[(v[0],v[2]) for v in tri]
    a,b,c=points
    cross=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    if abs(cross)<1e-5:return None
    if cross<0:points.reverse()
    return sum(v[1] for v in tri)/3,points

def overlapping_area(a,b):
    # Clip one triangle against the other. Bounding boxes alone also match
    # adjacent road triangles and must not promote them to decal layers.
    polygon=a[:]
    def side(p,q,r):return (q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0])
    for edge in range(3):
        p,q=b[edge],b[(edge+1)%3];out=[]
        if not polygon:return 0
        previous=polygon[-1];dp=side(p,q,previous)
        for current in polygon:
            dc=side(p,q,current)
            if (dp>=0)!=(dc>=0):
                t=dp/(dp-dc);out.append((previous[0]+t*(current[0]-previous[0]),previous[1]+t*(current[1]-previous[1])))
            if dc>=0:out.append(current)
            previous,dp=current,dc
        polygon=out
    return abs(sum(polygon[i][0]*polygon[(i+1)%len(polygon)][1]-polygon[(i+1)%len(polygon)][0]*polygon[i][1] for i in range(len(polygon))))*.5 if polygon else 0

def triangle_plane(tri):
    p,q,r=[v[:3] for v in tri]
    a=[q[i]-p[i] for i in range(3)];b=[r[i]-p[i] for i in range(3)]
    n=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
    length=math.sqrt(sum(x*x for x in n))
    if length<1e-7:return None
    n=[x/length for x in n];axis=max(range(3),key=lambda i:abs(n[i]))
    if n[axis]<0:n=[-x for x in n]
    key=tuple(round(x*100000) for x in n)+(round(sum(n[i]*p[i] for i in range(3))*100),)
    axes=[i for i in range(3) if i!=axis]
    bounds=tuple(x for i in axes for x in (min(v[i] for v in (p,q,r)),max(v[i] for v in (p,q,r))))
    return key,bounds

def course_layers(models):
    seen={};layers={};floors={}
    for model in models:
        for shape in model['shapes']:
            rop=shape.get('sourceMaterial',{}).get('gxMaterial',{}).get('ropInfo',{})
            planes=[x for start in range(0,len(shape['vertices']),3) if (x:=triangle_plane(shape['vertices'][start:start+3]))]
            layer=False
            if rop.get('depthTest') and not rop.get('depthWrite') and rop.get('blendMode')==1:
                for key,b in planes:
                    old=seen.get(key)
                    if old and b[0]<old[1]-1e-3 and b[1]>old[0]+1e-3 and b[2]<old[3]-1e-3 and b[3]>old[2]+1e-3:
                        layer=True;break
            layers[id(shape)]=layer
            horizontal=[value for start in range(0,len(shape['vertices']),3)
                        if (value:=horizontal_triangle(shape['vertices'][start:start+3]))]
            if rop.get('depthTest') and rop.get('depthWrite') and rop.get('blendMode')==0:
                for height,tri in horizontal:
                    key=math.floor(height/8)
                    for bin_ in (key-1,key):
                        for oldHeight,oldTri,oldShape in floors.get(bin_,[]):
                            if oldShape==id(shape) or not .1<height-oldHeight<=8:continue
                            if overlapping_area(tri,oldTri)>1:
                                layer=True;break
                        if layer:break
                    if layer:break
                layers[id(shape)]=layer
                for height,tri in horizontal:floors.setdefault(math.floor(height/8),[]).append((height,tri,id(shape)))
            # Ignore geometry that cannot contribute to the depth-tested scene.
            if rop.get('depthTest'):
                for key,b in planes:
                    old=seen.get(key)
                    seen[key]=b if old is None else (min(b[0],old[0]),max(b[1],old[1]),min(b[2],old[2]),max(b[3],old[3]))
    return layers
