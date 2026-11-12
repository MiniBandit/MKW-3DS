"""Retain r_parasol's original SHP0 position/normal target and weight curves.

SHP0 v4 layout: BrawlCrate BrawlLib/SSBB/Types/Animations/SHP0.cs.
The DOBJ v1 extension stores sparse local-space deltas and sampled weights;
the existing palette skinning applies after morphing. No geometry is invented.
"""
from pathlib import Path
import struct


def append_parasol_morph(archive, bank):
    b = Path(archive).read_bytes()
    u32 = lambda p: struct.unpack_from('>I', b, p)[0]
    s32 = lambda p: struct.unpack_from('>i', b, p)[0]
    u16 = lambda p: struct.unpack_from('>H', b, p)[0]
    string = lambda p: b[p:b.index(b'\0', p)].decode('ascii')
    def dictionary(p):
        return [(string(p+u32(p+8+16*i+8)), p+u32(p+8+16*i+12))
                for i in range(1, u32(p+4)+1)]
    groups = dict(dictionary(u16(12)+8))
    model = dict(dictionary(groups['3DModels(NW4R)']))['r_parasol']
    def buffers(at):
        result = {}
        for name, p in dictionary(model+u32(model+at)):
            # This original model uses XYZ F32 for both position and normal.
            if u32(p+0x18) != 4 or b[p+0x1d] != 12:
                raise ValueError('Unsupported SHP0 target format: '+name)
            result[name] = [struct.unpack_from('>3f', b, p+u32(p+8)+i*12)
                            for i in range(u16(p+0x1e))]
        return result
    positions, normals = buffers(0x18), buffers(0x1c)
    data = Path(bank).read_bytes()
    header = struct.unpack_from('<4s9I', data)
    _, version, vertices, matrices, shapes, nodes, chr_count, pat, srt, clr = header
    if version != 1 or not vertices or chr_count or pat or srt or clr:
        raise ValueError('Expected captured static primary r_parasol bank')
    source = [struct.unpack_from('<6fI', data, 40+i*28) for i in range(vertices)]
    # With no animation records, the existing bank ends after its node table.
    bind_at = 40+vertices*28+shapes*8
    base = data[bind_at:bind_at+matrices*48]
    clips, morphs, receipt = [], [], []
    for name, a in dictionary(groups['AnmShp(NW4R)']):
        if u32(a+8) != 4:
            raise ValueError('Unsupported SHP0 version')
        frames, loop = u16(a+0x24)+1, u32(a+0x28)
        entries = dictionary(a+u32(a+0x10))
        if len(entries) != 1:
            raise ValueError('Expected one original parasol morph entry')
        entry_name, e = entries[0]
        flags, fixed, index_at = u32(e), u32(e+12), e+u32(e+16)
        count = u16(e+10)
        if flags != 7 or count != 1 or fixed:
            raise ValueError('Unsupported parasol SHP0 channels or fixed weights')
        strings_at = a+u32(a+0x14)
        names = [string(strings_at+s32(strings_at+i*4)) for i in range(2)]
        base_name, target_name = names[u16(e+8)], names[u16(index_at)]
        if entry_name != base_name:
            raise ValueError('SHP0 entry/base name mismatch')
        pairs = {}
        for p, n, q, r in zip(positions[base_name], normals[base_name],
                               positions[target_name], normals[target_name], strict=True):
            key = tuple(p+n);delta = tuple(y-x for x,y in zip(p+n,q+r))
            if key in pairs and pairs[key] != delta:
                raise ValueError('Ambiguous original vertex target')
            pairs[key] = delta
        changed, deltas = [], []
        for i, v in enumerate(source):
            key = tuple(v[:6])
            if key not in pairs:
                raise ValueError('Cooked vertex lost its original SHP0 correspondence')
            delta = pairs[key]
            if any(abs(x)>1e-8 for x in delta):changed.append(i);deltas.extend(delta)
        offset_word = index_at-count*4
        curve = offset_word+s32(offset_word)
        # NW4R F96 keys store frame, value, tangent (not tangent, frame, value).
        keys = [struct.unpack_from('>3f', b, curve+8+i*12) for i in range(u16(curve))]
        def weight(f):
            if f <= keys[0][0]:return keys[0][1]
            if f >= keys[-1][0]:return keys[-1][1]
            for left, right in zip(keys, keys[1:]):
                fa, va, ta = left;fb, vb, tb = right
                if fa <= f <= fb:
                    t = (f-fa)/(fb-fa);t2=t*t;t3=t2*t
                    return (2*t3-3*t2+1)*va+(t3-2*t2+t)*(fb-fa)*ta+(-2*t3+3*t2)*vb+(t3-t2)*(fb-fa)*tb
            raise ValueError('SHP0 curve interval missing')
        weights = [weight(f) for f in range(frames)]
        # Identity animation record keeps the renderer's existing clip/frame API.
        clip_index = len(clips)
        clips.append(struct.pack('<32s4I', name.encode(), frames, loop, 1, 0)+base)
        morphs.append(struct.pack('<3I', clip_index, len(changed), frames)+
                      struct.pack('<%dI'%len(changed), *changed)+
                      struct.pack('<%df'%len(deltas), *deltas)+
                      struct.pack('<%df'%frames, *weights))
        receipt.append({'name':name,'frames':frames,'vertices':len(changed),'sourceKeys':keys})
    head = bytearray(data[:40]);struct.pack_into('<I',head,24,len(clips))
    Path(bank).write_bytes(bytes(head)+data[40:]+b''.join(clips)+b'MORF'+struct.pack('<I',len(morphs))+b''.join(morphs))
    return receipt


