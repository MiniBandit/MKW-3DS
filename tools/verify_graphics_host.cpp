#include <mkw/menu_text_motion.hpp>
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/bloom_math.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace mkw;
void require(bool ok,const char* message){if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main(){
 const auto* pane=uiFindPane("character_select","chara_c_down");
 const auto* select=uiFindAnimationClip("CharacterSelect4_0","Select","Select");
 const auto* free=uiFindAnimationClip("CharacterSelect4_0","Select","SelectToFree");
 require(pane&&select&&free,"Missing character selection resources");
 auto m=uiTextMotion(*pane,{{free,free->reverse?0.f:free->duration,0,select}});
 require(m.absoluteAlpha&&std::fabs(m.alpha-50.f/255.f)<1e-5f,"Zero reference alpha failed to expose the deselected underlay");
 require(std::fabs(m.sx-.9f)<1e-5f&&std::fabs(m.sy-.9f)<1e-5f,"Character control failed to shrink back to its source free size");
 MenuState s;s.enter(UiPage::Character);s.advance(30);unsigned old=s.cursor;
 UiInput right;right.right=true;s.input(right);require(s.cursor!=old,"Navigation fixture did not move");s.advance(3);
 unsigned previous=s.cursor;auto partial=s.focus.controls[old];require(partial.phase==UiFocusPhase::SelectToFree&&partial.clip,"Missing deselection animation");
 UiInput left;left.left=true;s.input(left);const auto& reversed=s.focus.controls[old];
 require(reversed.phase==UiFocusPhase::FreeToSelect&&reversed.clip,"Interrupted deselection did not reverse");
 const float expected=reversed.clip->duration*(1.f-partial.frame/partial.clip->duration);
 require(std::fabs(reversed.frame-expected)<1e-5f,"Selection reversal restarted instead of preserving progress");
 const UiPage pages[]={UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,UiPage::VSMode,UiPage::BattleMode,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::BattleCup,UiPage::BattleStage,UiPage::Ghost,UiPage::Confirm,UiPage::QuitConfirm,UiPage::Error};
 std::vector<UiQuad> q;UiRaceView race{};unsigned snapshots=0,quadCount=0;
 for(auto page:pages){MenuState state;state.enter(page);state.advance(30);unsigned count=state.count();
  for(unsigned i=0;i<count;++i){UiInput key;if(state.columns()>1)key.right=true;else key.down=true;state.input(key);
   for(unsigned frame=0;frame<16;++frame){composeMenu(state,race,q);++snapshots;
    for(const auto& a:q){require(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.w)&&std::isfinite(a.h)&&a.w>0&&a.h>0&&a.alpha>=0&&a.alpha<=1,"Focus motion produced an invalid quad");++quadCount;}
    state.advance(1);
   }
  }
 }
 for(const auto& b:CourseBlooms){
  require(std::isfinite(b.threshold)&&b.threshold>=0,"Invalid threshold");
  for(unsigned band=0;band<2;++band)for(unsigned stage=0;stage<2;++stage){float i=bloomTapIntensity(b,band,stage);require(i>=0&&i<=1&&std::isfinite(i),"Invalid blur tap intensity");}
  std::printf("B %s %08x %.9g %.9g %.9g\n",b.archive,bloomThresholdColor(b),bloomTapIntensity(b,0),bloomTapIntensity(b,1),bloomTapIntensity(b,1,1));
 }
 require(std::size(CourseBlooms)==42&&BloomFullQuads==38&&BloomReducedQuads==22,"Bloom resource/draw budget changed");
 std::printf("PASS focus_snapshots=%u quads=%u zero_alpha_and_reversal=pass bloom_courses=42\n",snapshots,quadCount);
}
