#include <mkw/menu_transition.hpp>
#include <mkw/menu_model_motion.hpp>
#include <mkw/menu_voice_assets.hpp>
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
 case UiPage::License:if(state.cursor==4)return "LicenseSelectS";return state.cursor<2?"LicenseSelectT":"LicenseSelectB";
 case UiPage::MainMenu:if(state.cursor==7)return "FileAdmin";return state.cursor==0?"TopMenuSingle":state.cursor<4?"TopMenuMulti":state.cursor<6?"TopMenuWifi":"TopMenuChannel";
 case UiPage::Settings:return "OptionTopButton";
 case UiPage::LicenseSettings:return "LicenseManagementButton";
 case UiPage::RecordsGeneral:case UiPage::RecordsFavorites:case UiPage::RecordsOther:return "NextRecordButton";
 case UiPage::Mode:return "SingleTop";
 case UiPage::EngineClass:return "ClassSelect2";
 case UiPage::VSMode:return state.cursor==2?"RuleSetting":"VSSelect";
 case UiPage::TeamConfirm:return "TeamConfirmOK";
 case UiPage::VSRules:return state.cursor==6?"VSSetting":state.cursor==5?"VSSettingUpDownBase":"VSSettingRadioBase";
 case UiPage::BattleMode:return state.cursor==2?"RuleSetting":"BattleSelect";
 case UiPage::BattleRules:return state.cursor==5?"BattleSetting":state.cursor==4?"BattleSettingUpDownBase":"BattleSettingRadioBase";
 case UiPage::Character:{
  static const char* controls[]={"CharacterSelect4_0","CharacterSelect4_1","CharacterSelect4_2","CharacterSelect4_3"};
  return controls[std::min(state.cursor/8,3u)];
 }
 case UiPage::Vehicle:return state.columns()==2?"MachineSelect2_6":"MachineSelect1_6";
 case UiPage::Drift:return state.cursor==2?"DriftHelp":"PadEasy";
 case UiPage::Cup:return "CupSelectCup";
 case UiPage::Course:return "CourseSelectCourse";
 case UiPage::BattleCup:return "BattleCupSelectCup";
 case UiPage::BattleStage:return "BattleStageSelectStage";
 case UiPage::Ghost:return "TimeAttackGhostList";
 case UiPage::DriftHelp:return "OKKey";
 case UiPage::Confirm:return "MenuEndConfirm";
 case UiPage::Pause:return "OptionTopButton";
 case UiPage::QuitConfirm:return "OptionTopButton";
 case UiPage::EraseLicense:return "YesNoButton";
 case UiPage::Unlock:return "NextButton";
 case UiPage::Error:return "ConfirmOK";
 default:return "";
 }
}

float uiMenuMaxDelay(const MenuState& state){
 switch(state.page){
 case UiPage::License:return std::max(maximum("LicenseSelectT"),maximum("LicenseSelectB"));
 case UiPage::MainMenu:return std::max(maximum("FileAdmin"),std::max(maximum("TopMenuWifiWaku"),maximum("TopMenuChannelWaku")));
 case UiPage::Settings:return maximum("OptionTopButton");
 case UiPage::LicenseSettings:return maximum("LicenseManagementButton");
 case UiPage::RecordsGeneral:case UiPage::RecordsFavorites:case UiPage::RecordsOther:return maximum("Record");
 case UiPage::Mode:return delay("SingleTop","ButtonBT"); // Mission control is absent.
 case UiPage::EngineClass:return maximum("ClassSelect2");
 case UiPage::VSMode:return maximum("VSSelect");
 case UiPage::TeamConfirm:return maximum("TeamConfirmOK");
 case UiPage::VSRules:return maximum("VSSetting");
 case UiPage::BattleRules:return maximum("BattleSetting");
 case UiPage::BattleMode:return maximum("BattleSelect");
 case UiPage::Character:return delay("CharacterSelect4_3","Button1");
 case UiPage::Vehicle:return std::max(maximum("MachineSelectNULL6"),maximum("MachineAbility"));
 case UiPage::Drift:return delay("PadEasy","ButtonNormal");
 case UiPage::Cup:return std::max(maximum("CupSelectCup"),delay("CupSelectNULL","CupSelectCourseNULL")+maximum("CupSelectCourse"));
 case UiPage::Course:return std::max(delay("CupSelectNULL","CupSelectCourseNULL")+maximum("CourseSelectCourse"),maximum("CourseSelectCup"));
 case UiPage::BattleCup:return std::max(maximum("BattleCupSelectCup"),delay("BattleCupSelectNULL","CupSelectCourseNULL")+maximum("BattleCupSelectStage"));
 case UiPage::BattleStage:return maximum("BattleStageSelectStage");
 case UiPage::Ghost:return maximum("TimeAttackGhostList");
 case UiPage::Confirm:return maximum("MenuEndConfirm");
 case UiPage::Pause:return maximum(uiPauseMenuControl(state));
 case UiPage::QuitConfirm:return maximum("OptionTopButton");
 case UiPage::EraseLicense:return maximum("YesNoButton");
 case UiPage::Error:return delay("ConfirmOK","OK");
 default:return 0;
 }
}

float uiMenuExitHold(const MenuState& state,bool back){
 // Physical B invokes the original page BackPress path with delay zero.
 // A pointer-clicked Back button would instead play its own OK animation.
 if(back)return 0.f;
 // The original CharacterSelect click holds for the longer of its button
 // animation and the selected driver's authored celebration (80823694).
 if(state.page==UiPage::Character){
  const unsigned driver=state.characterDriver(state.cursor);
  return std::max(uiButtonConfirmFrames(uiSelectedMenuControl(state)),driver<24?float(MenuSelectAnimationFrames[driver]):0.f);
 }
 if(state.page==UiPage::Title){
  const auto* clip=uiFindAnimationClip("PressStart","eAFPressStart","PressStart");
  return clip&&clip->rate>0?clip->duration/clip->rate:0.f;
 }
 return uiButtonConfirmFrames(uiSelectedMenuControl(state));
}

