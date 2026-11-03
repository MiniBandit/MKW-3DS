#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/menu_model_motion.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
using namespace mkw;
static void check(const std::vector<UiQuad>& quads,bool lower){
 for(const auto& q:quads){
  assert(q.bottom==lower&&std::isfinite(q.x)&&std::isfinite(q.y)&&q.w>0&&q.h>0);
  if(!q.affine){assert(q.x>=0&&q.y>=0&&q.x+q.w<=(lower?320:400)+.01f&&q.y+q.h<=240.01f);}
  assert(q.menu.role!=UiMenuRole::Content);
  if(lower)assert(q.menu.role!=UiMenuRole::Statistics&&q.menu.role!=UiMenuRole::Name&&q.menu.role!=UiMenuRole::Preview);
  else assert(q.menu.role!=UiMenuRole::Back);
 }
}
int main(int argc,char** argv){
 const UiPage pages[]={UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::Confirm,UiPage::Error,UiPage::VSMode,UiPage::BattleMode,UiPage::BattleCup,UiPage::BattleStage,UiPage::Ghost,UiPage::DriftHelp,UiPage::VSRules,UiPage::TeamConfirm,UiPage::Settings,UiPage::LicenseSettings,UiPage::EraseLicense,UiPage::RecordsGeneral,UiPage::RecordsFavorites,UiPage::RecordsOther};
 std::vector<UiQuad> source,top,bottom;unsigned cases=0;FILE* dump=argc>1?std::fopen(argv[1],"w"):nullptr;
 for(auto page:pages)for(unsigned mode=0;mode<4;++mode){
  MenuState s;s.selection.mode=UiMode(mode);s.selection.mixedEngineTypes=true;s.enter(page);s.advance(120);
  for(unsigned cursor=0;cursor<s.count();++cursor){
   s.cursor=cursor;composeMenu(s,{},source);layoutMenuScreens(s,source,top,bottom);check(top,false);check(bottom,true);assert(!bottom.empty());++cases;
   if(dump&&mode==0&&cursor==0){
    for(const auto* list:{&top,&bottom})for(const auto& q:*list)std::fprintf(dump,"%u %u %u %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",unsigned(page),q.bottom,unsigned(q.menu.role),(q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"license_portrait"),q.x,q.y,q.w,q.h,q.u0,q.v0,q.u1,q.v1,q.alpha);
   }
  }
 }
 // Six-machine restrictions retain vertical navigation; the normal twelve
 // choices use both halves. Stats and name stay exclusively on the top.
 for(unsigned engine=0;engine<3;++engine){
  MenuState s;s.selection.mode=UiMode::GrandPrix;s.selection.engineClass=UiEngineClass(engine);s.enter(UiPage::Vehicle);s.advance(120);
  composeMenu(s,{},source);layoutMenuScreens(s,source,top,bottom);
  std::set<unsigned> choices,statistics;
  for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Choice){choices.insert(q.menu.group);if(q.vehicleIcon<12){const float cx=q.x+q.w/2;assert(s.columns()==1?std::fabs(cx-160)<12:(q.menu.group%2?cx>160:cx<160));}}
  for(const auto& q:top)if(q.menu.role==UiMenuRole::Statistics)statistics.insert(q.menu.group);
  assert(choices.size()==s.count()&&statistics.size()==7);
  for(const auto& q:top)if(q.menu.role==UiMenuRole::Statistics&&*UiSprites[q.sprite].text)assert(std::abs(q.x-22)<.01f);
 }
 // A confirmation composes a course-page underlay. Its preview stays on
 // top and its four choices stay below, independent of the modal cursor.
 MenuState confirm;confirm.selection.mode=UiMode::VS;confirm.enter(UiPage::Confirm);confirm.advance(120);
 composeMenu(confirm,{},source);layoutMenuScreens(confirm,source,top,bottom);
 bool coursePreview=false,modal=false;std::set<unsigned> underlayChoices;
 for(const auto& q:top){coursePreview|=q.menu.page==UiPage::Course&&q.menu.role==UiMenuRole::Preview;modal|=q.menu.page==UiPage::Confirm&&q.menu.role==UiMenuRole::Choice;}
 for(const auto& q:bottom){if(q.menu.page==UiPage::Course&&q.menu.role==UiMenuRole::Choice)underlayChoices.insert(q.menu.group);assert(q.menu.page!=UiPage::Confirm);}
 assert(coursePreview&&modal&&underlayChoices.size()==4);
 confirm.selection.mode=UiMode::GrandPrix;
 composeMenu(confirm,{},source);layoutMenuScreens(confirm,source,top,bottom);
 for(const auto& q:top)assert(q.menu.role!=UiMenuRole::PreviewLabel);
 // Reflowed cup/course pages must animate both exit and entrance in both directions.
 for(bool back:{false,true}){
  MenuState state;state.selection.mode=UiMode::VS;state.enter(back?UiPage::Course:UiPage::Cup);state.advance(120);
  MenuTransition move;UiInput key{};key.back=back;key.accept=!back;move.input(state,key);
  bool exitSlide=false,enterSlide=false;
  for(unsigned frame=0;frame<180&&move.active();++frame){
   const auto view=move.view();composeMenu(move.visual(state),{},source,view);
   for(const auto& q:source)if(q.menu.role==UiMenuRole::Choice&&std::abs(q.screenSlide)>.01f&&std::abs(q.screenSlide)<.99f){
    if(view.entering)enterSlide=true;else exitSlide=true;
   }
   move.advance(state,1);
  }
  assert(exitSlide&&enterSlide);
 }
 // Check actual forward/backward clocks, including the page handoff.
 for(bool back:{false,true}){
  MenuState model;model.enter(back?UiPage::Drift:UiPage::Vehicle);model.advance(120);
  MenuTransition move;UiInput key{};key.back=back;key.accept=!back;move.input(model,key);
  float previous=back?200:270;
  for(unsigned frame=0;frame<180&&move.active();++frame){
   const auto pose=uiTopMenuModelPlacement(move.visual(model).page,move.view());
   assert(std::abs(pose.x-previous)<=7.01f);assert(std::abs(pose.y-86)<.01f);
   assert(back?pose.x>=previous-.01f:pose.x<=previous+.01f);
   previous=pose.x;move.advance(model,1);
  }
  assert(std::abs(uiTopMenuModelPlacement(model.page,{}).x-previous)<=7.01f);
 }
 for(bool back:{false,true}){
  MenuState model;model.enter(back?UiPage::Cup:UiPage::Drift);model.advance(120);
  MenuTransition move;UiInput key{};key.back=back;key.accept=!back;move.input(model,key);
  bool sawMoving=false,sawOff=false;
  for(unsigned frame=0;frame<180&&move.active();++frame){
   const auto& visual=move.visual(model);const auto pose=uiTopMenuModelPlacement(visual.page,move.view());
   if(visual.page==UiPage::Drift){sawMoving|=pose.x<190&&pose.x>-300;sawOff|=pose.x<=-300;}
   move.advance(model,1);
  }
  assert(sawMoving&&sawOff);
 }
 MenuState title;title.licenseOccupied[2]=true;title.selection.license=2;title.enter(UiPage::Title);title.advance(120);
 composeMenu(title,{},source);layoutMenuScreens(title,source,top,bottom);
 bool lastCard=false;for(const auto& q:bottom){lastCard|=q.menu.role==UiMenuRole::TitleCard&&q.menu.group==2;assert(std::string((q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"license_portrait"))!="title_scene");}
 assert(lastCard);
 {
  MenuState edit;edit.selection.license=2;edit.enter(UiPage::LicenseSettings);assert(edit.count()==2);
  UiInput accept{};accept.accept=true;edit.input(accept);
  assert(edit.page==UiPage::LicenseSettings&&edit.editLicenseRequest==2&&edit.eraseLicenseRequest==-1);
  edit.editLicenseRequest=-1;UiInput down{};down.down=true;edit.input(down);edit.input(accept);
  assert(edit.page==UiPage::EraseLicense&&edit.eraseLicenseRequest==-1);
  UiInput back{};back.back=true;edit.input(back);
  assert(edit.page==UiPage::LicenseSettings&&edit.cursor==1&&edit.eraseLicenseRequest==-1);
 }
 // Saved names use individual source glyphs on both the license page and
 // its flying title card; they must not fall back to the baked License N.
 for(auto page:{UiPage::License,UiPage::Title,UiPage::LicenseSettings}){
  MenuState named;named.licenseOccupied[0]=true;named.selection.license=0;
  named.licenseNames[0]={'M','a','r','i','o',' ',0x00e9,'1','2','3'};named.enter(page);named.advance(120);
  composeMenu(named,{},source);layoutMenuScreens(named,source,top,bottom);
  unsigned glyphs=0;
  for(const auto& q:source)if(std::string((q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"license_portrait")).starts_with("name_glyph_"))++glyphs;
  assert(glyphs==10);if(page!=UiPage::Title){check(top,false);check(bottom,true);}
 }
 // Lower backdrops remain stripes throughout title->license, including
 // the frame where the cross-screen card hands off to the selected preview.
 MenuTransition titleMove;UiInput start{};start.accept=true;titleMove.input(title,start);
 for(unsigned frame=0;frame<180&&titleMove.active();++frame){
  composeMenu(titleMove.visual(title),{},source,titleMove.view());layoutMenuScreens(titleMove.visual(title),source,top,bottom);
  bool stripes=false;
  for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Background){assert(std::string((q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"license_portrait"))=="background");stripes=true;}
  assert(stripes);titleMove.advance(title,1);
 }
 for(auto page:{UiPage::License,UiPage::MainMenu}){
  title.enter(page);title.advance(120);composeMenu(title,{},source);layoutMenuScreens(title,source,top,bottom);
  for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Background)assert(std::string((q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"license_portrait"))=="background");
 }

 // Back must carry the highlighted card to Title, retaining its identity.
 title.enter(UiPage::License);title.cursor=2;title.advance(120);MenuTransition returning;UiInput back{};back.back=true;returning.input(title,back);
 float previousTransfer=1;
 for(unsigned frame=0;frame<180&&returning.active();++frame){
  composeMenu(returning.visual(title),{},source,returning.view());bool card=false;
  for(const auto& q:source)if(q.menu.role==UiMenuRole::TitleCard){assert(q.menu.group==2);assert(q.titleTransfer<=previousTransfer+.001f);previousTransfer=q.titleTransfer;card=true;break;}
  assert(card);returning.advance(title,1);
 }
 assert(title.page==UiPage::Title&&title.titleLicense==2&&previousTransfer<.001f);
 // Selection pulses must bind the actual icon, never the baked placeholder.
 for(auto page:{UiPage::Character,UiPage::Vehicle,UiPage::Cup}){
  MenuState focus;focus.enter(page);focus.advance(120);UiInput right{};right.down=true;focus.input(right);focus.advance(4);composeMenu(focus,{},source);
  bool light=false;for(const auto& q:source){const std::string key=(q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"license_portrait");assert(key!="character_selectin_chara_light_02"&&key!="vehicle_selectin_chara_light_02"&&key!="cup_selectin_icon_light_02");
   if(q.menu.role==UiMenuRole::Choice&&q.menu.group==focus.cursor&&(q.fontRemap||q.vehicleColor0==0xffffff00u))light=true;
  }assert(light);
 }
 // Transition samples retain their per-page tags and never overflow a
 // viewport after the control's animated displacement is applied.
 MenuState s;s.selection.mode=UiMode::VS;s.enter(UiPage::Vehicle);s.advance(120);MenuTransition transition;UiInput accept{};accept.accept=true;transition.input(s,accept);
 for(unsigned frame=0;frame<140&&transition.active();++frame){composeMenu(transition.visual(s),{},source,transition.view());layoutMenuScreens(transition.visual(s),source,top,bottom);check(top,false);check(bottom,true);transition.advance(s,1);}
 for(auto page:{UiPage::Strap,UiPage::Intro,UiPage::Pause,UiPage::QuitConfirm,UiPage::Race,UiPage::Results}){
  MenuState u;u.enter(page);u.advance(120);composeMenu(u,{},source);layoutMenuScreens(u,source,top,bottom);assert(bottom.empty()&&source.size()==top.size());
  for(unsigned i=0;i<source.size();++i)assert(source[i].x==top[i].x&&source[i].y==top[i].y&&source[i].w==top[i].w&&source[i].h==top[i].h);
 }
 // Reflowed slide controls traverse the LCD, retain visible clipped pieces
 // at intermediate frames, and are fully outside at the phase boundary.
 MenuState sliding;sliding.enter(UiPage::Mode);sliding.advance(120);
 for(bool back:{false,true}){
  UiTransitionView motion;motion.active=true;motion.back=back;motion.maxDelay=uiMenuMaxDelay(sliding);
  bool sawPartial=false;
  for(unsigned frame=0;frame<=unsigned(motion.maxDelay+10);++frame){
   motion.elapsed=float(frame);composeMenu(sliding,{},source,motion);layoutMenuScreens(sliding,source,top,bottom);
   check(top,false);check(bottom,true);
   for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Choice){sawPartial|=q.u0>0||q.u1<.999f;
    if(frame==unsigned(motion.maxDelay+10))assert(false&&"slide remains on LCD at phase end");}
  }
  assert(sawPartial);
 }
 // Dynamic identities retain their slot through each card layout. Empty
 // slots emit no portrait, and title travel does not borrow another identity.
 for(unsigned selected=0;selected<4;++selected){
  MenuState identity;identity.licenseOccupied={true,true,true,true};identity.selection.license=selected;
  identity.enter(UiPage::License);identity.cursor=selected;identity.advance(120);
  composeMenu(identity,{},source);std::set<unsigned> slots;
  for(const auto& q:source)if(q.licensePortrait<4){assert(q.sprite==~0u);slots.insert(q.licensePortrait);}
  assert(slots.size()==4);
  identity.licenseOccupied[(selected+1)%4]=false;composeMenu(identity,{},source);
  for(const auto& q:source)assert(q.licensePortrait!=(selected+1)%4);
  identity.enter(UiPage::Title);identity.advance(120);composeMenu(identity,{},source);layoutMenuScreens(identity,source,top,bottom);
  unsigned portraits=0;for(const auto& q:bottom)if(q.licensePortrait<4){assert(q.licensePortrait==selected);++portraits;}
  assert(portraits>0);
 }
 if(dump)std::fclose(dump);std::printf("PASS %u stable page/cursor/mode layouts, vehicle navigation, modal underlay, transitions, unchanged pages\n",cases);
}
