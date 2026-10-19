// NW4R EF runtime as linked into Mario Kart Wii (RMCE main.dol), ported for RKRace:
//   Effect::Calc 0x800268d0, Emitter 0x80027b40-0x80029d80 (InitializeDatas 0x80027ef0,
//   CreateEmitter 0x80028330, CreateEmitterTmp 0x80028610, Emission 0x80028df0,
//   CalcEmitter 0x80029240, CalcEmission 0x80029490, CalcBillboard 0x800295b0,
//   RestructMatrix 0x80029790, CalcGlobalMtx 0x800299c0), Particle::Initialize 0x80032730,
//   ParticleManager::Initialize 0x80033550, colour modifier 0x80035470, Effect::ForeachEmitter
//   0x80026d20 (EGG::Effect::setColor),
//   ParticleManager::Calc 0x80033810 (fields, postfield 0x80031b40), emitter forms
//   0x80038070-0x8003a5c0, CalcVelocity 0x80037590, CalcLife 0x80037a60, CreationQueue
//   0x80037b50-0x80037e50 and the ef_util matrix helpers. ogws (Wii Sports) sources name
//   the shared structure; docs/VFX_RESEARCH.md lists the MKW differences.
#include "mkw/nw4r_ef_internal.hpp"
#include "mkw/nw4r_sin_table.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace mkw::ef {
namespace {
constexpr float Pi=3.1415927410125732f;
constexpr float FIdxPerRad=40.7436637878418f;
constexpr float FltEpsilon=1.1920928955078125e-07f,FltMin=1.1754943508222875e-38f;

}
void sinCosFIdx(float fidx,float& s,float& c){
    float a=std::fabs(fidx);
    if(65536.f<a){do a-=65536.f;while(65536.f<=a);}
    const u32 whole=a>=65535.f?65535u:u32(a);
    const float frac=a-float(whole);
    const float* t=SinCosTable[whole&0xff];
    s=t[0]+frac*t[2];c=t[1]+frac*t[3];
    if(fidx<0.f)s=-s;
}

// ---- math (nw4r::math and ef_util) ----
Mtx34 mul(const Mtx34& a,const Mtx34& b){
    Mtx34 r;
    for(int i=0;i<3;++i){
        for(int j=0;j<4;++j)r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j];
        r.m[i][3]+=a.m[i][3];
    }
    return r;
}
bool inverse(Mtx34& out,const Mtx34& m){
    const float(*a)[4]=m.m;
    const float det=a[0][0]*a[1][1]*a[2][2]+a[0][1]*a[1][2]*a[2][0]+a[0][2]*a[1][0]*a[2][1]
                   -a[2][0]*a[1][1]*a[0][2]-a[1][0]*a[0][1]*a[2][2]-a[0][0]*a[2][1]*a[1][2];
    if(det==0.f)return false;
    const float d=1.f/det;
    Mtx34 r;
    r.m[0][0]=(a[1][1]*a[2][2]-a[2][1]*a[1][2])*d;r.m[0][1]=-(a[0][1]*a[2][2]-a[2][1]*a[0][2])*d;r.m[0][2]=(a[0][1]*a[1][2]-a[1][1]*a[0][2])*d;
    r.m[1][0]=-(a[1][0]*a[2][2]-a[2][0]*a[1][2])*d;r.m[1][1]=(a[0][0]*a[2][2]-a[2][0]*a[0][2])*d;r.m[1][2]=-(a[0][0]*a[1][2]-a[1][0]*a[0][2])*d;
    r.m[2][0]=(a[1][0]*a[2][1]-a[2][0]*a[1][1])*d;r.m[2][1]=-(a[0][0]*a[2][1]-a[2][0]*a[0][1])*d;r.m[2][2]=(a[0][0]*a[1][1]-a[1][0]*a[0][1])*d;
    for(int i=0;i<3;++i)r.m[i][3]=-(r.m[i][0]*a[0][3]+r.m[i][1]*a[1][3]+r.m[i][2]*a[2][3]);
    out=r;return true;
}
Vec3 transformCoord(const Mtx34& m,Vec3 v){
    return {m.m[0][0]*v.x+m.m[0][1]*v.y+m.m[0][2]*v.z+m.m[0][3],m.m[1][0]*v.x+m.m[1][1]*v.y+m.m[1][2]*v.z+m.m[1][3],
            m.m[2][0]*v.x+m.m[2][1]*v.y+m.m[2][2]*v.z+m.m[2][3]};
}
Vec3 transformNormal(const Mtx34& m,Vec3 v){
    return {m.m[0][0]*v.x+m.m[0][1]*v.y+m.m[0][2]*v.z,m.m[1][0]*v.x+m.m[1][1]*v.y+m.m[1][2]*v.z,
            m.m[2][0]*v.x+m.m[2][1]*v.y+m.m[2][2]*v.z};
}
Mtx34 noTranslate(Mtx34 m){m.m[0][3]=m.m[1][3]=m.m[2][3]=0;return m;}
void sinCosRad(float rad,float& s,float& c){sinCosFIdx(rad*FIdxPerRad,s,c);}
float sinFIdx(float fidx){float s,c;sinCosFIdx(fidx,s,c);return s;}
float cosFIdx(float fidx){float s,c;sinCosFIdx(fidx,s,c);return c;}
Mtx34 rotXYZ(float x,float y,float z){
    float sx,cx,sy,cy,sz,cz;sinCosRad(x,sx,cx);sinCosRad(y,sy,cy);sinCosRad(z,sz,cz);
    Mtx34 r;
    r.m[0][0]=cz*cy;r.m[1][0]=sz*cy;r.m[2][0]=-sy;
    r.m[0][1]=(sx*sy)*cz-cx*sz;r.m[1][1]=(sx*sy)*sz+cx*cz;r.m[2][1]=sx*cy;
    r.m[0][2]=(cx*sy)*cz+sx*sz;r.m[1][2]=(cx*sy)*sz-sx*cz;r.m[2][2]=cx*cy;
    r.m[0][3]=r.m[1][3]=r.m[2][3]=0;
    return r;
}
Mtx34 rotAxis(Vec3 axis,float radians){
    float s,c;sinCosRad(radians,s,c);
    const float t=1.f-c;
    Vec3 v=axis;normalize(v);
    Mtx34 r;
    r.m[0][0]=t*v.x*v.x+c;r.m[0][1]=t*v.x*v.y-s*v.z;r.m[0][2]=t*v.x*v.z+s*v.y;r.m[0][3]=0;
    r.m[1][0]=t*v.x*v.y+s*v.z;r.m[1][1]=t*v.y*v.y+c;r.m[1][2]=t*v.y*v.z-s*v.x;r.m[1][3]=0;
    r.m[2][0]=t*v.x*v.z-s*v.y;r.m[2][1]=t*v.y*v.z+s*v.x;r.m[2][2]=t*v.z*v.z+c;r.m[2][3]=0;
    return r;
}
Mtx34 translateBy(const Mtx34& m,Vec3 t){
    Mtx34 r=m;
    for(int i=0;i<3;++i)r.m[i][3]=m.m[i][0]*t.x+m.m[i][1]*t.y+m.m[i][2]*t.z+m.m[i][3];
    return r;
}
Mtx34 scaleBy(const Mtx34& m,Vec3 s){
    // 0x800372a0: a zero scale becomes 1e-5 so the matrix stays invertible.
    const float sx=s.x==0.f?9.999999747378752e-06f:s.x,sy=s.y==0.f?9.999999747378752e-06f:s.y,sz=s.z==0.f?9.999999747378752e-06f:s.z;
    Mtx34 r=m;
    for(int i=0;i<3;++i){r.m[i][0]*=sx;r.m[i][1]*=sy;r.m[i][2]*=sz;}
    return r;
}
namespace {
// 0x80037320: rows scaled (S * m), zero scale components replaced by 1e-5.
Mtx34 scaleRows(const Mtx34& m,Vec3 s){
    constexpr float Tiny=9.999999747378752e-06f;
    const float v[3]={s.x==0.f?Tiny:s.x,s.y==0.f?Tiny:s.y,s.z==0.f?Tiny:s.z};
    Mtx34 r=m;
    for(int i=0;i<3;++i)for(int j=0;j<4;++j)r.m[i][j]*=v[i];
    return r;
}
// PSMTXTransApply: T * m.
Mtx34 transApply(const Mtx34& m,Vec3 t){Mtx34 r=m;r.m[0][3]+=t.x;r.m[1][3]+=t.y;r.m[2][3]+=t.z;return r;}
}
bool normalize(Vec3& v){
    const float len2=v.z*v.z+v.x*v.x+v.y*v.y;
    if(len2==0.f)return false;
    const float k=1.f/std::sqrt(len2);
    v={v.x*k,v.y*k,v.z*k};return true;
}
Vec3 rotationToVecY(Vec3 rot){
    float sx,cx,sy,cy,sz,cz;sinCosRad(rot.x,sx,cx);sinCosRad(rot.y,sy,cy);sinCosRad(rot.z,sz,cz);
    return {cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx};
}
Mtx34 dirMtxY(Vec3 dir){
    const float sx=dir.z;
    float d=1.f-sx*sx;if(d<0.f)d=0.f;
    const float cx=d>0.f?std::sqrt(d):0.f;
    float sz=0.f,cz=1.f;
    if(cx>FltEpsilon){cz=dir.y/cx;sz=dir.x/-cx;}
    Mtx34 r;
    r.m[0][0]=cz;r.m[0][1]=dir.x;r.m[0][2]=sx*sz;r.m[0][3]=0;
    r.m[1][0]=sz;r.m[1][1]=dir.y;r.m[1][2]=-cz*sx;r.m[1][3]=0;
    r.m[2][0]=0;r.m[2][1]=dir.z;r.m[2][2]=cx;r.m[2][3]=0;
    return r;
}
Mtx34 rotationMtx(const Mtx34& m){
    Vec3 x=m.col(0),y=m.col(1);
    if(!normalize(x))x.x=1.f;
    if(!normalize(y))y.y=1.f;
    const Vec3 z=cross(x,y);y=cross(z,x);
    Mtx34 r;r.setCol(0,x);r.setCol(1,y);r.setCol(2,z);r.m[0][3]=r.m[1][3]=r.m[2][3]=0;
    return r;
}
Vec3 rotationOf(const Mtx34& m){
    auto colLen=[&](int c){return std::sqrt(m.m[2][c]*m.m[2][c]+m.m[0][c]*m.m[0][c]+m.m[1][c]*m.m[1][c]);};
    const float sx=colLen(0),sy=colLen(1),sz=colLen(2);
    if(sx<FltMin||sy<FltMin||sz<FltMin)return {};
    float f=-m.m[2][0]/sx;f=std::min(1.f,std::max(-1.f,f));
    Vec3 r;r.y=float(std::asin(double(f)));
    auto atan2f=[](float y,float x){return float(std::atan2(double(y),double(x)));};
    if(cosFIdx(FIdxPerRad*r.y)<FltMin){r.x=atan2f(m.m[0][1],m.m[1][1]);r.z=0.f;}
    else{r.x=atan2f(m.m[2][1]/sy,m.m[2][2]/sz);r.z=atan2f(m.m[1][0],m.m[0][0]);}
    return r;
}
Vec3 scaleOf(const Mtx34& m){
    auto lenSq=[](Vec3 v){return dot(v,v);};
    Vec3 s{},v0=m.col(0),v1,v2;
    float mag=lenSq(v0);
    if(mag>FltEpsilon){
        mag=1.f/std::sqrt(mag);s.x=1.f/mag;v0=v0*mag;
        v1=m.col(1);float sh0=dot(v0,v1);v1=v1-v0*sh0;
        mag=lenSq(v1);
        if(mag>FltEpsilon){
            mag=1.f/std::sqrt(mag);s.y=1.f/mag;v1=v1*mag;
            v2=m.col(2);const float sh2=dot(v1,v2);v2=v2-v1*sh2;
            const float sh1=dot(v0,v2);v2=v2-v0*sh1;
            mag=lenSq(v2);
            if(mag>FltEpsilon){
                s.z=std::sqrt(mag);
                if(dot(v0,cross(v1,v2))<0.f)s={-s.x,-s.y,-s.z};
            }else s.z=0.f;
        }else{
            s.y=0.f;v2=m.col(2);const float sh1=dot(v0,v2);v2=v2-v0*sh1;
            mag=lenSq(v2);s.z=mag>FltEpsilon?std::sqrt(mag):0.f;
        }
    }else{
        s.x=0.f;v1=m.col(1);mag=lenSq(v1);
        if(mag>FltEpsilon){mag=1.f/std::sqrt(mag);s.y=1.f/mag;v1=v1*mag;v2=m.col(2);v2=v2-v1*dot(v1,v2);s.z=length(v2);}
        else{s.y=0.f;s.z=length(m.col(2));}
    }
    return s;
}

