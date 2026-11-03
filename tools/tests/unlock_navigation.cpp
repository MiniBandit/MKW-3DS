#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 MenuState s;s.selection.license=2;s.selection.driver=1;s.enter(UiPage::Unlock);
 s.unlockCondition=17;s.unlockBit=0;
 auto selection=s.selection;
 UiInput back;back.back=true;s.input(back);assert(s.page==UiPage::Unlock&&!s.unlockNextRequested);
 UiInput move;move.up=move.down=move.left=move.right=move.start=move.switchType=true;s.input(move);
 assert(s.page==UiPage::Unlock&&!s.unlockNextRequested&&s.selection==selection);
 UiInput next;next.accept=true;assert(s.input(next)==UiAction::None);
 assert(s.unlockNextRequested&&s.page==UiPage::Unlock&&s.unlockCondition==17&&s.unlockBit==0);
 for(auto& flags:s.licenseUnlocks)for(auto word:flags)assert(word==0);
 MenuState following=s;following.unlockCondition=18;following.unlockBit=1;following.unlockNextRequested=false;
 MenuTransition transition;
 assert(transition.begin(s,following));
 assert(transition.exiting()&&transition.visual(following).unlockBit==0);
 assert(!transition.begin(s,following)); // no overlapping input during confirmation
 transition.advance(following,1000,false);
 assert(transition.holding()&&transition.visual(following).unlockBit==0);
 transition.advance(following,1000,true);
 assert(!transition.active()&&transition.visual(following).unlockBit==1);
 assert(!transition.begin(following,following)); // unchanged reward / write failure retry
 MenuState main=following;main.enter(UiPage::MainMenu);
 assert(transition.begin(following,main));
 assert(transition.visual(main).page==UiPage::Unlock);
 transition.advance(main,1000,true);assert(!transition.active()&&transition.visual(main).page==UiPage::MainMenu);
 puts("Unlock navigation: back ignored, no roster/settings mutation, Next requests activation without granting flags passed");
}
