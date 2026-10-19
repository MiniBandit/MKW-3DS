// Race bottom screen: see include/mkw/race_bottom_screen.hpp and docs/BOTTOM_SCREEN.md.
#include <mkw/race_bottom_screen.hpp>
#include <mkw/race_map_assets.hpp>
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
constexpr float OrdinalX=3.f,FaceX=32.f,ItemX=56.f,RowIcon=17.f;
// Retail CtrlRace2DMapCharacter (USA 0x807E1620/0x807E1B20): the face pane is
// 32 x 1.6 layout units, scaled 0.8 for the local racer and 0.6 for the
// others; the two black shadow copies 2.0/1.6 of it, 1.5 units either side;
// the others are drawn at alpha 160. One layout unit is 240/456 px here.
constexpr float Unit=240.f/456.f;
constexpr float FaceUnits=32.f*1.6f;
constexpr float LocalScale=.8f,LocalShadowScale=.85f,OtherScale=.6f,OtherShadowScale=.65f;
constexpr uint32_t OtherAlpha=160;
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
static_assert(RaceBottomScreen::QuadCapacity==220&&RaceBottomScreen::QuadCapacity<=unsigned(Renderer::BottomQuadCapacity),
              "every quad a frame can add fits the renderer's bottom sprite region");

struct RaceBottomScreen::Impl {
    Texture map,overview,atlas;
    unsigned course=~0u;
    bool target=false;
    std::unique_ptr<Renderer::MaterialQuad[]> quads; // 2 x QuadCapacity, alternating per frame
    // Animation state (60 Hz ticks).
    uint32_t lastTick=0;bool started=false;
    float blend=0;bool overviewWanted=false;     // 0 detailed .. 1 whole course
    float standings=0;                           // slide-in 0..1
    std::array<float,12> row{};bool rowsSet=false;
    unsigned roulette=0;
    // One frame's quads, in three in-place batches.
    Renderer::MaterialQuad* base=nullptr;unsigned count=0;
    bool add(const RaceMapRect& r,float x0,float y0,float x1,float y1,uint32_t color=0xffffffffu,bool mirrorU=false,bool mirrorV=false){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=RaceMapAtlasSize[0],ah=RaceMapAtlasSize[1];
        float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        if(mirrorU)std::swap(u0,u1);
        if(mirrorV)std::swap(v0,v1);
        auto& q=base[count++];
        q.x[0]=x0;q.y[0]=y0;q.x[1]=x1;q.y[1]=y0;q.x[2]=x0;q.y[2]=y1;q.x[3]=x1;q.y[3]=y1;
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        for(auto& c:q.color)c=color;
        return true;
    }
    // A quad with free corners (TL, TR, BL, BR) over atlas rectangle r.
    bool addCorners(const RaceMapRect& r,const float (&x)[4],const float (&y)[4],uint32_t color){
        if(count>=QuadCapacity||!r.w)return false;
        const float aw=RaceMapAtlasSize[0],ah=RaceMapAtlasSize[1];
        const float u0=r.x/aw,u1=(r.x+r.w)/aw,v0=r.y/ah,v1=(r.y+r.h)/ah;
        auto& q=base[count++];
        for(unsigned k=0;k<4;++k){q.x[k]=x[k];q.y[k]=y[k];q.color[k]=color;}
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
        const auto& p=K::Kart::KartObjectManager::Instance()->object(i)->pos();
        auto& r=v.racer[i];r.x=p.x;r.z=p.z;r.local=i==0;
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
    {const auto& f=K::Kart::KartObjectManager::Instance()->object(0)->move()->smoothedForward();v.headingX=f.x;v.headingZ=f.z;}
    for(const auto& a:items.actors()){
        if(!a.active||v.items>=v.item.size())continue;
        int marker=-1;
        if(a.explosion)marker=a.kind==ItemKind::BlueShell?int(BottomMarker::BlastBlue):a.kind==ItemKind::Bobomb?int(BottomMarker::BlastRed):-1;
        else if(a.kind==ItemKind::Banana)marker=int(BottomMarker::Banana);
        else if(a.kind==ItemKind::GreenShell)marker=int(BottomMarker::GreenShell);
        else if(a.kind==ItemKind::RedShell)marker=int(BottomMarker::RedShell);
        else if(a.kind==ItemKind::BlueShell)marker=int(BottomMarker::SpinyShell);
        else if(a.kind==ItemKind::Bobomb)marker=int(BottomMarker::Bobomb);
        else if(a.kind==ItemKind::FakeBox)marker=int(BottomMarker::FakeBox);
        if(marker<0)continue;
        const auto p=a.visualPosition();auto& it=v.item[v.items++];
        it.x=p.x;it.z=p.z;it.marker=uint8_t(marker);it.radius=a.radius; // a blast's live retail radius
    }
    const unsigned stage=unsigned(race->stage());
    const auto& me=race->player(0);
    const auto& timer=me.finished()?me.raceTimer():race->timerManager().currentTimer();
    v.milliseconds=uint32_t(timer.min)*60000u+uint32_t(timer.sec)*1000u+uint32_t(timer.mil);
    // CtrlRaceTime shows with the HUD from the countdown; Mario Kart 7 shows
    // its standings once the race has started.
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
        m.started=false;m.blend=0;m.overviewWanted=false;m.standings=0;m.rowsSet=false;
        return true;
    }
    unload(renderer);
    if(!renderer.enableBottom())return false;
    m.target=true;m.course=course;
    m.quads.reset(new(std::nothrow) Renderer::MaterialQuad[2*QuadCapacity]);
    if(!m.quads){unload(renderer);return false;}
    const auto& c=RaceMapCourses[course];
    renderer.loadVramTexture(m.atlas,RaceMapAtlasTexture);
    renderer.loadVramTexture(m.map,c.texture);
    renderer.loadVramTexture(m.overview,c.overview);
    m.started=false;m.blend=0;m.overviewWanted=false;m.standings=0;m.rowsSet=false;
    return true;
}

