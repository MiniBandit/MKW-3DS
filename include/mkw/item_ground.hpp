#pragma once
#include "mkw/race_items.hpp"
namespace mkw::items {
// Keep a resting sphere in contact without moving it one unit into the floor.
// Exact tangency can produce no collision on the next KCL movement query.
inline ItemSurface groundContact(ItemWorldQuery query,const ItemActor& a){
    if(!query)return {};
    auto surface=query(a.previous,a.position,a.terrainRadius);
    if(!a.settled||surface.kill||surface.cannon||length(surface.floorNormal)>.1f)return surface;
    const Vec3 inset=normalized(a.up,{0,1,0});
    auto probe=query(a.previous,a.position-inset,a.terrainRadius);
    if(length(probe.floorNormal)>.1f&&!probe.kill&&!probe.cannon){
        surface.floorNormal=probe.floorNormal;
        surface.floorTypes=probe.floorTypes;
        surface.floorMotion=probe.floorMotion;surface.floorObject=probe.floorObject;surface.floorObjectY=probe.floorObjectY;
        surface.correction=probe.correction-inset;
    }
    return surface;
}
inline Vec3 scatteredGroundVelocity(Vec3 velocity,Vec3 normal){
    // The airborne .995 damping is insufficient for a dropped object on a
    // floor. Stop small residual motion rather than sliding indefinitely.
    const Vec3 grounded=tangent(velocity,normal)*.8f;
    return length(grounded)<.5f?Vec3{}:grounded;
}
}
