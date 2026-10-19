#pragma once
#include <mkw/menu_focus.hpp>
#include <array>
#include <vector>
#include <cstdint>
namespace mkw {
enum class UiPage {Title,MainMenu,Mode,Character,Vehicle,Drift,Cup,Course,Confirm,Race,Pause,QuitConfirm,Results,Error,Strap,License,EngineClass,VSMode,BattleMode,BattleCup,BattleStage,Ghost,Intro};
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
struct UiSelection {
 unsigned driver=0,vehicle=0,course=0;bool automatic=false;
 UiMode mode=UiMode::GrandPrix;UiEngineClass engineClass=UiEngineClass::CC150;
 unsigned license=0,battleStage=0;bool teams=false,coinRunners=false;
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
 unsigned cursor=0,age=0,focusAge=0;bool unavailable=false;
 // Results is the retail VS end-of-race menu (VSRaceEnd): Next Race / Quit
 // Game, or only Next after the final race of a VS set (IsFinalRace).
 bool finalRace=false;
 UiFocusState focus{};
 UiAction input(UiInput);
 // Touch: highlights a control as the d-pad would (the caller then sends accept
 // or back through input). False when the page has no such control.
 bool selectSlot(unsigned slot);
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
 // Only the original 150cc physics runs (Kinoko's class rate 1.0).
 static bool raceEngineClass(UiEngineClass c){return c!=UiEngineClass::Mirror;}
 static bool raceDriver(unsigned driver){return driver<24;}
 static bool raceVehicle(unsigned choice){return choice<12;}
 bool supportsRace()const;
};
// UiQuad::slot of the Confirm page's OK button (the controls of the page under it keep their own).
inline constexpr unsigned UiConfirmOkSlot=100;
// UiQuad::slot of the cup heading on the course and stage pages (art of the page, not a control).
inline constexpr unsigned UiHeadingSlot=101;
struct UiQuad {unsigned sprite;float x,y,w,h,alpha=1;uint32_t tint=0xffffffff;bool bottom=false;float u0=0,v0=0,u1=1,v1=1;unsigned movie=~0u;std::array<uint32_t,4> corners{{0xffffffff,0xffffffff,0xffffffff,0xffffffff}};bool additive=false;unsigned vehicleIcon=~0u;uint32_t vehicleColor0=0,vehicleColor1=0xffffffff;unsigned slot=~0u;};
struct UiRacer {unsigned driver=0,rank=1,lap=1;bool finished=false;float x=0,z=0;};
struct UiRaceView {std::array<UiRacer,12> racers{};unsigned count=0;unsigned milliseconds=0;};
// The same command list is consumed by Citro3D and the host layout preview.
struct UiTransitionView;
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&);
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&,const UiTransitionView&);
std::vector<UiQuad> composeMenu(const MenuState&,const UiRaceView&);
}
