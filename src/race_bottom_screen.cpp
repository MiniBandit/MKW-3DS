// Race bottom screen: see include/mkw/race_bottom_screen.hpp and docs/BOTTOM_SCREEN.md.
#include <mkw/race_bottom_screen.hpp>
#include <mkw/render_quality.hpp>
#include <mkw/native_mii_portrait.hpp>
#include <mkw/race_map_assets.hpp>
#include <mkw/race_bottom_rows.hpp>
#include <mkw/battle_map_assets.hpp>
#include <mkw/battle_bottom_scores.hpp>
#include <mkw/map_object_assets.hpp>
#include <mkw/course_catalog.hpp>
#include <game/field/ObjectDrivableDirector.hh>
#include <game/field/obj/ObjectTownBridge.hh>
#include <game/field/obj/ObjectTwistedWay.hh>
#include <game/field/obj/ObjectEscalator.hh>
#include <game/field/obj/ObjectVolcanoPiece.hh>
#include <mkw/race_grid.hpp>
#include <mkw/race_items.hpp>
#include <mkw/renderer.hpp>
#include <game/kart/KartMove.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mkw/hud_feedback.hpp>
#include <mkw/diag.hpp>
#include <mkw/loading_screen.hpp>
#include <atomic>
#include <cstring>
#include <game/kart/KartAction.hh>
#include <new>
namespace mkw {
namespace {
constexpr float ScreenW=320.f,ScreenH=240.f;
// The standings column (left) and the map (the rest), as the user asked; the
// map follows the player at its centre, Mario Kart 7's framing.
constexpr float Column=82.f,MapX=Column,MapW=ScreenW-Column,MapCX=MapX+MapW*.5f,MapCY=ScreenH*.5f;
// Standings rows: the Wii result board's bar (78x19, baked), 12 rows that
// fill the screen's height (1 px apart); in each, the position, then the
// face and the held item at 17 px.
constexpr float RowTop=1.f,RowPitch=20.f,RowX=2.f,RowH=19.f;
// Floating rows (RaceRowsUi, 19 px): 1 px of map above the first and below the
// last, the rest shared out as 0-1 px gaps; each row snaps to a whole pixel.
// DIAG rows-margin.txt overrides the margin (pixels, e.g. 0.5).
static float floatTop=1.f;
inline float floatRowY(float row){
    const float pitch=(ScreenH-2*floatTop-RaceRowsUi.rowH)/11.f;
    return std::floor(floatTop+row*pitch+.5f);
}
constexpr float OrdinalX=3.f,FaceX=32.f,ItemX=56.f,RowIcon=17.f;
// Retail CtrlRace2DMapCharacter (USA 0x807E1620/0x807E1B20): the face pane is
// 32 x 1.6 layout units, scaled 0.8 for the local racer and 0.6 for the
// others; the two black shadow copies 2.0/1.6 of it, 1.5 units either side;
// the others are drawn at alpha 160. One layout unit is 240/456 px here.
constexpr float Unit=240.f/456.f;
constexpr float FaceUnits=32.f*1.6f;
constexpr float LocalScale=.8f,LocalShadowScale=.85f,OtherScale=.6f,OtherShadowScale=.65f;
// All racer portraits are opaque (Dad's HW09 layout policy).
// Searchlight (light pane, origin bottom centre): 60x64 x 1.2, pane scale 0.8, alpha 0xB4.
constexpr float LightW=60.f*1.2f*.8f*Unit,LightH=64.f*1.2f*.8f*Unit;
constexpr uint32_t LightColor=0xffff00b4u;
// CtrlRaceTime at the top right, the retail layout uniformly scaled: 0.75
// draws its 32-unit digits at the atlas's 24 px. The TIME pane's left half is
// transparent, so the visible timer spans x 113-316, clear of the standings.
constexpr float TimerScale=.75f,TimerRight=316.f,TimerTop=4.f;
// fn_1_2DBB6C (USA 0x807E2674) with flag 0: the HUD slot colour on top, then
// +(40,130,40) clamped per lower corner, cumulatively.
constexpr uint32_t TimerGold[4]={0xe8d400ffu,0xe8d400ffu,0xffff28ffu,0xffff50ffu};
constexpr float MarkerSize=13.f;
float smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
}
static_assert(RaceBottomScreen::QuadCapacity==276&&RaceBottomScreen::QuadCapacity<=unsigned(Renderer::BottomQuadCapacity),
              "every quad a frame can add fits the renderer's bottom sprite region");

struct RaceBottomScreen::Impl {
    Texture map,overview,atlas,objects,scores,mii,rows;
    // The UI atlas's rectangles (RaceMapUiWithShadows: DIAG no-bottom-atlas256.txt).
    const RaceMapAtlas* ui=&RaceMapUi;
    // The local outline drawn from the face in a team colour: its own draw
    // (colour0 white), at this index of the overlay batch.
    unsigned outline=~0u;
    NativeMii miiIdentity{};
    Renderer::MaterialQuad miiQuads[2][2]{};bool miiMap=false,miiRow=false;
    unsigned course=~0u;
    bool target=false;
    std::unique_ptr<Renderer::MaterialQuad[]> quads; // 2 x QuadCapacity, alternating per frame
    // Animation state (60 Hz ticks).
    uint32_t lastTick=0;bool started=false;
    float blend=0;bool overviewWanted=false;     // 0 detailed .. 1 whole course
    float timerSlide=-1.f;
    float standings=0;                           // slide-in 0..1
    std::array<float,12> row{};bool rowsSet=false;
    unsigned roulette=0;
    unsigned skippedFrames=0; // DIAG: frames left alone at 30 Hz (skipRace)
    // The map stops at the standings column's edge; markers wholly under it
    // are not drawn (A/B: no-map-clip.txt).
    bool mapClip=true;
    // The standings rows float over the full-width map: no column backdrop, no
    // map clip (A/B: no-floating-rows.txt).
    bool floatingRows=true;
    // Race frames whose quads equal the last shown frame's keep the lower
    // LCD's picture: no draw, no clear, no transfer (A/B: no-race-bottom-keep.txt).
    // The race's 2D lower screen binds no depth buffer (A/B: no-bottom-depth-off.txt).
    bool depthOff=true;
    // Its depth buffer only exists as the top LCD's, borrowed by the intro's
    // 3D lower view (A/B: no-bottom-depth-free.txt).
    bool depthFree=true;
    bool keep=true,shownValid=false;uint64_t shownHash=0;uint32_t shownSerial=0;unsigned shownApt=0,shownScopes=0;
    unsigned keptFrames=0,drawnFrames=0;uint32_t miiGeneration=0;
    // The composed frame's draws (compose -> submit).
    struct Batches {unsigned fills=0,mapFirst=0,mapCount=0,objectFirst=0,objectCount=0,overlayFirst=0,overlayCount=0,
                    topFirst=0,topCount=0,rowsFirst=0,rowsCount=0,scoreFirst=0,scoreCount=0,fadeFirst=0,mii=0,outline=~0u;const Texture* mapTexture=nullptr;bool covered=false,atlas=false;} b;
    // One frame's quads, in three in-place batches.
    Renderer::MaterialQuad* base=nullptr;unsigned count=0;
    bool add(const RaceMapRect& r,float x0,float y0,float x1,float y1,uint32_t color=0xffffffffu,bool mirrorU=false,bool mirrorV=false,bool score=false){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=score?BattleBottomScoreAtlasWidth:ui->size[0],ah=score?BattleBottomScoreAtlasHeight:ui->size[1];
        float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        if(mirrorU)std::swap(u0,u1);
        if(mirrorV)std::swap(v0,v1);
        auto& q=base[count++];
        q.x[0]=x0;q.y[0]=y0;q.x[1]=x1;q.y[1]=y0;q.x[2]=x0;q.y[2]=y1;q.x[3]=x1;q.y[3]=y1;
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        for(auto& c:q.color)c=color;
        return true;
    }
    // A quad over rectangle r of the rows texture (RaceRowsUi).
    bool addRow(const RaceMapRect& r,float x0,float y0,float x1,float y1,uint32_t color=0xffffffffu){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=RaceRowsUi.size[0],ah=RaceRowsUi.size[1];
        const float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        auto& q=base[count++];
        q.x[0]=x0;q.y[0]=y0;q.x[1]=x1;q.y[1]=y0;q.x[2]=x0;q.y[2]=y1;q.x[3]=x1;q.y[3]=y1;
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        for(auto& c:q.color)c=color;
        return true;
    }
    // A quad with free corners (TL, TR, BL, BR) over atlas rectangle r.
    bool addCorners(const RaceMapRect& r,const float (&x)[4],const float (&y)[4],uint32_t color){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=ui->size[0],ah=ui->size[1];
        const float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        auto& q=base[count++];
        for(unsigned k=0;k<4;++k){q.x[k]=x[k];q.y[k]=y[k];q.color[k]=color;}
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        return true;
    }
};

const Texture* RaceBottomScreen::miiPortrait() const{return impl_->mii.ready?&impl_->mii:nullptr;}

bool RaceBottomScreen::setMiiPortrait(Renderer& renderer,const NativeMii& identity){
 if(impl_->mii.ready&&impl_->miiIdentity==identity)return true;
 renderer.waitForGpu();impl_->mii=Texture{};std::vector<uint8_t> rgba;
 ++impl_->miiGeneration;
 if(!composeNativeMiiPortrait(identity,64,rgba)||!impl_->mii.initRgba8(64,64,rgba.data(),rgba.size()))return false;
 impl_->miiIdentity=identity;return true;
}

void fillBottomView(BottomView& v,const RaceGrid& grid,const RaceItems& items,bool introActive,uint32_t tick,float leaveFade){
    namespace K=Kinoko;
    const auto previousObjects=v.mapObject;
    const unsigned previousCount=v.mapObjects,previousCourse=v.mapCourse,previousTick=v.tick;
    v=BottomView{};
    auto* race=K::System::RaceManager::Instance();
    const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
    v.mirror=scenario.mirror;
    v.timeTrial=scenario.gameMode==K::System::RaceConfig::Scenario::GameMode::Time_Trial;
    // Snapshot live physical transforms, independent of the upper camera's culling.
    // No scene pointers survive this call (restart/unload cannot leave stale actors).
    unsigned mapCourse=~0u;
    for(unsigned i=0;i<std::size(Courses);++i)if(Courses[i].id==scenario.course){mapCourse=i;break;}
    v.mapCourse=mapCourse;
    const auto objectArt=mapObjectAtlas(mapCourse);
    const bool reuseMap=previousCourse==mapCourse&&tick>=previousTick&&tick/3==previousTick/3;
    if(reuseMap){v.mapObject=previousObjects;v.mapObjects=previousCount;}
    else if(objectArt.count)if(const auto* director=K::Field::ObjectDrivableDirector::Instance()){
        for(const auto* object:director->objects()){
            const auto* geo=object->mapObj();
            const unsigned id=unsigned(object->id());
            // DCPillar also registers a stationary collision base with the same id.
            if(id==0x208&&!(object->loadFlags()&1))continue;
            if((id==0x1fc||id==0x20d)&&!static_cast<const K::Field::ObjectVolcanoPiece*>(object)->mapRoadPresent())continue;
            const bool bridge=id==0x1ff;
            unsigned state=0;
            auto matrix=object->transform();
            if(bridge&&geo){
                const float frame=static_cast<const K::Field::ObjectTownBridge*>(object)->animationFrame();
                state=unsigned(std::clamp(std::lround(frame*15.f/89.f),0l,15l))+(geo->rot().y<0?16:0);
                matrix.makeRT(geo->rot()*(3.14159265358979323846f/180.f),object->pos());
            }
            const bool racing= race&&race->isStageReached(K::System::RaceManager::Stage::Race);
            const unsigned timer=race?race->timer():0;
            if(id==0x1fe){
                const auto* road=static_cast<const K::Field::ObjectTwistedWay*>(object);
                const unsigned frame=racing?timer:road->introTimer()-1;
                state=(frame%120)/3;
            }else if(id==0x202){state=((racing?timer:0)%160)/3;}
            else if(id==0x25e){
                const float phase=static_cast<const K::Field::ObjectEscalator*>(object)->stepFactor();
                state=std::min(19u,unsigned(std::max(0.f,phase)));
                // The atlas animates within the fixed landing footprint. Remove
                // the tread-loop offset so its wrap cannot move the whole tile.
                const auto shift=matrix.multVector33({0.f,10.f*phase,-30.f*phase});
                matrix.makeRT(object->rot(),object->pos()-shift);
            }
            else if(id==0x208&&geo){
                state=unsigned(std::clamp(std::lround(object->rot().x*(180.f/3.14159265358979323846f)/5.f),0l,24l));
                matrix.makeRT(geo->rot()*(3.14159265358979323846f/180.f),object->pos());
            }else if(id==0x209&&geo){
                const float rise=object->pos().y-geo->pos().y;
                state=unsigned(std::clamp(std::lround(rise*15.f/std::max(1u,unsigned(geo->setting(1)))),0l,15l));
            }
            for(unsigned a=0;a<objectArt.count;++a){
                const auto& art=objectArt.art[a];
                if(state!=art.state||unsigned(object->id())!=art.id||(art.variant>=0&&(!geo||geo->setting(id==0x209?1:0)!=unsigned(art.variant))))continue;
                if(v.mapObjects>=v.mapObject.size())break;
                auto& out=v.mapObject[v.mapObjects++];out.art=a;
                const auto& scale=object->scale();
                for(unsigned k=0;k<4;++k){
                    const K::EGG::Vector3f local{(k&1?art.x1:art.x0)*scale.x,art.height*scale.y,(k&2?art.z1:art.z0)*scale.z};
                    const auto p=matrix.multVector(local);
                    out.x[k]=p.x;out.z[k]=p.z;out.height+=p.y*.25f;
                }
                break;
            }
        }
        std::sort(v.mapObject.begin(),v.mapObject.begin()+v.mapObjects,[](const auto& a,const auto& b){return a.height<b.height;});
    }

    v.racers=std::min(grid.count(),12u);
    for(unsigned i=0;i<v.racers;++i){
        const auto& p=K::Kart::KartObjectManager::Instance()->object(i)->pos();
        auto& r=v.racer[i];r.x=p.x;r.z=p.z;r.local=i==0;
        const auto* kart=K::Kart::KartObjectManager::Instance()->object(i);const auto* action=kart->action();
        const int damage=int(action->currentAction());
        if(kart->status().onBit(K::Kart::eStatus::InAction)&&((damage>=0&&damage<=11)||damage==17))r.hitAngle=mapHitAngle(action->frame());
        r.team=scenario.teams?RaceTeam(scenario.players[i].team):RaceTeam::None;
        r.character=uint8_t(scenario.players[i].character);
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
    {const auto& f=K::Kart::KartObjectManager::Instance()->object(0)->move()->smoothedForward();v.headingX=f.x;v.headingZ=f.z;}
    items.forEachActorSlotRead([&](unsigned slot){
        const auto& a=items.actors()[slot];
        if(!a.active||v.items>=v.item.size())return true;
        int marker=-1;
        if(a.explosion)marker=a.kind==ItemKind::BlueShell?int(BottomMarker::BlastBlue):a.kind==ItemKind::Bobomb?int(BottomMarker::BlastRed):-1;
        else if(a.kind==ItemKind::Banana)marker=int(BottomMarker::Banana);
        else if(a.kind==ItemKind::GreenShell)marker=int(BottomMarker::GreenShell);
        else if(a.kind==ItemKind::RedShell)marker=int(BottomMarker::RedShell);
        else if(a.kind==ItemKind::BlueShell)marker=int(BottomMarker::SpinyShell);
        else if(a.kind==ItemKind::Bobomb)marker=int(BottomMarker::Bobomb);
        else if(a.kind==ItemKind::FakeBox)marker=int(BottomMarker::FakeBox);
        if(marker<0)return true;
        const auto p=a.visualPosition();auto& it=v.item[v.items++];
        it.x=p.x;it.z=p.z;it.marker=uint8_t(marker);it.radius=a.radius; // a blast's live retail radius
        return true;
    });
    const unsigned stage=unsigned(race->stage());
    const auto& me=race->player(0);
    const auto& timer=me.finished()?me.raceTimer():race->timerManager().currentTimer();
    v.milliseconds=uint32_t(timer.min)*60000u+uint32_t(timer.sec)*1000u+uint32_t(timer.mil);
    // CtrlRaceTime shows with the HUD from the countdown; Mario Kart 7 shows
    // its standings once the race has started.
    v.timerVisible=!introActive&&stage>=1;v.standingsVisible=!v.timeTrial&&!introActive&&stage>=2;v.tick=tick;
    // Both screens go black together when leaving after the race (Mario Kart 7).
    v.fade=std::clamp(leaveFade,0.f,1.f);
}

namespace {
// The HOME Menu, sleep and the like may change what the lower LCD shows.
std::atomic<unsigned> aptEvents{0};
void countAptEvent(APT_HookType,void*){aptEvents.fetch_add(1,std::memory_order_relaxed);}
aptHookCookie aptCookie;
}

RaceBottomScreen::RaceBottomScreen():impl_(std::make_unique<Impl>()){aptHook(&aptCookie,countAptEvent,nullptr);}
RaceBottomScreen::~RaceBottomScreen(){aptUnhook(&aptCookie);}
bool RaceBottomScreen::active() const {return impl_->target;}

static bool sdFile(const char* name){
    char path[96];std::snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/%s",name);
    FILE* f=std::fopen(path,"rb");if(f)std::fclose(f);return f!=nullptr;
}

bool RaceBottomScreen::load(Renderer& renderer,unsigned course){
    auto& m=*impl_;
    m.mapClip=!sdFile("no-map-clip.txt");m.floatingRows=!sdFile("no-floating-rows.txt");
    floatTop=1.f;
#ifndef MKW_RELEASE
    if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/rows-margin.txt","r")){float v;if(std::fscanf(f,"%f",&v)==1&&v>=0&&v<=8)floatTop=v;std::fclose(f);}
#endif
    endRace();m.keep=!sdFile("no-race-bottom-keep.txt");m.depthOff=!sdFile("no-bottom-depth-off.txt");m.depthFree=!sdFile("no-bottom-depth-free.txt");
    const auto* art=raceMapCourse(course);
    if(!art){unload(renderer);return false;}
    if(m.target&&m.course==course){
        // A restart of the same race: reset the presentation, keep the textures.
        m.started=false;m.blend=course>=32?1.f:0.f;m.overviewWanted=course>=32;m.standings=0;m.timerSlide=-1.f;m.rowsSet=false;
        return true;
    }
    unload(renderer);
    if(!renderer.enableBottom())return false;
    // Before the course textures take the VRAM that is left.
    renderer.shareBottomDepth(m.depthFree);
    m.target=true;m.course=course;
    m.quads.reset(new(std::nothrow) Renderer::MaterialQuad[2*QuadCapacity]);
    if(!m.quads){unload(renderer);return false;}
    const auto& c=*art;
    // DIAG A/B: the earlier 512 KB atlas, when the pack still has it.
    m.ui=&RaceMapUi;
    if(sdFile("no-bottom-atlas256.txt")&&renderer.loadVramTexture(m.atlas,RaceMapUiWithShadows.texture,GPU_CLAMP_TO_EDGE,renderQuality().bottomRgba4))
        m.ui=&RaceMapUiWithShadows;
    else renderer.loadVramTexture(m.atlas,RaceMapUi.texture,GPU_CLAMP_TO_EDGE,renderQuality().bottomRgba4);
    if(course>=32)renderer.loadVramTexture(m.scores,BattleBottomScoreTexture);
    if(course<32)renderer.loadVramTexture(m.map,c.texture);
    renderer.loadVramTexture(m.overview,c.overview);
    if(course>=32&&(!m.atlas.ready||!m.overview.ready||!m.scores.ready)){unload(renderer);return false;}
    // Floating rows need their own texture; without it the column comes back.
    if(m.floatingRows&&!renderer.loadVramTexture(m.rows,RaceRowsUi.texture,GPU_CLAMP_TO_EDGE,renderQuality().bottomRgba4)){
        std::fprintf(stderr,"standings rows load failed: %s\n",RaceRowsUi.texture);m.floatingRows=false;}
    const auto objectArt=mapObjectAtlas(course);
    if(objectArt.texture&&!renderer.loadVramTexture(m.objects,objectArt.texture))
        std::fprintf(stderr,"minimap object atlas load failed: %s\n",objectArt.texture);
    m.started=false;m.blend=course>=32?1.f:0.f;m.overviewWanted=course>=32;m.standings=0;m.timerSlide=-1.f;m.rowsSet=false;
    return true;
}

void RaceBottomScreen::unload(Renderer& renderer){
    auto& m=*impl_;
    endRace();
    if(!m.target&&!m.map.ready&&!m.overview.ready&&!m.atlas.ready&&!m.objects.ready&&!m.scores.ready&&!m.rows.ready&&!m.quads)return;
    // Nothing recorded may still sample these textures or read the quads.
    renderer.waitForGpu();
    m.map.reset();m.overview.reset();m.atlas.reset();m.objects.reset();m.scores.reset();m.rows.reset();
    m.quads.reset();
    renderer.releaseBottom();
    m.target=false;m.course=~0u;
}

void RaceBottomScreen::toggleView(){
    if(impl_->course>=32)return; // Battle is always the whole arena, including touch input.
    impl_->overviewWanted=!impl_->overviewWanted;
}

void RaceBottomScreen::describe(char* text,unsigned size) const {
    const auto& m=*impl_;
    std::snprintf(text,size,"bottom screen course=%d target=%d map=%lu overview=%lu atlas=%lu objects=%lu vram_free=%lu",
                  m.course==~0u?-1:int(m.course),int(m.target),(unsigned long)m.map.bytes(),(unsigned long)m.overview.bytes(),
                  (unsigned long)m.atlas.bytes(),(unsigned long)m.objects.bytes(),(unsigned long)vramSpaceFree());
}

bool RaceBottomScreen::compose(Renderer& renderer,const BottomView& view){
    auto& m=*impl_;m.miiMap=m.miiRow=false;m.outline=~0u;
    if(!m.target||!m.quads)return false;
    const auto& course=*raceMapCourse(m.course);
    // Animations advance by simulation ticks (presentation runs at 30 or 60 Hz).
    const unsigned elapsed=m.started?std::min<unsigned>(view.tick-m.lastTick,30u):0u;
    m.lastTick=view.tick;m.started=true;
    for(unsigned i=0;i<elapsed;++i){
        m.blend=std::clamp(m.blend+(m.overviewWanted?1.f:-1.f)/12.f,0.f,1.f);
        m.standings=std::clamp(m.standings+(view.standingsVisible?1.f:-1.f)/16.f,0.f,1.f);
        ++m.roulette;
    }
    if(view.timeTrial)m.standings=0.f; // Never retain a sliding column from another mode.
    const float columnSlide=view.timeTrial?0.f:smooth(m.standings);
    // Floating rows hide no strip of the screen: every map stays centred on its
    // full width (the column moved it right of its edge).
    const bool centred=view.timeTrial||(m.floatingRows&&m.rows.ready);
    const float mapWidth=centred?ScreenW:MapW;
    const float mapCenterX=centred?ScreenW*.5f:ScreenW*.5f+(MapCX-ScreenW*.5f)*columnSlide;
    // The opaque standings column covers x < columnEdge (drawn after the map).
    const float columnEdge=m.standings>0?Column-(1-smooth(m.standings))*(Column+4):0.f;
    const float clip=m.mapClip&&!m.floatingRows&&columnEdge>0?columnEdge:0.f;
    // CtrlRaceBase's authored 20-tick entrance: -1 -> 0 in .05 steps.
    m.timerSlide=view.timerVisible?std::min(0.f,m.timerSlide+float(elapsed)*.05f):-1.f;
    m.base=m.quads.get()+(renderer.recordingFrame()&1)*QuadCapacity;m.count=0;
    const bool atlas=m.atlas.ready;

    // 1. Opaque fills: the whole screen, then the standings column (no clear).
    m.add(m.ui->white,0,0,ScreenW,ScreenH,0x0b0f17ffu);
    const unsigned fills=m.count;

    // 2. The map: the view's world rectangle over the course texture. Clamped
    // sampling repeats the baked surround past the course's edges.
    const BottomRacer* local=nullptr;
    for(unsigned i=0;i<view.racers;++i)if(view.racer[i].local)local=&view.racer[i];
    const float frameW=course.x1-course.x0,frameH=course.z1-course.z0;
    const float detailed=course.unitsPerPixel,whole=std::max(frameW/mapWidth,frameH/ScreenH);
    const float b=m.course>=32?1.f:smooth(m.blend);
    const float upp=std::exp(std::log(detailed)*(1-b)+std::log(std::max(whole,detailed))*b);
    // Mario Kart 7 turns the close-up map with the kart so its heading points
    // up; the whole-course view stays north-up, and the turn fades with the zoom.
    const float s=view.mirror?-1.f:1.f;
    float turn=0.f;
    {
        const float hx=s*view.headingX,hz=view.headingZ;
        // Shortest way round, so the blend into the overview never spins a full turn.
        if(hx*hx+hz*hz>1e-8f)turn=std::remainder(-1.5707963f-std::atan2(hz,hx),6.2831853f)*(1-b);
    }
    const float rc=std::cos(turn),rs=std::sin(turn);
    float cx=local?local->x:(course.x0+course.x1)*.5f,cz=local?local->z:(course.z0+course.z1)*.5f;
    // North-up, Mario Kart 7 keeps its view inside the map: the player leaves
    // the centre near the course's edge instead of the view running off it.
    // Turning, the player stays centred (clamped sampling repeats the baked
    // surround past the course's edges).
    auto clampAxis=[](float c,float half,float lo,float hi){return hi-lo<=2*half?(lo+hi)*.5f:std::clamp(c,lo+half,hi-half);};
    {
        const float kx=clampAxis(cx,mapWidth*.5f*upp,course.x0,course.x1),kz=clampAxis(cz,ScreenH*.5f*upp,course.z0,course.z1);
        cx=kx+(cx-kx)*(1-b);cz=kz+(cz-kz)*(1-b);
    }
    cx+=((course.x0+course.x1)*.5f-cx)*b;cz+=((course.z0+course.z1)*.5f-cz)*b;
    // World (x,z) to screen: mirror x, turn about the player, scale.
    auto screenAt=[&](float x,float z,float& outX,float& outY){
        const float dx=s*(x-cx),dz=z-cz;
        outX=mapCenterX+(rc*dx-rs*dz)/upp;outY=MapCY+(rs*dx+rc*dz)/upp;
    };
    const Texture* mapTexture=b>.5f&&m.overview.ready?&m.overview:m.map.ready?&m.map:nullptr;
    const unsigned mapFirst=m.count;
    if(mapTexture&&m.count<QuadCapacity){
        auto& q=m.base[m.count++];
        // The screen's corners mapped back to the world (the inverse of
        // screenAt), so in Mirror the map flips about mapCenterX like the
        // markers do, and turns with them about the player. It starts at
        // the column's edge: the column hides everything left of it.
        for(unsigned k=0;k<4;++k){
            q.x[k]=(k&1)?ScreenW:clip;q.y[k]=(k&2)?ScreenH:0;
            const float ox=(q.x[k]-mapCenterX)*upp,oy=(q.y[k]-MapCY)*upp;
            const float wx=cx+s*(rc*ox+rs*oy),wz=cz+(-rs*ox+rc*oy);
            q.u[k]=(wx-course.x0)/frameW;q.v[k]=(wz-course.z0)/frameH;
            q.color[k]=0xffffffffu;
        }
    }
    const unsigned mapCount=m.count-mapFirst;

    // Live drivable-object silhouettes sit above the baked map, below markers.
    const unsigned objectFirst=m.count;
    const auto objectArt=mapObjectAtlas(m.course);
    if(m.objects.ready)for(unsigned i=0;i<std::min(view.mapObjects,BottomView::MaxMapObjects);++i){
        const auto& item=view.mapObject[i];if(item.art>=objectArt.count)continue;
        const auto& art=objectArt.art[item.art];
        float x[4],y[4];
        for(unsigned k=0;k<4;++k)screenAt(item.x[k],item.z[k],x[k],y[k]);
        if(*std::max_element(x,x+4)<0||*std::min_element(x,x+4)>ScreenW||
           *std::max_element(y,y+4)<0||*std::min_element(y,y+4)>ScreenH)continue;
        if(m.count>=QuadCapacity)break;
        auto& q=m.base[m.count++];
        for(unsigned k=0;k<4;++k){
            q.x[k]=x[k];q.y[k]=y[k];q.color[k]=0xffffffffu;
            q.u[k]=float(art.x+((k&1)?art.w:0))/objectArt.width;
            q.v[k]=float(art.y+((k&2)?art.h:0))/objectArt.height;
        }
    }
    unsigned objectCount=m.count-objectFirst;

    // 3. Everything else from the atlas, in drawing order.
    unsigned overlayFirst=m.count;
    auto onMap=[&](float x,float y,float margin){return x>-margin&&x<ScreenW+margin&&y>-margin&&y<ScreenH+margin;};
    if(atlas){
        // Start line (KMP checkpoint 0): Mario Kart 7's checker marker across the road.
        {
            float ax,ay,bx,by;
            screenAt(course.startLeft[0],course.startLeft[1],ax,ay);screenAt(course.startRight[0],course.startRight[1],bx,by);
            const float dx=bx-ax,dy=by-ay,length=std::sqrt(dx*dx+dy*dy);
            if(length>1.f&&(onMap(ax,ay,8)||onMap(bx,by,8))){
                const float thick=std::clamp(length/8.f,3.f,6.f);
                const unsigned tiles=std::clamp(unsigned(std::lround(length/thick)),1u,24u);
                const float ux=dx/length,uy=dy/length,nx=-uy*thick*.5f,ny=ux*thick*.5f;
                for(unsigned t=0;t<tiles;++t){
                    const float s0=length*float(t)/float(tiles),s1=length*float(t+1)/float(tiles);
                    const float x[4]={ax+ux*s0-nx,ax+ux*s1-nx,ax+ux*s0+nx,ax+ux*s1+nx};
                    const float y[4]={ay+uy*s0-ny,ay+uy*s1-ny,ay+uy*s0+ny,ay+uy*s1+ny};
                    m.addCorners(m.ui->mk7[unsigned(Mk7Marker::StartLine)],x,y,0xffffffffu);
                }
            }
        }
        // Items on the course: Mario Kart 7's map markers (the Wii fake box).
        for(unsigned i=0;i<std::min<unsigned>(view.items,BottomView::MaxItems);++i){
            const auto& it=view.item[i];
            float x,y;screenAt(it.x,it.z,x,y);
            if(it.marker==unsigned(BottomMarker::BlastRed)||it.marker==unsigned(BottomMarker::BlastBlue)){
                // A quarter ring texture, mirrored into the four quadrants.
                const float r=std::clamp(it.radius/upp,8.f,36.f);
                if(!onMap(x,y,r))continue;
                const auto& art=m.ui->mk7[it.marker];
                m.add(art,x-r,y-r,x,y,0xffffffffu,false,true);
                m.add(art,x,y-r,x+r,y,0xffffffffu,true,true);
                m.add(art,x-r,y,x,y+r,0xffffffffu,false,false);
                m.add(art,x,y,x+r,y+r,0xffffffffu,true,false);
                continue;
            }
            if(!onMap(x,y,MarkerSize))continue;
            const auto& art=it.marker==unsigned(BottomMarker::FakeBox)?m.ui->fakeBox:m.ui->mk7[it.marker];
            const float h=MarkerSize*.5f;
            m.add(art,x-h,y-h,x+h,y+h,0xffffffffu,false,it.marker!=unsigned(BottomMarker::FakeBox));
        }
        // Racers: the worst position first, the local racer last (retail z).
        std::array<const BottomRacer*,12> order{};unsigned n=0;
        for(unsigned i=0;i<std::min(view.racers,12u);++i)if(!view.racer[i].local)order[n++]=&view.racer[i];
        std::sort(order.begin(),order.begin()+n,[](const BottomRacer* a,const BottomRacer* b){return a->rank>b->rank;});
        if(local)order[n++]=local;
        for(unsigned i=0;i<n;++i){
            const auto& r=*order[i];
            float x,y;screenAt(r.x,r.z,x,y);
            const float scale=r.local?LocalScale:OtherScale;
            const float face=FaceUnits*scale*Unit;
            if(!onMap(x,y,face))continue;
            if(r.local){
                // The searchlight points along the kart, its apex on the face.
                const float mx=s*view.headingX,mz=view.headingZ;
                float hx=rc*mx-rs*mz,hy=rs*mx+rc*mz;const float l=std::sqrt(hx*hx+hy*hy);
                if(l>1e-4f){
                    hx/=l;hy/=l;const float px=-hy,py=hx;
                    const float tx=x+hx*LightH,ty=y+hy*LightH,w=LightW*.5f;
                    const float cxs[4]={tx-px*w,tx+px*w,x-px*w,x+px*w},cys[4]={ty-py*w,ty+py*w,y-py*w,y+py*w};
                    m.addCorners(m.ui->searchlight,cxs,cys,LightColor);
                }
            }
            const auto& art=m.ui->faces[std::min<unsigned>(r.character,23)];
            const uint32_t alpha=255;
            const float hf=face*.5f;
            // common_w039_map_chara_icon_{red,blue}: recolor only the
            // two shadow copies. White RGB + original alpha avoids tinting faces.
            const auto& silhouette=m.ui->faceShadows[std::min<unsigned>(r.character,23)];
            const uint32_t outline=(r.team==RaceTeam::Red?0xff000000u:r.team==RaceTeam::Blue?0x0000ff00u:0u)|alpha;
            auto portrait=[&](const RaceMapRect& rect,float half,uint32_t color){
                const float angle=view.mirror?-r.hitAngle:r.hitAngle; // spins turn the other way in Mirror
                const float c=std::cos(angle),sn=std::sin(angle);float xs[4],ys[4];
                for(unsigned j=0;j<4;++j){const float dx=(j&1)?half:-half,dy=(j&2)?half:-half;
                    xs[j]=x+c*dx-sn*dy;ys[j]=y+sn*dx+c*dy;}
                m.addCorners(rect,xs,ys,color);
            };
            const bool native=r.character>=24&&m.mii.ready;
            if(r.local&&!native){
                // The outline: white with the face's alpha, tinted. The 256x256
                // atlas draws it from the face with colour0 white; in black (no
                // team) the face's colour times black is the same black.
                const uint32_t tint=(outline&0xffffff00u)|80u;
                if(silhouette.w)portrait(silhouette,face*.53f,tint);
                else{portrait(art,face*.53f,tint);if(tint&0xffffff00u)m.outline=m.count-1;}
            }
            portrait(art,hf,0xffffff00u|alpha);
            if(native){auto& q=m.miiQuads[view.tick&1][0];q=m.base[--m.count];for(unsigned k=0;k<4;++k){q.u[k]=(k&1)?1.f:0.f;q.v[k]=(k&2)?1.f:0.f;}m.miiMap=true;}
        }
    }
    unsigned overlayCount=m.count-overlayFirst;
    if(clip>0){
        // Map objects and markers wholly left of the column's edge are hidden
        // by the opaque column: not drawn.
        auto under=[&](const Renderer::MaterialQuad& q){return std::max({q.x[0],q.x[1],q.x[2],q.x[3]})<=clip;};
        unsigned outline=~0u;
        auto keep=[&](unsigned first,unsigned count,unsigned to){
            unsigned w=to;for(unsigned i=first;i<first+count;++i)if(!under(m.base[i])){if(i==m.outline)outline=w;m.base[w++]=m.base[i];}return w-to;};
        objectCount=keep(objectFirst,objectCount,objectFirst);
        const unsigned first=objectFirst+objectCount;
        overlayCount=keep(overlayFirst,overlayCount,first);
        overlayFirst=first;m.count=first+overlayCount;m.outline=outline;
        if(m.miiMap&&under(m.miiQuads[view.tick&1][0]))m.miiMap=false;
    }

    // 3b. Floating standings rows (RaceRowsUi), each its own box over the map.
    const bool floatRows=m.floatingRows&&m.rows.ready&&m.standings>0;
    const unsigned rowsFirst=m.count;
    if(floatRows){
        const auto& R=RaceRowsUi;
        const float slide=(1-smooth(m.standings))*-(Column+4);
        if(!m.rowsSet){for(unsigned i=0;i<view.racers;++i)m.row[i]=float(std::clamp<unsigned>(view.racer[i].rank,1,12)-1);m.rowsSet=true;}
        for(unsigned i=0;i<std::min(view.racers,12u);++i){
            const float target=float(std::clamp<unsigned>(view.racer[i].rank,1,12)-1);
            for(unsigned t=0;t<elapsed;++t)m.row[i]+=(target-m.row[i])*.22f;
            if(std::fabs(target-m.row[i])<.01f)m.row[i]=target;
        }
        auto drawRow=[&](unsigned i){
            const auto& r=view.racer[i];
            // Faces and items sit 1 px above the box's centre: its border is heavier below.
            const float x=RowX+slide,y=floatRowY(m.row[i]),icon=R.icon,iy=y+std::floor((R.rowH-icon)*.5f)-1;
            m.addRow(r.local?R.player:R.other,x,y,x+R.rowW,y+R.rowH);
            if(!view.battle){
                const auto& ord=R.ordinals[std::clamp<unsigned>(r.rank,1,12)-1];
                const float oy=y+std::floor((R.rowH-ord.h)*.5f);
                m.addRow(ord,x+R.ordinalX,oy,x+R.ordinalX+ord.w,oy+ord.h);
            }else{
                // Battle: the score where the ordinal goes, in the team's colour.
                char text[4];const unsigned n=unsigned(std::snprintf(text,sizeof text,"%u",std::min(unsigned(r.score),999u)));
                const uint32_t color=r.team==RaceTeam::Red?0xff3030ffu:r.team==RaceTeam::Blue?0x4080ffffu:0xffffffffu;
                float dx=x+R.ordinalX;
                for(unsigned j=0;j<n;++j){const auto& d=R.digits[text[j]-'0'];const float dy=y+std::floor((R.rowH-d.h)*.5f);
                    m.addRow(d,dx,dy,dx+d.w,dy+d.h,color);dx+=d.w;}
            }
            m.addRow(R.faces[std::min<unsigned>(r.character,23)],x+R.faceX,iy,x+R.faceX+icon,iy+icon);
            if(r.character>=24&&m.mii.ready){auto& q=m.miiQuads[view.tick&1][1];q=m.base[--m.count];for(unsigned k=0;k<4;++k){q.u[k]=(k&1)?1.f:0.f;q.v[k]=(k&2)?1.f:0.f;}m.miiRow=true;}
            if(r.item<R.items.size()&&R.items[r.item].w)m.addRow(R.items[r.item],x+R.itemX,iy,x+R.itemX+icon,iy+icon);
        };
        for(unsigned i=0;i<std::min(view.racers,12u);++i)if(!view.racer[i].local)drawRow(i);
        for(unsigned i=0;i<std::min(view.racers,12u);++i)if(view.racer[i].local)drawRow(i);
    }
    const unsigned rowsCount=m.count-rowsFirst;

    // 4. The standings column and its rows over the map's left edge, then the time.
    const unsigned topFirst=m.count;
    if(m.standings>0){
        const float edge=columnEdge;
        if(edge>0&&!m.floatingRows){m.add(m.ui->white,0,0,edge,ScreenH,0x101626ffu);
            m.add(m.ui->white,std::max(0.f,edge-1),0,edge,ScreenH,0x5a6a8aff);}
    }
    if(atlas&&m.standings>0&&!floatRows){
        // Rows slide in from the left once the race starts and move between
        // places when positions change (Mario Kart 7).
        const float slide=(1-smooth(m.standings))*-(Column+4);
        if(!m.rowsSet){for(unsigned i=0;i<view.racers;++i)m.row[i]=float(std::clamp<unsigned>(view.racer[i].rank,1,12)-1);m.rowsSet=true;}
        for(unsigned i=0;i<std::min(view.racers,12u);++i){
            const float target=float(std::clamp<unsigned>(view.racer[i].rank,1,12)-1);
            for(unsigned t=0;t<elapsed;++t)m.row[i]+=(target-m.row[i])*.22f;
            if(std::fabs(target-m.row[i])<.01f)m.row[i]=target;
        }
        auto drawRow=[&](unsigned i){
            const auto& r=view.racer[i];
            const float x=RowX+slide,y=RowTop+m.row[i]*RowPitch;
            m.add(r.local?m.ui->rowPlayer:m.ui->rowOther,x,y,x+m.ui->rowPlayer.w,y+RowH);
            if(!view.battle){
                const auto& ord=m.ui->ordinals[std::clamp<unsigned>(r.rank,1,12)-1];
                const float oy=y+std::floor((RowH-ord.h)*.5f);
                m.add(ord,x+OrdinalX,oy,x+OrdinalX+ord.w,oy+ord.h);
            }
            m.add(m.ui->faces[std::min<unsigned>(r.character,23)],x+FaceX,y+1,x+FaceX+RowIcon,y+1+RowIcon);
            if(r.character>=24&&m.mii.ready){auto& q=m.miiQuads[view.tick&1][1];q=m.base[--m.count];for(unsigned k=0;k<4;++k){q.u[k]=(k&1)?1.f:0.f;q.v[k]=(k&2)?1.f:0.f;}m.miiRow=true;}
            if(r.item<m.ui->items.size()&&m.ui->items[r.item].w)m.add(m.ui->items[r.item],x+ItemX,y+1,x+ItemX+RowIcon,y+1+RowIcon);
        };
        for(unsigned i=0;i<std::min(view.racers,12u);++i)if(!view.racer[i].local)drawRow(i);
        for(unsigned i=0;i<std::min(view.racers,12u);++i)if(view.racer[i].local)drawRow(i);
    }
    if(atlas&&view.timerVisible){
        // CtrlRaceTime_refresh: minutes clamp at 99'59"999.
        unsigned ms=view.milliseconds,minutes=ms/60000,seconds=ms/1000%60,milli=ms%1000;
        if(minutes>99){minutes=99;seconds=59;milli=999;}
        const unsigned digit[7]={minutes/10,minutes%10,seconds/10,seconds%10,milli/100,milli/10%10,milli%10};
        // RaceTimerPanes: race_null, time_01, coron01, time_02, time_03, coron00, time_04..06, set_p.
        const float right=RaceTimerPanes[8].x+RaceTimerPanes[8].w*.5f;
        const float sq=m.timerSlide*m.timerSlide;
        const float lift=400.f*1.0005f*(sq>=.1f?sq-.1f:-sq)*TimerScale*Unit;
        // tt_time_E's alpha starts at pixel64 of120. Centre the visible
        // TIME + seven digit slots, retaining the original pane spacing/size.
        const auto& label=RaceTimerPanes[9];
        const float left=label.x-label.w*.5f+label.w*(64.f/120.f);
        const float centered=ScreenW*.5f-(left+right)*TimerScale*.5f;
        const float ox=centered+(TimerRight-right*TimerScale-centered)*columnSlide,oy=TimerTop+RaceTimerPanes[0].h*.5f*TimerScale-lift;
        const RaceMapRect* art[10]={&m.ui->digits[digit[0]],&m.ui->digits[digit[1]],&m.ui->colon,&m.ui->digits[digit[2]],&m.ui->digits[digit[3]],
                                    &m.ui->dot,&m.ui->digits[digit[4]],&m.ui->digits[digit[5]],&m.ui->digits[digit[6]],&m.ui->timeLabel};
        for(unsigned i=0;i<10&&m.count<QuadCapacity;++i){
            const auto& p=RaceTimerPanes[i];
            const float x=ox+p.x*TimerScale,y=oy-p.y*TimerScale,hw=p.w*.5f*TimerScale,hh=p.h*.5f*TimerScale;
            if(!m.add(*art[i],x-hw,y-hh,x+hw,y+hh))break;
            auto& q=m.base[m.count-1];
            for(unsigned k=0;k<4;++k)q.color[k]=TimerGold[k];
        }
    }
    const unsigned topCount=m.count-topFirst;
    // Battle scores use original-font glyphs baked at their actual LCD size,
    // with no padded 3D timer sprite squeezed down to a few visible pixels.
    const unsigned scoreFirst=m.count;
    if(view.battle&&m.scores.ready&&m.standings>0&&!floatRows){
        const float slide=(1-smooth(m.standings))*-(Column+4);
        for(unsigned i=0;i<std::min(view.racers,12u);++i){
            const auto& r=view.racer[i];char text[4];
            const unsigned n=unsigned(std::snprintf(text,sizeof text,"%u",std::min(unsigned(r.score),999u)));
            const float left=RowX+slide+(floatRows?RaceRowsUi.ordinalX:OrdinalX),y=(floatRows?floatRowY(m.row[i])+1.5f:RowTop+m.row[i]*RowPitch+2.f);
            for(unsigned j=0;j<n;++j)m.add(BattleBottomScoreDigits[text[j]-'0'],left+j*9.f,y,left+(j+1)*9.f,y+15.f,r.team==RaceTeam::Red?0xff3030ffu:r.team==RaceTeam::Blue?0x4080ffffu:0xffffffffu,false,false,true);
        }
    }
    const unsigned scoreCount=m.count-scoreFirst,fadeFirst=m.count;
    if(view.fade>0)m.add(m.ui->white,0,0,ScreenW,ScreenH,uint32_t(std::lround(view.fade*255.f)));
    // NW4R default material: lerp(C0 = transparent black, C1 = white, texture) x vertex colour.
    // The full-screen map hides the fill and the clear, and needs no blend:
    // race maps and overviews are ETC1 (no alpha), drawn in white. Battle
    // maps are RGBA4 and keep all three.
    const bool covered=skipCoveredFill&&mapCount&&m.course<32;
    m.b={fills,mapFirst,mapCount,objectFirst,objectCount,overlayFirst,overlayCount,topFirst,topCount,rowsFirst,rowsCount,scoreFirst,scoreCount,fadeFirst,view.tick&1,m.outline,mapTexture,covered,atlas};
    return true;
}

void RaceBottomScreen::submit(Renderer& renderer){
    auto& m=*impl_;const auto& b=m.b;
    if(b.atlas&&b.fills&&!b.covered)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base,b.fills,0x00000000u,0xffffffffu);
    if(!b.atlas&&!b.covered)renderer.drawScreenMaterialQuadsInPlace(nullptr,m.base,b.fills,0,0xffffffffu);
    if(b.mapCount)renderer.drawScreenMaterialQuadsInPlace(b.mapTexture,m.base+b.mapFirst,b.mapCount,0x00000000u,0xffffffffu,b.covered);
    if(b.covered)renderer.bottomCovered();
    if(b.objectCount)renderer.drawScreenMaterialQuadsInPlace(&m.objects,m.base+b.objectFirst,b.objectCount,0x00000000u,0xffffffffu);
    if(b.outline>=b.overlayFirst&&b.outline<b.overlayFirst+b.overlayCount){
        // lerp(white, white, face) = white RGB with the face's alpha, as its silhouette.
        const unsigned before=b.outline-b.overlayFirst,after=b.overlayCount-before-1;
        if(before)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+b.overlayFirst,before,0x00000000u,0xffffffffu);
        renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+b.outline,1,0xffffff00u,0xffffffffu);
        if(after)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+b.outline+1,after,0x00000000u,0xffffffffu);
    }else if(b.overlayCount)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+b.overlayFirst,b.overlayCount,0x00000000u,0xffffffffu);
    if(m.miiMap)renderer.drawScreenMaterialQuadsInPlace(&m.mii,&m.miiQuads[b.mii][0],1,0,0xffffffffu);
    if(b.rowsCount)renderer.drawScreenMaterialQuadsInPlace(&m.rows,m.base+b.rowsFirst,b.rowsCount,0x00000000u,0xffffffffu);
    if(b.topCount){
        if(b.atlas)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+b.topFirst,b.topCount,0x00000000u,0xffffffffu);
        else renderer.drawScreenMaterialQuadsInPlace(nullptr,m.base+b.topFirst,b.topCount,0,0xffffffffu);
    }
    if(m.miiRow)renderer.drawScreenMaterialQuadsInPlace(&m.mii,&m.miiQuads[b.mii][1],1,0,0xffffffffu);
    if(b.scoreCount)renderer.drawScreenMaterialQuadsInPlace(&m.scores,m.base+b.scoreFirst,b.scoreCount,0x00000000u,0xffffffffu);
    if(m.count>b.fadeFirst)renderer.drawScreenMaterialQuadsInPlace(b.atlas?&m.atlas:nullptr,m.base+b.fadeFirst,m.count-b.fadeFirst,0x00000000u,0xffffffffu);
}