namespace {
template<class T> void unref(System& s,T* o);
void sendClosing(System& s,Particle* p);
void sendClosing(System& s,ParticleManager* pm);
void sendClosing(System& s,Emitter* em);
void sendClosing(System& s,Effect* e);
void destroyFunc(System& s,Emitter* em);
void destroyFunc(System&,Particle*){}
void destroyFunc(System&,ParticleManager*){}
template<class T> void destroy(System& s,T* o){destroyFunc(s,o);o->life=Wait;if(o->refs==0)sendClosing(s,o);}
template<class T> void unref(System& s,T* o){if(--o->refs==0&&o->life==Wait)sendClosing(s,o);}


void drawAdd(Effect& e,ParticleManager* pm){
    // DrawOrder::Add: after the last manager whose weight is not greater.
    ParticleManager* at=nullptr;
    for(ParticleManager* it=e.drawHead;it;it=it->drawNext)at=it;
    while(at&&at->weight>pm->weight)at=at->drawPrev;
    ParticleManager* next=at?at->drawNext:e.drawHead;
    pm->drawPrev=at;pm->drawNext=next;
    if(next)next->drawPrev=pm;
    if(at)at->drawNext=pm;else e.drawHead=pm;
}
void drawRemove(Effect& e,ParticleManager* pm){
    if(pm->drawPrev)pm->drawPrev->drawNext=pm->drawNext;else e.drawHead=pm->drawNext;
    if(pm->drawNext)pm->drawNext->drawPrev=pm->drawPrev;
    pm->drawPrev=pm->drawNext=nullptr;
}

void sendClosing(System& s,Particle* p){
    ParticleManager* pm=p->manager;
    unref(s,pm);
    pm->particles.remove(p);
    p->life=Closing;
    if(pm->lastCalced==p)pm->lastCalced=nullptr;
    s.deadParticles.push_back(p);
    --s.liveParticles;
}
void sendClosing(System& s,ParticleManager* pm){
    Emitter* em=pm->emitter;
    drawRemove(*em->effect,pm);
    unref(s,em);
    em->managers.remove(pm);
    pm->life=Closing;
    s.deadManagers.push_back(pm);
}
void sendClosing(System& s,Emitter* em){
    Effect* e=em->effect;
    if(em->parent)unref(s,em->parent);
    if(em->referenceParticle){unref(s,em->referenceParticle);em->referenceParticle=nullptr;}
    unref(s,e);
    e->emitters.remove(em);
    em->life=Closing;
    s.deadEmitters.push_back(em);
}
void sendClosing(System& s,Effect* e){
    s.active.remove(e);
    e->life=Closing;
    s.deadEffects.push_back(e);
}

u32 retireParticle(System& s,Particle* p){if(p->life!=Active)return 0;destroy(s,p);return 1;}
u32 retireParticleAll(System& s,ParticleManager& pm){
    u32 n=0;
    for(Particle* p=pm.particles.head;p;){Particle* next=p->next;if(p->life==Active)n+=retireParticle(s,p);p=next;}
    return n;
}
u32 retireManager(System& s,ParticleManager* pm){if(pm->life!=Active)return 0;destroy(s,pm);return 1;}
template<class T,class F> void snapshot(List<T>& list,F f){
    // These snapshots nest while calculating and retiring effects. Use the
    // actual pool bound: a 1024-pointer particle array at every level needlessly
    // consumed several KiB of the Old 3DS main thread's 64 KiB stack.
    static_assert(std::is_same_v<T,Particle>||std::is_same_v<T,ParticleManager>||
                  std::is_same_v<T,Emitter>||std::is_same_v<T,Effect>);
    constexpr size_t Capacity=std::is_same_v<T,Particle>?System::ParticleCapacity:
        std::is_same_v<T,ParticleManager>?System::ManagerCapacity:
        std::is_same_v<T,Emitter>?System::EmitterCapacity:System::EffectCapacity;
    T* items[Capacity];u32 n=0;
    for(T* it=list.head;it&&n<Capacity;it=it->next)items[n++]=it;
    for(u32 i=0;i<n;++i)f(items[i]);
}
u32 retireParticleAll(System& s,Emitter& em){u32 n=0;snapshot(em.managers,[&](ParticleManager* pm){n+=retireParticleAll(s,*pm);});return n;}
u32 retireManagerAll(System& s,Emitter& em){
    u32 n=0;snapshot(em.managers,[&](ParticleManager* pm){if(pm->life==Active)n+=retireManager(s,pm);});return n;
}
u32 retireEmitter(System& s,Emitter* em){if(em->life!=Active)return 0;destroy(s,em);return 1;}
u32 retireEmitterAll(System& s,Effect& e){
    u32 n=0;snapshot(e.emitters,[&](Emitter* em){if(em->life==Active)n+=retireEmitter(s,em);});return n;
}

void destroyFunc(System& s,Emitter* em){
    em->form=0xff;
    if(em->prm.comFlags&EmitterDesc::SyncLife)retireParticleAll(s,*em);
    retireManagerAll(s,*em);
    if(em->prm.comFlags&EmitterDesc::SyncLife){
        snapshot(em->effect->emitters,[&](Emitter* other){
            for(Emitter* it=other->parent;it;it=it->parent){
                if(it!=em)continue;
                retireParticleAll(s,*other);
                if(other->life==Active)retireEmitter(s,other);
                break;
            }
        });
    }
}

// ---- matrices ----
Mtx34 restructMatrix(const Mtx34& orig,bool inheritS,bool inheritR,s8 inheritT,bool inheritAxis){
    if(inheritS&&inheritR&&inheritT==100)return orig;
    Mtx34 r=Mtx34::identity();
    if(!inheritS&&!inheritR&&inheritT==0)return r;
    if(inheritT!=0){
        Vec3 t=orig.translate();
        if(inheritT!=100)t=t*(float(inheritT)/100.f);
        r=translateBy(r,t);
    }
    if(inheritR||inheritS){
        const bool pivot=inheritAxis&&inheritT!=100;
        if(pivot)r=translateBy(r,orig.translate());
        if(inheritR)r=mul(r,rotationMtx(orig));
        if(inheritS)r=scaleBy(r,scaleOf(orig));
        if(pivot)r=translateBy(r,orig.translate()*-1.f);
    }
    return r;
}
void setMtxDirty(Emitter& em){
    em.mtxDirty=true;
    for(ParticleManager* pm=em.managers.head;pm;pm=pm->next)pm->mtxDirty=true;
    if(!em.effect)return;
    for(Emitter* other=em.effect->emitters.head;other;other=other->next){
        if(other->mtxDirty)continue;
        for(Emitter* it=other->parent;it;it=it->parent){
            if(it!=&em)continue;
            other->mtxDirty=true;
            for(ParticleManager* pm=other->managers.head;pm;pm=pm->next)pm->mtxDirty=true;
        }
    }
}
}

Mtx34 emitterGlobalMtx(Emitter& em){
    if(em.mtxDirty){
        if(!em.parent)em.mtx=em.effect->root;
        else{
            const Mtx34 orig=emitterGlobalMtx(*em.parent);
            em.mtx=restructMatrix(orig,em.prm.inherit&1,em.prm.inherit&2,em.prm.inheritTranslate,em.prm.inherit&4);
        }
        em.mtx=translateBy(em.mtx,em.prm.translate);
        em.mtx=mul(em.mtx,rotXYZ(em.prm.rotate.x,em.prm.rotate.y,em.prm.rotate.z));
        em.mtx=scaleBy(em.mtx,em.prm.scale);
        em.mtxDirty=false;
    }
    return em.mtx;
}
Mtx34 managerGlobalMtx(ParticleManager& pm){
    if(pm.mtxDirty){
        pm.mtx=restructMatrix(emitterGlobalMtx(*pm.emitter),pm.flag&1,pm.flag&2,pm.inheritTranslate,pm.flag&4);
        pm.mtxDirty=false;
    }
    return pm.mtx;
}

