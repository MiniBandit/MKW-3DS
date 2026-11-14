#include "mkw/lens_flare.hpp"
#include <game/system/CourseMap.hh>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace mkw {
namespace {
namespace K=Kinoko;
// The element table after "lensflare.brres" (USA 0x8088c81c, 21 x 12 bytes):
// texture index (-1: no element), scale, colour. The texture index is the
// lensflare.brres order (rk_lens01..04) and its GX map (0x8088c7fc).
struct Element {int texture;float scale;uint8_t r,g,b;};
constexpr Element Elements[21]={
    {1,5.0f,0x23,0x46,0xaf},{-1,0,0,0,0},{3,.35f,0x64,0x64,0xc8},{3,.5f,0x4b,0xaa,0x4b},
    {0,1.0f,0x7d,0x7d,0x32},{0,.75f,0x3c,0x1e,0x1e},{-1,0,0,0,0},{-1,0,0,0,0},
    {1,.175f,0xf5,0xa0,0xff},{0,1.685f,0x7d,0x1e,0x00},{3,.4f,0x7d,0x4b,0x4b},{3,.5f,0xaa,0x7d,0x4b},
    {3,.785f,0x64,0x96,0xc3},{0,1.135f,0x00,0x32,0x7d},{1,.15f,0xb9,0xc8,0xff},{1,1.9f,0x96,0x28,0x28},
    {0,2.5f,0x2d,0x8c,0x91},{0,1.0f,0x1e,0x64,0xaf},{-1,0,0,0,0},{-1,0,0,0,0},
    {2,5.0f,0xff,0xff,0xff},
};
constexpr unsigned LensFxObject=3;   // ObjFlow id of lensFX
constexpr int FadeMax=40,FadeIn=2,FadeOut=4;   // flare+36: +2 a frame to 40, -4 (80546278)
// Retail lays the flare out in its 2D space (TV mode 1: 832x480, 0x8029fb80);
// the top screen is the same height at half the scale.
constexpr float Scale=240.f/480.f;
constexpr float ScreenW=400.f,ScreenH=240.f;
}

void LensFlare::reset(){
    hasSource_=false;haveScreen_=false;fade_=0;
    if(const auto* map=K::System::CourseMap::Instance())
        for(unsigned i=0;i<map->getGeoObjCount();++i){
            const auto* obj=map->getGeoObj(i);
            if(!obj||obj->id()!=LensFxObject||!(obj->presenceFlag()&1))continue;
            source_={obj->pos().x,obj->pos().y,obj->pos().z};hasSource_=true;break;
        }
    if(hasSource_&&!loaded_){
        loaded_=true;
        for(unsigned i=0;i<textures_.size();++i){
            char path[40];std::snprintf(path,sizeof path,"textures/lensflare_%02u.t3x",i);
            // 64x64: never halved by Texture Quality.
            if(!textures_[i].load(path,0))std::printf("Missing texture: %s\n",path);
        }
    }
}

void LensFlare::update(Renderer& renderer,unsigned physicsSteps){
    if(!hasSource_)return;
    const int steps=int(std::min(physicsSteps-lastSteps_,8u));lastSteps_=physicsSteps;
    // 8054611c: the source must project onto the screen; 805472e4/80545c84:
    // it is hidden when all nine depth samples around it are nearer than it.
    // Our sky writes no depth (cleared to 0), so a covered sample is any
    // non-zero depth: the source is farther than every course surface.
    float x,y,z;
    const bool onScreen=renderer.projectTop(source_,x,y,z)&&z<0&&x>=0&&x<ScreenW&&y>=0&&y<ScreenH;
    const bool covered=renderer.depthProbeCovered()>=9;
    if(onScreen){x_=x;y_=y;haveScreen_=true;renderer.requestDepthProbe(x,y,-z);}
    fade_=onScreen&&!covered?std::min(FadeMax,fade_+FadeIn*steps):std::max(0,fade_-FadeOut*steps);
}

