"""Offline adaptation of RMCE01's statue shader (USA805a5f18).

Inputs are course.bmm, course.btiMat and course.btiEnv, loaded by USA805a4dec.
Bake at the portrait's front view: the environment highlight is intentionally
fixed in the resulting billboard, not recalculated with the race camera.
"""
import io,struct,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'vendor/gclib'))
from gclib.bti import BTI

class StatueMaterial:
    def __init__(self,folder):
        b=(folder/'course.bmm').read_bytes()
        if len(b)!=20:raise ValueError('Expected original 20-byte course.bmm')
        self.color=np.frombuffer(b[:3],dtype=np.uint8).astype(float)/255
        self.mix=b[3]/255
        self.radius,*self.offset=struct.unpack_from('>4f',b,4)
        if not np.isfinite([self.radius,*self.offset]).all() or self.radius<=0:
            raise ValueError('Invalid statue projection parameters')
        self.textures=[]
        for name in ['course.btiMat','course.btiEnv']:
            t=BTI(io.BytesIO((folder/name).read_bytes()))
            self.textures.append((np.asarray(t.render().convert('RGBA'),dtype=float)/255,
                                  (t.wrap_s.value,t.wrap_t.value)))

    def coordinates(self,vertices):
        if vertices.shape[1]<9:raise ValueError('Re-export portrait geometry with normals for statue baking')
        def normal(v):
            length=np.linalg.norm(v,axis=1,keepdims=True)
            return np.divide(v,length,out=np.zeros_like(v),where=length>0)
        # Texcoord 2: translated object position, normalize, LightOrtho.
        p=normal(vertices[:,:3]+self.offset)
        # Texcoord 1: view normal, normalize, .5/- .5 scale and .5 bias.
        n=normal(vertices[:,6:9])
        return np.column_stack((.5+p[:,0]/(2*self.radius),.5-p[:,1]/(2*self.radius),
                                .5+n[:,0]/2,.5-n[:,1]/2))

    def shade(self,color,coordinates,opaque,sampler):
        mat,wrap=self.textures[0]
        material=sampler(mat,coordinates[...,0],coordinates[...,1],wrap)[...,:3]
        env,wrap=self.textures[1]
        environment=sampler(env,coordinates[...,2],coordinates[...,3],wrap)[...,:3]
        # Original TEV stages 1..3: opaque lerp(C2, TEX, K1.a),
        # translucent TEX*C2; then *RASC; then + environment.
        stage=np.clip(color*(1-self.mix)+material*self.mix if opaque else color*material,0,1)
        stage=np.clip(stage*self.color,0,1)
        return np.clip(stage+environment,0,1)
