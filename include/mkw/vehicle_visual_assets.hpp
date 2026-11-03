#pragma once
#include <mkw/animation.hpp>
#include <mkw/garage.hpp>
#include <mkw/race_teams.hpp>
#include <array>
#include <memory>
#include <string>

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
 // lowOnly: just the shadow and any separate tires, for a racer drawn entirely
 // with its low-detail models (RacerVisuals, Old 3DS memory).
 bool load(unsigned character,unsigned vehicle,RaceTeam team,std::string* failure=nullptr,bool lowOnly=false);
};
}