namespace {
// ---- particles ----
void initParameter(ParticleParameter& p,const ParticleDesc& d,Emitter& em){
    std::copy(&d.color[0][0][0],&d.color[0][0][0]+16,&p.color[0][0][0]);
    p.size[0]=d.size[0];p.size[1]=d.size[1];
    p.scale[0]=d.scale[0];p.scale[1]=d.scale[1];
    for(int i=0;i<3;++i)p.rotate[i]=d.rotate[i];
    for(int i=0;i<3;++i){
        float rad=d.rotateOffset[i];
        if(d.rotateOffsetRandom[i]){
            const float rnd=float(em.random.rand())/65536.f;
            rad=rad+float(d.rotateOffsetRandom[i])*(rad*(2.f*rnd-1.f))/100.f;
        }
        const int v=int(std::ceil(128.f*(rad/Pi)-0.5f));
        p.rotateOffset[i]=u8(v);
    }
    for(int i=0;i<3;++i){
        p.texture[i]=d.texture[i]==0xffff?0xffffffffu:d.texture[i];
        p.textureScale[i][0]=d.textureScale[i][0];p.textureScale[i][1]=d.textureScale[i][1];
        p.textureRotate[i]=d.textureRotate[i];
        p.textureTranslate[i][0]=d.textureTranslate[i][0];p.textureTranslate[i][1]=d.textureTranslate[i][1];
    }
    p.textureWrap=d.textureWrap;p.textureReverse=d.textureReverse;p.acmpRef0=d.acmpRef0;p.acmpRef1=d.acmpRef1;
    p.collisionStatus=0;
}

// Collision fade for a reference particle's size or alpha (+0xe4 counter, manager +0x88..0x8a).
u32 fadeByCollision(const Particle& ref,u32 value,u8 limit){
    const u32 counter=ref.collisionCounter;
    if(counter!=0xff&&counter<limit){
        const int start=ref.manager->collisionFadeStart;
        value=u32(int(value*u32(start+(100-start)*int(counter)/int(limit)))/100);
    }
    return value;
}

void initParticle(Particle& pt,u16 life,Vec3 pos,Vec3 vel,ParticleManager& pm,const Mtx34* space,float momentum,
                  const InheritSetting* setting,Particle* ref){
    if(life==0)life=1;
    pt.refs=0;pt.life=Active;
    pt.manager=&pm;++pm.refs;
    pt.eval=EvalWait;pt.calcRemain=0;
    Emitter& em=*pm.emitter;
    initParameter(pt.p,pm.res->ptcl,em);
    pt.p.momentum=momentum;
    pt.tick=0;
    pt.seed=u16(em.random.rand());
    pt.lifeSpan=life;
    pt.collisionCounter=0xff;
    const u32 r=em.random.seed*0x343fdu+0x269ec3u;em.random.seed=r;
    pt.p.alphaFlickRnd=s8(u8(u8(r>>16)+u8(r/0xfe0000u)*2-0x7e));
    pt.p.position=pos;pt.p.prevPosition=pos;pt.p.velocity=vel;
    // Retail Particle::Initialize USA80032898..32918 adds Effect+84 in
    // emitter-local space before the optional particle-space transform.
    const Vec3 inherited=em.effect->velocity;
    if(inherited.x!=0||inherited.y!=0||inherited.z!=0){
        Mtx34 inv;
        if(inverse(inv,noTranslate(emitterGlobalMtx(em))))
            pt.p.velocity=pt.p.velocity+transformNormal(inv,inherited);
    }
    if(space){
        pt.p.position=transformCoord(*space,pt.p.position);
        pt.p.velocity=transformNormal(*space,pt.p.velocity);
    }
    pt.prevAxis={2.f,0.f,0.f};
    if(!ref||!setting)return;
    if(setting->speed!=0){
        Vec3 g=transformNormal(noTranslate(managerGlobalMtx(*ref->manager)),ref->p.velocity);
        Mtx34 inv;if(inverse(inv,noTranslate(managerGlobalMtx(pm))))g=transformNormal(inv,g);
        const float k=0.01f*float(setting->speed);
        pt.p.velocity.x+=k*g.x;pt.p.velocity.y+=k*g.y;pt.p.velocity.z+=k*g.z;
    }
    if(setting->scale!=0){
        const ParticleManager& rpm=*ref->manager;
        const u16 sameFlags=rpm.res->desc.draw.flags&(DrawSetting::XYSameSize|DrawSetting::XYSameScale);
        float sx=ref->p.size[0]*ref->p.scale[0];
        if(ref->collisionCounter!=0xff&&ref->collisionCounter<rpm.collisionFadeScale)
            sx=sx*0.01f*float(fadeByCollision(*ref,100,rpm.collisionFadeScale));
        pt.p.size[0]=0.01f*float(setting->scale)*sx*rpm.modifierScale[0];
        float sy;
        switch(sameFlags){
        case DrawSetting::XYSameScale:sy=ref->p.size[1]*ref->p.scale[0];break;
        case DrawSetting::XYSameSize:sy=ref->p.size[0]*ref->p.scale[1];break;
        case DrawSetting::XYSameSize|DrawSetting::XYSameScale:sy=ref->p.size[0]*ref->p.scale[0];break;
        default:sy=ref->p.size[1]*ref->p.scale[1];break;
        }
        if(ref->collisionCounter!=0xff&&ref->collisionCounter<rpm.collisionFadeScale)
            sy=sy*0.01f*float(fadeByCollision(*ref,100,rpm.collisionFadeScale));
        pt.p.size[1]=0.01f*float(setting->scale)*sy*rpm.modifierScale[1];
    }
    if(setting->alpha!=0){
        const ParticleManager& rpm=*ref->manager;
        // 0x80032ca8: the parent manager's modifier applied to white; flag 4 scales by its alpha.
        u8 white[2][4]={{0xff,0xff,0xff,0xff},{0xff,0xff,0xff,0xff}};
        modifyColor(rpm,white[0],white[1]);
        const u32 modifierAlpha=white[0][3];
        for(int layer=0;layer<2;++layer){
            auto inheritAlpha=[&](u8 flags)->u32{
                u32 v=0xff;
                if(flags&1){
                    u32 a=ref->p.color[layer][0][3];
                    if(ref->collisionCounter!=0xff&&ref->collisionCounter<rpm.collisionFadeAlpha)a=(a*ref->collisionCounter)/rpm.collisionFadeAlpha&0xff;
                    v=(a+1)*0xff>>8;
                }
                if(flags&2){
                    u32 a=ref->p.color[layer][1][3];
                    if(ref->collisionCounter!=0xff&&ref->collisionCounter<rpm.collisionFadeAlpha)a=(a*ref->collisionCounter)/rpm.collisionFadeAlpha&0xff;
                    v=(v*a+0xff)>>8;
                }
                if(flags&4)v=((v*(flickAlpha(*ref)&0xff)+0xff)*modifierAlpha+0xff)>>16;
                return v;
            };
            if(setting->alphaFlagPri)pt.p.color[layer][0][3]=u8((inheritAlpha(setting->alphaFlagPri)*setting->alpha)/100);
            if(setting->alphaFlagSec)pt.p.color[layer][1][3]=u8((inheritAlpha(setting->alphaFlagSec)*setting->alpha)/100);
        }
    }
    if(setting->color!=0){
        for(int layer=0;layer<2;++layer)for(int idx=0;idx<2;++idx)for(int c=0;c<3;++c)
            pt.p.color[layer][idx][c]=u8((ref->p.color[layer][idx][c]*u32(setting->color))/100);
    }
    if(setting->flag&InheritSetting::InheritRot){
        for(int i=0;i<3;++i){
            const float rad=ref->p.rotate[i]+0.02454369328916073f*float(ref->p.rotateOffset[i]);
            pt.p.rotateOffset[i]=u8(int(std::ceil(128.f*(rad/Pi)-0.5f)));
        }
    }
}

Particle* createParticle(System& s,ParticleManager& pm,u16 life,Vec3 pos,Vec3 vel,const Mtx34* space,float momentum,
                         const InheritSetting* setting,Particle* ref,u16 calcRemain){
    Particle* p=s.particles.alloc();
    if(!p)return nullptr;
    initParticle(*p,life,pos,vel,pm,space,momentum,setting,ref);
    p->calcRemain+=calcRemain;
    pm.particles.append(p);
    p->life=Active;
    ++s.liveParticles;
    return p;
}

// ---- emitter forms (EmitterForm::Emission) ----
u16 calcLife(u16 life,float lifeRandom,Emitter& em){
    if(lifeRandom==0.f)return life;
    const float x=lifeRandom*(float(life)*(float(em.random.rand())/65536.f));
    const float left=float(life)-x;
    if(left<1.f)return 1;
    if(left<=65535.f)return u16(life-int(x));
    return 0xffff;
}
Vec3 calcVelocity(Emitter& em,Vec3 normal,Vec3 fromOrigin,Vec3 fromYAxis){
    const EmitterParameter& p=em.prm;
    Vec3 v=p.velPowerRadiationDir==0.f?Vec3{}:fromOrigin*p.velPowerRadiationDir;
    if(p.velPowerYAxis!=0.f)v=v+fromYAxis*p.velPowerYAxis;
    if(p.velPowerRandomDir!=0.f){
        const u32 r1=em.random.rand(),r2=em.random.rand(),r3=em.random.rand();
        float sr,cr,sp,cp,sh,ch;
        sinCosRad(2.f*(Pi*(float(r1)/65536.f)),sr,cr);
        sinCosRad(2.f*(Pi*(float(r2)/65536.f)),sp,cp);
        sinCosRad(2.f*(Pi*(float(r3)/65536.f)),sh,ch);
        v.x+=p.velPowerRandomDir*(ch*cr*sp+sr*sh);
        v.y+=p.velPowerRandomDir*(sh*cr*sp-sr*ch);
        v.z+=cp*p.velPowerRandomDir*cr;
    }
    if(p.velPowerNormalDir!=0.f)v=v+normal*p.velPowerNormalDir;
    if(p.velPowerSpecDir!=0.f){
        if(p.velDiffusionSpecDir==0.f){
            const Vec3 d=transformCoord(rotXYZ(p.velSpecDir.x,p.velSpecDir.y,p.velSpecDir.z),{0,1,0});
            v=v+d*p.velPowerSpecDir;
        }else{
            const u32 r1=em.random.rand(),r2=em.random.rand();
            const Mtx34 a=rotXYZ(p.velDiffusionSpecDir*(float(r2)/65536.f),2.f*Pi*(float(r1)/65536.f),0.f);
            const Mtx34 m=mul(rotXYZ(p.velSpecDir.x,p.velSpecDir.y,p.velSpecDir.z),a);
            v=v+m.col(1)*p.velPowerSpecDir;
        }
    }
    if(p.velInitVelocityRandom!=0){
        const u32 r=em.random.rand();
        const float k=1.f-0.01f*float(p.velInitVelocityRandom)*(2.f*(float(r)/65536.f)-1.f);
        v=v*k;
    }
    return v;
}
void emitOne(System& s,Emitter& em,ParticleManager& pm,Vec3 pos,Vec3 vel,u16 life,float lifeRandom,const Mtx34* space){
    const float rnd=float(em.random.rand())/65536.f;
    const u16 l=calcLife(life,lifeRandom,em);
    const float momentum=1.f+0.01f*float(em.prm.velMomentumRandom)*rnd;
    createParticle(s,pm,l,pos,vel,space,momentum,&em.inherit,em.referenceParticle,em.calcRemain);
}
void emitPoint(System& s,Emitter& em,ParticleManager& pm,int count,u16 life,float lifeRandom,const Mtx34* space){
    for(int i=0;i<count;++i){
        const float rx=2.f*(float(em.random.rand())/65536.f)-1.f;
        Vec3 n;
        n.x=(rx<0.f?0.66f-0.34f*rx:0.66f+0.34f*rx)*rx;
        const float d=1.f-n.x*n.x;
        const float radius=0.f<d?std::sqrt(d):0.f;
        float sa,ca;sinCosRad(2.f*(Pi*(float(em.random.rand())/65536.f)),sa,ca);
        n.y=ca*radius;n.z=sa*radius;
        Vec3 fromY{n.x,0.f,n.z};normalize(fromY);
        const Vec3 vel=calcVelocity(em,n,n,fromY);
        emitOne(s,em,pm,{},vel,life,lifeRandom,space);
    }
}
void emitLine(System& s,Emitter& em,ParticleManager& pm,int count,u32 flags,const float* params,u16 life,float lifeRandom,const Mtx34* space){
    for(int i=0;i<count;++i){
        float t;
        if(!(flags&0x20000))t=float(em.random.rand())/65536.f;
        else if(count<2)t=0.f;
        else t=float(i)/float(count-1);
        if(flags&0x4000000)t-=0.5f;
        t*=params[0];
        float s1,c1,s2,c2,s3,c3;sinCosRad(params[1],s1,c1);sinCosRad(params[2],s2,c2);sinCosRad(params[3],s3,c3);
        const Vec3 pos{t*(s2*c1*c3+s1*s3),t*(-c3*s1+s3*c1*s2),t*(c1*c2)};
        const Vec3 fromY{pos.x,0.f,pos.z};
        const Vec3 vel=calcVelocity(em,{0,1,0},pos,fromY);
        emitOne(s,em,pm,pos,vel,life,lifeRandom,space);
    }
}
void emitDisc(System& s,Emitter& em,ParticleManager& pm,int count,u32 flags,const float* params,u16 life,float lifeRandom,const Mtx34* space){
    if(count<1)return;
    const float xr=FltEpsilon<std::fabs(params[0])?params[0]:FltEpsilon;
    const float zr=(flags&0x2000000)?xr:(FltEpsilon<std::fabs(params[4])?params[4]:FltEpsilon);
    const float base=(flags&0x40000)?params[2]:2.f*Pi*(float(em.random.rand())/65536.f);
    const bool uniform=flags&0x20000;
    float step=0.f,angle=0.f;
    if(uniform){
        const float span=std::fmod(params[3]-params[2],6.283185307179586f);
        if(span<0.0001917476038215682f||6.282993793487549f<span)step=(params[3]-params[2])/float(em.prm.emitDiv);
        else step=(params[3]-params[2])/float(int(em.prm.emitDiv)-1);
    }
    for(int i=0;i<count;++i){
        const float hollow=params[1]/100.f;
        const float u=float(em.random.rand())/65536.f;
        float k;
        if(!(flags&0x1000000))k=u+hollow*(1.f-u);
        else{const float a=u+(1.f-u)*(hollow*hollow);k=0.f<a?std::sqrt(a):0.f;}
        if(!uniform)angle=(params[3]-params[2])*(float(em.random.rand())/65536.f);
        float sa,ca;sinCosRad(base+angle,sa,ca);
        const Vec3 pos{xr*(sa*k),0.f,zr*(-ca*k)};
        const Vec3 out{sa,0.f,-ca};
        Vec3 normal{0,1,0};
        if(em.prm.velDiffusionEmitterNormal!=0.f){
            float sd,cd;sinCosRad(k*em.prm.velDiffusionEmitterNormal,sd,cd);
            normal={sd*sa,cd,-ca*sd};
        }
        const Vec3 vel=calcVelocity(em,normal,out,out);
        emitOne(s,em,pm,pos,vel,life,lifeRandom,space);
        if(uniform)angle+=step;
    }
}
void emitCylinder(System& s,Emitter& em,ParticleManager& pm,int count,u32 flags,const float* params,u16 life,float lifeRandom,const Mtx34* space){
    // NW4R EmitterFormCylinder::Emission/EmissionSub. Keep the source
    // random stream and uniform count-by-count rings (start charge / spins).
    if(count<1)return;
    const float xr=FltEpsilon<std::fabs(params[0])?params[0]:FltEpsilon;
    const float yr=FltEpsilon<std::fabs(params[4])?params[4]:FltEpsilon;
    const float zr=flags&0x2000000?xr:(FltEpsilon<std::fabs(params[5])?params[5]:FltEpsilon);
    const float base=flags&0x40000?params[2]:2.f*Pi*(float(em.random.rand())/65536.f);
    const bool uniform=flags&0x20000;
    float step=0.f;
    if(uniform){
        const float span=std::fmod(params[3]-params[2],2.f*Pi);
        step=(params[3]-params[2])/float(span<0.0001917476038215682f||6.282993793487549f<span?em.prm.emitDiv:int(em.prm.emitDiv)-1);
    }
    const int rings=uniform?count:1;
    for(int ring=0;ring<rings;++ring){
        float angle=0;
        const float height=count>1?(float(ring)/float(count-1)-.5f)*yr*2.f:0.f;
        for(int i=0;i<count;++i){
            float radius=float(em.random.rand())/65536.f;
            const float hollow=params[1]/100.f;
            radius=flags&0x1000000?std::sqrt(radius+hollow*hollow*(1-radius)):radius+hollow*(1-radius);
            if(!uniform)angle=(params[3]-params[2])*(float(em.random.rand())/65536.f);
            float sa,ca;sinCosRad(base+angle,sa,ca);
            const Vec3 fromY{sa,0.f,-ca};
            const Vec3 pos{sa*radius*xr,uniform?height:(2.f*(float(em.random.rand())/65536.f)-1.f)*yr,-ca*radius*zr};
            Vec3 normal{pos.x,0.f,pos.z},origin=pos;
            normalize(normal);normalize(origin);
            const Vec3 velocity=calcVelocity(em,normal,origin,fromY);
            emitOne(s,em,pm,pos,velocity,life,lifeRandom,space);
            if(uniform)angle+=step;
        }
    }
}
void emitSphere(System& s,Emitter& em,ParticleManager& pm,int count,u32 flags,const float* params,u16 life,float lifeRandom,const Mtx34* space){
    if(count<1)return;
    const float hollow=params[1]/100.f;
    const float xr=FltEpsilon<std::fabs(params[0])?params[0]:FltEpsilon;
    float yr=xr,zr=xr;
    if(!(flags&0x2000000)){
        yr=FltEpsilon<std::fabs(params[4])?params[4]:FltEpsilon;
        zr=FltEpsilon<std::fabs(params[5])?params[5]:FltEpsilon;
    }
    float base=params[2];
    if(!(flags&0x40000))base=base+2.f*Pi*(float(em.random.rand())/65536.f);
    auto emitAt=[&](Vec3 pos,Vec3 fromY){
        Vec3 n=pos;normalize(n);
        normalize(fromY);
        const Vec3 vel=calcVelocity(em,n,n,fromY);
        emitOne(s,em,pm,pos,vel,life,lifeRandom,space);
    };
    if(!(flags&0x20000)){
        for(int i=0;i<count;++i){
            float r=float(em.random.rand())/65536.f;
            if(!(flags&0x1000000))r=r+(params[1]/100.f)*(1.f-r);
            else{r=1.f-r*(r*r);r=r+(params[1]/100.f)*(1.f-r);}
            const float v=float(em.random.rand())/65536.f,w=float(em.random.rand())/65536.f;
            float sl,cl,sn,cn;
            sinCosRad(1.5707963705062866f+Pi*w,sl,cl);
            sinCosRad(base+(params[3]-params[2])*v,sn,cn);
            const Vec3 pos{sn*-cl*(xr*r),-sl*(yr*r),cn*cl*(zr*r)};
            emitAt(pos,{pos.x,0.f,pos.z});
        }
        return;
    }
    // Uniform: latitude rings of 1, 4, 8, ... points, then back down (count*count*4+2 particles).
    const u32 rings=u32(count)*2;
    const float span=params[3]-params[2];
    const float m=std::fmod(span,6.283185307179586f);
    const bool full=m<0.0001917476038215682f||6.282993793487549f<m;
    int ring=0,doubled=0;u32 inRing=0,perRing=1;
    for(int i=0;i<count*count*4+2;++i){
        const float lat=1.5707963705062866f+Pi*(float(ring)/float(int(rings)));
        float lon=base;
        if(perRing!=1)lon=base+span*(float(int(inRing))/float(int(full?perRing:perRing-1)));
        ++inRing;
        u32 nextPer=perRing;
        if(inRing==perRing){
            doubled+=2;inRing=0;++ring;
            if(doubled<int(rings+1))nextPer=perRing!=1?perRing+4:perRing+3;
            else nextPer=perRing!=4?perRing-4:1;
        }
        float r=float(em.random.rand())/65536.f;
        if(flags&0x1000000)r=1.f-r*(r*r);
        r=hollow+r*(1.f-hollow);
        float sn,cn;sinCosRad(lon,sn,cn);
        sn=sn*-1.f;
        float sl,cl;sinCosRad(lat,sl,cl);
        Vec3 pos{sn*cl*(xr*r),sl*(-yr*r),cl*cn*(zr*r)};
        Vec3 fromY{sn,0.f,cn};
        if(std::cos(lat)<0.f){fromY.x=-fromY.x;fromY.z=-fromY.z;}
        Vec3 n=pos;normalize(n);
        const Vec3 vel=calcVelocity(em,n,n,fromY);
        emitOne(s,em,pm,pos,vel,life,lifeRandom,space);
        perRing=nextPer;
    }
}
// NW4R EmitterFormCube random-volume/surface branch, USA
// 8003b4d4..8003bde0; EmissionSub 8003a280. Uniform lattice emission
// remains rejected by the cooker until its distinct ordering is ported.
void emitCube(System& s,Emitter& em,ParticleManager& pm,int count,u32 flags,const float* params,u16 life,float lifeRandom,const Mtx34* space){
    if(flags&0x20000)return;
    const float xr=std::max(params[0],0.00001f),yr=std::max(params[1],0.00001f),zr=std::max(params[2],0.00001f);
    const float inner=params[3]==0.f?0.00001f:0.01f*params[3],outer=1.f-inner;
    float diffusion=em.prm.velDiffusionEmitterNormal;
    if(std::fabs(diffusion-Pi)<3.74507038714e-7f)diffusion-=3.74507038714e-7f;
    auto random=[&](){return float(em.random.rand())/65536.f;};
    auto signedRandom=[&](){return 2.f*random()-1.f;};
    auto shell=[](float v){return v>=0.f?1.f-v:-1.f-v;};
    for(int i=0;i<count;++i){
        Vec3 p;
        if(inner==0.f){p={signedRandom(),signedRandom(),signedRandom()};}
        else if(inner==1.f){
            const unsigned face=em.random.rand()%6;
            const float a=signedRandom(),b=signedRandom();
            if(face<2)p={a,b,face==0?1.f:-1.f};
            else if(face<4)p={a,face==2?1.f:-1.f,b};
            else p={face==4?1.f:-1.f,a,b};
        }else{
            const float r=random();
            const float yVolume=outer*(xr*yr)*zr;
            const float zVolume=outer*(inner*(xr*yr)*zr);
            const float xVolume=inner*((inner*(yr*(xr*outer)))*zr);
            const float choice=(xVolume+(yVolume+zVolume))*r;
            if(choice<yVolume){
                p.x=signedRandom();p.y=shell(outer*signedRandom());p.z=signedRandom();
            }else if(choice<yVolume+zVolume){
                p.x=signedRandom();p.y=inner*signedRandom();p.z=shell(outer*signedRandom());
            }else{
                p.x=shell(outer*signedRandom());p.y=inner*signedRandom();p.z=inner*signedRandom();
            }
        }
        Vec3 normal{};
        if(p.y>=0.f&&p.y>=std::fabs(p.x)&&p.y>=std::fabs(p.z))normal.y=1;
        else if(p.y<0.f&&-p.y>=std::fabs(p.x)&&-p.y>=std::fabs(p.z))normal.y=-1;
        else if(p.x>=0.f&&p.x>=std::fabs(p.y)&&p.x>=std::fabs(p.z))normal.x=1;
        else if(p.x<0.f&&-p.x>=std::fabs(p.y)&&-p.x>=std::fabs(p.z))normal.x=-1;
        else normal.z=p.z>=0.f?1.f:-1.f;
        p={p.x*xr,p.y*yr,p.z*zr};
        if(diffusion!=0.f){
            auto axial=[&](float a,float b){return std::sqrt(a*a+b*b)/std::tan(std::max(std::fabs(a),std::fabs(b))*diffusion);};
            if(normal.x!=0.f)normal={normal.x*axial(p.y,p.z),p.y,p.z};
            else if(normal.y!=0.f)normal={p.x,normal.y*axial(p.x,p.z),p.z};
            else normal={p.x,p.y,normal.z*axial(p.x,p.y)};
        }
        normalize(normal);
        Vec3 origin=p;
        if(dot(origin,origin)<=FltMin)origin={signedRandom(),signedRandom(),signedRandom()};
        normalize(origin);
        Vec3 fromY{p.x,0,p.z};
        if(dot(fromY,fromY)<=FltMin)fromY={signedRandom(),0,signedRandom()};
        normalize(fromY);
        const auto velocity=calcVelocity(em,normal,origin,fromY);
        emitOne(s,em,pm,p,velocity,life,lifeRandom,space);
    }
}
void formEmission(System& s,Emitter& em,ParticleManager& pm,int count,u32 flags,const float* params,u16 life,float lifeRandom,const Mtx34* space){
    switch(em.form){
    case 9:emitPoint(s,em,pm,count,life,lifeRandom,space);break;
    case 1:emitLine(s,em,pm,count,flags,params,life,lifeRandom,space);break;
    case 0:emitDisc(s,em,pm,count,flags,params,life,lifeRandom,space);break;
    case 5:emitCube(s,em,pm,count,flags,params,life,lifeRandom,space);break;
    case 7:emitCylinder(s,em,pm,count,flags,params,life,lifeRandom,space);break;
    case 8:emitSphere(s,em,pm,count,flags,params,life,lifeRandom,space);break;
    default:break;
    }
}

// ---- emitters ----
void initializeDatas(System& s,Emitter& em,const EmitterResource& res){
    const EmitterDesc& d=res.desc;
    em.tick=0;em.life=Active;em.res=&res;em.eval=EvalWait;
    em.calcRemain=d.emitPast;
    em.prm.comFlags=d.commonFlag;em.prm.emitFlags=d.emitFlag;em.prm.emitSpan=d.emitLife;
    em.waitTime=d.emitStart;em.intervalWait=0;
    em.prm.lodFar=float(d.lodFar)/100.f;em.prm.lodNear=float(d.lodNear)/100.f;em.prm.lodMinEmit=float(d.lodMinEmit)/100.f;
    em.prm.emitRatio=d.emit;em.prm.emitRandom=float(d.emitRandom)/100.f;
    em.prm.emitInterval=d.emitInterval;em.prm.emitDiv=d.emitDiv;
    em.prm.emitCount=0.f;em.firstEmission=true;
    em.prm.emitIntervalRandom=float(d.emitIntervalRandom)/100.f;
    em.prm.translate=d.translate;em.prm.scale=d.scale;em.prm.rotate=d.rotate;
    em.prm.inherit=3;em.prm.inheritTranslate=100;
    em.prm.velInitVelocityRandom=d.velInitVelocityRandom;em.prm.velMomentumRandom=d.velMomentumRandom;
    em.prm.velPowerRadiationDir=d.velPowerRadiationDir;em.prm.velPowerYAxis=d.velPowerYAxis;
    em.prm.velPowerRandomDir=d.velPowerRandomDir;em.prm.velPowerNormalDir=d.velPowerNormalDir;
    em.prm.velDiffusionEmitterNormal=d.velDiffusionEmitterNormal;em.prm.velPowerSpecDir=d.velPowerSpecDir;
    em.prm.velDiffusionSpecDir=d.velDiffusionSpecDir;em.prm.velSpecDir=d.velSpecDir;
    em.seed=u16(d.randomSeed);
    if(!em.seed)em.seed=u16(s.random.rand());
    em.random.srand(em.seed);
    for(int i=0;i<6;++i)em.prm.params[i]=d.commonParam[i];
    em.mtxDirty=true;em.referenceParticle=nullptr;
    em.form=u8(d.emitFlag&0xff);
    em.parent=nullptr;em.effect=nullptr;
}
ParticleManager* newManager(System& s,Emitter& em,const EmitterResource& res){
    ParticleManager* pm=s.managers.alloc();
    if(!pm)return nullptr;
    pm->refs=0;pm->life=Active;pm->emitter=&em;++em.refs;pm->res=&res;pm->mtxDirty=true;pm->lastCalced=nullptr;
    pm->modifierScale[0]=pm->modifierScale[1]=1.f;pm->modifierRotate={};
    // ParticleManager::Initialize (0x80033550) copies the draw setting's lighting type and
    // ambient into the modifier's SimpleLight.
    pm->lightType=res.desc.draw.lightingType;
    std::copy(res.desc.draw.lightingAmbient,res.desc.draw.lightingAmbient+4,pm->lightAmbient);
    return pm;
}
bool initializeEmitter(System& s,Emitter& em,Effect& e,const EmitterResource& res,u8 weight){
    em.refs=0;em.life=Active;
    initializeDatas(s,em,res);
    em.effect=&e;
    ParticleManager* pm=newManager(s,em,res);
    if(!pm)return false;
    ++e.refs;
    em.managers.append(pm);
    pm->life=Active;
    pm->flag=0;
    const u32 common=res.desc.commonFlag;
    if(common&EmitterDesc::InheritPtclScale)pm->flag|=1;
    if(common&EmitterDesc::InheritPtclRot)pm->flag|=2;
    pm->inheritTranslate=res.desc.inheritPtclTranslate;
    if(common&EmitterDesc::InheritPtclAxis)pm->flag|=4;
    pm->weight=weight;
    drawAdd(e,pm);
    return true;
}
Emitter* effectCreateEmitter(System& s,Effect& e,const EmitterResource& res,u8 weight,u16 calcRemain){
    Emitter* em=s.emitters.alloc();
    if(!em)return nullptr;
    if(!initializeEmitter(s,*em,e,res,weight)){s.emitters.release(em);return nullptr;}
    e.emitters.append(em);
    em->life=Active;
    em->calcRemain+=calcRemain;
    return em;
}

// Translation that puts a child emitter at `pos` (a particle position) given its rotation and scale.
Vec3 childTranslate(Emitter& child,ParticleManager* pm,const Vec3* pos){
    const Vec3 save=child.prm.translate;
    child.prm.translate={};
    setMtxDirty(child);
    Mtx34 m=scaleBy(rotXYZ(child.prm.rotate.x,child.prm.rotate.y,child.prm.rotate.z),child.prm.scale);
    Mtx34 inv;inverse(inv,emitterGlobalMtx(child));
    m=mul(m,inv);
    if(pm)m=mul(m,managerGlobalMtx(*pm));
    if(pos)m=translateBy(m,*pos);
    Mtx34 rs=scaleBy(rotXYZ(child.prm.rotate.x,child.prm.rotate.y,child.prm.rotate.z),child.prm.scale);
    inverse(rs,rs);
    m=mul(m,rs);
    return {save.x+m.m[0][3],save.y+m.m[1][3],save.z+m.m[2][3]};
}

Emitter* createEmitter(System& s,Emitter& self,const EmitterResource& res,const InheritSetting& setting,Particle* particle,u16 calcRemain,const Vec3* position){
    if(!(res.desc.commonFlag&EmitterDesc::Relocated))return nullptr;
    Emitter* em=effectCreateEmitter(s,*self.effect,res,setting.weight,0);
    if(!em)return nullptr;
    em->calcRemain+=calcRemain;
    em->parent=&self;++self.refs;
    em->prm.inherit=0;
    const u32 common=self.prm.comFlags;
    if(common&EmitterDesc::EmitInheritScale)em->prm.inherit|=1;
    if(common&EmitterDesc::EmitInheritRot)em->prm.inherit|=2;
    em->prm.inheritTranslate=self.res->desc.inheritChildEmitTranslate;
    if(common&EmitterDesc::EmitInheritAxis)em->prm.inherit|=4;
    const Vec3* pos=position?position:particle?&particle->p.position:nullptr;
    em->prm.translate=childTranslate(*em,particle?particle->manager:nullptr,pos);
    setMtxDirty(*em);
    if(setting.any()){
        em->referenceParticle=particle;++particle->refs;
        em->inherit=setting;
    }
    return em;
}

void emission(System& s,Emitter& em,ParticleManager& pm){
    if(em.intervalWait!=0){--em.intervalWait;return;}
    em.intervalWait=em.prm.emitInterval;
    if(em.prm.emitIntervalRandom!=0.f){
        const float rnd=float(em.random.rand())/65536.f;
        em.intervalWait=u16(em.intervalWait+u16(int(std::ceil((float(em.prm.emitInterval)*em.prm.emitIntervalRandom-1.f)*rnd))));
    }
    if(!(em.prm.emitFlags&0x20000)){
        float count;
        if(em.prm.emitRandom==0.f)count=em.prm.emitRatio;
        else{
            const float rnd=float(em.random.rand())/65536.f;
            count=em.prm.emitRatio+em.prm.emitRatio*em.prm.emitRandom*(2.f*rnd-1.f);
        }
        // EMIT_FLAG_8 (LOD by camera distance) is unused by RKRace emitters (lod bytes are 0).
        em.prm.emitCount+=count;
        if(em.firstEmission&&em.prm.emitRatio!=0.f&&em.prm.emitCount<1.f)em.prm.emitCount=1.f;
    }else em.prm.emitCount=float(em.prm.emitDiv);
    if(1.f<=em.prm.emitCount){
        const EmitterDesc& d=em.res->desc;
        if(em.form!=0xff){
            // Waiting emitters and fractional emission rates do not consume this
            // transform. Preserve the RNG/count updates above, then calculate it
            // only when a form actually emits particles.
            Mtx34 inv;inverse(inv,managerGlobalMtx(pm));
            const Mtx34 space=mul(inv,emitterGlobalMtx(em));
            formEmission(s,em,pm,int(em.prm.emitCount),em.prm.emitFlags,em.prm.params,d.ptclLife,float(d.ptclLifeRandom)/100.f,&space);
        }
        em.prm.emitCount-=float(int(em.prm.emitCount));
    }
    if(em.firstEmission)em.firstEmission=false;
}

void calcEmitter(System& s,Emitter& em){
    if((em.prm.comFlags&EmitterDesc::DisableCalc)||em.life!=Active||em.eval!=EvalWait||em.waitTime!=0)return;
    const bool maxLife=em.prm.comFlags&EmitterDesc::MaxLife;
    if(!maxLife?em.tick>=em.prm.emitSpan:em.tick==0xffffffffu){retireEmitter(s,&em);return;}
    const u32 span=maxLife?0xffffffffu:em.prm.emitSpan;
    bool dirty=false;
    const auto& tracks=em.res->emitTracks;
    for(size_t i=em.tick==0?0:em.res->emitInitTracks;i<tracks.size();++i){
        const u8* c=tracks[i];
        if((c[4]&8)||u8(c[0]+0x55)>=2)continue;
        animF32(c,reinterpret_cast<float*>(reinterpret_cast<u8*>(&em.prm)+c[1]),em.tick,em.seed,span);
        if(c[1]>0x6f)dirty=true;
    }
    if(dirty)setMtxDirty(em);
}
void calcBillboard(System& s,Emitter& em){
    if(em.life!=Active&&em.life!=Wait)return;
    if(!(em.prm.emitFlags&0x10000)&&!(em.prm.emitFlags&0x8000))return;
    em.prm.rotate={};
    em.mtxDirty=true;
    const Mtx34 glb=emitterGlobalMtx(em);
    Mtx34 view=mul(s.cameraMtx,glb);
    Mtx34 t=view;
    auto colLen=[&](int c){return std::sqrt(t.m[2][c]*t.m[2][c]+t.m[0][c]*t.m[0][c]+t.m[1][c]*t.m[1][c]);};
    const float l0=colLen(0),l1=colLen(1),l2=colLen(2);
    t.m[0][0]=l0;t.m[1][0]=0;t.m[2][0]=0;
    t.m[1][1]=l1;t.m[0][1]=0;t.m[2][1]=0;
    t.m[2][2]=l2;t.m[0][2]=0;t.m[1][2]=0;
    if(em.prm.emitFlags&0x8000)t=mul(t,rotXYZ(1.5707963705062866f,0,0));
    Mtx34 inv;inverse(inv,view);
    Mtx34 m=mul(inv,t);
    // 0x8002973c divides by the emitter rotation (zeroed above; 1/0 reads as 100000)
    // where ogws divides by the scale; rotationOf() is unaffected by the uniform factor.
    const Vec3 r=em.prm.rotate;
    const Vec3 rs{r.x==0.f?100000.f:1.f/r.x,r.y==0.f?100000.f:1.f/r.y,r.z==0.f?100000.f:1.f/r.z};
    for(int i=0;i<3;++i){m.m[i][0]*=rs.x;m.m[i][1]*=rs.y;m.m[i][2]*=rs.z;}
    m=scaleRows(m,em.prm.scale);
    em.prm.rotate=rotationOf(m);
    setMtxDirty(em);
}
void calcEmission(System& s,Emitter& em){
    if((em.prm.comFlags&EmitterDesc::DisableCalc)||em.eval!=EvalWait)return;
    em.eval=EvalDone;
    if(em.calcRemain)em.effect->flags|=0x10000;
    if(em.life==Active){
        ParticleManager& pm=*em.managers.head;
        if(em.waitTime==0){emission(s,em,pm);++em.tick;}
        else --em.waitTime;
    }else if(em.waitTime==0)++em.tick;
    else --em.waitTime;
}

ParticleManager* findManager(Emitter& em,const EmitterResource& res,u32 flags,s8 inheritT,u8 weight){
    for(ParticleManager* pm=em.managers.head;pm;pm=pm->next)
        if(pm->res==&res&&(pm->flag&7)==flags&&pm->inheritTranslate==inheritT&&pm->weight==weight)return pm;
    return nullptr;
}

void createEmitterTmp(System& s,Emitter& self,const EmitterResource& res,const InheritSetting& setting,Particle* particle,u16 calcRemain,const Vec3* position){
    if(!(res.desc.commonFlag&EmitterDesc::Relocated))return;
    Emitter tmp;
    initializeDatas(s,tmp,res);
    tmp.calcRemain+=calcRemain;
    tmp.effect=self.effect;tmp.parent=&self;
    if((setting.flag&InheritSetting::FollowEmit)&&setting.speed!=0){
        // MKW: the move direction is the last step's displacement.
        Vec3 dir=particle->p.position-particle->p.prevPosition;
        dir=transformNormal(noTranslate(managerGlobalMtx(*particle->manager)),dir);
        if(normalize(dir)){
            if(setting.speed<0)dir=dir*-1.f;
            Mtx34 m=mul(dirMtxY(dir),rotXYZ(tmp.prm.rotate.x,tmp.prm.rotate.y,tmp.prm.rotate.z));
            tmp.prm.rotate=rotationOf(m);
        }
    }
    const Vec3* pos=position?position:particle?&particle->p.position:nullptr;
    tmp.prm.translate=childTranslate(tmp,particle?particle->manager:nullptr,pos);
    setMtxDirty(tmp);
    calcEmitter(s,tmp);
    calcBillboard(s,tmp);
    const u32 common=self.prm.comFlags,childCommon=res.desc.commonFlag;
    u32 flags=0;
    if((common&EmitterDesc::EmitInheritScale)&&(childCommon&EmitterDesc::InheritPtclScale))flags|=1;
    if((common&EmitterDesc::EmitInheritRot)&&(childCommon&EmitterDesc::InheritPtclRot))flags|=2;
    if((common&EmitterDesc::EmitInheritAxis)||(childCommon&EmitterDesc::InheritPtclAxis))flags|=4;
    const int a=res.desc.inheritPtclTranslate,b=self.res->desc.inheritChildEmitTranslate;
    s8 inheritT;
    if(b==0||a==0)inheritT=0;
    else if(b==100)inheritT=s8(a);
    else if(a==100)inheritT=s8(b);
    else inheritT=s8(b*a/100);
    ParticleManager* pm=findManager(self,res,flags,inheritT,setting.weight);
    if(!pm){
        pm=newManager(s,self,res);
        if(!pm)return;
        self.managers.append(pm);
        pm->life=Active;
        pm->flag=flags;pm->inheritTranslate=inheritT;pm->weight=setting.weight;
        drawAdd(*self.effect,pm);
    }
    if(setting.any()){tmp.referenceParticle=particle;++particle->refs;tmp.inherit=setting;}
    else tmp.referenceParticle=nullptr;
    emission(s,tmp,*pm);
    tmp.parent=nullptr;tmp.effect=nullptr;
    if(tmp.referenceParticle){unref(s,particle);tmp.referenceParticle=nullptr;}
    if(self.life!=Active&&pm->life==Active)retireManager(s,pm);
}

void executeQueue(System& s){
    for(u32 i=0;i<s.queueCount;++i){
        System::Creation& c=s.queue[i];
        Emitter& em=*c.particle->manager->emitter;
        const Vec3* pos=(c.flags&1)?&c.position:nullptr;
        if(c.type==0)createEmitterTmp(s,em,*c.res,c.setting,c.particle,c.calcRemain,pos);
        else createEmitter(s,em,*c.res,c.setting,c.particle,c.calcRemain,pos);
        unref(s,c.particle);
    }
    s.queueCount=0;
}
void addCreation(System& s,u8 type,const InheritSetting& setting,Particle& particle,const EmitterResource& res,u16 calcRemain,const Vec3* position){
    if(s.queueCount>=System::QueueCapacity)return;
    System::Creation& c=s.queue[s.queueCount++];
    c.type=type;c.flags=position?1:0;c.calcRemain=calcRemain;c.setting=setting;c.particle=&particle;++particle.refs;c.res=&res;
    c.position=position?*position:Vec3{};
}
InheritSetting readSetting(const u8* p){
    InheritSetting s;
    s.speed=s16(be16(p));s.scale=p[2];s.alpha=p[3];s.color=p[4];s.weight=p[5];s.type=p[6];s.flag=p[7];
    s.alphaFlagPri=p[8];s.alphaFlagSec=p[9];
    return s;
}

// ---- particle manager calc ----
u8 planeStatus(Vec3 p,u8 option){
    float y;
    switch(option){case 1:y=p.z;break;case 2:y=-p.x;break;default:y=p.y;break;}
    if(y>0.f)return 2;
    if(y<0.f)return 3;
    return 1;
}
// Shape 0 (plane) segment test of 0x8002f1e0; returns the hit fraction or 2 for none.
float planeHit(const u8* info,const Mtx34& toLocal,Vec3 next,Vec3 cur,Vec3& dir,Vec3& vel,Vec3& hit,Vec3& hitLocal){
    const u8 option=info[0x2a];
    if(option>2)return 2.f;
    const int axis=option==0?1:option==1?2:0;
    auto at=[&](Vec3& v)->float&{return axis==0?v.x:axis==1?v.y:v.z;};
    const float n=at(next),c=at(cur);
    if(!(FltEpsilon<=std::fabs(c)||FltEpsilon<=std::fabs(n)))return 2.f;
    float side;
    if(c<=0.f||FltEpsilon<=n){
        if(0.f<=c||n<=FltEpsilon)return 2.f;
        side=-1.f;
    }else side=1.f;
    const float t=-c/at(dir);
    hit=cur+(next-cur)*t;
    if(be16(info+0x2c)&2){at(vel)=at(vel)*-1.f;at(dir)=at(dir)*-1.f;at(hit)=at(hit)+side*FltEpsilon;}
    hitLocal=transformCoord(toLocal,hit);
    return t;
}
// Postfield (0x80031b40, plane shapes): returns whether the caller integrates the movement.
bool postField(System& s,Particle& p,const float* transform,const u8* info,const EmitterResource* child,const Mtx34& emToGlb,
               const Mtx34& glbToLoc,Vec3 pos,Vec3& addPos,Vec3& vel,bool& kill){
    const u16 flags=be16(info+0x2c);
    Mtx34 m=scaleRows(Mtx34::identity(),{transform[0],transform[1],transform[2]});
    m=mul(rotXYZ(transform[3],transform[4],transform[5]),m);
    m=transApply(m,{transform[6],transform[7],transform[8]});
    if(flags&1)m=transApply(m,emToGlb.translate());
    m=mul(glbToLoc,m);
    const Mtx34 mNT=noTranslate(m);
    Mtx34 inv;inverse(inv,m);
    const Mtx34 invNT=noTranslate(inv);
    Vec3 cur=transformCoord(inv,pos);
    u8 status=planeStatus(cur,info[0x2a]);
    const u8 prev=p.p.collisionStatus;
    p.p.collisionStatus=status;
    const float limit=bef(info+0x24);const u8 limitMode=info[0x28];
    auto clamp=[&](Vec3& v){
        if(limitMode==1){if(limit*limit<dot(v,v)){normalize(v);v=v*limit;}}
        else if(limitMode==2&&limit*limit<dot(v,v))kill=true;
    };
    clamp(vel);
    if(kill)return true;
    if(p.tick<be16(info+0x2e)||flags==0)return true;
    const u8 kind=info[0x2b];
    auto spawn=[&](const Vec3* at){
        const InheritSetting setting=readSetting(info+0x3c);
        if((flags&0x10)&&child)addCreation(s,0,setting,p,*child,p.calcRemain,at);
        if((flags&0x20)&&child)addCreation(s,1,setting,p,*child,p.calcRemain,at);
    };
    if(kind==0){
        Vec3 next=transformCoord(inv,pos+(addPos+vel)*p.p.momentum);
        Vec3 velC=transformNormal(invNT,vel);
        for(int iter=0;iter<5;++iter){
            const Vec3 d=cur-next;
            bool hitSomething=false;
            Vec3 hitC{},hitLocal{},newPos{},newVel{};
            if(1.4210854715202004e-14f<=dot(d,d)){
                Vec3 dir=next-cur,velWork=velC;
                const float t=planeHit(info,m,next,cur,dir,velWork,hitC,hitLocal);
                if(t<=1.5f){
                    hitSomething=true;
                    newPos=hitLocal;newVel=vel;
                    if((flags&2)||(flags&8)||((flags&4)&&u8(status-1)<2)){
                        newVel=transformCoord(mNT,velWork);
                        newVel={newVel.x*bef(info+0x30),newVel.y*bef(info+0x34),newVel.z*bef(info+0x38)};
                        Vec3 rest=transformCoord(mNT,dir*(1.f-t));
                        rest={rest.x*bef(info+0x30),rest.y*bef(info+0x34),rest.z*bef(info+0x38)};
                        newPos=hitLocal+rest;
                    }
                    if(flags&0x40){kill=true;newPos=hitLocal;}
                    if(flags&0x30)spawn(&hitLocal);
                }
            }
            if(!hitSomething){
                if(iter==0){
                    if(be16(info+0x2e)<p.tick&&((prev==2&&(status==3||status==1))||(prev==3&&u8(status-1)<2))){
                        if(flags&0x40)kill=true;
                        spawn(nullptr);
                    }
                    return true;
                }
                clamp(p.p.velocity);
                return false;
            }
            p.p.position=newPos;p.p.velocity=newVel;
            p.p.collisionStatus|=0x80;
            if(kill)return false;
            cur=hitC;status=1;
            next=transformCoord(inv,newPos);velC=transformNormal(invNT,newVel);
        }
        kill=true;return false;
    }
    if(kind==1&&status!=2)return true;
    if(kind==2&&status!=3)return true;
    if(flags&8){
        const Vec3 k{bef(info+0x30),bef(info+0x34),bef(info+0x38)};
        vel={vel.x*k.x,vel.y*k.y,vel.z*k.z};addPos={addPos.x*k.x,addPos.y*k.y,addPos.z*k.z};
        clamp(vel);
    }
    if(flags&0x40)kill=true;
    spawn(nullptr);
    return true;
}

void calcManager(System& s,ParticleManager& pm){
    Particle* first=pm.lastCalced?pm.lastCalced->next:pm.particles.head;
    if(!first)return;
    // Colour, size and texture curves never consume field-space transforms.
    // Child emitters are queued until after this manager's calculation, so the
    // transform sources stay unchanged while evaluating these particle curves.
    Mtx34 locToGlb,glbToLoc,emToGlb,locToEm,glbToLocNT,locToEmNT,emToLocNT;
    float emScale=1.f;
    bool haveFieldSpace=false;
    auto fieldSpace=[&]{
        if(haveFieldSpace)return;
        haveFieldSpace=true;
        locToGlb=managerGlobalMtx(pm);
        glbToLoc=Mtx34::identity();inverse(glbToLoc,locToGlb);
        emToGlb=emitterGlobalMtx(*pm.emitter);
        emScale=length(transformNormal(noTranslate(emToGlb),{0.5773502588272095f,0.5773502588272095f,0.5773502588272095f}));
        const Mtx34 emToLoc=mul(glbToLoc,emToGlb);
        locToEm=Mtx34::identity();inverse(locToEm,emToLoc);
        glbToLocNT=noTranslate(glbToLoc);locToEmNT=noTranslate(locToEm);emToLocNT=noTranslate(emToLoc);
    };
    Emitter& em=*pm.emitter;
    const auto& tracks=pm.res->ptclTracks;
    for(Particle* p=first;p;){
        Particle* next=p->next;
        if(p->life!=Active||p->eval!=EvalWait){p=next;continue;}
        p->eval=EvalDone;
        if(p->calcRemain)em.effect->flags|=0x10000;
        const Vec3 vel=p->p.velocity,pos=p->p.position;
        const Vec3 moved=p->p.position-p->p.prevPosition;
        p->p.prevPosition=p->p.position;
        if(p->tick>=p->lifeSpan){retireParticle(s,p);p=next;continue;}
        Vec3 addVel{},addPos{};
        bool post=false;
        float postTransform[9]{};const u8* postInfo=nullptr;const EmitterResource* postChild=nullptr;
        for(size_t i=p->tick==0?0:pm.res->ptclInitTracks;i<tracks.size();++i){
            const u8* c=tracks[i];
            if((c[4]&8)||(i<32&&(em.effect->stoppedParticleTracks>>i&1u)))continue;
            u32 tick,life;u16 seed;
            if(!(c[4]&0x10)){tick=p->tick;life=p->lifeSpan;seed=p->seed;}
            else{tick=em.tick;life=(em.prm.comFlags&EmitterDesc::MaxLife)?0xffffffffu:em.prm.emitSpan;seed=em.seed;}
            if(u8(c[0]+0x55)>=2)continue;
            const u8 target=c[1];
            switch(c[2]){
            case 4:animTexture(c,*p,tick,seed,life);break;
            case 5:animChild(c,*p,tick,seed,life);break;
            case 6:animRotate(c,p->p.rotate,tick,seed,life);break;
            case 0:animU8(c,reinterpret_cast<u8*>(&p->p)+target,tick,seed,life);break;
            case 3:animF32(c,reinterpret_cast<float*>(reinterpret_cast<u8*>(&p->p)+target),tick,seed,life);break;
            case 7:{
                fieldSpace();
                const u8* info=c+0x20+be32(c+0xc)+be32(c+0x10)+be32(c+0x14)+be32(c+0x18);
                const u8 space=info[0],addTo=info[1],flags=info[3];
                float power=bef(info+4);
                const float f8=bef(info+8),fc=bef(info+0xc),f10=bef(info+0x10);
                if(be32(c+0xc))animF32(c,&power,tick,seed,life);
                Vec3 fpos{};
                if(target==6||u8(target-2)<3){
                    if(space==1)fpos=transformCoord(locToEm,pos);
                    else fpos=transformCoord(locToGlb,pos)-transformCoord(emToGlb,{});
                }
                Vec3 v{};
                if(target==1)v=vel*(power-1.f);
                else if(target==0)v=rotationToVecY({f8,fc,f10})*power;
                else if(target==3){
                    // PC retail ParticleManager::Calc 800345c8..8003464c:
                    // Newton field points toward info[3..5]; negative power
                    // repels. Outside the authored radius, strength falls as
                    // radius squared / distance squared (ink clearing).
                    v=Vec3{fc,f10,bef(info+0x14)}-fpos;
                    const float distanceSquared=dot(v,v),radiusSquared=f8*f8;
                    normalize(v);
                    v=v*power;
                    if(distanceSquared>radiusSquared)v=v*(radiusSquared/distanceSquared);
                }
                else if(target==6){
                    const Mtx34 r=rotAxis(rotationToVecY({f8,fc,f10}),power);
                    v=transformCoord(r,fpos)-fpos;
                }else if(target==7){
                    const u32 r=curveHash(seed,be16(c+6),0,tick&0xffff);
                    const u32 interval=be16(info+0xc)+1u;
                    bool apply;
                    if(!(c[4]&0x10))apply=tick!=0&&tick==(tick/interval)*interval;
                    else apply=p->tick==0||tick==(tick/interval)*interval;
                    if(apply){
                        if(flags&1){
                            Vec3 base=vel;
                            if(addTo==1&&p->tick!=0)base=moved*(1.f/p->p.momentum);
                            power=length(transformNormal(locToEmNT,base));
                        }
                        if(!(flags&2)){
                            Vec3 dir=transformNormal(locToEmNT,p->tick==0?vel:moved);
                            if(!normalize(dir))dir.y=1.f;
                            const Mtx34 dm=dirMtxY(dir);
                            if(f8==0.f)v={0.f,(flags&1)?power:float(s16(r>>16))/32768.f*power,0.f};
                            else{
                                const u32 r1=r*0x343fdu+0x269ec3u;
                                const float phi=2.f*(Pi*(float(r1>>16)/65535.f));
                                float st,ct;sinCosRad((float(r>>16)/65535.f)*f8,st,ct);
                                float sp,cp;sinCosRad(phi,sp,cp);
                                v={st*sp,ct,st*cp};
                                if(!(flags&1)){const u32 r2=r1*0x343fdu+0x269ec3u;v=v*((float(r2>>16)/65535.f)*power);}
                                else v=v*power;
                            }
                            v=transformCoord(dm,v);
                        }else{
                            const u32 r1=r*0x343fdu+0x269ec3u,r2=r1*0x343fdu+0x269ec3u;
                            v={float(s16(r>>16))/32768.f,float(s16(r1>>16))/32768.f,float(s16(r2>>16))/32768.f};
                            if((flags&1)&&!normalize(v))v.y=1.f;
                            v=v*power;
                        }
                        if(!(flags&4))v.x=0.f;
                        if(!(flags&8))v.y=0.f;
                        if(!(flags&0x10))v.z=0.f;
                    }
                }
                if(space==1)v=transformNormal(emToLocNT,v);
                else if(space==0)v=transformNormal(glbToLocNT,v);
                else if(space==3)v=transformNormal(glbToLocNT,v*emScale);
                if(addTo==0)addVel=addVel+v;
                else if(addTo==1)addPos=addPos+v;
                break;
            }
            case 2:{
                if(be32(c+0x1c)){
                    const u8* names=c+0x20+be32(c+0xc)+be32(c+0x10)+be32(c+0x14);
                    postInfo=names+be32(c+0x18);
                    for(int k=0;k<9;++k)postTransform[k]=bef(postInfo+k*4);
                    postChild=nullptr;
                    if(be32(c+0x18)>4){
                        const u32 index=be32(names+4+be16(postInfo+0x46)*4);
                        if(index<s.resources.size())postChild=&s.resources[index];
                    }
                }
                if(be32(c+0xc))animF32(c,postTransform+target/4,tick,seed,life);
                post=true;
                break;
            }
            default:break;
            }
        }
        if(p->collisionCounter!=0xff)++p->collisionCounter;
        if(post&&postInfo){
            fieldSpace();
            Vec3 newVel=vel+addVel;bool kill=false;
            const bool move=postField(s,*p,postTransform,postInfo,postChild,emToGlb,glbToLoc,pos,addPos,newVel,kill);
            if(kill){if(p->life==Active)retireParticle(s,p);p=next;continue;}
            if(move){
                p->p.velocity=newVel;
                p->p.position=p->p.position+addPos*p->p.momentum+newVel*p->p.momentum;
            }
        }else{
            p->p.velocity=p->p.velocity+addVel;
            const float m=p->p.momentum;
            p->p.position.x=p->p.position.x+addPos.x*m+p->p.velocity.x*m;
            p->p.position.y=p->p.position.y+addPos.y*m+p->p.velocity.y*m;
            p->p.position.z=p->p.position.z+addPos.z*m+p->p.velocity.z*m;
        }
        ++p->tick;
        p=next;
    }
    pm.lastCalced=pm.particles.tail;
}
void beginCalc(ParticleManager& pm,bool onlyIfRemain){
    pm.lastCalced=nullptr;
    for(Particle* p=pm.particles.head;p;p=p->next){
        if(onlyIfRemain&&!p->calcRemain)continue;
        if(p->calcRemain)--p->calcRemain;
        if(p->life==Active&&p->eval==EvalDone)p->eval=EvalWait;
    }
}
void endCalc(ParticleManager& pm){for(Particle* p=pm.particles.head;p;p=p->next)if(p->life==Active&&p->eval==EvalSkip)p->eval=EvalDone;}
void calcParticles(System& s,Emitter& em){
    if(em.prm.comFlags&EmitterDesc::DisableCalc)return;
    snapshot(em.managers,[&](ParticleManager* pm){
        calcManager(s,*pm);
        // EGG::ExpParticleManager runs this after the NW4R calculation.
        // Repeated changes to the control's life do not re-age old waves.
        if(pm->overrideLife)for(Particle* p=pm->particles.head;p;p=p->next)
            if(p->tick==1&&(p->life==Active||p->life==Wait))p->lifeSpan=pm->particleLife;
    });
}

void calcEffect(System& s,Effect& e){
    for(Emitter* em=e.emitters.head;em;em=em->next){
        if((em->life==Active||em->life==Wait)&&em->eval==EvalDone)em->eval=EvalWait;
        for(ParticleManager* pm=em->managers.head;pm;pm=pm->next)beginCalc(*pm,false);
    }
    e.flags|=0x10000;
    while(e.flags&0x10000){
        e.flags&=~0x10000u;
        for(;;){
            snapshot(e.emitters,[&](Emitter* em){calcEmitter(s,*em);});
            for(Emitter* em=e.emitters.head;em;em=em->next)
                if((em->life==Active||em->life==Wait)&&((em->prm.emitFlags&0x10000)||(em->prm.emitFlags&0x8000)))calcBillboard(s,*em);
            snapshot(e.emitters,[&](Emitter* em){
                if(em->eval==EvalWait)++em->tick;
                calcParticles(s,*em);
                if(em->eval==EvalWait)--em->tick;
            });
            snapshot(e.emitters,[&](Emitter* em){calcEmission(s,*em);});
            snapshot(e.emitters,[&](Emitter* em){calcParticles(s,*em);});
            if(!s.queueCount)break;
            executeQueue(s);
        }
        if(e.flags&0x10000){
            for(Emitter* em=e.emitters.head;em;em=em->next){
                if(em->life==Active&&em->calcRemain){--em->calcRemain;if(em->eval==EvalDone)em->eval=EvalWait;}
                for(ParticleManager* pm=em->managers.head;pm;pm=pm->next)beginCalc(*pm,true);
            }
        }
    }
    for(Emitter* em=e.emitters.head;em;em=em->next){
        if((em->life==Active||em->life==Wait)&&em->eval==EvalSkip)em->eval=EvalDone;
        for(ParticleManager* pm=em->managers.head;pm;pm=pm->next)endCalc(*pm);
    }
}

System* currentSystem=nullptr;
}


