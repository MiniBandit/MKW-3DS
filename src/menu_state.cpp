#include <mkw/menu_ui.hpp>
#include <mkw/roster.hpp>
#include <algorithm>
#include <cmath>

namespace mkw {
namespace {
constexpr unsigned KartChoices[]={0,1,2,6,7,8};
constexpr unsigned BikeChoices[]={3,4,5,9,10,11};
// Each row pairs a kart and bike, in the original machine family order.
constexpr unsigned AllVehicleChoices[]={0,3,1,4,2,5,6,9,7,10,8,11};

bool kartsOnly(const MenuState& m){const auto& s=m.selection;return s.mode==UiMode::GrandPrix&&s.engineClass==UiEngineClass::CC50&&(!s.mixedEngineTypes||!m.mixedTypesUnlocked(0));}
bool bikesOnly(const MenuState& m){const auto& s=m.selection;return s.mode==UiMode::GrandPrix&&s.engineClass==UiEngineClass::CC100&&(!s.mixedEngineTypes||!m.mixedTypesUnlocked(1));}

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
 if(s.ledMode>3)s.ledMode=0;
 s.ledBrightness=std::clamp(s.ledBrightness/10,5u,10u)*10;
 s.battleCpu=std::min(s.battleCpu,2u);s.battleVehicles=std::min(s.battleVehicles,2u);
 s.battleCourses=std::min(s.battleCourses,2u);s.battleItems=std::min(s.battleItems,2u);
 if(std::find(std::begin(UiBattleWinCounts),std::end(UiBattleWinCounts),s.battleWins)==std::end(UiBattleWinCounts))s.battleWins=2;
 if(unsigned(s.mode)>unsigned(UiMode::Battle))s.mode=UiMode::GrandPrix;
 if(unsigned(s.engineClass)>unsigned(UiEngineClass::Mirror))s.engineClass=UiEngineClass::CC150;
}

unsigned gridColumn(UiPage page,unsigned slot,unsigned count,unsigned columns){
 // Match the reflowed character row, including profiles with locked choices.
 const unsigned rowStart=(slot/columns)*columns;
 return slot%columns+(page==UiPage::Character?columns-std::min(columns,count-rowStart):0);
}
unsigned moveGrid(UiPage page,unsigned cursor,unsigned count,unsigned columns,const UiInput& key){
 if(count<=1)return 0;
 const bool vertical=key.up||key.down;
 const int direction=(key.up||(!vertical&&key.left))?-1:1;
 const unsigned rows=(count+columns-1)/columns;
 const unsigned column=gridColumn(page,cursor,count,columns),row=cursor/columns;
 const unsigned limit=vertical?rows:columns;
 // Search along one axis only. This preserves spatial navigation and skips the
 // two empty slots preceding the Mii buttons instead of selecting a phantom.
 for(unsigned step=1;step<=limit;++step){
  const unsigned target=(int(vertical?row:column)+int(limit)+direction*int(step))%limit;
  for(unsigned candidate=0;candidate<count;++candidate){
   const unsigned cr=candidate/columns,cc=gridColumn(page,candidate,count,columns);
   if(vertical?(cc==column&&cr==target):(cr==row&&cc==target))return candidate;
  }
 }
 return cursor;
}
}

