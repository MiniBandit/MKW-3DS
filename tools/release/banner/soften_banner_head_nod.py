"""Reduce downward head pitch about the existing neck pivot, keeping arms intact."""
import math
import numpy as np

def soften_head_nod(poses,slot=6):
    # face_1 local +X points forward. Lift only the low part of the nod;
    # keep head translation/neck attachment and all other palette matrices.
    for frame in poses:
        r=frame[slot,:,:3]
        forward=r[:,0]/np.linalg.norm(r[:,0])
        elevation=math.asin(float(np.clip(forward[1],-1,1)))
        threshold=math.radians(10)
        if elevation>=threshold:continue
        target=threshold+.25*(elevation-threshold)
        axis=np.cross(forward,[0.,1.,0.]);axis/=np.linalg.norm(axis)
        x,y,z=axis;k=np.array([[0,-z,y],[z,0,-x],[-y,x,0]])
        angle=target-elevation;lift=np.eye(3)+math.sin(angle)*k+(1-math.cos(angle))*(k@k)
        frame[slot,:,:3]=lift@r
    return poses

UPPER_BODY_SLOTS=(2,3,4,5,6)
def soften_torso_lean(poses):
    # Source palettes are global. Preserve the authored steering-arm chain
    # (arm_l1=8, arm_l2=1, wrist_l1=0) rather than lifting it with the torso.
    # Only torso, cheering right arm and head receive the reduced lean.
    for frame in poses:
        spine=frame[2];angle=math.atan2(spine[2,0],spine[1,0])
        if angle<=0:continue
        delta=-.75*angle;c,s=math.cos(delta),math.sin(delta)
        turn=np.array([[1,0,0],[0,c,-s],[0,s,c]])
        pivot=spine[:,3].copy()
        for slot in UPPER_BODY_SLOTS:
            frame[slot,:,3]=pivot+turn@(frame[slot,:,3]-pivot)
            frame[slot,:,:3]=turn@frame[slot,:,:3]
    return poses
