#include <mkw/menu_ui.hpp>
#include <algorithm>
#include <cmath>

namespace mkw {
namespace {
constexpr unsigned KartChoices[]={0,1,2,6,7,8};
constexpr unsigned BikeChoices[]={3,4,5,9,10,11};
// Each row pairs a kart and bike, in the original machine family order.
constexpr unsigned AllVehicleChoices[]={0,3,1,4,2,5,6,9,7,10,8,11};

bool kartsOnly(const UiSelection& s){return s.mode==UiMode::GrandPrix&&s.engineClass==UiEngineClass::CC50&&!s.mixedEngineTypes;}
bool bikesOnly(const UiSelection& s){return s.mode==UiMode::GrandPrix&&s.engineClass==UiEngineClass::CC100&&!s.mixedEngineTypes;}

unsigned moveMainMenu(unsigned cursor,const UiInput& key){
 const auto& origin=UiMainMenuControls[cursor];
 if(key.up||key.down){
  // There are two visual rows, with different child counts. Choose the
  // closest child in the other row using original control centers.
  unsigned best=cursor;float distance=1e9f;
  for(unsigned i=0;i<UiMainMenuControls.size();++i){
   const auto& target=UiMainMenuControls[i];
   if(target.y==origin.y)continue;
   const float delta=std::fabs(target.x-origin.x);
   if(delta<distance){distance=delta;best=i;}
  }
  return best;
 }
 const unsigned first=cursor<4?0:4,last=cursor<4?3:6;
 return key.left?(cursor==first?last:cursor-1):(cursor==last?first:cursor+1);
}

void normalize(UiSelection& s){
 if(s.driver>=UiCharacters.size())s.driver=0;
 if(s.vehicle>=12)s.vehicle=0;
 if(s.course>=32)s.course=0;
 if(s.battleStage>=10)s.battleStage=0;
 if(s.license>=4)s.license=0;
 if(unsigned(s.mode)>unsigned(UiMode::Battle))s.mode=UiMode::GrandPrix;
 if(unsigned(s.engineClass)>unsigned(UiEngineClass::Mirror))s.engineClass=UiEngineClass::CC150;
}

unsigned gridColumn(UiPage page,unsigned slot,unsigned columns){
 // The two Mii buttons occupy the right half of a seventh character row.
 return page==UiPage::Character&&slot>=24?slot-22:slot%columns;
}
unsigned moveGrid(UiPage page,unsigned cursor,unsigned count,unsigned columns,const UiInput& key){
 if(count<=1)return 0;
 const bool vertical=key.up||key.down;
 const int direction=(key.up||(!vertical&&key.left))?-1:1;
 const unsigned rows=(count+columns-1)/columns;
 const unsigned column=gridColumn(page,cursor,columns),row=cursor/columns;
 const unsigned limit=vertical?rows:columns;
 // Search along one axis only. This preserves spatial navigation and skips the
 // two empty slots preceding the Mii buttons instead of selecting a phantom.
 for(unsigned step=1;step<=limit;++step){
  const unsigned target=(int(vertical?row:column)+int(limit)+direction*int(step))%limit;
  for(unsigned candidate=0;candidate<count;++candidate){
   const unsigned cr=candidate/columns,cc=gridColumn(page,candidate,columns);
   if(vertical?(cc==column&&cr==target):(cr==row&&cc==target))return candidate;
  }
 }
 return cursor;
}
}

unsigned MenuState::count()const{
 switch(page){
 case UiPage::License:case UiPage::Mode:case UiPage::EngineClass:return 4;
 case UiPage::Results:return finalRace?1:2;
 case UiPage::MainMenu:return unsigned(UiMainMenuControls.size());
 case UiPage::Character:return unsigned(UiCharacterDrivers.size());
 case UiPage::Vehicle:return kartsOnly(selection)||bikesOnly(selection)?6:12;
 case UiPage::VSMode:case UiPage::BattleMode:case UiPage::BattleCup:case UiPage::Drift:case UiPage::QuitConfirm:return 2;
 case UiPage::Cup:return 8;
 case UiPage::Course:return 4;
 case UiPage::BattleStage:return 5;
 case UiPage::Ghost:return 3;
 case UiPage::Pause:return uiPauseButtonCount(selection.mode);
 default:return 1;
 }
}

unsigned MenuState::columns()const{
 switch(page){
 case UiPage::License:case UiPage::MainMenu:return 2;
 case UiPage::Character:case UiPage::Cup:return 4;
 case UiPage::Vehicle:return kartsOnly(selection)||bikesOnly(selection)?1:2;
 case UiPage::BattleCup:return 2;
 default:return 1;
 }
}

unsigned MenuState::characterDriver(unsigned slot)const{
 return UiCharacterDrivers[std::min(slot,unsigned(UiCharacterDrivers.size()-1))];
}

unsigned MenuState::vehicleChoice(unsigned slot)const{
 if(kartsOnly(selection))return KartChoices[std::min(slot,5u)];
 if(bikesOnly(selection))return BikeChoices[std::min(slot,5u)];
 return AllVehicleChoices[std::min(slot,11u)];
}

unsigned MenuState::vehicleSlot(unsigned choice)const{
 const unsigned limit=kartsOnly(selection)||bikesOnly(selection)?6:12;
 for(unsigned i=0;i<limit;++i)if(vehicleChoice(i)==choice)return i;
 return 0;
}

unsigned MenuState::engineClassMovie(unsigned slot)const{
 slot=std::min(slot,3u);
 return selection.mixedEngineTypes&&slot<2?slot+4:slot;
}

bool MenuState::supportsRace()const{
 // Browsing a retail menu does not silently change the chosen race rules or
 // substitute a different character/machine when that runtime is unavailable.
 const bool courseReady=selection.mode==UiMode::Battle?selection.battleStage<10&&!selection.coinRunners:selection.course<32;
 return raceMode(selection.mode)&&(selection.mode!=UiMode::GrandPrix||raceEngineClass(selection.engineClass))&&!selection.teams&&raceDriver(selection.driver)&&raceVehicle(selection.vehicle)&&courseReady;
}

UiInput UiDirectionRepeat::step(bool up,bool down,bool left,bool right,unsigned frames){
 // fn_1_DF4E4's mask: up 1 (else down 2), right 4 (else left 8). The count
 // restarts only from or to no direction; fn_1_DF6BC fires at 0 and 15+15k.
 const unsigned mask=(up?1u:down?2u:0u)|(right?4u:left?8u:0u);
 auto repeats=[](unsigned held){return held<15?0u:(held-15)/15+1;};
 const bool press=mask&&!mask_;const unsigned before=held_;
 held_=mask&&mask_?held_+frames:0;mask_=mask;
 UiInput key;
 if(mask&&(press||repeats(held_)>repeats(before))){key.up=mask&1;key.down=mask&2;key.right=mask&4;key.left=mask&8;}
 return key;
}

void MenuState::advance(unsigned ticks){age+=ticks;focusAge+=ticks;sectionAge+=ticks;uiFocusAdvance(*this,ticks);}

void MenuState::enter(UiPage next){
 normalize(selection);
 // Leaving the race (results, pause) is a section change: a new MenuBG.
 if(retainsRaceAssets())sectionAge=0;
 page=next;age=focusAge=0;unavailable=false;cursor=0;
 switch(page){
 case UiPage::License:cursor=selection.license;break;
 case UiPage::Mode:cursor=unsigned(selection.mode);break;
 case UiPage::EngineClass:cursor=unsigned(selection.engineClass);break;
 case UiPage::VSMode:cursor=selection.teams?1:0;break;
 case UiPage::BattleMode:cursor=selection.coinRunners?1:0;break;
 case UiPage::Character:
  for(unsigned i=0;i<UiCharacterDrivers.size();++i)if(characterDriver(i)==selection.driver){cursor=i;break;}
  break;
 case UiPage::Vehicle:cursor=vehicleSlot(selection.vehicle);break;
 case UiPage::Drift:cursor=selection.automatic?0:1;break;
 case UiPage::Cup:cursor=selection.course/4;break;
 case UiPage::Course:cursor=selection.course%4;break;
 case UiPage::BattleCup:cursor=selection.battleStage/5;break;
 case UiPage::BattleStage:cursor=selection.battleStage%5;break;
 case UiPage::Ghost:cursor=ghostTimes[selection.course%ghostTimes.size()]?0:2;break; // Race This Ghost, or Solo when none
 default:break;
 }
 uiFocusReset(*this);
}

UiAction MenuState::input(UiInput key){
 normalize(selection);
 cursor=std::min(cursor,count()-1);
 if(page==UiPage::Race){if(key.start)enter(UiPage::Pause);return UiAction::None;}
 // RaceMenuPage_onInit (PAL 0x80858D60) gives the pause pages only an input
 // handler 2 (START), fn_1_349CE4 0x8085A098: close and resume. No page of
 // the family has a Back handler, so B does nothing on Pause or QuitConfirm.
 if(page==UiPage::Pause&&key.start){enter(UiPage::Race);return UiAction::ResumeRace;}
 if((page==UiPage::Pause||page==UiPage::QuitConfirm)&&key.back)return UiAction::None;
 if(key.back){
  switch(page){
  case UiPage::Intro:case UiPage::License:enter(UiPage::Title);break;
  case UiPage::MainMenu:enter(UiPage::License);break;
  case UiPage::Mode:enter(UiPage::MainMenu);break;
  case UiPage::EngineClass:case UiPage::VSMode:case UiPage::BattleMode:enter(UiPage::Mode);break;
  case UiPage::Character:
   enter(selection.mode==UiMode::GrandPrix?UiPage::EngineClass:selection.mode==UiMode::VS?UiPage::VSMode:selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::Mode);break;
  case UiPage::Vehicle:enter(UiPage::Character);break;
  case UiPage::Drift:enter(UiPage::Vehicle);break;
  case UiPage::Cup:case UiPage::BattleCup:enter(UiPage::Drift);break;
  case UiPage::Course:enter(UiPage::Cup);break;
  case UiPage::BattleStage:enter(UiPage::BattleCup);break;
  case UiPage::Ghost:enter(UiPage::Course);break;
  // ConfirmPage's back (PAL 0x805C22B8, result 1): TimeAttackBackPage_onRefocus
  // (0x80639260) replaces itself with page 0x6F, RaceCourseSelectPage.
  case UiPage::NoGhosts:enter(UiPage::Course);break;
  case UiPage::Confirm:case UiPage::Error:
   enter(selection.mode==UiMode::GrandPrix?UiPage::Cup:selection.mode==UiMode::Battle?UiPage::BattleStage:selection.mode==UiMode::TimeTrials?ghostPage():UiPage::Course);break;
  default:break;
  }
  return UiAction::None;
 }
 if(key.switchType&&page==UiPage::EngineClass){
  // GPClassSelect::OnStartPress (USA 80825168) uses one toggle for both
  // unlocked classes. The current highlighted class does not limit it.
  selection.mixedEngineTypes=!selection.mixedEngineTypes;
  focusAge=0;unavailable=false;
  return UiAction::None;
 }
 if(key.up||key.down||key.left||key.right){
  const unsigned previousCursor=cursor;
  if(page==UiPage::MainMenu)cursor=moveMainMenu(cursor,key);
  else if(columns()==1){
   // Horizontal input also traverses a one-column control group, as it does
   // when navigating the same controls with the Wii controller sideways.
   const int step=key.up||(!key.down&&key.left)?-1:1;
   cursor=(int(cursor)+int(count())+step)%count();
  }else cursor=moveGrid(page,cursor,count(),columns(),key);
  if(cursor!=previousCursor)focusAge=0;
  uiFocusSelect(*this,previousCursor);unavailable=false;
 }
 if(!key.accept&&!(key.start&&(page==UiPage::Title||page==UiPage::Strap||page==UiPage::Intro)))return UiAction::None;
 switch(page){
 case UiPage::Strap:enter(UiPage::Intro);break;
 case UiPage::Intro:enter(UiPage::Title);break;
 case UiPage::Title:enter(UiPage::License);break;
 case UiPage::License:selection.license=cursor;enter(UiPage::MainMenu);break;
 case UiPage::MainMenu:if(cursor==0)enter(UiPage::Mode);else unavailable=true;break;
 case UiPage::Mode:
  if(!raceMode(UiMode(cursor))){unavailable=true;break;}
  selection.mode=UiMode(cursor);
  enter(selection.mode==UiMode::GrandPrix?UiPage::EngineClass:selection.mode==UiMode::VS?UiPage::VSMode:selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::Character);break;
 case UiPage::EngineClass:
  if(!raceEngineClass(UiEngineClass(cursor))){unavailable=true;break;}
  selection.engineClass=UiEngineClass(cursor);enter(UiPage::Character);break;
 case UiPage::VSMode:
  if(cursor==1){unavailable=true;break;} // Team VS has no race runtime.
  selection.teams=false;enter(UiPage::Character);break;
 case UiPage::BattleMode:
  if(cursor==1){unavailable=true;break;} // Coin Runners needs coin collection and round scoring.
  selection.coinRunners=false;enter(UiPage::Character);break;
 case UiPage::Character:
  // Includes the two Mii outfits, which also need Mii data.
  if(!raceDriver(characterDriver(cursor))){unavailable=true;break;}
  selection.driver=characterDriver(cursor);enter(UiPage::Vehicle);break;
 case UiPage::Vehicle:
  if(!raceVehicle(vehicleChoice(cursor))){unavailable=true;break;}
  selection.vehicle=vehicleChoice(cursor);enter(UiPage::Drift);break;
 case UiPage::Drift:selection.automatic=cursor==0;enter(selection.mode==UiMode::Battle?UiPage::BattleCup:UiPage::Cup);break;
 case UiPage::Cup:
  // A Grand Prix selects the whole cup. VS and Time Trials select a course.
  if(selection.course/4!=cursor||selection.mode==UiMode::GrandPrix)selection.course=cursor*4;
  enter(selection.mode==UiMode::GrandPrix?UiPage::Confirm:UiPage::Course);break;
 case UiPage::Course:selection.course=selection.course/4*4+cursor;enter(selection.mode==UiMode::TimeTrials?ghostPage():UiPage::Confirm);break;
 // ConfirmPage's OK (PAL 0x805C21C0, result 0): TimeAttackBackPage_onRefocus
 // (0x80639260) sets up a solo run and changes section at once; there is no
 // race confirmation page on this path.
 case UiPage::NoGhosts:
  selection.ghostChoice=2;
  if(supportsRace())return UiAction::StartRace;
  unavailable=true;break;
 case UiPage::Ghost:
  // Racing or watching a ghost needs one for the course.
  if(cursor==2||ghostTimes[selection.course%ghostTimes.size()]){selection.ghostChoice=cursor;enter(UiPage::Confirm);}
  else unavailable=true;
  break;
 case UiPage::BattleCup:
  if(selection.battleStage/5!=cursor)selection.battleStage=cursor*5;
  enter(UiPage::BattleStage);break;
 case UiPage::BattleStage:selection.battleStage=selection.battleStage/5*5+cursor;enter(UiPage::Confirm);break;
 case UiPage::Confirm:if(supportsRace())return UiAction::StartRace;unavailable=true;break;
 // RaceMenuPage_onButtonFront (PAL 0x8085A0F4), by button id.
 case UiPage::Pause:
  switch(uiPauseButton(selection.mode,cursor)){
  case UiPauseButton::Continue:enter(UiPage::Race);return UiAction::ResumeRace; // case 0
  case UiPauseButton::Restart:return UiAction::RestartRace;                     // case 2
  // Cases 5 and 6 change section to 0x4A / 0x49, the single-player menus
  // entered at page 0x6E (cup select) / 0x6B (character select).
  case UiPauseButton::ChangeCourse:enter(UiPage::Cup);break;
  case UiPauseButton::ChangeCharacter:enter(UiPage::Character);break;
  // Case 1: with the section's confirmation page (0x2C, only Grand Prix
  // section 0x1E among the single-player races) it pushes that page,
  // otherwise it quits at once (section 0x41).
  case UiPauseButton::Quit:enter(selection.mode==UiMode::GrandPrix?UiPage::QuitConfirm:UiPage::MainMenu);break;
  }
  break;
 // Page 0x2C: ButtonConfirmContinue (case 0x17) closes the pages and resumes
 // while paused; ButtonConfirmQuit (case 0x18) quits as case 1 does.
 case UiPage::QuitConfirm:
  if(cursor==0){enter(UiPage::Race);return UiAction::ResumeRace;}
  enter(UiPage::MainMenu);break;
 case UiPage::Results:
  // RaceMenu::OnButtonClick (USA 0x80838664): Next goes to the VS course
  // choice (section 0x4B); Quit (and, without the award scene, the final
  // race's Next) returns to the menus (section 0x41).
  // A Grand Prix goes straight on to the cup's next course.
  if(cursor==0&&!finalRace&&selection.mode==UiMode::GrandPrix){selection.course=selection.course/4*4+std::min(selection.course%4+1,3u);enter(UiPage::Confirm);return UiAction::StartRace;}
  if(cursor==0&&!finalRace)enter(selection.mode==UiMode::Battle?UiPage::BattleCup:UiPage::Cup);
  else enter(UiPage::MainMenu);
  break;
 case UiPage::Error:enter(selection.mode==UiMode::GrandPrix?UiPage::Cup:selection.mode==UiMode::Battle?UiPage::BattleStage:selection.mode==UiMode::TimeTrials?ghostPage():UiPage::Course);break;
 default:break;
 }
 return UiAction::None;
}
}
