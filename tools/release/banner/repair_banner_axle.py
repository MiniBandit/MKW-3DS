"""Add metal axle stubs with positive overlap into both rear wheel hubs."""
import numpy as np

def axle_arrays(arrays, indices):
    arrays={k:np.asarray(v) for k,v in arrays.items()}
    p=arrays['POSITION']; extra={k:[] for k in arrays}; faces=[]
    # Authored rear shaft has a marginal join near x=0.495. Continue it well
    # into the hub at x=0.58, with overlap at both ends, at the wheel axis.
    for sign in (-1,1):
        mask=(p[:,0]*sign>.32)&(p[:,0]*sign<.51)&(p[:,1]>.23)&(p[:,1]<.31)&(p[:,2]>-.48)&(p[:,2]<-.38)
        candidates=np.flatnonzero(mask)
        if not len(candidates):raise ValueError('Source rear metal shaft not found')
        sample=candidates[np.argmin(np.linalg.norm(p[candidates]-[sign*.48,.27,-.43],axis=1))]
        base=len(p)+len(extra['POSITION'])
        for x in (.43,.615):
            for j in range(8):
                theta=2*np.pi*j/8;ny,nz=np.cos(theta),np.sin(theta)
                for k,v in arrays.items():
                    val=v[sample].copy()
                    if k=='POSITION':val=np.array([sign*x,.27+.032*ny,-.43+.032*nz])
                    elif k=='NORMAL':val=np.array([0,ny,nz])
                    elif k=='COLOR_0':
                        # Fixed baked ambient/top light, matching source shader inputs.
                        val=val.astype(float);val[:3]*=.72+.28*max(0,ny);val=val.astype(v.dtype)
                    extra[k].append(val)
        for j in range(8):
            a=base+j;b=base+(j+1)%8;c=base+8+j;d=base+8+(j+1)%8
            faces.extend([[a,b,c],[b,d,c]])
        for j in range(1,7):faces.extend([[base,base+j+1,base+j],[base+8,base+8+j,base+8+j+1]])
    return {k:np.concatenate((v,np.asarray(extra[k],dtype=v.dtype))) for k,v in arrays.items()},np.concatenate((np.asarray(indices).ravel(),np.array(faces).ravel()))

AXLE_NOTE='Two 8-sided metal shaft extensions, abs X .43 to .615, Y .27, Z -.43, radius .032; overlap original shaft and rear wheel hub centers abs X .58.'