def append_wanwan_morph(archive, bank):
    """Native Wanwan SHP0: body/eye/mouth share one F96 weight curve.

    Re-export original target buffers to preserve independent GX indices.
    Leave the existing CHR0/palette and its runtime state clock untouched.
    """
    b = Path(archive).read_bytes()
    u32 = lambda p: struct.unpack_from('>I', b, p)[0]
    s32 = lambda p: struct.unpack_from('>i', b, p)[0]
    u16 = lambda p: struct.unpack_from('>H', b, p)[0]
    string = lambda p: b[p:b.index(b'\0', p)].decode('ascii')
    def dictionary(p):
        return [(string(p+u32(p+8+16*i+8)), p+u32(p+8+16*i+12))
                for i in range(1, u32(p+4)+1)]
    groups = dict(dictionary(u16(12)+8))
    model = dict(dictionary(groups['3DModels(NW4R)']))['wanwan']
    def buffers(at):
        return {name:[struct.unpack_from('>3f', b, p+u32(p+8)+i*12)
                      for i in range(u16(p+0x1e))]
                for name,p in dictionary(model+u32(model+at))
                if u32(p+0x18)==4 and b[p+0x1d]==12}
    positions,normals=buffers(0x18),buffers(0x1c)
    data=Path(bank).read_bytes()
    _,version,nv,nm,ns,nn,nc,np,nt,nl=struct.unpack_from('<4s9I',data)
    if version!=1 or nc!=1 or np or nt or nl or b'MORF' in data:
        raise ValueError('Expected original one-CHR Wanwan bank')
    chr_at=40+nv*28+ns*8+nm*48+nn*140
    frames,loop,step,animated=struct.unpack_from('<4I',data,chr_at+32)
    if step!=1:raise ValueError('Wanwan CHR sampling changed')
    name,a=dict(dictionary(groups['AnmShp(NW4R)'])).popitem()
    if name!='wanwan' or u32(a+8)!=4 or u16(a+0x24)!=frames or u32(a+0x28)!=loop:
        raise ValueError('Wanwan SHP/CHR timelines differ')
    strings_at=a+u32(a+0x14)
    names=[string(strings_at+s32(strings_at+i*4)) for i in range(6)]
    targets=[];shared_keys=None
    for entry,e in dictionary(a+u32(a+0x10)):
        index_at=e+u32(e+16)
        if u32(e)!=7 or u16(e+10)!=1 or u32(e+12):
            raise ValueError('Unsupported Wanwan SHP channels')
        base,target=names[u16(e+8)],names[u16(index_at)]
        if entry!=base:raise ValueError('Wanwan SHP base mismatch')
        targets.append((base,target))
        word=index_at-4;curve=word+s32(word)
        keys=[struct.unpack_from('>3f',b,curve+8+i*12) for i in range(u16(curve))]
        if shared_keys is not None and shared_keys!=keys:raise ValueError('Wanwan weights must share curve')
        shared_keys=keys
    def weight(f):
        if f<=keys[0][0]:return keys[0][1]
        if f>=keys[-1][0]:return keys[-1][1]
        for (fa,va,ta),(fb,vb,tb) in zip(keys,keys[1:]):
            if fa<=f<=fb:
                t=(f-fa)/(fb-fa);t2=t*t;t3=t2*t
                return (2*t3-3*t2+1)*va+(t3-2*t2+t)*(fb-fa)*ta+(-2*t3+3*t2)*vb+(t3-t2)*(fb-fa)*tb
        raise ValueError('Missing SHP interval')
    # Re-export the target buffers through the SAME GX draw traversal. Equal
    # source normals can have different target normals, so value lookup would
    # lose the original separate normal indices at hard edges.
    import os, subprocess, tempfile
    modified=bytearray(b)
    for offset in (0x18,0x1c):
        entries=dict(dictionary(model+u32(model+offset)))
        for base,target in targets:
            src,dst=entries[base],entries[target]
            count=u16(src+0x1e)
            if count!=u16(dst+0x1e):raise ValueError('SHP target count mismatch')
            at=src+u32(src+8);other=dst+u32(dst+8)
            modified[at:at+count*12]=b[other:other+count*12]
    root=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='wanwan-shp-',dir=root/'build') as tmp:
        tmp=Path(tmp);patched=tmp/'target.brres';patched.write_bytes(modified)
        target_bank=tmp/'target.dobj'
        env={**os.environ,'MKW_FREEZE_NONLINEAR_UV':'1','MKW_OBJECT_MATERIALS':'1',
             'MKW_OBJECT_BANK':str(target_bank),'MKW_RETAIL_LIGHTING':'1'}
        subprocess.run(['node',str(root/'build/export_brres.mjs'),str(patched),str(tmp/'export'),'wanwan'],
                       env=env,check=True,capture_output=True)
        target=target_bank.read_bytes()
    if target[:40]!=data[:40]:raise ValueError('SHP target changed DOBJ layout')
    changed=[];deltas=[];unmatched=0
    for i in range(nv):
        v=struct.unpack_from('<6fI',data,40+i*28)
        t=struct.unpack_from('<6fI',target,40+i*28)
        if v[6]!=t[6]:raise ValueError('SHP target changed matrix order')
        d=tuple(y-x for x,y in zip(v[:6],t[:6]))
        if any(abs(x)>1e-8 for x in d):changed.append(i);deltas.extend(d)
        else:unmatched+=1
    # The archive's eye/body/mouth together cover all non-chain vertices.
    if not changed:raise ValueError('Empty Wanwan SHP target')
    weights=[weight(f) for f in range(frames)]
    payload=struct.pack('<3I',0,len(changed),frames)+struct.pack('<%dI'%len(changed),*changed)+struct.pack('<%df'%len(deltas),*deltas)+struct.pack('<%df'%frames,*weights)
    Path(bank).write_bytes(data+b'MORF'+struct.pack('<I',1)+payload)
    return {'clip':'wanwan','frames':frames,'vertices':len(changed),'target_buffers':targets,'unchanged_vertices':unmatched,'sourceKeys':keys,'weights':weights,'original_prefix_bytes':len(data)}
