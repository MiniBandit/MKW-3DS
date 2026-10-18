#pragma once
#include <mkw/menu_focus.hpp>
#include <array>
#include <vector>
#include <cstdint>
namespace mkw {
// NoGhosts: ConfirmPage (retail page 0x52) as TimeAttackBackPage (0x70) opens
// it for a Time Trials course without ghosts. TimeAttackBack: that empty page,
// on screen only while the section fades out after its OK.
enum class UiPage {Title,MainMenu,Mode,Character,Vehicle,Drift,Cup,Course,Confirm,Race,Pause,QuitConfirm,Results,Error,Strap,License,EngineClass,VSMode,BattleMode,BattleCup,BattleStage,Ghost,Intro,NoGhosts,TimeAttackBack};
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
// MultiControlInputManager's direction hold: fn_1_DF4E4 (PAL 805ef898) counts
// the 60 Hz frames one direction set is held, MultiControlInputManager_calc
// (805f0e94) moves on frame 0 and on frames 15, 30, 45... Counted in display
// frames, not loop passes. reset() is the page activation (fn_1_DF5A4).
class UiDirectionRepeat {
public:
 // The directions held now; frames: 60 Hz frames since the previous step.
 UiInput step(bool up,bool down,bool left,bool right,unsigned frames);
 void reset(){mask_=0;held_=0;}
private:
 unsigned mask_=0,held_=0;
};
struct UiSelection {
 unsigned driver=0,vehicle=0,course=0;bool automatic=false;
 UiMode mode=UiMode::GrandPrix;UiEngineClass engineClass=UiEngineClass::CC150;
 unsigned license=0,battleStage=0;bool teams=false,coinRunners=false;
 // Time Trials ghost page: 0 Race This Ghost, 1 Watch the Replay, 2 Solo.
 unsigned ghostChoice=2;
 // The retail unlocked engine-class page switches both 50cc/100cc entries
 // together between their restricted type and the combined kart/bike list.
 bool mixedEngineTypes=false;
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
 // The fastest usable ghost per course in milliseconds (0: none), filled in
 // by the race runtime before the Time Trial ghost page.
 std::array<uint32_t,32> ghostTimes{};
 unsigned cursor=0,age=0,focusAge=0;bool unavailable=false;
 // Frames since the menu section began: the section's MenuBG keeps running
 // across its pages (MenuTransition carries it over each page change).
 unsigned sectionAge=0;
 // Results is the retail VS end-of-race menu (VSRaceEnd): Next Race / Quit
 // Game, or only Next after the final race of a VS set (IsFinalRace).
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
 // The Time Trials page after the course: the ghost list, or the no-ghost
 // popup (TimeAttackBackPage_onReady, PAL 0x806393C8) when the list is empty.
 UiPage ghostPage()const{return ghostTimes[selection.course%ghostTimes.size()]?UiPage::Ghost:UiPage::NoGhosts;}
 // The race runtime starts races with all 24 retail drivers and all twelve
 // machines each. Other choices stay browsable, but accepting one reports
 // "Not available" on its own page rather than at the final confirmation.
 static bool raceMode(UiMode mode){return mode==UiMode::VS||mode==UiMode::GrandPrix||mode==UiMode::TimeTrials||mode==UiMode::Battle;}
 // Only the original 150cc physics runs (Kinoko's class rate 1.0).
 static bool raceEngineClass(UiEngineClass){return true;}
 static bool raceDriver(unsigned driver){return driver<24;}
 static bool raceVehicle(unsigned choice){return choice<12;}
 bool supportsRace()const;
};
// material: the NW4R default material, lerp(color0,color1,texture) times the
// corner colours (pane TEV C0/C1 applied live, e.g. a white glow silhouette).
struct UiQuad {unsigned sprite;float x,y,w,h,alpha=1;uint32_t tint=0xffffffff;bool bottom=false;float u0=0,v0=0,u1=1,v1=1;unsigned movie=~0u;std::array<uint32_t,4> corners{{0xffffffff,0xffffffff,0xffffffff,0xffffffff}};bool additive=false;unsigned vehicleIcon=~0u;uint32_t vehicleColor0=0,vehicleColor1=0xffffffff;bool material=false;uint32_t color0=0,color1=0xffffffff;};
struct UiRacer {unsigned driver=0,rank=1,lap=1;bool finished=false;float x=0,z=0;};
struct UiRaceView {std::array<UiRacer,12> racers{};unsigned count=0;unsigned milliseconds=0;};
// The same command list is consumed by Citro3D and the host layout preview.
struct UiTransitionView;
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&);
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&,const UiTransitionView&);
std::vector<UiQuad> composeMenu(const MenuState&,const UiRaceView&);
}
