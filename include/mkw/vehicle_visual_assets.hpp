#pragma once
#include <mkw/animation.hpp>
#include <mkw/garage.hpp>
#include <mkw/race_teams.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace mkw {
// Owned meshes and banks, independent of KartObject / RaceConfig lifetimes.
// Each actor owns deforming vertices; Model still shares immutable textures.
struct VehicleVisualAssets {
 const GarageChoice* choice=nullptr;
 std::unique_ptr<Model> body,driver,shadow;
 std::array<std::unique_ptr<Model>,4> wheels;
 std::array<std::unique_ptr<Model>,9> extras;
 std::unique_ptr<DriverAnimation> animation,bodyAnimation;
 std::array<std::unique_ptr<DriverAnimation>,9> extraAnimation;
 // Worker-safe transaction: missing required parts leave the current set intact.
 // Replacing an already rendered set requires a GPU drain by the caller.
 // unlitBanks (opponents, DIAG): the driver bank comes from its compact .dsku
 // when race.pak has one (tools/pack_unlit_banks.py), else the .dskm.
 // live: models keep their normals for shader lighting (no load-time bake).
 bool load(unsigned character,unsigned vehicle,RaceTeam team,std::string* failure=nullptr,bool unlitBanks=false,bool live=false);
 // The animation bank paths load() would read, for DriverAnimation::keepOnly.
 static void bankPaths(unsigned character,unsigned vehicle,RaceTeam team,bool unlitBanks,std::vector<std::string>& out);
};
}
