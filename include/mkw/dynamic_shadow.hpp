#pragma once
#include <cmath>
#include <mkw/math.hpp>
#include <mkw/retail_lighting.hpp>

namespace mkw {
// The light the player's dynamic shadow follows: the one lighting the racers
// (the course's first character light-texture entry, else light 0). Course
// lights are mostly given in view space, as the Wii lights the karts relative to
// the camera, so the shadow keeps the same side of the kart as its shading.
struct ShadowSun { Vec3 direction{.3f,-1.f,.25f}; bool view=false; };
inline ShadowSun shadowSun(const CourseLighting* course){
    ShadowSun sun;
    if(!course)return sun;
    const auto& lm=course->textures[0];
    const unsigned index=lm.count&&lm.entries[0].light<16?lm.entries[0].light:0;
    const auto& l=course->lights[index];
    if(!(l.flags&1)||l.type!=1)return sun;
    sun.direction={l.direction[0],l.direction[1],l.direction[2]};
    sun.view=l.space!=0;
    return sun;
}
// The sun in world space for a camera with these world-space axes. A light
// pointing upward (Moonview Highway's) casts the other way, and a low sun is
// raised so the shadow is at most 1.2 times its caster's height.
inline Vec3 shadowLightDirection(const ShadowSun& sun,Vec3 right,Vec3 up,Vec3 back){
    Vec3 d=sun.view?right*sun.direction.x+up*sun.direction.y+back*sun.direction.z:sun.direction;
    if(d.y>0)d=d*-1.f;
    const float down=std::max(-d.y,.05f),across=std::sqrt(d.x*d.x+d.z*d.z);
    d.y=-down;
    if(across>down*1.2f){const float k=down*1.2f/across;d.x*=k;d.z*=k;}
    return normalized(d,{0,-1,0});
}
}
