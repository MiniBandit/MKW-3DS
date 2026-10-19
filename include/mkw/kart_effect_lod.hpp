#pragma once
#include "mkw/math.hpp"
#include "mkw/nw4r_ef.hpp"
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace mkw {
// The kart effect LOD of MKW's EffectManager (docs/VFX_RESEARCH.md, "Kart effect LOD").
// Addresses are USA StaticR (relocated at 0x8050bf60), RMCE main.dol below 0x80400000.
// Presentation only: it reads kart positions and the presented camera.

// A kart's entry in the clip manager (0x80783344): the KartDynamics position, a 200-unit
// sphere and a 100000-unit far distance (Player_init 0x80587a08 -> 0x80783688). The manager
// runs after the cameras every frame (GameScene::calc 0x80516f54 -> 0x8056075c), so the LOD
// of the next frame reads it through KartObjectProxy +0x50 (0x8058ad08).
struct EffectClip {
    bool culled=false;      // +0x20 + screen, bit 0: outside the frustum or beyond the far distance
    float distance=FLT_MAX; // +0xc + 4 * screen: the scaled squared distance to the camera
    // A race start (PlayerSub10_setInitialPhysicsValues 0x8057d820 -> 0x80783920) clears bit 0
    // of the entry's init flags (3) and keeps the init distance FLT_MAX until the first clip.
};

// One screen's clip camera (0x80782c4c).
struct EffectClipCamera {
    static constexpr float KartRadius=200.f,KartFar=100000.f; // 0x8088d6b8, 0x8088d6bc
    static constexpr float FovClamp=55.f,FovScale=0.0181818176060915f; // 0x808a0140, 0x808a0144
    static constexpr float FIdxPerDegree=0.7111111283302307f;            // 0x808a013c
    Vec3 position{};
    // Outward side-plane normals through the camera: left, right (normalised), top, bottom.
    std::array<Vec3,4> planes{};
    float distanceScale=1;

    // A look-at camera with world up (0, 1, 0) as the port presents it. fovy is the full
    // vertical field of view in degrees, which RaceCamera writes to the screen camera's +0x10
    // (0x80596ffc); aspect is the screen's width / height (+0x8 / +0xc). Retail passes fovy,
    // not fovy / 2, to SinCosFIdx, so its frustum is about twice as wide as the view.
    static EffectClipCamera lookAt(Vec3 eye,Vec3 target,float fovy,float aspect){
        EffectClipCamera c;
        const Vec3 back=normalized(eye-target,{0,0,1}),right=normalized(cross({0,1,0},back),{1,0,0}),up=cross(back,right);
        auto toWorld=[&](Vec3 v){return right*v.x+up*v.y+back*v.z;}; // VEC3TransformNormal by the inverse view
        auto unit=[](Vec3 v){const float d=dot(v,v);return d>0.f?v*(1.f/std::sqrt(d)):v;}; // 0x80782e90
        c.position=eye;
        float s,co;ef::sinCosFIdx(FIdxPerDegree*fovy,s,co);
        const float zoom=FovScale*(fovy>FovClamp?FovClamp:fovy);
        c.distanceScale=zoom*zoom;
        c.planes[0]=unit(toWorld({-co,0,s*aspect}));
        c.planes[1]=unit(toWorld({co,0,s*aspect}));
        c.planes[2]=toWorld({0,co,s});
        c.planes[3]=toWorld({0,-co,s});
        return c;
    }
    // One entry on one screen, as the clip manager tests a kart (+0x1e mode 2: no area mask,
    // init flags without 0x10: the four side planes only).
    EffectClip clip(Vec3 centre) const {
        const Vec3 d=centre-position;
        EffectClip out;
        out.distance=distanceScale*((d.z*d.z+d.x*d.x)+d.y*d.y);
        if(out.distance>KartFar*KartFar){out.culled=true;return out;} // flags |= 3
        for(const Vec3& n:planes)if((d.x*n.x+d.y*n.y)+d.z*n.z>KartRadius){out.culled=true;break;}
        return out;
    }
    // Object::LoadClipInfo mode1/2 (USA8080d614/8080d890): authored sphere
    // and far distance, no AREA masking (entry mode2). Mode2 also accepts
    // the horizontal sector formed by rotating the side planes to world up.
    bool objectCulled(Vec3 centre,float radius,float far,bool horizontal) const {
        const Vec3 d=centre-position;
        if(distanceScale*((d.z*d.z+d.x*d.x)+d.y*d.y)>far*far)return true;
        auto outside=[&](Vec3 n){return (d.x*n.x+d.y*n.y)+d.z*n.z>radius;};
        const bool side=outside(planes[0])||outside(planes[1]);
        if(!horizontal)return side||outside(planes[2])||outside(planes[3]);
        if(!side)return false;
        // USA807ce5ac: shortest-arc rotation of a vector from the side
        // planes' cross product to Y; dot below the original threshold
        // negates the vector. This preserves the original tilted-camera OR.
        Vec3 from=cross(planes[0],planes[1]);const float length=dot(from,from);
        if(length>0)from=from*(1.f/std::sqrt(length));
        const Vec3 to{0,1,0},axis=cross(from,to);const float co=dot(from,to);
        if(co==-1.f)return false; // original 0/0 yields no positive outside test
        auto turn=[&](Vec3 v){return co< -1.f?v*-1.f:v*co+cross(axis,v)+axis*(dot(axis,v)/(1.f+co));};
        return outside(turn(planes[0]))||outside(turn(planes[1]));
    }
};

// EffectManager's per-kart effect LOD (KartEffect +7) for one screen: every frame before the
// KartEffect updates (0x806789bc) it ranks the karts by distance (0x8067998c, 0x80679c34),
// sets +7 on every racer that no local player drives (0x8068f8d0) and clears it again for
// the screen's own kart and the nearest N that are visible and within the distance limit.
class KartEffectLod {
public:
    static constexpr unsigned MaxKarts=12;
    // 0x8067998c: the screen's own kart, a kart in range, beyond the limit, culled.
    enum Category : int32_t {Focus=0,Near=1,Far=2,Hidden=3};
    struct Limits { int32_t nearest; float distance; };
    // EffectManager init (0x806770f8): N (+0x3c) and the limit DAT_808bc7dc on the scaled
    // squared distance: 3 and 3000^2 with one screen, 2 and 2000^2 with two, 1 and 2000^2
    // with three or four; Time Trials 10000^2, the credits 50000^2 with N = every kart.
    static Limits limits(unsigned screens,int gameMode,unsigned karts){
        Limits l{3,9000000.f};
        if(screens==2)l={2,4000000.f};
        else if(screens>=3)l={1,4000000.f};
        if(gameMode==TimeTrialMode)l.distance=100000000.f;
        else if(gameMode==CreditsMode)l={int32_t(karts),2500000000.f};
        return l;
    }
    static constexpr int VsMode=1,TimeTrialMode=2,CreditsMode=12; // RaceConfig::Settings::GameMode

    // RaceScene init (0x806785c4 after 0x806770f8): ranks in kart order, +7 on every kart no
    // local player drives.
    void reset(unsigned karts,const std::array<bool,MaxKarts>& local,int gameMode=VsMode,unsigned screens=1){
        *this={};
        count_=karts<MaxKarts?karts:MaxKarts;limits_=limits(screens,gameMode,count_);
        for(unsigned k=0;k<count_;++k){rank_[k]=order_[k]=uint8_t(k);total_=uint16_t(total_+k);lod_[k]=!local[k];}
    }
    // One frame (0x806789bc) from the clip of the frame before. focus is the kart the
    // screen's camera follows (RaceCamera +0x9c), hudPlayer RaceConfig's hudPlayerIds[0].
    void update(unsigned focus,int hudPlayer,const std::array<bool,MaxKarts>& local,const std::array<EffectClip,MaxKarts>& clip){
        rank(focus,hudPlayer,clip);
        for(unsigned k=0;k<count_;++k)if(!local[k])lod_[k]=true;
        for(unsigned k=0;k<count_;++k)
            if(rank_[k]<=unsigned(limits_.nearest)&&category_[k]<=Near)lod_[k]=false;
    }
    bool lod(unsigned kart) const {return kart<count_&&lod_[kart];}
    // 0x80678818: a racer's effect group calculates while +7 is clear or +9 (an active
    // power-up effect, docs/VFX_RESEARCH.md) is set.
    bool calcs(unsigned kart,bool keepAlive) const {return !lod(kart)||keepAlive;}
    // 0x80678aec on this screen: the camera's own kart (when its KartBlink shows it, always
    // outside battles), a ranked kart in range, or +9.
    bool draws(unsigned kart,unsigned focus,bool keepAlive) const {
        if(kart==focus)return true;
        return (kart<count_&&category_[kart]==Near&&rank_[kart]<=unsigned(limits_.nearest))||keepAlive;
    }
    unsigned rankOf(unsigned kart) const {return rank_[kart];}
    Category category(unsigned kart) const {return category_[kart];}
    unsigned atRank(unsigned rank) const {return order_[rank];}
    const Limits& limits() const {return limits_;}
private:
    // 0x8067998c: categories and distances, the repair of a stale rank 0, then one pass that
    // moves each kart in range at most one place up, past a kart out of range or farther away.
    void rank(unsigned focus,int hudPlayer,const std::array<EffectClip,MaxKarts>& clip){
        for(unsigned k=0;k<count_;++k){
            if(k==focus){distance_[k]=-1.f;category_[k]=Focus;rank_[k]=0;order_[0]=uint8_t(k);}
            else if(clip[k].culled){distance_[k]=-2.f;category_[k]=Hidden;}
            else{
                distance_[k]=clip[k].distance;
                if(distance_[k]<limits_.distance)category_[k]=Near;
                else{category_[k]=Far;distance_[k]=-3.f;}
            }
        }
        repairRankZero();
        for(unsigned k=0;k<count_;++k){
            if(category_[k]!=Near)continue;
            int ahead=int8_t(rank_[k]-1);
            if(ahead==-1){
                // A kart in range still at rank 0 takes the screen's HUD player's index as its
                // rank. Unreachable while the focus never changes (the port's case); retail
                // would then read order[-1] when that index is 0.
                rank_[k]=uint8_t(hudPlayer);order_[0]=uint8_t(k);
                ahead=int8_t(rank_[k]-1);
            }
            if(ahead<=0)continue;
            const unsigned other=uint8_t(int8_t(order_[unsigned(ahead)]));
            if(other>=count_)continue;
            const Category c=category_[other];
            if(c==Far||c==Hidden||(c==Near&&distance_[k]<distance_[other])){
                --rank_[k];++rank_[other];
                order_[unsigned(ahead)]=uint8_t(k);order_[unsigned(ahead)+1]=uint8_t(other);
            }
        }
    }
    // 0x80679c34: when more than one kart has rank 0 (the focus changed), the last of them
    // that is not the focus takes the missing rank, the rank total minus the sum of ranks.
    void repairRankZero(){
        unsigned zeros=0;int stale=-1;
        for(unsigned k=0;k<count_;++k)if(rank_[k]==0){++zeros;if(category_[k]!=Focus)stale=int(k);}
        if(zeros==1)return;
        int sum=0;
        for(unsigned k=0;k<count_;++k)sum+=int8_t(rank_[k]);
        const int missing=int(total_)-sum;
        if(stale<0||missing<0||missing>=int(count_))return; // retail writes out of bounds here
        rank_[unsigned(stale)]=uint8_t(missing);order_[unsigned(missing)]=uint8_t(stale);
    }
    std::array<uint8_t,MaxKarts> rank_{},order_{};
    std::array<float,MaxKarts> distance_{};
    std::array<Category,MaxKarts> category_{};
    std::array<bool,MaxKarts> lod_{};
    uint16_t total_=0;
    unsigned count_=0;
    Limits limits_{3,9000000.f};
};
}