u32 flickAlpha(const Particle& p){
    const DrawSetting& d=p.manager->res->desc.draw;
    if(d.alphaFlickType==0)return 0xff;
    u32 cycle=0xffff;
    int scaled=int(d.alphaFlickRandom)*int(d.alphaFlickCycle)*int(p.p.alphaFlickRnd);
    const u32 c=u32(d.alphaFlickCycle)+u32(scaled/12700);
    if(c<0x10000)cycle=c&0xffff;
    const int at=int(p.tick)-1;
    const u32 pos=u32(at-(at/int(cycle))*int(cycle))&0xffff;
    const int amp=d.alphaFlickAmplitude;
    int a=0;
    switch(d.alphaFlickType){
    case 1:a=cycle<pos<<1?short(amp*3+(amp*int(pos)*-4)/int(cycle)+0x80):short((0x80-amp)+int((u32(amp)*pos*4)/cycle));break;
    case 2:a=short(amp+(amp*int(pos)*-2)/int(cycle)+0x80);break;
    case 3:a=short((0x80-amp)+int((u32(amp)*pos*2)/cycle));break;
    case 4:a=cycle<pos<<1?short(0x80-amp):short(amp+0x80);break;
    case 5:a=int(128.f+float(amp)*sinFIdx(FIdxPerRad*((6.2831854820251465f*float(pos))/float(cycle))));break;
    }
    if(short(a)<0)a=0;
    if(0xff<short(a))a=0xff;
    return u32(a)&0xff;
}
void modifyColor(const ParticleManager& pm,u8 primary[4],u8 secondary[4]){
    if(pm.lightType!=1)return;
    for(int c=0;c<4;++c){
        primary[c]=u8((u32(primary[c])*pm.lightAmbient[c]+0x80)>>8);
        secondary[c]=u8((u32(secondary[c])*pm.lightAmbient[c]+0x80)>>8);
    }
}

