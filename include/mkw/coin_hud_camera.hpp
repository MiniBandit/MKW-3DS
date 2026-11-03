#pragma once
#include <mkw/coin_hud_stack.hpp>
#include <c3d/maths.h>
namespace mkw {
inline void coinHudCamera(C3D_Mtx& projection,C3D_Mtx& view){
    Mtx_OrthoTilt(&projection,-CoinHudCamera::Width*.5f,CoinHudCamera::Width*.5f,
        -CoinHudCamera::Height*.5f,CoinHudCamera::Height*.5f,CoinHudCamera::Near,CoinHudCamera::Far,false);
    Mtx_LookAt(&view,FVec3_New(0,0,CoinHudCamera::EyeZ),FVec3_New(0,0,0),FVec3_New(0,1,0),false);
}
inline C3D_Mtx coinHudColumnTransform(const CoinHudStack::Column& c){
    C3D_Mtx transform;Mtx_Identity(&transform);
    Mtx_Translate(&transform,c.position.x,c.position.y,c.position.z,false);
    Mtx_RotateX(&transform,c.pitch,true);Mtx_Scale(&transform,c.scale.x,c.scale.y,c.scale.z);
    return transform;
}
}
