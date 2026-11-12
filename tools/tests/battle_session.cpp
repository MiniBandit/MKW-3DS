#include <mkw/battle_session.hpp>
#include <mkw/menu_ui.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/race_results.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace mkw;
int main(){
 std::array<uint16_t,12> scores{};std::array<RaceTeam,12> teams;
 for(unsigned i=0;i<12;++i)teams[i]=RaceTeam(i&1);
 BattleSession set;
 for(unsigned target:BattleSession::WinCounts){
  set.reset(target);assert(set.target()==target&&!set.complete()&&!set.nextRound());
  scores={};assert(set.seal(scores,teams,12)&&set.roundWinner()==RaceTeam::None);
  assert(set.wins(RaceTeam::Red)==0&&set.wins(RaceTeam::Blue)==0&&set.nextRound());
  scores[1]=5;assert(set.seal(scores,teams,12));
  assert(set.roundWinner()==RaceTeam::Blue&&set.wins(RaceTeam::Blue)==1);
  assert(!set.seal(scores,teams,12)&&set.wins(RaceTeam::Blue)==1);
  for(unsigned n=0;n<target;++n){
   assert(set.nextRound());scores={};scores[2]=3;
   assert(set.seal(scores,teams,12));assert(set.complete()==(n+1==target));
  }
  assert(set.winner()==RaceTeam::Red&&set.wins(RaceTeam::Red)==target&&!set.nextRound());
 }
 UiInput accept{};accept.accept=true;UiInput back{};back.back=true;UiInput right{};right.right=true;
 MenuState menu;menu.selection.mode=UiMode::Battle;menu.enter(UiPage::BattleMode);
 menu.cursor=menu.auxiliarySlot();menu.input(accept);assert(menu.page==UiPage::BattleRules&&menu.count()==6);
 menu.cursor=4;
 for(unsigned n=0;n<6;++n){assert(menu.selection.battleWins==UiBattleWinCounts[n]);menu.input(right);}
 assert(menu.selection.battleWins==2);
 menu.cursor=0;menu.input(right);assert(menu.selection.battleCpu==2);
 menu.input(back);assert(menu.page==UiPage::BattleMode&&menu.selection.battleCpu==1); // B cancels
 menu.input(accept);assert(menu.page==UiPage::BattleRules);
 menu.cursor=0;menu.input(right);menu.cursor=5;menu.input(accept);
 assert(menu.page==UiPage::BattleMode&&menu.selection.battleCpu==2); // OK commits
 menu.cursor=1;menu.input(accept);menu.cursor=0;menu.input(accept);menu.input(accept);
 assert(menu.page==UiPage::Vehicle&&menu.count()==2&&menu.vehicleChoice(0)==0&&menu.vehicleChoice(1)==3);
 menu.selection.battleCourses=2;menu.input(accept);menu.input(accept);
 assert(menu.page==UiPage::Confirm&&menu.selection.battleStage==0);
 for(unsigned n=1;n<10;++n){menu.finish(false);menu.cursor=0;assert(menu.input(accept)==UiAction::StartRace);assert(menu.selection.battleStage==n);}
 menu.selection.battleCourses=1;menu.prepareBattleStages();
 std::array<bool,10> used{};for(unsigned n=0;n<10;++n){assert(!used[menu.selection.battleStage]);used[menu.selection.battleStage]=true;menu.nextBattleStage();}
 assert(menu.selection.battleStage==menu.battleStageDeck[0]);
 // Exercise actual source BRCTR composition/caption lookup for every setting.
 for(unsigned target:BattleSession::WinCounts){
  menu.selection.battleWins=target;menu.enter(UiPage::BattleRules);menu.advance(60);
  auto quads=composeMenu(menu,{});unsigned text=0;
  char wanted[24];snprintf(wanted,sizeof wanted,"Wins: %u",target);
  bool caption=false;
  for(const auto& q:quads)if(q.sprite<std::size(UiSprites)){const auto& s=UiSprites[q.sprite];text+=*s.text!=0;caption|=!strcmp(s.text,wanted);}
  assert(text&&caption);
 }
 // Battle rows add this round's score from0 and expose session wins independently.
 RaceResultsFlow flow;RaceResultEntry row[2]={{0,0,true,0,3,RaceTeam::Red},{1,7,false,0,2,RaceTeam::Blue}};
 for(bool final:{false,true}){
  flow.reset();flow.finish();bool filled=false;
  for(unsigned t=0;t<5000&&flow.phase()!=RaceFlowPhase::Menu;++t){
   const auto event=flow.step({true,false});
   if(event&RaceResultsFlow::EventFill){assert(!filled);filled=true;flow.fill(row,2,final);flow.setBattle(final?2:1,0,2);}
  }
  assert(filled&&flow.phase()==RaceFlowPhase::Menu&&flow.battle()&&flow.teamDifference()==1);
  assert(flow.battleWins(RaceTeam::Red)==(final?2:1)&&flow.buttonCount()==(final?1:2));
 }
 puts("PASS retail2/3/4/5/8/10-win sets, draws/idempotent scores/round transitions, Battle Rules commit-cancel, standard vehicles,10-stage deck, generated-caption composition and team result flow");
}
