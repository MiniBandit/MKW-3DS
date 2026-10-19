#include <mkw/menu_bottom_ui.hpp>
#include <algorithm>

namespace mkw {
namespace {
// A quad moves with its control; the main menu's four group frames (slots
// UiFocusMainParentBase...) move with their buttons.
bool belongs(UiPage page,unsigned slot,unsigned count){
    return slot<count||(page==UiPage::MainMenu&&slot>=UiFocusMainParentBase&&slot<UiFocusMainParentBase+4);
}
// Lower-screen area for the controls: below the title, above Back.
constexpr float AreaX=8,AreaY=34,AreaW=304,AreaH=166,MaxScale=1.35f;
struct Placement {bool valid=false;float x0=0,y0=0,scale=1,ox=0,oy=0;unsigned count=0,columns=0;UiPage page=UiPage::Strap;};
// The placement is computed once per page shape from its settled layout.
// The preview is centred on the top screen, so its name plate (lower left) follows it.
void centerNamePlate(const MenuState& state,std::vector<UiQuad>& quads){
    if(state.page!=UiPage::Character&&state.page!=UiPage::Vehicle)return;
    auto plate=[](const UiQuad& q){return q.slot==~0u&&!q.bottom&&q.y>=165&&q.y+q.h<=206&&q.x<200&&q.w<200;};
    float x0=1e9f,x1=-1e9f;
    for(const auto& q:quads)if(plate(q)){x0=std::min(x0,q.x);x1=std::max(x1,q.x+q.w);}
    if(x1<=x0)return;
    const float dx=200-(x0+x1)*.5f;
    for(auto& q:quads)if(plate(q))q.x+=dx;
}
Placement& cached(const MenuState& state){
    static std::array<Placement,12> cache{};
    for(auto& entry:cache)if(entry.valid&&entry.page==state.page&&entry.count==state.count()&&entry.columns==state.columns())return entry;
    for(auto& entry:cache)if(!entry.valid)return entry;
    return cache[0];
}
Placement compute(const MenuState& state){
    // The settled layout: the page after its entrance, no transition.
    MenuState settled=state;settled.age=1000;settled.focusAge=1000;
    UiRaceView race{};
    std::vector<UiQuad> rest;
    composeMenu(settled,race,rest);
    float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;unsigned found=0;
    for(const auto& q:rest)if(belongs(state.page,q.slot,state.count())&&!q.bottom){
        x0=std::min(x0,q.x);y0=std::min(y0,q.y);x1=std::max(x1,q.x+q.w);y1=std::max(y1,q.y+q.h);++found;
    }
    Placement p;
    if(!found||x1<=x0||y1<=y0)return p;
    p.valid=true;p.page=state.page;p.count=state.count();p.columns=state.columns();
    p.x0=x0;p.y0=y0;p.scale=std::min({AreaW/(x1-x0),AreaH/(y1-y0),MaxScale});
    p.ox=AreaX+(AreaW-(x1-x0)*p.scale)*.5f;p.oy=AreaY+(AreaH-(y1-y0)*p.scale)*.5f;
    return p;
}
}

bool movesControls(UiPage page){
    switch(page){
    case UiPage::Mode:case UiPage::EngineClass:case UiPage::Character:case UiPage::Drift:
    case UiPage::Cup:case UiPage::Course:case UiPage::BattleCup:case UiPage::BattleStage:case UiPage::VSMode:case UiPage::BattleMode:
    case UiPage::License:case UiPage::MainMenu:case UiPage::Ghost:case UiPage::Confirm:case UiPage::Pause:case UiPage::QuitConfirm:
        return true;
    default:return false;
    }
}

void relocateControls(const MenuState& state,std::vector<UiQuad>& quads,MenuBottomMap& map){
    map=MenuBottomMap{};
    if(state.page==UiPage::Vehicle){
        centerNamePlate(state,quads);
        // The vehicle buttons are 3D models drawn with a depth buffer, which the lower
        // screen's target does not have. The lower screen lists the vehicles itself
        // (menu_bottom.cpp), so the top screen drops its list.
        quads.erase(std::remove_if(quads.begin(),quads.end(),[&](const UiQuad& q){return q.slot<state.count();}),quads.end());
        return;
    }
    if(!movesControls(state.page))return;
    Placement& place=cached(state);
    if(!place.valid||place.page!=state.page||place.count!=state.count()||place.columns!=state.columns())place=compute(state);
    if(!place.valid)return;
    centerNamePlate(state,quads);
    map.valid=true;map.page=state.page;
    for(auto& q:quads){
        if(!belongs(state.page,q.slot,state.count())||q.bottom)continue;
        const float x=(q.x-place.x0)*place.scale+place.ox,y=(q.y-place.y0)*place.scale+place.oy,w=q.w*place.scale,h=q.h*place.scale;
        q.x=x;q.y=y;q.w=w;q.h=h;q.bottom=true;
        if(q.slot<map.rects.size()){
            auto& r=map.rects[q.slot];
            if(!r.present){r={true,x,y,x+w,y+h};}
            else{r.x0=std::min(r.x0,x);r.y0=std::min(r.y0,y);r.x1=std::max(r.x1,x+w);r.y1=std::max(r.y1,y+h);}
        }
    }
}
}
