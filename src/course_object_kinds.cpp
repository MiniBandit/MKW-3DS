#include "mkw/course_object_kinds.hpp"
#include "mkw/factory_belts.hpp"
#include "mkw/render_quality.hpp"
// Kinoko's ObjectBreakable::enableCollision hides ObjectBase's const one, a
// warning reported at ObjectBase.hh: silenced over the Kinoko headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#include <game/kart/KartObjectManager.hh>
#include <game/system/RaceConfig.hh>
#include <game/field/ObjectDirector.hh>
#include <game/field/obj/ObjectBase.hh>
#include <game/field/obj/ObjectBasabasa.hh>
#include <game/field/obj/ObjectBird.hh>
#include <game/field/obj/ObjectBreakable.hh>
#include <game/field/obj/ObjectCarTGE.hh>
#include <game/field/obj/ObjectChoropu.hh>
#include <game/field/obj/ObjectCow.hh>
#include <game/field/obj/ObjectCrab.hh>
#include <game/field/obj/ObjectDossun.hh>
#include <game/field/obj/ObjectFireRing.hh>
#include <game/field/obj/ObjectFirebar.hh>
#include <game/field/obj/ObjectFlamePole.hh>
#include <game/field/obj/ObjectFlamePoleFoot.hh>
#include <game/field/obj/ObjectFlamePoleV.hh>
#include <game/field/obj/ObjectHanachan.hh>
#include <game/field/obj/ObjectFireSnake.hh>
#include <game/field/obj/ObjectHeyho.hh>
#include <game/field/obj/ObjectHeyhoBall.hh>
#include <game/field/obj/ObjectItemboxPress.hh>
#include <game/field/RailManager.hh>
#include <game/field/obj/ObjectKoopaBall.hh>
#include <game/field/obj/ObjectTruckWagon.hh>
#include <game/field/obj/ObjectTwistedWay.hh>
#include <game/field/obj/ObjectKuribo.hh>
#include <game/field/obj/ObjectObakeBlock.hh>
#include <game/field/obj/ObjectPakkunF.hh>
#include <game/field/obj/ObjectPenguin.hh>
#include <game/field/obj/ObjectPress.hh>
#include <game/field/obj/ObjectStarRing.hh>
#include <game/field/obj/ObjectVolcanoBall.hh>
#include <game/field/obj/ObjectVolcanoBallLauncher.hh>
#include <game/field/obj/ObjectWanwan.hh>
#include <game/system/RaceManager.hh>
#pragma GCC diagnostic pop
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace K=Kinoko;
namespace {
using K::Field::ObjectId;
Vec3 vec(const K::EGG::Vector3f& v){return {v.x,v.y,v.z};}
Vec3 position(const C3D_Mtx& m){return {m.r[0].w,m.r[1].w,m.r[2].w};}
bool named(const Visual& v,const char* name){return !std::strcmp(v.name,name);}
C3D_Mtx liveMatrix(const K::Field::ObjectBase& object){return objectMatrix(object.transform(),object.scale());}
// Beyond renderQuality().objectLowDistance a model's authored -low LOD replaces it.
int clip(const Tracks& t,const char* name){return t.model&&t.model->bank?t.model->bank->clip(name):-1;}
int pattern(const Tracks& t,const char* name){return t.model&&t.model->bank?t.model->bank->pattern(name):-1;}
int scroll(const Tracks& t,const char* name){return t.model&&t.model->bank?t.model->bank->scroll(name):-1;}
int color(const Tracks& t,const char* name){return t.model&&t.model->bank?t.model->bank->color(name):-1;}
float clipFrames(const Tracks& t,int index){return t.model&&t.model->bank?float(t.model->bank->clipFrames(index)):0.f;}
Tracks find(const ObjectAssets& assets,const char* resource,const char* model,const char* tracks=nullptr){return assets.tracks(assets.find(resource,model),tracks);}
// Frames since Kinoko last changed this object's observed state.
unsigned stateAge(ObjectDraw& d,Visual& v,int state){
    if(state!=v.lastState||d.tick<v.stateTick){v.lastState=state;v.stateTick=d.tick;}
    return d.tick-v.stateTick;
}
// Retail plays decorative loops from frame zero; non-looping pattern clips
// (signs, colour variants) hold their first frame unless a presenter picks one.
Anim loopAt(const Tracks& t,float frame){
    Anim a=animAt(t,frame);
    if(t.model&&t.model->bank&&a.pat>=0&&!t.model->bank->patternLoops(a.pat))a.patFrame=0;
    return a;
}
// Facing from motion: Kinoko moves these parts by position only.
C3D_Mtx facingMatrix(Vec3 position,Vec3 forward,Vec3 scale){
    forward=normalized(forward,{0,0,1});
    const Vec3 side=normalized(cross({0,1,0},forward),{1,0,0});
    return basisMatrix(side,cross(forward,side),forward,position,scale);
}

void drawGeneric(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=v.kinetic&&v.object?liveMatrix(*v.object):v.placement;
    const float frame=float(d.tick)*v.rate+v.phase;
    const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
    const Tracks& t=far?v.low:v.main;
    Anim a=loopAt(t,frame);
    if(v.variant>=0)a.patFrame=a.clrFrame=float(v.variant);
    d.submit(t.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Cannons: DK Summit's DonkyCannon_wii, DK Mountain's DonkyCannonGC and Maple
// Treeway's tree_cannon are one retail class (RMCP init 806bfddc, calc
// 806bfed0). Its one clip, the shot, rests on frame zero (setFrame stops it)
// and plays once at rate 1 when any kart starts a cannon (CannonStart) while it
// rests, stopping on frame zero again at its last frame; the generic presenter
// looped it. CannonStart lasts one step, so a presentation every second step
// also starts the shot from a kart first seen InCannon, one step late.
void drawDonkyCannon(ObjectDraw& d,Visual& v){
    const auto* karts=K::Kart::KartObjectManager::Instance();
    const auto* config=K::System::RaceConfig::Instance();
    const unsigned players=karts&&config?config->raceScenario().playerCount:0;
    // picks: each kart's CannonStart or InCannon when last presented.
    if(d.tick<v.lastTick){v.picks.clear();v.lastState=-1;}
    v.lastTick=d.tick;
    if(v.picks.size()!=players)v.picks.assign(players,0);
    int start=-1;
    for(unsigned i=0;i<players;++i){
        const auto& status=karts->object(i)->status();
        const bool firing=status.onBit(K::Kart::eStatus::CannonStart);
        const bool in=firing||status.onBit(K::Kart::eStatus::InCannon);
        if(in&&!v.picks[i])start=std::max(start,int(firing||!d.tick?d.tick:d.tick-1));
        v.picks[i]=in;
    }
    // lastState: 1 while the shot plays from stateTick, 0 back at rest, -1 never
    // fired. A start on the step the shot reaches its last frame is ignored.
    const float last=std::max(clipFrames(v.main,v.main.clip)-1.f,0.f);
    if(v.lastState==1&&float(d.tick-v.stateTick)>=last)v.lastState=0;
    if(v.lastState!=1&&start>=0&&(v.lastState<0||unsigned(start)>v.stateTick+unsigned(last))){v.lastState=1;v.stateTick=unsigned(start);}
    const float frame=v.lastState==1?float(d.tick-v.stateTick):0.f;
    const bool far=v.low.model&&length(position(v.placement)-d.camera)>renderQuality().objectLowDistance;
    const Tracks& t=far?v.low:v.main;
    d.submit(t.model,v.placement,animAt(t,frame));
    if(v.shadow.model)d.shadow(v.shadow.model,v.placement);
}
// Toad's Factory's belts scroll their SRT0 at the rate their constructor stores
// (USA 0x807F0C4C: BeltEasy 0.9, BeltCrossing 1.25; classifyObject sets v.rate).
// The switching belt (ObjBeltCurveA) also loads BeltCurveB and BeltCurveC, the
// direction chevrons and LED arrows, and shows exactly one: B while the belt runs
// forward, C while it runs back (0x807F05F4; 0x80558670 adds or removes a model
// from the scene). Near a change its overlay fades by B_on_off/C_on_off and every
// SRT0 slows to a stop and back (0x807F07CC; include/mkw/factory_belts.hpp).
void drawBeltCurve(ObjectDraw& d,Visual& v){
    const auto timing=BeltCurveTiming::fromSettings(v.geo->setting(1),v.geo->setting(2),v.geo->setting(3));
    const unsigned timer=K::System::RaceManager::Instance()->timer();
    const C3D_Mtx& m=v.placement;
    const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
    const float scroll=timing.scroll(d.tick,timer);
    const Tracks& belt=far?v.low:v.main;
    d.submit(belt.model,m,animAt(belt,scroll));
    // extra: BeltCurveB, BeltCurveB-low, BeltCurveC, BeltCurveC-low.
    const Tracks& overlay=v.extra[(timing.forward(timer)?0:2)+(far&&v.extra[1].model?1:0)];
    if(!overlay.model)return;
    Anim a=animAt(overlay,scroll);a.clrFrame=timing.fadeFrame(timer);
    d.submit(overlay.model,m,a);
}
// Sky suns and the Earth's glow ring are camera-facing sprites at their placement.
void drawSun(ObjectDraw& d,Visual& v){
    const C3D_Mtx p=v.placement;
    const float scale=length(Vec3{p.r[0].z,p.r[1].z,p.r[2].z});
    d.submit(v.main.model,d.view.cameraFacing(position(p),scale),animAt(v.main,float(d.tick)));
}
void drawKuribo(ObjectDraw& d,Visual& v){
    const auto& k=*static_cast<const K::Field::ObjectKuribo*>(v.object);
    // Squashed Goombas blink out (retail 0x806DC790); the walk clip holds while
    // hit, blinking or popping back up (0x806DB5B0), Kinoko's scale squashes it.
    if(!k.visible())return;
    if(k.walking())v.phase=k.animTimer();
    const C3D_Mtx m=liveMatrix(k);
    // Kinoko's walk timer is the CHR0 frame that gates each step.
    Anim a;a.clip=v.clips[0];a.frame=v.phase;
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Retail simulates the chomp's chain as damped point links pinned to the
// chomp and to the stake ring (RMCU 806d4680/806e11ec): gravity, velocity
// x0.85, two substeps with 1.1x length constraints, then floor contact.
// Kinoko omits it (presentation only). The floor here is the line between
// the stake's and the chomp's ground heights: querying KCL from presentation
// would disturb Kinoko's collision caches.
void stepChain(Visual& v,const K::Field::ObjectWanwan& w,unsigned steps){
    auto& p=v.points;auto& vel=v.velocities;
    const unsigned n=unsigned(p.size());
    const Vec3 anchor=vec(w.anchor()),attach=vec(w.chainAttachPos());
    const float link=135.f*w.scale().x;
    const float anchorFloor=anchor.y-20.f,chompFloor=w.pos().y-570.f;
    auto floorAt=[&](Vec3 q){
        const Vec3 a{anchor.x,0,anchor.z},b{attach.x,0,attach.z};const Vec3 c{q.x,0,q.z};
        const float span=length(b-a);const float t=span>1.f?std::clamp(dot(c-a,b-a)/(span*span),0.f,1.f):0.f;
        return anchorFloor+(chompFloor-anchorFloor)*t+30.f;
    };
    for(unsigned step=0;step<steps;++step){
        for(unsigned sub=0;sub<2;++sub){
            for(unsigned i=1;i+1<n;++i){vel[i].y-=2.5f;vel[i]=vel[i]*.85f;p[i]+=vel[i];}
            p[0]=attach;
            Vec3 ring=p[n-1]-anchor;
            if(length(ring)>120.f)p[n-1]=anchor+normalized(ring)*120.f;
            for(unsigned pass=0;pass<4;++pass){
                for(unsigned i=1;i<n;++i){
                    const Vec3 delta=p[i]-p[i-1];const float dist=length(delta);const float limit=link*1.1f;
                    if(dist<=limit)continue;
                    const Vec3 fix=delta*((dist-limit)/dist);
                    if(i==n-1)p[i-1]+=fix;
                    else if(i==1)p[i]=p[i]-fix;
                    else{p[i]=p[i]-fix*.5f;p[i-1]+=fix*.5f;}
                }
            }
        }
        for(unsigned i=1;i+1<n;++i){const float floor=floorAt(p[i]);if(p[i].y<floor){p[i].y=floor;vel[i]=vel[i]*.5f;vel[i].y=0;}}
    }
}
void drawWanwan(ObjectDraw& d,Visual& v){
    const auto& w=*static_cast<const K::Field::ObjectWanwan*>(v.object);
    const C3D_Mtx m=liveMatrix(w);
    // The chain attachment follows the 15-key bounce while waiting and its
    // first key while lunging (ObjectWanwan::calcChainAttachMat).
    const float frame=w.stateId()==1?0.f:float(w.frame()%15);
    d.submit(v.main.model,m,animAt(v.main,frame));
    if(v.extra[1].model)d.submit(v.extra[1].model,v.placement,{});
    if(v.shadow.model)d.shadow(v.shadow.model,m);
    const unsigned links=w.chainCount();
    if(!v.extra[0].model||!links)return;
    const Vec3 anchor=vec(w.anchor()),attach=vec(w.chainAttachPos());
    if(v.points.size()!=links+2||d.tick<v.lastTick||d.tick-v.lastTick>120){
        // (Re)start from a straight chain on a new scene or long gap.
        v.points.assign(links+2,{});v.velocities.assign(links+2,{});
        for(unsigned i=0;i<links+2;++i)v.points[i]=attach+(anchor-attach)*(float(i)/float(links+1));
        v.lastTick=d.tick;
        stepChain(v,w,60);
    }
    stepChain(v,w,std::min(d.tick-v.lastTick,8u));v.lastTick=d.tick;
    const float scale=w.scale().x;
    for(unsigned i=0;i<links;++i){
        const Vec3 a=v.points[i],b=v.points[i+2];
        const Vec3 forward=normalized(b-a,{0,0,1});
        Vec3 side=normalized(cross({0,1,0},forward),{1,0,0});
        const Vec3 up=normalized(cross(forward,side));
        // Neighbouring links interlock at right angles about the chain axis.
        const Vec3 x=i&1?up:side,y=i&1?side*-1.f:up;
        d.submit(v.extra[0].model,basisMatrix(x,y,forward,v.points[i+1],{scale,scale,scale}),{});
    }
}
void drawHanachan(ObjectDraw& d,Visual& v){
    const auto& h=*static_cast<const K::Field::ObjectHanachan*>(v.object);
    const bool walking=h.stateId()==0;
    const float frame=float(d.tick);
    for(size_t i=0;i<h.partCount();++i){
        const auto* part=h.part(i);if(!part)continue;
        const C3D_Mtx m=liveMatrix(*part);
        if(i==0){
            Anim a;a.clip=v.clips[walking?0:1];a.frame=frame;
            d.submit(v.main.model,m,a);
            if(v.shadow.model)d.shadow(v.shadow.model,m);
        }else{
            const Tracks& body=v.extra[(i-1)%4];
            Anim a;a.clip=body.model&&body.model->bank?body.model->bank->clip(walking?"walk":"wait"):-1;a.frame=frame;
            d.submit(body.model,m,a);
            if(v.extra[4].model)d.shadow(v.extra[4].model,m);
        }
    }
}
void drawPenguinSlide(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectPenguinS*>(v.object);
    const C3D_Mtx m=liveMatrix(p);
    Anim a;a.frame=float(d.tick);
    switch(p.state()){
    case 2: // Diving onto its belly: Kinoko counts the dive clip down.
        a.clip=v.clips[1];a.frame=clipFrames(v.main,a.clip)-float(p.anmTimer());break;
    case 3:a.clip=v.clips[2];break;
    case 4:a.clip=v.clips[3];a.frame=clipFrames(v.main,a.clip)-float(p.anmTimer());break;
    default:a.clip=v.clips[0];break;
    }
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawPenguinWalk(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    Anim a;a.clip=v.clips[0];a.frame=float(d.tick);
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
template<class T> void drawFireballs(ObjectDraw& d,Visual& v){
    const auto& owner=*static_cast<const T*>(v.object);
    if(v.main.model)d.submit(v.main.model,liveMatrix(owner),animAt(v.main,float(d.tick)));
    const float frame=float(d.tick);
    for(const auto* ball:owner.fireballs()){
        if(!ball)continue;
        // Fireballs are camera-facing: a flattened core and its flame card.
        const C3D_Mtx m=d.view.cameraFacing(vec(ball->pos()),ball->scale().x);
        if(v.extra[0].model)d.submit(v.extra[0].model,m,{});
        if(v.extra[1].model)d.submit(v.extra[1].model,m,animAt(v.extra[1],frame));
    }
}
void drawMushroom(ObjectDraw& d,Visual& v){
    // Kinoko's live transform carries the pulse scale, bend and bob.
    const C3D_Mtx m=liveMatrix(*v.object);
    d.submit(v.extra[0].model,m,{});
    d.submit(v.main.model,m,{});
}
// Thwomps (RMCU ObjectDossun 806d... startBeforeFall/calcStomp): the face
// pattern plays forward from frame 0 when the stomp begins and backward from
// its end when the Thwomp starts rising again.
void drawThwomp(ObjectDraw& d,Visual& v){
    const unsigned owners=v.geo->setting(1)==2?2:1; // dossunc (+ follower holder)
    if(v.parts.size()<=owners)return;
    const float faceEnd=v.main.model&&v.main.model->bank&&v.clips[0]>=0?float(v.main.model->bank->patternFrames(v.clips[0])-1):0.f;
    for(size_t i=0;i+owners<v.parts.size();++i){
        const auto& t=*static_cast<const K::Field::ObjectDossun*>(v.parts[i]);
        const C3D_Mtx m=liveMatrix(t);
        const int state=t.anmState();
        // One observed state per Thwomp: placements here own a single one,
        // follower holders reuse the first's timing for both.
        const unsigned age=i==0?stateAge(d,v,state):d.tick-v.stateTick;
        Anim a;a.pat=v.clips[0];
        a.patFrame=state==0?0.f:state==4?std::max(0.f,faceEnd-float(age)):std::min(faceEnd,float(age));
        d.submit(v.main.model,m,a);
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }
}
// Monty Moles: underground while digging, the hole and 'start' clip while
// peeking, 'jump' while leaping (Kinoko ObjectChoropu states 0/1/3).
void drawChoropu(ObjectDraw& d,Visual& v){
    const auto& c=*static_cast<const K::Field::ObjectChoropu*>(v.object);
    const int state=c.stateId();
    const bool stationary=named(v,"choropu2");
    // The placement's hole object is the first part Kinoko registers.
    const K::Field::ObjectBase* hole=v.parts.size()>1?v.parts[0]:nullptr;
    if(hole&&v.extra[0].model&&(stationary||state==1||state==3))d.submit(v.extra[0].model,liveMatrix(*hole),{});
    if(state!=1&&state!=3)return;
    Anim a;a.clip=v.clips[state==1?0:1];a.frame=float(c.stateFrame());
    const C3D_Mtx m=liveMatrix(c);
    d.submit(v.main.model,m,a);
}
// Cows: wait, walk/run by speed, and the eat start/loop/end sequence.
void drawCow(ObjectDraw& d,Visual& v){
    if(v.parts.size()<2)return;
    for(size_t i=0;i+1<v.parts.size();++i){
        const auto& cow=*static_cast<const K::Field::ObjectCow*>(v.parts[i]);
        const C3D_Mtx m=liveMatrix(cow);
        Anim a;a.frame=float(d.tick+i*17);
        int state;unsigned frame;
        if(i==0){const auto& l=static_cast<const K::Field::ObjectCowLeader&>(cow);state=l.stateId();frame=l.stateFrame();
            if(state==1){
                const unsigned eat=l.eatFrames();
                if(frame<40){a.clip=v.clips[3];a.frame=float(frame);}
                else if(frame<40u+eat){a.clip=v.clips[4];a.frame=float(frame-40);}
                else{a.clip=v.clips[5];a.frame=float(frame-40-eat);}
            }
        }else{const auto& f=static_cast<const K::Field::ObjectCowFollower&>(cow);state=f.stateId();frame=f.stateFrame();}
        if(a.clip<0){
            const float speed=cow.xzSpeed();
            a.clip=speed<.3f?v.clips[0]:speed>3.f?v.clips[2]:v.clips[1];
        }
        d.submit(v.main.model,m,a);
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }
}
// Shy Guys: body colour from setting 1 (Kinoko m_color), move/jump/landing.
void drawHeyho(ObjectDraw& d,Visual& v){
    const auto& h=*static_cast<const K::Field::ObjectHeyho*>(v.object);
    const int anim=h.currentAnim();
    const unsigned age=stateAge(d,v,anim);
    Anim a;a.pat=v.clips[3];a.patFrame=float(v.geo->setting(1));
    a.clip=v.clips[anim==2?1:anim==3?2:0];a.frame=anim==1?float(d.tick):float(age);
    const C3D_Mtx m=liveMatrix(h);
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawPakkun(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectPakkunF*>(v.object);
    Anim a;
    if(p.attacking()){a.clip=v.clips[0];a.frame=float(p.attackFrame());}
    else{a.clip=v.clips[1];a.frame=float(d.tick);}
    d.submit(v.main.model,liveMatrix(p),a);
}
void drawCrab(ObjectDraw& d,Visual& v){
    const auto& c=*static_cast<const K::Field::ObjectCrab*>(v.object);
    const int state=c.state();
    if(state==2)return; // deactivated: under the sand
    Anim a;a.frame=float(d.tick);
    a.clip=state==1?v.clips[2]:v.clips[c.backwards()?1:0];
    const C3D_Mtx m=liveMatrix(c);
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Cataquacks: walk loop; setting 0 selects the body colour pattern frame.
void drawPoihana(ObjectDraw& d,Visual& v){
    Anim a;a.clip=v.clips[0];a.frame=float(d.tick);a.pat=v.clips[1];a.patFrame=float(v.geo->setting(0));
    const C3D_Mtx m=liveMatrix(*v.object);
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Flocks (seagulls, bats): one model per Kinoko part, facing its motion.
void drawFlock(ObjectDraw& d,Visual& v){
    const bool bats=named(v,"basabasa");
    const size_t count=v.parts.size()>1?v.parts.size()-1:0;
    if(v.points.size()!=count){v.points.assign(count,{});v.velocities.assign(count,{0,0,1});}
    const float frames=v.main.model&&v.main.model->bank&&v.clips[0]>=0?float(v.main.model->bank->clipFrames(v.clips[0])):1.f;
    for(size_t i=0;i<count;++i){
        const auto* part=v.parts[i];
        if(bats&&!static_cast<const K::Field::ObjectBasabasaDummy*>(part)->active())continue;
        const Vec3 p=vec(part->pos());
        const Vec3 delta=p-v.points[i];
        if(dot(delta,delta)>.01f&&dot(delta,delta)<1e6f)v.velocities[i]=delta;
        v.points[i]=p;
        Anim a;a.clip=v.clips[0];a.frame=float(d.tick)+std::fmod(float(i*37+v.index*11),frames);
        d.submit(v.main.model,facingMatrix(p,v.velocities[i],vec(part->scale())),a);
    }
}
// Grumble Volcano lava balls (retail RMCP 806e2f38/806e3324): the rock shows
// only while it falls (Kinoko state 1). Dormant (0) it is hidden, and burning
// (2) retail hides it every step: only the course effect's rk_volcFire_L flames
// remain where it landed.
void drawVolcanoBalls(ObjectDraw& d,Visual& v){
    const auto& launcher=*static_cast<const K::Field::ObjectVolcanoBallLauncher*>(v.object);
    for(const auto* ball:launcher.balls()){
        if(!ball||ball->stateId()!=1)continue;
        const C3D_Mtx m=liveMatrix(*ball);
        d.submit(v.main.model,m,{});
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }
}
// Bowser's Castle flame poles: the floor ring always, the erupting pillar
// (a scaled child object) while Kinoko keeps it active.
void drawFlamePole(ObjectDraw& d,Visual& v){
    const auto& foot=*static_cast<const K::Field::ObjectFlamePoleFoot*>(v.object);
    d.submit(v.main.model,liveMatrix(foot),animAt(v.main,float(d.tick)));
    const auto* pole=foot.pole();
    if(pole&&pole->active()&&v.extra[0].model)d.submit(v.extra[0].model,liveMatrix(*pole),animAt(v.extra[0],float(d.tick)));
}
// Moonview Highway traffic: setting 3 picks the paint (Kinoko m_mdlName);
// wheels and lamps are separate models sharing the body transform.
void drawHighwayCar(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    d.submit(v.main.model,m,{});
    if(v.extra[0].model)d.submit(v.extra[0].model,m,animAt(v.extra[0],float(d.tick)));
    if(v.extra[1].model)d.submit(v.extra[1].model,m,{});
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Fire Snakes: head with eyes plus two trailing segments (Kinoko kids,
// 0.75x and 0.5x) replaying the head's earlier transforms. Hidden while
// despawned (state 0).
void drawFireSnake(ObjectDraw& d,Visual& v){
    const auto& s=*static_cast<const K::Field::ObjectFireSnake*>(v.object);
    if(s.stateId()==0)return;
    const float frame=float(d.tick);
    const C3D_Mtx m=liveMatrix(s);
    d.submit(v.main.model,m,animAt(v.main,frame));
    if(v.extra[0].model)d.submit(v.extra[0].model,m,{});
    for(size_t i=0;i<2;++i)if(const auto* kid=s.kid(i))d.submit(v.main.model,liveMatrix(*kid),animAt(v.main,frame));
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// The common Bob-omb explosion (bomb_core) at transform, frame into its clip.
void drawExplosion(ObjectDraw& d,const C3D_Mtx& transform,float frame){
    d.explosion(transform,frame);
}
// Moonview Highway's Bob-omb cars (Kinoko ObjectCarTGE, car type 2): drawn
// from Kinoko's transform and scale, including the regrow stretch, and hidden
// from the blast until they regrow. The blast shows the common Bob-omb core
// on the car's matrix at the blast radius over 900 (retail USA 0x806CAF5C).
void drawBombCarLive(ObjectDraw& d,Visual& v){
    const auto& car=*static_cast<const K::Field::ObjectCarTGE*>(v.object);
    if(car.bombShown()){
        const C3D_Mtx m=liveMatrix(car);
        d.submit(v.main.model,m,{});
        if(v.extra[0].model)d.submit(v.extra[0].model,m,animAt(v.extra[0],float(d.tick)));
        if(v.extra[1].model)d.submit(v.extra[1].model,m,animAt(v.extra[1],float(d.tick)));
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }
    if(car.blastActive()){
        const float scale=car.blastRadius()/900.f;
        drawExplosion(d,objectMatrix(car.transform(),{scale,scale,scale}),float(car.explodeFrame()));
    }
}
// Bowser's Castle fireball: a rolling ball in its fire shell; at the end of
// its route the common Bob-omb explosion clip plays at the ball's scale.
void drawKoopaBall(ObjectDraw& d,Visual& v){
    const auto& b=*static_cast<const K::Field::ObjectKoopaBall*>(v.object);
    const int state=b.state();
    const C3D_Mtx m=liveMatrix(b);
    if(state==0){
        d.submit(v.main.model,m,{});
        if(v.extra[0].model)d.submit(v.extra[0].model,m,animAt(v.extra[0],float(d.tick)));
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }else if(state==2){
        // PAL 0x807719E8 (from calcTangible 0x80770F4C): bombCore at the ball's
        // position with identity rotation and uniform scale 2.0 (0x808CF3F4).
        // The cooked pose bank already holds the CHR0 scale (mesh radius ~870 at
        // full size), so 2 x 870 is Kinoko's 1740-unit RADIUS_AABB.
        drawExplosion(d,basisMatrix({1,0,0},{0,1,0},{0,0,1},vec(b.pos()),{2.f,2.f,2.f}),float(int(b.explodeFrames())-b.explodeTimer()));
    }
}
// GBA Shy Guy Beach cannonballs (Kinoko ObjectHeyhoBall): nothing while in
// the ship (state 0), the ball while it arcs down, its blink clip for the
// 180 frames it rests, then the common explosion over the 1500-unit blast
// that Kinoko models by scaling the ball's collision.
void drawHeyhoBall(ObjectDraw& d,Visual& v){
    const auto& b=*static_cast<const K::Field::ObjectHeyhoBall*>(v.object);
    const int state=b.stateId();
    if(state==0)return;
    if(state==3){
        // Enter-explode PAL 0x806D0D84: bombCore at m_initPos with identity
        // rotation and uniform scale 1500/900 (1500.0 at 0x808C4BB8, 900 the
        // bombCore normalisation at 0x8089FDAC). While exploding Kinoko keeps
        // the ball at m_initPos - 50 Y (enterBlinking), so m_initPos is pos + 50 Y.
        const float scale=1500.f/900.f;
        drawExplosion(d,basisMatrix({1,0,0},{0,1,0},{0,0,1},vec(b.pos())+Vec3{0,50,0},{scale,scale,scale}),float(b.stateFrame()));
        return;
    }
    const C3D_Mtx m=liveMatrix(b);
    Anim a;
    if(state==2){a.clr=v.clips[0];a.clrFrame=float(b.stateFrame());}
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// The statue's vomit clip starts as its fireball leaves the mouth.
void drawKoopaFigure(ObjectDraw& d,Visual& v){
    Anim a;a.clip=v.clips[0];a.frame=float(d.tick);
    if(v.partner){
        const int state=static_cast<const K::Field::ObjectKoopaBall*>(v.partner)->state();
        const unsigned age=stateAge(d,v,state);
        if(state==0&&v.clips[1]>=0&&float(age)<clipFrames(v.main,v.clips[1])){a.clip=v.clips[1];a.frame=float(age);}
    }
    d.submit(v.main.model,v.placement,a);
}
// DS Peach Gardens chomps roam as the manager's child object.
void drawHwanwan(ObjectDraw& d,Visual& v){
    if(v.parts.size()<2)return;
    const auto& w=*v.parts[0];
    const C3D_Mtx m=liveMatrix(w);
    d.submit(v.main.model,m,animAt(v.main,float(d.tick)));
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Wario's Gold Mine carts: the manager spawns and recycles active carts.
void drawTruckWagon(ObjectDraw& d,Visual& v){
    for(size_t i=0;i+1<v.parts.size();++i){
        const auto& cart=*static_cast<const K::Field::ObjectTruckWagonCart*>(v.parts[i]);
        if(!cart.isActive())continue;
        const C3D_Mtx m=liveMatrix(cart);
        Anim a;a.clip=v.clips[0];a.frame=float(d.tick);a.srt=v.clips[1];a.srtFrame=float(d.tick);
        const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
        d.submit(far?v.low.model:v.main.model,m,a);
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }
}
// Psea's logical position includes the authored Psea5spc mesh height.
// Mesh vertices already contain that height: only the parent displacement
// belongs in their transform. Retail places sand 48 units above the wave
// layers, and the specular layer one unit above Psea4tex (USA 80818720).
void drawTide(ObjectDraw& d,Visual& v){
    C3D_Mtx m=liveMatrix(*v.object);
    m.r[1].w-=9590.399f+1.f;
    for(unsigned i=0;i<v.extra.size();++i){
        const auto& t=v.extra[i];if(!t.model)continue;
        C3D_Mtx layer=m;
        if(i==0)layer.r[1].w+=48.f;
        else if(i==4)layer.r[1].w+=1.f;
        d.submit(t.model,layer,animAt(t,float(d.tick)));
    }
}
constexpr float Pi=3.14159265f;
// Retail's deforming roads run on the race timer once the race starts.
bool racing(uint32_t& timer){
    const auto* race=K::System::RaceManager::Instance();
    timer=race?race->timer():0;
    return race&&race->isStageReached(K::System::RaceManager::Stage::Race);
}
// Row-major 3x4 affine product out=a*b.
void mul34(const float* a,const float* b,float* out){
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
        out[r*4+c]=a[r*4]*b[c]+a[r*4+1]*b[4+c]+a[r*4+2]*b[8+c]+(c==3?a[r*4+3]:0.f);
}
// Procedural joints start from the bind palette; each posed joint becomes
// parent world * its retail local matrix.
const ObjectBank* beginPose(Visual& v,ObjModel* m){
    if(!m||!m->bank||!m->bank->skinned())return nullptr;
    const auto& b=*m->bank;
    v.palette.assign(b.bind(),b.bind()+size_t(b.matrixCount())*12);
    if(m->slots.empty())b.slotBounds(m->slots,m->slotUsed);
    return &b;
}
void poseJoint(Visual& v,const ObjectBank& b,int node,const float* parent,const float* local){
    if(node<0)return;
    const int slot=b.nodes()[node].slot;
    if(slot>=0)mul34(parent,local,&v.palette[size_t(slot)*12]);
}
// The posed palette and its model-space bounds, so culling sees the
// deformation rather than the flat bind pose.
Anim finishPose(Visual& v,const ObjModel* m,Anim a){
    bool first=true;
    for(size_t i=0;i<m->slots.size();++i)if(m->slotUsed[i]){
        const auto b=transformBounds(m->slots[i],&v.palette[i*12]);
        if(first){v.posed=b;first=false;}else{v.posed.include(b.minimum);v.posed.include(b.maximum);}
    }
    a.clip=-1;a.palette=v.palette.data();a.bounds=first?nullptr:&v.posed;
    return a;
}
int jointNode(const ObjectBank& b,const char* format,int index){char name[16];std::snprintf(name,sizeof name,format,index);return b.node(name);}
// Rainbow Road's aurora road (RMCP ObjectAurora 807fac84): joints 3..38 move
// to (416.66n, h, 0) in joint1's frame, h being the wave Kinoko collides
// with, sampled at 416.66(n-2): a chirp from frequency 1.25 at the start to
// 4 at 2:13. Before the race it runs on a frame counter of its own.
void drawAurora(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    ObjModel* model=v.main.model;
    const ObjectBank* bank=beginPose(v,model);
    const int root=bank?bank->node("joint1"):-1;
    if(root<0)return;
    uint32_t timer;
    const float frame=racing(timer)?float(timer):float(stateAge(d,v,0));
    const float minutes=(frame/60.f-30.f)/60.f;
    const float frequency=std::min(1.f+minutes*minutes,4.f);
    const float* parent=bank->nodes()[root].world;
    for(int i=1;i<=36;++i){
        const int n=jointNode(*bank,"joint%d",i+2);if(n<0)continue;
        const float period=Pi*2.f*(416.66f*float(i))/15000.f;
        float local[12];std::memcpy(local,bank->nodes()[n].local,sizeof local);
        local[3]=416.66f*float(i+2);local[7]=period*80.f*std::sin(period*frequency+Pi*frame/50.f);local[11]=0;
        poseJoint(v,*bank,n,parent,local);
    }
    d.submit(model,m,finishPose(v,model,loopAt(v.main,float(d.tick))));
}
// Bowser's Castle's twisting road (RMCP ObjectTwistedWay::calc 80813cfc):
// joints 3..20 roll about their x axis by Kinoko's calcWave angle. Unlike
// the collision it also sways before the race, on Kinoko's intro timer.
void drawTwistedWay(ObjectDraw& d,Visual& v){
    const auto& road=*static_cast<const K::Field::ObjectTwistedWay*>(v.object);
    const C3D_Mtx m=liveMatrix(road);
    const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
    ObjModel* model=far?v.low.model:v.main.model;
    const ObjectBank* bank=beginPose(v,model);
    const int root=bank?bank->node("joint1"):-1;
    if(root<0){d.submit(model,m,{});return;}
    uint32_t timer;
    // Retail reads the intro timer before Kinoko's calc increments it.
    const uint32_t frame=racing(timer)?timer:road.introTimer()-1;
    const float t=float((frame%120)*2);
    const float* parent=bank->nodes()[root].world;
    for(int i=1;i<19;++i){
        const int n=jointNode(*bank,"joint%d",i+2);if(n<0)continue;
        const float z=-1.f+float(2*(i+1))/20.f;
        const float angle=.2f*(z+1.f)*(z+1.f)*(z-1.f)*(z-1.f)*std::sin(t*Pi/120.f+4.f*z);
        const float c=std::cos(angle),s=std::sin(angle);const float* l=bank->nodes()[n].local;
        const float local[12]={1,0,0,l[3], 0,c,-s,l[7], 0,s,c,l[11]};
        poseJoint(v,*bank,n,parent,local);
    }
    d.submit(model,m,finishPose(v,model,{}));
}
// DK Mountain's rope bridge (RMCP ObjectTuribashi 80805ad8/80805c24): the
// root is scaled 0.8 across and joints 02..037 roll about z by the swing
// Kinoko collides with, still until the race starts.
void drawTuribashi(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    ObjModel* model=v.main.model;
    const ObjectBank* bank=beginPose(v,model);
    if(!bank)return;
    uint32_t timer;
    const uint32_t frame=racing(timer)?timer:0;
    const float t=float((frame%160)*2);
    const float narrow[12]={.8f,0,0,0, 0,1,0,0, 0,0,1,0};
    float parent[12];std::memcpy(parent,narrow,sizeof parent);
    if(const int root=bank->node("DKturibashiGC");root>=0)mul34(bank->nodes()[root].world,narrow,parent);
    for(int i=0;i<37;++i){
        const int n=jointNode(*bank,"joint0%d",i+1);if(n<0)continue;
        float angle=0;
        if(i>0){const float z=-1.f+float(2*(i+1))/38.f;angle=.12f*(z+1.f)*(z+1.f)*(z-1.f)*(z-1.f)*std::sin(Pi*t/160.f+.7f*z);}
        const float c=std::cos(angle),s=std::sin(angle);const float* l=bank->nodes()[n].local;
        const float local[12]={c,s,0,l[3], -s,c,0,l[7], 0,0,1,l[11]};
        poseJoint(v,*bank,n,parent,local);
    }
    d.submit(model,m,finishPose(v,model,{}));
}
// Maple Treeway's net (RMCP ObjectAmi 80807ff4): joint k+1 sits at
// (600k, y, 0) in joint1's frame, y being the height Kinoko collides with:
// a 70-frame bounce, strongest mid-net, on a 910-per-radian slope.
void drawAmi(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    ObjModel* model=v.main.model;
    const ObjectBank* bank=beginPose(v,model);
    const int root=bank?bank->node("joint1"):-1;
    if(root<0)return;
    uint32_t timer;
    const float frame=racing(timer)?float(timer):0.f;
    const float bounce=std::sin(Pi*frame/35.f);
    const float* parent=bank->nodes()[root].world;
    for(int k=1;k<24;++k){
        const int n=jointNode(*bank,"joint%d",k+1);if(n<0)continue;
        const float phase=Pi*2.f*(600.f*float(k))/13800.f;
        float local[12];std::memcpy(local,bank->nodes()[n].local,sizeof local);
        local[3]=600.f*float(k);local[7]=550.f*std::sin(phase*.5f)*bounce-910.f*phase;local[11]=0;
        poseJoint(v,*bank,n,parent,local);
    }
    d.submit(model,m,finishPose(v,model,{}));
}
// Toad's Factory stompers (Kinoko ObjectPress): on landing the crush clip
// plays forward at rate 2 for 15 frames, then back before the press rises.
void drawPress(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectPress*>(v.object);
    const int state=p.state();
    const float age=float(stateAge(d,v,state));
    const float last=std::max(clipFrames(v.main,v.clips[0])-1.f,0.f);
    float frame=0;
    if(state==3)frame=age<15.f?std::min(age*2.f,last):std::max(last+1.f-(age-15.f)*2.f,0.f);
    const C3D_Mtx m=liveMatrix(p);
    const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
    d.submit(far?v.low.model:v.main.model,m,animAt(far?v.low:v.main,frame));
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Toad's Factory item box lines (Kinoko ObjectItemboxLine, retail RMCP
// 8076d044): bricks ride the belt until the route point marked 2, after the
// side stomper (the line's ObjectPressSenko) has come down; from there VS
// races carry an item box instead, which the race items draw and collect.
void drawItemboxLine(ObjectDraw& d,Visual& v){
    const auto* rails=K::Field::RailManager::Instance();
    for(size_t i=0;i+1<v.parts.size();++i){
        const auto* part=v.parts[i];
        if(part->id()==ObjectId::Press){
            const auto& press=*static_cast<const K::Field::ObjectPress*>(part);
            const int state=press.state();
            const float age=float(stateAge(d,v,state));
            const float last=std::max(clipFrames(v.extra[1],v.clips[0])-1.f,0.f);
            float frame=0;
            if(state==3)frame=age<15.f?std::min(age*2.f,last):std::max(last+1.f-(age-15.f)*2.f,0.f);
            const C3D_Mtx m=liveMatrix(press);
            d.submit(v.extra[1].model,m,animAt(v.extra[1],frame));
            if(v.extra[2].model)d.shadow(v.extra[2].model,m);
        }else if(part->id()==ObjectId::Itembox){
            const auto& box=*static_cast<const K::Field::ObjectItemboxPress*>(part);
            const auto* rail=box.railInterpolator();
            if(box.state()==0||!rail||!rails)continue;
            const auto points=rails->rail(unsigned(rail->railIdx()))->points();
            size_t reveal=points.size();
            for(size_t k=0;k<points.size();++k)if(points[k].setting[1]==2){reveal=k;break;}
            if(size_t(rail->curPointIdx())>=reveal)continue;
            C3D_Mtx m=liveMatrix(box);m.r[1].w-=180.f; // the brick rests on the belt
            d.submit(v.main.model,m,{});
        }
    }
}
// Dry Dry Ruins pillars (Kinoko ObjectPillar): the column at its live
// transform as it topples, over its stationary base.
void drawPillar(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    d.submit(v.main.model,m,{});
    if(v.extra[0].model)d.submit(v.extra[0].model,v.placement,{});
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
// Lava columns (Kinoko ObjectFlamePoleV, retail RMCP init 806c3e90, enter
// erupting 806c4178, enter dormant 806c4478): hidden from init, through the
// start delay and while dormant; shown from each eruption to the end of its
// lowering (states 1-3) at Kinoko's position and scale. Their CHR0 and SRT0
// play at rate 1 from init.
void drawFlamePoleV(ObjectDraw& d,Visual& v){
    const auto& pole=*static_cast<const K::Field::ObjectFlamePoleV*>(v.object);
    const unsigned state=pole.stateId();
    if(state<1||state>3)return;
    d.submit(v.main.model,liveMatrix(pole),loopAt(v.main,float(d.tick)));
}
// Toad's Factory crate lines (Kinoko ObjectWoodboxW, retail RMCP 8077e1e4): the
// spawner has no model (0x8077ECD8). Each crate (WoodboxWSub) shows from its
// spawn (Breakable::enableCollision 0x8076ED70) until its route ends
// (0x8077E49C), at its live transform; the spawner is the last part.
void drawWoodboxLine(ObjectDraw& d,Visual& v){
    for(const auto* part:v.parts){
        if(part==v.object||static_cast<const K::Field::ObjectBreakable*>(part)->state()-1u>1u)continue;
        d.submit(v.main.model,liveMatrix(*part),{});
    }
}
// Delfino Square's drawbridge (RMCP ObjectTownBridge::calc 80809774): Kinoko
// tilts the object by the bridge angle (rot z) and retail stops its CHR0 on
// frame 89|angle|/45 (clamped 0..89) of that same angle; its SRT0 plays at
// rate 1 from load (0x8080A8DC, 0x8055F1E4).
void drawTownBridge(ObjectDraw& d,Visual& v){
    const float angle=std::abs(v.object->rot().z)*180.f/Pi;
    Anim a=loopAt(v.main,float(d.tick));
    a.frame=std::clamp(89.f*std::min(angle,45.f)/45.f,0.f,89.f);
    d.submit(v.main.model,liveMatrix(*v.object),a);
}
// SNES Ghost Valley 2 blocks (Kinoko ObjectObakeBlock; retail ObakeManager draw
// RMCP 8080b578): at rest an unrotated block at its position, while falling at
// Kinoko's position and tumble rotation, gone once the fall ends (256 steps).
// Retail's 325-unit cube is the cooked 250-unit block at the placement's 1.3.
void drawObakeBlock(ObjectDraw& d,Visual& v){
    using Fall=K::Field::ObjectObakeBlock::FallState;
    const auto& block=*static_cast<const K::Field::ObjectObakeBlock*>(v.object);
    if(block.fallState()==Fall::FinishedFalling)return;
    K::EGG::Matrix34f t;
    if(block.fallState()==Fall::Falling)t.makeRT(block.rot(),block.pos());
    else t.makeT(block.pos());
    v.drawn=objectMatrix(t,block.scale());v.drawnValid=true;
    d.submit(v.main.model,v.drawn,{});
}
// Rainbow Road's star rings (Kinoko ObjectStarRing; retail cannon class type 3, init 806bfddc,
// calc 806bfed0): StarRing_wait loops until a racer enters the cannon, then the first ring
// built plays StarRing_shot once and the wait starts again from frame 0. The SRT0 plays from load.
void drawStarRing(ObjectDraw& d,Visual& v){
    const auto& ring=*static_cast<const K::Field::ObjectStarRing*>(v.object);
    Anim a=loopAt(v.main,float(d.tick));
    a.clip=ring.shot()?v.clips[0]:v.clips[1];a.frame=ring.animFrame();
    d.submit(v.main.model,v.placement,a);
}
bool hidden(const Visual& v,int course){
    // Effects, sound sources and triggered splashes have no idle model; item
    // boxes are race items. Coconut Mall keeps its validated palm/escalator path.
    // The Factory line also owns its moving conveyor bricks, which are scenery.
    if(static_cast<ObjectId>(v.geo->id())==ObjectId::ItemboxLine)return false;
    // Arena coins (coin, pile_coin) are Coin Runners pickups, never Balloon Battle scenery.
    static const char* names[]={"itembox","pocha","pochaYogan","pochaMori","f_itembox","s_itembox","sin_itembox","w_itembox","coin","pile_coin"};
    for(const char* n:names)if(named(v,n))return true;
    if(course==5&&(named(v,"PalmTree")||named(v,"escalator")||named(v,"escalator_group")))return true;
    return false;
}
}
bool classifyObject(Visual& v,const ObjectAssets& assets,int course){
    if(hidden(v,course))return false;
    auto* main=assets.main(v.resource);
    v.main=assets.tracks(main);
    v.low=assets.tracks(assets.low(main));
    v.shadow=assets.tracks(assets.shadow(main));
    v.kinetic=v.object!=nullptr;
    v.variant=-1;
    v.present=drawGeneric;
    const auto id=static_cast<ObjectId>(v.geo->id());
    const bool live=v.object!=nullptr;
    if(classifyMover(v,assets))return true;
    if(named(v,"DonkyCannon_wii")||named(v,"DonkyCannonGC")||named(v,"tree_cannon")){v.present=drawDonkyCannon;return true;}
    if(id==ObjectId::Kuribo&&live){
        v.present=drawKuribo;v.clips[0]=clip(v.main,"walk_l");
    }else if(id==ObjectId::Wanwan&&live){
        v.present=drawWanwan;
        v.main=find(assets,"wanwan","wanwan","wanwan");
        v.shadow=find(assets,"wanwan","wanwan-shadow");
        v.extra[0]=find(assets,"wanwan","wanwan_chn");
        v.extra[1]=find(assets,"wanwan","pile");
    }else if(id==ObjectId::Hanachan&&live){
        v.present=drawHanachan;
        v.main=find(assets,"hanachan","hanachan");
        v.shadow=find(assets,"hanachan","hanachan-shadow");
        static const char* bodies[]={"hanachan_body1","hanachan_body2","hanachan_body3","hanachan_body4"};
        for(unsigned i=0;i<4;++i)v.extra[i]=find(assets,"hanachan",bodies[i]);
        v.extra[4]=find(assets,"hanachan","hanachan-body_shadow");
        v.clips={clip(v.main,"head_walk"),clip(v.main,"head_wait"),-1,-1,-1,-1,-1,-1};
    }else if(id==ObjectId::PenguinS&&live){
        v.present=drawPenguinSlide;
        v.clips={clip(v.main,"walk"),clip(v.main,"dive"),clip(v.main,"slider"),clip(v.main,"stand_up"),-1,-1,-1,-1};
    }else if(id==ObjectId::PenguinM&&live){
        v.present=drawPenguinWalk;v.clips[0]=clip(v.main,"walk");
    }else if((id==ObjectId::WLFirebarGC||id==ObjectId::KoopaFirebar||id==ObjectId::WLFireRingGC)&&live){
        // koopaFirebar's own model is the pivot block; all use the GCN fireball.
        v.present=id==ObjectId::WLFireRingGC?drawFireballs<K::Field::ObjectFireRing>:drawFireballs<K::Field::ObjectFirebar>;
        v.main=id==ObjectId::KoopaFirebar?find(assets,"koopaFirebar","koopaFirebar"):Tracks{};
        v.extra[0]=find(assets,"WLfirebarGC","WLfirebarGC");
        v.extra[1]=find(assets,"WLfirebarGC","fireBPlane","fireBPlane");
    }else if((id==ObjectId::KinokoUd||id==ObjectId::KinokoBend||id==ObjectId::KinokoNm)&&live){
        // Setting 0 picks the light or dark Mushroom Gorge palette. Bouncing and
        // bending mushrooms have red caps, the static ones green (getKclName).
        v.present=drawMushroom;
        const bool dark=v.geo->setting(0)==1;
        const char* cap=id==ObjectId::KinokoNm?(dark?"kinoko_d_g":"kinoko_g"):(dark?"kinoko_d_r":"kinoko_r");
        v.main=find(assets,"kinoko",cap);
        v.extra[0]=find(assets,"kinoko",dark?"kinoko_d_kuki":"kinoko_kuki");
        v.low=v.shadow={};
    }else if(id==ObjectId::Dossunc&&live){
        v.present=drawThwomp;v.main=find(assets,"dossun","dossun");v.shadow=find(assets,"dossun","dossun-shadow");
        v.clips[0]=pattern(v.main,"dossun");
    }else if((id==ObjectId::Choropu||id==ObjectId::Choropu2)&&live){
        v.present=drawChoropu;v.main=find(assets,"choropu","choropu");v.extra[0]=find(assets,"choropu","holl");
        v.clips[0]=clip(v.main,"start");v.clips[1]=clip(v.main,"jump");v.shadow={};
    }else if(id==ObjectId::Cow&&live){
        v.present=drawCow;
        v.clips={clip(v.main,"wait"),clip(v.main,"walk"),clip(v.main,"run"),clip(v.main,"eat_st"),clip(v.main,"eat"),clip(v.main,"eat_ed"),-1,-1};
    }else if(id==ObjectId::Heyho&&live){
        v.present=drawHeyho;
        v.clips={clip(v.main,"move"),clip(v.main,"jump"),clip(v.main,"jump_ed"),pattern(v.main,"body_color"),-1,-1,-1,-1};
    }else if(id==ObjectId::PakkunF&&live){
        v.present=drawPakkun;v.clips[0]=clip(v.main,"attack");v.clips[1]=clip(v.main,"wait");
    }else if(id==ObjectId::Crab&&live){
        v.present=drawCrab;v.clips={clip(v.main,"walk_l"),clip(v.main,"walk_r"),clip(v.main,"wait"),-1,-1,-1,-1,-1};
    }else if(id==ObjectId::Poihana&&live){
        v.present=drawPoihana;v.clips[0]=clip(v.main,"walk");v.clips[1]=pattern(v.main,"body_color");
    }else if((id==ObjectId::Seagull||id==ObjectId::Basabasa)&&live){
        v.present=drawFlock;v.clips[0]=clip(v.main,id==ObjectId::Seagull?"flying":"fly");
    }else if(id==ObjectId::VolcanoBall&&live){
        v.present=drawVolcanoBalls;
    }else if(id==ObjectId::FlamePole&&live){
        v.present=drawFlamePole;v.main=find(assets,"FlamePole","FlamePole","FlamePole");
        v.extra[0]=find(assets,"FlamePole","FlamePoleEff","FlamePoleEff");
    }else if((id==ObjectId::FlamePoleV||id==ObjectId::FlamePoleVBig)&&live){
        // Both load FlamePole_v (retail getResources 0x806C488C); the big one's
        // flow entry lists FlamePole_v_big first, an archive no course has.
        v.present=drawFlamePoleV;v.main=assets.tracks(assets.main("FlamePole_v"));v.low=v.shadow={};
    }else if(id==ObjectId::W_Woodbox&&live){
        // Retail's crate model is getKclName (0x8077ED7C): ironbox while
        // ObjectDirector +0x55 (time trials, ghost races) is set, else woodbox.
        const auto* config=K::System::RaceConfig::Instance();
        v.present=drawWoodboxLine;v.main=find(assets,"woodbox",config&&config->raceScenario().isTimeTrial()?"ironbox":"woodbox");
        v.low=v.shadow={};
    }else if(id==ObjectId::TownBridge&&live){
        v.present=drawTownBridge;
    }else if((id==ObjectId::ObakeBlockSFCc||id==ObjectId::ObakeBlock2SFCc||id==ObjectId::ObakeBlock3SFCc)&&live){
        v.present=drawObakeBlock;
    }else if(id==ObjectId::VolcanoPiece&&live){
        char name[24];std::snprintf(name,sizeof name,"VolcanoPiece%d",int(int16_t(v.geo->setting(0))));
        v.main=find(assets,v.resource,name,name);
    }else if(id==ObjectId::VolcanoRock&&live){
        const char* name=v.geo->setting(0)?"VolcanoRock2":"VolcanoRock1";
        v.main=find(assets,v.resource,name,name);
    }else if((id==ObjectId::CarBody||id==ObjectId::KartTruck)&&live){
        const bool truck=id==ObjectId::KartTruck;const unsigned paint=std::min<unsigned>(v.geo->setting(3),2);
        static const char* car[]={"K_car_b","K_car_r","K_car_y"};static const char* lorry[]={"K_truck_b","K_truck_o","K_truck_g"};
        v.present=drawHighwayCar;
        v.main=find(assets,v.resource,truck?lorry[paint]:car[paint]);
        v.extra[0]=find(assets,v.resource,truck?"K_truck_tire":"K_car_tire",truck?"K_truck_tire":"K_car_tire");
        v.extra[1]=find(assets,v.resource,truck?"K_truck_light":"K_car_light");
        v.shadow=find(assets,v.resource,truck?"K_truck-shadow":"K_car_body-shadow");
    }else if((id==ObjectId::FireSnake||id==ObjectId::FireSnakeV)&&live){
        v.present=drawFireSnake;v.main=find(assets,"FireSnake","FireSnake","FireSnake");
        v.extra[0]=find(assets,"FireSnake","FireSnake_eye");v.shadow=find(assets,"FireSnake","FireSnake-shadow");
    }else if(id==ObjectId::KoopaBall&&live){
        v.present=drawKoopaBall;v.main=find(assets,"koopaBall","koopaBall");
        v.extra[0]=find(assets,"koopaBall","ef_koopaBall","ef_koopaBall");v.shadow=find(assets,"koopaBall","koopaBall-shadow");
    }else if(id==ObjectId::HeyhoBallGBA&&live){
        v.present=drawHeyhoBall;v.clips[0]=color(v.main,"HeyhoBallGBA_blink");
    }else if(named(v,"koopaFigure")){
        v.present=drawKoopaFigure;v.clips[0]=clip(v.main,"wait");v.clips[1]=clip(v.main,"vomit");
    }else if(id==ObjectId::Hwanwan&&live){
        v.present=drawHwanwan;v.main=find(assets,"wanwan","wanwan","wanwan");v.shadow=find(assets,"wanwan","wanwan-shadow");
    }else if(id==ObjectId::TruckWagon&&live){
        v.present=drawTruckWagon;v.main=find(assets,"TruckWagon","TruckWagon");v.low=find(assets,"TruckWagon","TruckWagon_LOD");
        v.shadow=find(assets,"TruckWagon","TruckWagon-shadow");
        v.clips[0]=clip(v.main,"TruckWagon_run");v.clips[1]=scroll(v.main,"TruckWagon_tire");
    }else if(id==ObjectId::Psea&&live){
        v.present=drawTide;static const char* seas[]={"Psea1sand","Psea2dark","Psea3nami","Psea4tex","Psea5spc"};
        for(unsigned i=0;i<5;++i)v.extra[i]=find(assets,"Psea",seas[i],seas[i]);
    }else if(id==ObjectId::Aurora&&live){
        v.present=drawAurora;
    }else if(id==ObjectId::TwistedWay&&live){
        v.present=drawTwistedWay;
    }else if(id==ObjectId::Turibashi&&live){
        v.present=drawTuribashi;
    }else if(id==ObjectId::Ami&&live){
        v.present=drawAmi;
    }else if(id==ObjectId::ItemboxLine&&live){
        v.present=drawItemboxLine;v.main=find(assets,"Block","Block");
        v.extra[1]=find(assets,"Press","Press");v.extra[2]=find(assets,"Press","Press-shadow");v.clips[0]=clip(v.extra[1],"Press");
        v.low=v.shadow={};
    }else if(id==ObjectId::Press&&live){
        v.present=drawPress;v.clips[0]=clip(v.main,"Press");
    }else if(id==ObjectId::DCPillar&&live){
        v.present=drawPillar;v.extra[0]=find(assets,v.resource,"dc_pillar_base");
    }else if(named(v,"CarB")||named(v,"b_teresa")||named(v,"BGteresaSFC")){
        // Three authored variants and no setting choosing one: spread them
        // over the placements.
        static const char* car[]={"","1","2"};static const char* boo[]={"","2","3"};
        char model[40];std::snprintf(model,sizeof model,"%s%s",v.resource,(named(v,"CarB")?car:boo)[v.index%3]);
        if(auto* m=assets.find(v.resource,model)){v.main=assets.tracks(m);v.low=assets.tracks(assets.low(m));v.shadow=assets.tracks(assets.shadow(m));}
    }else if((id==ObjectId::KBombCar||id==ObjectId::KBombCar1)&&live){
        v.present=drawBombCarLive;v.main=find(assets,v.resource,"K_bomb_car");
        v.extra[0]=find(assets,v.resource,"Kbomneji","Kbomneji");v.extra[1]=find(assets,v.resource,"K_bomb_tire00","K_bomb_tire00");
        v.shadow=find(assets,v.resource,"K_bomb_car-shadow");v.low={};
    }else if(id==ObjectId::BeltCurveA){
        v.present=drawBeltCurve;v.kinetic=false;
        static const char* overlays[]={"BeltCurveB","BeltCurveB-low","BeltCurveC","BeltCurveC-low"};
        static const char* fades[]={"B_on_off","Blow_on_off","C_on_off","Clow_on_off"};
        for(unsigned i=0;i<4;++i){v.extra[i]=find(assets,v.resource,overlays[i]);v.extra[i].clr=color(v.extra[i],fades[i]);}
    }else if(id==ObjectId::BeltEasy){
        v.rate=.9f;
    }else if(id==ObjectId::BeltCrossing){
        v.rate=1.25f;
    }else if(named(v,"StarRing")){
        v.main=assets.tracks(v.main.model,"StarRing_wait");
        if(live){v.present=drawStarRing;v.clips[0]=clip(v.main,"StarRing_shot");v.clips[1]=clip(v.main,"StarRing_wait");}
    }else if(named(v,"pylon01")){
        v.variant=v.geo->setting(0);
    }else if(named(v,"sun")||named(v,"SpaceSun")||named(v,"EarthRing")){
        // Suns and the Earth's glow ring are camera-facing sprites.
        v.present=drawSun;
    }
    if(v.present==drawGeneric&&!v.main.model)return false;
    if(v.present==drawTide)return v.extra[0].model!=nullptr;
    return true;
}
void drawObject(ObjectDraw& d,Visual& v){if(v.present)v.present(d,v);}
bool skyObject(const Visual& v){return v.present==drawSun;}
bool waterObject(const Visual& v){return v.present==drawTide;}
const K::Field::ObjectBase* reviewFocus(const Visual& v){
    if(v.present==drawTruckWagon){
        for(size_t i=0;i+1<v.parts.size();++i)if(static_cast<const K::Field::ObjectTruckWagonCart*>(v.parts[i])->isActive())return v.parts[i];
    }else if((v.present==drawHwanwan||v.present==drawCow||v.present==drawFlock||v.present==drawThwomp)&&v.parts.size()>1)return v.parts[0];
    else if(v.present==drawHanachan){
        const auto& h=*static_cast<const K::Field::ObjectHanachan*>(v.object);
        if(h.partCount()&&h.part(0))return h.part(0);
    }
    return v.object;
}
}
