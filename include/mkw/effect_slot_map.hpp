#pragma once
// Compile-time map of the race effect slots: every fixed range must stay apart.
// A clash (Blooper ink on Moo Moo Meadows' mole slots, until Nov 10) is otherwise
// silent: two effects take turns killing each other's emitters.
#include <mkw/kart_item_effects.hpp>
#include <mkw/kart_smoke_effects.hpp>
#include <mkw/kart_surface_effects.hpp>
#include <mkw/creature_effects.hpp>
#include <mkw/blooper_screen_effects.hpp>
#include <mkw/opponent_effects.hpp>
#include <mkw/bike_drift_effects.hpp>
#include <mkw/bike_scrape_effects.hpp>

namespace mkw::effect_slots {
struct Range {unsigned first,end;};
inline constexpr Range Ranges[]={
 {KartFxSlots::FirstSlot,KartFxSlots::FirstFreeSlot},
 {KartFxSlots::FirstFreeSlot,KartFxSlots::FirstFreeSlot+8}, // start dust (main.cpp)
 {RemoteKartSmoke::FirstSlot,RemoteKartSmoke::FirstSlot+(RemoteKartSmoke::MaxRacers-1)*RemoteKartSmoke::SlotsPerRacer},
 {KartLandingEffects::FirstSlot,KartLandingEffects::FirstSlot+12*3},
 {CreatureEffects::FirstSlot,CreatureEffects::CrabFirstSlot},
 {CreatureEffects::CrabFirstSlot,CreatureEffects::PokeyFirstSlot},
 {CreatureEffects::PokeyFirstSlot,CreatureEffects::FactoryFirstSlot},
 {CreatureEffects::FactoryFirstSlot,CreatureEffects::MoleFirstSlot},
 {CreatureEffects::MoleFirstSlot,CreatureEffects::MoleFirstSlot+CreatureEffects::MaxMoles*2},
 {BlooperScreenEffects::FirstSlot,BlooperScreenEffects::FirstSlot+5},
 {OpponentDrivingEffects::FirstSlot,OpponentDrivingEffects::EndSlot},
 {CreatureEffects::PropellerFirstSlot,CreatureEffects::VolcanoFirstSlot},
 {CreatureEffects::VolcanoFirstSlot,CreatureEffects::VolcanoFirstSlot+CreatureEffects::MaxVolcanoBalls*2},
 {KartBrakeEffects::FirstSlot,KartBrakeEffects::FirstSlot+KartBrakeEffects::MaxRacers},
 {CreatureEffects::LeafFirstSlot,CreatureEffects::ChimneyFirstSlot},
 {CreatureEffects::ChimneyFirstSlot,CreatureEffects::ChimneyFirstSlot+CreatureEffects::MaxChimneys*CreatureEffects::ChimneySlots},
 {KartSurfaceEffects::FirstSlot,KartSurfaceEffects::FirstSlot+12*KartSurfaceEffects::Slots},
 {BikeDriftEffects::FirstSlot,BikeDriftEffects::FirstSlot+BikeDriftEffects::Slots},
 {BikeScrapeEffects::FirstSlot,BikeScrapeEffects::FirstSlot+BikeScrapeEffects::Slots},
 {KartWheelSpinEffects::FirstSlot,KartWheelSpinEffects::FirstSlot+KartWheelSpinEffects::SlotsPerRacer*KartWheelSpinEffects::MaxRacers},
 {CreatureEffects::FlameFirstSlot,CreatureEffects::FlameFirstSlot+2*CreatureEffects::MaxFlames},
 {CreatureEffects::N64StatueFirstSlot,CreatureEffects::N64StatueFirstSlot+CreatureEffects::MaxN64Statues},
 {KartGasSmoke::FirstSlot,KartGasSmoke::FirstSlot+KartGasSmoke::MaxRacers*KartGasSmoke::Pipes},
 {KartFxSlots::PowReduceFirstSlot,KartFxSlots::PowReduceFirstSlot+KartFxSlots::MaxRacers},
 {CreatureEffects::PoihanaFirstSlot,CreatureEffects::FireSnakeFirstSlot},
 {CreatureEffects::FireSnakeFirstSlot,CreatureEffects::FireSnakeFirstSlot+CreatureEffects::MaxFireSnakes},
};
constexpr bool apart(){
 for(const auto& a:Ranges){
  if(a.first<32||a.end>RaceEffects::SlotCount)return false; // 0..31: fixed race slots (main.cpp)
  for(const auto& b:Ranges)if(&a!=&b&&a.first<b.end&&b.first<a.end)return false;
 }
 return true;
}
static_assert(apart(),"race effect slot ranges overlap or leave 32..SlotCount");
}
