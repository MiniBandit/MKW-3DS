#pragma once
#include "mkw/math.hpp"
#include <array>
#include <algorithm>

namespace mkw {
// Separate from the princess CHR0 hair layer. Retail Toadette has two four-point
// chains, three rendered joints each (USA StaticR 807db7f4 / 80780290).
// All state is world-space so steering, braking and head animation impart inertia.
class ToadetteBraids {
public:
    struct Frame {Vec3 p,x,y,z;};
    using Frames=std::array<Frame,6>;
    void reset(){initialized_=false;}
    const Frames& step(const Frames& rest,Vec3 head,unsigned tick,bool racing){
        if(initialized_&&tick==tick_)return output_;
        const unsigned elapsed=initialized_?tick-tick_:0;
        const bool discontinuity=!initialized_||elapsed>8||length(rest[0].p-lastRoot_)>2000.f;
        if(discontinuity){
            for(unsigned side=0;side<2;++side){
                auto& chain=chains_[side];chain.p[0]=rest[side*3].p;
                for(unsigned j=0;j<3;++j)chain.p[j+1]=chain.p[j]+rest[side*3+j].x*lengths[j];
                chain.v={};
            }
            initialized_=true;
        }
        // Missed rendering frames still advance the 60 Hz chain clock. A long
        // absence (reload, respawn or hidden menu) starts in the current pose.
        const unsigned steps=discontinuity?1:std::max(1u,elapsed);
        for(unsigned step=0;step<steps;++step){
            for(unsigned side=0;side<2;++side){
                auto& chain=chains_[side];const auto old=chain.p;
                chain.p[0]=rest[side*3].p;
                for(unsigned j=1;j<4;++j)chain.p[j]+=chain.v[j];
                for(unsigned j=0;j<3;++j){
                    const auto& basis=rest[side*3+j];
                    const float scale=length(basis.x);
                    const float radius=20.f*scale;
                    Vec3 correction{};
                    const Vec3 radial=chain.p[j+1]-head;
                    if(length(radial)<radius){
                        const Vec3 pushed=head+normalized(radial,basis.x)*radius;
                        correction=chain.p[j+1]-pushed;chain.p[j+1]=pushed;
                    }
                    const Vec3 forward=normalized(basis.x);
                    Vec3 wanted=normalized(chain.p[j+1]-chain.p[j],forward);
                    const float cosine=std::clamp(dot(forward,wanted),-1.f,1.f);
                    const float angle=std::acos(cosine);
                    const Vec3 axis=normalized(cross(forward,wanted),normalized(basis.z));
                    const float turn=std::min(angle,limits[j]*0.0174532925199433f);
                    wanted=rotate(forward,axis,turn);
                    chain.p[j+1]=chain.p[j]+wanted*(lengths[j]*scale);
                    auto& out=output_[side*3+j];out.p=chain.p[j];
                    out.x=rotate(basis.x,axis,turn);out.y=rotate(basis.y,axis,turn);out.z=rotate(basis.z,axis,turn);
                    // Retail acceleration follows each authored joint's X axis,
                    // rather than an invented sine animation or world-down tug.
                    chain.v[j+1]=(chain.p[j+1]-old[j+1]+basis.x*1.5f+correction)*(racing?.96f:.7f);
                }
            }
        }
        tick_=tick;lastRoot_=rest[0].p;return output_;
    }
private:
    static Vec3 rotate(Vec3 v,Vec3 axis,float angle){
        const float c=std::cos(angle),s=std::sin(angle);
        return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));
    }
    inline static constexpr float lengths[3]={13,11,20};
    inline static constexpr float limits[3]={40,45,60};
    struct Chain {std::array<Vec3,4> p{},v{};};
    std::array<Chain,2> chains_{};
    Frames output_{};
    Vec3 lastRoot_{};
    unsigned tick_=0;bool initialized_=false;
};
}
