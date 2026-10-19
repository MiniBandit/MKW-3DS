#include <mkw/menu_bottom.hpp>
#include <mkw/mii_store.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
constexpr std::uint32_t TileColor=0x26365aff,TileSelected=0x3b5aa6ff,TileDisabled=0x23252cff,BackColor=0x5a2530ff,OkColor=0x2d6a3aff;
constexpr const char* CupNames[8]={"MUSHROOM","FLOWER","STAR","SPECIAL","SHELL","BANANA","LEAF","LIGHTNING"};
constexpr const char* MainMenuNames[7]={"SINGLE PLAYER","2 PLAYERS","3 PLAYERS","4 PLAYERS","WFC 1 PLAYER","WFC 2 PLAYERS","CHANNEL"};

void copy(char (&destination)[32],const char* source){std::snprintf(destination,sizeof destination,"%s",source?source:"");}

struct Builder {
    MenuBottomLayout& layout;
    const MenuState& state;
    BottomWidget* label(float x,float y,const char* text,float size=1,std::uint32_t color=0xffffffffu){
        auto* w=layout.add();if(!w)return nullptr;
        w->kind=BottomWidget::Label;w->x=x;w->y=y;copy(w->text,text);w->textSize=size;w->color=color;return w;
    }
    BottomWidget* tile(float x,float y,float w,float h,const char* text,int slot,bool enabled=true,float size=1,const char* sub=""){
        auto* t=layout.add();if(!t)return nullptr;
        t->kind=BottomWidget::Tile;t->x=x;t->y=y;t->w=w;t->h=h;copy(t->text,text);copy(t->sub,sub);t->textSize=size;
        t->slot=slot;t->enabled=enabled;t->selected=slot>=0&&unsigned(slot)==state.cursor;
        t->color=!enabled?TileDisabled:t->selected?TileSelected:TileColor;
        return t;
    }
    void back(){
        auto* b=layout.add();if(!b)return;
        b->kind=BottomWidget::Button;b->x=8;b->y=206;b->w=72;b->h=28;copy(b->text,"BACK");b->textSize=1.5f;b->back=true;b->color=BackColor;
    }
    void title(const char* text,const char* sub=""){
        label(8,6,text,2);
        if(*sub)label(8,24,sub,1,0x9fb0d0ffu);
    }
    // A column of full-width rows.
    void rows(unsigned count,float x,float y,float w,float h,float gap,const char* const* names,const bool* enabled=nullptr,float size=2){
        for(unsigned i=0;i<count;++i)tile(x,y+i*(h+gap),w,h,names[i],int(i),!enabled||enabled[i],size);
    }
};

const char* driverName(const UiSelection& s){return UiCharacters[std::min<unsigned>(s.driver,UiCharacters.size()-1)].name;}
const char* vehicleName(const UiSelection& s){
    const unsigned weight=std::min<unsigned>(UiCharacters[std::min<unsigned>(s.driver,UiCharacters.size()-1)].weight,2u);
    return UiVehicleNames[weight][std::min(s.vehicle,11u)];
}
}

BottomWidget* MenuBottomLayout::add(){
    if(count>=Capacity)return nullptr;
    widgets[count]=BottomWidget{}; // never keep what the previous page left here
    return &widgets[count++];
}

