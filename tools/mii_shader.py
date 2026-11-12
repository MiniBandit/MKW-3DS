"""Derive a Mii-only fragment-lighting variant; ordinary scene shader unchanged.

Uniform layout remains identical so the scene renderer can supply the same
projection (including screen placement), model and inverse-transpose normals.
The normal quaternion follows CFL's vshader.v.pica, with its -Z guard.
"""
def variant(source):
    source=source.replace(".proc main", ".entry native_mii\n.proc native_mii")
    source=source.replace('.out outtc texcoord0\n.out outtcw texcoord0w.x', '.out outtc texcoord0.st\n.out - texcoord0w outtc.z')
    source=source.replace('.out outcolor color','.out outcolor color\n.out outview view\n.out outnq normalquat')
    # Pack projected-W into unused texcoord0.z, keeping seven output registers.
    source=source.replace('mov outtc, r13\n        mov outtcw, r10.xxxx','mov r13.z, r10.x\n        mov outtc, r13')
    source=source.replace('mov outtc, r13\n    mov outtcw, r10.xxxx','mov r13.z, r10.x\n    mov outtc, r13')
    assert 'outtcw' not in source
    tail='''    ; CFL quaternion/view outputs. Only the native head variant has these
    ; interpolants; ordinary racers/terrain retain their original shader.
    mov outview, -r12
    dp3 r14.x, normalRow0, r15
    dp3 r14.y, normalRow1, r15
    dp3 r14.z, normalRow2, r15
    dp3 r6.x, r14, r14
    max r6.x, normalEpsilon.x, r6.x
    rsq r6.x, r6.x
    mul r14.xyz, r14, r6.x
    mov r0, constants.yxxx
    add r7.x, constants.y, r14.z
    mul r7.x, half.x, r7.x
    cmp constants.x, ge, ge, r7.x
    ifc cmp.x
        ; At -Z use CFL's degenerate quaternion (1,0,0,0).
    .else
        rsq r7.x, r7.x
        mul r6.xy, half.xy, r14.xy
        rcp r0.z, r7.x
        mul r0.xy, r6.xy, r7.x
    .end
    mov outnq, r0
'''
    assert source.count('    end\n.end')==1
    return source.replace('    end\n.end',tail+'    end\n.end')
