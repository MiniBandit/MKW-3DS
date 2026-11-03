"""Conservative source-geometry margins around offline raster visibility.
Adjacent faces share visibility at their common edges. Coplanar connected
faces share it across material-local surfaces. These are offline unions only;
no triangle positions are included in this helper. The approved HW132 mask
is separately bundled as include/mkw/castle_regions.hpp.
"""
import numpy as np

def expand(masks,vertices,indices,submeshes):
 count=len(masks);points=np.asarray([v[:3] for v in vertices])[np.asarray(indices).reshape(-1,3)]
 normals=np.cross(points[:,1]-points[:,0],points[:,2]-points[:,0])
 normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-20)
 parent=list(range(count));edges={};keys=[]
 def root(n):
  while parent[n]!=n:parent[n]=parent[parent[n]];n=parent[n]
  return n
 for t in range(count):
  p=[tuple(np.round(v,2))for v in points[t]]
  triangle=[tuple(sorted((p[a],p[b])))for a,b in ((0,1),(1,2),(2,0))];keys.append(triangle)
  for edge in triangle:edges[edge]=edges.get(edge,0)|masks[t]
 for sub in submeshes:
  seen={}
  for t in range(sub[0]//3,(sub[0]+sub[1])//3):
   for edge in keys[t]:
    for other in seen.get(edge,[]):
     if np.dot(normals[t],normals[other])>.9999 and abs(np.dot(normals[t],points[other,0]-points[t,0]))<1:
      parent[root(t)]=root(other)
    seen.setdefault(edge,[]).append(t)
 components={}
 for t,m in enumerate(masks):r=root(t);components[r]=components.get(r,0)|m
 result=[]
 for t in range(count):
  mask=components[root(t)]
  for edge in keys[t]:mask|=edges[edge]
  result.append(mask)
 return bytes(result)
