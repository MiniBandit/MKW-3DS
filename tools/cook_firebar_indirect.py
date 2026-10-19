"""Cook a bounded, seekable 100-phase LA8 fireBPlane texture stream.

Input is an existing original-BRRES export, not a game image or edited asset.
Runtime integration is separate; this does not package or launch the game.
"""
from pathlib import Path
import argparse,hashlib,io,json,struct
import numpy as np
from gclib.gx_enums import ImageFormat,PaletteFormat
from gclib.texture_utils import decode_image
from firebar_indirect import relative

def cook(source,output,size=256):
    data=json.loads((source/'model.json').read_text())
    selected=[];hashes={}
    for name,fmt in [('ef_fireBPlane',3),('ef_tsubu3',1)]:
        t=next(t for t in data['textures'] if t['name']==name)
        if (t['width'],t['height'],t['format'])!=(64,64,fmt):raise ValueError('Unexpected source texture '+name)
        raw=(source/(t['file']+'.bin')).read_bytes();hashes[name]=hashlib.sha256(raw).hexdigest()
        selected.append(np.asarray(decode_image(io.BytesIO(raw),io.BytesIO(),ImageFormat(fmt),PaletteFormat(0),0,64,64).convert('RGBA'),dtype=float))
    srt=json.loads((source/'srt0.json').read_text())
    clip=next(c for c in srt if c['name']=='fireBPlane')
    if (clip['duration'],clip['loopMode'],clip['texMtxMode'])!=(600,1,2):raise ValueError('Unexpected source animation')
    tracks=next(m for m in clip['matAnimations'] if m['materialName']=='mat_fireBplane')['texAnimations']
    for t,scale,angle in zip(tracks,(1.149999976158142,1.25),(1440,-720)):
        for axis in ('scaleS','scaleT'):
            if list(t[axis]['frames'].values())!=[scale]:raise ValueError('Changed authored scale')
        frames=t['rotation']['frames']
        if len(frames)!=2 or any(abs(f['value']-f['frame']*angle/600)>1e-6 or abs(f['tangent']-angle/600)>1e-6 for f in frames):raise ValueError('Nonlinear source rotation')
        if t['translationS'] is not None or t['translationT'] is not None:raise ValueError('Changed translation')
    if size not in (128,256):raise ValueError('Unsupported candidate resolution')
    gutter=2/64;frames=100;frame_bytes=size*size*2
    # Same row orientation and byte order as cook_race_effects.write_t3x LA8.
    yy,xx=np.mgrid[:size,:size];morton=np.zeros_like(xx)
    for bit in range(3):morton|=((xx>>bit)&1)<<(bit*2);morton|=((yy>>bit)&1)<<(bit*2+1)
    index=((yy//8)*(size//8)+xx//8)*64+morton
    u=(xx+.5)/size*(1+2*gutter)-gutter;v=(yy+.5)/size*(1+2*gutter)-gutter
    header=struct.pack('<4s7I2f',b'FTAN',1,size,size,5,frames,frame_bytes,40,1/(1+2*gutter),gutter/(1+2*gutter))
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=output.with_suffix(output.suffix+'.tmp')
    with temporary.open('wb') as f:
        f.write(header)
        for phase in range(frames):
            image=np.rint(relative(*selected,u,v,phase)).clip(0,255).astype(np.uint8)
            if not np.array_equal(image[...,0],image[...,1]) or not np.array_equal(image[...,0],image[...,2]):raise ValueError('Not an LA source')
            tiled=np.empty((size*size,2),dtype=np.uint8)
            tiled[index.ravel(),0]=image[...,3].ravel();tiled[index.ravel(),1]=image[...,0].ravel()
            # Verify every address/channel before writing the GPU payload.
            restored=tiled[index.ravel()].reshape(size,size,2)
            assert np.array_equal(restored[...,0],image[...,3]) and np.array_equal(restored[...,1],image[...,0])
            f.write(tiled.tobytes())
    assert temporary.stat().st_size==40+frames*frame_bytes
    temporary.replace(output)
    report={'candidate_only':True,'source_sha256':hashes,'size':size,'frames':frames,'frame_bytes':frame_bytes,'file_bytes':output.stat().st_size,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'format':'FTAN1, PICA LA8, top-row-first 8x8 Morton tiles, no mips','runtime_integrated':False}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    print(json.dumps(cook(a.source,a.output),indent=2))
