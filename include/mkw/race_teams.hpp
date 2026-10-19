#pragma once
#include <array>
#include <cstdint>

namespace mkw {
// Retail RaceConfig::Player team ids, including the non-team sentinel.
enum class RaceTeam : uint8_t {Red=0,Blue=1,None=2};
constexpr bool sameRaceTeam(RaceTeam a,RaceTeam b){return a!=RaceTeam::None&&a==b;}
// Single-player TeamConfirm::prepareOfflineSingle (RMCU 808309B4):
// choose the human's side, then fill CPU slots backwards, alternating sides
// and skipping a full team. For twelve racers this is exactly parity below.
inline std::array<RaceTeam,12> offlineVsTeams(bool enabled,RaceTeam human){
 std::array<RaceTeam,12> teams;teams.fill(RaceTeam::None);
 if(!enabled||human==RaceTeam::None)return teams;
 for(unsigned i=0;i<teams.size();++i)teams[i]=RaceTeam(unsigned(human)^(i&1));
 return teams;
}
// Shared by the menu preview and current offline race roster.
constexpr unsigned offlineRaceDriver(unsigned local,unsigned slot,unsigned count=24){return (local+slot)%count;}
constexpr const char* teamVehicleSuffix(unsigned vehicle,RaceTeam team){
 return (vehicle<3||(vehicle>=18&&vehicle<=20))?(team==RaceTeam::Red?"red_":team==RaceTeam::Blue?"blue_":""):"";
}
struct RaceTeamScore {
 unsigned previous=0,gain=0;
 unsigned total()const{return previous+gain;}
};
}
