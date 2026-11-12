#pragma once
#include <algorithm>
#include <mkw/math.hpp>
namespace mkw {
// Retail USA StaticR: init 0x2cac54, follow 0x2cb188, face 0x2cc410.
// These are world-space heights, independent of kart bank and wheel scale.
struct ThunderCloudVisual {
    Vec3 position{},facing{0,0,1};
    float flashFrame=1.f;
    unsigned animationFrame=0;
    float ramp=1.f,lateFollow=0.f,bounce=0.f,bounceVelocity=0.f;
    void spawn(Vec3 kart,Vec3 direction){
        *this={};position=kart+Vec3{0,600,0};facing=normalized(Vec3{direction.x,0,direction.z},{0,0,1});
    }
    void transfer(){ramp=lateFollow=0;bounceVelocity=10;}
    void step(Vec3 kart,Vec3 direction,unsigned age,float hopHeight=0.f,bool respawning=false){
        animationFrame=age;
        // ObjKumo 807d7990..7c58 drives the original 16-frame CLR0.
        static constexpr unsigned Groups[]={0,1,1,1,2,2,2,3,3,4};
        static constexpr unsigned Length[]={6,6,5,5,5},Spacing[]={15,15,14,13,13};
        static constexpr float Gain[]={2.5f,2.5f,3.f,3.5f,3.5f};
        static constexpr unsigned Decay[]={2,2,3,3,2};
        const unsigned groups=Groups[std::min(age/60,9u)],phase=age%60;
        unsigned pulses=0;
        for(unsigned g=0;g<groups;++g)for(unsigned f=0;f<Length[groups];++f)
            if(phase==g*Spacing[groups]+f||(groups==4&&f==phase%Spacing[groups])){++pulses;break;}
        if(pulses)for(unsigned p=0;p<pulses;++p)flashFrame=std::min(15.f,flashFrame+Gain[groups]);
        else flashFrame=std::max(0.f,flashFrame-Decay[groups]);
        ramp+=.005f;
        lateFollow=respawning?.5f:std::clamp(lateFollow+(age>520?.005f:-.005f),0.f,.4f);
        bounceVelocity=bounceVelocity*.93f-bounce*.02f;
        bounce+=bounceVelocity;
        const Vec3 target=kart+Vec3{0,365-hopHeight,0};
        const float horizontal=std::max(std::min(ramp,.4f),lateFollow);
        const float vertical=std::max(std::min(ramp,.3f),lateFollow);
        position.x+=(target.x-position.x)*horizontal;
        position.y+=(target.y-position.y)*vertical;
        position.z+=(target.z-position.z)*horizontal;
        // ObjKumo 807d8444 blends Z, then 807882d4 rebuilds an
        // orthonormal basis around its unchanged world-up axis.
        const Vec3 blended=lerp(facing,direction,.3f);
        facing=normalized(Vec3{blended.x,0,blended.z},facing);
    }
    // ItemObjProperties object14: held/released model scale .5 (808a2eb8).
    static constexpr float ModelScale=.5f;
    Vec3 modelPosition()const{return position+Vec3{0,bounce,0};}
};
}