void RaceBottomScreen::draw(Renderer& renderer,const BottomView& view){
    // Pause pages and the like: what follows on the lower LCD is not ours.
    impl_->shownValid=false;
    if(compose(renderer,view))submit(renderer);
}

// Everything the composed frame draws: its quads, batches and textures.
uint64_t RaceBottomScreen::frameHash() const{
    const auto& m=*impl_;const auto& b=m.b;
    uint32_t a=2166136261u,c=0x9e3779b9u;
    auto word=[&](uint32_t w){a=(a^w)*16777619u;c=(c^w)*0x27d4eb2du;c^=c>>15;};
    auto bytes=[&](const void* p,size_t n){const auto* q=static_cast<const uint8_t*>(p);
        for(size_t i=0;i+4<=n;i+=4){uint32_t w;std::memcpy(&w,q+i,4);word(w);}};
    for(uint32_t w:std::initializer_list<uint32_t>{b.fills,b.mapFirst,b.mapCount,b.objectFirst,b.objectCount,b.overlayFirst,b.overlayCount,b.topFirst,b.topCount,b.rowsFirst,b.rowsCount,
                    b.scoreFirst,b.scoreCount,b.fadeFirst,b.outline,m.count,unsigned(b.covered),unsigned(b.atlas),unsigned(m.miiMap),unsigned(m.miiRow),m.miiGeneration})word(w);
    word(uint32_t(reinterpret_cast<uintptr_t>(b.mapTexture)));
    static_assert(sizeof(Renderer::MaterialQuad)%4==0);
    bytes(m.base,m.count*sizeof(Renderer::MaterialQuad));
    if(m.miiMap)bytes(&m.miiQuads[b.mii][0],sizeof(Renderer::MaterialQuad));
    if(m.miiRow)bytes(&m.miiQuads[b.mii][1],sizeof(Renderer::MaterialQuad));
    return uint64_t(a)<<32|c;
}