void LensFlare::draw(Renderer& renderer) const{
    if(!hasSource_||!fade_||!haveScreen_)return;
    // 805465b8: n = normalize(centre - source); element i sits 15*i/|n.y|
    // retail pixels along n (15 px a step vertically), turned to face along n.
    float nx=ScreenW*.5f-x_,ny=ScreenH*.5f-y_;
    const float length=std::hypot(nx,ny);
    if(length>1e-4f){nx/=length;ny/=length;}else{nx=0;ny=1;}
    const float step=15.f*Scale/std::max(std::fabs(ny),1e-3f);
    // Up to 8 quads an element (the ring); copied when drawn.
    static std::array<std::array<Renderer::MaterialQuad,21*3>,4> quads;std::array<unsigned,4> counts{};
    struct Point {float x,y,u,v;};
    for(unsigned i=0;i<21;++i){
        const auto& e=Elements[i];
        if(e.texture<0||!textures_[unsigned(e.texture)].ready)continue;
        const unsigned t=unsigned(e.texture);
        const float px=x_+nx*step*float(i),py=y_+ny*step*float(i);
        // 805455dc: size = scale x texture size (64), here at half scale.
        const float half=32.f*e.scale*Scale;
        if(px+half*1.5f<0||px-half*1.5f>ScreenW||py+half*1.5f<0||py-half*1.5f>ScreenH)continue;
        // I8 elements: table colour, alpha = texel x fade; rk_lens03: its own colours, alpha = fade.
        const uint32_t rgba=t==2?0xffffff00u|uint32_t(fade_):
            uint32_t(e.r)<<24|uint32_t(e.g)<<16|uint32_t(e.b)<<8|uint32_t(fade_);
        // a, b: texels from the element's centre (64 = its edge). 80545a74:
        // local x runs along (n.y, -n.x), local y along n.
        const auto at=[&](float a,float b){
            const float lx=a*half/64.f,ly=b*half/64.f;
            return Point{ny*lx+nx*ly+px,-nx*lx+ny*ly+py,1+a/64.f,1+b/64.f};
        };
        const auto emit=[&](const Point& p0,const Point& p1,const Point& p2,const Point& p3){
            auto& q=quads[t][counts[t]++];const Point* p[4]={&p0,&p1,&p2,&p3};
            for(unsigned k=0;k<4;++k){q.x[k]=p[k]->x;q.y[k]=p[k]->y;q.u[k]=p[k]->u;q.v[k]=p[k]->v;q.color[k]=rgba;}
        };
        if(!trim){emit(at(-64,-64),at(64,-64),at(-64,64),at(64,64));continue;}
        // The cooked textures (rk_lens01..04, one quadrant each) are zero
        // beyond (dx+dy)/sqrt2 = 66.47, 67.18, 64.35, 55.15 texels from the
        // centre, and rk_lens03 inside r = 40.70. Bilinear filtering reads
        // texels less than 1 away on each axis: the corners are cut sqrt2
        // further out, at D (Cut = D*sqrt2 - 64 is where the cut meets the
        // edge), and the ring's hole is sqrt2 smaller. Additive texels of 0 add nothing.
        constexpr float Cut[4]={32.1f,33.1f,29.1f,16.1f};   // D = 67.9, 68.6, 65.8, 56.6
        constexpr float Hole=39.2f;
        const float c=Cut[t];
        const float oa[8]={64,64,c,-c,-64,-64,-c,c},ob[8]={-c,c,64,64,c,-c,-64,-64};
        Point o[8];for(unsigned k=0;k<8;++k)o[k]=at(oa[k],ob[k]);
        if(t==2){
            // Ring: 8 quads between the cut square and an octagon inside the hole.
            Point in[8];
            for(unsigned k=0;k<8;++k){const float r=Hole/std::hypot(oa[k],ob[k]);in[k]=at(oa[k]*r,ob[k]*r);}
            for(unsigned k=0;k<8;++k){const unsigned n=(k+1)&7;emit(in[k],in[n],o[k],o[n]);}
        }else{
            // Corner-cut square: bottom trapezoid, middle band, top trapezoid.
            emit(o[6],o[7],o[5],o[0]);emit(o[5],o[0],o[4],o[1]);emit(o[4],o[1],o[3],o[2]);
        }
    }
    // Retail TEV (80547110, 805458a0): additive (SRCALPHA, ONE); textures are
    // one quadrant of the element, mirrored over texture coordinates 0..2.
    for(unsigned t=0;t<4;++t)if(counts[t]){
        const bool own=t==2;
        renderer.drawScreenMaterialQuads(&textures_[t],quads[t].data(),counts[t],own?0x000000ffu:0xffffff00u,0xffffffffu,
                                         GPU_MIRRORED_REPEAT,GPU_MIRRORED_REPEAT,true);
    }
}
}
