#pragma once
#include "mkw/math.hpp"
#include "mkw/nw4r_ef_internal.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace mkw {
// Emitter placement: orthonormal local axes in world space plus a uniform scale.
struct EffectFrame {
    Vec3 origin{},x{1,0,0},y{0,1,0},z{0,0,1};
    float scale=1;
    Vec3 toWorld(Vec3 p) const { return origin+(x*p.x+y*p.y+z*p.z)*scale; }
    Vec3 rotate(Vec3 v) const { return x*v.x+y*v.y+z*v.z; }
};
// One cooked RKRace texture (tools/cook_race_effects.py): GX size and the padded PICA size.
struct EffectTexture { char name[32]; uint16_t width,height,paddedWidth,paddedHeight; };

// RKRace effects on the NW4R EF port (src/nw4r_ef*.cpp), driven like MKW's EGG::Effect
// handles: a slot holds one effect instance, whose root matrix the owner moves each step.
// Steps at the 60 Hz simulation rate; presentation only.
class RaceEffects {
public:
    static constexpr unsigned SlotCount=1914;
    RaceEffects();
    ~RaceEffects();
    bool load(const char* path);
    bool loadMemory(const void* data,size_t size);
    const char* loadError() const {return loadError_;}
    int loadErrno() const {return loadErrno_;}
    void unload();
    int find(const char* name) const;
    // One-shot effect with its authored emission life, not tied to a slot.
    void burst(int spec,const EffectFrame& frame);
    void burst(int spec,const EffectFrame& frame,float specificVelocity);
    // Persistent attachment: created when it becomes active, moved every step, and faded
    // (emitters stop, particles live on and keep following) when inactive. spec < 0 kills.
    void attach(unsigned slot,int spec,const EffectFrame& frame,bool active);
    // Kill and create again, as KartEffect does for rk_jumpSeikou (0x80697d34).
    void restart(unsigned slot,int spec,const EffectFrame& frame);
    void move(unsigned slot,const EffectFrame& frame);
    // EGG::Effect calls: create (vtable +0xc) starts an instance unless one is running,
    // followFade (+0x14) retires its emitters while it keeps following, kill (+0x18)
    // retires emitters and particles.
    bool alive(unsigned slot) const;
    // EGG::Effect's group (its constructor's argument) is the EffectSystem group the
    // instance is created in; KartEffect uses 2 + racer (USA 0x8068abbc).
    void create(unsigned slot,int spec,const EffectFrame& frame,uint8_t group=0);
    void followFade(unsigned slot);
    // EGG Fade (+0x10): stop emission and release the handle. Existing
    // particles retain their final transform and can coexist with a new instance.
    void fade(unsigned slot);
    void kill(unsigned slot);
    void stopParticleTrack(unsigned slot,unsigned track,bool stop);
    // EGG SetDisableCalc / SetDisableDraw (flags at Effect+0x50 bits0/1).
    // Invalid handles are ignored; this does not kill, fade or reset an instance.
    void setDisabled(unsigned slot,bool calc,bool draw);
    // setColor (+0x50, 0x80223794): the instance's active particle managers scale their
    // colours by this RGBA (light type 1) from the next draw on.
    void setColor(unsigned slot,const uint8_t rgba[4]);
    // EGG SetLife / SetEmitRatio: affect existing managers/emitters.
    // Life is applied to newly calculated particles, not a reset of all ages.
    void setParticleLife(unsigned slot,uint16_t life);
    void setParticleScale(unsigned slot,float x,float y);
    void setEmissionRatio(unsigned slot,float ratio);
    void setSpecificVelocity(unsigned slot,float speed);
    void setVelocity(unsigned slot,Vec3 velocity);
    // EffectSystem::SetProcessCamera (0x800276b0): the view matrix (world to camera, 3x4)
    // that billboard emitters (EMIT_FLAG_15/16) calculate against. MKW's EffectManager
    // calc (RMCU 0x80678818) sets it after the kart effect groups, so each step uses the
    // camera of the step before.
    void setCamera(const float view[3][4]);
    // The same from a look-at camera (C_MTXLookAt).
    void setCamera(Vec3 eye,Vec3 target,Vec3 up);
    // The groups the next step() calculates and the draw lists include, one bit per group
    // (EffectManager 0x80678818 / 0x80678aec). reset() enables every group.
    void setGroups(uint32_t calc,uint32_t draw);
    void step();
    void reset();
    unsigned liveParticles() const;
    unsigned generation() const { return generation_; }
    ef::System& system() { return *system_; }
    const ef::System& system() const { return *system_; }
    std::vector<EffectTexture> textures;
    bool ready=false;
private:
    struct Slot { ef::Effect* effect=nullptr; uint16_t serial=0; int spec=-1; bool fading=false; };
    ef::Effect* start(int spec,const EffectFrame& frame,uint8_t group=0);
    std::unique_ptr<ef::System> system_;
    std::vector<uint8_t> blob_;
    std::array<Slot,SlotCount> slots_{};
    uint16_t serial_=0;
    unsigned generation_=0,diagnosticMask_=0;
    const char* loadError_="not loaded";
    int loadErrno_=0;
};
}
