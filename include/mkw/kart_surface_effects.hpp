#pragma once
#include "mkw/kart_surface_classification.hpp"
#include "mkw/kart_surface_colors.hpp"
#include "mkw/kart_surface_emission.hpp"
#include "mkw/race_effects.hpp"

namespace mkw {
// Single-view wheel-surface paths, local and remote: USA80693758, 806a9a6c,
// 806aa104, 806ab77c. Water remains owned by the existing water bridge.
class KartSurfaceEffects {
public:
    static constexpr unsigned FirstSlot=1547,Slots=13;
    static_assert(FirstSlot+12*Slots<=RaceEffects::SlotCount);
    struct Wheel {Vec3 edge;uint32_t types=0;unsigned variant=0;bool floor=false;};
    void reset(){*this=KartSurfaceEffects{};}
    void step(RaceEffects& effects,const EffectFrame& pose,const Wheel (&wheel)[2],
              unsigned course,bool bike,bool enabled,bool driftAtLeastTwo,
              float speed,float softLimit,bool charging,bool startSmoke,bool sandArea,unsigned racer=0) {
        if(racer>=12)return;
        if(generation_!=effects.generation()) {
            reset();generation_=effects.generation();
            spec_[0]=effects.find("rk_dirtSmokeA");spec_[1]=effects.find("rk_weed");spec_[2]=effects.find("rk_mud");
            spec_[3]=effects.find("rk_snowHDirt");spec_[4]=effects.find("rk_snowRoad0");
            spec_[5]=effects.find("rk_snowRoad1");spec_[6]=effects.find("rk_flower");
            // KartEffect constructor USA80689c88/80689d00: course3 volcano, course9 tile.
            spec_[7]=effects.find(course==9?"rk_stoneTile":course==3?"rk_stoneVolc":"rk_stone");
        }
        base_=FirstSlot+racer*Slots;group_=uint8_t(2+racer);
        const bool local=racer==0;
        KartSurfaceColors colors(course);if(course==14)colors.setSandArea(sandArea);
        if(!ready_) { previous_[0]=wheel[0].edge;previous_[1]=wheel[1].edge;ready_=true; }
        const Vec3 velocity[2]={wheel[0].edge-previous_[0],wheel[1].edge-previous_[1]};
        auto sand=pose;sand.origin=bike?previous_[1]:(previous_[0]+previous_[1])*.5f;
        // Keep fading particles attached even after their floor category changes.
        effects.move(base_,sand);
        effects.setVelocity(base_,velocity[bike?1:0]*KartSandEmission::InheritedVelocity);
        for(unsigned w=0;w<2;++w) {
            auto frame=pose;frame.origin=wheel[w].edge;
            for(unsigned base:{1u,3u,6u,8u,10u})effects.move(base_+base+w,frame);
        }
        auto middle=pose;middle.origin=bike?wheel[1].edge:(wheel[0].edge+wheel[1].edge)*.5f;
        auto snow=middle;snow.origin+=pose.y*75.f;
        effects.move(base_+5,snow);effects.move(base_+12,middle);
        const float reference=std::max(50.f,softLimit);
        const float selected=charging||startSmoke?reference*.5f:std::clamp(speed,0.f,reference);
        for(unsigned w=bike?1:0;w<2;++w) {
            const int previousKind=surface_[w].kind;
            if(enabled&&wheel[w].floor)surface_[w].update(wheel[w].types,wheel[w].variant,course,false,driftAtLeastTwo);
            else surface_[w].kind=KartSurfaceClassification::None;
            const auto current=surface_[w];
            auto frame=pose;frame.origin=wheel[w].edge;
            if(current.kind==KartSurfaceClassification::Sand) {
                activate(effects,0,spec_[0],sand);
                const auto emission=KartSandEmission::sample(speed,softLimit,local,false,false,charging||startSmoke,unsigned(current.color));
                effects.setEmissionRatio(base_,emission.ratio);effects.setParticleLife(base_,emission.life);
                effects.setColor(base_,colors[unsigned(current.color)].data());
                effects.setVelocity(base_,velocity[bike?1:0]*KartSandEmission::InheritedVelocity);
            } else if(current.kind==KartSurfaceClassification::Grass||current.kind==KartSurfaceClassification::Mud) {
                const bool grass=current.kind==KartSurfaceClassification::Grass;
                const unsigned slot=(grass?1:3)+w;
                activate(effects,slot,spec_[grass?1:2],frame);
                effects.setEmissionRatio(base_+slot,selected*((local?.5f:grass?.15f:.25f)/reference));
                effects.setColor(base_+slot,colors[(grass?16:20)+unsigned(current.color)].data());
            } else if(current.kind==KartSurfaceClassification::SnowDirt) {
                activate(effects,5,spec_[3],snow);
                const float v=charging||startSmoke?60.f:std::clamp(speed,0.f,120.f);
                const float limit=120.f*.13750000298023224f;
                const float maximum=authoredRatio(effects,spec_[3]);
                const float ratio=v<=.12f?0.f:v<=limit?.12f+(maximum-.12f)*(v/limit):maximum;
                effects.setEmissionRatio(base_+5,ratio);
            } else if(current.kind==KartSurfaceClassification::SnowRoad) {
                activate(effects,6+w,spec_[4],frame);activate(effects,8+w,spec_[5],frame);
                effects.setEmissionRatio(base_+6+w,selected*((local?.5f:.15f)/reference));
                const float scale=selected/reference;
                effects.setParticleScale(base_+8+w,scale,scale);
                const uint8_t rgba[4]={255,255,255,uint8_t(255.f*scale)};
                effects.setColor(base_+8+w,rgba);
            } else if(current.kind==KartSurfaceClassification::Flower) {
                activate(effects,10+w,spec_[6],frame);
                effects.setEmissionRatio(base_+10+w,selected*(authoredRatio(effects,spec_[6])/reference));
            } else if(current.kind==KartSurfaceClassification::Stone) {
                activate(effects,12,spec_[7],middle);
                effects.setEmissionRatio(base_+12,selected*(authoredRatio(effects,spec_[7])/reference));
            }
            // The retail dispatcher creates the new category before fading the old.
            if(current.kind!=previousKind) {
                if(previousKind==KartSurfaceClassification::Sand)deactivate(effects,0);
                if(previousKind==KartSurfaceClassification::Grass)deactivate(effects,1+w);
                if(previousKind==KartSurfaceClassification::Mud)deactivate(effects,3+w);
                if(previousKind==KartSurfaceClassification::SnowDirt)deactivate(effects,5);
                if(previousKind==KartSurfaceClassification::SnowRoad){deactivate(effects,6+w);deactivate(effects,8+w);}
                if(previousKind==KartSurfaceClassification::Flower)deactivate(effects,10+w);
                if(previousKind==KartSurfaceClassification::Stone)deactivate(effects,12);
            }
        }
        previous_[0]=wheel[0].edge;previous_[1]=wheel[1].edge;
    }
private:
    static float authoredRatio(const RaceEffects& e,int spec){return spec<0?0.f:e.system().resources[unsigned(spec)].desc.emit;}
    void activate(RaceEffects& e,unsigned index,int spec,const EffectFrame& pose){
        if(!active_[index])e.fade(base_+index);
        e.create(base_+index,spec,pose,group_);e.move(base_+index,pose);active_[index]=true;
    }
    void deactivate(RaceEffects& e,unsigned index){e.followFade(base_+index);active_[index]=false;}
    std::array<KartSurfaceClassification,2> surface_{};
    std::array<Vec3,2> previous_{};
    std::array<bool,Slots> active_{};
    int spec_[8]{-1,-1,-1,-1,-1,-1,-1,-1};
    unsigned base_=FirstSlot;uint8_t group_=2;
    unsigned generation_=~0u;
    bool ready_=false;
};
}
