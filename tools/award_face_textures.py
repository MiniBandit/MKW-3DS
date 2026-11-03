"""Keep GX CMPR RGB even for alpha-zero texels read by opaque materials.
GX's palette3 is the midpoint with alpha0, not PC DXT1's transparent black.
These opaque TEVs still read its RGB. ETC1A4 may discard invisible RGB, so
retain the complete source RGBA8. See noclip/rust/src/gx_texture.rs:decode_cmpr.
"""
import struct
from PIL import Image

def selected(prefix,name):
    # Every driver expression path (race, menu, award, staff and LOD) uses
    # the same source faces. Alpha-zero CMPR RGB is visible to opaque TEV.
    return '_eye' in name.lower() or '_face' in name.lower() or '_mouth' in name.lower() or name=='funky_all'


def needs_exact(raw,width,height):
    # GX's transparent entry is only present in the endpoint-ordered mode.
    # Keep ordinary opaque textures compressed; they have no hidden RGB to lose.
    for at in range(0,len(raw),8):
        a,b,bits=struct.unpack_from('>HHI',raw,at)
        if a<=b and (a or b) and any((bits>>(2*i))&3==3 for i in range(16)):
            return True
    return False


def decode(raw,width,height):
    out=Image.new('RGBA',(width,height));px=out.load();at=0
    def rgb(n):
        r=(n>>11)&31;g=(n>>5)&63;b=n&31
        return ((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))
    for by in range(0,height,8):
        for bx in range(0,width,8):
            for sy,sx in ((0,0),(0,4),(4,0),(4,4)):
                a,b,bits=struct.unpack_from('>HHI',raw,at);at+=8
                c0,c1=rgb(a),rgb(b)
                if a>b:
                    c2=tuple((5*x+3*y)>>3 for x,y in zip(c0,c1))+(255,)
                    c3=tuple((3*x+5*y)>>3 for x,y in zip(c0,c1))+(255,)
                else:
                    mid=tuple((x+y)>>1 for x,y in zip(c0,c1));c2=mid+(255,);c3=mid+(0,)
                colors=(c0+(255,),c1+(255,),c2,c3)
                for y in range(4):
                    for x in range(4):
                        xx,yy=bx+sx+x,by+sy+y
                        if xx<width and yy<height:px[xx,yy]=colors[(bits>>(30-2*(y*4+x)))&3]
    return out


def preserve_rgba(path,im):
    """Tex3DS discards RGB at alpha0 in its PNG loader; restore exact GPU tiles."""
    data=path.read_bytes();count=struct.unpack_from('<H',data)[0];start=5+count*12+4
    width=1<<((data[2]&7)+3);height=1<<(((data[2]>>3)&7)+3)
    if data[3]!=0 or data[4]!=0 or (width,height)!=im.size:raise ValueError('Expected one-level RGBA8 award face')
    def tile(x,y):return ((y>>3)*(width>>3)+(x>>3))*64+sum(((x>>j)&1)<<(2*j)|((y>>j)&1)<<(2*j+1)for j in range(3))
    raw=data[start:];px=im.load()
    def score(flip):return sum(tuple(raw[tile(x,y)*4:tile(x,y)*4+4][::-1])==px[x,height-1-y if flip else y]for y in range(height)for x in range(width)if px[x,height-1-y if flip else y][3])
    flip=score(True)>score(False);raw=bytearray(width*height*4)
    for y in range(height):
        for x in range(width):raw[tile(x,y)*4:tile(x,y)*4+4]=bytes(px[x,height-1-y if flip else y][::-1])
    path.write_bytes(data[:start]+raw)
