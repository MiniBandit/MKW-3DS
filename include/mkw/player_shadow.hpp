#pragma once
// What remains of the removed Dynamic Shadows feature: its solid-surface
// test, used by the Dry Dry Ruins intro's scenery clearance (main.cpp).
namespace mkw::playerShadow {
// Authoritative GX blend/Z state, already preserved by gx_material.py.
// Light shafts/glow/decal alpha passes are not solid surfaces.
inline bool receives(unsigned flags){
 // Authored alpha-blended sand still writes solid scene depth. Exclude
 // additive glow, then trust the explicit GX depth state for both passes.
 if(flags&(1u<<31))return !(flags&64)&&(flags&(1u<<23))&&(flags&(1u<<24));
 return !(flags&2);
}
}
