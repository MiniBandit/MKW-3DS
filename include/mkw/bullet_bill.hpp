#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
// Recovered from the locally supplied RMCU StaticR.rel KartKiller routines.
// Source addresses below refer to the USA relocated image.
inline constexpr unsigned BulletDuration=480,BulletMinimumDuration=180,BulletEndDuration=70;
inline constexpr float BulletModelHeight=150.f,BulletUpSmoothing=.1f;
inline float bulletTurnRate(float previous,float yawRadians){
    const float target=.03f+std::fabs(yawRadians)*57.295780181884766f*.003f;
    return previous+.05f*(target-previous);
}
// Killer::UpdateRoute USA805b1284..1410. Radius check is XZ only;
// route direction is normalised in 3D before its Y component is discarded.
inline bool bulletAdvancePoint(Vec3 position,Vec3 previous,Vec3 target,float width){
    Vec3 direction=normalized(target-previous,{});direction.y=0;
    Vec3 delta=position-target;delta.y=0;
    const float radius=std::clamp(width*50.f,700.f,1000.f);
    return dot(delta,direction)>0||dot(delta,delta)<radius*radius;
}
inline unsigned bulletTargetRank(unsigned rank,unsigned racers){
    constexpr std::array<unsigned,12> gains={1,1,1,1,2,2,3,3,4,4,5,5};
    const unsigned gain=gains[std::clamp(racers,1u,12u)-1];
    return rank>gain?rank-gain:1;
}
// Retail Killer::UpdateGravity (USA805b2434): setting1 1 forces gravity,
// 2 forces flight, 3 uses the route tube without the route-plane side test.
// UpdateGravity precedes the wall-contact history update (USA805b1870).
struct BulletGravity {
    bool latched=false,noGravity=false;
    unsigned recovery=0,wallFrames=0,wallGrace=0;
    float wallNormalY=0;
    void reset(){*this={};}
    bool step(uint16_t setting1,bool insideRoute,bool beforeRespawn){
        if(setting1==1){noGravity=false;latched=false;}
        else if(setting1==2){noGravity=true;latched=false;}
        else if(!latched&&insideRoute){noGravity=true;latched=true;}
        if(wallFrames>10&&wallNormalY<-.75f)recovery=60;
        if(beforeRespawn||recovery){noGravity=false;latched=false;}
        if(recovery)--recovery;
        return noGravity;
    }
    void observeWall(bool contact,float normalY){
        if(contact){wallNormalY=normalY;wallGrace=4;}
        if(contact||wallGrace){if(++wallFrames>=120)wallFrames=0;}
        else wallFrames=0;
        if(wallGrace)--wallGrace;
    }
};
// USA805b2acc: project on the route line, compare perpendicular distance
// against interpolated ITPT width * 50. The interpolation alone is clamped;
// the distance is to the line, not a clamped segment or spherical endpoint.
inline bool bulletInsideRoute(Vec3 position,Vec3 start,Vec3 end,
        float startWidth,float endWidth,Vec3 normal,bool checkRouteSide,float sideThreshold){
    const Vec3 fromStart=position-start;
    // USA805b2b38 uses currentPoint - position, not position - currentPoint.
    // Reject BELOW the authored route plane; ordinary road points sit above
    // the kart origin. Reversing this test enabled flight over normal slopes.
    if(checkRouteSide&&dot(normal,fromStart)<-sideThreshold)return false;
    const Vec3 delta=end-start;const float distance=length(delta);
    float offset=0,allowed=startWidth*50.f;
    if(distance>0){
        const Vec3 dir=delta/distance;const float along=dot(fromStart,dir);
        offset=length(dir*along-fromStart);
        allowed+=(endWidth-startWidth)*50.f*std::clamp(along/distance,0.f,1.f);
    }
    return offset<allowed;
}
inline Vec3 bulletVisualPosition(Vec3 position,Vec3 smoothedUp,float initialYPos){
    // Retail .text a4f28..a4fa4: compensate the vehicle-specific BSP origin;
    // kart_killer's origin is its centre, not its bottom vertex.
    return position+smoothedUp*(BulletModelHeight-initialYPos);
}
}
