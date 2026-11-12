#pragma once
#include <mkw/menu_focus.hpp>
#include <mkw/race_teams.hpp>
#include <mkw/license_records.hpp>
#include <mkw/license_times.hpp>
#include <mkw/license_unlocks.hpp>
#include <mkw/cpu_roster.hpp>
#include <array>
#include <algorithm>
#include <vector>
#include <cstdint>
namespace mkw {
enum class UiPage {Title,MainMenu,Mode,Character,Vehicle,Drift,Cup,Course,Confirm,Race,Pause,QuitConfirm,Results,Error,Strap,License,EngineClass,VSMode,BattleMode,BattleCup,BattleStage,Ghost,Intro,DriftHelp,VSRules,TeamConfirm,Settings,LicenseSettings,EraseLicense,RecordsGeneral,RecordsFavorites,RecordsOther,Unlock,BattleRules};
enum class UiAction {None,StartRace,RestartRace,ResumeRace,Exit};
enum class UiMode {GrandPrix,TimeTrials,VS,Battle};
enum class UiEngineClass {CC50,CC100,CC150,Mirror};
struct UiMainMenuControl {unsigned group;float x,y;};
// Seven source child controls in four frames. Centers combine the original
// widescreen BRCTR frame transform and child translation (source canvas units).
inline constexpr std::array<UiMainMenuControl,7> UiMainMenuControls{{
 {0,-135.f,68.8f},{1,86.4f,68.8f},{1,135.f,68.8f},{1,183.6f,68.8f},
 {2,-172.8f,-93.2f},{2,-97.2f,-93.2f},{3,135.f,-93.2f}
}};
struct UiInput {bool up=false,down=false,left=false,right=false,accept=false,back=false,start=false,switchType=false;};
inline constexpr unsigned UiBattleWinCounts[]={2,3,4,5,8,10}; // original808a6d58
inline constexpr unsigned UiVsRaceCounts[]={2,3,4,5,8,10,12,16,32};
struct UiSelection {
 unsigned driver=0,vehicle=0,course=0;bool automatic=false;
 unsigned miiWeight=1,miiCharacter=30;bool miiSelected=false;
 UiMode mode=UiMode::GrandPrix;UiEngineClass engineClass=UiEngineClass::CC150;
 unsigned vsClass=2,vsCpu=1,vsVehicles=2,vsCourses=0,vsItems=0,vsRaces=4;
 unsigned battleCpu=1,battleVehicles=2,battleCourses=0,battleItems=0,battleWins=2;
 RaceTeam humanTeam=RaceTeam::Red;
 unsigned license=0,battleStage=0;bool teams=false,coinRunners=false;
 // The retail unlocked engine-class page switches both 50cc/100cc entries
 // together between their restricted type and the combined kart/bike list.
 bool mixedEngineTypes=false;
 unsigned ledMode=0,ledBrightness=100;bool dynamicShadows=true; // Per-license port options, separate from retail RKSD.
 bool operator==(const UiSelection&)const=default;
};
struct UiCharacterInfo {const char* name;unsigned retailId,weight;};
// The first 24 entries are the cooked retail race roster. Mii identity models
// require the separate profile-rendering path before they can enter a race.
inline constexpr std::array<UiCharacterInfo,26> UiCharacters{{
 {"Mario",0,1},{"Luigi",7,1},{"Peach",16,1},{"Yoshi",10,1},
 {"Baby Mario",6,0},{"Baby Peach",1,0},{"Toad",8,0},{"Koopa Troopa",14,0},
 {"Wario",11,2},{"Waluigi",2,2},{"Donkey Kong",9,2},{"Bowser",3,2},
 {"Baby Daisy",4,0},{"Baby Luigi",12,0},{"Birdo",17,1},{"Daisy",15,1},
 {"Diddy Kong",18,1},{"Dry Bones",5,0},{"Dry Bowser",21,2},{"Funky Kong",22,2},
 {"King Boo",19,2},{"Rosalina",23,2},{"Toadette",13,0},{"Bowser Jr.",20,1},
 {"Mii Outfit A",24,1},{"Mii Outfit B",26,1}
}};
// Full retail roster in screen row order; race storage order is independent.
inline constexpr std::array<unsigned,26> UiCharacterDrivers{{
 4,13,5,12, 6,22,7,17, 0,1,2,15, 3,14,16,23, 8,9,10,11, 20,21,19,18, 24,25
}};
// Choices match GarageChoices (VehiclesPerDriver per driver) and the vehicle sprite keys.
inline constexpr const char* UiVehicleNames[3][12]={
 {"Standard Kart S","Booster Seat","Mini Beast","Standard Bike S","Bullet Bike","Bit Bike","Cheep Charger","Tiny Titan","Blue Falcon","Quacker","Magikruiser","Jet Bubble"},
 {"Standard Kart M","Classic Dragster","Wild Wing","Standard Bike M","Mach Bike","Sugarscoot","Super Blooper","Daytripper","Sprinter","Zip Zip","Sneakster","Dolphin Dasher"},
 {"Standard Kart L","Offroader","Flame Flyer","Standard Bike L","Flame Runner","Wario Bike","Piranha Prowler","Jetsetter","Honeycoupe","Shooting Star","Spear","Phantom"}
};
inline constexpr const char* UiBattleStageNames[]={"Block Plaza","Delfino Pier","Funky Stadium","Chain Chomp Wheel","Thwomp Desert","SNES Battle Course 4","GBA Battle Course 3","N64 Skyscraper","GCN Cookie Land","DS Twilight House"};
// Platform-independent navigation. Scene mutation is exclusively handled by main.
class MenuState {
public:
 UiPage page=UiPage::Strap;UiSelection selection{};
 unsigned cursor=0,age=0,focusAge=0;bool unavailable=false;
 int previewCourse=-1;
 LicenseRecords licenseRecords{};
 std::array<CourseBestTimes,4> licenseBestTimes{};
 unsigned unlockCondition=~0u,unlockBit=~0u;
 bool unlockNextRequested=false;
 unsigned titleLicense=~0u; // transient card shown when returning from license browsing
 unsigned titleArtwork=0,lastTitleArtwork=~0u;
 std::uint32_t titleRandom=0x731f29a5u;
 std::array<bool,4> licenseOccupied{};std::array<std::array<std::uint32_t,32>,4> licenseCups{};bool saveFailed=false;int eraseLicenseRequest=-1;
 std::array<std::array<std::uint16_t,10>,4> licenseNames{};
 std::array<LicenseUnlockFlags,4> licenseUnlocks{};
 const LicenseUnlockFlags& unlocks()const{return licenseUnlocks[std::min(selection.license,3u)];}
 int selectRaceMiiRequest=-1;unsigned selectedMiiVoice=0;
 void confirmRaceMii(unsigned weight,unsigned character);
 unsigned driverWeight()const{return selection.driver<24?UiCharacters[selection.driver].weight:std::min(selection.miiWeight,2u);}
 bool driverUnlocked(unsigned driver)const;
 bool vehicleUnlocked(unsigned choice)const;
 bool cupUnlocked(unsigned cup)const;
 bool choiceVisible(unsigned slot)const;
 unsigned visibleChoiceIndex(unsigned slot)const;
 unsigned visibleChoiceCount()const;
 bool engineUnlocked(unsigned cc)const{return cc<3||(cc==3&&licenseCompleted(unlocks(),52));}
 bool mixedTypesUnlocked(unsigned cc)const{return cc>=2||(cc<2&&licenseCompleted(unlocks(),30+cc));}
 int editLicenseRequest=-1;
 unsigned auxiliaryReturnCursor=0;UiPage rulesReturn=UiPage::VSMode;
 UiSelection rulesEntry{};
 std::array<unsigned,32> vsCourseDeck{};unsigned vsCourseIndex=0;
 std::array<unsigned,10> battleStageDeck{};unsigned battleStageIndex=0;
 void prepareBattleStages();void nextBattleStage();
 bool rulePage()const{return page==UiPage::VSRules||page==UiPage::BattleRules;}
 unsigned ruleCountRow()const{return page==UiPage::BattleRules?4:5;}
 unsigned ruleOkRow()const{return ruleCountRow()+1;}
 unsigned vsRandom=0x4d4b5756;
 CpuRoster cpuRoster;CpuRosterRandom cpuRandom;
 void prepareVsCourses();
 void nextVsCourse();
 // The corner control is an independent PushButton, outside the main grid.
 unsigned auxiliarySlot()const{return page==UiPage::MainMenu?7:(page==UiPage::Drift||page==UiPage::VSMode||page==UiPage::BattleMode)?2:~0u;}
 // Results is the retail VS end-of-race menu (VSRaceEnd): Next Race / Quit
 // Game, or only Next after the final race of a VS set (IsFinalRace).
 // Dad requested Restart in every playable mode; retain TT's extra
 // ChangeCourse/ChangeCharacter actions. Original GP/VS/BT omit Restart. TT adds
 // Restart,ChangeCourse,ChangeCharacter before Quit (USA808a84a8..d0).
 unsigned pauseCount()const{return selection.mode==UiMode::TimeTrials?5u:3u;}
 unsigned pauseQuitSlot()const{return pauseCount()-1;}
 const char* pauseVariant(unsigned i)const{
  static constexpr const char* tt[]={"ButtonContinue","ButtonRestart","ButtonChangeCourse","ButtonChangeCharacter","ButtonQuit"};
  return selection.mode==UiMode::TimeTrials?tt[std::min(i,4u)]:i==0?"ButtonContinue":i==1?"ButtonRestart":"ButtonQuit";
 }
 const char* pauseLabel(unsigned i)const{
  static constexpr const char* tt[]={"Continue","Restart","Change Course","Change Character","Quit"};
  return selection.mode==UiMode::TimeTrials?tt[std::min(i,4u)]:i==0?"Continue":i==1?"Restart":"Quit";
 }
 bool finalRace=false;
 UiFocusState focus{};
 UiAction input(UiInput);
 void advance(unsigned ticks);
 void enter(UiPage);
 void raceStarted(){enter(UiPage::Race);}
 void finish(bool last=false){finalRace=last;enter(UiPage::Results);}
 // The race keeps running behind its end-of-race menu.
 bool simulates()const{return page==UiPage::Race||page==UiPage::Results;}
 // Logical Race is selected before the pause exit animation completes.
 // Its assets must survive every frame of that transition back to gameplay.
 bool retainsRaceAssets()const{return simulates()||page==UiPage::Pause||page==UiPage::QuitConfirm;}
 unsigned count()const;unsigned columns()const;
 unsigned characterDriver(unsigned slot)const;
 unsigned vehicleChoice(unsigned slot)const;
 unsigned vehicleSlot(unsigned choice)const;
 unsigned engineClassMovie(unsigned slot)const;
 // The race runtime starts races with all 24 retail drivers and all twelve
 // machines each. Other choices stay browsable, but accepting one reports
 // "Not available" on its own page rather than at the final confirmation.
 static bool raceMode(UiMode mode){return mode==UiMode::VS||mode==UiMode::GrandPrix||mode==UiMode::TimeTrials||mode==UiMode::Battle;}
 // Mirror shares source physics coordinates with a reflected race camera/input.
 static bool raceEngineClass(UiEngineClass c){return unsigned(c)<=unsigned(UiEngineClass::Mirror);}
 static bool raceDriver(unsigned driver){return driver<26;}
 static bool raceVehicle(unsigned choice){return choice<12;}
 bool supportsRace()const;
};
// A control keeps its source animation and a stable layout rectangle. Layout
// uses the latter, never the animated bounds, so slide/focus tracks cannot
// move neighbouring controls. Modal underlays retain their own page tags.
enum class UiMenuRole : uint8_t {Content,Background,Header,Footer,Title,Description,Back,Choice,Auxiliary,Name,Statistics,Preview,PreviewLabel,Information,Shade,Dialog,Notice,TitleCard,CupRecord,TimeTrialRecord};
struct UiMenuLayout {
 UiPage page=UiPage::Race;UiMenuRole role=UiMenuRole::Content;
 unsigned group=0;float x=0,y=0,w=400,h=240;bool preserveAspect=false,selected=false;uint8_t columns=0;
};
struct UiQuad {unsigned sprite;float x,y,w,h,alpha=1;uint32_t tint=0xffffffff;bool bottom=false;float u0=0,v0=0,u1=1,v1=1;unsigned movie=~0u;std::array<uint32_t,4> corners{{0xffffffff,0xffffffff,0xffffffff,0xffffffff}};bool additive=false;unsigned vehicleIcon=~0u;uint32_t vehicleColor0=0,vehicleColor1=0xffffffff;bool fontRemap=false;uint32_t fontColor0=0,fontColor1=0xffffffff;bool affine=false;std::array<std::array<float,2>,4> positions{};UiMenuLayout menu{};float screenSlide=0;float titleTransfer=0;float previewFullAlpha=-1;unsigned licensePortrait=~0u;};
struct UiRacer {unsigned driver=0,rank=1,lap=1;bool finished=false;float x=0,z=0;};
struct UiRaceView {std::array<UiRacer,12> racers{};unsigned count=0;unsigned milliseconds=0;};
// The same command list is consumed by Citro3D and the host layout preview.
struct UiTransitionView;
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&);
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&,const UiTransitionView&);
// The top backdrop is drawn separately before the 3D preview. This returns
// top foreground and the entire bottom page, with bottom flags ready for
// MenuRenderer::draw. Strap/Intro and race output keep their old layout.
void layoutMenuScreens(const MenuState&,const std::vector<UiQuad>& source,
                       std::vector<UiQuad>& top,std::vector<UiQuad>& bottom);
// Allocation size of the actual sprite-index set requested by MenuRenderer.
// Does not read or retain textures; movies and live vehicle icons are excluded.
std::uint32_t uiResidentTextureBytes(const std::vector<UiQuad>&);
std::vector<UiQuad> composeMenu(const MenuState&,const UiRaceView&);
}
