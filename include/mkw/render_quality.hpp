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
// 3DS presentation policy: subpixel object rejection and spare-VRAM texture
// placement apply to both models. Old 3DS additionally trades visible detail
// for frame rate; New 3DS retains the full authored effect sampling kernel.
struct RenderQuality {
    bool reduced=false;
    bool mipmaps=true; // authored per-material filters/LOD bias; LOD0 fallback remains available
    // Textures of 256x256 texels or more load from their first mip level
    // (Texture::loadMemory): Old 3DS, or Texture Quality: Low.
    bool halfLargeTextures=false;
    // Added to every mip-mapped texture's LOD (negative: sharper levels).
    // DIAG: lod-bias.txt holds a value from -2 to 0.
    float lodBias=0;
    unsigned halvedTextures=0; // QA: textures loaded at half size (Texture Quality: Low).
    // Objects whose bounding sphere spans less than this radius/distance
    // ratio (about 4 px of the 240 px screen at the race FOV) are not drawn.
    float minScreenRatio=.003f;
    // Course objects switch to their authored -low LOD beyond this distance.
    float objectLowDistance=7000.f;
    // 3D slider up (New 3DS): both eyes draw every course-object triangle.
    // Their -low LODs come in at the Old 3DS distance, and parts drop at twice
    // the 2D ratio (about 8 px by the measure above: still specks, where the
    // Old 3DS's .015, about 20 px, pops visibly). Reduced values win.
    // Course objects pass stereo=true only for a frame drawn in stereo.
    static constexpr float stereoObjectLowDistance=4000.f,stereoMinScreenRatio=.006f;
    float objectLow(bool stereo) const {return stereo&&!reduced?stereoObjectLowDistance:objectLowDistance;}
    float tinyRatio(bool stereo) const {return stereo&&!reduced?stereoMinScreenRatio:minScreenRatio;}
    // Retail stencil shadow volumes; bloom blur rounds (horizontal + vertical).
    bool shadowVolumes=true;
    unsigned bloomBlurRounds=2;
    // Item boxes below this ratio draw without their sparkle pass.
    float boxSparkleRatio=0;
    // Live effect particles; emitters skip new particles past it.
    unsigned particleBudget=~0u;
    // Kart effect LOD as retail sets it for this many screens: 1 gives full
    // effects to the 3 nearest karts within 3,000 units, 2 to the 2 nearest
    // within 2,000 (retail's two-player split screen).
    unsigned kartEffectScreens=1;
    // Opponents switch to the Wii's split-screen kart bodies, low-detail
    // drivers and fewer wheels with camera distance (RacerLod). Off: the Wii's
    // single-player rule, only drivers swap to model_lod beyond 4,000 units
    // (wiiLowDriver); bodies, wheels and accessories keep full detail.
    bool racerLod=false;
    // CPU-skinned spectator crowds and animated item models (Bullet Bill,
    // Thundercloud, POW...) advance in steps of this many ticks (4: 15 Hz),
    // so their skinned pose repeats on alternate frames; 0 every tick.
    unsigned animationStep=0;
    // The race's lower screen draws on every other presented frame (30 FPS
    // while the top screen presents at 60). A skipped frame leaves the lower
    // LCD showing its last image: citro3d only transfers targets drawn to.
    bool lowerScreenHalfRate=false;
    // Course bloom (retail posteffect.bblm); Balanced turns it off.
    bool bloom=true;
    // Course, sky and course-object textures move from LINEAR (FCRAM) to VRAM
    // after the race's VRAM users have loaded, leaving vramReserve bytes free.
    bool vramCourseTextures=true;
    // The player's dynamic shadow (License Settings). Off in Performance: it
    // redraws the ground under each wheel, about a third of a race's draws.
    bool dynamicShadow=true;
    static constexpr std::size_t vramReserve=1024*1024;
    // New 3DS races: nothing allocates VRAM after the move in a normal race. This
    // margin covers a Mii driver restored after a pause (Old 3DS pause textures
    // and the award ceremony still use vramReserve).
    static constexpr std::size_t raceVramReserve=128*1024;
    // The race's lower screen redraws on every Nth presented frame; an undrawn
    // screen keeps its last picture, fades still draw every frame. 1 everywhere:
    // Old 3DS draws it at 30 Hz like the upper screen (15 Hz saved ~0.7 ms of
    // GPU a frame on average). DIAG: bottom-half-rate.txt sets 2.
    unsigned bottomInterval=1;
    // The race lower screen's RGBA8 UI atlas is stored as RGBA4 (Old 3DS: 256 KB
    // of VRAM for course textures). DIAG: bottom-rgba8.txt keeps RGBA8.
    bool bottomRgba4=false;
    void setReduced(bool on){
        *this=RenderQuality{};
        if(!on)return;
        reduced=true;halfLargeTextures=true;minScreenRatio=.015f;objectLowDistance=4000.f;
        shadowVolumes=false;bloomBlurRounds=1;boxSparkleRatio=.04f;particleBudget=96;kartEffectScreens=2;racerLod=true;animationStep=4;vramCourseTextures=true;bottomRgba4=true;
    }
    // License Settings > Graphics: 0 Auto (Balanced on New 3DS, Performance on
    // Old 3DS), 1 Quality, 2 Performance (reduced detail, no dynamic shadow,
    // the race's lower screen at 30 FPS), 3 Balanced (full detail without the
    // dynamic shadow and bloom, the two biggest drawing costs).
    void setGraphics(unsigned choice,bool new3ds){
        if(choice==0)choice=new3ds?3:2;
        setReduced(choice==2);
        if(choice==3){dynamicShadow=false;bloom=false;}
    }
#ifdef MKW_RENDER_QUALITY_C3D
    // True when bounds b placed by transform t are too small to draw from camera.
    bool tooSmall(const Bounds& b,const C3D_Mtx& t,Vec3 camera,bool stereo=false) const {
        const float ratio=tinyRatio(stereo);
        if(ratio<=0)return false;
        // Compare squared radii/distances: this hot path runs for every
        // object and racer part. Preserve uncertain/borderline geometry.
        if(!(ratio<1.f))return screenRatio(b,t,camera)<ratio;
        const Vec3 c=(b.minimum+b.maximum)*.5f,h=(b.maximum-b.minimum)*.5f;
        float scale2=0;
        for(unsigned col=0;col<3;++col){
            const float x=t.r[0].c[3-col],y=t.r[1].c[3-col],z=t.r[2].c[3-col];
            scale2=std::max(scale2,x*x+y*y+z*z);
        }
        const Vec3 d{t.r[0].x*c.x+t.r[0].y*c.y+t.r[0].z*c.z+t.r[0].w-camera.x,
                     t.r[1].x*c.x+t.r[1].y*c.y+t.r[1].z*c.z+t.r[1].w-camera.y,
                     t.r[2].x*c.x+t.r[2].y*c.y+t.r[2].z*c.z+t.r[2].w-camera.z};
        return dot(h,h)*scale2 < ratio*ratio*dot(d,d)*(1.f-1e-5f);
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
    // Lighter 3D (diag::stereoShadowCull): the screenRatio below which a shadow
    // volume is skipped, its bounds' sphere under 8 px across on the 240 px top
    // screen at vertical FOV fovy (4 px radius = r/d * 120/tan(fovy/2); about 52
    // sphere radii away at 60 degrees). The visible shadow is where the volume
    // meets the ground: at most ~6 px wide (a box's footprint is under 0.71 of
    // its diagonal), and seen that far from a race camera a few hundred units
    // up, a pixel or two tall at half darkness. Off-centre it projects up to ~2x
    // wider; still a sliver.
    static float stereoShadowRatio(float fovy){return 4.f*std::tan(fovy*(3.14159265f/360.f))/120.f;}
    // True when a volume with bounds b at t is skipped for ratio (0: never).
    // Empty or unknown bounds always draw.
    static bool shadowTooSmall(const Bounds& b,const C3D_Mtx& t,Vec3 camera,float ratio){
        return ratio>0&&(b.maximum.x>b.minimum.x||b.maximum.y>b.minimum.y||b.maximum.z>b.minimum.z)&&screenRatio(b,t,camera)<ratio;
    }
#endif
};
inline RenderQuality& renderQuality(){static RenderQuality quality;return quality;}
}