void MenuState::prepareBattleStages(){
 battleStageIndex=0;for(unsigned i=0;i<10;++i)battleStageDeck[i]=i;
 if(selection.battleCourses==1)for(unsigned n=10;n>1;--n)std::swap(battleStageDeck[n-1],battleStageDeck[cpuRandom.next(n)]);
 selection.battleStage=battleStageDeck[0];
}
void MenuState::nextBattleStage(){selection.battleStage=battleStageDeck[++battleStageIndex%10];}
void MenuState::prepareVsCourses(){
 vsCourseIndex=0;
 for(unsigned i=0;i<32;++i)vsCourseDeck[i]=i;
 if(selection.vsCourses==1){
  vsRandom^=age+selection.driver*0x9e3779b9u;
  for(unsigned n=32;n>1;--n){vsRandom=vsRandom*1664525u+1013904223u;std::swap(vsCourseDeck[n-1],vsCourseDeck[vsRandom%n]);}
 }
 selection.course=vsCourseDeck[0];
}
void MenuState::nextVsCourse(){selection.course=vsCourseDeck[++vsCourseIndex%32];}
unsigned MenuState::count()const{
 switch(page){
 case UiPage::License:return 4;
 case UiPage::Drift:case UiPage::VSMode:case UiPage::Settings:return 3;
 case UiPage::LicenseSettings:return 5;
 case UiPage::VSRules:return 7;
 case UiPage::BattleRules:return 6;
 case UiPage::Mode:return 4;
 case UiPage::EngineClass:return engineUnlocked(3)?4:3;
 case UiPage::Results:return finalRace?1:2;
 case UiPage::MainMenu:return unsigned(UiMainMenuControls.size())+1;
 case UiPage::Character:return unsigned(UiCharacterDrivers.size());
 case UiPage::Vehicle:return selection.mode==UiMode::Battle?2:kartsOnly(*this)||bikesOnly(*this)?6:12;
 case UiPage::TeamConfirm:return 1;
 case UiPage::BattleMode:return 3;
 case UiPage::BattleCup:case UiPage::QuitConfirm:case UiPage::EraseLicense:return 2;
 case UiPage::Cup:return 8;
 case UiPage::Course:return 4;
 case UiPage::BattleStage:return 5;
 case UiPage::Ghost:return 1;
 case UiPage::Pause:return pauseCount();
 default:return 1;
 }
}

bool MenuState::choiceVisible(unsigned slot)const{
 if(slot>=count())return false;
 switch(page){
 case UiPage::Character:return driverUnlocked(characterDriver(slot))&&raceDriver(characterDriver(slot));
 case UiPage::Vehicle:return vehicleUnlocked(vehicleChoice(slot))&&raceVehicle(vehicleChoice(slot));
 case UiPage::Cup:return cupUnlocked(slot);
 case UiPage::EngineClass:return engineUnlocked(slot)&&raceEngineClass(UiEngineClass(slot));
 default:return true;
 }
}
unsigned MenuState::visibleChoiceIndex(unsigned slot)const{
 unsigned n=0;for(unsigned i=0;i<slot&&i<count();++i)n+=choiceVisible(i);return n;
}
unsigned MenuState::visibleChoiceCount()const{
 unsigned n=0;for(unsigned i=0;i<count();++i)n+=choiceVisible(i);return n;
}

unsigned MenuState::columns()const{
 switch(page){
 case UiPage::License:case UiPage::MainMenu:return 2;
 case UiPage::Character:case UiPage::Cup:return 4;
 case UiPage::Vehicle:return kartsOnly(*this)||bikesOnly(*this)?1:2;
 case UiPage::BattleCup:return 2;
 default:return 1;
 }
}

unsigned MenuState::characterDriver(unsigned slot)const{
 return UiCharacterDrivers[std::min(slot,unsigned(UiCharacterDrivers.size()-1))];
}

unsigned MenuState::vehicleChoice(unsigned slot)const{
 if(selection.mode==UiMode::Battle)return slot?3:0;
 if(kartsOnly(*this))return KartChoices[std::min(slot,5u)];
 if(bikesOnly(*this))return BikeChoices[std::min(slot,5u)];
 return AllVehicleChoices[std::min(slot,11u)];
}

unsigned MenuState::vehicleSlot(unsigned choice)const{
 const unsigned limit=selection.mode==UiMode::Battle?2:kartsOnly(*this)||bikesOnly(*this)?6:12;
 for(unsigned i=0;i<limit;++i)if(vehicleChoice(i)==choice)return i;
 return 0;
}

unsigned MenuState::engineClassMovie(unsigned slot)const{
 slot=std::min(slot,3u);
 return selection.mixedEngineTypes&&slot<2&&mixedTypesUnlocked(slot)?slot+4:slot;
}

