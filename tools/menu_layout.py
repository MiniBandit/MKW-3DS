"""Offline NW4R layout reader used by the private Wii menu asset pipeline.

Coordinates remain in the original BRLYT/BRCTR space. This is deliberately not
an authoring tool: material colours, samplers, pane anchors and animation keys
are taken from the retail resource, and every consumed resource is receipted.
"""
import math
import struct
from functools import lru_cache
import numpy as np
from PIL import Image
from cook_race_hud import sections, string, animation, sample
from cook_race_items import decode_tpl


def controllers(path):
    data = path.read_bytes()
    table, strings = struct.unpack_from('>2H', data, 14)
    return {string(data, strings + struct.unpack_from('>H', data, table+12+i*60)[0]):
            list(struct.unpack_from('>5f', data, table+44+i*60))
            for i in range(struct.unpack_from('>H', data, table+2)[0])}


def controller_alphas(path):
    data=path.read_bytes();table,strings=struct.unpack_from('>2H',data,14)
    return {string(data,strings+struct.unpack_from('>H',data,table+12+i*60)[0]):
            struct.unpack_from('>H',data,table+14+i*60)[0]
            for i in range(struct.unpack_from('>H',data,table+2)[0])}


def controller_animations(path):
    """BRCTR animation groups select a subset of a BRLAN's authored tracks."""
    data=path.read_bytes();offset,_,strings=struct.unpack_from('>3H',data,12)
    _,count,_,_=struct.unpack_from('>4H',data,offset)
    table=offset+8+count*8;groups=[]
    for index in range(count):
        name,panes,first,length=struct.unpack_from('>4H',data,offset+8+index*8)
        animations=[]
        for i in range(first,first+length):
            entry=table+i*12;aname,file=struct.unpack_from('>2H',data,entry)
            animations.append({'name':string(data,strings+aname),'file':string(data,strings+file)})
        groups.append({'name':string(data,strings+name),'panes':string(data,strings+panes),'animations':animations})
    return {'layout':string(data,strings+struct.unpack_from('>H',data,6)[0]),'groups':groups}


def pane_nodes(path):
    """Unanimated authored hierarchy for runtime interpolation of bound BRLANs."""
    data=path.read_bytes();materials=[];nodes=[];stack=[];last=''
    for tag,block,s in sections(data):
        if tag==b'mat1':
            for i in range(struct.unpack_from('>H',data,s)[0]):
                at=block+struct.unpack_from('>I',data,s+4+i*4)[0]
                materials.append((string(data,at),[list(struct.unpack_from('>4h',data,at+20+j*8)) for j in range(2)]))
        elif tag in (b'pan1',b'pic1',b'txt1',b'bnd1',b'wnd1'):
            name=string(data,s+4);values=list(struct.unpack_from('>10f',data,s+28))
            node=dict(name=name,parent=stack[-1] if stack else '',kind=tag.decode(),values=values,
                      flags=data[s],origin=data[s+1],alpha=data[s+2]/255,material='',
                      materialColors=[[0]*4,[255]*4],vertexColors=[[255]*4]*4,fontSize=[0,0])
            if tag in (b'pic1',b'wnd1'):
                content=s+68 if tag==b'pic1' else block+struct.unpack_from('>I',data,s+88)[0]
                node['vertexColors']=[list(data[content+j*4:content+(j+1)*4]) for j in range(4)]
                node['material'],node['materialColors']=materials[struct.unpack_from('>H',data,content+16)[0]]
            elif tag==b'txt1':
                node['material'],node['materialColors']=materials[struct.unpack_from('>H',data,s+72)[0]]
                top,bottom=list(data[s+84:s+88]),list(data[s+88:s+92])
                node['vertexColors']=[top,top,bottom,bottom]
                node['fontSize']=list(struct.unpack_from('>2f',data,s+92))
            nodes.append(node);last=name
        elif tag==b'pas1':stack.append(last)
        elif tag==b'pae1':stack.pop()
    return nodes


@lru_cache(maxsize=256)
def texture(path):
    return np.asarray(decode_tpl(path), dtype=np.float32) / 255


def sample_texture(tex, u, v, sampler):
    def wrap(c, mode):
        if mode == 1:
            return c % 1
        if mode == 2:
            c = c % 2
            return np.where(c <= 1, c, 2-c)
        return np.clip(c, 0, 1)
    u, v = wrap(u, (sampler >> 8) & 3), wrap(v, sampler & 3)
    # GX uses normalized coordinates with half-texel centres.
    x, y = u*tex.shape[1]-.5, v*tex.shape[0]-.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x-x0)[...,None], (y-y0)[...,None]
    def at(xx, yy):
        if (sampler >> 8) & 3 == 1: xx %= tex.shape[1]
        if sampler & 3 == 1: yy %= tex.shape[0]
        return tex[np.clip(yy,0,tex.shape[0]-1),np.clip(xx,0,tex.shape[1]-1)]
    return (at(x0,y0)*(1-fx)+at(x0+1,y0)*fx)*(1-fy)+(at(x0,y0+1)*(1-fx)+at(x0+1,y0+1)*fx)*fy


