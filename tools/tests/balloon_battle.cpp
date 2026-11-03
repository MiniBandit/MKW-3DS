#include <mkw/balloon_battle.hpp>
#include <mkw/menu_ui.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 BalloonBattle b;b.reset(12);assert(b.rank(0)==1&&b.balloons(0)==3);
 assert(b.loseBalloon(1,0)&&b.score(0)==1&&b.balloons(1)==2);
 assert(b.loseBalloon(1,1)&&b.score(1)==0);
 assert(b.loseBalloon(0,2)&&b.score(2)==1);
 assert(b.loseBalloon(0,2)&&b.loseBalloon(0,2));assert(b.balloons(0)==3&&b.score(0)==0&&b.score(2)==3&&b.rank(2)==1);
 auto hits=b.hits();assert(!b.loseBalloon(12,0)&&b.hits()==hits);
 assert(b.loseBalloon(3,0xff)&&b.balloons(3)==2);
 for(unsigned arena=0;arena<10;++arena){MenuState s;s.selection.mode=UiMode::Battle;s.selection.battleStage=arena;assert(s.supportsRace());s.selection.coinRunners=true;assert(!s.supportsRace());}
 MenuState s;s.selection.mode=UiMode::Battle;s.selection.battleStage=10;assert(!s.supportsRace());
 s.enter(UiPage::BattleMode);s.cursor=1;UiInput input;input.accept=true;s.input(input);assert(s.unavailable&&s.page==UiPage::BattleMode);
 puts("PASS upstream balloon loss/score/reset/rank and invalid hits; all ten arena gates, Coin Runners remains unavailable");
}
