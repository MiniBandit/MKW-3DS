// Host checks of source timing, retail parameter data and accepted menu paths.
// No game/emulator launch and no framebuffer or physical-device claims.
#include <bit>
#include <mkw/blooper.hpp>
#include <mkw/cpu_speed.hpp>
#include <mkw/menu_ui.hpp>
#include <game/system/RaceConfig.hh>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cstdio>
using namespace mkw;
static void near(float a,float b){assert(std::fabs(a-b)<.000001f);}
int main(int argc,char** argv){
 assert(argc==2);
 std::ifstream f(argv[1],std::ios::binary);
 std::vector<char> data((std::istreambuf_iterator<char>(f)),{});
 // Known values from the original kartAISpdParam.bin, including a second
 // course. GP's course-major records differ from VS's class-major records.
 CpuSpeedParam p;
 assert(readCpuSpeedParam(data.data(),data.size(),0,0,1,p,true));near(p.advantage,-18);near(p.minimum,30);
 assert(readCpuSpeedParam(data.data(),data.size(),0,1,1,p,true));near(p.advantage,-8);near(p.minimum,40);
 assert(readCpuSpeedParam(data.data(),data.size(),0,2,1,p,true));near(p.advantage,0);near(p.minimum,50);
 assert(readCpuSpeedParam(data.data(),data.size(),1,0,1,p,true));near(p.advantage,-13);near(p.bias,1.1f);
 assert(readCpuSpeedParam(data.data(),data.size(),0,2,1,p));near(p.advantage,2);near(p.minimum,50);
 assert(!readCpuSpeedParam(data.data(),data.size()-1,0,2,1,p));
 assert(!readCpuSpeedParam(data.data(),data.size(),32,2,1,p));
 Kinoko::System::RaceConfig::Scenario scenario;
 for(unsigned c=0;c<3;++c){scenario.engineClass=c;near(scenario.engineClassRate(),c==0?.8f:c==1?.9f:1.f);}
 // 50cc and 100cc reach a real StartRace action, with their authored vehicle
 // restriction; changing the engine-class type switch restores all 12 slots.
 UiInput accept;accept.accept=true;
 for(unsigned c=0;c<3;++c){
  MenuState menu;menu.enter(UiPage::EngineClass);menu.cursor=c;
  menu.input(accept);assert(menu.page==UiPage::Character&&!menu.unavailable);
  menu.input(accept);assert(menu.page==UiPage::Vehicle&&menu.count()==(c<2?6:12));
  for(unsigned i=0;i<menu.count();++i)assert((menu.vehicleChoice(i)%6<3)==(c==0)||c==2);
  menu.input(accept);menu.input(accept);menu.input(accept);
  assert(menu.page==UiPage::Confirm&&menu.supportsRace());
  assert(menu.input(accept)==UiAction::StartRace);
  MenuState mixed;mixed.selection.mixedEngineTypes=true;mixed.selection.engineClass=UiEngineClass(c);mixed.enter(UiPage::Vehicle);assert(mixed.count()==12);
 }
 // Entry disappears at the source clip boundary, then the player's attack
 // starts at its staggered frame. Withdrawal interrupts attack without ink.
 constexpr unsigned start=1000;
 assert(!blooperVisual(start-1,start,0,0,1).clip);
 assert(!std::strcmp(blooperVisual(start+69,start,0,0,1).clip,"start"));
 assert(!blooperVisual(start+70,start,0,0,1).clip);
 for(unsigned i=0;i<12;++i){
  const unsigned attack=start+blooperHitDelay(i)-148;
  assert(!blooperVisual(attack-1,start,attack,0,1).clip);
  auto v=blooperVisual(attack,start,attack,0,1);assert(!std::strcmp(v.clip,"attack")&&v.frame==0);
  v=blooperVisual(attack+148,start,attack,0,2);assert(v.frame==148);near(v.forward,140);near(v.scale,.44f);
  assert(!blooperVisual(attack+176,start,attack,0,1).clip);
  const unsigned end=attack+20;
  v=blooperVisual(end,start,attack,end,1);assert(!std::strcmp(v.clip,"attack_end")&&v.frame==0);
  assert(!blooperVisual(end+80,start,attack,end,1).clip);
 }
 std::puts("PASS retail GP/VS parameter records, 50/100/150cc selection and rates, all 12 Blooper attack schedules and clip boundaries");
}
