#pragma once
#include <mkw/menu_ui.hpp>

namespace mkw {
// The visual page remains the outgoing snapshot until its exit has finished.
// elapsed is a native 60 Hz page frame, initialized to -1 on each phase.
struct UiTransitionView {
 bool active=false,entering=false,back=false;
 float elapsed=-1,maxDelay=0,exitHold=0;
 float holdFrame=0,blackAlpha=0;
 bool section=false;
 const char* selectedControl="";
 bool modal=false,preload=false,confirming=false;
 float underlayHoldFrame=-1;
 const char* underlaySelectedControl="";
 const MenuState* underlay=nullptr;
 UiPage from=UiPage::Strap,to=UiPage::Strap;
 int fromBackground=-1,toBackground=-1;
};

const char* uiSelectedMenuControl(const MenuState&,bool back=false);
const char* uiPauseMenuControl(const MenuState&);
float uiMenuMaxDelay(const MenuState&);
float uiMenuExitHold(const MenuState&,bool back=false);
bool uiMenuChangesSection(UiPage from,UiPage to,UiAction action=UiAction::None);

class MenuTransition {
public:
 // Wraps MenuState::input and drops inputs while a transition is active.
 // Terminal actions are released only after the outgoing page has finished.
 UiAction input(MenuState& logical,UiInput);
 bool begin(const MenuState& before,const MenuState& after,bool back=false,
            UiAction action=UiAction::None);
 // Start on `incoming` as a section change seen from black (the race faded
 // out): fade in with the page's entrance. The caller resets the page age.
 void enterSection(const MenuState& incoming);
 void holdConfirmation(float frames);
 void enterPage(const MenuState& incoming);
 // Replaces logical.advance(): the incoming clock is held while the previous
 // page is visible. Returns true when the visible page changes this call.
 // While incomingReady is false the transition holds at the boundary where
 // the incoming page would first appear (end of exit, or a modal's start).
 bool advance(MenuState& logical,unsigned ticks,bool incomingReady=true);
 const MenuState& visual(const MenuState& logical)const;
 UiTransitionView view()const;
 bool active()const{return phase_!=Phase::Idle;}
 // The incoming page is the logical state while the outgoing one is visible.
 bool exiting()const{return phase_==Phase::Exit||(phase_==Phase::Enter&&openingModal_&&elapsed_<0);}
 bool holding()const{return holding_;}
 UiAction takeAction();
 void reset();
private:
 enum class Phase {Idle,Exit,Enter};
 Phase phase_=Phase::Idle;
 MenuState outgoing_{};
 MenuState underlay_{};
 UiPage incoming_=UiPage::Strap;
 int fromBackground_=-1,toBackground_=-1;
 bool back_=false,section_=false,skipEnter_=false,skipExit_=false;
 bool hasUnderlay_=false,openingModal_=false,closingModal_=false,holding_=false;
 float elapsed_=-1,exitHold_=0,outDelay_=0,inDelay_=0;
 float underlayHoldFrame_=-1;
 const char* underlaySelectedControl_="";
 const char* selectedControl_="";
 UiAction pending_=UiAction::None,ready_=UiAction::None;
};
}
