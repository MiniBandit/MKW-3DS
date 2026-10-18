// Race bottom screen: see include/mkw/race_bottom_screen.hpp.
#include <mkw/race_bottom_screen.hpp>
#include <mkw/race_map_assets.hpp>
#include <mkw/race_grid.hpp>
#include <mkw/race_items.hpp>
#include <mkw/renderer.hpp>
#include <game/kart/KartAction.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartState.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
namespace mkw {
namespace {
constexpr float ScreenW=320.f,ScreenH=240.f,Pi=3.14159265358979f;
// The standings column (left).
constexpr float Column=82.f;
// Standings rows: the Wii result board's bar (78x19, baked), 12 rows that
// fill the screen's height (1 px apart); in each, the position, then the
// face and the held item at 17 px.
constexpr float RowTop=1.f,RowPitch=20.f,RowX=2.f,RowH=19.f;
constexpr float OrdinalX=3.f,FaceX=32.f,ItemX=56.f,RowIcon=17.f;
// The map. Retail shows Map2DRenderer's 220x220 capture on CtrlRace2DMap's
// race_null pane (220x220 layout units; map_1 scales the control 1.3 on a
// 608x456 screen). The 240 px lower screen holds it at one texel per pixel and
// at no larger whole scale, so one layout unit of the map's own layouts (the
// pane, the icons, the start line, the show/hide slide) is one pixel here and
// every size keeps its retail proportion to the map. It sits below the timer's
// 20 px row, its right edge on the timer's.
constexpr float MapSize=float(RaceMapCapture),MapX=96.f,MapY=20.f;
constexpr float MapCX=MapX+MapSize*.5f,MapCY=MapY+MapSize*.5f;
// The race_null pane's alpha (CtrlRace2DMap_load PAL 0x807EA27C: lbl_1_data_20C48
// 0x808D3818 = 100). Over the lower screen's black, as over the race view.
constexpr uint32_t MapAlpha=100;
// CtrlRace2DMapCharacter (common_w039_map_chara_icon.brlyt): the face chara_0_0
// and its black shadows chara_shadow_0_0/1 (x +1.5/-1.5) are 32x32 panes whose
// scale calfTransform (0x807EB3CC) sets each frame: the local racer 0.8 / 0.85
// (lbl_1_data_20C58, DAT_808D382C), the others 0.6 / 0.65 (DAT_808D3830/34),
// which CtrlRace2DMap_load makes 0.8 / 0.85 in Time Trial.
constexpr float FaceUnits=32.f,ShadowOffset=1.5f;
constexpr float LocalFace=.8f,LocalShadow=.85f,OtherFace=.6f,TimeTrialFace=.8f;
// The others' face alpha (DAT_808D3824 = 160); their shadows and searchlight
// are hidden (no teams). chara_0_0's material: alpha compare GEQUAL 140.
constexpr uint32_t OtherAlpha=160;
constexpr int FaceAlphaRef=140;
// The local racer's searchlight (light: 60x64, origin bottom centre; initSelf
// 0x807EAECC sets scale 0.8 and alpha 0xB4; material C0 = C1 = yellow).
constexpr float LightW=60.f*.8f,LightH=64.f*.8f;
constexpr uint32_t LightColor=0xffff00b4u;
// The start line (start_line.brlyt race_null, tt_start_line_22).
constexpr float StartW=22.f,StartH=4.f;
// CtrlRaceBase's show/hide slide (CtrlRaceBase_process 0x807EC3E0): 0.05 per
// frame (lbl_1_data_20EC0); fn_1_2DBE58 0x807EC20C moves the control's race_null
// pane 400 x 1.0005 x (s < 0.1 ? -s : s - 0.1), s = slide^2, toward the nearer
// vertical edge (CtrlRaceBase_initSelf 0x807EC6E8): map_1 sits below the screen
// centre, so the map, its icons and its start line leave downward.
constexpr float SlideStep=.05f,SlideDistance=400.f*1.0005f;
// CtrlRaceTime at RaceTimerScale in the top 20 px, right-aligned with the map.
constexpr float TimerRight=MapX+MapSize,TimerTop=0.f;
// fn_1_2DBB6C (USA 0x807E2674) with flag 0: the HUD slot colour on top, then
// +(40,130,40) clamped per lower corner, cumulatively.
constexpr uint32_t TimerGold[4]={0xe8d400ffu,0xe8d400ffu,0xffff28ffu,0xffff50ffu};
// common_w039_map_chara_icon_rotate.brlan: 60 frames, not looping; pane chara_0_n (the
// face and both shadows, not the searchlight) turns RLPA Z from 0 at frame 0 to 720
// degrees at frame 59, slope 12.203 at both keys: linear, counter-clockwise on screen.
constexpr int SpinFrames=60;
float smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
float slideOffset(float slide){const float s=slide*slide;return SlideDistance*(s<.1f?-s:s-.1f);}
}
static_assert(RaceBottomScreen::QuadCapacity==79&&RaceBottomScreen::QuadCapacity<=unsigned(Renderer::BottomQuadCapacity),
              "every quad a frame can add fits the renderer's bottom sprite region");

struct RaceBottomScreen::Impl {
    Texture map,atlas;
    unsigned course=~0u;
    bool target=false;
    std::unique_ptr<Renderer::MaterialQuad[]> quads; // 2 x QuadCapacity, alternating per frame
    // Animation state (60 Hz ticks).
    uint32_t lastTick=0;bool started=false;
    float standings=0;                           // slide-in 0..1
    unsigned roulette=0;
    // The map group's CtrlRaceBase slide: state 1 hidden, 2 showing, 3 shown, 0 hiding.
    int slideState=1;float slide=-1;
    // Each map icon's rotate frame (-1: the default animation) and the last action start count.
    std::array<int,12> spin{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};std::array<uint32_t,12> starts{};bool startsSet=false;
    void reset(){started=false;standings=0;slideState=1;slide=-1;spin.fill(-1);startsSet=false;}
    // One frame's quads, in in-place batches.
    Renderer::MaterialQuad* base=nullptr;unsigned count=0;
    bool add(const RaceMapRect& r,float x0,float y0,float x1,float y1,uint32_t color=0xffffffffu){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=RaceMapAtlasSize[0],ah=RaceMapAtlasSize[1];
        const float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        auto& q=base[count++];
        q.x[0]=x0;q.y[0]=y0;q.x[1]=x1;q.y[1]=y0;q.x[2]=x0;q.y[2]=y1;q.x[3]=x1;q.y[3]=y1;
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        for(auto& c:q.color)c=color;
        return true;
    }
    // A pane of w x h layout units (pixels) whose origin sits (ox, oy) from its
    // own centre, turned counter-clockwise by angle (radians, layout y up)
    // about (px, py) on screen, over atlas rectangle r. Corners TL, TR, BL, BR.
    bool addPane(const RaceMapRect& r,float px,float py,float ox,float oy,float w,float h,float angle,uint32_t color){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=RaceMapAtlasSize[0],ah=RaceMapAtlasSize[1];
        const float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        const float c=std::cos(angle),s=std::sin(angle);
        const float lx[4]={ox-w*.5f,ox+w*.5f,ox-w*.5f,ox+w*.5f},ly[4]={oy+h*.5f,oy+h*.5f,oy-h*.5f,oy-h*.5f};
        auto& q=base[count++];
        for(unsigned k=0;k<4;++k){q.x[k]=px+lx[k]*c-ly[k]*s;q.y[k]=py-(lx[k]*s+ly[k]*c);q.color[k]=color;}
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        return true;
    }
};

void fillBottomView(BottomView& v,const RaceGrid& grid,const RaceItems& items,bool introActive,uint32_t tick,float leaveFade){
    namespace K=Kinoko;
    v=BottomView{};
    auto* race=K::System::RaceManager::Instance();
    const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
    v.racers=std::min(grid.count(),12u);
    for(unsigned i=0;i<v.racers;++i){
        auto* kart=K::Kart::KartObjectManager::Instance()->object(i);
        const auto& p=kart->pos();
        auto& r=v.racer[i];r.x=p.x;r.z=p.z;r.local=i==0;
        r.ghost=grid.ghost(i);r.finished=grid.finished(i);
        r.inAction=kart->status().onBit(K::Kart::eStatus::InAction);r.actionStarts=kart->action()->startCount();
        r.character=uint8_t(std::min<unsigned>(unsigned(scenario.players[i].character),23));
        r.rank=uint8_t(std::clamp(grid.rank(i),1u,12u));
        // The item window's picture (CtrlRaceItemWindow): a spinning roulette's
        // icon, else the held item; a Triple Mushroom shows its count (19 = two).
        const auto& h=items.hud(i);
        if(h.roulette)r.item=uint8_t(h.rouletteIcon);
        else{
            const auto picture=itemWindowPicture(h);
            r.item=picture.kind==ItemKind::TripleMushroom&&picture.variant==2?19:
                   picture.kind==ItemKind::TripleMushroom&&picture.variant==1?uint8_t(ItemKind::Mushroom):uint8_t(picture.kind);
        }
    }
    // KartMove +0x80 (smoothedForward) turns the searchlight (calfTransform 0x807EB3CC).
    {const auto& f=K::Kart::KartObjectManager::Instance()->object(0)->move()->smoothedForward();v.headingX=f.x;v.headingZ=f.z;}
    v.timeTrial=scenario.isTimeTrial();
    // Watching a ghost replay, player 0 is the Ghost-type racer. Retail's page for it
    // (GhostReplayRacePage_getControls 0x80633C84: mask 4) has neither CtrlRaceTime
    // (bit 1) nor CtrlRace2DMap (bit 3; RacePage_initControls 0x80857CC0).
    v.replay=v.racers&&grid.ghost(0);
    const unsigned stage=unsigned(race->stage());
    const auto& me=race->player(0);
    const auto& timer=me.finished()?me.raceTimer():race->timerManager().currentTimer();
    v.milliseconds=uint32_t(timer.min)*60000u+uint32_t(timer.sec)*1000u+uint32_t(timer.mil);
    // CtrlRaceTime and CtrlRace2DMap show with the HUD from the countdown
    // (HasStarted); Mario Kart 7 shows its standings once the race has started.
    v.timerVisible=!introActive&&stage>=1;v.standingsVisible=!introActive&&stage>=2;v.tick=tick;
    // Both screens go black together when leaving after the race (Mario Kart 7).
    v.fade=std::clamp(leaveFade,0.f,1.f);
}

RaceBottomScreen::RaceBottomScreen():impl_(std::make_unique<Impl>()){}
RaceBottomScreen::~RaceBottomScreen()=default;
bool RaceBottomScreen::active() const {return impl_->target;}

bool RaceBottomScreen::load(Renderer& renderer,unsigned course){
    auto& m=*impl_;
    if(course>=RaceMapCourses.size()){unload(renderer);return false;}
    if(m.target&&m.course==course){
        // A restart of the same race: reset the presentation, keep the textures.
        m.reset();
        return true;
    }
    unload(renderer);
    if(!renderer.enableBottom())return false;
    m.target=true;m.course=course;
    m.quads.reset(new(std::nothrow) Renderer::MaterialQuad[2*QuadCapacity]);
    if(!m.quads){unload(renderer);return false;}
    renderer.loadVramTexture(m.atlas,RaceMapAtlasTexture);
    renderer.loadVramTexture(m.map,RaceMapCourses[course].texture);
    m.reset();
    return true;
}

void RaceBottomScreen::unload(Renderer& renderer){
    auto& m=*impl_;
    if(!m.target&&!m.map.ready&&!m.atlas.ready&&!m.quads)return;
    // Nothing recorded may still sample these textures or read the quads.
    renderer.waitForGpu();
    m.map.reset();m.atlas.reset();
    m.quads.reset();
    renderer.releaseBottom();
    m.target=false;m.course=~0u;
}

void RaceBottomScreen::describe(char* text,unsigned size) const {
    const auto& m=*impl_;
    std::snprintf(text,size,"bottom screen course=%d target=%d map=%lu atlas=%lu vram_free=%lu",
                  m.course==~0u?-1:int(m.course),int(m.target),(unsigned long)m.map.bytes(),
                  (unsigned long)m.atlas.bytes(),(unsigned long)vramSpaceFree());
}

void RaceBottomScreen::draw(Renderer& renderer,const BottomView& view){
    auto& m=*impl_;
    if(!m.target||!m.quads)return;
    const auto& course=RaceMapCourses[m.course];
    // Animations advance by simulation ticks (presentation runs at 30 or 60 Hz).
    const unsigned elapsed=m.started?std::min<unsigned>(view.tick-m.lastTick,30u):0u;
    m.lastTick=view.tick;m.started=true;
    const unsigned racers=std::min(view.racers,12u);
    // An action that started since the last presented frame starts the spin even if it is over.
    std::array<bool,12> startedNow{};
    for(unsigned r=0;r<racers;++r){startedNow[r]=m.startsSet&&view.racer[r].actionStarts!=m.starts[r];m.starts[r]=view.racer[r].actionStarts;}
    m.startsSet=true;
    for(unsigned i=0;i<elapsed;++i){
        m.standings=std::clamp(m.standings+(view.standingsVisible?1.f:-1.f)/16.f,0.f,1.f);
        ++m.roulette;
        // CtrlRaceBase_process: HasStarted (fn_1_2DC010 0x807EC3C4, the race frame
        // counter runs) starts the show slide; nothing hides it during a race here.
        if(view.timerVisible&&m.slideState!=3)m.slideState=2;
        switch(m.slideState){
        case 1:m.slide=-1;break;
        case 3:m.slide=0;break;
        case 0:m.slide=std::fabs(m.slide)+SlideStep;if(m.slide>=1){m.slide=-1;m.slideState=1;}break;
        default:m.slide=-std::fabs(m.slide)+SlideStep;if(m.slide>=0){m.slide=0;m.slideState=3;}break;
        }
        // Per 60 Hz frame, as LayoutUIControl_calc (0x8063DB00) runs them: calcSelf starts
        // "rotate" at frame 0 when it is not playing and the racer is in an action
        // (Group_setAnimation 0x8063C91C), then the animator (0x8063C7BC -> 0x8063CBE4)
        // advances it; reaching frame 60 switches back to "default", and the next frame
        // starts it again while the action lasts. Every racer's icon spins.
        for(unsigned r=0;r<racers;++r){
            int& s=m.spin[r];
            if(s<0&&(view.racer[r].inAction||(i==0&&startedNow[r])))s=0;
            if(s>=0&&++s>=SpinFrames)s=-1;
        }
    }
    m.base=m.quads.get()+(renderer.recordingFrame()&1)*QuadCapacity;m.count=0;
    const bool atlas=m.atlas.ready;

    // 1. The lower screen's black (no clear).
    m.add(RaceMapWhite,0,0,ScreenW,ScreenH,0x000000ffu);
    const unsigned fills=m.count;

    // 2. CtrlRace2DMap: its race_null pane (the capture), then its children by
    // zIndex (ControlGroup::draw 0x805C2948 sorts them ascending): the start line
    // (-500), then each racer's icon (calfTransform: 1 + players - 0.1 x rank,
    // +10 for the local racer). The RaceHUD page's exit slide replaces the show slide.
    const float slide=view.exitSlide>=0?view.exitSlide:m.slide;
    const bool mapShown=!view.replay&&std::fabs(slide)<1.f;
    const float drop=slideOffset(slide);
    // fn_1_2D0530 (0x807E08E4): u = (x-x0)/(x1-x0), v = (z-z0)/(z1-z0), mirrored u = 1-u;
    // the icon pane sits at (220(u-1/2), 220(v-1/2)) (calcTransform 0x807EACFC).
    auto mapU=[&](float x){const float u=(x-course.x0)/(course.x1-course.x0);return view.mirror?1-u:u;};
    auto mapV=[&](float z){return (z-course.z0)/(course.z1-course.z0);};
    auto screenX=[&](float u){return MapCX+MapSize*(u-.5f);};
    auto screenY=[&](float v){return MapCY+drop-MapSize*(v-.5f);};
    const unsigned mapFirst=m.count;
    if(mapShown&&m.map.ready&&m.count<QuadCapacity){
        auto& q=m.base[m.count++];
        // The capture's 220x220 texels, one per pixel; mirror mode flips it with the race view.
        const float t=float(RaceMapCapture)/float(RaceMapTextureSize),y=MapY+drop;
        float u0=0,u1=t;if(view.mirror)std::swap(u0,u1);
        q.x[0]=MapX;q.y[0]=y;q.x[1]=MapX+MapSize;q.y[1]=y;q.x[2]=MapX;q.y[2]=y+MapSize;q.x[3]=MapX+MapSize;q.y[3]=y+MapSize;
        q.u[0]=u0;q.v[0]=0;q.u[1]=u1;q.v[1]=0;q.u[2]=u0;q.v[2]=t;q.u[3]=u1;q.v[3]=t;
        for(auto& c:q.color)c=0xffffff00u|MapAlpha;
    }
    const unsigned mapCount=m.count-mapFirst;
    // Batches of the map's children: start line, the others' faces, the local
    // racer's searchlight and shadows, the local racer's face.
    const unsigned startFirst=m.count;
    if(mapShown&&atlas){
        // KTPT 0 (CtrlRace2DMap_load: findKartStartPoint 0x80514368 for one racer is the
        // point itself); pane rotation Z = its yaw, unmirrored (calcTransform).
        m.addPane(RaceMapStartLine,screenX(mapU(course.startX)),screenY(mapV(course.startZ)),0,0,StartW,StartH,course.startYaw*Pi/180.f,0xffffffffu);
    }
    const unsigned startCount=m.count-startFirst;
    struct Icon {const BottomRacer* racer;unsigned id;float z;};
    std::array<Icon,12> icons{};unsigned n=0;
    if(mapShown&&atlas)for(unsigned i=0;i<racers;++i){
        const auto& r=view.racer[i];
        // calcSelf 0x807EB250: a ghost's icon hides once it has finished.
        if(r.ghost&&r.finished)continue;
        icons[n++]={&r,i,(r.local?10.f:0.f)+float(racers)-.1f*float(r.rank)};
    }
    std::sort(icons.begin(),icons.begin()+n,[](const Icon& a,const Icon& b){return a.z<b.z;});
    auto spinAngle=[&](unsigned id){
        const int frame=id<12?m.spin[id]:-1;
        return frame<0?0.f:720.f*float(std::min(frame,SpinFrames-1))/float(SpinFrames-1)*(Pi/180.f);
    };
    const unsigned othersFirst=m.count;
    for(unsigned i=0;i<n;++i){
        const auto& r=*icons[i].racer;
        if(r.local)continue;
        const float face=FaceUnits*(view.timeTrial?TimeTrialFace:OtherFace);
        m.addPane(RaceMapFaces[std::min<unsigned>(r.character,23)],screenX(mapU(r.x)),screenY(mapV(r.z)),0,0,face,face,
                  spinAngle(icons[i].id),0xffffff00u|OtherAlpha);
    }
    const unsigned othersCount=m.count-othersFirst;
    const Icon* local=nullptr;
    for(unsigned i=0;i<n;++i)if(icons[i].racer->local)local=&icons[i];
    const unsigned lightFirst=m.count;
    if(local){
        const auto& r=*local->racer;
        const float u=mapU(r.x),v=mapV(r.z),x=screenX(u),y=screenY(v);
        // The searchlight points along the projected direction of travel: angle =
        // atan2(-du, dv) of (pos + smoothedForward) - pos in map space; it does not spin.
        const float du=mapU(r.x+view.headingX)-u,dv=mapV(r.z+view.headingZ)-v;
        const float light=du==0&&dv==0?0.f:std::atan2(-du,dv);
        m.addPane(RaceMapSearchlight,x,y,0,LightH*.5f,LightW,LightH,light,LightColor);
        // chara_0_n turns the face and both shadows together about the icon's centre.
        const float turn=spinAngle(local->id),shadow=FaceUnits*LocalShadow;
        const float c=std::cos(turn),s=std::sin(turn);
        for(const float ox:{ShadowOffset,-ShadowOffset})
            m.addPane(RaceMapFaces[std::min<unsigned>(r.character,23)],x+ox*c,y-ox*s,0,0,shadow,shadow,turn,0x000000ffu);
    }
    const unsigned lightCount=m.count-lightFirst;
    const unsigned localFirst=m.count;
    if(local){
        const auto& r=*local->racer;
        const float face=FaceUnits*LocalFace;
        m.addPane(RaceMapFaces[std::min<unsigned>(r.character,23)],screenX(mapU(r.x)),screenY(mapV(r.z)),0,0,face,face,spinAngle(local->id),0xffffffffu);
    }
    const unsigned localCount=m.count-localFirst;

    // 3. The standings column and its rows over the map's left edge, then the time.
    const unsigned topFirst=m.count;
    m.add(RaceMapWhite,0,0,Column,ScreenH,0x101626ffu);
    m.add(RaceMapWhite,Column-1,0,Column,ScreenH,0x5a6a8aff);
    if(atlas&&m.standings>0){
        // Rows slide in from the left once the race starts (Mario Kart 7). Each racer's
        // row is the slot of its place: places are a permutation (RaceGrid::afterStep),
        // so every row has its own slot and two racers trading places swap rows at once.
        const float slideIn=(1-smooth(m.standings))*-(Column+4);
        auto drawRow=[&](unsigned i){
            const auto& r=view.racer[i];
            const unsigned place=std::clamp<unsigned>(r.rank,1,12)-1;
            const float x=RowX+slideIn,y=RowTop+float(place)*RowPitch;
            m.add(r.local?RaceMapRowPlayer:RaceMapRowOther,x,y,x+RaceMapRowPlayer.w,y+RowH);
            const auto& ord=RaceMapOrdinals[place];
            const float oy=y+std::floor((RowH-ord.h)*.5f);
            m.add(ord,x+OrdinalX,oy,x+OrdinalX+ord.w,oy+ord.h);
            m.add(RaceMapRowFaces[std::min<unsigned>(r.character,23)],x+FaceX,y+1,x+FaceX+RowIcon,y+1+RowIcon);
            if(r.item<RaceMapItems.size()&&RaceMapItems[r.item].w)m.add(RaceMapItems[r.item],x+ItemX,y+1,x+ItemX+RowIcon,y+1+RowIcon);
        };
        for(unsigned i=0;i<racers;++i)drawRow(i);
    }
    if(atlas&&view.timerVisible&&!view.replay){
        // CtrlRaceTime_refresh (0x807F84C0): minutes clamp at 99'59"999.
        unsigned ms=view.milliseconds,minutes=ms/60000,seconds=ms/1000%60,milli=ms%1000;
        if(minutes>99){minutes=99;seconds=59;milli=999;}
        const unsigned digit[7]={minutes/10,minutes%10,seconds/10,seconds%10,milli/100,milli/10%10,milli%10};
        // RaceTimerPanes: race_null, time_01, coron01, time_02, time_03, coron00, time_04..06, set_p.
        const float scale=RaceTimerScale,right=RaceTimerPanes[8].x+RaceTimerPanes[8].w*.5f;
        const float ox=TimerRight-right*scale,oy=TimerTop+RaceTimerPanes[0].h*.5f*scale;
        const RaceMapRect* art[10]={&RaceMapDigits[digit[0]],&RaceMapDigits[digit[1]],&RaceMapColon,&RaceMapDigits[digit[2]],&RaceMapDigits[digit[3]],
                                    &RaceMapDot,&RaceMapDigits[digit[4]],&RaceMapDigits[digit[5]],&RaceMapDigits[digit[6]],&RaceMapTimeLabel};
        for(unsigned i=0;i<10&&m.count<QuadCapacity;++i){
            // The art is baked at the pane's drawn size: on whole pixels every digit
            // samples its texels 1:1, so all of them are equally sharp.
            const auto& p=RaceTimerPanes[i];const auto& a=*art[i];
            const float x=std::round(ox+p.x*scale-a.w*.5f),y=std::round(oy-p.y*scale-a.h*.5f);
            if(!m.add(a,x,y,x+a.w,y+a.h))break;
            auto& q=m.base[m.count-1];
            for(unsigned k=0;k<4;++k)q.color[k]=TimerGold[k];
        }
    }
    if(view.fade>0)m.add(RaceMapWhite,0,0,ScreenW,ScreenH,uint32_t(std::lround(view.fade*255.f)));
    const unsigned topCount=m.count-topFirst;
    // NW4R default material: lerp(C0 = transparent black, C1 = white, texture) x vertex colour.
    const Texture* ui=atlas?&m.atlas:nullptr;
    renderer.drawScreenMaterialQuadsInPlace(ui,m.base,fills,0x00000000u,0xffffffffu);
    if(mapCount)renderer.drawScreenMaterialQuadsInPlace(&m.map,m.base+mapFirst,mapCount,0x00000000u,0xffffffffu);
    if(startCount)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+startFirst,startCount,0x00000000u,0xffffffffu);
    if(othersCount)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+othersFirst,othersCount,0x00000000u,0xffffffffu,FaceAlphaRef);
    if(lightCount)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+lightFirst,lightCount,0x00000000u,0xffffffffu);
    if(localCount)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+localFirst,localCount,0x00000000u,0xffffffffu,FaceAlphaRef);
    if(topCount)renderer.drawScreenMaterialQuadsInPlace(ui,m.base+topFirst,topCount,0x00000000u,0xffffffffu);
}
}
