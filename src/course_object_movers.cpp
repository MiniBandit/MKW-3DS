#include "mkw/course_object_kinds.hpp"
#include "mkw/render_quality.hpp"
#include <game/field/RailInterpolator.hh>
#include <game/field/RailManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/RaceManager.hh>
#include <algorithm>
#include <cmath>
#include <cstring>

// Route followers for placements Kinoko does not simulate. Each owns rail
// interpolators on the rails Kinoko's RailManager builds for every POTI
// route, so the simulation is only read. Motion follows the retail classes
// (RMCP addresses below) and starts on the first frame the object is drawn.
namespace mkw {
namespace K=Kinoko;
namespace {
constexpr float Pi=3.14159265f;
Vec3 vec(const K::EGG::Vector3f& v){return {v.x,v.y,v.z};}
bool named(const Visual& v,const char* name){return !std::strcmp(v.name,name);}
Vec3 placementScale(const Visual& v){const auto& s=v.geo->scale();return {s.x,s.y,s.z};}
Tracks find(const ObjectAssets& assets,const char* resource,const char* model,const char* tracks=nullptr){return assets.tracks(assets.find(resource,model),tracks);}
int clip(const Tracks& t,const char* name){return t.model&&t.model->bank?t.model->bank->clip(name):-1;}
int pattern(const Tracks& t,const char* name){return t.model&&t.model->bank?t.model->bank->pattern(name):-1;}
// Deterministic stand-in for the race RNG retail draws flock offsets from.
uint32_t nextRandom(uint32_t& state){state=state*1664525u+1013904223u;return state>>8;}
float randomUnit(uint32_t& state){return float(nextRandom(state)&0xffff)/65536.f;}

std::shared_ptr<K::Field::RailInterpolator> makeRail(int path,float speed,unsigned start=0){
    auto* rails=K::Field::RailManager::Instance();
    const auto* map=K::System::CourseMap::Instance();
    if(path<0||!rails||!map||unsigned(path)>=map->getPointInfoCount())return nullptr;
    std::shared_ptr<K::Field::RailInterpolator> r;
    if(map->getPointInfo(path)->setting(0)==0)r=std::make_shared<K::Field::RailLinearInterpolator>(speed,unsigned(path));
    else r=std::make_shared<K::Field::RailSmoothInterpolator>(speed,unsigned(path));
    // Like retail objects, init again: the constructor's init runs before
    // the speed is set, leaving a smooth rail's segment velocity at zero.
    if(start>=r->pointCount())start=0;
    r->init(0.f,start);
    return r;
}
// Simulation ticks to advance this frame.
unsigned advance(ObjectDraw& d,Visual& v){
    if(!v.railStarted||d.tick<v.railTick){v.railStarted=true;v.railTick=d.tick;return 0;}
    const unsigned steps=std::min(d.tick-v.railTick,600u);
    v.railTick=d.tick;
    return steps;
}
// Kinoko ObjectBase::OrthonormalBasis: z along h, x level.
C3D_Mtx orthonormal(Vec3 h,Vec3 p,Vec3 scale){
    Vec3 z=h;if(std::fabs(z.y)<.001f)z.y=.001f;
    const Vec3 flat=normalized(Vec3{h.x,0,h.z},{0,0,1});
    Vec3 x=cross(flat,z);if(z.y>0)x=x*-1.f;
    x=normalized(x,{1,0,0});
    return basisMatrix(x,cross(z,x),z,p,scale);
}
// Kinoko ObjectBase::SetRotTangentHorizontal with up +Y: a level heading.
C3D_Mtx level(Vec3 h,Vec3 p,Vec3 scale){
    const Vec3 up{0,1,0};
    const Vec3 z=normalized(h-up*dot(h,up),{0,0,1});
    return basisMatrix(cross(up,z),up,z,p,scale);
}
void drawn(Visual& v,const C3D_Mtx& m){v.drawn=m;v.drawnValid=true;}

// Luigi Circuit's airship and the Mario Circuit / GCN Mario Circuit balloons
// share one retail class (RMCP 806dd2d8, calc 806dd5e0): the route at
// setting 0 speed, a heading eased 1% per frame toward the route (kept
// nearly level), and a bob of setting 1 units over 900 frames (500 for
// Mario Circuit's balloon, which keeps its placement heading).
void drawFlyer(ObjectDraw& d,Visual& v){
    if(v.rails.empty()){
        auto r=makeRail(v.geo->pathId(),float(v.geo->setting(0)));if(!r)return;
        v.heading=vec(r->curTangentDir());v.rails.push_back(r);
    }
    auto& r=*v.rails[0];
    const bool castle=named(v,"castleballoon1");
    for(unsigned n=advance(d,v);n;--n){
        r.calc();
        if(!castle){
            Vec3 t=vec(r.curTangentDir());if(std::fabs(t.y)>.1f)t.y=.01f;
            v.heading=normalized(lerp(v.heading,t,.01f),{0,0,1});
        }
        ++v.frames;
    }
    const float period=castle?500.f:900.f;
    Vec3 p=vec(r.curPos());
    p.y+=float(int16_t(v.geo->setting(1)))*std::sin(2.f*Pi*float(v.frames?v.frames-1:0)/period);
    C3D_Mtx m;
    if(castle){m=v.placement;m.r[0].w=p.x;m.r[1].w=p.y;m.r[2].w=p.z;}
    else m=orthonormal(v.heading,p,placementScale(v));
    const bool far=v.low.model&&length(p-d.camera)>renderQuality().objectLow(d.stereo);
    d.submit(far?v.low.model:v.main.model,m,{});
    if(v.shadow.model)d.shadow(v.shadow.model,m);
    drawn(v,m);
}
// Daisy Circuit / Koopa Cape cruising yachts (RMCP 80886574): the route at
// setting 0 speed facing along it, bobbing 50 units over 180 frames with
// each successive yacht 90 frames later.
void drawCruiser(ObjectDraw& d,Visual& v){
    if(v.rails.empty()){
        auto r=makeRail(v.geo->pathId(),float(v.geo->setting(0)));if(!r)return;
        v.heading=vec(r->curTangentDir());v.rails.push_back(r);
        // Retail numbers the yachts in construction order.
        int order=0;
        const auto* map=K::System::CourseMap::Instance();
        for(unsigned i=0;i<v.index;++i){const auto* g=map->getGeoObj(i);if(g&&g->id()==v.geo->id()&&(g->presenceFlag()&1))++order;}
        v.picks={order};
    }
    auto& r=*v.rails[0];
    for(unsigned n=advance(d,v);n;--n){r.calc();v.heading=normalized(lerp(v.heading,vec(r.curTangentDir()),.1f),{0,0,1});}
    const auto* race=K::System::RaceManager::Instance();
    const float t=float(unsigned(v.picks[0])*90u+(race?race->timer():0u));
    Vec3 p=vec(r.curPos());p.y+=50.f*std::sin(2.f*t*Pi/180.f);
    const C3D_Mtx m=level(v.heading,p,placementScale(v));
    d.submit(v.main.model,m,animAt(v.main,float(d.tick)));
    if(v.shadow.model)d.shadow(v.shadow.model,m);
    drawn(v,m);
}
// Koopa Cape's moray eel (RMCP 80887be4): its smooth route at setting 0
// speed, turning 5% per frame toward the route, swim clip at speed/35.
void drawMoray(ObjectDraw& d,Visual& v){
    if(v.rails.empty()){
        auto r=makeRail(v.geo->pathId(),float(v.geo->setting(0)));if(!r)return;
        v.heading=vec(r->curTangentDir());v.rails.push_back(r);v.phase=0;
    }
    auto& r=*v.rails[0];
    for(unsigned n=advance(d,v);n;--n){
        r.calc();
        v.heading=normalized(lerp(v.heading,vec(r.curTangentDir()),.05f),{0,0,1});
        v.phase+=r.currVel()/35.f;
    }
    const C3D_Mtx m=level(v.heading,vec(r.curPos()),placementScale(v));
    Anim a;a.clip=v.clips[0];a.frame=v.phase;
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
    drawn(v,m);
}
// Moonview Highway Bob-omb cars without a Kinoko object (Kinoko builds them
// for races, as retail ObjectCarTGE car type 2, and never in time trials;
// src/course_object_kinds.cpp draws those). Ported here: start point setting 0; local and highway speeds
// (settings 1/2) switched where the route's speed marks change; the body
// trails the rail point, its heading eased 10% per frame.
void drawBombCar(ObjectDraw& d,Visual& v){
    const float local=float(v.geo->setting(1)),highway=float(v.geo->setting(2));
    if(v.rails.empty()){
        const unsigned start=v.geo->setting(0);
        auto r=makeRail(v.geo->pathId(),float(start),start);if(!r)return;
        const unsigned mark=r->curPoint().setting[1];
        if(mark==1)r->setCurrVel(highway);else if(mark==0)r->setCurrVel(local);
        const unsigned next=r->nextPoint().setting[1];
        v.moverState=mark==0&&next==1?1:mark==1&&next==0?2:0;
        v.speed=r->speed();v.heading={0,0,0};v.rails.push_back(r);
    }
    auto& r=*v.rails[0];
    const float trail=1500.f*v.geo->scale().z*.5f;
    for(unsigned n=advance(d,v);n;--n){
        if(v.moverState==1){v.speed+=200.f;if(v.speed>highway){v.speed=highway;v.moverState=0;}r.setCurrVel(v.speed);}
        else if(v.moverState==2){v.speed-=200.f;if(v.speed<local){v.speed=local;v.moverState=0;}r.setCurrVel(v.speed);}
        if(r.calc()==K::Field::RailInterpolator::Status::SegmentEnd){
            const unsigned cur=r.curPoint().setting[1],next=r.nextPoint().setting[1];
            if(cur==0&&next==1)v.moverState=1;else if(cur==1&&next==0)v.moverState=2;
        }
        K::EGG::Vector3f behind,tangent;
        r.evalCubicBezierOnPath(trail,behind,tangent);
        const Vec3 delta=normalized(vec(r.curPos())-vec(behind),{0,0,1});
        v.heading=v.heading+(delta-v.heading)*.1f;v.heading.y=delta.y;
        v.heading=normalized(v.heading,{0,0,1});
    }
    Vec3 p=vec(r.curPos())-v.heading*(trail*.5f);
    if(v.heading.y<=-.05f)p.y-=5.f;
    // setMatrixTangentTo(OrthonormalBasis(heading).y, heading)
    const C3D_Mtx basis=orthonormal(v.heading,{0,0,0},{1,1,1});
    const Vec3 up=normalized(Vec3{basis.r[0].c[2],basis.r[1].c[2],basis.r[2].c[2]},{0,1,0});
    const Vec3 z=normalized(v.heading-up*dot(v.heading,up),{0,0,1});
    const C3D_Mtx m=basisMatrix(cross(up,z),up,z,p,placementScale(v));
    d.submit(v.main.model,m,{});
    if(v.extra[0].model)d.submit(v.extra[0].model,m,animAt(v.extra[0],float(d.tick)));
    if(v.extra[1].model)d.submit(v.extra[1].model,m,animAt(v.extra[1],float(d.tick)));
    if(v.shadow.model)d.shadow(v.shadow.model,m);
    drawn(v,m);
}
// N64 Sherbet Land's big penguin: retail's walking-penguin base (Kinoko
// ObjectPenguin): its route at setting 0 speed, heading eased 20% per frame.
void drawBigPenguin(ObjectDraw& d,Visual& v){
    if(v.rails.empty()){
        auto r=makeRail(v.geo->pathId(),float(v.geo->setting(0)));if(!r)return;
        v.heading={0,0,1};v.rails.push_back(r);
    }
    auto& r=*v.rails[0];
    for(unsigned n=advance(d,v);n;--n){r.calc();v.heading=normalized(lerp(v.heading,vec(r.curTangentDir()),.2f),{0,0,1});}
    const C3D_Mtx m=orthonormal(v.heading,vec(r.curPos()),placementScale(v));
    Anim a;a.clip=v.clips[0];a.frame=float(d.tick);
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
    drawn(v,m);
}
// DK Mountain's birds run retail's ObjectBird, which Kinoko builds only for
// seagulls: a leader on the route at setting 0 speed and setting 1
// followers (default 5) steering toward the flock centre at up to 1.2x
// that speed, kept 300 apart. Offsets and clip phases use a fixed seed.
void drawBirds(ObjectDraw& d,Visual& v){
    const float speed=float(v.geo->setting(0));
    if(v.rails.empty()){
        auto r=makeRail(v.geo->pathId(),speed);if(!r)return;
        r->calc();r->setCurrVel(speed);
        const unsigned count=v.geo->setting(1)?v.geo->setting(1):5;
        uint32_t seed=0x9e3779b9u^v.index;
        const float frames=v.main.model&&v.main.model->bank&&v.clips[0]>=0?float(v.main.model->bank->clipFrames(v.clips[0])):1.f;
        v.points.assign(count+1,vec(r->curPos()));v.velocities.assign(count+1,{0,0,speed});
        v.picks.assign(count+1,0);
        for(unsigned i=0;i<=count;++i)v.picks[i]=int(randomUnit(seed)*frames);
        for(unsigned i=1;i<=count;++i){
            const float z=randomUnit(seed)*1000.f-500.f,y=randomUnit(seed)*1000.f-500.f,x=randomUnit(seed)*1000.f-500.f;
            v.points[i]=v.points[0]+Vec3{x,y,z};
        }
        v.heading=vec(r->curTangentDir());v.rails.push_back(r);
    }
    auto& r=*v.rails[0];
    auto& p=v.points;auto& vel=v.velocities;
    const size_t count=p.size()-1;
    for(unsigned n=advance(d,v);n;--n){
        for(size_t i=1;i<count;++i)for(size_t j=i+1;j<=count;++j){
            Vec3 delta=p[i]-p[j];const float len=length(delta);
            if(len>=300.f)continue;
            p[j]=p[j]-normalized(delta,{1,0,0})*(300.f-len);
        }
        r.calc();p[0]=vec(r.curPos());v.heading=vec(r.curTangentDir());
        for(size_t i=1;i<=count;++i){
            Vec3 centre=p[0];
            for(size_t k=1;k<=count;++k)if(k!=i)centre+=p[k];
            vel[i]+=normalized(centre*(1.f/float(count))-p[i],{0,0,1})*.5f;
            if(length(vel[i])>speed*1.2f)vel[i]=normalized(vel[i],{0,0,1})*(speed*1.2f);
            p[i]+=vel[i];
        }
    }
    const Vec3 scale=placementScale(v);
    for(size_t i=0;i<=count;++i){
        const Vec3 forward=i?vel[i]:v.heading;
        const Vec3 f=normalized(forward,{0,0,1});const Vec3 side=normalized(cross({0,1,0},f),{1,0,0});
        Anim a;a.clip=v.clips[0];a.frame=float(d.tick)+float(v.picks[i]);
        d.submit(v.main.model,basisMatrix(side,cross(f,side),f,p[i],scale),a);
    }
    drawn(v,basisMatrix({1,0,0},{0,1,0},{0,0,1},p[0],scale));
}
// DK Summit chairlift (RMCP 8082963c): 20 chairs spaced evenly along the
// cable route at speed 30, each facing the cable's level direction from the
// segment it is on. Each pair of chairs half a loop apart shares 0-2 Shy
// Guys, who ride whichever of the two is climbing (route points marked 1),
// 2820 below the cable, 450 to either side when two ride together.
void drawChairlift(ObjectDraw& d,Visual& v){
    constexpr unsigned Chairs=20,Pairs=Chairs/2;
    if(v.rails.empty()){
        for(unsigned i=0;i<Chairs;++i){
            auto r=makeRail(v.geo->pathId(),30.f);if(!r){v.rails.clear();return;}
            r->setCurrVel(30.f);
            const float spacing=r->railLength()/float(Chairs);
            for(float travelled=0;travelled<spacing*float(i);travelled+=r->currVel())r->calc();
            v.rails.push_back(r);
        }
        v.points.assign(Chairs,{0,0,1});
        // picks: segment each chair faces, then riders and colours per pair.
        v.picks.assign(Chairs+Pairs*3,-1);
        uint32_t seed=0x2545f491u^v.index;
        for(unsigned i=0;i<Pairs;++i){
            v.picks[Chairs+i*3]=int(nextRandom(seed)%3);
            v.picks[Chairs+i*3+1]=int(nextRandom(seed)%3);v.picks[Chairs+i*3+2]=int(nextRandom(seed)%3);
        }
    }
    const unsigned steps=advance(d,v);
    const Vec3 scale=placementScale(v);
    C3D_Mtx chair[Chairs];
    for(unsigned i=0;i<Chairs;++i){
        auto& r=*v.rails[i];
        for(unsigned n=0;n<steps;++n)r.calc();
        if(v.picks[i]!=r.curPointIdx()){
            const Vec3 t=vec(r.curTangentDir());
            v.points[i]=normalized(Vec3{t.x,0,t.z},v.points[i]);v.picks[i]=r.curPointIdx();
        }
        const Vec3 z=v.points[i];
        chair[i]=basisMatrix(cross({0,1,0},z),{0,1,0},z,vec(r.curPos()),scale);
        if(v.extra[0].model)d.submit(v.extra[0].model,chair[i],{});
    }
    for(unsigned i=0;i<Pairs;++i){
        const unsigned riders=unsigned(v.picks[Chairs+i*3]);
        if(!riders)continue;
        const auto& partner=*v.rails[i];
        const C3D_Mtx& m=partner.curPoint().setting[0]?chair[i]:chair[i+Pairs];
        for(unsigned k=0;k<riders;++k){
            const float x=riders==1?0.f:k==0?-450.f:450.f;
            C3D_Mtx at=m;
            for(unsigned row=0;row<3;++row)at.r[row].w+=m.r[row].c[3]*x+m.r[row].c[2]*-2820.f;
            Anim a;a.pat=v.clips[0];a.patFrame=float(v.picks[Chairs+i*3+1+k]);
            d.submit(v.main.model,at,a);
        }
    }
    drawn(v,chair[0]);
}
}
bool classifyMover(Visual& v,const ObjectAssets& assets){
    if(v.geo->pathId()<0)return false;
    if(named(v,"skyship")||named(v,"castleballoon1")||named(v,"MashBalloonGC")){
        v.present=drawFlyer;
    }else if(named(v,"cruiserR")){
        v.present=drawCruiser;
    }else if(named(v,"moray")){
        v.present=drawMoray;v.clips[0]=clip(v.main,"swim");
    }else if(named(v,"K_bomb_car")&&!v.object){
        v.present=drawBombCar;v.main=find(assets,"K_bomb_car","K_bomb_car");
        v.extra[0]=find(assets,"K_bomb_car","Kbomneji","Kbomneji");v.extra[1]=find(assets,"K_bomb_car","K_bomb_tire00","K_bomb_tire00");
        v.shadow=find(assets,"K_bomb_car","K_bomb_car-shadow");v.low={};
    }else if(named(v,"penguin_l")){
        v.present=drawBigPenguin;v.clips[0]=clip(v.main,"walk");
    }else if(named(v,"bird")){
        v.present=drawBirds;v.clips[0]=clip(v.main,"flying");v.shadow={};
    }else if(named(v,"heyho2")){
        v.present=drawChairlift;v.main=find(assets,"heyho2","heyho2");v.clips[0]=pattern(v.main,"body_color");
        v.extra[0]=find(assets,"K_chairlift00","K_chairlift00");v.shadow={};v.low={};
    }else return false;
    return v.main.model||v.extra[0].model;
}
}
