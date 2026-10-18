#pragma once
#include "mkw/kart_smoke_effects.hpp"
#include "mkw/race_effects.hpp"
#include <array>
#include <cstdint>

namespace mkw {
// KartEffect's floor effects (PAL addresses). Each wheel's collision (fn_1_A6B98 0x805B6F4C)
// hands the closest floor KCL entry's type bit and variant to the selector (0x806A0FDC via
// EffectManager 0x8067DA5C), which picks one of eight effects per wheel (KartEffect +0x164)
// and a colour index (+0x184). The dispatcher (0x80697BE0) runs the picked handler for each
// rear wheel and the stop handler of the effect a wheel just left; the end of the update
// (0x80696CF0) keeps the picks and the rear wheel edges for the next frame.
enum class TerrainFx : int8_t {None=-1,Dirt=0,Weed=1,SnowHDirt=2,Water=3,SnowRoad=4,Mud=5,Flower=6,Stone=7};
struct TerrainPick { TerrainFx effect=TerrainFx::None; uint8_t colour=0; };
struct TerrainFlags {
    bool chargedDrift=false; // +0xab: drift state above 1 (a charged mini-turbo)
    bool multiScreen=false;  // +0xb: two or more screens
    bool dkMountain=false;   // +0x143: course 0x13, GCN DK Mountain
    bool dkJungle=false;     // +0x149: course 0x1D, N64 DK's Jungle Parkway
};
// 0x806A0FDC. types: the entry's KCL type bit (0 with no floor contact), variant: (attribute
// >> 5) & 7 (-1 with no contact). Branches that store nothing keep the wheel's last pick, and
// some store only the effect. Returns true for heavy off-road variant 5 (+0x6b: rk_deepSnow).
inline bool selectTerrain(TerrainPick& p,uint32_t types,int v,const TerrainFlags& f){
    using F=TerrainFx;
    auto fx=[&](F e){p.effect=e;};
    auto fxc=[&](F e,uint8_t c){p.effect=e;p.colour=c;};
    if(types&0x2){ // 1 slippery road
        if(v==0)fxc(F::Dirt,0);else if(v==1)fxc(F::Dirt,1);
        else if(v==5&&!f.multiScreen)fxc(F::Dirt,2);else fx(F::None);
    }else if((types&0x4)&&!f.chargedDrift){ // 2 weak off-road (+0xac)
        switch(v){
        case 0:fxc(F::Dirt,3);break;case 1:fxc(F::Dirt,4);break;case 2:fx(F::Water);break;
        case 3:fxc(F::Weed,2);break;case 4:fxc(F::Dirt,5);break;case 5:fxc(F::Dirt,6);break;
        case 6:case 7:fx(F::Stone);break;default:fx(F::None);
        }
    }else if(types&0x8){ // 3 off-road
        switch(v){
        case 0:fxc(F::Dirt,7);break;case 1:fxc(F::Dirt,8);break;case 5:fxc(F::Dirt,9);break;
        case 7:fxc(F::Dirt,10);break;case 2:fxc(F::Mud,0);break;case 6:fx(F::Stone);break;
        case 4:fxc(F::Weed,0);break;default:fx(F::None);
        }
    }else if(types&0x10){ // 4 heavy off-road
        switch(v){
        case 0:fxc(F::Dirt,11);break;case 1:fxc(F::Dirt,12);break;case 6:fxc(F::Dirt,13);break;
        case 3:fx(F::Flower);break;case 5:fx(F::SnowHDirt);return true;
        case 2:fxc(F::Mud,1);break;case 4:fxc(F::Weed,1);break;default:fx(F::None);
        }
    }else if(types&0x400){ // 0xA solid out-of-bounds
        if(v==7)fxc(F::Dirt,14);else fx(F::None);
    }else if((types&0x1)&&(f.dkMountain||f.dkJungle)){ // 0 road on the DK courses
        if(v!=1)fx(F::None);
        else if(f.dkMountain?!f.multiScreen:f.dkJungle)fxc(F::Dirt,15);
    }else if((types&0x800000)&&f.dkMountain){ // 0x17 road 2 on GCN DK Mountain
        if(v!=2)fx(F::None);
        else if(!f.multiScreen)fxc(F::Weed,3);
    }else if(types&0x400000){ // 0x16 sticky road
        if(v==7)fxc(F::Mud,0);else fx(F::None);
    }else if((types&0x800)&&!f.chargedDrift&&v<=3){ // 0xB moving water
        fx(F::Water);
    }else if((types&0x1)&&v==5){ // 0 road, variant 5
        fx(F::SnowRoad);
    }else if(!(types&0x200000))fx(F::None); // 0x15 moving road keeps the pick (+0x31)
    return false;
}
struct TerrainColour { uint8_t r,g,b,a; };
// 0x806B171C: the course's floor colours, KartEffect +0x194 + 4 i: dirt 0-15 (+0x184), weed
// from 16 (+0x1d4), the rest for the other handlers. Course ids are RaceConfig +0xB68.
inline std::array<TerrainColour,23> terrainColours(unsigned course){
    std::array<TerrainColour,23> c{{
        {0xff,0xff,0xba,0x96},{0xe7,0xc0,0x6f,0x96},{0xf3,0xdf,0xa3,0x96},{0xcc,0x8b,0x54,0xc8},{0x9f,0x7d,0x40,0xc8},
        {0xff,0xff,0xba,0x96},{0xe7,0xe7,0xc4,0x64},{0xf3,0xe4,0x78,0xff},{0xb7,0x90,0x54,0xff},{0xf3,0xe4,0x85,0xc8},
        {0xb7,0x9a,0x87,0xc8},{0xf3,0xe4,0x78,0xff},{0xb7,0x90,0x54,0xff},{0xff,0xd0,0x6d,0xff},{0xbb,0x86,0x30,0xff},
        {0xe7,0xc0,0x6f,0x96},{0x6e,0xc3,0x32,0xff},{0x27,0x87,0x00,0xff},{0x47,0x6b,0x3f,0xff},{0x50,0xf0,0x00,0xff},
        {0x74,0x4d,0x00,0xff},{0x63,0x4b,0x2d,0xff},{0x9f,0x70,0x3c,0xff}}};
    struct Override { uint8_t course,index; TerrainColour colour; };
    static constexpr Override overrides[]={
        {0,7,{0xf3,0xe4,0x90,0xff}},{0,16,{0x3c,0xa0,0x00,0xff}},{1,16,{0x64,0xe7,0x00,0xff}},
        {2,7,{0x45,0x75,0x55,0x80}},{2,16,{0x44,0xca,0x00,0xff}},{3,4,{0x7c,0x50,0x27,0xff}},
        {5,17,{0x3c,0xc3,0x00,0xff}},{11,16,{0x8c,0x78,0x00,0xff}},{12,7,{0x46,0x41,0x32,0xff}},
        {12,16,{0x6b,0x4d,0x2f,0xff}},{12,21,{0x6e,0x27,0x00,0xff}},{14,7,{0xcf,0xb7,0x55,0xff}},
        {15,16,{0x28,0xb9,0x00,0xff}},{16,16,{0x9e,0xe7,0x30,0xff}},{17,16,{0x55,0xaa,0x00,0xff}},
        {18,8,{0x69,0x50,0x34,0xff}},{18,20,{0x52,0x5b,0x43,0xff}},{19,8,{0x6f,0x57,0x39,0xff}},
        {19,15,{0xe7,0xb2,0x5f,0x64}},{21,9,{0xe7,0xb8,0x57,0xff}},{22,1,{0xf3,0xe2,0x8e,0xc8}},
        {22,16,{0x32,0xb9,0x00,0xff}},{23,16,{0xcf,0xcf,0x3f,0xff}},{23,21,{0x70,0x45,0x00,0xff}},
        {24,7,{0xd8,0xc7,0x8d,0xff}},{26,7,{0xf1,0xdb,0x9f,0xff}},{26,16,{0x55,0xa7,0x00,0xff}},
        {28,16,{0x5a,0xaf,0x00,0xff}},{29,8,{0x75,0x55,0x33,0xff}},{29,16,{0x49,0x9e,0x00,0xff}},
        {30,7,{0x9f,0x61,0x57,0xff}},{31,0,{0xff,0xee,0x9d,0x96}},{31,16,{0x3f,0xb7,0x00,0xff}},
        {35,3,{0xff,0xf1,0x8a,0x96}},{36,0,{0xf1,0xd2,0x94,0x96}},{39,7,{0xd8,0xc7,0x8d,0xff}},
        {40,7,{0xff,0xe0,0xb0,0xff}}};
    for(const auto& o:overrides)if(o.course==course)c[o.index]=o.colour;
    return c;
}

// One racer's input for a step, read after it.
struct TerrainInput {
    uint32_t types[2]{};int variants[2]{-1,-1}; // rear left, rear right (a bike: [1], its rear wheel)
    Vec3 edges[2]{};                            // the rear wheel edges (0x8068FFDC), as above
    bool bike=false,chargedDrift=false;
    bool local=false;      // +0x129: a racer this console's player drives (CPUs and ghosts are not)
    bool lod=false;        // +7: the kart effect LOD has culled the racer
    bool startBoost=false; // +0x43 charging a stand-still mini-turbo, or +0xba the start smoke
    bool scaled=false;float particleScale=1; // +0xb0 and +0x738 (KartItemEffects::effectScale)
    bool dryDryAreaOutside=false;            // Dry Dry Ruins: no KMP area of type 1 at the kart
    float speed=0;         // +0x6e8: |velocity|
    float speedLimit=50;   // +0x6f0: max(KartMove +0x18, 50)
    EffectFrame pose;      // +0x6f4: the kart pose, scale 1
};

// The floor effects on RaceEffects. Only rk_dirtSmokeA (effect 0) and the water spray are
// cooked: the water handler (0x806AF768) is KartItemEffects' wheel spray, which takes the
// Water picks here; weed, snowHDirt, snowRoad, mud, flower and stone are not cooked yet.
class KartTerrainEffects {
public:
    static constexpr unsigned MaxRacers=12;
    // One emitter per local racer (karts share it between the rear wheels, +0x318).
    static constexpr unsigned DirtSlot=KartLandingEffects::FirstSlot+12*3;
    static_assert(DirtSlot<RaceEffects::SlotCount);
    bool bind(RaceEffects& e){dirt_=e.find("rk_dirtSmokeA");return dirt_>=0;}
    void reset(unsigned course,bool multiScreen=false){
        state_={};course_=course;colours_=terrainColours(course);
        flags_.multiScreen=multiScreen;flags_.dkMountain=course==0x13;flags_.dkJungle=course==0x1d;
    }
    // The step's wheel selections, then the dispatcher's handlers; returns the picks.
    const std::array<TerrainPick,2>& step(RaceEffects& e,unsigned racer,const TerrainInput& in){
        static const std::array<TerrainPick,2> none{};
        if(racer>=MaxRacers)return none;
        auto& s=state_[racer];
        TerrainFlags f=flags_;f.chargedDrift=in.chargedDrift;
        // The rk_deepSnow flag (+0x6b) has no cooked effect to start.
        for(unsigned w=in.bike?1:0;w<2;++w)(void)selectTerrain(s.pick[w],in.types[w],in.variants[w],f);
        for(unsigned w=in.bike?1:0;w<2;++w){
            if(s.pick[w].effect==TerrainFx::Dirt)dirt(e,racer,in,s,w);
            if(s.pick[w].effect!=s.previous[w].effect&&s.previous[w].effect==TerrainFx::Dirt)dirtStop(e,in,s);
        }
        s.previous=s.pick;s.edges[0]=in.edges[0];s.edges[1]=in.edges[1];s.started=true;
        return s.pick;
    }
private:
    struct State { std::array<TerrainPick,2> pick{},previous{};Vec3 edges[2]{};bool started=false; };
    EffectFrame dirtFrame(const TerrainInput& in,const State& s) const {
        // The kart pose (setMtx) moved to the previous frame's rear edges (+0x7d4 / +0x7e0):
        // their midpoint for a kart, the rear wheel for a bike.
        EffectFrame f=in.pose;
        const Vec3 l=s.started?s.edges[0]:in.edges[0],r=s.started?s.edges[1]:in.edges[1];
        f.origin=in.bike?r:(l+r)*.5f;
        return f;
    }
    // 0x806ADEF4, the rk_dirtSmokeA handler: only for a local racer outside the LOD. The
    // port has one local racer (player 0), so one slot.
    void dirt(RaceEffects& e,unsigned racer,const TerrainInput& in,const State& s,unsigned wheel){
        if(in.lod||!in.local||dirt_<0)return;
        if(course_==0xe)colours_[9]=in.dryDryAreaOutside?TerrainColour{0xe7,0xb8,0x57,0xff}:TerrainColour{0xcf,0xb7,0x55,0xff};
        const float top=in.speedLimit;
        float f=top;
        if(in.speed<top){f=in.speed;if(in.speed<=0.f)f=0.f;}
        const uint8_t colour=s.pick[wheel].colour;
        if(colour==0xe)f=top;
        if(in.startBoost&&!flags_.multiScreen)f=.5f*top;
        // +0xa54..+0xa60 (0x806B11B0): emission 0 to .5 and life 30 to 15 for a one-screen
        // player (+0xf), .375 and 20 to 10 otherwise; +0xc (three screens) halves emission.
        const bool own=!flags_.multiScreen;
        const float maxEmit=own?.5f:.375f,lifeStart=own?30.f:20.f,lifeEnd=own?15.f:10.f;
        const float ratio=f*(maxEmit/top);
        const float life=lifeStart-f*((lifeStart-lifeEnd)/top);
        const EffectFrame frame=dirtFrame(in,s);
        const unsigned slot=DirtSlot;
        e.create(slot,dirt_,frame,uint8_t(KartFxSlots::FirstGroup+racer));
        e.move(slot,frame);
        e.setEmissionRatio(slot,ratio);
        e.setParticleLife(slot,uint16_t(int(life)));
        const auto& c=colours_[colour<colours_.size()?colour:0];
        const uint8_t rgba[4]={c.r,c.g,c.b,c.a};
        e.setColor(slot,rgba);
        if(in.scaled)e.setParticleScale(slot,in.particleScale,in.particleScale);
        // Not ported: EGG::Effect +0x4c (0x802237D0) with .92 x the rear wheel velocity.
    }
    // 0x806AE3A4: followFade, then the same transform (a bike's setMtx comes last, so its
    // fading emitter sits at the kart's own position). Only a local racer's emitter exists.
    void dirtStop(RaceEffects& e,const TerrainInput& in,const State& s){
        if(!in.local)return;
        const unsigned slot=DirtSlot;
        if(!e.alive(slot))return;
        e.followFade(slot);
        e.move(slot,in.bike?in.pose:dirtFrame(in,s));
        if(in.scaled)e.setParticleScale(slot,in.particleScale,in.particleScale);
    }
    int dirt_=-1;
    unsigned course_=~0u;
    TerrainFlags flags_{};
    std::array<TerrainColour,23> colours_{};
    std::array<State,MaxRacers> state_{};
};
}
