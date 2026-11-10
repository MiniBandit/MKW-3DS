#pragma once
#include <cstring>
#include "mkw/retail_lighting.hpp"
namespace mkw::opaqueOrder {
// NW4R draws a model's DrawOpa list in authored order, then its DrawXlu list.
// A DrawOpa material that blends and writes depth stays in that first list:
// shapes drawn after it are depth-tested against it. The cook marks every
// blended material translucent (Submesh flag 2), which moved these to the
// late pass. Sherbet Land's ice floor a2_jimen then let the ice walls
// a3_kabe1 and snow yukiyuka (after it in DrawOpa) show through it.
// The same applies to Dry Dry Ruins sand_m, GCN Waluigi Stadium
// wl_road3BANKover and Chain Chomp Wheel cs_glass_m. Rainbow Road z2_road
// keeps its own pass (rainbow_shadow.hpp).
struct Range {const char* name;unsigned vertices,indices,first,count;};
inline constexpr Range Ranges[]={
{"course_27_000.gmdl",7919,22587,14001,1086}, // a2_jimen
{"course_14_000.gmdl",20844,52638,39645,8358}, // sand_m
{"course_18_000.gmdl",16166,35475,25680,2247}, // wl_road3BANKover
{"course_34_000.gmdl",11267,31656,25836,2280}, // cs_glass_m
};
template<class Model> void apply(Model& model,const char* path){
const char* name=std::strrchr(path,'/');name=name?name+1:path;
for(const auto& r:Ranges)if(model.vertexCount==r.vertices&&model.indexCount==r.indices&&!std::strcmp(name,r.name))
for(auto& s:model.submeshes)if(s.first==r.first&&s.count==r.count&&(s.flags&2)&&(s.flags&(1u<<24)))s.lighting|=retail::AuthoredOpaqueOrder;
}
}
