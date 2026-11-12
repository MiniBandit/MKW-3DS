// NW4R EF draw strategies as linked into Mario Kart Wii (RMCE main.dol), producing
// view-space geometry for src/renderer.cpp:
//   DrawStrategyImpl: InitTev 0x8003c400, InitColor 0x8003c6f0, SetupGP 0x8003cac0,
//     _SetupTevReg 0x8003cf60, texture matrices 0x8003d790, draw order 0x8003de00,
//     AheadContext 0x8003df20, CalcAhead 0x80040060-0x80040340 / 0x80042aa0 /
//     0x800469c0 / 0x80046ae0;
//   billboard 0x8003e290 (normal), 0x8003f680 (directional);
//   directional 0x80040a90 (emitter space), 0x80042310 (view space);
//   stripe 0x80045f30 / 0x80044270 / 0x800445f0, tube 0x80045d40 / 0x80044aa0 / 0x80044fa0.
// The view matrix carries MKW's per-emitter z offset (DrawInfo +0x88, ParticleManager::Draw).
#include "mkw/nw4r_ef_draw.hpp"
#include <cmath>
#include <cstring>

namespace mkw::ef {
namespace {
constexpr float Eps=1.1920928955078125e-07f;
constexpr float FIdxToRad=0.02454369328916073f;
constexpr Vec3 XUnit{1,0,0},YUnit{0,1,0},ZUnit{0,0,1};

// ---- TEV registers (_SetupTevReg) ----
struct Registers { u8 c[3][4]{}; u8 k[4][4]{}; };
u8 fadedAlpha(const Particle& p,u8 a){
    u32 v=a;
    const u32 counter=p.collisionCounter,limit=p.manager->collisionFadeAlpha;
    if(counter!=0xff&&counter<limit)v=(v*counter)/limit&0xff;
    return u8(v);
}
Registers tevRegisters(const Particle& p,const DrawSetting& d){
    const u8* in=d.colorInput;const u8* ain=d.alphaInput;
    bool anyAlpha=false;
    for(int i=1;i<8;++i)anyAlpha|=ain[i]!=0;
    const u32 flick=anyAlpha&&d.alphaFlickType?flickAlpha(p):0xff;
    // Per layer, primary and secondary RGBA: alpha after the collision fade and flicker,
    // then the manager's colour modifier (calls at 0x8003d210 / 0x8003d254).
    u8 color[2][2][4];
    for(int l=0;l<2;++l){
        for(int i=0;i<2;++i){
            u32 a=fadedAlpha(p,p.p.color[l][i][3]);
            if((flick&0xff)!=0xff)a=(a*(flick&0xff)+0x80)>>8;
            for(int c=0;c<3;++c)color[l][i][c]=p.p.color[l][i][c];
            color[l][i][3]=u8(a);
        }
        modifyColor(*p.manager,color[l][0],color[l][1]);
    }
    u8 alpha[2][2];
    for(int l=0;l<2;++l)for(int i=0;i<2;++i)alpha[l][i]=color[l][i][3];
    auto rgb=[&](u8 sel,u8* out){
        const u8* pri0=color[0][0];const u8* sec0=color[0][1];const u8* pri1=color[1][0];const u8* sec1=color[1][1];
        for(int c=0;c<3;++c){
            switch(sel){
            case 1:out[c]=pri0[c];break;
            case 2:out[c]=sec0[c];break;
            case 5:out[c]=u8((u32(pri0[c])*sec0[c]+0x80)>>8);break;
            case 3:out[c]=pri1[c];break;
            case 4:out[c]=sec1[c];break;
            case 6:out[c]=u8((u32(pri1[c])*sec1[c]+0x80)>>8);break;
            default:out[c]=0;break;
            }
        }
    };
    auto alphaOf=[&](u8 sel)->u8{
        switch(sel){
        case 1:return alpha[0][0];
        case 2:return alpha[0][1];
        case 5:return u8((u32(alpha[0][0])*alpha[0][1]+0x80)>>8);
        case 3:return alpha[1][0];
        case 4:return alpha[1][1];
        case 6:return u8((u32(alpha[1][0])*alpha[1][1]+0x80)>>8);
        default:return 0;
        }
    };
    Registers r;
    for(int i=0;i<3;++i)if(in[1+i]||ain[1+i]){rgb(in[1+i],r.c[i]);r.c[i][3]=alphaOf(ain[1+i]);}
    for(int i=0;i<4;++i)if(in[4+i]||ain[4+i]){rgb(in[4+i],r.k[i]);r.k[i][3]=alphaOf(ain[4+i]);}
    return r;
}
void channelValue(const Registers& r,const Channel& ch,u8 out[4]){
    auto reg=[&](u8 v)->const u8*{
        static const u8 zero[4]{},quarter[4]{64,64,64,64},half[4]{128,128,128,128},one[4]{255,255,255,255};
        switch(v){
        case RegC0:case RegC1:case RegC2:return r.c[v-RegC0];
        case RegK0:case RegK1:case RegK2:case RegK3:return r.k[v-RegK0];
        case ValQuarter:return quarter;
        case ValHalf:return half;
        case ValOne:return one;
        default:return zero;
        }
    };
    const u8* rgb=reg(ch.rgb);
    if(ch.rgbFromAlpha)out[0]=out[1]=out[2]=rgb[3];
    else{out[0]=rgb[0];out[1]=rgb[1];out[2]=rgb[2];}
    out[3]=reg(ch.alpha)[3];
}

// ---- texture matrices (0x8003d790) ----
struct TexMtx { float m[2][3]; };
TexMtx textureMatrix(const Particle& p,int layer){
    const u32 wrapS=(p.p.textureWrap>>(layer*4))&3,wrapT=(p.p.textureWrap>>(layer*4+2))&3;
    const u32 rev=(p.p.textureReverse>>(layer*2))&3;
    int scaleS=wrapS==2?2:1;if(rev==1||rev==3)scaleS=-scaleS;
    int scaleT=wrapT==2?2:1;if(u8(rev-2)<2)scaleT=-scaleT;
    int transS=(rev==1||rev==3)?1:0;if(wrapS==2)transS<<=1;
    int transT=u8(rev-2)<2?1:0;if(wrapT==2)transT<<=1;
    TexMtx m{{{1,0,p.p.textureTranslate[layer][0]},{0,1,p.p.textureTranslate[layer][1]}}};
    auto apply=[&](const float a[2][3]){
        TexMtx r;
        for(int i=0;i<2;++i){
            for(int j=0;j<3;++j)r.m[i][j]=a[i][0]*m.m[0][j]+a[i][1]*m.m[1][j];
            r.m[i][2]+=a[i][2];
        }
        m=r;
    };
    const float rot=p.p.textureRotate[layer];
    if(rot!=0.f){
        float s,c;sinCosRad(rot,s,c);
        const float r[2][3]={{c,-s,0.5f*((1.f-c)+s)},{s,c,0.5f*((1.f-c)-s)}};
        apply(r);
    }
    const float tsx=p.p.textureScale[layer][0],tsy=p.p.textureScale[layer][1];
    const float fs=float(scaleS),ft=float(scaleT);
    if(fs!=1.f||ft!=1.f||tsx!=1.f||tsy!=1.f||transS!=0||transT!=0){
        const float sx=fs*tsx,sy=ft*tsy;
        const float a[2][3]={{sx,0,float(transS)+(0.5f*fs-0.5f*sx)},{0,sy,float(transT)+(0.5f*ft-0.5f*sy)}};
        apply(a);
    }
    return m;
}

// ---- AheadContext and CalcAhead ----
struct Ahead {
    ParticleManager* pm;
    Mtx34 emitter,manager,managerInv;
    Vec3 axisY,center,worldY;
    Vec3 defaultSide,cameraDir; // stripes
};
Ahead aheadContext(ParticleManager& pm){
    Ahead a;a.pm=&pm;
    a.emitter=emitterGlobalMtx(*pm.emitter);
    a.manager=managerGlobalMtx(pm);
    a.managerInv=Mtx34::identity();inverse(a.managerInv,a.manager);
    Vec3 y=transformNormal(a.managerInv,a.emitter.col(1));
    if(dot(y,y)>=Eps){normalize(y);}else y=YUnit;
    a.axisY=y;
    a.center=transformCoord(a.managerInv,a.emitter.translate());
    a.worldY=YUnit;
    if(pm.res->desc.draw.typeDir==5){
        Vec3 w=a.managerInv.col(1);
        if(dot(w,w)>=Eps)normalize(w);else w=YUnit;
        a.worldY=w;
    }
    a.defaultSide=transformNormal(a.managerInv,a.emitter.col(0));
    a.cameraDir=ZUnit;
    return a;
}
Vec3 normalizedOr(Vec3 v,Vec3 fallback){if(dot(v,v)>=Eps){normalize(v);return v;}return fallback;}
Particle* activeBefore(Particle* p){for(p=p->prev;p&&p->life!=Active;p=p->prev){}return p;}
Particle* activeAfter(Particle* p){for(p=p->next;p&&p->life!=Active;p=p->next){}return p;}
Vec3 particleBoth(const Ahead& a,Particle& p){
    Vec3 u{},v{};
    if(Particle* o=activeBefore(&p)){u=o->p.position-p.p.position;if(dot(u,u)>=Eps)normalize(u);}
    if(Particle* o=activeAfter(&p)){v=o->p.position-p.p.position;if(dot(v,v)>=Eps)normalize(v);}
    return normalizedOr(u-v,a.axisY);
}
// typeDir for billboards and directionals (0x8003ffe0 / 0x80042a10); stripes (0x800468f0).
Vec3 calcAhead(const Ahead& a,Particle& p,bool stripe){
    switch(a.pm->res->desc.draw.typeDir){
    case 1:return normalizedOr(p.p.position-a.center,a.axisY);
    case 2:return a.axisY;
    case 3:
        if(stripe){Particle* o=activeAfter(&p);return normalizedOr(o?p.p.position-o->p.position:p.p.position-a.center,a.axisY);}
        else{Particle* o=activeBefore(&p);return normalizedOr(o?o->p.position-p.p.position:p.p.position-a.center,a.axisY);}
    case 4:return stripe?normalizedOr(p.p.position-p.p.prevPosition,a.axisY):particleBoth(a,p);
    case 5:return stripe||a.pm->res->desc.draw.ptclType==4?a.worldY:normalizedOr(p.p.position-p.p.prevPosition,a.axisY);
    case 6:return particleBoth(a,p);
    default:return normalizedOr(p.p.position-p.p.prevPosition,a.axisY);
    }
}

// ---- sizes and rotation shared by the strategies ----
u32 fadePercent(const Particle& p,u8 limit){
    const ParticleManager& pm=*p.manager;
    return u32(pm.collisionFadeStart+int(100-pm.collisionFadeStart)*int(p.collisionCounter)/int(limit));
}
float sizeX(const Particle& p){
    float s=p.p.size[0]*p.p.scale[0];
    const u32 counter=p.collisionCounter;
    if(counter!=0xff&&counter<p.manager->collisionFadeScale)s=s*(0.01f*float(fadePercent(p,p.manager->collisionFadeScale)));
    return s*p.manager->modifierScale[0];
}
float sizeY(const Particle& p){
    const ParticleManager& pm=*p.manager;
    float s;
    switch(pm.res->desc.draw.flags&(DrawSetting::XYSameSize|DrawSetting::XYSameScale)){
    case DrawSetting::XYSameScale:s=p.p.size[1]*p.p.scale[0];break;
    case DrawSetting::XYSameSize:s=p.p.size[0]*p.p.scale[1];break;
    case DrawSetting::XYSameSize|DrawSetting::XYSameScale:s=p.p.size[0]*p.p.scale[0];break;
    default:s=p.p.size[1]*p.p.scale[1];break;
    }
    const u32 counter=p.collisionCounter;
    if(counter!=0xff&&counter<pm.collisionFadeScale)s=s*(0.01f*float(fadePercent(p,pm.collisionFadeScale)));
    return s*pm.modifierScale[1];
}
Vec3 drawRotate(const Particle& p){
    Vec3 r{p.p.rotate[0],p.p.rotate[1],p.p.rotate[2]};
    if(p.p.rotateOffset[0])r.x=r.x+FIdxToRad*float(p.p.rotateOffset[0]);
    if(p.p.rotateOffset[1])r.y=r.y+FIdxToRad*float(p.p.rotateOffset[1]);
    if(p.p.rotateOffset[2])r.z=r.z+FIdxToRad*float(p.p.rotateOffset[2]);
    const ParticleManager& pm=*p.manager;
    return {r.x+pm.modifierRotate.x,r.y+pm.modifierRotate.y,r.z+pm.modifierRotate.z};
}
float colLength(const Mtx34& m,int c){return std::sqrt(m.m[2][c]*m.m[2][c]+m.m[0][c]*m.m[0][c]+m.m[1][c]*m.m[1][c]);}
// Rotation by typeAxis (0x80041d10): X, Y, Z or XYZ, angles negated.
Mtx34 axisRotation(Vec3 rot,u8 axis){
    float sx=0,cx=1,sy=0,cy=1,sz=0,cz=1;
    Mtx34 r=Mtx34::identity();
    switch(axis){
    case 0:if(rot.x!=0.f)sinCosRad(-rot.x,sx,cx);r.m[1][1]=cx;r.m[1][2]=-sx;r.m[2][1]=sx;r.m[2][2]=cx;break;
    case 1:if(rot.y!=0.f)sinCosRad(-rot.y,sy,cy);r.m[0][0]=cy;r.m[0][2]=sy;r.m[2][0]=-sy;r.m[2][2]=cy;break;
    case 2:if(rot.z!=0.f)sinCosRad(-rot.z,sz,cz);r.m[0][0]=cz;r.m[0][1]=-sz;r.m[1][0]=sz;r.m[1][1]=cz;break;
    default:
        if(rot.x!=0.f)sinCosRad(-rot.x,sx,cx);
        if(rot.y!=0.f)sinCosRad(-rot.y,sy,cy);
        if(rot.z!=0.f)sinCosRad(-rot.z,sz,cz);
        r.m[0][0]=cy*cz;r.m[0][1]=sx*sy*cz-cx*sz;r.m[0][2]=cx*sy*cz+sx*sz;
        r.m[1][0]=cy*sz;r.m[1][1]=sx*sy*sz+cx*cz;r.m[1][2]=cx*sy*sz-sx*cz;
        r.m[2][0]=-sy;r.m[2][1]=cy*sx;r.m[2][2]=cx*cy;
        break;
    }
    return r;
}
// Local quad matrix of a directional particle (inline in 0x80040a90, 0x800420d0).
Mtx34 directionalLocal(const Mtx34& R,float px,float py,float sx,float sy,float stretch,bool option1){
    Mtx34 L;
    const float sxpx=sx*px;
    if(!option1){
        const float k=sy*((py+stretch)-1.f),ys=sy*stretch;
        L.m[0][0]=R.m[0][0]*sx;L.m[0][1]=R.m[0][1]*ys;L.m[0][2]=R.m[0][2]*sx;L.m[0][3]=px+(-R.m[0][0]*sxpx-R.m[0][1]*k);
        L.m[1][0]=R.m[1][0]*sx;L.m[1][1]=R.m[1][1]*ys;L.m[1][2]=R.m[1][2]*sx;L.m[1][3]=py+(-R.m[1][0]*sxpx-R.m[1][1]*k);
        L.m[2][0]=R.m[2][0]*sx;L.m[2][1]=R.m[2][1]*ys;L.m[2][2]=R.m[2][2]*sx;L.m[2][3]=-R.m[2][0]*sxpx-R.m[2][1]*k;
    }else{
        const float sypy=sy*py;
        L.m[0][0]=R.m[0][0]*sx;L.m[0][1]=R.m[0][2]*sy;L.m[0][2]=R.m[0][1]*sx;L.m[0][3]=px+(-R.m[0][0]*sxpx-R.m[0][2]*sypy);
        L.m[1][0]=R.m[1][0]*sx;L.m[1][1]=R.m[1][2]*sy;L.m[1][2]=R.m[1][1]*sx;L.m[1][3]=-R.m[1][0]*sxpx-R.m[1][2]*sypy;
        L.m[2][0]=R.m[2][0]*sx;L.m[2][1]=R.m[2][2]*sy;L.m[2][2]=R.m[2][1]*sx;L.m[2][3]=(-R.m[2][0]*sxpx-R.m[2][2]*sypy)-py;
    }
    return L;
}

// ---- output ----
struct Writer {
    DrawList& out;
    const std::vector<Material>& materials;
    const TextureSize* sizes;u32 sizeCount;
    u32 vertexLimit;
    const mkw::ClipFrustum* frustum=nullptr;
    const EmitterResource* res=nullptr;
    const Material* mat=nullptr;
    u8 cull=0;
    // Per particle state set by setup().
    TexMtx tex[2]{};
    float textureScale[2][2]{};
    u32 texture[2]{};
    u8 wrap[2]{};
    u8 vertex[4]{};
    float lookupS=0,lookupT=0;
    u8 constant[6][4]{};
    u8 ref0=0,ref1=0;
    bool projected[2]{};
    // GX SetupGP for one particle: registers, texture objects and matrices.
    void setup(const Particle& p){
        if(out.vertices.size()>=vertexLimit)return;
        const DrawSetting& d=res->desc.draw;
        const Registers r=tevRegisters(p,d);
        channelValue(r,mat->vertex,vertex);
        u8 look[4];channelValue(r,mat->lookup,look);
        std::memset(constant,0,sizeof constant);
        for(u32 i=0;i<mat->stages;++i)if(mat->stage[i].constant.rgb!=ValNone||mat->stage[i].constant.alpha!=ValNone)channelValue(r,mat->stage[i].constant,constant[i]);
        const u32 index=u32(out.lookup.size());
        if(index<DrawList::LookupTexels){
            out.lookup.push_back(u32(look[0])|u32(look[1])<<8|u32(look[2])<<16|u32(look[3])<<24);
            // Texel index -> 8x8 Morton tile position; rows run from the top of the image.
            const u32 tile=index/64,m=index%64;
            u32 x=0,y=0;
            for(int b=0;b<3;++b){x|=((m>>(2*b))&1)<<b;y|=((m>>(2*b+1))&1)<<b;}
            x+=(tile%(DrawList::LookupSize/8))*8;y+=(tile/(DrawList::LookupSize/8))*8;
            lookupS=(float(x)+0.5f)/float(DrawList::LookupSize);
            lookupT=1.f-(float(y)+0.5f)/float(DrawList::LookupSize);
        }
        for(int u=0;u<2;++u){
            const bool enabled=(d.flags&(u==0?DrawSetting::Tex1:DrawSetting::Tex2))!=0;
            texture[u]=enabled?p.p.texture[u]:0xffffffffu;
            wrap[u]=u8((p.p.textureWrap>>(u*4))&0xf);
            if(enabled)tex[u]=textureMatrix(p,u);
            if(texture[u]<sizeCount){
                const TextureSize& z=sizes[texture[u]];
                textureScale[u][0]=float(z.width)/float(z.paddedWidth);
                textureScale[u][1]=float(z.height)/float(z.paddedHeight);
            }
            projected[u]=enabled&&(d.flags&(u==0?DrawSetting::Tex1Proj:DrawSetting::Tex2Proj));
        }
        ref0=p.p.acmpRef0;ref1=p.p.acmpRef1;
        const int l0=mat->swapUnits?1:0,l1=1-l0; // GX layer on PICA units 0 and 1
        DrawBatch* b=out.batches.empty()?nullptr:&out.batches.back();
        if(b&&b->res==res&&b->cull==cull&&b->texture[0]==texture[l0]&&b->texture[1]==texture[l1]&&b->wrap[0]==wrap[l0]&&
           b->wrap[1]==wrap[l1]&&b->alphaRef0==ref0&&b->alphaRef1==ref1&&!std::memcmp(b->constant,constant,sizeof constant))return;
        DrawBatch nb{};
        nb.res=res;nb.first=u32(out.vertices.size());nb.count=0;
        nb.texture[0]=texture[l0];nb.texture[1]=texture[l1];nb.wrap[0]=wrap[l0];nb.wrap[1]=wrap[l1];
        std::memcpy(nb.constant,constant,sizeof constant);
        nb.cull=cull;nb.alphaRef0=ref0;nb.alphaRef1=ref1;
        out.batches.push_back(nb);
    }
    // Texture coordinate of GX layer u in PICA form (t from the top, padded sizes) with a
    // projective q. A projected layer is GX_TG_POS through PNMTX0 (the eye position) and
    // the post matrix DrawInfo projection x texture matrix (0x8003dd20); MKW's effect draws
    // (0x80678aec, 0x8067926c, 0x8067966c) leave that projection identity, so
    // (s, t, q) = (texture matrix rows 0 and 1 on (x, y, 1), eye z), divided per pixel.
    void uv(int u,float s,float t,Vec3 eye,float& os,float& ot,float& oq) const {
        oq=1.f;
        if(texture[u]==0xffffffffu||texture[u]>=sizeCount){os=s;ot=t;return;}
        const TexMtx& m=tex[u];
        const float sx=textureScale[u][0],sy=textureScale[u][1];
        if(projected[u]){
            const float ps=m.m[0][0]*eye.x+m.m[0][1]*eye.y+m.m[0][2],pt=m.m[1][0]*eye.x+m.m[1][1]*eye.y+m.m[1][2];
            // (s/q * sx, 1 - t/q * sy) homogeneously, negated so q > 0 in front of the camera.
            os=-ps*sx;ot=-(eye.z-pt*sy);oq=-eye.z;
            return;
        }
        const float gs=m.m[0][0]*s+m.m[0][1]*t+m.m[0][2],gt=m.m[1][0]*s+m.m[1][1]*t+m.m[1][2];
        os=gs*sx;
        ot=1.f-gt*sy;
    }
    DrawVertex make(Vec3 v,float s,float t) const {
        DrawVertex dv;
        dv.x=v.x;dv.y=v.y;dv.z=v.z;
        const int l0=mat->swapUnits?1:0;
        float unused;
        uv(l0,s,t,v,dv.s0,dv.t0,dv.q0);uv(1-l0,s,t,v,dv.s1,dv.t1,unused);
        dv.s2=lookupS;dv.t2=lookupT;
        std::memcpy(dv.color,vertex,4);
        return dv;
    }
    void emit(const DrawVertex& v){
        if(out.vertices.size()>=vertexLimit)return;
        out.vertices.push_back(v);++out.batches.back().count;
    }
    bool visible(Vec3 a,Vec3 b,Vec3 c) const {
        if(!frustum)return true;
        mkw::Bounds bounds{{a.x,a.y,a.z},{a.x,a.y,a.z}};
        bounds.include({b.x,b.y,b.z});bounds.include({c.x,c.y,c.z});
        return frustum->intersects(bounds);
    }
    void triangle(const DrawVertex& a,const DrawVertex& b,const DrawVertex& c){
        if(!visible({a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z}))return;
        emit(a);emit(b);emit(c);
    }
    // GX_TRIANGLESTRIP as a triangle list; odd triangles swap their first two vertices
    // so every triangle keeps the strip's winding.
    struct Strip {
        Writer& writer;
        DrawVertex prev[2]{};
        u32 count=0;
        void push(const DrawVertex& v){
            if(count<2){prev[count++]=v;return;}
            if(count&1)writer.triangle(prev[1],prev[0],v);
            else writer.triangle(prev[0],prev[1],v);
            prev[0]=prev[1];prev[1]=v;++count;
        }
    };
    // GX_QUADS: (v0,v1,v2) and (v0,v2,v3); corners carry texcoords (0,1),(0,0),(1,0),(1,1).
    void emitQuad(Vec3 a,Vec3 b,Vec3 c,Vec3 d,bool first,bool second){
        const DrawVertex va=make(a,0,1),vb=make(b,0,0),vc=make(c,1,0),vd=make(d,1,1);
        if(first){emit(va);emit(vb);emit(vc);}
        if(second){emit(va);emit(vc);emit(vd);}
    }
    void quad(Vec3 a,Vec3 b,Vec3 c,Vec3 d){
        const bool first=visible(a,b,c),second=visible(a,c,d);
        if(first||second)emitQuad(a,b,c,d,first,second);
    }
    // Billboard geometry is known before material setup. Fully clipped
    // particles need no TEV register/UV work, lookup texel or empty batch.
    // Their simulation and axis history continue unchanged.
    void quad(const Particle& p,Vec3 a,Vec3 b,Vec3 c,Vec3 d){
        const bool first=visible(a,b,c),second=visible(a,c,d);
        if(!first&&!second)return;
        setup(p);emitQuad(a,b,c,d,first,second);
    }
};

Mtx34 viewWithZOffset(const Mtx34& view,const ParticleManager& pm){
    Mtx34 v=view;
    const float z=pm.res->desc.draw.zOffset;
    if(z==0.f)return v;
    // Perspective projection: along the eye ray through the emitter.
    Vec3 pos=transformCoord(view,emitterGlobalMtx(*pm.emitter).translate());
    if(!normalize(pos)){v.m[2][3]+=z;return v;}
    if(pos.z<0.f){v.m[0][3]-=pos.x*z;v.m[1][3]-=pos.y*z;v.m[2][3]-=pos.z*z;}
    else{v.m[0][3]+=pos.x*z;v.m[1][3]+=pos.y*z;v.m[2][3]+=pos.z*z;}
    return v;
}
template<class F> void forEachDraw(ParticleManager& pm,F f){
    const bool elders=pm.res->desc.draw.flags&DrawSetting::DrawOrder;
    if(elders){for(Particle* p=pm.particles.head;p;p=p->next)if(p->life==Active)f(*p);}
    else{for(Particle* p=pm.particles.tail;p;p=p->prev)if(p->life==Active)f(*p);}
}

// ---- billboard ----
void drawBillboard(Writer& w,ParticleManager& pm,const Mtx34& view){
    const DrawSetting& d=pm.res->desc.draw;
    const float px=float(d.pivotX)/100.f,py=float(d.pivotY)/100.f;
    const Mtx34 m=mul(view,managerGlobalMtx(pm));
    const float vx=colLength(m,0),vy=colLength(m,1);
    if(d.typeOption==2){
        // Directional billboard: rolled to the projected ahead direction, stretched by speed.
        const Ahead a=aheadContext(pm);
        forEachDraw(pm,[&](Particle& p){
            const float sx=sizeX(p);if(sx<Eps)return;
            const float sy=sizeY(p);if(sy<Eps)return;
            Vec3 ahead=transformNormal(m,calcAhead(a,p,false));
            const float len2=ahead.x*ahead.x+ahead.y*ahead.y;
            float rs=0.f,rc=1.f;
            if(len2>0.f){const float len=std::sqrt(len2);rs=-ahead.x*(1.f/len);rc=ahead.y*(1.f/len);}
            float stretch=1.f;
            if(d.typeOption0){
                const Vec3 mv=p.p.position-p.p.prevPosition;
                const float l=dot(mv,mv)>0.f?std::sqrt(dot(mv,mv)):0.f;
                stretch=stretch+(0.5f*l)/sy;
            }
            const Vec3 pos=transformCoord(m,p.p.position);
            const float e0=px-sx*px,e1=sy*((stretch+py)-1.f)-py;
            const float A=vx*rc,B=vy*rs,C=vx*rs,D=vy*rc;
            const Vec3 c{pos.x+A*e0+B*e1,pos.y+(C*e0-D*e1),pos.z};
            const Vec3 d0{A*sx-stretch*B*sy,C*sx+stretch*D*sy,0.f};
            const Vec3 d1{A*sx+stretch*B*sy,C*sx-stretch*D*sy,0.f};
            w.quad(p,c-d0,c-d1,c+d0,c+d1);
        });
        return;
    }
    // Normal billboard; typeOption 3 keeps the camera roll out.
    float rc=1.f,rs=0.f;
    const float mag=std::sqrt(view.m[0][1]*view.m[0][1]+view.m[1][1]*view.m[1][1]);
    if(d.typeOption!=3&&mag!=0.f){rc=view.m[1][1]*(1.f/mag);rs=-view.m[0][1]*(1.f/mag);}
    forEachDraw(pm,[&](Particle& p){
        const float sx=sizeX(p);if(sx<Eps)return;
        const float sy=sizeY(p);if(sy<Eps)return;
        const Vec3 rot=drawRotate(p);
        const Vec3 pos=transformCoord(m,p.p.position);
        float sr=0.f,cr=1.f;
        if(rot.z!=0.f)sinCosRad(-rot.z,sr,cr);
        const float crsx=cr*sx,srsx=sr*sx,crsy=cr*sy,srsy=sr*sy;
        const float vxrc=vx*rc,vxrs=vx*rs,vyrc=vy*rc,vyrs=vy*rs;
        const float e0=(px-crsx*px)-srsy*py,e1=(py+srsx*px)-crsy*py;
        const Vec3 c{vxrc*e0+vyrs*e1+pos.x,vxrs*e0-vyrc*e1+pos.y,pos.z};
        const Vec3 d0{vxrc*crsx-vxrc*srsy-vyrs*srsx-vyrs*crsy,vxrs*crsx-vxrs*srsy+vyrc*srsx+vyrc*crsy,0.f};
        const Vec3 d1{vxrc*crsx+vxrc*srsy-vyrs*srsx+vyrs*crsy,vxrs*crsx+vxrs*srsy+vyrc*srsx-vyrc*crsy,0.f};
        w.quad(p,c-d0,c-d1,c+d0,c+d1);
    });
}

// ---- directional ----
void drawDirectional(Writer& w,ParticleManager& pm,const Mtx34& view){
    const DrawSetting& d=pm.res->desc.draw;
    const Ahead a=aheadContext(pm);
    const float px=float(d.pivotX)/100.f,py=float(d.pivotY)/100.f;
    const Mtx34 viewPm=mul(view,managerGlobalMtx(pm));
    if((d.typeOption2&3)==1){
        // View-space variant (0x80042310).
        const float lx=colLength(viewPm,0),ly=colLength(viewPm,1),lz=colLength(viewPm,2);
        for(Particle* p=pm.particles.tail;p&&1.f<p->prevAxis.x;p=p->prev)p->prevAxis={};
        forEachDraw(pm,[&](Particle& p){
            const float sx=sizeX(p);if(sx<Eps)return;
            const float sy=sizeY(p);if(sy<Eps)return;
            float stretch=1.f;
            if(d.typeOption0){const Vec3 mv=p.p.position-p.p.prevPosition;stretch=stretch+(0.5f*(dot(mv,mv)>0.f?std::sqrt(dot(mv,mv)):0.f))/sy;}
            w.setup(p);
            const Mtx34 L=directionalLocal(axisRotation(drawRotate(p),d.typeAxis),px,py,sx,sy,stretch,d.typeOption1!=0);
            Vec3 ahead=transformNormal(viewPm,calcAhead(a,p,false));
            Vec3 y=ahead;if(dot(y,y)>=Eps)normalize(y);
            Vec3 x{ahead.y,-ahead.x,0.f},z;
            if(dot(x,x)>=Eps){
                normalize(x);
                z={-ahead.x*ahead.z,-ahead.y*ahead.z,ahead.x*ahead.x+ahead.y*ahead.y};
            }else{
                x=cross(ahead,p.prevAxis);
                if(dot(x,x)>=Eps)normalize(x);else x=XUnit;
                z=cross(x,ahead);
            }
            if(dot(z,z)>=Eps)normalize(z);
            p.prevAxis=z;
            const Vec3 pos=transformCoord(viewPm,p.p.position);
            Mtx34 B;B.setCol(0,x*lx);B.setCol(1,y*ly);B.setCol(2,z*lz);B.setCol(3,pos);
            const Mtx34 M=mul(B,L);
            const Vec3 c=M.col(3),cx=M.col(0),cy=M.col(1),cz=M.col(2);
            const Vec3 e0=cx+cy,e1=cx-cy;
            w.quad(c-e0,c-e1,c+e0,c+e1);
            if(d.typeOption==1){const Vec3 f0=cy+cz,f1=cy-cz;w.quad(c-e0,c-f0,c+f1,c+f1);}
        });
        return;
    }
    // Emitter-space variant (0x80040a90): the frame follows the ahead direction and the
    // tracked axis, positions go through view x manager.
    const Mtx34 toPm=mul(a.managerInv,a.emitter);
    Vec3 sideDefault=toPm.col(2);if(dot(sideDefault,sideDefault)>=Eps)normalize(sideDefault);
    const Vec3 axisInit=toPm.col(1);
    for(Particle* p=pm.particles.tail;p&&1.f<p->prevAxis.x;p=p->prev)p->prevAxis=axisInit;
    static constexpr float Quad[4][3]={{-1,-1,0},{-1,1,0},{1,1,0},{1,-1,0}};
    static constexpr float Cross[4][3]={{0,-1,1},{0,1,1},{0,1,-1},{0,-1,-1}};
    forEachDraw(pm,[&](Particle& p){
        const float sx=sizeX(p);if(sx<Eps)return;
        const float sy=sizeY(p);if(sy<Eps)return;
        float stretch=1.f;
        if(d.typeOption0){const Vec3 mv=p.p.position-p.p.prevPosition;stretch=stretch+(0.5f*(dot(mv,mv)>0.f?std::sqrt(dot(mv,mv)):0.f))/sy;}
        w.setup(p);
        const Mtx34 L=directionalLocal(axisRotation(drawRotate(p),d.typeAxis),px,py,sx,sy,stretch,d.typeOption1!=0);
        const Vec3 ahead=calcAhead(a,p,false);
        Vec3 side=cross(p.prevAxis,ahead);
        if(dot(side,side)>=Eps)normalize(side);else side=sideDefault;
        const Vec3 axis=cross(ahead,side);
        p.prevAxis=axis;
        Mtx34 B;B.setCol(0,axis);B.setCol(1,ahead);B.setCol(2,side);B.setCol(3,p.p.position);
        const Mtx34 M=mul(viewPm,mul(B,L));
        auto emit=[&](const float (*q)[3]){
            Vec3 v[4];
            for(int i=0;i<4;++i)v[i]=transformCoord(M,{q[i][0],q[i][1],q[i][2]});
            w.quad(v[0],v[1],v[2],v[3]);
        };
        emit(Quad);
        if(d.typeOption==1)emit(Cross);
    });
}

// ---- stripe ----
Vec3 stripeInitialAxis(const Ahead& a,u8 option2){
    Vec3 v;
    switch(option2&0x38){
    case 0x08:v=transformNormal(a.managerInv,transformNormal(a.emitter,XUnit));break;
    case 0x10:v=transformNormal(a.managerInv,transformNormal(a.emitter,ZUnit));break;
    case 0x18:v=transformNormal(a.managerInv,transformNormal(a.emitter,{1,1,1}));break;
    default:return a.axisY;
    }
    return normalizedOr(v,a.axisY);
}
// One section of a stripe (0x800445f0) or tube (0x80044aa0): the frame along the strip.
struct Section { Vec3 center,a,b;float t; };
Mtx34 stripeFrame(Ahead& a,Particle& p,bool billboard,Vec3 pos){
    const Vec3 ahead=calcAhead(a,p,true);
    Vec3 side=cross(ahead,billboard?a.cameraDir:p.prevAxis);
    if(dot(side,side)>=Eps)normalize(side);else side=a.defaultSide;
    Vec3 axis=cross(side,ahead);
    if(dot(axis,axis)>=Eps)normalize(axis);
    p.prevAxis=axis;
    Mtx34 B;B.setCol(0,side);B.setCol(1,ahead);B.setCol(2,axis);B.setCol(3,pos);
    return B;
}
void drawStripe(Writer& w,ParticleManager& pm,const Mtx34& view){
    const DrawSetting& d=pm.res->desc.draw;
    u32 count=0;for(Particle* p=pm.particles.head;p;p=p->next)if(p->life==Active)++count;
    if(!count)return;
    Ahead a=aheadContext(pm);
    const Vec3 initial=stripeInitialAxis(a,d.typeOption2);
    for(Particle* p=pm.particles.tail;p&&1.f<p->prevAxis.x;p=p->prev)p->prevAxis=initial;
    const bool billboard=d.typeOption==2;
    const Mtx34 viewPm=mul(view,managerGlobalMtx(pm));
    if(billboard){
        Mtx34 inv;
        if(inverse(inv,viewPm)){a.cameraDir=inv.col(2);}else a.cameraDir=ZUnit;
    }
    const u8 connect=d.typeOption2&7;
    if((connect==1&&count<3)||(connect==0&&count<2)){
        // Too short to draw: the frames still advance.
        forEachDraw(pm,[&](Particle& p){stripeFrame(a,p,billboard,p.p.position);});
        return;
    }
    // One TEV setup for the whole strip, from the first particle in draw order.
    Particle* first=nullptr;forEachDraw(pm,[&](Particle& p){if(!first)first=&p;});
    w.setup(*first);
    const bool reversed=d.flags&DrawSetting::DrawOrder;
    const float px=0.01f*float(d.pivotX);
    if(d.typeOption==3){
        // Tube (0x80045d40): rings of typeOption0 sides drawn twice, GX cull front then
        // cull back, so the inside lands before the outside.
        const u32 sides=d.typeOption0;
        if(sides<3||sides>32)return;
        float ring[33][2];
        ring[0][0]=0.f;ring[0][1]=1.f;
        for(u32 k=1;k<sides;++k)sinCosFIdx(0.7111111283302307f*((360.f/float(sides))*float(k)),ring[k][0],ring[k][1]);
        ring[sides][0]=0.f;ring[sides][1]=1.f;
        const float py=0.01f*float(d.pivotY);
        const float step=(d.typeOption2&0xc0)==0x40?1.f:1.f/float(int(count-1));
        for(u8 pass:{u8(1),u8(2)}){
            w.cull=pass;
            w.setup(*first);
            int index=reversed?int(count)-1:0;const int inc=reversed?-1:1;
            Section prev{};bool havePrev=false;
            forEachDraw(pm,[&](Particle& p){
                const Mtx34 B=stripeFrame(a,p,billboard,p.p.position);
                const Vec3 rot=drawRotate(p);
                const float wx=sizeX(p),wy=sizeY(p);
                float s,c;sinCosRad(rot.y,s,c);
                Mtx34 L;
                L.m[0][0]=c*wx;L.m[0][1]=0;L.m[0][2]=-s*wy;L.m[0][3]=(px-px*(c*wx))+py*(s*wy);
                L.m[1][0]=0;L.m[1][1]=1;L.m[1][2]=0;L.m[1][3]=0;
                L.m[2][0]=s*wx;L.m[2][1]=0;L.m[2][2]=c*wy;L.m[2][3]=(py-px*(s*wx))-py*(c*wy);
                const Mtx34 M=mul(viewPm,mul(B,L));
                const Section cur{M.translate(),transformNormal(M,XUnit),transformNormal(M,ZUnit),step*float(index)};
                index+=inc;
                if(havePrev){
                    const Section& v1=reversed?prev:cur;const Section& v2=reversed?cur:prev;
                    Writer::Strip strip{w};
                    for(u32 k=0;k<=sides;++k){
                        const float sn=ring[k][0],cs=ring[k][1],sk=float(k)*(1.f/float(int(sides)));
                        strip.push(w.make(v1.center+v1.a*cs+v1.b*sn,sk,v1.t));
                        strip.push(w.make(v2.center+v2.a*cs+v2.b*sn,sk,v2.t));
                    }
                }
                prev=cur;havePrev=true;
            });
        }
        w.cull=0;
        return;
    }
    // Ribbon (and a perpendicular one for the cross option): two vertices per particle,
    // texcoord S 1 then 0, T along the strip from the youngest particle.
    const float step=(d.typeOption2&0xc0)==0x40?1.f:1.f/float(int(count-1));
    auto ribbon=[&](Vec3 sideA,Vec3 sideB){
        Writer::Strip strip{w};
        int index=reversed?int(count)-1:0;const int inc=reversed?-1:1;
        forEachDraw(pm,[&](Particle& p){
            const Mtx34 B=stripeFrame(a,p,billboard,p.p.position);
            const Vec3 rot=drawRotate(p);
            const float wx=sizeX(p);
            float s,c;sinCosRad(rot.y,s,c);
            s*=wx;c*=wx;
            Mtx34 L;
            L.m[0][0]=c;L.m[0][1]=0;L.m[0][2]=-s;L.m[0][3]=px-c*px;
            L.m[1][0]=0;L.m[1][1]=1;L.m[1][2]=0;L.m[1][3]=0;
            L.m[2][0]=s;L.m[2][1]=0;L.m[2][2]=c;L.m[2][3]=-s*px;
            const Mtx34 M=mul(viewPm,mul(B,L));
            const float t=step*float(index);index+=inc;
            strip.push(w.make(transformCoord(M,sideA),1.f,t));
            strip.push(w.make(transformCoord(M,sideB),0.f,t));
        });
    };
    ribbon(XUnit,{-1,0,0});
    if(d.typeOption==1)ribbon({0,0,-1},ZUnit);
}

// DrawSmoothStripeStrategy's closed ribbon (RMCE 80048f20). The original
// DrawSection_B (80047bb0) evaluates a quadratic, three-control-point
// B-spline, including the texture coordinate. Keep a rolling window, so
// no per-frame particle-sized scratch allocation or large stack is needed.
struct SmoothSection {Vec3 a,b;float t;};
void drawSmoothStripe(Writer& w,ParticleManager& pm,const Mtx34& view){
    const DrawSetting& d=pm.res->desc.draw;
    // Other connection forms and tubes require their own source ports.
    // The cooker accepts only this path, used by rk_niseBoxBiri.
    if((d.typeOption2&7)!=1||d.typeOption>2||d.typeDir!=0)return;
    u32 count=0;for(Particle* p=pm.particles.head;p;p=p->next)if(p->life==Active)++count;
    if(!count)return;
    Ahead a=aheadContext(pm);const Vec3 initial=stripeInitialAxis(a,d.typeOption2);
    for(Particle* p=pm.particles.tail;p&&1.f<p->prevAxis.x;p=p->prev)p->prevAxis=initial;
    const bool billboard=d.typeOption==2;
    const Mtx34 viewPm=mul(view,managerGlobalMtx(pm));
    if(billboard){Mtx34 inv;a.cameraDir=inverse(inv,viewPm)?inv.col(2):ZUnit;}
    if(count<3){forEachDraw(pm,[&](Particle& p){stripeFrame(a,p,billboard,p.p.position);});return;}
    Particle* first=nullptr;forEachDraw(pm,[&](Particle& p){if(!first)first=&p;});
    w.setup(*first);
    const bool reversed=d.flags&DrawSetting::DrawOrder;
    const unsigned divisions=std::max<unsigned>(d.typeOption1,1);
    // Closed loops place controls at half-intervals outside [0,1], then
    // wrap the first two positions with continued texture coordinates.
    // This avoids stretching/reversing the final seam (8004903c..84).
    const float step=(d.typeOption2&0xc0)==0x40?.5f:.5f/float(count);
    const float px=.01f*float(d.pivotX);
    auto ribbon=[&](Vec3 edgeA,Vec3 edgeB){
        Writer::Strip strip{w};
        SmoothSection first0{},first1{},prev0{},prev1{},last0{},last1{};
        unsigned seen=0;int index=reversed?int(count)*2+1:-1;const int increment=reversed?-2:2;
        auto pair=[&](const SmoothSection& v){strip.push(w.make(v.a,1.f,v.t));strip.push(w.make(v.b,0.f,v.t));};
        auto interpolate=[](const SmoothSection& p0,const SmoothSection& p1,const SmoothSection& p2,float t){
            const float t2=t*t,w2=.5f*t2,w0=.5f+w2-t,w1=.5f-t2+t;
            return SmoothSection{p0.a*w0+p1.a*w1+p2.a*w2,p0.b*w0+p1.b*w1+p2.b*w2,p0.t*w0+p1.t*w1+p2.t*w2};
        };
        auto section=[&](const SmoothSection& p0,const SmoothSection& p1,const SmoothSection& p2){
            for(unsigned n=0;n<divisions;++n)pair(interpolate(p0,p1,p2,float(n)/float(divisions)));
            last0=p1;last1=p2;
        };
        auto consume=[&](const SmoothSection& v){
            if(seen==0){first0=prev0=v;}
            else if(seen==1){first1=prev1=v;}
            else {section(prev0,prev1,v);prev0=prev1;prev1=v;}
            ++seen;
        };
        forEachDraw(pm,[&](Particle& p){
            const Mtx34 B=stripeFrame(a,p,billboard,p.p.position);
            float s,c;sinCosRad(drawRotate(p).y,s,c);const float width=sizeX(p);s*=width;c*=width;
            const Mtx34 L{{{c,0,-s,px-c*px},{0,1,0,0},{s,0,c,-s*px}}};
            const Mtx34 M=mul(viewPm,mul(B,L));
            consume({transformCoord(M,edgeA),transformCoord(M,edgeB),step*float(index)});index+=increment;
        });
        first0.t=step*float(index);index+=increment;consume(first0);
        first1.t=step*float(index);consume(first1);
        // DrawSectionEnd (80047eb0) writes the final midpoint at t=1.
        pair({(last0.a+last1.a)*.5f,(last0.b+last1.b)*.5f,(last0.t+last1.t)*.5f});
    };
    ribbon(XUnit,{-1,0,0});
    if(d.typeOption==1)ribbon({0,0,-1},ZUnit);
}
}

// ---- GX TEV -> PICA combiner programs ----
// Each retail TEV setup the RKRace roots use, with its PICA equivalent. Registers come
// from _SetupTevReg; GX stage outputs to C2/A2 go through the combiner buffer, which a
// PICA stage reads two stages after the one that writes it (buffer updates are delayed).
namespace {
struct TevStageKey { u8 texture,color[4],colorOp[5],alpha[4],alphaOp[5],kColor,kAlpha; };
bool sameStages(const DrawSetting& d,std::initializer_list<TevStageKey> stages){
    if(d.numTevs!=stages.size())return false;
    int i=0;
    for(const TevStageKey& k:stages){
        if(d.tevTexture[i]!=k.texture||std::memcmp(d.tevColor[i],k.color,4)||std::memcmp(d.tevColorOp[i],k.colorOp,5)||
           std::memcmp(d.tevAlpha[i],k.alpha,4)||std::memcmp(d.tevAlphaOp[i],k.alphaOp,5)||d.kColorSel[i]!=k.kColor||d.kAlphaSel[i]!=k.kAlpha)return false;
        ++i;
    }
    return true;
}
CombinerStage stage(Func rf,std::initializer_list<std::pair<Src,Operand>> rs,Func af,std::initializer_list<std::pair<Src,Operand>> as){
    CombinerStage st;
    st.rgbFunc=rf;st.alphaFunc=af;
    int i=0;for(auto [src,op]:rs){st.rgbSrc[i]=src;st.rgbOp[i]=op;++i;}
    i=0;for(auto [src,op]:as){st.alphaSrc[i]=src;st.alphaOp[i]=op;++i;}
    return st;
}
constexpr auto C=Operand::Color,IC=Operand::OneMinusColor,A=Operand::Alpha;
}

bool translateMaterial(const DrawSetting& d,Material& m){
    m=Material{};
    const TevStageKey lerp0{0,{2,4,8,15},{0,0,0,1,0},{7,1,4,7},{0,0,0,1,0},0,0};
    // C = lerp(C0, C1, TEX0C), A = A0 * TEX0A: INTERPOLATE(C1, C0, TEX0).
    const CombinerStage standard=stage(Func::Interpolate,{{Src::Vertex,C},{Src::Lookup,C},{Src::Tex0,C}},
                                       Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
    const bool maskedFragments=sameStages(d,{{0,{14,2,8,15},{0,0,0,1,3},{7,1,4,7},{0,0,0,1,3},12,0},
                                             {1,{15,14,8,4},{0,0,0,1,0},{7,7,7,2},{0,0,0,1,0},13,0},
                                             {255,{6,15,15,0},{0,0,0,1,0},{7,3,0,7},{0,0,0,1,0},0,0}});
    if(sameStages(d,{lerp0})){
        m.vertex={RegC1,RegC0};m.lookup={RegC0,ValNone};
        m.stages=1;m.stage[0]=standard;
    }else if(sameStages(d,{{0,{15,15,15,2},{0,0,0,1,0},{7,1,4,7},{0,0,0,1,0},0,0}})){
        // Blooper's main blot: C = C0, A = A0 * TEX0A.
        m.vertex={RegC0,RegC0};m.stages=1;
        m.stage[0]=stage(Func::Replace,{{Src::Vertex,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
    }else if(sameStages(d,{{0,{15,15,15,8},{0,0,0,1,0},{7,1,4,7},{0,0,0,1,0},0,0}})){
        // C = TEX0C, A = A0 * TEX0A.
        m.vertex={ValNone,RegC0};
        m.stages=1;m.stage[0]=stage(Func::Replace,{{Src::Tex0,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
    }else if(sameStages(d,{lerp0,{1,{15,0,8,15},{0,0,0,1,0},{0,7,7,7},{0,0,0,1,0},0,0}})){
        // Then C = CPREV * TEX1C, A = APREV.
        m.vertex={RegC1,RegC0};m.lookup={RegC0,ValNone};
        m.stages=2;m.stage[0]=standard;
        m.stage[1]=stage(Func::Modulate,{{Src::Prev,C},{Src::Tex1,C}},Func::Replace,{{Src::Prev,A}});
    }else if(sameStages(d,{lerp0,{1,{6,0,8,15},{0,0,0,1,0},{7,0,4,7},{0,0,0,1,0},0,0}})){
        // Then C = lerp(C2, CPREV, TEX1C), A = APREV * TEX1A (rk_slipStream). C2 is the
        // second layer's secondary colour, animated by emitter time: a stage constant.
        m.vertex={RegC1,RegC0};m.lookup={RegC0,ValNone};
        m.stages=2;m.stage[0]=standard;
        m.stage[1]=stage(Func::Interpolate,{{Src::Prev,C},{Src::Const,C},{Src::Tex1,C}},Func::Modulate,{{Src::Prev,A},{Src::Tex1,A}});
        m.stage[1].constant={RegC2,ValNone};
    }else if(sameStages(d,{lerp0,{1,{15,13,8,0},{0,0,0,1,0},{0,7,7,7},{0,0,0,1,0},0,0}})){
        // Then C = CPREV + HALF * TEX1C, A = APREV.
        m.vertex={RegC1,RegC0};m.lookup={RegC0,ValNone};
        m.stages=2;m.stage[0]=standard;
        m.stage[1]=stage(Func::MultiplyAdd,{{Src::Tex1,C},{Src::Const,C},{Src::Prev,C}},Func::Replace,{{Src::Prev,A}});
        m.stage[1].constant={ValHalf,ValNone};
    }else if(sameStages(d,{{0,{14,2,8,15},{0,0,0,1,3},{7,1,4,7},{0,0,0,1,3},12,0},
                          {1,{15,14,8,4},{0,0,0,1,0},{7,7,7,2},{0,0,0,1,0},13,0},
                          {255,{6,15,15,0},{0,0,0,1,0},{7,7,7,3},{0,0,0,1,0},0,0}})||maskedFragments){
        // Fake-box fragments: C2 = lerp(K0,C0,TEX0), A2 = A0*TEX0A;
        // CPREV = K1*TEX1+C1; final C = C2+CPREV, A = A2.
        // Buffer stage 0 reaches stage 2; stage 3 still reads that value.
        m.vertex={RegC0,RegC0};m.lookup={RegK0,ValNone};m.stages=4;
        m.stage[0]=stage(Func::Interpolate,{{Src::Vertex,C},{Src::Lookup,C},{Src::Tex0,C}},
                                           Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
        m.stage[0].bufferRgb=m.stage[0].bufferAlpha=true;
        // Remote ordinary boxes share the RGB path but multiply A2 by A1.
        m.stage[1]=stage(Func::Modulate,{{Src::Tex1,C},{Src::Const,C}},Func::Replace,{{maskedFragments?Src::Const:Src::Prev,A}});
        m.stage[1].constant={RegK1,maskedFragments?RegC1:ValNone};
        m.stage[2]=stage(Func::Add,{{Src::Prev,C},{Src::Const,C}},Func::Replace,{{maskedFragments?Src::Prev:Src::Buffer,A}});
        m.stage[2].constant={RegC1,ValNone};
        m.stage[3]=stage(Func::Add,{{Src::Buffer,C},{Src::Prev,C}},maskedFragments?Func::Modulate:Func::Replace,
                                           {{maskedFragments?Src::Buffer:Src::Prev,A},{Src::Prev,A}});
    }else if(sameStages(d,{{0,{15,2,8,4},{0,0,0,1,0},{7,1,4,7},{0,0,0,1,0},0,0}})){
        // C = C0 * TEX0C + C1, A = A0 * TEX0A.
        m.vertex={RegC0,RegC0};m.lookup={RegC1,ValNone};
        m.stages=1;m.stage[0]=stage(Func::MultiplyAdd,{{Src::Vertex,C},{Src::Tex0,C},{Src::Lookup,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
    }else if(sameStages(d,{{0,{14,2,8,15},{0,2,1,1,3},{7,1,4,7},{0,0,0,1,0},12,0},
                          {1,{14,4,8,15},{0,0,0,1,0},{7,0,4,7},{0,0,0,1,0},13,0},
                          {1,{6,15,15,0},{0,0,0,1,0},{7,0,4,0},{0,0,0,1,0},0,0}})){
        // C2 = 2 * (lerp(K0, C0, TEX0C) - 0.5); APREV = A0 * TEX0A
        // CPREV = lerp(K1, C1, TEX1C); APREV *= TEX1A
        // C = C2 + CPREV; A = APREV * TEX1A + APREV
        m.vertex={RegC0,RegC0};m.lookup={RegK0,ValNone};
        m.stages=5;
        m.stage[0]=stage(Func::Interpolate,{{Src::Vertex,C},{Src::Lookup,C},{Src::Tex0,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
        m.stage[1]=stage(Func::AddSigned,{{Src::Prev,C},{Src::Const,C}},Func::Replace,{{Src::Prev,A}});
        m.stage[1].constant={ValZero,ValNone};m.stage[1].rgbScale=2;m.stage[1].bufferRgb=true;
        m.stage[2]=stage(Func::Modulate,{{Src::Tex1,C},{Src::Const,C}},Func::Modulate,{{Src::Prev,A},{Src::Tex1,A}});
        m.stage[2].constant={RegC1,ValNone};
        m.stage[3]=stage(Func::MultiplyAdd,{{Src::Tex1,IC},{Src::Const,C},{Src::Prev,C}},Func::Replace,{{Src::Prev,A}});
        m.stage[3].constant={RegK1,ValNone};
        m.stage[4]=stage(Func::Add,{{Src::Buffer,C},{Src::Prev,C}},Func::MultiplyAdd,{{Src::Prev,A},{Src::Tex1,A},{Src::Prev,A}});
    }else if(sameStages(d,{{0,{14,2,8,15},{0,0,0,1,3},{7,1,4,7},{0,0,0,1,3},12,0},
                          {1,{14,4,8,15},{0,0,0,1,0},{7,2,4,7},{0,0,0,1,0},13,0},
                          {255,{6,0,13,15},{0,0,0,1,0},{7,3,0,7},{0,0,0,1,0},0,0}})){
        // C2 = lerp(K0, C0, TEX0C), A2 = A0 * TEX0A; CPREV = lerp(K1, C1, TEX1C), APREV = A1 * TEX1A;
        // C = lerp(C2, CPREV, HALF), A = A2 * APREV.
        m.vertex={RegC0,RegC0};m.lookup={RegK0,ValNone};
        m.stages=4;
        m.stage[0]=stage(Func::Interpolate,{{Src::Vertex,C},{Src::Lookup,C},{Src::Tex0,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
        m.stage[0].bufferRgb=m.stage[0].bufferAlpha=true;
        m.stage[1]=stage(Func::Modulate,{{Src::Tex1,C},{Src::Const,C}},Func::Modulate,{{Src::Tex1,A},{Src::Const,A}});
        m.stage[1].constant={RegC1,RegC1};
        m.stage[2]=stage(Func::MultiplyAdd,{{Src::Tex1,IC},{Src::Const,C},{Src::Prev,C}},Func::Replace,{{Src::Prev,A}});
        m.stage[2].constant={RegK1,ValNone};
        m.stage[3]=stage(Func::Interpolate,{{Src::Buffer,C},{Src::Prev,C},{Src::Const,C}},Func::Modulate,{{Src::Buffer,A},{Src::Prev,A}});
        m.stage[3].constant={ValHalf,ValNone};
    }else if(sameStages(d,{{0,{15,14,8,2},{0,0,0,1,3},{7,1,4,7},{0,0,0,1,3},12,0},
                          {1,{14,4,8,15},{0,0,0,1,0},{7,2,4,7},{0,0,0,1,0},13,0},
                          {255,{15,15,15,6},{0,0,0,1,0},{7,3,0,7},{0,0,0,1,0},0,0}})){
        // C2 = K0 * TEX0C + C0, A2 = A0 * TEX0A; CPREV = lerp(K1, C1, TEX1C), APREV = A1 * TEX1A;
        // C = C2, A = A2 * APREV.
        m.vertex={RegK0,RegC0};m.lookup={RegC0,ValNone};
        m.stages=4;
        m.stage[0]=stage(Func::MultiplyAdd,{{Src::Tex0,C},{Src::Vertex,C},{Src::Lookup,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
        m.stage[0].bufferRgb=m.stage[0].bufferAlpha=true;
        m.stage[1]=stage(Func::Modulate,{{Src::Tex1,C},{Src::Const,C}},Func::Modulate,{{Src::Tex1,A},{Src::Const,A}});
        m.stage[1].constant={RegC1,RegC1};
        m.stage[2]=stage(Func::MultiplyAdd,{{Src::Tex1,IC},{Src::Const,C},{Src::Prev,C}},Func::Replace,{{Src::Prev,A}});
        m.stage[2].constant={RegK1,ValNone};
        m.stage[3]=stage(Func::Replace,{{Src::Buffer,C}},Func::Modulate,{{Src::Buffer,A},{Src::Prev,A}});
    }else if(sameStages(d,{{0,{8,15,15,15},{0,2,3,1,0},{7,1,4,7},{0,0,0,1,0},0,0},
                          {1,{8,15,15,0},{0,2,0,1,0},{0,7,7,7},{0,0,0,1,0},0,0}})){
        // C = (TEX0C - 0.5) / 2 (clamped); then C = TEX1C + CPREV - 0.5. A = A0 * TEX0A.
        m.vertex={ValNone,RegC0};
        m.stages=3;
        m.stage[0]=stage(Func::Modulate,{{Src::Tex0,C},{Src::Const,C}},Func::Modulate,{{Src::Tex0,A},{Src::Vertex,A}});
        m.stage[0].constant={ValHalf,ValNone};
        m.stage[1]=stage(Func::Subtract,{{Src::Prev,C},{Src::Const,C}},Func::Replace,{{Src::Prev,A}});
        m.stage[1].constant={ValQuarter,ValNone};
        m.stage[2]=stage(Func::AddSigned,{{Src::Tex1,C},{Src::Prev,C}},Func::Replace,{{Src::Prev,A}});
    }else if(sameStages(d,{{0,{3,8,8,5},{0,2,0,1,0},{4,7,7,7},{0,0,0,1,0},0,0},
                          {1,{8,15,15,0},{0,0,0,1,0},{7,3,4,7},{0,0,0,1,0},0,0}})){
        // C = A0 * (1 - TEX0C) + TEX0C * TEX0C + A1 - 0.5; then C = TEX1C + CPREV, A = A2 * TEX1A.
        m.vertex={ValNone,RegC1};m.lookup={ValNone,RegC2};
        m.stages=3;
        m.stage[0]=stage(Func::Interpolate,{{Src::Tex0,C},{Src::Const,C},{Src::Tex0,C}},Func::Replace,{{Src::Tex0,A}});
        m.stage[0].constant={RegC0,ValNone,true};
        m.stage[1]=stage(Func::AddSigned,{{Src::Prev,C},{Src::Vertex,A}},Func::Replace,{{Src::Prev,A}});
        m.stage[2]=stage(Func::Add,{{Src::Tex1,C},{Src::Prev,C}},Func::Modulate,{{Src::Tex1,A},{Src::Lookup,A}});
    }else if(sameStages(d,{{0,{2,4,8,15},{0,0,0,1,3},{7,1,4,7},{0,0,0,1,3},0,0},
                          {1,{15,15,15,8},{0,0,0,1,0},{7,7,7,2},{0,0,0,1,0},0,0},
                          {255,{15,15,15,6},{0,0,0,1,0},{7,3,0,7},{0,0,0,1,0},0,0}})){
        // C2 = lerp(C0, C1, TEX0C), A2 = A0 * TEX0A; CPREV = TEX1C, APREV = A1; C = C2, A = A2 * APREV.
        m.vertex={RegC1,RegC0};m.lookup={RegC0,RegC1};
        m.stages=3;
        m.stage[0]=standard;m.stage[0].bufferRgb=m.stage[0].bufferAlpha=true;
        m.stage[1]=stage(Func::Replace,{{Src::Tex1,C}},Func::Replace,{{Src::Lookup,A}});
        m.stage[2]=stage(Func::Replace,{{Src::Buffer,C}},Func::Modulate,{{Src::Buffer,A},{Src::Prev,A}});
    }else return false;
    const bool projected0=(d.flags&DrawSetting::Tex1)&&(d.flags&DrawSetting::Tex1Proj),
               projected1=(d.flags&DrawSetting::Tex2)&&(d.flags&DrawSetting::Tex2Proj);
    if(projected0&&projected1)return false; // one projection unit; unused by RKRace
    m.projected=projected0||projected1;
    if(projected1){
        m.swapUnits=true;
        auto swap=[](Src& src){if(src==Src::Tex0)src=Src::Tex1;else if(src==Src::Tex1)src=Src::Tex0;};
        for(u32 i=0;i<m.stages;++i)for(int k=0;k<3;++k){swap(m.stage[i].rgbSrc[k]);swap(m.stage[i].alphaSrc[k]);}
    }
    m.valid=true;
    return true;
}

void buildDrawList(System& s,const std::vector<Material>& materials,const TextureSize* textures,u32 textureCount,
                   const Mtx34& view,DrawList& out,u32 vertexLimit,const mkw::ClipFrustum* frustum){
    out.clear();
    Writer w{out,materials,textures,textureCount,u32(triangleVertexCount(vertexLimit,vertexLimit)),frustum};
    for(unsigned cameraSpace=0;cameraSpace<2;++cameraSpace)
    for(Effect* e=s.active.head;e;e=e->next){
        if((e->flags&2u)||!(s.drawGroups>>e->group&1u)||unsigned(s.cameraSpaceGroups>>e->group&1u)!=cameraSpace)continue;
        for(ParticleManager* pm=e->drawHead;pm;pm=pm->drawNext){
            const EmitterResource& res=*pm->res;
            const DrawSetting& d=res.desc.draw;
            if((d.flags&DrawSetting::Hidden)||(pm->emitter->prm.comFlags&EmitterDesc::DisableDraw))continue;
            if(!pm->particles.head||res.index>=materials.size()||!materials[res.index].valid)continue;
            w.res=&res;w.mat=&materials[res.index];w.cull=0;
            const Mtx34 v=viewWithZOffset(cameraSpace?Mtx34::identity():view,*pm);
            switch(d.ptclType){
            case 3:drawBillboard(w,*pm,v);break;
            case 4:drawDirectional(w,*pm,v);break;
            case 5:drawStripe(w,*pm,v);break;
            case 6:drawSmoothStripe(w,*pm,v);break;
            default:break;
            }
        }
    }
}
}
