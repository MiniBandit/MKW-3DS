"""Static tiled UV localization must preserve samples and gradients, not seams."""
import importlib.util,math,random,struct
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'world_uv.py'
spec=importlib.util.spec_from_file_location('world_uv',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def sample(u,wrap):
    if wrap==0:return min(1,max(0,u))
    if wrap==1:return u-math.floor(u)
    u=u%2;return u if u<=1 else 2-u

random.seed(903)
cases=0
for wraps in ((1,2,0,1),(2,1,2,0),(0,0,0,0)):
    flags=sum(w<<s for w,s in zip(wraps,(8,10,16,18)))
    for _ in range(100):
        base=[random.uniform(-500,500) for _ in range(4)]
        tri=[[0.]*6+[x+random.uniform(-2,2) for x in base]+[255]*4 for _ in range(3)]
        new=m.local_triangle(tri,flags)
        assert tri!=new or not any(wraps)
        assert m.local_triangle(tri,flags,True)==tri
        for _ in range(10):
            weights=[random.random() for _ in range(3)];weights=[x/sum(weights) for x in weights]
            for c,w in enumerate(wraps):
                u=sum(weights[i]*tri[i][6+c] for i in range(3));v=sum(weights[i]*new[i][6+c] for i in range(3))
                assert abs(sample(u,w)-sample(v,w))<1e-10
                assert abs((tri[1][6+c]-tri[0][6+c])-(new[1][6+c]-new[0][6+c]))<1e-10
            cases+=1
print(f'PASS {cases} barycentric cases: repeat/mirror phase, derivatives, clamp and animated UVs preserved')
