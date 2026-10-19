#pragma once
// A port of the NW4R EF particle runtime as linked into Mario Kart Wii (RMCE main.dol
// 0x80026180-0x80048000), for the RKRace emitters cooked by tools/cook_race_effects.py.
// Object model, calculation order, curve evaluation, emitter forms, fields and random
// streams follow the retail code; docs/VFX_RESEARCH.md maps each part to its address.
// Presentation only: nothing here feeds race or replay state.
#include "mkw/math.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkw::ef {
using u8=uint8_t;using s8=int8_t;using u16=uint16_t;using s16=int16_t;using u32=uint32_t;using s32=int32_t;

// Big-endian reads: curves and descriptors keep their BREFF byte order.
inline u16 be16(const u8* p){return u16(p[0]<<8|p[1]);}
inline u32 be32(const u8* p){return u32(p[0])<<24|u32(p[1])<<16|u32(p[2])<<8|p[3];}
inline float bef(const u8* p){u32 v=be32(p);float f;__builtin_memcpy(&f,&v,4);return f;}

// math::MTX34, row major.
struct Mtx34 {
    float m[3][4];
    static Mtx34 identity(){return {{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};}
    Vec3 col(int c) const {return {m[0][c],m[1][c],m[2][c]};}
    void setCol(int c,Vec3 v){m[0][c]=v.x;m[1][c]=v.y;m[2][c]=v.z;}
    Vec3 translate() const {return col(3);}
};
Mtx34 mul(const Mtx34& a,const Mtx34& b);
bool inverse(Mtx34& out,const Mtx34& in);
Vec3 transformCoord(const Mtx34& m,Vec3 v); // with translation
Vec3 transformNormal(const Mtx34& m,Vec3 v); // linear part only
Mtx34 noTranslate(Mtx34 m);
Mtx34 rotXYZ(float x,float y,float z); // MTX34RotXYZRad, table sin/cos
Mtx34 rotAxis(Vec3 axis,float radians); // MTX34RotAxisRad
Mtx34 translateBy(const Mtx34& m,Vec3 t); // MTX34Trans: m * T(t)
Mtx34 scaleBy(const Mtx34& m,Vec3 s); // MTX34Scale: m * S(s), zero scales become 1e-5
bool normalize(Vec3& v); // false (and v unchanged) for a zero vector
void sinCosRad(float rad,float& s,float& c); // math::SinCosRad (table lookup)
void sinCosFIdx(float fidx,float& s,float& c); // math::SinCosFIdx, 256 per turn
float sinFIdx(float fidx);
float cosFIdx(float fidx);
Vec3 rotationToVecY(Vec3 rot); // 0x80036fd0: Y axis of RotXYZ(rot)
Mtx34 dirMtxY(Vec3 dir); // GetDirMtxY
Mtx34 rotationMtx(const Mtx34& m); // MtxGetRotationMtx
Vec3 rotationOf(const Mtx34& m); // MtxGetRotation
Vec3 scaleOf(const Mtx34& m); // MtxGetScale

// ef::Random: 32-bit LCG, values from the high half.
struct Random {
    u32 seed=0;
    void srand(u32 s){seed=s;}
    u32 rand(){seed=seed*0x343fdu+0x269ec3u;return seed>>16;}
    float randFloat(){return float(rand())/65536.f;}
};

enum LifeStatus:u8 {Closed,Active,Wait,Closing};
enum EvalStatus:u8 {EvalWait,EvalDone,EvalSkip};

// EmitterDesc + EmitterDrawSetting (MKW layout), parsed from the BREFF bytes.
struct DrawSetting {
    enum : u16 {ZTest=1<<0,ZUpdate=1<<1,ZBeforeTex=1<<2,ClipDisable=1<<3,Tex1=1<<4,Tex2=1<<5,TexInd=1<<6,
                Tex1Proj=1<<7,Tex2Proj=1<<8,TexIndProj=1<<9,Hidden=1<<10,DrawOrder=1<<11,Fog=1<<12,
                XYSameSize=1<<13,XYSameScale=1<<14};
    u16 flags;
    u8 acmpComp0,acmpComp1,acmpOp,numTevs,flagClamp,indirectTargetStage;
    u8 tevTexture[4];
    u8 tevColor[4][4],tevColorOp[4][5],tevAlpha[4][4],tevAlphaOp[4][5];
    u8 kColorSel[4],kAlphaSel[4];
    u8 blendType,blendSrc,blendDst,blendOp;
    u8 colorInput[8]; // rasterised, C0..C2, K0..K3 (ColorInput::TevColor)
    u8 alphaInput[8];
    u8 zCompare,alphaFlickType;
    u16 alphaFlickCycle;
    u8 alphaFlickRandom,alphaFlickAmplitude;
    u8 lightingMode,lightingType;
    u8 lightingAmbient[4],lightingDiffuse[4];
    float lightingRadius;
    Vec3 lightingPosition;
    float indTexOffsetMtx[2][3];
    s8 indTexScaleExp,pivotX,pivotY;
    u8 ptclType,typeOption,typeDir,typeAxis,typeOption0,typeOption1,typeOption2;
    float zOffset;
};
struct EmitterDesc {
    enum : u32 {SyncLife=1<<0,DisableDraw=1<<1,MaxLife=1<<2,InheritPtclScale=1<<5,InheritPtclRot=1<<6,
                EmitInheritScale=1<<7,EmitInheritRot=1<<8,DisableCalc=1<<9,InheritPtclAxis=1<<10,
                EmitInheritAxis=1<<11,Relocated=1u<<30};
    u32 commonFlag,emitFlag;
    u16 emitLife,ptclLife;
    s8 ptclLifeRandom,inheritChildPtclTranslate,emitIntervalRandom,emitRandom;
    float emit;
    u16 emitStart,emitPast,emitInterval;
    s8 inheritPtclTranslate,inheritChildEmitTranslate;
    float commonParam[6];
    u16 emitDiv;
    s8 velInitVelocityRandom,velMomentumRandom;
    float velPowerRadiationDir,velPowerYAxis,velPowerRandomDir,velPowerNormalDir,velDiffusionEmitterNormal,
          velPowerSpecDir,velDiffusionSpecDir;
    Vec3 velSpecDir,scale,rotate,translate;
    u8 lodNear,lodFar,lodMinEmit,lodAlpha;
    u32 randomSeed;
    DrawSetting draw;
};
// ParticleParameterDesc with texture names resolved to RaceEffects texture indices.
struct ParticleDesc {
    u8 color[2][2][4];
    float size[2],scale[2],rotate[3];
    float textureScale[3][2],textureRotate[3],textureTranslate[3][2];
    u16 texture[3]; // 0xffff: none
    u16 textureWrap;
    u8 textureReverse,acmpRef0,acmpRef1,rotateOffsetRandom[3];
    float rotateOffset[3];
};
// One cooked emitter resource: descriptors plus big-endian curve blobs. Child and texture
// curves' name tables hold resolved u32 indices (emitter / texture), 0xffffffff if absent.
struct EmitterResource {
    char name[32];
    EmitterDesc desc;
    ParticleDesc ptcl;
    std::vector<const u8*> ptclTracks,emitTracks;
    u16 ptclInitTracks=0,emitInitTracks=0;
    u16 index=0;
};

// ParticleParameter, byte compatible with NW4R (curve targets are offsets into it).
struct ParticleParameter {
    u8 color[2][2][4]; // 0x00 [layer][pri/sec] RGBA
    float size[2]; // 0x10
    float scale[2]; // 0x18
    float rotate[3]; // 0x20
    float textureScale[3][2]; // 0x2c
    float textureRotate[3]; // 0x44
    float textureTranslate[3][2]; // 0x50
    u32 texture[3]; // 0x68: RaceEffects texture index, 0xffffffff none
    u16 textureWrap; // 0x74
    u8 textureReverse,acmpRef0,acmpRef1; // 0x76
    s8 alphaFlickRnd; // 0x79
    u8 rotateOffset[3]; // 0x7a
    u8 collisionStatus; // 0x7d
    u8 pad[2];
    Vec3 velocity,position,prevPosition; // 0x80
    float momentum; // 0xa4
};
static_assert(offsetof(ParticleParameter,size)==0x10&&offsetof(ParticleParameter,rotate)==0x20&&
              offsetof(ParticleParameter,textureScale)==0x2c&&offsetof(ParticleParameter,textureRotate)==0x44&&
              offsetof(ParticleParameter,textureTranslate)==0x50&&offsetof(ParticleParameter,texture)==0x68&&
              offsetof(ParticleParameter,textureWrap)==0x74&&offsetof(ParticleParameter,acmpRef0)==0x77&&
              offsetof(ParticleParameter,velocity)==0x80&&offsetof(ParticleParameter,momentum)==0xa4,"ParticleParameter");

// EmitterParameter, byte compatible with NW4R (emitter-track targets).
struct EmitterParameter {
    u32 comFlags,emitFlags; // 0x00
    float emitRatio,emitRandom; // 0x08
    u16 emitInterval,emitDiv; // 0x10
    float emitIntervalRandom,emitCount; // 0x14
    u16 emitSpan,pad; // 0x1c
    float lodNear,lodFar,lodMinEmit; // 0x20
    float params[6]; // 0x2c
    u8 inherit; s8 inheritTranslate,velInitVelocityRandom,velMomentumRandom; // 0x44
    float velPowerRadiationDir,velPowerYAxis,velPowerRandomDir,velPowerNormalDir,velDiffusionEmitterNormal,
          velPowerSpecDir,velDiffusionSpecDir; // 0x48
    Vec3 velSpecDir,translate,scale,rotate; // 0x64
};
static_assert(offsetof(EmitterParameter,params)==0x2c&&offsetof(EmitterParameter,inherit)==0x44&&
              offsetof(EmitterParameter,velPowerSpecDir)==0x5c&&offsetof(EmitterParameter,translate)==0x70&&
              offsetof(EmitterParameter,rotate)==0x88&&sizeof(EmitterParameter)==0x94,"EmitterParameter");

// EmitterInheritSetting (MKW: ten bytes, alpha source flags appended).
struct InheritSetting {
    enum : u8 {FollowEmit=1<<0,InheritRot=1<<1};
    s16 speed=0;u8 scale=0,alpha=0,color=0,weight=0,type=0,flag=0,alphaFlagPri=0,alphaFlagSec=0;
    bool any() const {return speed||scale||alpha||color||(flag&InheritRot);}
};

struct Effect;struct Emitter;struct ParticleManager;struct Particle;

// Intrusive list (ut::List): append and remove keep NW4R iteration order.
template<class T> struct List {
    T* head=nullptr;T* tail=nullptr;u16 count=0;
    void append(T* p){p->prev=tail;p->next=nullptr;(tail?tail->next:head)=p;tail=p;++count;}
    void insertBefore(T* at,T* p){if(!at){append(p);return;}p->next=at;p->prev=at->prev;(at->prev?at->prev->next:head)=p;at->prev=p;++count;}
    void remove(T* p){(p->prev?p->prev->next:head)=p->next;(p->next?p->next->prev:tail)=p->prev;p->prev=p->next=nullptr;--count;}
};

struct Particle {
    Particle* prev=nullptr;Particle* next=nullptr;
    LifeStatus life=Closed;u32 refs=0;
    ParticleParameter p{};
    ParticleManager* manager=nullptr;
    Vec3 prevAxis{};
    EvalStatus eval=EvalWait;
    u16 tick=0,seed=0,lifeSpan=0,calcRemain=0;
    u8 collisionCounter=0xff; // +0xe4: frames since a postfield collision, 0xff none
};

struct ParticleManager {
    ParticleManager* prev=nullptr;ParticleManager* next=nullptr; // emitter's activity list
    ParticleManager* drawPrev=nullptr;ParticleManager* drawNext=nullptr; // effect's draw order
    LifeStatus life=Closed;u32 refs=0;
    Emitter* emitter=nullptr;
    const EmitterResource* res=nullptr;
    List<Particle> particles,closing;
    Particle* lastCalced=nullptr;
    u32 flag=0; // 1 inherit scale, 2 inherit rotation, 4 inherit axis
    s8 inheritTranslate=100;
    u8 weight=128;
    u8 collisionFadeAlpha=0,collisionFadeScale=0; s8 collisionFadeStart=0; // +0x88..0x8a
    bool mtxDirty=true;
    Mtx34 mtx=Mtx34::identity();
    float modifierScale[2]{1,1};
    Vec3 modifierRotate{};
    // SimpleLight (+0x6c type, +0x6d ambient RGBA): the emitter's DrawSetting lighting at
    // Initialize (0x80033550); EGG::Effect::setColor (0x80223794) sets type 1 and the
    // ambient. Type 1 scales colours by the ambient (modifyColor); type 2 is not ported.
    u8 lightType=0;
    u8 lightAmbient[4]{};
    // EGG::ExpParticleManager SetLife/Calc (RMCU 80226058 / 80225f84):
    // override only particles whose first calculation has reached tick 1.
    bool overrideLife=false;
    u16 particleLife=0;
};

struct Emitter {
    Emitter* prev=nullptr;Emitter* next=nullptr;
    LifeStatus life=Closed;u32 refs=0;
    EmitterParameter prm{};
    EvalStatus eval=EvalWait;
    const EmitterResource* res=nullptr;
    Effect* effect=nullptr;
    List<ParticleManager> managers,closing;
    bool firstEmission=true;
    u16 waitTime=0,intervalWait=0;
    u32 tick=0;
    u16 calcRemain=0,seed=0;
    Random random;
    u8 form=0xff;
    Emitter* parent=nullptr;
    Particle* referenceParticle=nullptr;
    InheritSetting inherit{};
    bool mtxDirty=true;
    Mtx34 mtx=Mtx34::identity();
};

struct Effect {
    Effect* prev=nullptr;Effect* next=nullptr;
    LifeStatus life=Closed;u32 refs=0;
    List<Emitter> emitters,closing;
    ParticleManager* drawHead=nullptr; // ParticleManagers by weight (DrawOrder::Add)
    Mtx34 root=Mtx34::identity();
    Vec3 velocity{}; // EGG SetVelocity: world velocity inherited at particle birth.
    u32 stoppedParticleTracks=0; // EGG SetAnimStop, scoped to this instance.
    u32 flags=0; // 0x10000: an object still has calcRemain
    u16 slot=0xffff,serial=0;
    u8 group=0; // EffectSystem group (CreateEffect's groupID)
};

// Curve evaluation (ResAnimCurve executors), exposed for host tests.
void animU8(const u8* curve,u8* target,u32 tick,u16 seed,u32 life);
void animF32(const u8* curve,float* target,u32 tick,u16 seed,u32 life);
void animRotate(const u8* curve,float* target,u32 tick,u16 seed,u32 life);
}
