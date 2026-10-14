#pragma once
#include "mkw/math.hpp"
#include "mkw/visibility.hpp"
#if __has_include(<citro3d.h>)
#include <citro3d.h>
#define MKW_RENDER_QUALITY_C3D 1
#endif
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace mkw {
// Detail traded for frame rate on Old 3DS (the user accepted lower visual
// fidelity there on 2026-10-07). New 3DS and QA runs with full-quality.txt
// keep every draw, so captures can still prove nothing else changed.
struct RenderQuality {
    bool reduced=false;
    // Objects whose bounding sphere spans less than this radius/distance
    // ratio (about 4 px of the 240 px screen at the race FOV) are not drawn.
    float minScreenRatio=0;
    // Course objects switch to their authored -low LOD beyond this distance.
    float objectLowDistance=7000.f;
    // Retail stencil shadow volumes; bloom blur rounds (horizontal + vertical).
    bool shadowVolumes=true;
    // Full-screen bloom: an offscreen scene pass, blur passes and a composite
    // each frame, plus about 1 MB of VRAM. Off in reduced quality.
    bool bloom=true;
    unsigned bloomBlurRounds=2;
    // Item boxes below this ratio draw without their sparkle pass.
    float boxSparkleRatio=0;
    // Live effect particles; emitters skip new particles past it.
    unsigned particleBudget=~0u;
    // Kart effect LOD as retail sets it for this many screens: 1 gives full
    // effects to the 3 nearest karts within 3,000 units, 2 to the 2 nearest
    // within 2,000 (retail's two-player split screen).
    unsigned kartEffectScreens=1;
    // CPU-skinned spectator crowds and animated item models (Bullet Bill,
    // Thundercloud, POW...) advance in steps of this many ticks (4: 15 Hz),
    // so their skinned pose repeats on alternate frames; 0 every tick.
    unsigned animationStep=0;
    // Course, sky and course-object textures move from LINEAR (FCRAM) to VRAM
    // after the race's VRAM users have loaded, leaving vramReserve bytes free.
    bool vramCourseTextures=false;
    static constexpr std::size_t vramReserve=1024*1024;
    void setReduced(bool on){
        *this=RenderQuality{};
        if(!on)return;
        reduced=true;minScreenRatio=.015f;objectLowDistance=500.f;
        shadowVolumes=false;bloom=false;bloomBlurRounds=0;boxSparkleRatio=.04f;particleBudget=48;kartEffectScreens=3;animationStep=4;vramCourseTextures=true;
    }
#ifdef MKW_RENDER_QUALITY_C3D
    // True when bounds b placed by transform t are too small to draw from camera.
    bool tooSmall(const Bounds& b,const C3D_Mtx& t,Vec3 camera) const {
        if(minScreenRatio<=0)return false;
        return screenRatio(b,t,camera)<minScreenRatio;
    }
    static float screenRatio(const Bounds& b,const C3D_Mtx& t,Vec3 camera){
        const Vec3 c=(b.minimum+b.maximum)*.5f,h=(b.maximum-b.minimum)*.5f;
        float scale=0;
        for(unsigned col=0;col<3;++col){
            const float x=t.r[0].c[3-col],y=t.r[1].c[3-col],z=t.r[2].c[3-col];
            scale=std::max(scale,std::sqrt(x*x+y*y+z*z));
        }
        const Vec3 w{t.r[0].x*c.x+t.r[0].y*c.y+t.r[0].z*c.z+t.r[0].w,
                     t.r[1].x*c.x+t.r[1].y*c.y+t.r[1].z*c.z+t.r[1].w,
                     t.r[2].x*c.x+t.r[2].y*c.y+t.r[2].z*c.z+t.r[2].w};
        const float radius=length(h)*scale,distance=length(w-camera);
        return distance>radius?radius/distance:1.f;
    }
#endif
};
inline RenderQuality& renderQuality(){static RenderQuality quality;return quality;}
}
