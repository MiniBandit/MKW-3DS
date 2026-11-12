#pragma once
#include <mkw/math.hpp>
#include <cstdint>

namespace mkw {
struct AwardEnvironmentObject {
 uint16_t id,index;int16_t model,route;uint16_t presence;const char* clip;
 Vec3 position,rotation,scale;
 uint16_t settings[8];
};
struct AwardEnvironmentStart {Vec3 position,rotation;int16_t player;};
struct AwardEnvironmentRoute {const unsigned char* data;};
struct AwardEnvironmentDef {
 const char* name;unsigned course,sky;
 const AwardEnvironmentObject* objects;unsigned count;
 const AwardEnvironmentRoute* routes;unsigned routeCount;
 const AwardEnvironmentStart* starts;unsigned startCount;
};
}