void childKey(const u8* curve,const u8* key,u16 seed,Particle& particle,u32 loop){
    System& s=*currentSystem;
    const u8* setting=key+0xc;
    const u8* random=curve+0x20+be32(curve+0xc)+be32(curve+0x10);
    const u8* names=random+be32(curve+0x14);
    if(key[2]){
        const u32 r=curveHash(seed,be16(curve+6),loop,be16(key+0xc));
        if(!(key[2]&2))return; // retail reads an address from the seed here; RKRace has no such keys
        const u32 count=be16(random);
        if(!count)return;
        setting=random+4+((r>>16)%count)*12;
    }
    const u32 index=be32(names+4+be16(setting+10)*4);
    if(index>=s.resources.size())return;
    addCreation(s,setting[6]==0?0:1,readSetting(setting),particle,s.resources[index],particle.calcRemain,nullptr);
}

void System::reset(){
    particles.reset();managers.reset();emitters.reset();effects.reset();
    active={};queueCount=0;liveParticles=0;random.srand(0);calcGroups=drawGroups=~0u;cameraSpaceGroups=0;
    deadParticles.clear();deadManagers.clear();deadEmitters.clear();deadEffects.clear();
}
Effect* System::createEffect(const EmitterResource& res,u16 calcRemain,u8 group){
    currentSystem=this;
    Effect* e=effects.alloc();
    if(!e)return nullptr;
    e->refs=0;e->life=Active;e->root=Mtx34::identity();e->flags=0;e->stoppedParticleTracks=0;e->group=group;
    if(!effectCreateEmitter(*this,*e,res,128,calcRemain)){effects.release(e);return nullptr;}
    active.append(e);
    e->life=Wait;
    return e;
}
void System::calc(){
    currentSystem=this;
    snapshot(active,[&](Effect* e){if((calcGroups>>e->group&1u)&&!(e->flags&1u))calcEffect(*this,*e);});
    garbageCollection();
}
void System::retireEffect(Effect& e){
    currentSystem=this;
    retireEmitterAll(*this,e);
    snapshot(e.emitters,[&](Emitter* em){retireParticleAll(*this,*em);});
}
void System::retireEmitters(Effect& e){currentSystem=this;retireEmitterAll(*this,e);}
void System::setColor(Effect& e,const u8 rgba[4]){
    // ForeachParticleManager (0x80029c90) without its ignore-life flag: active managers only.
    for(Emitter* em=e.emitters.head;em;em=em->next)
        for(ParticleManager* pm=em->managers.head;pm;pm=pm->next)
            if(pm->life==Active){pm->lightType=1;std::copy(rgba,rgba+4,pm->lightAmbient);}
}
void System::setParticleLife(Effect& e,u16 life){
    // EGG SetLife (8022302c) enumerates current emitter/manager lists.
    for(Emitter* em=e.emitters.head;em;em=em->next)
        for(ParticleManager* pm=em->managers.head;pm;pm=pm->next){pm->particleLife=life;pm->overrideLife=true;}
}
void System::setParticleScale(Effect& e,float x,float y){
    // EGG SetScale(Vector2) (8022391c): ParticleManager modifier,
    // separate from the emitter transform and authored emission geometry.
    for(Emitter* em=e.emitters.head;em;em=em->next)
        for(ParticleManager* pm=em->managers.head;pm;pm=pm->next){pm->modifierScale[0]=x;pm->modifierScale[1]=y;}
}
void System::setEmissionRatio(Effect& e,float ratio){
    // EGG SetEmitRatio (802230ec): absolute emitRatio, not a multiplier.
    for(Emitter* em=e.emitters.head;em;em=em->next)em->prm.emitRatio=ratio;
}
void System::setSpecificVelocity(Effect& e,float speed){
    for(Emitter* em=e.emitters.head;em;em=em->next)em->prm.velPowerSpecDir=speed;
}
void System::setVelocity(Effect& e,Vec3 velocity){e.velocity=velocity;}
void System::setRootMtx(Effect& e,const Mtx34& m){
    e.root=m;
    for(Emitter* em=e.emitters.head;em;em=em->next)if(!em->parent)setMtxDirty(*em);
}
void System::garbageCollection(){
    for(Particle* p:deadParticles){p->life=Closed;particles.release(p);}
    for(ParticleManager* pm:deadManagers){pm->life=Closed;managers.release(pm);}
    for(Emitter* em:deadEmitters){em->life=Closed;emitters.release(em);}
    for(Effect* e:deadEffects){e->life=Closed;effects.release(e);}
    deadParticles.clear();deadManagers.clear();deadEmitters.clear();deadEffects.clear();
}
}