def render_layout(path, source, animations=(), frame=0, bounds=None, exclude=(), include=None,
                  pane_overrides=None, material_overrides=None, aspect_scale=(608/832,1.0),animation_groups=None,exclude_exact=(),z_range=None,texture_overrides=None,animation_frames=None):
    """Bake a source picture-pane subtree, returning image, bounds and pane poses.

    Bounds are [left, top, right, bottom] in a y-down coordinate system whose
    origin is the original Wii screen centre. Excluded replaceable panes are
    still reported in the pose table so the native frontend can place contents.
    """
    source(path)
    d = path.read_bytes()
    groups={};material_names=[];pane_materials={}
    for tag,block,s in sections(d):
        if tag==b'grp1':
            groups[string(d,s)]=[d[s+20+i*16:s+36+i*16].split(b'\0',1)[0].decode() for i in range(struct.unpack_from('>H',d,s+16)[0])]
        elif tag==b'mat1':
            material_names=[string(d,block+struct.unpack_from('>I',d,s+4+i*4)[0]) for i in range(struct.unpack_from('>H',d,s)[0])]
        elif tag in (b'pic1',b'txt1',b'wnd1'):
            content=s+68 if tag==b'pic1' else block+struct.unpack_from('>I',d,s+88)[0] if tag==b'wnd1' else s+56
            pane_materials[string(d,s+4)]=material_names[struct.unpack_from('>H',d,content+16)[0]]
    tracks = {};track_frames={}
    for index,ap in enumerate(animations):
        source(ap)
        incoming=animation(ap)[1]
        if animation_groups is not None and animation_groups[index]:
            members=groups[animation_groups[index]]
            allowed=set(members)|{pane_materials[p] for p in members if p in pane_materials}
            incoming={k:v for k,v in incoming.items() if k[0] in allowed}
        tracks.update(incoming)
        if animation_frames is not None:track_frames.update({key:animation_frames[index] for key in incoming})
    def value(name, kind, target, default):
        key=name,kind,target
        return sample(tracks[key],track_frames.get(key,frame)) if key in tracks else default
    textures, fonts, materials, panes, stack, posed = [], [], [], [], [], {}
    for tag, block, s in sections(d):
        if tag == b'txl1':
            table=s+4
            textures=[string(d, table+struct.unpack_from('>I',d,table+i*8)[0])
                      for i in range(struct.unpack_from('>H',d,s)[0])]
        elif tag == b'fnl1':
            table=s+4
            fonts=[string(d,table+struct.unpack_from('>I',d,table+i*8)[0]) for i in range(struct.unpack_from('>H',d,s)[0])]
        elif tag == b'mat1':
            for i in range(struct.unpack_from('>H',d,s)[0]):
                o=block+struct.unpack_from('>I',d,s+4+i*4)[0]
                name=string(d,o)
                regs=np.array(struct.unpack_from('>12h',d,o+20),dtype=np.float32).reshape(3,4)
                if material_overrides and name in material_overrides: regs[:]=material_overrides[name]
                for c in range(12): regs.flat[c]=value(name,'RLMC',c+4,regs.flat[c])
                flags=struct.unpack_from('>I',d,o+60)[0]
                samplers=[struct.unpack_from('>HH',d,o+64+j*4) for j in range(flags&15)]
                off=o+64+len(samplers)*4
                matrix=list(struct.unpack_from('>5f',d,off)) if (flags>>4)&15 else [0,0,0,1,1]
                for c in range(5):matrix[c]=value(name,'RLTS',c,matrix[c])
                off += ((flags>>4)&15)*20 + ((flags>>8)&15)*4
                vertex=True; matcol=np.ones(4,dtype=np.float32)
                if flags&(1<<25):
                    vertex=d[off]==1 or d[off+1]==1;off+=4
                if flags&(1<<27):matcol=np.array(list(d[off:off+4]),dtype=np.float32)/255;off+=4
                if flags&(1<<12):off+=4
                off+=((flags>>13)&3)*20 + ((flags>>15)&7)*4
                stages=[d[off+j*16:off+(j+1)*16] for j in range((flags>>18)&31)]
                off+=len(stages)*16
                alpha_compare=tuple(d[off:off+4]) if flags&(1<<23) else (0x77,0,0,0)
                if flags&(1<<23):off+=4
                blend=tuple(d[off:off+4]) if flags&(1<<24) else (1,4,5,3)
                materials.append(dict(name=name,regs=regs/255,samplers=samplers,matrix=matrix,
                                      constants=np.array(list(d[o+44:o+60]),dtype=np.float32).reshape(4,4)/255,
                                      stages=stages,vertex=vertex,matcol=matcol,alpha_compare=alpha_compare,blend=blend))
        elif tag in (b'pan1',b'pic1',b'txt1',b'bnd1',b'wnd1'):
            name=string(d,s+4);v=list(struct.unpack_from('>10f',d,s+28))
            for i in range(10):v[i]=value(name,'RLPA',i,v[i])
            alpha=value(name,'RLVC',16,d[s+2])/255
            if pane_overrides and name in pane_overrides:
                ov=pane_overrides[name]
                for k,i in [('x',0),('y',1),('sx',6),('sy',7),('w',8),('h',9)]:
                    if k in ov:v[i]=ov[k]
                alpha=ov.get('alpha',alpha)
            visible=bool(value(name,'RLVI',0,d[s]&1))
            if pane_overrides and name in pane_overrides:
                visible=bool(pane_overrides[name].get('visible',visible))
            if d[s]&4:
                v[6]*=aspect_scale[0];v[7]*=aspect_scale[1]
            rad=math.radians(v[5]);co,si=math.cos(rad),math.sin(rad)
            matrix=np.array([[co*v[6],-si*v[7],v[0]],[si*v[6],co*v[7],v[1]],[0,0,1]],dtype=float)
            if stack:
                parent,pa,pv,pz=stack[-1];matrix=parent@matrix;alpha*=pa;visible &= pv
            z=v[2]+(stack[-1][3] if stack else 0)
            pose=(matrix,alpha if d[s]&2 else (stack[-1][1] if stack else 1),visible,z)
            base=d[s+1];left=-(base%3)*v[8]/2;top=(base//3)*v[9]/2
            corners=matrix@np.array([[left,left+v[8],left,left+v[8]],[top,top,top-v[9],top-v[9]],[1,1,1,1]])
            rect=[float(corners[0].min()),float(-corners[1].max()),float(corners[0].max()),float(-corners[1].min())]
            posed[name]=dict(rect=rect,alpha=alpha,kind=tag.decode(),visible=visible,z=z,pivot=[float(matrix[0,2]),float(-matrix[1,2])])
            if tag==b'txt1':
                fw,fh=struct.unpack_from('>2f',d,s+92)
                posed[name]['fontSize']=[fw*float(np.linalg.norm(matrix[:2,0])),fh*float(np.linalg.norm(matrix[:2,1]))]
                pos,align=d[s+76],d[s+77]
                posed[name]['textPosition']=pos
                posed[name]['textAlign']=(pos%3 if align==0 else align-1)
                posed[name]['fontName']=fonts[struct.unpack_from('>H',d,s+74)[0]]
                posed[name]['font']=1 if 'rodan' in posed[name]['fontName'] else 0
                material=materials[struct.unpack_from('>H',d,s+72)[0]]
                posed[name]['textColors']=(material['regs'][:2]*255).round().astype(int).tolist()
                posed[name]['textVertexColors']=[list(d[s+84:s+88]),list(d[s+88:s+92])]
                posed[name]['blend']=list(material['blend'])
            if tag not in (b'pic1',b'wnd1'):continue
            content=s+68 if tag==b'pic1' else block+struct.unpack_from('>I',d,s+88)[0]
            colors=np.array(list(d[content:content+16]),dtype=np.float32).reshape(4,4)
            for i in range(16):colors.flat[i]=value(name,'RLVC',i,colors.flat[i])
            if pane_overrides and name in pane_overrides and 'vertexColors' in pane_overrides[name]:
                colors[:]=pane_overrides[name]['vertexColors']
            mi=struct.unpack_from('>H',d,content+16)[0]
            posed[name]['vertexColors']=colors.round().astype(int).tolist()
            posed[name]['materialColors']=(materials[mi]['regs'][:2]*255).round().astype(int).tolist()
            posed[name]['blend']=list(materials[mi]['blend'])
            if not visible or alpha<=0:continue
            draw=not (name in exclude_exact or any(x in name for x in exclude) or (include is not None and name not in include) or (z_range is not None and not z_range[0]<=z<z_range[1]))
            uv=struct.unpack_from('>8f',d,content+20) if d[content+18] else (0,0,1,0,0,1,1,1)
            panes.append((name,matrix,alpha,left,top,v[8],v[9],colors/255,mi,uv,z,rect,draw))
        elif tag==b'pas1':stack.append(pose)
        elif tag==b'pae1':stack.pop()
    if bounds is None:
        drawn=[p for p in panes if p[-1]]
        if not drawn:raise ValueError('Layout has no visible picture panes: '+str(path))
        bounds=[math.floor(min(p[-2][0] for p in drawn))-2,math.floor(min(p[-2][1] for p in drawn))-2,
                math.ceil(max(p[-2][2] for p in drawn))+2,math.ceil(max(p[-2][3] for p in drawn))+2]
    bounds=list(bounds)
    out=Image.new('RGBA',(round(bounds[2]-bounds[0]),round(bounds[3]-bounds[1])))
    # Section::Draw enables LEQUAL with depth writes. Preserve original pane
    # submission order: an almost transparent foreground film pane still writes
    # depth, rejecting the subsequently submitted opaque border interior.
    # Sorting by Z before blending would incorrectly reveal that interior.
    depth=np.full((out.height,out.width),-np.inf,dtype=np.float32)
    for name,matrix,alpha,left,top,w,h,colors,mi,uv,z,rect,draw in panes:
        if w<=0 or h<=0:continue
        x0,y0=max(bounds[0],math.floor(rect[0])),max(bounds[1],math.floor(rect[1]))
        x1,y1=min(bounds[2],math.ceil(rect[2])),min(bounds[3],math.ceil(rect[3]))
        if x1<=x0 or y1<=y0:continue
        yy,xx=np.mgrid[y0:y1,x0:x1];points=np.stack((xx+.5,-yy-.5,np.ones_like(xx)))
        local=np.einsum('ij,jkl->ikl',np.linalg.inv(matrix),points)
        xs=(local[0]-left)/w;ys=(top-local[1])/h
        mask=(xs>=0)&(xs<=1)&(ys>=0)&(ys<=1)
        region=depth[y0-bounds[1]:y1-bounds[1],x0-bounds[0]:x1-bounds[0]]
        mask &= z>=region
        if not np.any(mask):continue
        x,y=xs[...,None],ys[...,None]
        grad=(colors[0]*(1-x)+colors[1]*x)*(1-y)+(colors[2]*(1-x)+colors[3]*x)*y
        mat=materials[mi];regs=mat['regs'];tex=np.ones_like(grad)
        if mat['samplers']:
            ti,sampler=mat['samplers'][0];tp=path.parent.parent/'timg'/textures[ti];source(tp)
            if texture_overrides and name in texture_overrides:tp=texture_overrides[name];source(tp)
            u=(uv[0]*(1-xs)+uv[2]*xs)*(1-ys)+(uv[4]*(1-xs)+uv[6]*xs)*ys
            v=(uv[1]*(1-xs)+uv[3]*xs)*(1-ys)+(uv[5]*(1-xs)+uv[7]*xs)*ys
            ts,tt,rr,ss,st=mat['matrix'];co,si=math.cos(math.radians(rr)),math.sin(math.radians(rr))
            u,v=ss*co*(u-.5)-st*si*(v-.5)+ts+.5,ss*si*(u-.5)+st*co*(v-.5)+tt+.5
            tex=sample_texture(texture(tp),u,v,sampler)
        if not mat['stages']:
            rgba=(regs[0]+tex*(regs[1]-regs[0]))*grad if mat['samplers'] else regs[1]*grad
        else:
            # NW4R's explicit TEV stages, including the source gradient/checker
            # border shaders. Register zero is PREV; C0/C1/C2 follow it.
            ras=grad if mat['vertex'] else np.broadcast_to(mat['matcol'],grad.shape)
            tev=[np.zeros_like(grad),*[np.broadcast_to(r,grad.shape).copy() for r in regs]]
            constants=mat['constants']
            def konst(sel, alpha=False):
                if sel<8:return np.full_like(grad[...,0] if alpha else grad[...,:3],(8-sel)/8)
                if not alpha and 12<=sel<16:return np.broadcast_to(constants[sel-12,:3],grad[...,:3].shape)
                if sel>=16:
                    c=constants[(sel-16)%4,(sel-16)//4]
                    return np.full_like(grad[...,0] if alpha else grad[...,:3],c)
                return np.zeros_like(grad[...,0] if alpha else grad[...,:3])
            for stg in mat['stages']:
                def inp(c,a=False,k=0):
                    if a:
                        return tev[c][...,3] if c<4 else tex[...,3] if c==4 else ras[...,3] if c==5 else konst(k,True) if c==6 else np.zeros_like(xs)
                    if c<8:return tev[c//2][...,:3] if c%2==0 else np.repeat(tev[c//2][...,3,None],3,axis=-1)
                    if c==8:return tex[...,:3]
                    if c==9:return np.repeat(tex[...,3,None],3,axis=-1)
                    if c==10:return ras[...,:3]
                    if c==11:return np.repeat(ras[...,3,None],3,axis=-1)
                    return np.ones_like(grad[...,:3]) if c==12 else np.full_like(grad[...,:3],.5) if c==13 else konst(k) if c==14 else np.zeros_like(grad[...,:3])
                for a,ofs in [(False,4),(True,8)]:
                    b0,b1,b2,b3=stg[ofs:ofs+4];A=inp(b0&15,a,b3>>3);B=inp(b0>>4,a,b3>>3);C=inp(b1&15,a,b3>>3);D=inp(b1>>4,a,b3>>3)
                    op=b2&15
                    if op not in (0,1):raise ValueError(f'Unsupported source TEV operation {op}: {path} / {mat["name"]}')
                    result=D+((A*(1-C)+B*C)*(1 if op==0 else -1))
                    bias=(b2>>4)&3;result+=(.5 if bias==1 else -.5 if bias==2 else 0)
                    result*= [1,2,4,.5][b2>>6]
                    if b3&1:result=np.clip(result,0,1)
                    dest=(b3>>1)&3
                    if a:tev[dest][...,3]=result
                    else:tev[dest][...,:3]=result
            rgba=tev[0]
        rgba[...,3]*=alpha
        compare,operation,ref_a,ref_b=mat['alpha_compare']
        def alpha_test(kind,ref):
            a=np.clip(rgba[...,3]*255,0,255)
            if kind==0:return np.zeros_like(mask)
            if kind==1:return a<ref
            if kind==2:return a==ref
            if kind==3:return a<=ref
            if kind==4:return a>ref
            if kind==5:return a!=ref
            if kind==6:return a>=ref
            return np.ones_like(mask)
        a,b=alpha_test(compare&15,ref_a),alpha_test(compare>>4,ref_b)
        mask &= (a&b,a|b,a^b,~(a^b))[operation]
        region[mask]=z
        # Replacement movie/icon images still write their authored depth,
        # while their pixels are supplied separately by the native frontend.
        if not draw:continue
        rgba[...,3]*=mask
        out.alpha_composite(Image.fromarray(np.uint8(np.clip(rgba*255,0,255))),(x0-bounds[0],y0-bounds[1]))
    return out,bounds,posed


def messages(path, icon_glyphs=False):
    """Return retail BMG message IDs and text, skipping binary control commands."""
    d=path.read_bytes()
    if d[:8]!=b'MESGbmg1':raise ValueError('Invalid BMG: '+str(path))
    offset=32;blocks={}
    for _ in range(struct.unpack_from('>I',d,12)[0]):
        size=struct.unpack_from('>I',d,offset+4)[0]
        if size<8 or offset+size>len(d):raise ValueError('Invalid BMG block')
        blocks[d[offset:offset+4]]=offset;offset+=size
    info,strings=blocks[b'INF1'],blocks[b'DAT1']+8
    count,stride=struct.unpack_from('>HH',d,info+8)
    result={}
    for index in range(count):
        at=strings+struct.unpack_from('>I',d,info+16+index*stride)[0];chars=[]
        while at+2<=len(d) and d[at:at+2]!=b'\0\0':
            if d[at:at+2]==b'\0\x1a':
                size=d[at+2]
                if size<4:raise ValueError('Invalid BMG control command')
                if icon_glyphs and size==8 and d[at+3:at+6]==b'\x01\x00\x00':
                    chars.append(d[at+6:at+8].decode('utf-16be'))
                at+=size;continue
            chars.append(d[at:at+2].decode('utf-16be'));at+=2
        identifier=struct.unpack_from('>I',d,blocks[b'MID1']+16+index*4)[0] if b'MID1' in blocks else index
        result[identifier]=''.join(chars)
    return result


def message_font_indices(path):
    """Read the INF1 font attribute consumed by BMGHolder::GetFontIndex."""
    d=path.read_bytes();offset=32
    for _ in range(struct.unpack_from('>I',d,12)[0]):
        if d[offset:offset+4]==b'INF1':
            count,stride=struct.unpack_from('>HH',d,offset+8)
            return {d[offset+16+index*stride+4] for index in range(count)}
        offset+=struct.unpack_from('>I',d,offset+4)[0]
    raise ValueError('Missing INF1 font attributes: '+str(path))