bool uiMenuChangesSection(UiPage from,UiPage to,UiAction action){
 if(action!=UiAction::None)return action!=UiAction::ResumeRace;
 // Exiting a paused race changes sections just as the normal race exit does.
 // Both LCDs must retain their pictures until ColorFader reaches black.
 if((from==UiPage::Pause||from==UiPage::QuitConfirm)&&
    to!=UiPage::Pause&&to!=UiPage::QuitConfirm&&to!=UiPage::Race)return true;
 return (from==UiPage::MainMenu&&to==UiPage::Mode)||(from==UiPage::Mode&&to==UiPage::MainMenu)
     ||(from==UiPage::MainMenu&&to==UiPage::LicenseSettings)||(from==UiPage::LicenseSettings&&to==UiPage::MainMenu)
     ||from==UiPage::Intro||to==UiPage::Intro||from==UiPage::Race||to==UiPage::Race;
}

void MenuTransition::reset(){
 fromBackground_=toBackground_=-1;
 phase_=Phase::Idle;elapsed_=-1;exitHold_=outDelay_=inDelay_=0;
 pending_=ready_=UiAction::None;selectedControl_="";section_=back_=skipEnter_=skipExit_=false;
 hasUnderlay_=openingModal_=closingModal_=holding_=false;underlayHoldFrame_=-1;underlaySelectedControl_="";
}

bool MenuTransition::begin(const MenuState& before,const MenuState& after,bool back,UiAction action){
 // Consecutive rewards share a page class, but are separate original notices.
 const bool nextReward=before.page==UiPage::Unlock&&after.page==UiPage::Unlock&&
     (before.unlockCondition!=after.unlockCondition||before.unlockBit!=after.unlockBit);
 if(active()||(before.page==after.page&&action==UiAction::None&&!nextReward))return false;
 openingModal_=(after.page==UiPage::Confirm||after.page==UiPage::DriftHelp)&&before.page!=after.page;
 closingModal_=hasUnderlay_&&(before.page==UiPage::Confirm||before.page==UiPage::DriftHelp)&&after.page==underlay_.page&&action==UiAction::None;
 if(openingModal_){underlay_=before;hasUnderlay_=true;}
 outgoing_=before;incoming_=after.page;back_=back;
 fromBackground_=uiBackdropModelIndex(before);toBackground_=uiBackdropModelIndex(after);
 exitHold_=uiMenuExitHold(before,back);outDelay_=uiMenuMaxDelay(before);inDelay_=uiMenuMaxDelay(after);
 selectedControl_=back?"":uiSelectedMenuControl(before);
 section_=uiMenuChangesSection(before.page,after.page,action);
 // Retail shares Cup/Course controls and skips forward exit/entrance
 // (USA 8082761c, 80825b60, 808275c0). The two-LCD reflow separates
 // these controls; animate both directions using the ordinary page clock.
 skipExit_=skipEnter_=false;
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

void MenuTransition::holdConfirmation(float frames){
 if(phase_==Phase::Exit&&elapsed_<0)exitHold_=std::max(exitHold_,frames);
}
void MenuTransition::enterSection(const MenuState& incoming){
 // A section the race left for (the end-of-race menu's Next Race / Quit Game):
 // the race already faded to black, so the page starts with the scene fader's
 // fade in and its entrance animation, as after any other section change.
 reset();
 outgoing_=incoming;incoming_=incoming.page;section_=true;
 toBackground_=uiBackdropModelIndex(incoming);
 inDelay_=uiMenuMaxDelay(incoming);
 phase_=Phase::Enter;elapsed_=-1;
}

void MenuTransition::enterPage(const MenuState& incoming){
 reset();outgoing_=incoming;incoming_=incoming.page;
 toBackground_=uiBackdropModelIndex(incoming);
 inDelay_=uiMenuMaxDelay(incoming);phase_=Phase::Enter;elapsed_=-1;
}

UiAction MenuTransition::input(MenuState& logical,UiInput key){
 if(active())return UiAction::None;
 if(ready_!=UiAction::None)return takeAction();
 MenuState before=logical;
 const UiAction action=logical.input(key);
 if(before.page==UiPage::MainMenu&&key.accept&&!key.back){
  // Keep the same press clock on an outgoing snapshot and on unavailable
  // destinations which stay on this page. Moving focus alone never starts it.
  const unsigned slot=logical.page==before.page?logical.cursor:before.cursor;
  uiFocusMainConfirm(before,slot);
  if(logical.page==before.page)uiFocusMainConfirm(logical,slot);
 }
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
    if(pending_!=UiAction::None){ready_=pending_;pending_=UiAction::None;phase_=Phase::Idle;break;}
    if(closingModal_){logical.age=underlay_.age;logical.focusAge=underlay_.focusAge;hasUnderlay_=closingModal_=false;phase_=Phase::Idle;elapsed_=-1;changed=true;continue;}
    // The incoming page appears only once its assets are resident: hold the
    // completed exit (still presenting at 60 Hz) instead of blocking on I/O.
    if(!incomingReady){holding_=true;ticks=0;break;}
    phase_=skipEnter_?Phase::Idle:Phase::Enter;openingModal_=false;elapsed_=-1;logical.age=logical.focusAge=0;changed=true;
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
 result.fromBackground=fromBackground_;result.toBackground=toBackground_;
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
