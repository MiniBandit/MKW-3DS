#include <mkw/menu_bottom_ui.hpp>
#include <mkw/menu_assets.hpp>
#include <algorithm>
#include <cstring>

namespace mkw {
namespace {
// The Confirm page is the page that led to it, plus the OK button: the lower screen keeps
// that page's controls (tap one to choose it and start).
MenuState baseOf(const MenuState& state){
    if(state.page!=UiPage::Confirm)return state;
    MenuState base=state;
    base.page=state.selection.mode==UiMode::GrandPrix?UiPage::Cup:state.selection.mode==UiMode::Battle?UiPage::BattleStage:UiPage::Course;
    base.cursor=base.page==UiPage::Cup?state.selection.course/4:base.page==UiPage::BattleStage?state.selection.battleStage%5:state.selection.course%4;
    return base;
}
// The cup pages' top screen: the cup's picture large on the left and, for a Grand Prix cup,
// its four course names in a column on the right.
void restyleCupPreview(const MenuState& state,std::vector<UiQuad>& quads){
    if(state.page!=UiPage::Cup&&state.page!=UiPage::BattleCup)return;
    auto key=[](const UiQuad& q){return q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"";};
    auto picture=[&](const UiQuad& q){const char* k=key(q);return q.slot==~0u&&!q.bottom&&(!std::strncmp(k,"course_movie_frame",18)||!std::strncmp(k,"cup_movie_",10)||!std::strncmp(k,"battle_cup_movie_",17));};
    auto name=[&](const UiQuad& q){return q.slot==~0u&&!q.bottom&&!std::strncmp(key(q),"text_style_",11)&&q.y>=100&&q.y<=205&&q.x>=170;};
    float px0=1e9f,py0=1e9f,nx0=1e9f,nx1=-1e9f,ny0=1e9f;
    for(const auto& q:quads){
        if(picture(q)){px0=std::min(px0,q.x);py0=std::min(py0,q.y);}
        if(name(q)){nx0=std::min(nx0,q.x);nx1=std::max(nx1,q.x+q.w);ny0=std::min(ny0,q.y);}
    }
    constexpr float k=1.35f;
    for(auto& q:quads){
        if(picture(q)){q.x=28+(q.x-px0)*k;q.y=50+(q.y-py0)*k;q.w*=k;q.h*=k;}
        else if(name(q)&&nx1>nx0){q.x=q.x-(nx0+nx1)*.5f+342;q.y=70+(q.y-ny0);}
    }
}
// A quad moves with its control; the main menu's four group frames (slots
// UiFocusMainParentBase...) move with their buttons.
bool belongs(UiPage page,unsigned slot,unsigned count){
    return slot<count||(page==UiPage::MainMenu&&slot>=UiFocusMainParentBase&&slot<UiFocusMainParentBase+4);
}
// Lower-screen area for the controls: below the title, above Back.
constexpr float AreaX=8,AreaY=36,AreaW=304,AreaH=150,MaxScale=1.9f;
// The top screen is 400 wide, the lower 320: the page's chrome is copied squeezed to fit.
constexpr float ChromeScale=.8f;
struct Box {float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;bool any=false;};
struct Placement {
    bool valid=false;float x0=0,y0=0,scale=1,ox=0,oy=0;unsigned count=0,columns=0;UiPage page=UiPage::Strap;
    // Pages that are a list of banners: each control's settled box, the common scale and the row pitch.
    std::array<Box,32> boxes{};float stackScale=1,pitch=0;
};
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
// Pages whose highlighted choice is shown large on the top screen while the
// choices themselves are on the lower one.
bool spotlights(UiPage page){
    switch(page){
    case UiPage::MainMenu:case UiPage::License:case UiPage::Course:case UiPage::BattleStage:return true;
    default:return false;
    }
}
// Top-screen copies of the highlighted control's art, enlarged and centred.
std::vector<UiQuad> spotlight(const MenuState& state,const std::vector<UiQuad>& quads){
    std::vector<UiQuad> out;
    if(!spotlights(state.page))return out;
    // The main menu's choices sit in four framed groups (Single Player, Multiplayer, WFC, Channel):
    // the whole group of the highlighted button is shown.
    auto inGroup=[&](unsigned slot){
        if(state.page==UiPage::BattleStage)return slot<state.count(); // the five slices make one picture
        if(state.page!=UiPage::MainMenu)return slot==state.cursor;
        const unsigned group=state.cursor==0?0:state.cursor<4?1:state.cursor<6?2:3;
        if(slot==UiFocusMainParentBase+group)return true;
        return group==0?slot==0:group==1?(slot>=1&&slot<=3):group==2?(slot==4||slot==5):slot==6;
    };
    float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;
    for(const auto& q:quads)if(inGroup(q.slot)&&!q.bottom){x0=std::min(x0,q.x);y0=std::min(y0,q.y);x1=std::max(x1,q.x+q.w);y1=std::max(y1,q.y+q.h);}
    if(x1<=x0||y1<=y0)return out;
    const float scale=std::min({300.f/(x1-x0),150.f/(y1-y0),3.2f});
    const float centerY=state.page==UiPage::Course||state.page==UiPage::BattleStage?98.f:112.f;
    const float ox=200-(x0+x1)*.5f*scale,oy=centerY-(y0+y1)*.5f*scale;
    for(const auto& q:quads)if(inGroup(q.slot)&&!q.bottom){
        UiQuad copy=q;copy.x=q.x*scale+ox;copy.y=q.y*scale+oy;copy.w=q.w*scale;copy.h=q.h*scale;copy.slot=~0u;
        out.push_back(copy);
    }
    return out;
}
Placement& cached(const MenuState& state){
    static std::array<Placement,12> cache{};
    for(auto& entry:cache)if(entry.valid&&entry.page==state.page&&entry.count==state.count()&&entry.columns==state.columns())return entry;
    for(auto& entry:cache)if(!entry.valid)return entry;
    return cache[0];
}
// Pages that are a plain list of wide banners: spread evenly over the lower screen.
bool stacksControls(UiPage page){
    switch(page){
    case UiPage::Course:case UiPage::BattleStage:case UiPage::Mode:case UiPage::EngineClass:case UiPage::VSMode:case UiPage::BattleMode:case UiPage::Ghost:
        return true;
    default:return false;
    }
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
    if(stacksControls(state.page)){
        float maxW=1,maxH=1;
        for(const auto& q:rest)if(q.slot<state.count()&&!q.bottom&&q.slot<p.boxes.size()){
            auto& b=p.boxes[q.slot];b.any=true;b.x0=std::min(b.x0,q.x);b.y0=std::min(b.y0,q.y);b.x1=std::max(b.x1,q.x+q.w);b.y1=std::max(b.y1,q.y+q.h);
        }
        for(const auto& b:p.boxes)if(b.any){maxW=std::max(maxW,b.x1-b.x0);maxH=std::max(maxH,b.y1-b.y0);}
        p.pitch=AreaH/float(std::max(1u,state.count()));
        p.stackScale=std::min({284.f/maxW,(p.pitch-5.f)/maxH,2.4f});
    }
    p.x0=x0;p.y0=y0;p.scale=std::min({AreaW/(x1-x0),AreaH/(y1-y0),MaxScale});
    p.ox=AreaX+(AreaW-(x1-x0)*p.scale)*.5f;p.oy=AreaY+(AreaH-(y1-y0)*p.scale)*.5f;
    return p;
}
}

bool movesControls(UiPage page){
    switch(page){
    case UiPage::Mode:case UiPage::EngineClass:case UiPage::Character:case UiPage::Vehicle:case UiPage::Drift:
    case UiPage::Cup:case UiPage::Course:case UiPage::BattleCup:case UiPage::BattleStage:case UiPage::VSMode:case UiPage::BattleMode:
    case UiPage::License:case UiPage::MainMenu:case UiPage::Ghost:case UiPage::Confirm:case UiPage::Pause:case UiPage::QuitConfirm:
    case UiPage::Title:case UiPage::Strap:case UiPage::Intro:
        return true;
    default:return false;
    }
}

void relocateControls(const MenuState& state,std::vector<UiQuad>& quads,MenuBottomMap& map){
    map=MenuBottomMap{};
    if(!movesControls(state.page))return;
    if(state.page==UiPage::Title||state.page==UiPage::Strap||state.page==UiPage::Intro){
        // The lower screen is the striped background; on the title it carries the same
        // "Press the A Button" lettering as the top, enlarged. Any tap is the button.
        map.valid=true;map.page=state.page;map.slots=1;map.rects[0]={true,0,0,320,240};
        std::vector<UiQuad> extra;
        for(unsigned i=0;i<std::size(UiSprites);++i)if(!std::strcmp(UiSprites[i].key,"background")){
            UiQuad bg{};bg.sprite=i;bg.x=0;bg.y=0;bg.w=320;bg.h=240;bg.bottom=true;extra.push_back(bg);break;
        }
        if(state.page==UiPage::Title){
            for(const auto& q:quads)if(q.sprite<std::size(UiSprites)&&!std::strncmp(UiSprites[q.sprite].key,"text_style_",11)&&q.y>150&&q.y<185){
                UiQuad copy=q;const float k=1.8f;
                copy.x=160+(q.x-200)*k;copy.y=108+(q.y-167)*k;copy.w*=k;copy.h*=k;copy.bottom=true;extra.push_back(copy);
            }
        }
        quads.insert(quads.begin()+(quads.empty()?0:1),extra.begin(),extra.end());
        return;
    }
    const MenuState mapState=baseOf(state);
    Placement& place=cached(mapState);
    if(!place.valid||place.page!=mapState.page||place.count!=mapState.count()||place.columns!=mapState.columns())place=compute(mapState);
    if(!place.valid)return;
    centerNamePlate(state,quads);
    map.valid=true;map.page=state.page;map.slots=mapState.count();
    if(state.page==UiPage::Confirm){
        // The top shows only the confirmation (checkered, with OK), as the page under it
        // is on the lower screen now.
        std::vector<UiQuad> keep;
        for(size_t i=0;i<quads.size();++i){
            const auto& q=quads[i];
            const char* key=q.sprite<std::size(UiSprites)?UiSprites[q.sprite].key:"";
            const bool confirmArt=!std::strncmp(key,"confirm",7)||q.slot==UiConfirmOkSlot||!std::strcmp(key,"back_free")||!std::strcmp(key,"button_back")||
                                  (!std::strncmp(key,"text_style_",11)&&q.y<30&&q.slot==~0u&&i>0&&i>quads.size()/2);
            if(i==0||confirmArt||belongs(mapState.page,q.slot,mapState.count()))keep.push_back(q);
        }
        quads.swap(keep);
    }
    const std::vector<UiQuad> large=spotlight(mapState.page==state.page?state:state,quads);
    // Copies of the page's chrome for the lower screen, drawn first: the striped
    // background, the title bar and the Back button with their authored art.
    std::vector<UiQuad> chrome;
    for(const auto& q:quads){
        if(q.slot!=~0u||q.bottom||q.sprite>=std::size(UiSprites))continue;
        const char* key=UiSprites[q.sprite].key;
        const bool background=!std::strcmp(key,"background"),bar=!std::strcmp(key,"header")||!std::strcmp(key,"footer")||!std::strcmp(key,"footer_help"),
                   back=!std::strcmp(key,"back_free")||!std::strcmp(key,"button_back"),
                   title=!std::strncmp(key,"text_style_",11)&&q.y<30;
        if(!background&&!bar&&!back&&!title)continue;
        UiQuad copy=q;copy.x*=ChromeScale;copy.w*=ChromeScale;copy.bottom=true;
        if(!std::strcmp(key,"back_free"))map.back={true,copy.x,copy.y,copy.x+copy.w,copy.y+copy.h};
        chrome.push_back(copy);
    }
    // The background first, then the rest in their order.
    std::stable_partition(chrome.begin(),chrome.end(),[](const UiQuad& q){return q.sprite<std::size(UiSprites)&&!std::strcmp(UiSprites[q.sprite].key,"background");});
    // After quad 0: the menu loop draws that one as the top screen's backdrop.
    quads.insert(quads.begin()+(quads.empty()?0:1),chrome.begin(),chrome.end());
    const bool stacked=stacksControls(mapState.page);
    restyleCupPreview(state,quads);
    // The cup heading of the course and stage pages: smaller, at the top left of the top screen.
    {
        float x0=1e9f,y0=1e9f;
        for(const auto& q:quads)if(q.slot==UiHeadingSlot&&!q.bottom){x0=std::min(x0,q.x);y0=std::min(y0,q.y);}
        for(auto& q:quads)if(q.slot==UiHeadingSlot&&!q.bottom){q.x=24+(q.x-x0)*.55f;q.y=38+(q.y-y0)*.55f;q.w*=.55f;q.h*=.55f;q.slot=~0u;}
    }
    for(auto& q:quads){
        if(!belongs(mapState.page,q.slot,mapState.count())||q.bottom)continue;
        float x=(q.x-place.x0)*place.scale+place.ox,y=(q.y-place.y0)*place.scale+place.oy,w=q.w*place.scale,h=q.h*place.scale;
        if(stacked&&q.slot<place.boxes.size()&&place.boxes[q.slot].any){
            // Relative to the control's settled centre, so its entrance and focus motion still play.
            const auto& b=place.boxes[q.slot];
            const float cx=(b.x0+b.x1)*.5f,cy=(b.y0+b.y1)*.5f,center=AreaY+place.pitch*(float(q.slot)+.5f);
            x=160+(q.x-cx)*place.stackScale;y=center+(q.y-cy)*place.stackScale;w=q.w*place.stackScale;h=q.h*place.stackScale;
        }
        q.x=x;q.y=y;q.w=w;q.h=h;q.bottom=true;
        if(q.slot<map.rects.size()){
            auto& r=map.rects[q.slot];
            if(!r.present){r={true,x,y,x+w,y+h};}
            else{r.x0=std::min(r.x0,x);r.y0=std::min(r.y0,y);r.x1=std::max(r.x1,x+w);r.y1=std::max(r.y1,y+h);}
        }
    }
    quads.insert(quads.end(),large.begin(),large.end());
}
}