bool MenuState::driverUnlocked(unsigned driver)const{
 const unsigned id=driver<StarterDrivers.size()?StarterDrivers[driver].character:driver;
 return driver<26&&licenseCharacterUnlocked(unlocks(),id);
}
bool MenuState::vehicleUnlocked(unsigned choice)const{
 constexpr unsigned base[]={0,3,6,18,21,24,9,12,15,27,30,33};
 return choice<12&&selection.driver<26&&licenseVehicleUnlocked(unlocks(),base[choice]+driverWeight());
}
bool MenuState::cupUnlocked(unsigned cup)const{
 if(selection.mode==UiMode::GrandPrix)return licenseCupUnlocked(unlocks(),cup,unsigned(selection.engineClass));
 // SectionParams::IsCupUnlockedInOneCC, USA805cd9d4.
 for(unsigned cc=0;cc<4;++cc)if(licenseCupUnlocked(unlocks(),cup,cc))return true;
 return false;
}
bool MenuState::supportsRace()const{
 // Browsing a retail menu does not silently change the chosen race rules or
 // substitute a different character/machine when that runtime is unavailable.
 const bool arena=selection.mode==UiMode::Battle;
 return (!arena||(selection.battleStage<10&&(selection.vehicle==0||selection.vehicle==3)))&&(!kartsOnly(*this)||(selection.vehicle<3||(selection.vehicle>=6&&selection.vehicle<9)))&&
  (!bikesOnly(*this)||(selection.vehicle>=3&&selection.vehicle<6)||selection.vehicle>=9)&&
  driverUnlocked(selection.driver)&&vehicleUnlocked(selection.vehicle)&&(selection.driver<24||selection.miiSelected)&&
  (selection.mode!=UiMode::GrandPrix||engineUnlocked(unsigned(selection.engineClass)))&&
  (arena||(selection.mode==UiMode::VS&&selection.vsCourses)||cupUnlocked(selection.course/4))&&raceMode(selection.mode)&&(selection.mode!=UiMode::GrandPrix||raceEngineClass(selection.engineClass))&&(!selection.teams||selection.mode==UiMode::VS||arena)&&raceDriver(selection.driver)&&raceVehicle(selection.vehicle)&&(arena||selection.course<32);
}

void MenuState::confirmRaceMii(unsigned weight,unsigned character){
 if(selectRaceMiiRequest<24||selectRaceMiiRequest>25||weight>2)return;
 selection.driver=unsigned(selectRaceMiiRequest);selectRaceMiiRequest=-1;
 selection.miiWeight=weight;selection.miiCharacter=character;selection.miiSelected=true;
 cpuRoster.characters(selection.driver,unlocks(),cpuRandom,weight);
 if(selection.teams){vsRandom=vsRandom*1664525u+1013904223u;selection.humanTeam=RaceTeam((vsRandom>>16)&1);enter(UiPage::TeamConfirm);}
 else enter(UiPage::Vehicle);
}

void MenuState::advance(unsigned ticks){age+=ticks;focusAge+=ticks;uiFocusAdvance(*this,ticks);}

void MenuState::enter(UiPage next){
 if((next==UiPage::VSRules||next==UiPage::BattleRules)&&!rulePage())rulesEntry=selection;
 normalize(selection);
 page=next;age=focusAge=0;unavailable=false;cursor=0;
 switch(page){
 case UiPage::Title:{
  // Retail 806098c0..9990: any occupied license's completion bit56
  // enables five alternate pictures; reject the immediately preceding one.
  bool alternate=false;
  for(unsigned i=0;i<4;++i)alternate|=licenseOccupied[i]&&licenseCompleted(licenseUnlocks[i],56);
  titleArtwork=0;
  if(alternate){
   do{titleRandom^=titleRandom<<13;titleRandom^=titleRandom>>17;titleRandom^=titleRandom<<5;
      titleArtwork=1+titleRandom%5;}while(titleArtwork==lastTitleArtwork);
   lastTitleArtwork=titleArtwork;
  }
  break;
 }

 case UiPage::License:cursor=selection.license;break;
 case UiPage::Mode:cursor=unsigned(selection.mode);break;
 case UiPage::EngineClass:cursor=engineUnlocked(unsigned(selection.engineClass))?unsigned(selection.engineClass):2;break;
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
 case UiPage::Ghost:cursor=0;break; // Ghost racing/replays are deferred; solo remains usable.
 case UiPage::EraseLicense:case UiPage::QuitConfirm:cursor=1;break;
 default:break;
 }
 if(!choiceVisible(cursor))for(unsigned i=0;i<count();++i)if(choiceVisible(i)){cursor=i;break;}
 uiFocusReset(*this);
}

