#include <mkw/menu_touch.hpp>
#include <algorithm>
#include <cmath>

namespace mkw {
namespace {
struct Box {float l=1e9f,t=1e9f,r=-1e9f,b=-1e9f;
 void add(const UiQuad& q){
  if(q.affine){for(const auto& p:q.positions){l=std::min(l,p[0]);r=std::max(r,p[0]);t=std::min(t,p[1]);b=std::max(b,p[1]);}return;}
  l=std::min(l,q.x);t=std::min(t,q.y);r=std::max(r,q.x+q.w);b=std::max(b,q.y+q.h);
 }
 bool contains(float x,float y)const{return x>=l&&x<r&&y>=t&&y<b;}
 // Distance from the point to the box (0 inside; huge for an empty box).
 float distance(float x,float y)const{
  if(r<l)return 1e9f;
  const float dx=std::max({l-x,0.f,x-r}),dy=std::max({t-y,0.f,y-b});return std::sqrt(dx*dx+dy*dy);
 }
};
// A tap in the gap between two buttons, or just outside one, takes the nearest.
constexpr float NearMiss=8.f;
// Pages that only wait for A: a tap anywhere on the lower LCD. Confirm's OK
// and Back are on the upper LCD; the lower one shows the dimmed course list.
bool tapAnywhere(UiPage page){
 return page==UiPage::Strap||page==UiPage::Intro||page==UiPage::Title||page==UiPage::Unlock||
        page==UiPage::DriftHelp||page==UiPage::Confirm;
}
// Main Menu tiles hold one to three buttons side by side (UiMainMenuControls).
unsigned mainMenuSlot(unsigned tile,const Box& box,float x){
 static constexpr unsigned first[]={0,1,4,6},count[]={1,3,2,1};
 if(tile>=4)return ~0u;
 const unsigned n=count[tile];
 const float w=box.r-box.l;
 const unsigned k=w>0?std::min(unsigned(std::max(0.f,(x-box.l)/w*float(n))),n-1):0;
 return first[tile]+k;
}
}

UiTouchTarget uiTouchTarget(const MenuState& state,const std::vector<UiQuad>& bottom,float x,float y){
 using K=UiTouchTarget::Kind;
 if(tapAnywhere(state.page))return {K::Press};
 // Pause pages and races draw nothing to touch on the lower LCD.
 if(state.page==UiPage::Pause||state.page==UiPage::QuitConfirm||state.simulates())return {};
 constexpr unsigned Groups=32;
 Box back,choice[Groups],auxiliary[Groups];
 for(const auto& q:bottom){
  // Modal underlays and outgoing pages keep their own page tag.
  if(q.menu.page!=state.page||q.alpha<=0.01f)continue;
  const unsigned g=q.menu.group;
  if(q.menu.role==UiMenuRole::Back)back.add(q);
  else if(q.menu.role==UiMenuRole::Choice&&g<Groups)choice[g].add(q);
  else if(q.menu.role==UiMenuRole::Auxiliary&&g<Groups)auxiliary[g].add(q);
 }
 if(back.contains(x,y))return {K::Back};
 for(unsigned g=0;g<Groups;++g){
  if(!auxiliary[g].contains(x,y))continue;
  const unsigned corner=state.auxiliarySlot();
  if(corner!=~0u)return {K::Choice,corner};
  // Cup captions belong to their cup; the rules' OK is the row after the values.
  if(state.page==UiPage::Cup||state.page==UiPage::BattleCup||state.rulePage())return {K::Choice,g};
  if(state.page==UiPage::EngineClass)return {K::SwitchType};
  return {K::Press};
 }
 unsigned nearest=~0u;float best=NearMiss;
 for(unsigned g=0;g<Groups;++g){
  const float d=choice[g].distance(x,y);
  if(d>=best)continue;
  const unsigned slot=state.page==UiPage::MainMenu?mainMenuSlot(g,choice[g],x):g;
  if(slot<state.count()&&state.choiceVisible(slot)){nearest=slot;best=d;if(d==0)break;}
 }
 if(nearest!=~0u)return {K::Choice,nearest};
 return {};
}

UiInput MenuTouch::update(const MenuState& state,const std::vector<UiQuad>& bottom,bool touching,float x,float y){
 using K=UiTouchTarget::Kind;
 UiInput input{};
 if(held_){held_=touching;return input;}
 if(!touching){
  if(!active_)return input;
  active_=false;
  const auto target=uiTouchTarget(state,bottom,x_,y_);
  if(moved_||!(target==pressed_))return input;
  switch(target.kind){
  case K::Back:input.back=true;break;
  case K::Press:input.accept=true;break;
  case K::SwitchType:input.switchType=true;break;
  case K::Choice:
   // The tap also selects the button, in case its highlight has not landed.
   input.point=target.slot;
   // A rule's value row has no A action: a tap steps it, as Right does.
   if(state.rulePage()&&target.slot<state.ruleOkRow())input.right=true;
   else input.accept=true;
   break;
  default:break;
  }
  return input;
 }
 x_=x;y_=y;
 const auto target=uiTouchTarget(state,bottom,x,y);
 if(!active_){
  active_=true;moved_=false;pressed_=target;
 }else if(!(target==pressed_))moved_=true;
 if(target.kind==K::Choice&&target.slot!=state.cursor)input.point=target.slot;
 return input;
}
}
