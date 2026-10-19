#pragma once
// NW4R EF runtime internals shared by src/nw4r_ef*.cpp and RaceEffects.
#include "mkw/nw4r_ef.hpp"
#include <memory>

namespace mkw::ef {
u32 curveHash(u32 seed,u16 curveSeed,u32 loop,u32 index);
void animTexture(const u8* curve,Particle& particle,u32 tick,u16 seed,u32 life);
void animChild(const u8* curve,Particle& particle,u32 tick,u16 seed,u32 life);
// AnimCurveExecuteChild key (0x8002d690): queues the key's particle or emitter creation.
void childKey(const u8* curve,const u8* key,u16 seed,Particle& particle,u32 loop);

template<class T,size_t N> struct Pool {
    std::array<T,N> items{};
    std::array<T*,N> free{};
    size_t freeCount=0;
    void reset(){for(size_t i=0;i<N;++i)free[i]=&items[N-1-i];freeCount=N;}
    T* alloc(){if(!freeCount)return nullptr;T* p=free[--freeCount];*p=T{};return p;}
    void release(T* p){free[freeCount++]=p;}
    size_t used() const {return N-freeCount;}
};

// EffectSystem with its memory manager, creation queue and effect groups. Retail keeps an
// activity list per group and calculates and draws one group per call; MKW's EffectManager
// uses group 0 for shared effects and 2 + racer for each KartEffect (USA 0x8068abbc), and
// skips a racer's group under the kart effect LOD (calc 0x80678818, draw 0x80678aec). The
// port keeps one activity list and masks groups out instead, so enabled groups calculate and
// draw in creation order as before.
struct System {
    static constexpr size_t ParticleCapacity=1024,ManagerCapacity=320,EmitterCapacity=256,EffectCapacity=192,QueueCapacity=256;
    std::vector<EmitterResource> resources;
    Random random;
    Pool<Particle,ParticleCapacity> particles;
    Pool<ParticleManager,ManagerCapacity> managers;
    Pool<Emitter,EmitterCapacity> emitters;
    Pool<Effect,EffectCapacity> effects;
    List<Effect> active; // EffectSystem activity list (effects wait on their emitters)
    // Objects whose closing completed this step; freed by garbageCollection().
    std::vector<Particle*> deadParticles;std::vector<ParticleManager*> deadManagers;
    std::vector<Emitter*> deadEmitters;std::vector<Effect*> deadEffects;
    struct Creation {u8 type,flags;u16 calcRemain;InheritSetting setting;Particle* particle;const EmitterResource* res;Vec3 position;};
    std::array<Creation,QueueCapacity> queue{};
    u32 queueCount=0;
    // Camera for EMIT_FLAG_15/16 billboard emitters (EffectSystem::mProcessCameraMtx).
    Mtx34 cameraMtx=Mtx34::identity();
    u32 liveParticles=0;
    // Groups that calc() calculates and draw lists include, one bit per group. A masked
    // group's effects keep their state: they are neither aged nor drawn until unmasked.
    u32 calcGroups=~0u,drawGroups=~0u;
    u32 cameraSpaceGroups=0; // Screen effects draw after world effects, with identity view.

    void reset();
    Effect* createEffect(const EmitterResource& res,u16 calcRemain=0,u8 group=0);
    void calc(); // EffectSystem::Calc for each group in calcGroups, then garbage collection
    void retireEffect(Effect& e); // EGG kill: emitters and particles
    void retireEmitters(Effect& e); // EGG fade: emitters stop, particles live on
    // EGG::Effect::setColor (0x80223794 -> 0x80026d20): every active ParticleManager of the
    // effect's emitters gets light type 1 with this ambient colour.
    void setColor(Effect& e,const u8 rgba[4]);
    void setParticleLife(Effect& e,u16 life);
    void setParticleScale(Effect& e,float x,float y);
    void setEmissionRatio(Effect& e,float ratio);
    // EGG vtable +0x3c (8022333c): Emitter+0x7c is the specific-direction
    // velocity power, not the particle emission ratio.
    void setSpecificVelocity(Effect& e,float speed);
    void setVelocity(Effect& e,Vec3 velocity);
    void setRootMtx(Effect& e,const Mtx34& m);
    bool alive(const Effect* e,u16 serial) const {return e&&e->life!=Closed&&e->life!=Closing&&e->serial==serial;}
    void garbageCollection();
};
Mtx34 emitterGlobalMtx(Emitter& em);
Mtx34 managerGlobalMtx(ParticleManager& pm);
// Particle::Draw_GetColor alpha flicker (0x80033150): 0..255 multiplier, 255 if off.
u32 flickAlpha(const Particle& p);
// ParticleManager colour modifier (0x80035470): light type 1 multiplies a layer's primary
// and secondary RGBA by the ambient, (c * a + 0x80) >> 8; type 0 leaves them.
void modifyColor(const ParticleManager& pm,u8 primary[4],u8 secondary[4]);
}
