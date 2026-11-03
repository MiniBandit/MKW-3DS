#pragma once
#include <mkw/race_map_assets.hpp>
namespace mkw {
// Original arena map_model mesh and posLD/posRU world camera.
inline constexpr std::array<RaceMapCourse,10> BattleMapCourses{{
{"textures/battle_map_32.t3x","textures/battle_map_32_overview.t3x",-18750.000000000f,-18750.000000000f,18750.000000000f,18750.000000000f,512,512,256,256,64.f,{0,0},{0,0}}, // Block Plaza
{"textures/battle_map_33.t3x","textures/battle_map_33_overview.t3x",-35106.617187500f,-41483.597656250f,31267.771484375f,24888.080078125f,512,512,256,256,64.f,{0,0},{0,0}}, // Delfino Pier
{"textures/battle_map_34.t3x","textures/battle_map_34_overview.t3x",-40877.339843750f,-43521.910156250f,40800.000000000f,43560.000000000f,512,512,256,256,64.f,{0,0},{0,0}}, // Funky Stadium
{"textures/battle_map_35.t3x","textures/battle_map_35_overview.t3x",-28935.326171875f,-28935.326171875f,28935.326171875f,28935.326171875f,512,512,256,256,64.f,{0,0},{0,0}}, // Chain Chomp Wheel
{"textures/battle_map_36.t3x","textures/battle_map_36_overview.t3x",-16067.581054688f,-16067.581054688f,16067.581054688f,16067.581054688f,512,512,256,256,64.f,{0,0},{0,0}}, // Thwomp Desert
{"textures/battle_map_37.t3x","textures/battle_map_37_overview.t3x",-27401.882812500f,-27100.140625000f,26774.330078125f,27076.072265625f,512,512,256,256,64.f,{0,0},{0,0}}, // SNES Battle Course 4
{"textures/battle_map_38.t3x","textures/battle_map_38_overview.t3x",-27998.746093750f,-27697.003906250f,27371.193359375f,27672.935546875f,512,512,256,256,64.f,{0,0},{0,0}}, // GBA Battle Course 3
{"textures/battle_map_39.t3x","textures/battle_map_39_overview.t3x",-29267.791015625f,-28966.046875000f,28640.236328125f,28941.978515625f,512,512,256,256,64.f,{0,0},{0,0}}, // N64 Skyscraper
{"textures/battle_map_40.t3x","textures/battle_map_40_overview.t3x",-12898.162109375f,-12898.162109375f,12898.162109375f,12898.162109375f,512,512,256,256,64.f,{0,0},{0,0}}, // GCN Cookie Land
{"textures/battle_map_41.t3x","textures/battle_map_41_overview.t3x",-18672.011718750f,-18705.765625000f,18715.453125000f,18681.697265625f,512,512,256,256,64.f,{0,0},{0,0}}, // DS Twilight House
}};
inline const RaceMapCourse* raceMapCourse(unsigned index){
return index<RaceMapCourses.size()?&RaceMapCourses[index]:index-32<BattleMapCourses.size()?&BattleMapCourses[index-32]:nullptr;
}
}
