#include <mkw/battle_cpu_speed.hpp>
#include <mkw/battle_hud_assets.hpp>
#include <mkw/battle_map_assets.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>

int main(){
    using namespace mkw;
    auto close=[](float a,float b){assert(std::fabs(a-b)<.0001f);};
    constexpr Vec3 up{0,1,0},front{0,0,1};
    auto target=[](float degrees){const float a=degrees/57.295780181884765625f;return Vec3{std::sin(a)*2000,0,std::cos(a)*2000};};
    close(battleCpuSpeed(80,0,false,target(0),up,front),80);
    close(battleCpuSpeed(80,0,false,target(20),up,front),80);
    close(battleCpuSpeed(80,0,false,target(55),up,front),44);
    close(battleCpuSpeed(80,0,false,target(-55),up,front),44);
    close(battleCpuSpeed(80,0,false,target(90),up,front),8);
    close(battleCpuSpeed(80,0,false,target(180),up,front),8);
    close(battleCpuSpeed(80,10,true,target(55),up,front),30.8f);
    close(battleCpuSpeed(80,100,false,target(55),up,front),0);
    // Vertical target separation and non-unit body-up must not add a turn.
    close(battleCpuSpeed(80,10,false,{0,5000,2000},{0,2,0},front),70);
    close(battleCpuSpeed(80,10,false,{0,2000,0},up,front),70);
    close(battleCpuSpeed(80,10,false,target(90),{},front),70);
    close(battleCpuSpeed(80,0,false,{0,2000,2000},{0,1,1},{0,1,-1}),80);
    assert(raceMapCourse(31)==&RaceMapCourses[31]);
    for(unsigned i=32;i<42;++i){const auto* m=raceMapCourse(i);assert(m==&BattleMapCourses[i-32]);assert(m->x1>m->x0&&m->z1>m->z0);assert(m->width==512&&m->overviewWidth==256);}
    assert(!raceMapCourse(42)&&!raceMapCourse(~0u));
    assert(BattleHud_red_c.x<BattleHud_hifun.x&&BattleHud_hifun.x<BattleHud_blue_a.x);
    assert(BattleHud_blue_c.x+BattleHud_blue_c.w<=400);
    assert(BattleHud_personal.y+BattleHud_personal.h<=240);
    std::puts("PASS original Battle speed: 20/55/90/180 degrees, ink, penalty, slope/degenerate projection; 10 authored arena map cameras and HUD bounds");
}