UiAction MenuState::input(UiInput key){
 normalize(selection);
 if(page==UiPage::Unlock){if(key.accept)unlockNextRequested=true;return UiAction::None;}
 cursor=std::min(cursor,count()-1);
 if(page==UiPage::Race){if(key.start)enter(UiPage::Pause);return UiAction::None;}
 if(page==UiPage::Pause&&(key.start||key.back)){enter(UiPage::Race);return UiAction::ResumeRace;}
 if(key.back){
  switch(page){
  case UiPage::Intro:enter(UiPage::Title);break;
  case UiPage::License:titleLicense=cursor;enter(UiPage::Title);break;
  case UiPage::MainMenu:enter(UiPage::License);break;
  case UiPage::Settings:enter(UiPage::License);break;
  case UiPage::RecordsGeneral:case UiPage::RecordsFavorites:case UiPage::RecordsOther:enter(UiPage::LicenseSettings);break;
  case UiPage::EraseLicense:enter(UiPage::LicenseSettings);cursor=1;uiFocusReset(*this);break;
  case UiPage::LicenseSettings:enter(UiPage::MainMenu);cursor=7;uiFocusReset(*this);break;
  case UiPage::Mode:enter(UiPage::MainMenu);break;
  case UiPage::EngineClass:case UiPage::VSMode:case UiPage::BattleMode:enter(UiPage::Mode);break;
  case UiPage::Character:
   enter(selection.mode==UiMode::GrandPrix?UiPage::EngineClass:selection.mode==UiMode::VS?UiPage::VSMode:selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::Mode);break;
  case UiPage::TeamConfirm:enter(UiPage::Character);break;
  case UiPage::Vehicle:enter(selection.teams?UiPage::TeamConfirm:UiPage::Character);break;
  case UiPage::BattleRules:case UiPage::VSRules:enter(rulesReturn);cursor=auxiliarySlot();uiFocusReset(*this);break;
  case UiPage::DriftHelp:enter(UiPage::Drift);cursor=2;uiFocusReset(*this);break;
  case UiPage::Drift:enter(UiPage::Vehicle);break;
  case UiPage::Cup:case UiPage::BattleCup:enter(UiPage::Drift);break;
  case UiPage::Course:enter(UiPage::Cup);break;
  case UiPage::BattleStage:enter(UiPage::BattleCup);break;
  case UiPage::Ghost:enter(UiPage::Course);break;
  case UiPage::Confirm:case UiPage::Error:
   if((selection.mode==UiMode::VS&&selection.vsCourses)||(selection.mode==UiMode::Battle&&selection.battleCourses)){enter(UiPage::Drift);break;}
   enter(selection.mode==UiMode::GrandPrix?UiPage::Cup:selection.mode==UiMode::Battle?UiPage::BattleStage:selection.mode==UiMode::TimeTrials?UiPage::Ghost:UiPage::Course);break;
  case UiPage::QuitConfirm:enter(UiPage::Pause);cursor=pauseQuitSlot();break;
  default:break;
  }
  return UiAction::None;
 }
 if(key.switchType&&page==UiPage::EngineClass){
  if(!mixedTypesUnlocked(0)&&!mixedTypesUnlocked(1))return UiAction::None;
  // GPClassSelect::OnStartPress (USA 80825168) uses one toggle for both
  // unlocked classes. The current highlighted class does not limit it.
  selection.mixedEngineTypes=!selection.mixedEngineTypes;
  focusAge=0;unavailable=false;
  return UiAction::None;
 }
 if(page==UiPage::LicenseSettings&&cursor>=2&&cursor<5&&(key.left||key.right)){
  if(cursor==2)selection.ledMode=(selection.ledMode+(key.left?3:1))%4;
  else if(cursor==3)selection.dynamicShadows=!selection.dynamicShadows;
  else selection.ledBrightness=unsigned(std::clamp(int(selection.ledBrightness)+(key.left?-10:10),50,100));
  focusAge=0;return UiAction::None;
 }
 if(rulePage()&&(key.left||key.right)&&cursor<ruleOkRow()){
  if(page==UiPage::BattleRules){
   if(cursor==4){unsigned i=0;while(i+1<std::size(UiBattleWinCounts)&&UiBattleWinCounts[i]!=selection.battleWins)++i;
    selection.battleWins=UiBattleWinCounts[(i+std::size(UiBattleWinCounts)+(key.left?-1:1))%std::size(UiBattleWinCounts)];
   }else{unsigned* values[]={&selection.battleCpu,&selection.battleVehicles,&selection.battleCourses,&selection.battleItems};
    auto& value=*values[cursor];value=(value+3+(key.left?-1:1))%3;}
   focusAge=0;return UiAction::None;
  }
  if(cursor==5){
   unsigned i=0;while(i+1<std::size(UiVsRaceCounts)&&UiVsRaceCounts[i]!=selection.vsRaces)++i;
   selection.vsRaces=UiVsRaceCounts[(i+std::size(UiVsRaceCounts)+(key.left?-1:1))%std::size(UiVsRaceCounts)];focusAge=0;return UiAction::None;
  }
  unsigned* values[]={&selection.vsClass,&selection.vsCpu,&selection.vsVehicles,&selection.vsCourses,&selection.vsItems,&selection.vsRaces};
  const unsigned counts[]={4,3,3,3,4,32};
  const unsigned base=cursor==5?1:0;auto& value=*values[cursor];
  value=base+(value-base+counts[cursor]+(key.left?-1:1))%counts[cursor];
  focusAge=0;return UiAction::None;
 }
 if(key.up||key.down||key.left||key.right){
  const unsigned previousCursor=cursor;
  const unsigned auxiliary=auxiliarySlot();
  const bool hasAuxiliary=auxiliary!=~0u;
  const bool topRow=page==UiPage::License?cursor<2:page==UiPage::MainMenu?cursor<4:cursor==0;
  const bool rightEdge=page==UiPage::License?cursor%2==1:page==UiPage::MainMenu?(cursor==3||cursor==6):true;
  if(page==UiPage::Character||page==UiPage::Vehicle||page==UiPage::Cup||page==UiPage::EngineClass){
   std::array<unsigned,26> visible{};unsigned n=0,current=0;
   for(unsigned i=0;i<count()&&n<visible.size();++i)if(choiceVisible(i)){if(i==cursor)current=n;visible[n++]=i;}
   if(n){
    unsigned next=0;
    if(columns()==1){const int step=key.up||(!key.down&&key.left)?-1:1;next=(int(current)+int(n)+step)%n;}
    else next=moveGrid(page,current,n,columns(),key);
    cursor=visible[next];
   }
  }else if(hasAuxiliary&&cursor==auxiliary){
   cursor=std::min(auxiliaryReturnCursor,auxiliary-1);
  }else if(hasAuxiliary&&((key.up&&topRow)||(key.right&&rightEdge))){
   auxiliaryReturnCursor=cursor;cursor=auxiliary;
  }else if(page==UiPage::MainMenu)cursor=moveMainMenu(cursor,key);
  else if(columns()==1){
   // Horizontal input also traverses a one-column control group, as it does
   // when navigating the same controls with the Wii controller sideways.
   const int step=key.up||(!key.down&&key.left)?-1:1;
   const unsigned controls=hasAuxiliary?auxiliary:count();
   cursor=(int(cursor)+int(controls)+step)%controls;
  }else cursor=moveGrid(page,cursor,hasAuxiliary?auxiliary:count(),columns(),key);
  if(cursor!=previousCursor)focusAge=0;
  uiFocusSelect(*this,previousCursor);unavailable=false;
 }
 if(!key.accept&&!(key.start&&(page==UiPage::Title||page==UiPage::Strap||page==UiPage::Intro)))return UiAction::None;
 // Corner destinations are handled separately from the grid: never turn
 // a settings/help activation into a license choice or manual-drift selection.
 if(cursor==auxiliarySlot()){
  if(page==UiPage::MainMenu)enter(UiPage::LicenseSettings);
  else if(page==UiPage::Drift)enter(UiPage::DriftHelp);
  else if(page==UiPage::VSMode||page==UiPage::BattleMode){rulesReturn=page;enter(page==UiPage::BattleMode?UiPage::BattleRules:UiPage::VSRules);}
  else unavailable=true;
  return UiAction::None;
 }
 switch(page){
 case UiPage::Strap:enter(UiPage::Intro);break;
 case UiPage::Intro:enter(UiPage::Title);break;
 case UiPage::Title:enter(UiPage::License);if(titleLicense<4)cursor=titleLicense;uiFocusReset(*this);break;
 case UiPage::License:selection.license=cursor;enter(UiPage::MainMenu);break;
 // Wii-only destinations remain explicit until their platform handling is
 // provided: GC rumble, WFC settings and Wii Channel installation.
 case UiPage::Settings:unavailable=true;break;
 case UiPage::RecordsGeneral:enter(UiPage::RecordsFavorites);break;
 case UiPage::RecordsFavorites:enter(UiPage::RecordsOther);break;
 case UiPage::RecordsOther:enter(UiPage::RecordsGeneral);break;
 case UiPage::LicenseSettings:
  if(cursor==0)editLicenseRequest=int(selection.license);
  else if(cursor==1)enter(UiPage::EraseLicense);
  else if(cursor==2)selection.ledMode=(selection.ledMode+1)%4;
  else if(cursor==3)selection.dynamicShadows=!selection.dynamicShadows;
  else if(cursor==4)selection.ledBrightness=selection.ledBrightness>=100?50:selection.ledBrightness+10;
  break;
 case UiPage::EraseLicense:if(cursor==0){eraseLicenseRequest=int(selection.license);enter(UiPage::License);}else{enter(UiPage::LicenseSettings);cursor=1;uiFocusReset(*this);}break;
 case UiPage::MainMenu:if(cursor==0)enter(UiPage::Mode);else unavailable=true;break;
 case UiPage::Mode:
  if(!raceMode(UiMode(cursor))){unavailable=true;break;}
  selection.mode=UiMode(cursor);selection.teams=false;
  if(selection.mode==UiMode::GrandPrix)selection.engineClass=UiEngineClass::CC50;
  enter(selection.mode==UiMode::GrandPrix?UiPage::EngineClass:selection.mode==UiMode::VS?UiPage::VSMode:selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::Character);break;
 case UiPage::EngineClass:
  if(!engineUnlocked(cursor)||!raceEngineClass(UiEngineClass(cursor))){unavailable=true;break;}
  selection.engineClass=UiEngineClass(cursor);enter(UiPage::Character);break;
 case UiPage::BattleRules:case UiPage::VSRules:if(cursor==ruleOkRow()){enter(rulesReturn);cursor=auxiliarySlot();uiFocusReset(*this);}break;
 case UiPage::VSMode:
  selection.teams=cursor==1;enter(UiPage::Character);break;
 case UiPage::BattleMode:selection.coinRunners=cursor==1;selection.teams=true;enter(UiPage::Character);break;
 case UiPage::Character:
  // Includes the two Mii outfits, which also need Mii data.
  if(!driverUnlocked(characterDriver(cursor))||!raceDriver(characterDriver(cursor))){unavailable=true;break;}
  if(characterDriver(cursor)>=24){selectRaceMiiRequest=int(characterDriver(cursor));break;}
  selection.driver=characterDriver(cursor);
  cpuRoster.characters(selection.driver,unlocks(),cpuRandom);
  if(selection.teams){vsRandom=vsRandom*1664525u+1013904223u;selection.humanTeam=RaceTeam((vsRandom>>16)&1);enter(UiPage::TeamConfirm);}
  else enter(UiPage::Vehicle);break;
 case UiPage::TeamConfirm:enter(UiPage::Vehicle);break;
 case UiPage::Vehicle:
  if(!vehicleUnlocked(vehicleChoice(cursor))||!raceVehicle(vehicleChoice(cursor))){unavailable=true;break;}
  selection.vehicle=vehicleChoice(cursor);
  cpuRoster.vehicles(selection.vehicle,selection.mode==UiMode::VS?selection.vsVehicles:selection.mode==UiMode::Battle?selection.battleVehicles:2u,selection.mode==UiMode::Battle,unlocks(),cpuRandom);
  enter(UiPage::Drift);break;
 case UiPage::DriftHelp:enter(UiPage::Drift);cursor=2;uiFocusReset(*this);break;
 case UiPage::Drift:selection.automatic=cursor==0;
  if(selection.mode==UiMode::Battle&&selection.battleCourses){prepareBattleStages();enter(UiPage::Confirm);break;}
  if(selection.mode==UiMode::VS&&selection.vsCourses){prepareVsCourses();enter(UiPage::Confirm);}
  else enter(selection.mode==UiMode::Battle?UiPage::BattleCup:UiPage::Cup);break;
 case UiPage::Cup:
  if(!cupUnlocked(cursor)){unavailable=true;break;}
  // A Grand Prix selects the whole cup. VS and Time Trials select a course.
  if(selection.course/4!=cursor||selection.mode==UiMode::GrandPrix)selection.course=cursor*4;
  enter(selection.mode==UiMode::GrandPrix?UiPage::Confirm:UiPage::Course);break;
 case UiPage::Course:selection.course=selection.course/4*4+cursor;enter(selection.mode==UiMode::TimeTrials?UiPage::Ghost:UiPage::Confirm);break;
 case UiPage::Ghost:enter(UiPage::Confirm);break;
 case UiPage::BattleCup:
  if(selection.battleStage/5!=cursor)selection.battleStage=cursor*5;
  enter(UiPage::BattleStage);break;
 case UiPage::BattleStage:selection.battleStage=selection.battleStage/5*5+cursor;enter(UiPage::Confirm);break;
 case UiPage::Confirm:if(supportsRace())return UiAction::StartRace;unavailable=true;break;
 case UiPage::Pause:
  if(cursor==0){enter(UiPage::Race);return UiAction::ResumeRace;}
  if(cursor==1)return UiAction::RestartRace;
  if(selection.mode==UiMode::TimeTrials){
   if(cursor==2){enter(UiPage::Cup);break;}
   if(cursor==3){enter(UiPage::Character);break;}
  }
  enter(UiPage::QuitConfirm);break;
 case UiPage::QuitConfirm:
  if(cursor==0)enter(UiPage::MainMenu);else{enter(UiPage::Pause);cursor=pauseQuitSlot();}break;
 case UiPage::Results:
  // RaceMenu::OnButtonClick (USA 0x80838664): Next goes to the VS course
  // choice (section 0x4B); Quit (and, without the award scene, the final
  // race's Next) returns to the menus (section 0x41).
  // A Grand Prix goes straight on to the cup's next course.
  if(cursor==0&&!finalRace&&selection.mode==UiMode::GrandPrix){selection.course=selection.course/4*4+std::min(selection.course%4+1,3u);enter(UiPage::Confirm);return UiAction::StartRace;}
  if(cursor==0&&!finalRace&&selection.mode==UiMode::Battle&&selection.battleCourses){nextBattleStage();enter(UiPage::Confirm);return UiAction::StartRace;}
  if(cursor==0&&!finalRace&&selection.mode==UiMode::VS&&selection.vsCourses){nextVsCourse();enter(UiPage::Confirm);return UiAction::StartRace;}
  if(cursor==0&&!finalRace)enter(selection.mode==UiMode::Battle?UiPage::BattleCup:UiPage::Cup);
  else enter(UiPage::MainMenu);
  break;
 case UiPage::Error:enter(selection.mode==UiMode::GrandPrix?UiPage::Cup:selection.mode==UiMode::Battle?UiPage::BattleStage:selection.mode==UiMode::TimeTrials?UiPage::Ghost:UiPage::Course);break;
 default:break;
 }
 return UiAction::None;
}
}
