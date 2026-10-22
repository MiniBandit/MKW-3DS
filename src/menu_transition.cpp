#include <mkw/menu_transition.hpp>
#include <mkw/color_fader.hpp>
#include <mkw/menu_animations.hpp>
#include <mkw/menu_index.hpp>
#include <algorithm>
#include <cmath>

namespace mkw {
namespace {
float delay(const char* control,const char* variant){
 const auto* motion=uiFindControlMotion(control,variant);
 return motion?std::max(0.f,motion->delay):0.f;
}
float maximum(const char* control){
 float result=0;
 for(const auto& motion:UiControlMotions)
  if(!std::strcmp(control,motion.control))result=std::max(result,motion.delay);
 return result;
}
// Section changes fade through the scene's EGG::ColorFader (color_fader.hpp).
constexpr float FadeOutLastFrame=ColorFadeOutLastFrame,FadeInLastFrame=ColorFadeInLastFrame;
float fadeOpacity(float frame,bool entering){return colorFaderOpacity(frame,entering);}
}

const char* uiPauseMenuControl(const MenuState& state){
 switch(state.selection.mode){
 case UiMode::TimeTrials:return "PauseMenuTimeAttack";
 case UiMode::GrandPrix:return "PauseMenuGrandPrix";
 case UiMode::Battle:return "PauseMenuBT";
 default:return "PauseMenuVS";
 }
}

const char* uiSelectedMenuControl(const MenuState& state,bool back){
 if(back)return "Back";
 switch(state.page){
 case UiPage::Title:return "PressStart";
 case UiPage::License:return state.cursor<2?"LicenseSelectT":"LicenseSelectB";
 case UiPage::MainMenu:return state.cursor==0?"TopMenuSingle":state.cursor<4?"TopMenuMulti":state.cursor<6?"TopMenuWifi":"TopMenuChannel";
 case UiPage::Mode:return "SingleTop";
 case UiPage::EngineClass:return "ClassSelect2";
 case UiPage::VSMode:return "VSSelect";
 case UiPage::BattleMode:return "BattleSelect";
 case UiPage::Character:{
  static const char* controls[]={"CharacterSelect4_0","CharacterSelect4_1","CharacterSelect4_2","CharacterSelect4_3"};
  return controls[std::min(state.cursor/8,3u)];
 }
 case UiPage::Vehicle:return state.columns()==2?"MachineSelect2_6":"MachineSelect1_6";
 case UiPage::Drift:return "PadEasy";
 case UiPage::Cup:return "CupSelectCup";
 case UiPage::Course:return "CourseSelectCourse";
 case UiPage::BattleCup:return "BattleCupSelectCup";
 case UiPage::BattleStage:return "BattleStageSelectStage";
 case UiPage::Ghost:return "TimeAttackGhostList";
 case UiPage::Confirm:return "MenuEndConfirm";
 case UiPage::Pause:return uiPauseMenuControl(state);
 case UiPage::QuitConfirm:return uiPauseMenuControl(state); // AfterMenuEndConfirm's animations (menu_ui.cpp)
 case UiPage::Error:case UiPage::NoGhosts:return "ConfirmOK";
 default:return "";
 }
}

float uiMenuMaxDelay(const MenuState& state){
 switch(state.page){
 case UiPage::License:return std::max(maximum("LicenseSelectT"),maximum("LicenseSelectB"));
 case UiPage::MainMenu:return std::max(maximum("FileAdmin"),std::max(maximum("TopMenuWifiWaku"),maximum("TopMenuChannelWaku")));
 case UiPage::Mode:return delay("SingleTop","ButtonBT"); // Mission control is absent.
 case UiPage::EngineClass:return maximum("ClassSelect2");
 case UiPage::VSMode:return maximum("VSSelect");
 case UiPage::BattleMode:return maximum("BattleSelect");
 case UiPage::Character:return delay("CharacterSelect4_3","Button1");
 case UiPage::Vehicle:return std::max(maximum("MachineSelectNULL6"),maximum("MachineAbility"));
 case UiPage::Drift:return delay("PadEasy","ButtonNormal");
 case UiPage::Cup:return std::max(maximum("CupSelectCup"),delay("CupSelectNULL","CupSelectCourseNULL")+maximum("CupSelectCourse"));
 case UiPage::Course:return std::max(delay("CupSelectNULL","CupSelectCourseNULL")+maximum("CourseSelectCourse"),maximum("CourseSelectCup"));
 case UiPage::BattleCup:return std::max(maximum("BattleCupSelectCup"),delay("BattleCupSelectNULL","CupSelectCourseNULL")+maximum("BattleCupSelectStage"));
 case UiPage::BattleStage:return maximum("BattleStageSelectStage");
 case UiPage::Ghost:return maximum("TimeAttackGhostList");
 // Page fn_1_F2518 (PAL 806028cc) keeps the largest start frame among the
 // page's own controls. RaceConfirmPage (fn_1_3320C8, fn_1_332290) loads
 // MenuEndConfirm/ButtonOK, Back/ButtonBackMenuEndConfirm, RaceConfirmBack/
 // BlackBack and both obis - not MenuEndConfirm's ButtonBack (21).
 case UiPage::Confirm:return std::max({delay("MenuEndConfirm","ButtonOK"),delay("Back","ButtonBackMenuEndConfirm"),delay("RaceConfirmBack","BlackBack"),
                                       delay("MenuObiTopRaceConfirm","MenuEndConfirmObiTop"),delay("MenuObiBottomRaceConfirm","MenuEndConfirmObiBottom")});
 case UiPage::Pause:return maximum(uiPauseMenuControl(state));
 // AfterMenuEndConfirm.brctr start frames: ButtonConfirmContinue 4, ButtonConfirmQuit 2.
 case UiPage::QuitConfirm:return 4.f;
 case UiPage::Error:return delay("ConfirmOK","OK");
 // ConfirmPage_onInit (PAL 0x805C1EB8): MenuObiTitleText, ConfirmOK/OK,
 // Back/ButtonBackPopup and MessageWindowHalf.
 case UiPage::NoGhosts:return std::max({delay("MenuObiTitleText","MenuObiTitleText"),delay("ConfirmOK","OK"),delay("Back","ButtonBackPopup"),
                                        delay("MessageWindowHalf","MessageWindowHalf")});
 default:return 0;
 }
}

float uiMenuExitHold(const MenuState& state,bool back){
 // Physical B invokes the original page BackPress path with delay zero.
 // A pointer-clicked Back button would instead play its own OK animation.
 if(back)return 0.f;
 if(state.page==UiPage::Title){
  const auto* clip=uiFindAnimationClip("PressStart","eAFPressStart","PressStart");
  return clip&&clip->rate>0?clip->duration/clip->rate:0.f;
 }
 // CharacterSelect's front handler (PAL 8083dfa8) passes max(OK delay,
 // DriverModelMgr+0xc) to startReplace. The manager's ctor (fn_1_31FDCC,
 // 80830180) stores there the longest sel_gut CHR0 of all menu drivers
 // (fn_1_4AADC 8055ae90: frame count): Driver.szs fk.brres, 78 frames.
 if(state.page==UiPage::Character)return std::max(uiButtonConfirmFrames(uiSelectedMenuControl(state)),78.f);
 return uiButtonConfirmFrames(uiSelectedMenuControl(state));
}

bool uiMenuChangesSection(UiPage from,UiPage to,UiAction action){
 if(action!=UiAction::None)return action!=UiAction::ResumeRace;
 // The pause pages' Quit, Change Course and Change Character change section
 // (RaceMenuPage_onButtonFront, PAL 0x8085A0F4, cases 1/5/6/0x18); only the
 // GP Quit pushes page 0x2C (QuitConfirm) in the race section.
 const bool leavesPause=(from==UiPage::Pause||from==UiPage::QuitConfirm)&&to!=UiPage::Pause&&to!=UiPage::QuitConfirm&&to!=UiPage::Race;
 return (from==UiPage::MainMenu&&to==UiPage::Mode)||(from==UiPage::Mode&&to==UiPage::MainMenu)
     ||from==UiPage::Intro||to==UiPage::Intro||from==UiPage::Race||to==UiPage::Race||leavesPause;
}

void MenuTransition::reset(){
 phase_=Phase::Idle;elapsed_=-1;exitHold_=outDelay_=inDelay_=0;
 pending_=ready_=UiAction::None;selectedControl_="";section_=back_=skipEnter_=skipExit_=false;
 hasUnderlay_=openingModal_=closingModal_=holding_=false;underlayHoldFrame_=-1;underlaySelectedControl_="";
 fadeAfterExit_=false;
}

bool MenuTransition::begin(const MenuState& before,const MenuState& after,bool back,UiAction action){
 if(active()||(before.page==after.page&&action==UiAction::None))return false;
 openingModal_=after.page==UiPage::Confirm&&before.page!=UiPage::Confirm;
 closingModal_=hasUnderlay_&&before.page==UiPage::Confirm&&after.page==underlay_.page&&action==UiAction::None;
 if(openingModal_){underlay_=before;hasUnderlay_=true;}
 outgoing_=before;incoming_=after.page;back_=back;
 exitHold_=uiMenuExitHold(before,back);outDelay_=uiMenuMaxDelay(before);inDelay_=uiMenuMaxDelay(after);
 selectedControl_=back?"":uiSelectedMenuControl(before);
 section_=uiMenuChangesSection(before.page,after.page,action);
 // ConfirmPage's OK (PAL 0x805C21C0) replaces the popup after the OK
 // animation, so it slides out first; TimeAttackBackPage_onRefocus
 // (0x80639260) then changes section with delay 0 behind it.
 fadeAfterExit_=action==UiAction::StartRace&&before.page==UiPage::NoGhosts;
 if(fadeAfterExit_)section_=false;
 // Cup and Course pages share their cup controls and never slide between
 // each other (retail USA REL):
 //  CupSelect_BeforeExit 8082761c: forward to page 0x6f ends the exit state
 //   (805ddbc4) - after the OK hold the cup page leaves without sliding.
 //  CourseSelect_BeforeEntranceAnimations 80825b60: forward from page 0x6e
 //   calls Page_EndState - the course page appears already in place.
 //  CupSelect_BeforeEntrance 808275c0: back from page 0x6f calls
 //   Page_EndState. CourseSelect has no exit override, so its exit animates.
 skipExit_=!back&&before.page==UiPage::Cup&&after.page==UiPage::Course;
 skipEnter_=skipExit_||(back&&before.page==UiPage::Course&&after.page==UiPage::Cup);
 pending_=action;ready_=UiAction::None;elapsed_=-1;phase_=Phase::Exit;
 // Menu::ChangeSectionWithDelay checks whether the destination needs a
 // confirmation. Its virtual +0x24 dispatch adds StartRace immediately and
 // ignores the OK delay; the underlying button animates concurrently.
 if(openingModal_){
  section_=false;phase_=Phase::Enter;
  underlayHoldFrame_=-1;underlaySelectedControl_=selectedControl_;
 }
 return true;
}

void MenuTransition::enterSection(const MenuState& incoming){
 // A section the race left for (the end-of-race menu's Next Race / Quit Game):
 // the race already faded to black, so the page starts with the scene fader's
 // fade in and its entrance animation, as after any other section change.
 reset();
 outgoing_=incoming;incoming_=incoming.page;section_=true;
 inDelay_=uiMenuMaxDelay(incoming);
 phase_=Phase::Enter;elapsed_=-1;
}

void MenuTransition::enterPage(const MenuState& incoming){
 reset();outgoing_=incoming;incoming_=incoming.page;
 inDelay_=uiMenuMaxDelay(incoming);phase_=Phase::Enter;elapsed_=-1;
}

UiAction MenuTransition::input(MenuState& logical,UiInput key){
 if(active())return UiAction::None;
 if(ready_!=UiAction::None)return takeAction();
 const MenuState before=logical;
 const UiAction action=logical.input(key);
 return begin(before,logical,key.back,action)?UiAction::None:action;
}

bool MenuTransition::advance(MenuState& logical,unsigned ticks,bool incomingReady){
 bool changed=false;
 holding_=false;
 while(active()){
  const bool entering=phase_==Phase::Enter;
  const float duration=entering?(section_?std::max(FadeInLastFrame,UiPageMotionFrames+inDelay_):UiPageMotionFrames+inDelay_)
      :exitHold_+(openingModal_||skipExit_?0.f:section_?FadeOutLastFrame:UiPageMotionFrames+outDelay_);
  if(elapsed_>=duration){
   if(!entering){
    if(fadeAfterExit_){
     // The emptied TimeAttackBackPage stays on screen through the fade.
     fadeAfterExit_=false;section_=true;exitHold_=outDelay_=0;selectedControl_="";
     outgoing_.page=UiPage::TimeAttackBack;elapsed_=-1;continue;
    }
    if(pending_!=UiAction::None){ready_=pending_;pending_=UiAction::None;phase_=Phase::Idle;break;}
    if(closingModal_){logical.age=underlay_.age;logical.focusAge=underlay_.focusAge;logical.sectionAge=underlay_.sectionAge;hasUnderlay_=closingModal_=false;phase_=Phase::Idle;elapsed_=-1;changed=true;continue;}
    // The incoming page appears only once its assets are resident: hold the
    // completed exit (still presenting at 60 Hz) instead of blocking on I/O.
    if(!incomingReady){holding_=true;ticks=0;break;}
    phase_=skipEnter_?Phase::Idle:Phase::Enter;openingModal_=false;elapsed_=-1;logical.age=logical.focusAge=0;changed=true;
    // The section's background runs on through a page change; a section
    // change rebuilds the scene behind the fade.
    logical.sectionAge=section_?0:outgoing_.sectionAge;
   }else{phase_=Phase::Idle;elapsed_=-1;}
   continue;
  }
  if(!ticks)break;
  // A modal opens without an exit phase; it waits before its first frame.
  if(entering&&openingModal_&&elapsed_<0&&!incomingReady){holding_=true;ticks=0;break;}
  const unsigned remaining=std::max(1u,unsigned(std::ceil(duration-elapsed_)));
  const unsigned step=std::min(ticks,remaining);
  if(entering)logical.advance(step);else outgoing_.advance(step);
  if(hasUnderlay_){underlay_.advance(step);underlayHoldFrame_+=step;}
  elapsed_+=step;ticks-=step;
 }
 if(ticks&&!active()&&ready_==UiAction::None){logical.advance(ticks);if(hasUnderlay_){underlay_.advance(ticks);underlayHoldFrame_+=ticks;}}
 return changed;
}

const MenuState& MenuTransition::visual(const MenuState& logical)const{
 return phase_==Phase::Exit?outgoing_:logical;
}

UiTransitionView MenuTransition::view()const{
 UiTransitionView result;
 result.from=outgoing_.page;result.to=incoming_;
 result.modal=hasUnderlay_;result.underlay=hasUnderlay_?&underlay_:nullptr;
 if(hasUnderlay_){result.underlayHoldFrame=underlayHoldFrame_;result.underlaySelectedControl=underlaySelectedControl_;}
 result.active=active();if(!result.active)return result;
 result.entering=phase_==Phase::Enter;result.back=back_;result.section=section_;
 result.elapsed=elapsed_;result.maxDelay=result.entering?inDelay_:outDelay_;result.exitHold=exitHold_;
 result.holdFrame=result.entering?0.f:std::clamp(elapsed_,0.f,exitHold_);
 result.selectedControl=result.entering?"":selectedControl_;
 if(section_)result.blackAlpha=fadeOpacity(result.entering?elapsed_:elapsed_-exitHold_,result.entering);
 return result;
}

UiAction MenuTransition::takeAction(){const auto value=ready_;ready_=UiAction::None;return value;}
}