void RaceBottomScreen::drawRace(Renderer& renderer,const BottomView& view){
    auto& m=*impl_;
    if(!compose(renderer,view))return;
    const uint64_t hash=frameHash();
    const uint32_t serial=renderer.frameSerial();
    const unsigned apt=aptEvents.load(std::memory_order_relaxed),scopes=loadingScopes();
    // A DIAG leave-out frame draws less than it composed, and a loading frame
    // ends black: neither leaves this picture on the lower LCD.
    const bool leaveOut=diag::passState.skip==diag::PassBottom||diag::passState.skip==diag::PassBottomQuads;
    const bool shown=!leaveOut&&!renderer.loadingFrame;
    // The previous frame showed exactly this picture and nothing has touched
    // the lower LCD or its target since: draw nothing, and skip the clear.
    if(m.keep&&shown&&m.shownValid&&serial==m.shownSerial+1&&apt==m.shownApt&&scopes==m.shownScopes&&hash==m.shownHash&&renderer.bottomCovered()){
        m.shownSerial=serial;++m.keptFrames;return;
    }
    renderer.beginBottom();
    if(m.depthOff)renderer.bottomNoDepth();
    submit(renderer);
    m.shownValid=shown;m.shownHash=hash;m.shownSerial=serial;m.shownApt=apt;m.shownScopes=scopes;++m.drawnFrames;
}

bool RaceBottomScreen::skipRace(Renderer& renderer){
    auto& m=*impl_;
    // A loading frame ends black; a gap or an APT event may have changed the LCD.
    if(!m.shownValid||renderer.loadingFrame||renderer.frameSerial()!=m.shownSerial+1||
       aptEvents.load(std::memory_order_relaxed)!=m.shownApt||loadingScopes()!=m.shownScopes)return false;
    if(!renderer.bottomCovered())return false;
    m.shownSerial=renderer.frameSerial();++m.skippedFrames;
    return true;
}
void RaceBottomScreen::endRace(){
    auto& m=*impl_;
#ifndef MKW_RELEASE
    if(m.keptFrames+m.drawnFrames)if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/runtime.log","a")){
        std::fprintf(f,"bottom keep: course=%d kept=%u drawn=%u skipped=%u keep=%d\n",m.course==~0u?-1:int(m.course),m.keptFrames,m.drawnFrames,m.skippedFrames,int(m.keep));std::fclose(f);}
#endif
    m.keptFrames=m.drawnFrames=m.skippedFrames=0;m.shownValid=false;
}
}