void composeMenuBottom(const MenuState& state,MenuBottomLayout& layout,const MiiStore* miis,CourseNameFn courseName){
    layout.count=0;layout.page=state.page;layout.cursor=state.cursor;layout.message=state.unavailable;
    Builder b{layout,state};
    const auto& s=state.selection;
    char text[64];
    auto course=[&](unsigned index)->const char*{
        static char fallback[16];
        if(const char* name=courseName?courseName(index):nullptr)return name;
        std::snprintf(fallback,sizeof fallback,"COURSE %u",index+1);return fallback;
    };
    switch(state.page){
    case UiPage::Strap:case UiPage::Intro:case UiPage::Title:
        b.tile(40,90,240,60,"TOUCH TO START",0,true,2);
        break;
    case UiPage::License:{
        b.title("SELECT A LICENSE");
        static const char* names[4]={"LICENSE 1","LICENSE 2","LICENSE 3","LICENSE 4"};
        b.rows(4,20,40,280,32,6,names);b.back();break;
    }
    case UiPage::MainMenu:{
        b.title("MAIN MENU");
        b.tile(20,40,280,44,MainMenuNames[0],0,true,2);
        for(unsigned i=1;i<=3;++i)b.tile(20+(i-1)*95.f,92,90,34,MainMenuNames[i],int(i),false,1);
        for(unsigned i=4;i<=5;++i)b.tile(20+(i-4)*145.f,134,135,34,MainMenuNames[i],int(i),false,1);
        b.tile(20,176,280,26,MainMenuNames[6],6,false,1);
        b.back();break;
    }
    case UiPage::Mode:{
        b.title("SELECT A MODE");
        static const char* names[4]={"GRAND PRIX","TIME TRIALS","VS RACE","BATTLE"};
        bool enabled[4];for(unsigned i=0;i<4;++i)enabled[i]=MenuState::raceMode(UiMode(i));
        b.rows(4,20,34,280,32,6,names,enabled);b.back();break;
    }
    case UiPage::EngineClass:{
        b.title("ENGINE CLASS");
        static const char* names[4]={"50CC","100CC","150CC","MIRROR"};
        bool enabled[4];for(unsigned i=0;i<4;++i)enabled[i]=MenuState::raceEngineClass(UiEngineClass(i));
        b.rows(4,20,34,280,32,6,names,enabled);b.back();break;
    }
    case UiPage::VSMode:{
        b.title("VS RACE");
        static const char* names[2]={"VS RACE","TEAM VS"};
        const bool enabled[2]={true,false};
        b.rows(2,20,50,280,60,12,names,enabled);b.back();break;
    }
    case UiPage::BattleMode:{
        b.title("BATTLE");
        static const char* names[2]={"BALLOON BATTLE","COIN RUNNERS"};
        const bool enabled[2]={true,false};
        b.rows(2,20,50,280,60,12,names,enabled);b.back();break;
    }
    case UiPage::Character:{
        b.title("SELECT A CHARACTER");
        for(unsigned slot=0;slot<state.count();++slot){
            const unsigned driver=state.characterDriver(slot);
            const unsigned row=slot/4,column=slot>=24?slot-22:slot%4;
            const char* name=UiCharacters[std::min<unsigned>(driver,UiCharacters.size()-1)].name;
            const MiiEntry* mii=slot>=24&&miis?miis->at(slot-24):nullptr;
            if(mii)name=mii->name;
            auto* t=b.tile(4.f+column*79.f,30.f+row*25.f,76,22,name,int(slot),MenuState::raceDriver(driver));
            if(t&&mii&&*mii->portrait)std::snprintf(t->icon,sizeof t->icon,"%s",mii->portrait);
        }
        b.back();break;
    }
    case UiPage::Vehicle:{
        std::snprintf(text,sizeof text,"%s",driverName(s));
        b.title("SELECT A VEHICLE",text);
        const unsigned count=state.count(),columns=state.columns();
        for(unsigned slot=0;slot<count;++slot){
            const unsigned choice=state.vehicleChoice(slot);
            const unsigned weight=std::min<unsigned>(UiCharacters[std::min<unsigned>(s.driver,UiCharacters.size()-1)].weight,2u);
            const char* name=UiVehicleNames[weight][std::min(choice,11u)];
            if(columns==2)b.tile(6.f+(slot%2)*156.f,36.f+(slot/2)*27.f,152,24,name,int(slot),MenuState::raceVehicle(choice));
            else b.tile(60,36.f+slot*27.f,200,24,name,int(slot),MenuState::raceVehicle(choice));
        }
        b.back();break;
    }
    case UiPage::Drift:{
        std::snprintf(text,sizeof text,"%s / %s",driverName(s),vehicleName(s));
        b.title("DRIFT TYPE",text);
        static const char* names[2]={"AUTOMATIC","MANUAL"};
        b.rows(2,20,50,280,60,12,names);b.back();break;
    }
    case UiPage::Cup:{
        b.title("SELECT A CUP");
        for(unsigned i=0;i<8;++i){
            const char* name=CupNames[i];
            b.tile(3.f+(i%4)*80.f,50.f+(i/4)*62.f,74,56,name,int(i),true,std::strlen(name)>7?1.f:1.5f);
        }
        b.back();break;
    }
    case UiPage::Course:{
        const unsigned cup=std::min(s.course/4,7u);
        b.title("SELECT A COURSE",CupNames[cup]);
        for(unsigned i=0;i<4;++i){
            const char* name=course(cup*4+i);
            b.tile(10,34.f+i*40.f,300,34,name,int(i),true,std::strlen(name)*12<=280?2.f:1.5f);
        }
        b.back();break;
    }
    case UiPage::BattleCup:{
        b.title("BATTLE STAGES");
        static const char* names[2]={"STAGES 1-5","STAGES 6-10"};
        b.rows(2,20,50,280,60,12,names);b.back();break;
    }
    case UiPage::BattleStage:{
        b.title("SELECT A STAGE");
        const unsigned first=std::min(s.battleStage/5,1u)*5;
        for(unsigned i=0;i<5;++i)b.tile(10,34.f+i*33.f,300,28,UiBattleStageNames[first+i],int(i),true,1.5f);
        b.back();break;
    }
    case UiPage::Ghost:{
        b.title("TIME TRIAL");
        static const char* names[3]={"RACE A GHOST","WATCH REPLAY","START TIME TRIAL"};
        const bool enabled[3]={false,false,true};
        b.rows(3,20,40,280,44,8,names,enabled);b.back();break;
    }
    case UiPage::Confirm:{
        b.title("READY?");
        std::snprintf(text,sizeof text,"%s / %s",driverName(s),vehicleName(s));
        b.label(8,40,text,1.5f);
        const unsigned courseIndex=s.mode==UiMode::Battle?s.battleStage:s.course;
        std::snprintf(text,sizeof text,"%s",s.mode==UiMode::Battle?UiBattleStageNames[std::min(courseIndex,9u)]:course(std::min(courseIndex,31u)));
        b.label(8,62,text,1.5f);
        std::snprintf(text,sizeof text,"%s",s.mode==UiMode::GrandPrix?"GRAND PRIX":s.mode==UiMode::TimeTrials?"TIME TRIALS":s.mode==UiMode::VS?"VS RACE":"BATTLE");
        b.label(8,84,text,1,0x9fb0d0ffu);
        if(auto* ok=b.tile(70,120,180,56,"START",0,true,2.5f))ok->color=OkColor;
        b.back();break;
    }
    case UiPage::Error:
        b.title("SOMETHING WENT WRONG");
        b.tile(70,100,180,56,"OK",0,true,2);b.back();break;
    case UiPage::QuitConfirm:{
        b.title("QUIT THE RACE?");
        static const char* names[2]={"YES, QUIT","NO"};
        b.rows(2,20,50,280,60,12,names);b.back();break;
    }
    case UiPage::Pause:{
        b.title("PAUSED");
        static const char* names[3]={"RESUME","RESTART","QUIT"};
        b.rows(3,20,40,280,44,8,names);break;
    }
    case UiPage::Results:{
        b.title("RESULTS");
        static const char* names[2]={"NEXT","QUIT"};
        b.rows(state.count(),20,60,280,56,12,names);break;
    }
    default:break;
    }
    if(state.unavailable)b.label(86,192,"NOT AVAILABLE YET",1.5f,0xff6060ffu);
}

int hitMenuBottom(const MenuBottomLayout& layout,float x,float y){
    for(unsigned i=layout.count;i-->0;){
        const auto& w=layout.widgets[i];
        if(w.kind==BottomWidget::Label||(w.slot<0&&!w.back))continue;
        if(x>=w.x&&x<w.x+w.w&&y>=w.y&&y<w.y+w.h)return int(i);
    }
    return -1;
}
}