void RaceBottomScreen::unload(Renderer& renderer){
    auto& m=*impl_;
    if(!m.target&&!m.map.ready&&!m.overview.ready&&!m.atlas.ready&&!m.quads)return;
    // Nothing recorded may still sample these textures or read the quads.
    renderer.waitForGpu();
    m.map.reset();m.overview.reset();m.atlas.reset();
    m.quads.reset();
    renderer.releaseBottom();
    m.target=false;m.course=~0u;
}

void RaceBottomScreen::toggleView(){impl_->overviewWanted=!impl_->overviewWanted;}

void RaceBottomScreen::describe(char* text,unsigned size) const {
    const auto& m=*impl_;
    std::snprintf(text,size,"bottom screen course=%d target=%d map=%lu overview=%lu atlas=%lu vram_free=%lu",
                  m.course==~0u?-1:int(m.course),int(m.target),(unsigned long)m.map.bytes(),(unsigned long)m.overview.bytes(),
                  (unsigned long)m.atlas.bytes(),(unsigned long)vramSpaceFree());
}

void RaceBottomScreen::draw(Renderer& renderer,const BottomView& view){
    auto& m=*impl_;
    if(!m.target||!m.quads)return;
    const auto& course=RaceMapCourses[m.course];
    // Animations advance by simulation ticks (presentation runs at 30 or 60 Hz).
    const unsigned elapsed=m.started?std::min<unsigned>(view.tick-m.lastTick,30u):0u;
    m.lastTick=view.tick;m.started=true;
    // The fly-over runs before the race's clock: its tour shows the whole course at once.
    if(view.tour)m.blend=1;
    for(unsigned i=0;i<elapsed;++i){
        m.blend=std::clamp(m.blend+((m.overviewWanted||view.tour)?1.f:-1.f)/12.f,0.f,1.f); // the tour shows the whole course
        m.standings=std::clamp(m.standings+(view.standingsVisible?1.f:-1.f)/16.f,0.f,1.f);
        ++m.roulette;
    }
    m.base=m.quads.get()+(renderer.recordingFrame()&1)*QuadCapacity;m.count=0;
    const bool atlas=m.atlas.ready;

    // 1. Opaque fills: the whole screen, then the standings column (no clear).
    m.add(RaceMapWhite,0,0,ScreenW,ScreenH,0x0b0f17ffu);
    const unsigned fills=m.count;

    // 2. The map: the view's world rectangle over the course texture. Clamped
    // sampling repeats the baked surround past the course's edges.
    const BottomRacer* local=nullptr;
    for(unsigned i=0;i<view.racers;++i)if(view.racer[i].local)local=&view.racer[i];
    const float frameW=course.x1-course.x0,frameH=course.z1-course.z0;
    const float detailed=course.unitsPerPixel,whole=std::max(frameW/MapW,frameH/ScreenH);
    const float b=smooth(m.blend);
    const float upp=std::exp(std::log(detailed)*(1-b)+std::log(std::max(whole,detailed))*b);
    float cx=local?local->x:(course.x0+course.x1)*.5f,cz=local?local->z:(course.z0+course.z1)*.5f;
    // Mario Kart 7 keeps its view inside the map: the player leaves the centre
    // near the course's edge instead of the view running off it.
    auto clampAxis=[](float c,float half,float lo,float hi){return hi-lo<=2*half?(lo+hi)*.5f:std::clamp(c,lo+half,hi-half);};
    cx=clampAxis(cx,MapW*.5f*upp,course.x0,course.x1);cz=clampAxis(cz,ScreenH*.5f*upp,course.z0,course.z1);
    cx+=((course.x0+course.x1)*.5f-cx)*b;cz+=((course.z0+course.z1)*.5f-cz)*b;
    auto screenX=[&](float x){return MapCX+(x-cx)/upp;};
    auto screenY=[&](float z){return MapCY+(z-cz)/upp;};
    const Texture* mapTexture=b>.5f&&m.overview.ready?&m.overview:m.map.ready?&m.map:nullptr;
    const unsigned mapFirst=m.count;
    if(mapTexture&&m.count<QuadCapacity){
        auto& q=m.base[m.count++];
        const float wx0=cx-MapW*.5f*upp,wx1=cx+MapW*.5f*upp,wz0=cz-ScreenH*.5f*upp,wz1=cz+ScreenH*.5f*upp;
        const float u0=(wx0-course.x0)/frameW,u1=(wx1-course.x0)/frameW,v0=(wz0-course.z0)/frameH,v1=(wz1-course.z0)/frameH;
        q.x[0]=MapX;q.y[0]=0;q.x[1]=ScreenW;q.y[1]=0;q.x[2]=MapX;q.y[2]=ScreenH;q.x[3]=ScreenW;q.y[3]=ScreenH;
        q.u[0]=u0;q.v[0]=v0;q.u[1]=u1;q.v[1]=v0;q.u[2]=u0;q.v[2]=v1;q.u[3]=u1;q.v[3]=v1;
        for(auto& c:q.color)c=0xffffffffu;
    }
    const unsigned mapCount=m.count-mapFirst;

    // 3. Everything else from the atlas, in drawing order.
    const unsigned overlayFirst=m.count;
    auto onMap=[&](float x,float y,float margin){return x>MapX-margin&&x<ScreenW+margin&&y>-margin&&y<ScreenH+margin;};
    if(atlas){
        // Start line (KMP checkpoint 0): Mario Kart 7's checker marker across the road.
        {
            const float ax=screenX(course.startLeft[0]),ay=screenY(course.startLeft[1]);
            const float bx=screenX(course.startRight[0]),by=screenY(course.startRight[1]);
            const float dx=bx-ax,dy=by-ay,length=std::sqrt(dx*dx+dy*dy);
            if(length>1.f&&(onMap(ax,ay,8)||onMap(bx,by,8))){
                const float thick=std::clamp(length/8.f,3.f,6.f);
                const unsigned tiles=std::clamp(unsigned(std::lround(length/thick)),1u,24u);
                const float ux=dx/length,uy=dy/length,nx=-uy*thick*.5f,ny=ux*thick*.5f;
                for(unsigned t=0;t<tiles;++t){
                    const float s0=length*float(t)/float(tiles),s1=length*float(t+1)/float(tiles);
                    const float x[4]={ax+ux*s0-nx,ax+ux*s1-nx,ax+ux*s0+nx,ax+ux*s1+nx};
                    const float y[4]={ay+uy*s0-ny,ay+uy*s1-ny,ay+uy*s0+ny,ay+uy*s1+ny};
                    m.addCorners(RaceMapMk7[unsigned(Mk7Marker::StartLine)],x,y,0xffffffffu);
                }
            }
        }
        // Items on the course: Mario Kart 7's map markers (the Wii fake box).
        for(unsigned i=0;i<std::min<unsigned>(view.items,BottomView::MaxItems);++i){
            const auto& it=view.item[i];
            const float x=screenX(it.x),y=screenY(it.z);
            if(it.marker==unsigned(BottomMarker::BlastRed)||it.marker==unsigned(BottomMarker::BlastBlue)){
                // A quarter ring texture, mirrored into the four quadrants.
                const float r=std::clamp(it.radius/upp,8.f,36.f);
                if(!onMap(x,y,r))continue;
                const auto& art=RaceMapMk7[it.marker];
                m.add(art,x-r,y-r,x,y,0xffffffffu,false,false);
                m.add(art,x,y-r,x+r,y,0xffffffffu,true,false);
                m.add(art,x-r,y,x,y+r,0xffffffffu,false,true);
                m.add(art,x,y,x+r,y+r,0xffffffffu,true,true);
                continue;
            }
            if(!onMap(x,y,MarkerSize))continue;
            const auto& art=it.marker==unsigned(BottomMarker::FakeBox)?RaceMapFakeBox:RaceMapMk7[it.marker];
            const float h=MarkerSize*.5f;
            m.add(art,x-h,y-h,x+h,y+h);
        }
        // Racers: the worst position first, the local racer last (retail z).
        std::array<const BottomRacer*,12> order{};unsigned n=0;
        for(unsigned i=0;i<std::min(view.racers,12u);++i)if(!view.racer[i].local)order[n++]=&view.racer[i];
        std::sort(order.begin(),order.begin()+n,[](const BottomRacer* a,const BottomRacer* b){return a->rank>b->rank;});
        if(local)order[n++]=local;
        for(unsigned i=0;i<n;++i){
            const auto& r=*order[i];
            const float x=screenX(r.x),y=screenY(r.z);
            const float scale=r.local?LocalScale:OtherScale,shadowScale=r.local?LocalShadowScale:OtherShadowScale;
            const float face=FaceUnits*scale*Unit,shadow=32.f*2.f*shadowScale*Unit,offset=1.5f*shadowScale*Unit;
            if(!onMap(x,y,face))continue;
            if(r.local){
                // The searchlight points along the kart, its apex on the face.
                float hx=view.headingX,hy=view.headingZ;const float l=std::sqrt(hx*hx+hy*hy);
                if(l>1e-4f){
                    hx/=l;hy/=l;const float px=-hy,py=hx;
                    const float tx=x+hx*LightH,ty=y+hy*LightH,w=LightW*.5f;
                    const float cxs[4]={tx-px*w,tx+px*w,x-px*w,x+px*w},cys[4]={ty-py*w,ty+py*w,y-py*w,y+py*w};
                    m.addCorners(RaceMapSearchlight,cxs,cys,LightColor);
                }
            }
            const auto& art=RaceMapFaces[std::min<unsigned>(r.character,23)];
            const uint32_t alpha=r.local?255:OtherAlpha;
            const float hs=shadow*.5f,hf=face*.5f;
            m.add(art,x-hs-offset,y-hs,x+hs-offset,y+hs,alpha);
            m.add(art,x-hs+offset,y-hs,x+hs+offset,y+hs,alpha);
            m.add(art,x-hf,y-hf,x+hf,y+hf,0xffffff00u|alpha);
        }
    }
    // The tour's camera: a marker on the map with the viewing cone along its direction.
    if(atlas&&view.tour){
        const float x=screenX(view.tourX),y=screenY(view.tourZ);
        float hx=view.tourHeadX,hy=view.tourHeadZ;const float l=std::sqrt(hx*hx+hy*hy);
        if(l>1e-4f&&onMap(x,y,24)){
            hx/=l;hy/=l;const float px=-hy,py=hx;
            const float tx=x+hx*LightH*1.4f,ty=y+hy*LightH*1.4f,w=LightW*.7f;
            const float cxs[4]={tx-px*w,tx+px*w,x-px*w,x+px*w},cys[4]={ty-py*w,ty+py*w,y-py*w,y+py*w};
            m.addCorners(RaceMapSearchlight,cxs,cys,LightColor);
        }
        if(onMap(x,y,6)){m.add(RaceMapWhite,x-4,y-4,x+4,y+4,0x000000ffu);m.add(RaceMapWhite,x-3,y-3,x+3,y+3,0xff3030ffu);}
    }
    const unsigned overlayCount=m.count-overlayFirst;

    // 4. The standings column and its rows over the map's left edge, then the time.
    const unsigned topFirst=m.count;
    m.add(RaceMapWhite,0,0,Column,ScreenH,0x101626ffu);
    m.add(RaceMapWhite,Column-1,0,Column,ScreenH,0x5a6a8aff);
    if(atlas&&m.standings>0){
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
            m.add(r.local?RaceMapRowPlayer:RaceMapRowOther,x,y,x+RaceMapRowPlayer.w,y+RowH);
            const auto& ord=RaceMapOrdinals[std::clamp<unsigned>(r.rank,1,12)-1];
            const float oy=y+std::floor((RowH-ord.h)*.5f);
            m.add(ord,x+OrdinalX,oy,x+OrdinalX+ord.w,oy+ord.h);
            m.add(RaceMapFaces[std::min<unsigned>(r.character,23)],x+FaceX,y+1,x+FaceX+RowIcon,y+1+RowIcon);
            if(r.item<RaceMapItems.size()&&RaceMapItems[r.item].w)m.add(RaceMapItems[r.item],x+ItemX,y+1,x+ItemX+RowIcon,y+1+RowIcon);
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
        const float ox=TimerRight-right*TimerScale,oy=TimerTop+RaceTimerPanes[0].h*.5f*TimerScale;
        const RaceMapRect* art[10]={&RaceMapDigits[digit[0]],&RaceMapDigits[digit[1]],&RaceMapColon,&RaceMapDigits[digit[2]],&RaceMapDigits[digit[3]],
                                    &RaceMapDot,&RaceMapDigits[digit[4]],&RaceMapDigits[digit[5]],&RaceMapDigits[digit[6]],&RaceMapTimeLabel};
        for(unsigned i=0;i<10&&m.count<QuadCapacity;++i){
            const auto& p=RaceTimerPanes[i];
            const float x=ox+p.x*TimerScale,y=oy-p.y*TimerScale,hw=p.w*.5f*TimerScale,hh=p.h*.5f*TimerScale;
            if(!m.add(*art[i],x-hw,y-hh,x+hw,y+hh))break;
            auto& q=m.base[m.count-1];
            for(unsigned k=0;k<4;++k)q.color[k]=TimerGold[k];
        }
    }
    if(view.fade>0)m.add(RaceMapWhite,0,0,ScreenW,ScreenH,uint32_t(std::lround(view.fade*255.f)));
    const unsigned topCount=m.count-topFirst;
    // NW4R default material: lerp(C0 = transparent black, C1 = white, texture) x vertex colour.
    if(atlas&&fills)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base,fills,0x00000000u,0xffffffffu);
    if(!atlas)renderer.drawScreenMaterialQuadsInPlace(nullptr,m.base,fills,0,0xffffffffu);
    if(mapCount)renderer.drawScreenMaterialQuadsInPlace(mapTexture,m.base+mapFirst,mapCount,0x00000000u,0xffffffffu);
    if(overlayCount)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+overlayFirst,overlayCount,0x00000000u,0xffffffffu);
    if(topCount){
        if(atlas)renderer.drawScreenMaterialQuadsInPlace(&m.atlas,m.base+topFirst,topCount,0x00000000u,0xffffffffu);
        else renderer.drawScreenMaterialQuadsInPlace(nullptr,m.base+topFirst,topCount,0,0xffffffffu);
    }
}
}
