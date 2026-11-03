#include <mkw/menu_text_motion.hpp>
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace mkw;
static void require(bool ok,const char* reason){if(!ok){std::fprintf(stderr,"%s\n",reason);std::exit(1);}}
int main(){
 const auto* fade=uiFindAnimationClip("PressStart","eAFFade","Fadein");
 const auto* loop=uiFindAnimationClip("PressStart","eAFLoop","Loop");
 const auto* press=uiFindAnimationClip("PressStart","eAFPressStart","PressStart");
 require(fade&&fade->trackCount==1&&loop&&press,"Missing bound original prompt curves");
 for(unsigned f=0;f<=30;++f)for(const char* name:{"text_00","text_light"}){
  const auto* pane=uiFindPane("press_start",name);require(pane,"Missing prompt pane");
  for(unsigned mode=0;mode<3;++mode){
   auto motion=uiTextMotion(*pane,{{fade,mode==0?float(f):9.f,9.f},{loop,float(f),0.f},{mode==2?press:nullptr,float(f),0.f}});
   require(std::isfinite(motion.sx)&&std::isfinite(motion.sy)&&std::isfinite(motion.alpha),"Non-finite motion");
   std::printf("%u %s %u %.9g %.9g %.9g %.9g %.9g\n",f,name,mode,motion.sx,motion.sy,motion.tx,motion.ty,motion.alpha);
  }
 }
 MenuState s;s.enter(UiPage::Title);s.advance(30);require(uiMenuExitHold(s)==30,"Missing confirmation hold");
 MenuTransition tr;UiInput input;input.accept=true;tr.input(s,input);
 require(tr.active()&&tr.visual(s).page==UiPage::Title,"Title must remain visible during confirmation");
 tr.advance(s,4);require(tr.visual(s).page==UiPage::Title,"Title left before the original curve finished");
 std::vector<UiQuad> quads;UiRaceView race{};
 for(unsigned f=0;f<120;++f){
  composeMenu(tr.visual(s),race,quads,tr.view());
  for(const auto& q:quads)require(std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.w)&&std::isfinite(q.h)&&q.w>0&&q.h>0&&q.alpha>=0&&q.alpha<=1,"Invalid composed quad");
  tr.advance(s,1);
 }
 require(!tr.active()&&s.page==UiPage::License,"Title transition did not reach License");
}
