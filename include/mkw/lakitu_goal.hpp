#pragma once
#include <mkw/math.hpp>
namespace mkw {
// Goal offsets stay in the current kart/camera frame while the racer keeps
// driving. Keeping a finish-line world point leaves Lakitu behind the kart.
inline Vec3 lakituGoalAnchor(Vec3 kart,Vec3 cameraForward,float height){
    const Vec3 forward=normalized({cameraForward.x,0,cameraForward.z},{0,0,1});
    return kart+forward*700.f+Vec3{0,height,0};
}
}
