#include <mkw/menu_ui.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mkw/battle_session.hpp>
using namespace mkw;
int main(){
 UiInput a;a.accept=true;UiInput b;b.back=true;
 for(auto mode:{UiMode::GrandPrix,UiMode::VS,UiMode::Battle,UiMode::TimeTrials}){
  MenuState s;s.selection.mode=mode;s.enter(UiPage::Pause);
  assert(s.count()==(mode==UiMode::TimeTrials?5:3));
  assert(s.input(a)==UiAction::ResumeRace&&s.page==UiPage::Race);
  s.enter(UiPage::Pause);s.cursor=s.pauseQuitSlot();
  assert(s.input(a)==UiAction::None&&s.page==UiPage::QuitConfirm);
  s.input(b);assert(s.page==UiPage::Pause&&s.cursor==s.pauseQuitSlot());
  s.input(a);s.cursor=1;s.input(a);assert(s.page==UiPage::Pause&&s.cursor==s.pauseQuitSlot());
  s.cursor=1;assert(!std::strcmp(s.pauseVariant(1),"ButtonRestart"));assert(s.input(a)==UiAction::RestartRace);
  if(mode==UiMode::TimeTrials){
   s.cursor=1;assert(s.input(a)==UiAction::RestartRace);
   s.cursor=2;s.input(a);assert(s.page==UiPage::Cup&&!s.retainsRaceAssets());
   s.enter(UiPage::Pause);s.cursor=3;s.input(a);assert(s.page==UiPage::Character&&!s.retainsRaceAssets());
  }
 }
 // Restart a finished round twice: undo exactly that round, keep the first.
 BattleSession set;set.reset(2);std::array<uint16_t,12> scores{};
 std::array<RaceTeam,12> teams{};teams[1]=RaceTeam::Blue;
 scores[0]=3;set.seal(scores,teams,2);assert(set.nextRound());
 scores={};scores[1]=7;set.seal(scores,teams,2);
 set.restartRound();set.restartRound();
 assert(set.round()==2&&set.wins(RaceTeam::Red)==1&&set.wins(RaceTeam::Blue)==0);
 assert(set.totalScore(0)==3&&set.totalScore(1)==0);
 scores[1]=4;assert(set.seal(scores,teams,2));
 assert(set.totalScore(0)==3&&set.totalScore(1)==4&&set.wins(RaceTeam::Blue)==1);
 assert(set.nextRound());set.restartRound();assert(set.round()==3&&set.totalScore(1)==4);
 puts("PASS all4 modes Restart/Continue/Quit+cancel, TT extra actions, sealed/unsealed Battle rollback preserves prior rounds");
}
