#pragma once
#include <mkw/menu_ui.hpp>
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mkw {
// QA-only scripted controller for the menu/race frontend. Present only when
// menu-autoplay.txt exists on writable SD; package allowlists never install it.
// Tokens (whitespace separated):
//   A B U D L R S Y  press once when the menu is idle (no active transition)
//   wN               wait N presented frames
//   race:N           wait for a race, drive (A held) for N simulation frames
//   auto:N           as race:N, but player 0 follows the CPU route and uses items
//                    (race:N and auto:N also end when the player finishes)
//                    Diagnostic overlay: an auto:N race that runs out of frames,
//                    or whose best race completion hasn't risen for StallFrames,
//                    is stuck. The autopilot asks for a forced finish
//                    (takeForceFinish) so the results and the VS set carry on.
//   skip             wait for a race; press A on its first course-intro frame, as
//                    a player skipping the preview (no intro: nothing to press)
//   in a race:  A    press A once (skips a result board once it stops counting)
//               wN   wait N presented frames
//               next / quit  wait for the end-of-race menu, choose that option
//                            (after a set's final race, skip on to the last end)
//   end              finish successfully
// A file containing tokens replaces the default VS script.
class MenuAutopilot {
public:
 static constexpr unsigned StallFrames=3600;   // 60 s of simulation with no new best completion
 static constexpr float StallProgress=0.002f;  // in laps
 static constexpr const char* DefaultScript=
  "w30 A w20 A w20 A w20 A w20 A w20 D D A w20 A "      // strap,intro,title,license,main,mode(VS),solo
  "w20 R R L L D U A "                                   // characters: browse, then Mario
  "w20 D D U U A w20 A "                                 // vehicles: browse, Standard Kart M; manual drift
  "w20 R L A w20 D U A w20 A "                           // Mushroom Cup, Luigi Circuit, confirm
  "race:300 S w20 D D A w20 U A "                         // pause, quit, yes
  "w30 A w20 A w20 A w20 R A w20 R A w20 A "              // main, VS, solo, Luigi, Standard Bike M, manual
  "w20 R A w20 R A w20 A race:300 end";                   // Flower Cup, Coconut Mall, confirm
 bool open(const char* path){
  FILE* f=std::fopen(path,"r");if(!f)return false;
  std::string text;char buffer[256];size_t n;
  while((n=std::fread(buffer,1,sizeof buffer,f))>0)text.append(buffer,n);
  std::fclose(f);
  if(text.find_first_not_of(" \t\r\n01")==std::string::npos)text=DefaultScript;
  size_t at=0;
  while((at=text.find_first_not_of(" \t\r\n",at))!=std::string::npos){
   const size_t end=text.find_first_of(" \t\r\n",at);
   tokens_.push_back(text.substr(at,end==std::string::npos?std::string::npos:end-at));
   at=end;
  }
  active_=!tokens_.empty();return active_;
 }
 bool active()const{return active_;}
 // QA: player 0 is driven by the CPU route during an auto:N token.
 bool routeDrive()const{return active_&&!done_&&racing_&&route_;}
 bool finished()const{return done_;}
 bool passed()const{return done_&&!failed_;}
 unsigned racesStarted()const{return races_;}
 const char* failure()const{return failure_;}
 // Races (1-based autopilot count) that were forced to finish, comma separated.
 const char* stuckRaces()const{return stuck_.c_str();}
 // Why the last forced finish was asked for: "stalled" or "out of time".
 const char* stuckReason()const{return stuckReason_;}
 // The main loop finishes player 0 when this returns true (once per stuck race).
 bool takeForceFinish(){const bool r=forceRequest_;forceRequest_=false;return r;}
 // Menu context: one UiInput per presented frame. idle=no transition active.
 UiInput menu(const MenuState& state,bool idle){
  UiInput key{};if(!active_||done_)return key;
  ++menuFrames_;
  if(menuFrames_>36000){fail("menu script timed out");return key;}
  const std::string* token=current();if(!token)return key;
  if((*token)[0]=='w'){if(++wait_>=unsigned(std::atoi(token->c_str()+1))){wait_=0;++cursor_;}return key;}
  if(!token->compare(0,5,"race:")||!token->compare(0,5,"auto:")||!token->compare(0,4,"cpu:")||*token=="skip"){if(++raceWait_>1800)fail("race did not start");return key;}
  if(*token=="end"){done_=true;return key;}
  // T:x,y taps the lower screen at pixel x,y: four frames down, then lifted.
  if(!token->compare(0,2,"T:")&&touchFrames_){if(++touchFrames_>5){touchFrames_=0;++cursor_;}return key;}
  if(!idle||state.page==UiPage::Race)return key;
  if(++gap_<8)return key;gap_=0;
  if(!token->compare(0,2,"T:")){
   if(std::sscanf(token->c_str()+2,"%f,%f",&touchX_,&touchY_)!=2){fail("bad touch token");return key;}
   touchFrames_=1;return key;
  }
  switch((*token)[0]){
  case 'A':key.accept=true;break;case 'B':key.back=true;break;case 'U':key.up=true;break;
  case 'D':key.down=true;break;case 'L':key.left=true;break;case 'R':key.right=true;break;
  case 'S':key.start=true;break;case 'Y':key.switchType=true;break;
  default:fail("unknown token");return key;
  }
  ++cursor_;return key;
 }
 // The stylus this frame (a T:x,y token); main reads it after menu().
 bool touch(float& x,float& y)const{if(touchFrames_<1||touchFrames_>4)return false;x=touchX_;y=touchY_;return true;}
 // Race context, course intro: true once for a "skip" token (press A to skip it).
 bool introSkip(){
  if(!active_||done_)return false;
  const std::string* token=current();if(!token||*token!="skip")return false;
  raceWait_=0;++cursor_;return true;
 }
 // Race context: returns held/down key masks for one presented frame.
 // finished: the player has crossed the line; endMenu: the end-of-race menu
 // accepts input, with endCursor its selection. progress: player 0's race
 // completion in laps; finalRace: the set's last race.
 void race(u32& held,u32& down,unsigned simulationFrames,bool finished=false,bool endMenu=false,unsigned endCursor=0,
           float progress=0.f,bool finalRace=false){
  if(!active_||done_)return;
  held=KEY_A;down=0;
  const std::string* token=current();if(!token)return;
  // Once an end-of-race option is chosen the race is being left: the
  // following tokens belong to the menus, until the next race begins.
  // Diagnostic overlay: "next" starts the following race while the old one's
  // results still fade out (finished stays true); its race:/auto: token must
  // not begin, and end at once, before that race is gone.
  // cpu:N (test only): as auto:N, but when N runs out the race carries on
  // with no forced finish, so a mid-race pause (S) can follow.
  const bool timed=!token->compare(0,4,"cpu:");
  if(leaving_){if(finished||(token->compare(0,5,"race:")&&token->compare(0,5,"auto:")&&!timed)){held=0;return;}leaving_=false;}
  if(!token->compare(0,5,"race:")||!token->compare(0,5,"auto:")||timed){
   if(!racing_){racing_=true;raceStart_=simulationFrames;++races_;raceWait_=0;best_=progress;bestAt_=simulationFrames;forced_=false;}
   route_=token->compare(0,5,"race:")!=0;
   const bool expired=simulationFrames-raceStart_>=unsigned(std::atoi(token->c_str()+(timed?4:5)));
   if(finished||(expired&&(!route_||timed))){racing_=false;route_=false;++cursor_;return;}
   if(timed)return;
   // "race:N:items" also presses L (use item) at three fixed frames, for the
   // Time Trials ghost sync check (ghost-test.txt).
   if(!route_&&token->find(":items")!=std::string::npos){
    const unsigned t=simulationFrames-raceStart_;
    for(unsigned at:{360u,660u,960u})if(t>=at&&t<at+6)held|=KEY_L;
   }
   if(!route_)return;
   if(progress>best_+StallProgress){best_=progress;bestAt_=simulationFrames;}
   const bool stalled=simulationFrames-bestAt_>=StallFrames;
   if(!forced_&&(expired||stalled)){
    forced_=true;forceRequest_=true;forcedAt_=simulationFrames;stuckReason_=stalled?"stalled":"out of time";
    if(!stuck_.empty())stuck_+=',';stuck_+=std::to_string(races_);
   }
   if(forced_&&simulationFrames-forcedAt_>1800)fail("forced finish did not end the race");
   return;
  }
  if(*token=="end"){done_=true;return;}
  if(*token=="skip"){++cursor_;return;}
  if(*token=="S"){down=KEY_START;++cursor_;return;}
  if(*token=="A"){if(++gap_>=8){gap_=0;down=KEY_A;++cursor_;}return;}
  // The pause menu runs inside the race loop: its choices arrive here.
  if(*token=="U"||*token=="D"||*token=="L"||*token=="R"||*token=="B"){
   if(++gap_>=8){gap_=0;down=*token=="U"?KEY_DUP:*token=="D"?KEY_DDOWN:*token=="L"?KEY_DLEFT:*token=="R"?KEY_DRIGHT:KEY_B;++cursor_;}
   return;
  }
  if((*token)[0]=='w'){if(++wait_>=unsigned(std::atoi(token->c_str()+1))){wait_=0;++cursor_;}return;}
  if(*token=="next"||*token=="quit"){
   // The result boards before it take A, once they stop counting.
   if(!endMenu){held=0;if(++raceWait_>12000)fail("end-of-race menu did not appear");else if(raceWait_%120==0)down=KEY_A;return;}
   raceWait_=0;if(++gap_<8)return;gap_=0;
   const unsigned wanted=*token=="quit"?1:0;
   if(endCursor!=wanted){down=KEY_DDOWN;return;}
   down=KEY_A;++cursor_;leaving_=true;
   // A resumed benchmark's set is shorter than the script: after its final
   // race only the closing end token is left to run.
   if(finalRace)for(size_t i=tokens_.size();i-->cursor_;)if(tokens_[i]=="end"){cursor_=i;break;}
   return;
  }
 }
private:
 const std::string* current(){if(cursor_>=tokens_.size()){done_=true;return nullptr;}return &tokens_[cursor_];}
 void fail(const char* reason){failed_=true;done_=true;failure_=reason;}
 std::vector<std::string> tokens_;
 size_t cursor_=0;unsigned wait_=0,gap_=0,menuFrames_=0,raceWait_=0,raceStart_=0,races_=0,bestAt_=0,forcedAt_=0;
 float best_=0.f,touchX_=0,touchY_=0;unsigned touchFrames_=0;
 bool active_=false,done_=false,failed_=false,racing_=false,route_=false,leaving_=false,forced_=false,forceRequest_=false;
 const char* failure_="";const char* stuckReason_="";
 std::string stuck_;
};
}
