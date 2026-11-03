#include <mkw/item_odds.hpp>
#include <mkw/menu_ui.hpp>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
using namespace mkw;
int main(int argc,char** argv){
 assert(argc==2);std::ifstream input(argv[1],std::ios::binary);assert(input);
 const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
 ItemOdds odds;
 for(unsigned base:{6u,8u}){
  assert(odds.load(bytes.data(),bytes.size(),base,base+1));
  size_t off=1;for(unsigned table=0;table<base;++table)off+=2+bytes[off]*bytes[off+1];
  for(unsigned role=0;role<2;++role){assert(bytes[off]==3&&bytes[off+1]==19);off+=2;
   for(unsigned col=0;col<3;++col)for(unsigned item=0;item<19;++item)
    assert(odds.weights(role,col+1)[item]==bytes[off+item*3+col]);
   assert(odds.weights(role,12)==odds.weights(role,3));off+=3*19;
  }
 }
 assert(odds.load(bytes.data(),bytes.size(),2,3));
 assert(!odds.load(bytes.data(),bytes.size()-1,6,7));
 std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=RaceTeam(i&1);
 std::array<uint16_t,12> score{};
 // A rich teammate cannot push this racer into a stronger opponent column.
 score[2]=100;
 for(unsigned count:{4u,8u,12u})for(unsigned gap=0;gap<10;++gap){score[1]=gap;
  const unsigned lo=count<=8?1:2,hi=count<=4?3:count<=8?4:5;
  assert(battleItemColumn(false,0,count,score,teams)==(gap<=lo?1:gap<=hi?2:3));
  assert(battleItemColumn(true,0,count,score,teams)==(gap<=3?1:gap<=7?2:3));
 }
 score[0]=20;assert(battleItemColumn(false,0,12,score,teams)==1);
 UiInput accept{};accept.accept=true;UiInput back{};back.back=true;
 for(unsigned mode=0;mode<2;++mode){
  MenuState menu;menu.selection.mode=UiMode::Battle;menu.enter(UiPage::BattleMode);menu.cursor=mode;menu.input(accept);
  assert(menu.page==UiPage::Character&&menu.selection.coinRunners==bool(mode)&&menu.selection.teams);
  menu.cursor=0;menu.input(accept);assert(menu.page==UiPage::TeamConfirm);
  menu.input(accept);assert(menu.page==UiPage::Vehicle);
  menu.input(back);assert(menu.page==UiPage::TeamConfirm);
  menu.input(accept);menu.cursor=0;menu.input(accept);assert(menu.page==UiPage::Drift);
  menu.cursor=0;menu.input(accept);assert(menu.page==UiPage::BattleCup);
  for(unsigned stage=0;stage<10;++stage){menu.selection.battleStage=stage;assert(menu.supportsRace());}
  assert(menu.cpuRoster.ready);
 }
 puts("PASS original Battle ItemSlot tables and individual enemy-gap thresholds; both menu modes, team confirmation/back navigation and all10 arena start gates");
}
