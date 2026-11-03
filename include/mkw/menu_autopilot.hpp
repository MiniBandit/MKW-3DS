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
//   in a race:  A    press A once (skips a result board once it stops counting)
//               wN   wait N presented frames
//               next / quit  wait for the end-of-race menu, choose that option
//   end              finish successfully
// A file containing tokens replaces the default VS script.
class MenuAutopilot {
public:
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
 // Menu context: one UiInput per presented frame. idle=no transition active.
 UiInput menu(const MenuState& state,bool idle){
  UiInput key{};if(!active_||done_)return key;
  ++menuFrames_;
  if(menuFrames_>36000){fail("menu script timed out");return key;}
  const std::string* token=current();if(!token)return key;
  if((*token)[0]=='w'){if(++wait_>=unsigned(std::atoi(token->c_str()+1))){wait_=0;++cursor_;}return key;}
  if(!token->compare(0,5,"race:")||!token->compare(0,5,"auto:")){if(++raceWait_>1800)fail("race did not start");return key;}
  if(*token=="end"){done_=true;return key;}
  if(!idle||state.page==UiPage::Race)return key;
  if(++gap_<8)return key;gap_=0;
  switch((*token)[0]){
  case 'A':key.accept=true;break;case 'B':key.back=true;break;case 'U':key.up=true;break;
  case 'D':key.down=true;break;case 'L':key.left=true;break;case 'R':key.right=true;break;
  case 'S':key.start=true;break;case 'Y':key.switchType=true;break;
  default:fail("unknown token");return key;
  }
  ++cursor_;return key;
 }
 // Race context: returns held/down key masks for one presented frame.
 // finished: the player has crossed the line; endMenu: the end-of-race menu
 // accepts input, with endCursor its selection.
 void race(u32& held,u32& down,unsigned simulationFrames,bool finished=false,bool endMenu=false,unsigned endCursor=0){
  if(!active_||done_)return;
  held=KEY_A;down=0;
  const std::string* token=current();if(!token)return;
  // Once an end-of-race option is chosen the race is being left: the
  // following tokens belong to the menus, until the next race begins.
  if(leaving_){if(token->compare(0,5,"race:")&&token->compare(0,5,"auto:")){held=0;return;}leaving_=false;}
  if(!token->compare(0,5,"race:")||!token->compare(0,5,"auto:")){
   if(!racing_){racing_=true;raceStart_=simulationFrames;++races_;raceWait_=0;}
   route_=!token->compare(0,5,"auto:");
   if(finished||simulationFrames-raceStart_>=unsigned(std::atoi(token->c_str()+5))){racing_=false;route_=false;++cursor_;}
   return;
  }
  if(*token=="end"){done_=true;return;}
  if(*token=="S"){down=KEY_START;++cursor_;return;}
  if(*token=="A"){if(++gap_>=8){gap_=0;down=KEY_A;++cursor_;}return;}
  if((*token)[0]=='w'){if(++wait_>=unsigned(std::atoi(token->c_str()+1))){wait_=0;++cursor_;}return;}
  if(*token=="next"||*token=="quit"){
   if(!endMenu){if(++raceWait_>12000)fail("end-of-race menu did not appear");return;}
   raceWait_=0;if(++gap_<8)return;gap_=0;
   const unsigned wanted=*token=="quit"?1:0;
   if(endCursor!=wanted){down=KEY_DDOWN;return;}
   down=KEY_A;++cursor_;leaving_=true;return;
  }
 }
private:
 const std::string* current(){if(cursor_>=tokens_.size()){done_=true;return nullptr;}return &tokens_[cursor_];}
 void fail(const char* reason){failed_=true;done_=true;failure_=reason;}
 std::vector<std::string> tokens_;
 size_t cursor_=0;unsigned wait_=0,gap_=0,menuFrames_=0,raceWait_=0,raceStart_=0,races_=0;
 bool active_=false,done_=false,failed_=false,racing_=false,route_=false,leaving_=false;
 const char* failure_="";
};
}
