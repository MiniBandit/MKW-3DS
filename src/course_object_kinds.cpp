#include <game/field/obj/ObjectKarehayama.hh>
#include <game/system/RaceConfig.hh>
#include <game/field/obj/ObjectChimney.hh>
#include "mkw/course_object_kinds.hpp"
#include "mkw/factory_belts.hpp"
#include "mkw/joint_billboard.hpp"
#include "mkw/course_mii_statue.hpp"
#include "mkw/render_quality.hpp"
#include <game/field/ObjectDirector.hh>
#include <game/field/obj/ObjectBase.hh>
#include <game/field/obj/ObjectBasabasa.hh>
#include <game/field/obj/ObjectBird.hh>
#include <game/field/obj/ObjectCarTGE.hh>
#include <game/field/obj/ObjectChoropu.hh>
#include <game/field/obj/ObjectCow.hh>
#include <game/field/obj/ObjectCrab.hh>
#include <game/field/obj/ObjectParasolR.hh>
#include <game/field/obj/ObjectDossun.hh>
#include <game/field/obj/ObjectFireRing.hh>
#include <game/field/obj/ObjectFirebar.hh>
#include <game/field/obj/ObjectFlamePole.hh>
#include <game/field/obj/ObjectFlamePoleFoot.hh>
#include <game/field/obj/ObjectHanachan.hh>
#include <game/field/obj/ObjectDokan.hh>
#include <game/field/obj/ObjectItemboxLine.hh>
#include <game/field/obj/ObjectWoodbox.hh>
#include <game/field/obj/ObjectWoodboxWSub.hh>
#include <game/field/obj/ObjectFireSnake.hh>
#include <game/field/obj/ObjectHeyho.hh>
#include <game/field/obj/ObjectHwanwan.hh>
#include <game/field/obj/ObjectRock.hh>
#include <game/field/obj/ObjectHeyhoBall.hh>
#include <game/field/obj/ObjectItemboxPress.hh>
#include <game/field/RailManager.hh>
#include <game/field/obj/ObjectKoopaBall.hh>
#include <game/field/obj/ObjectKoopaFigure.hh>
#include <game/field/obj/ObjectSanbo.hh>
#include <game/field/obj/ObjectCannon.hh>
#include <game/field/obj/ObjectCarB.hh>
#include <game/field/obj/ObjectBGTeresa.hh>
#include <game/field/obj/ObjectTeresa.hh>
#include <game/field/obj/ObjectTruckWagon.hh>
#include <game/field/obj/ObjectTwistedWay.hh>
#include <game/field/obj/ObjectTownBridge.hh>
#include <game/field/obj/ObjectKuribo.hh>
#include <game/field/obj/ObjectPakkunF.hh>
#include <game/field/obj/ObjectPenguin.hh>
#include <game/field/obj/ObjectPoihana.hh>
#include <game/field/obj/ObjectPress.hh>
#include <game/field/obj/ObjectVolcanoBall.hh>
#include <game/field/obj/ObjectVolcanoBallLauncher.hh>
#include <game/field/obj/ObjectWanwan.hh>
#include <game/system/RaceManager.hh>
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

// The original star/meteor object rotates its entire model independently
// of CHR0; both distance variants receive the same current transform.
void drawInseki(ObjectDraw& d,Visual& v){
    const float angle=v.inseki.update(d.tick,int16_t(v.geo->setting(1)),
        int16_t(v.geo->setting(2)),int16_t(v.geo->setting(3)));
    K::EGG::Matrix34f placement,spin;
    placement.makeRT(v.geo->rot()*0.01745329238474369f,v.geo->pos());
    spin.setAxisRotation(angle,K::EGG::Vector3f::ez);
    const auto rotated=placement.multiplyTo(spin);
    C3D_Mtx matrix=objectMatrix(rotated,v.geo->scale());
    // The single-screen full bank animates six fragments; its low bank has
    // no animated joints. The port's distance-only switch snaps their poses.
    // Keep this small decorative model on its authored full animation.
    d.submit(v.main.model,matrix,loopAt(v.main,float(d.tick)*v.rate+v.phase));
    v.drawn=matrix;v.drawnValid=true;
}
void drawGeneric(ObjectDraw& d,Visual& v){
    if(v.object&&unsigned(v.object->id())==0x176&&!static_cast<const K::Field::ObjectKarehayama*>(v.object)->shown())return;
    if(v.object&&(unsigned(v.object->id())==0x14c||unsigned(v.object->id())==0x2c0)&&
        !static_cast<const K::Field::ObjectChimney*>(v.object)->modelVisible())return;
    if(v.object&&v.object->id()==ObjectId::Woodbox&&
        !static_cast<const K::Field::ObjectWoodbox*>(v.object)->shown())return;
    if(v.object&&v.object->id()==ObjectId::DKRockGC&&
        !static_cast<const K::Field::ObjectRock*>(v.object)->visible())return;
    const C3D_Mtx m=v.kinetic&&v.object?liveMatrix(*v.object):v.placement;
    const float frame=float(d.tick)*v.rate+v.phase;
    const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
    const Tracks& t=far?v.low:v.main;
    Anim a=loopAt(t,frame);
    if(v.object&&v.object->id()==ObjectId::TownBridge)
        a.frame=static_cast<const K::Field::ObjectTownBridge*>(v.object)->animationFrame();
    if(v.variant>=0)a.patFrame=a.clrFrame=float(v.variant);
    d.submit(t.model,m,a);
    if(v.shadow.model){
        if(v.object&&v.object->id()==ObjectId::DKRockGC){
            const auto& rock=*static_cast<const K::Field::ObjectRock*>(v.object);
            d.shadow(v.shadow.model,objectMatrix(rock.shadowTransform(),rock.shadowScale()));
        }else d.shadow(v.shadow.model,m);
    }
}
// Retail statues attach an additional RFL model at face_1. The 3DS path
// uses an offline portrait instead; no head assembly or capture during a race.
void drawMiiStatue(ObjectDraw& d,Visual& v){
    drawGeneric(d,v);
    const auto* binding=miiStatueBinding(v.geo->id());
    const auto* source=v.main.model;
    const auto* head=v.extra[5].model;
    if(!binding||!source||!source->bank||!head)return;
    const auto& bank=*source->bank;
    const int nodeIndex=bank.node("face_1");
    if(nodeIndex<0)return;
    const auto& node=bank.nodes()[nodeIndex];
    const float* face=node.world;
    if(node.slot>=0&&unsigned(node.slot)<bank.matrixCount()){
        v.palette.resize(bank.matrixCount()*12);
        bank.pose(v.main.clip,float(d.tick)*v.rate+v.phase,v.palette.data());
        face=v.palette.data()+node.slot*12;
    }
    C3D_Mtx joint;Mtx_Identity(&joint);
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)joint.r[r].c[3-c]=face[r*4+c];
    // Init retains (0,-pi/2,pi); SetParams changes translation and scale.
    K::EGG::Matrix34f local;
    local.makeRT({0,-1.5707963705062866f,3.1415927410125732f},{binding->x,binding->y,0});
    const C3D_Mtx offset=objectMatrix(local,{binding->scale,binding->scale,binding->scale});
    C3D_Mtx attached,world;
    Mtx_Multiply(&attached,&joint,&offset);
    const C3D_Mtx body=v.kinetic&&v.object?liveMatrix(*v.object):v.placement;
    Mtx_Multiply(&world,&body,&attached);
    // Baked portrait is centered at RFL Y=34.5; billboard around that point,
    // preserving original column lengths and projected local-Y roll.
    float rows[12],view[12];
    for(unsigned r=0;r<3;++r){
        world.r[r].w+=world.r[r].y*34.5f;
        for(unsigned c=0;c<4;++c){rows[r*4+c]=world.r[r].c[3-c];view[r*4+c]=d.view.view.r[r].c[3-c];}
    }
    if(!billboardJoint(rows,view))return;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)world.r[r].c[3-c]=rows[r*4+c];
    if(d.view.visible(*head->model,world))d.plain(*head->model,world);
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
    if(!k.visible())return;
    const C3D_Mtx m=liveMatrix(k);
    // Kinoko's walk timer is the CHR0 frame that gates each step.
    Anim a;a.clip=v.clips[0];a.frame=k.animTimer();
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
    for(size_t i=0;i<h.partCount();++i){
        const auto* part=h.part(i);if(!part)continue;
        C3D_Mtx m=liveMatrix(*part);
        const auto& offset=static_cast<const K::Field::ObjectHanachanPart*>(part)->hitOffset();
        m.r[0].w+=offset.x;m.r[1].w+=offset.y;m.r[2].w+=offset.z;
        if(i==0){
            Anim a;a.clip=v.clips[h.headAnimation()];a.frame=h.animationFrame(i);
            d.submit(v.main.model,m,a);
            if(v.shadow.model)d.shadow(v.shadow.model,m);
        }else{
            const Tracks& body=v.extra[(i-1)%4];
            Anim a;a.clip=body.model&&body.model->bank?body.model->bank->clip(h.bodyWaiting(i-1)?"wait":"walk"):-1;a.frame=h.animationFrame(i);
            d.submit(body.model,m,a);
            if(v.extra[4].model)d.shadow(v.extra[4].model,m);
        }
    }
}
void drawPuchiPakkun(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectDokan*>(v.object);
    const auto m=liveMatrix(p);
    const bool far=v.low.model&&length(position(m)-d.camera)>renderQuality().objectLowDistance;
    const Tracks& tracks=far?v.low:v.main;
    d.submit(tracks.model,m,loopAt(tracks,p.animationFrame()));
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawPenguinSlide(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectPenguinS*>(v.object);
    const C3D_Mtx m=liveMatrix(p);
    Anim a;a.frame=p.animationFrame();
    switch(p.presentationState()){
    case 2: // Diving onto its belly: Kinoko counts the dive clip down.
        a.clip=v.clips[1];if(!p.spinning())a.frame=clipFrames(v.main,a.clip)-float(p.anmTimer());break;
    case 3:a.clip=v.clips[2];break;
    case 4:a.clip=v.clips[3];if(!p.spinning())a.frame=clipFrames(v.main,a.clip)-float(p.anmTimer());break;
    default:a.clip=v.clips[0];break;
    }
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawPenguinWalk(ObjectDraw& d,Visual& v){
    const C3D_Mtx m=liveMatrix(*v.object);
    Anim a;a.clip=v.clips[0];a.frame=static_cast<const K::Field::ObjectPenguin*>(v.object)->animationFrame();
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
template<class T> void drawFireballs(ObjectDraw& d,Visual& v){
    const auto& owner=*static_cast<const T*>(v.object);
    if(v.main.model)d.submit(v.main.model,liveMatrix(owner),animAt(v.main,float(d.tick)));
    const float frame=float(d.tick);
    for(const auto* ball:owner.fireballs()){
        if(!ball)continue;
        // Retail child ObjectFireball loads only fireBPlane; the opaque
        // WLfirebarGC sphere belongs to the parent pivot, not every child.
        C3D_Mtx m=liveMatrix(*ball);
        // The parent can update the child position after its model tick.
        m.r[0].w=ball->pos().x;m.r[1].w=ball->pos().y;m.r[2].w=ball->pos().z;
        auto a=animAt(v.extra[1],frame);
        if(v.extra[1].model&&v.extra[1].model->bank){
            const auto& bank=*v.extra[1].model->bank;
            v.palette.resize(bank.matrixCount()*12);bank.pose(a.clip,a.frame,v.palette.data());
            C3D_Mtx mv;Mtx_Multiply(&mv,&d.view.view,&m);
            float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=mv.r[r].c[3-c];
            // Retail fireBPlane has a root BILLBOARD_ROT (mode 3) node.
            // With no parent, its projected local Y supplies the roll.
            const int index=bank.node("ef_fireBplane");
            if(index>=0){
                const auto& node=bank.nodes()[index];
                if(node.slot>=0&&unsigned(node.slot)<bank.matrixCount())billboardJoint(v.palette.data()+node.slot*12,rows);
            }
            a.palette=v.palette.data();
        }
        if(v.extra[1].model)d.submit(v.extra[1].model,m,a);
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
    // Moving moles register their ground cylinders before the actual hole.
    const K::Field::ObjectBase* hole=c.hole();
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
        Anim a;a.frame=float(cow.soundAnimationFrame());
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
    if(p.damaged()){a.clip=v.clips[2];a.frame=float(p.damageFrame());}
    else if(p.attacking()){a.clip=v.clips[0];a.frame=float(p.attackFrame());}
    else{a.clip=v.clips[1];a.frame=float(p.waitAnimationFrame());}
    if(p.turnAngle()!=0.f&&v.main.model&&v.main.model->bank){
        auto& model=*v.main.model;const auto& bank=*model.bank;
        K::EGG::Matrix34f rotation;rotation.setAxisRotation(p.turnAngle(),K::EGG::Vector3f::ex);
        float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=rotation[r,c];
        const ObjectBank::JointCallback callbacks[]={{"mouth_under_end",rows},{"skil_root",rows}};
        v.palette.resize(size_t(bank.matrixCount())*12);
        if(bank.poseCallbacks(a.clip,a.frame,callbacks,2,v.palette.data(),v.callbackScratch)){
            a.palette=v.palette.data();
            if(model.slots.empty())bank.slotBounds(model.slots,model.slotUsed);
            bool first=true;
            for(unsigned i=0;i<model.slots.size();++i)if(model.slotUsed[i]){
                const auto b=transformBounds(model.slots[i],v.palette.data()+size_t(i)*12);
                if(first){v.posed=b;first=false;}else{v.posed.include(b.minimum);v.posed.include(b.maximum);}
            }
            if(!first)a.bounds=&v.posed;
        }
    }
    d.submit(v.main.model,liveMatrix(p),a);
}
void drawCrab(ObjectDraw& d,Visual& v){
    const auto& c=*static_cast<const K::Field::ObjectCrab*>(v.object);
    if(!c.visible())return;
    Anim a;a.frame=c.animationFrame();
    a.clip=v.clips[c.animationClip()];
    const C3D_Mtx m=liveMatrix(c);
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawParasol(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectParasolR*>(v.object);
    const char* name=p.opened()?"open":"close";
    Anim a;a.clip=clip(v.main,name);a.frame=p.animationFrame();
    const auto m=liveMatrix(p);d.submit(v.main.model,m,a);
    if(v.shadow.model){a.clip=clip(v.shadow,name);d.shadow(v.shadow.model,m,a);}
}
// Cataquack state/clock are simulation-owned; setting 0 selects body colour.
void drawPoihana(ObjectDraw& d,Visual& v){
    const auto& p=*static_cast<const K::Field::ObjectPoihana*>(v.object);
    Anim a;a.clip=!std::strcmp(p.animationName(),"throw")?v.clips[2]:
        !std::strcmp(p.animationName(),"overturn")?v.clips[3]:v.clips[0];
    a.frame=p.animationFrame();a.pat=v.clips[1];a.patFrame=float(v.geo->setting(0));
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
        if(bats&&!static_cast<const K::Field::ObjectBasabasaDummy*>(part)->visible())continue;
        const Vec3 p=vec(part->pos());
        const Vec3 delta=p-v.points[i];
        if(dot(delta,delta)>.01f&&dot(delta,delta)<1e6f)v.velocities[i]=delta;
        v.points[i]=p;
        Anim a;a.clip=v.clips[0];a.frame=float(d.tick)+std::fmod(float(i*37+v.index*11),frames);
        const bool hit=bats&&static_cast<const K::Field::ObjectBasabasaDummy*>(part)->stateId()==1;
        if(hit)a.frame+=6.f*static_cast<const K::Field::ObjectBasabasaDummy*>(part)->stateFrame();
        const auto transform=hit?liveMatrix(*part):facingMatrix(p,v.velocities[i],vec(part->scale()));
        if(bats&&v.main.model&&v.main.model->bank){
            const auto& bank=*v.main.model->bank;
            v.palette.resize(bank.matrixCount()*12);bank.pose(a.clip,a.frame,v.palette.data());
            C3D_Mtx mv;Mtx_Multiply(&mv,&d.view.view,&transform);
            float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=mv.r[r].c[3-c];
            // Both retail basabasa archives mark exactly these nodes BILLBOARD=1.
            for(const char* name:{"basabasa_body","basabasa_face","basabasa_nose"}){
                const int index=bank.node(name);if(index<0)continue;
                const auto& node=bank.nodes()[index];
                if(node.slot>=0&&unsigned(node.slot)<bank.matrixCount())billboardJoint(v.palette.data()+node.slot*12,rows);
            }
            a.palette=v.palette.data();
        }
        d.submit(v.main.model,transform,a);
    }
}
// Grumble Volcano lava balls: hidden while dormant (Kinoko state 0).
void drawVolcanoBalls(ObjectDraw& d,Visual& v){
    const auto& launcher=*static_cast<const K::Field::ObjectVolcanoBallLauncher*>(v.object);
    for(const auto* ball:launcher.balls()){
        // Retail ToggleVisible(1) on launch, ToggleVisible(0) while burning.
        // The landed fire is an effect, not the falling stone mesh/shadow.
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
// despawned or in the one-update hit state (retail states 0 and 5).
void drawFireSnake(ObjectDraw& d,Visual& v){
    const auto& s=*static_cast<const K::Field::ObjectFireSnake*>(v.object);
    if(s.stateId()==0||s.stateId()==5)return;
    const float frame=float(d.tick);
    const C3D_Mtx m=liveMatrix(s);
    const auto segment=[&](const C3D_Mtx& transform){
        auto a=animAt(v.main,frame);
        if(v.main.model&&v.main.model->bank){
            const auto& bank=*v.main.model->bank;
            v.palette.resize(bank.matrixCount()*12);bank.pose(a.clip,a.frame,v.palette.data());
            const int index=bank.node("ef_fireSnakePl");
            if(index>=0){
                const auto& node=bank.nodes()[index];
                if(node.slot>=0&&unsigned(node.slot)<bank.matrixCount()){
                    C3D_Mtx mv;Mtx_Multiply(&mv,&d.view.view,&transform);
                    float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=mv.r[r].c[3-c];
                    // USA80065e80 -> 800657e0: ROT takes the XY Y-axis of
                    // childWorld*inverse(parentWorld), not model-view roll.
                    // Both original FireSnake archives have an identity local
                    // child, no CHR: the product is identity even though the
                    // sphere parent is rotated Z90. Only the flame faces us.
                    const float roll[2]={0,1};
                    billboardJoint(v.palette.data()+node.slot*12,rows,roll);
                }
            }
            a.palette=v.palette.data();
        }
        d.submit(v.main.model,transform,a);
    };
    segment(m);
    if(v.extra[0].model)d.submit(v.extra[0].model,m,{});
    for(size_t i=0;i<2;++i)if(const auto* kid=s.kid(i))segment(liveMatrix(*kid));
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
// its route the common Bob-omb explosion uses an independent world-space pose.
void drawKoopaBall(ObjectDraw& d,Visual& v){
    const auto& b=*static_cast<const K::Field::ObjectKoopaBall*>(v.object);
    const int state=b.state();
    const C3D_Mtx m=liveMatrix(b);
    if(state==0){
        d.submit(v.main.model,m,{});
        if(v.extra[0].model)d.submit(v.extra[0].model,m,animAt(v.extra[0],float(d.tick)));
        if(v.shadow.model)d.shadow(v.shadow.model,m);
    }else if(state==2){
        // USA8075ecb0..8075ed34: identity axes at the ball position,
        // ModelDirector::SetScale(2). CHR0 already expands the bomb core;
        // neither the rolling ball pose nor its collision radius belongs here.
        drawExplosion(d,basisMatrix({1,0,0},{0,1,0},{0,0,1},vec(b.pos()),{2,2,2}),
                      float(int(b.explodeFrames())-b.explodeTimer()));
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
        // USA806eb28c..806eb344: original blast radius / authored 900-unit core.
        const float scale=1500.f/900.f;
        drawExplosion(d,basisMatrix({1,0,0},{0,1,0},{0,0,1},vec(b.pos()),{scale,scale,scale}),float(b.stateFrame()));
        return;
    }
    const C3D_Mtx m=liveMatrix(b);
    Anim a;
    if(state==2){a.clr=v.clips[0];a.clrFrame=float(b.stateFrame());}
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawSanbo(ObjectDraw& d,Visual& v){
    const auto& pokey=*static_cast<const K::Field::ObjectSanbo*>(v.object);
    if(pokey.scattered()){
        for(unsigned i=0;i<4;++i){
            const auto& pose=pokey.partTransform(i);
            d.submit(v.extra[i?1:0].model,objectMatrix(pose,pokey.scale()),{});
            // USA80766fb0: flying-piece shadow uses translation only, not its tumble.
            if(v.shadow.model)d.shadow(v.shadow.model,basisMatrix({1,0,0},{0,1,0},{0,0,1},
                    vec(pose.base(3)),vec(pokey.scale())));
        }
        return;
    }
    const auto m=liveMatrix(pokey);
    Anim a=loopAt(v.main,pokey.animationFrame());
    a.clip=v.clips[pokey.reviving()?2:pokey.flinching()?1:0];a.frame=pokey.animationFrame();
    d.submit(v.main.model,m,a);
    if(v.shadow.model)d.shadow(v.shadow.model,m);
}
void drawTeresa(ObjectDraw& d,Visual& v){
    const auto& controller=*static_cast<const K::Field::ObjectTeresa*>(v.object);
    for(const auto& ghost:controller.ghosts()){
        if(!ghost.alpha)continue;
        const auto& tracks=v.extra[ghost.variant];
        Anim a=loopAt(tracks,float(d.tick));
        a.opacity=float(ghost.alpha)/255.f;a.mirrorU=ghost.mirrorU;
        auto p=d.camera+vec(ghost.offset)+vec(ghost.displacement);p.y=std::max(3000.f,p.y);
        C3D_Mtx m=d.view.cameraFacing(p,1.f);
        const auto scale=v.geo->scale();Mtx_Scale(&m,scale.x,scale.y,scale.z);
        d.submit(tracks.model,m,a);
    }
}
void drawBGTeresa(ObjectDraw& d,Visual& v){
    const auto& boo=*static_cast<const K::Field::ObjectBGTeresa*>(v.object);
    if(!boo.alpha())return;
    Anim a=loopAt(v.main,float(d.tick));
    // Retail changes K3.a, the multiplier of texture alpha in lambert2.
    a.opacity=float(boo.alpha())/255.f;a.mirrorU=boo.mirrorU();
    C3D_Mtx m=d.view.cameraFacing(vec(boo.pos()),1.f);
    const auto scale=boo.scale();Mtx_Scale(&m,scale.x,scale.y,scale.z);
    d.submit(v.main.model,m,a);
}
void drawCarB(ObjectDraw& d,Visual& v){
    const auto& car=*static_cast<const K::Field::ObjectCarB*>(v.object);
    const auto& tracks=v.extra[car.variant()];
    d.submit(tracks.model,v.placement,loopAt(tracks,float(d.tick)));
}
void drawCannon(ObjectDraw& d,Visual& v){
    if(!v.object)return;
    const auto& cannon=*static_cast<const K::Field::ObjectCannon*>(v.object);
    Anim a=loopAt(v.main,float(d.tick));
    a.clip=v.clips[cannon.starRing()&&!cannon.firing()?1:0];a.frame=cannon.animationFrame();
    d.submit(v.main.model,v.placement,a);
}
// The simulation starts mouth preparation 42 frames before ball release.
void drawKoopaFigure(ObjectDraw& d,Visual& v){
    if(!v.object)return;
    const auto& statue=*static_cast<const K::Field::ObjectKoopaFigure*>(v.object);
    Anim a;a.clip=v.clips[statue.vomiting()?1:0];a.frame=statue.animationFrame();
    d.submit(v.main.model,v.placement,a);
}
// DS Peach Gardens chomps roam as the manager's child object.
void drawHwanwan(ObjectDraw& d,Visual& v){
    if(v.parts.size()<2)return;
    const auto& w=*static_cast<const K::Field::ObjectHwanwan*>(v.parts[0]);
    const C3D_Mtx m=objectMatrix(w.modelTransform(),w.scale());
    d.submit(v.main.model,m,animAt(v.main,w.animationFrame()));
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
        }else if(part->id()==ObjectId::ItemboxLine){
            const auto& block=*static_cast<const K::Field::ObjectItemboxBlock*>(part);
            if(block.shown())d.submit(v.main.model,liveMatrix(block),{});
        }
    }
}
void drawWoodboxLine(ObjectDraw& d,Visual& v){
    for(size_t i=0;i+1<v.parts.size();++i){
        const auto& box=*static_cast<const K::Field::ObjectWoodboxWSub*>(v.parts[i]);
        if(box.shown())d.submit(v.main.model,liveMatrix(box),{});
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
bool hidden(const Visual& v,int course){
    // Effects, sound sources and triggered splashes have no idle model; item
    // boxes are race items. Coconut Mall keeps its validated palm/escalator path.
    static const char* names[]={"itembox","pocha","pochaYogan","pochaMori","f_itembox","s_itembox","sin_itembox","w_itembox"};
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
    // DK's low asset replaces the live display with a split DK-logo atlas
    // (18 vertices); the live wl_screen1 face uses only 6. Its frame has the
    // same 570 vertices, so retaining the live mesh is also slightly cheaper.
    // Do not switch back to the baked placeholder when the monitor is distant.
    if(course==6&&named(v,"dkmonitor"))v.low={};
    v.shadow=assets.tracks(assets.shadow(main));
    v.kinetic=v.object!=nullptr;
    v.variant=-1;
    v.present=drawGeneric;
    const auto id=static_cast<ObjectId>(v.geo->id());
    const bool live=v.object!=nullptr;
    if(classifyMover(v,assets))return true;
    if(miiStatueBinding(v.geo->id())&&main&&main->bank&&main->bank->node("face_1")>=0){
        const auto* race=K::System::RaceConfig::Instance();
        if(!race||!miiStatuePlayersEligible(race->raceScenario().players,race->raceScenario().playerCount))return true;
        // Eligible-race identity selection remains a prepared-pool candidate;
        // it does not yet implement official-database filtering and weights.
        char name[16];std::snprintf(name,sizeof name,"face_%u",v.index%6);
        v.extra[5]=assets.tracks(assets.find("__MiiStatueHeads",name));
        if(v.extra[5].model)v.present=drawMiiStatue;
        return true;
    }
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
        v.clips={clip(v.main,"head_walk"),clip(v.main,"head_wait"),clip(v.main,"head_wait2"),-1,-1,-1,-1,-1};
    }else if(id==ObjectId::PenguinS&&live){
        v.present=drawPenguinSlide;
        v.clips={clip(v.main,"walk"),clip(v.main,"dive"),clip(v.main,"slider"),clip(v.main,"stand_up"),-1,-1,-1,-1};
    }else if((id==ObjectId::PenguinM||id==ObjectId::PenguinL)&&live){
        v.present=drawPenguinWalk;v.clips[0]=clip(v.main,"walk");
    }else if((id==ObjectId::WLFirebarGC||id==ObjectId::KoopaFirebar||id==ObjectId::WLFireRingGC)&&live){
        // Parent pivot models are separate from each child's fireBPlane.
        v.present=id==ObjectId::WLFireRingGC?drawFireballs<K::Field::ObjectFireRing>:drawFireballs<K::Field::ObjectFirebar>;
        v.main=id==ObjectId::KoopaFirebar?find(assets,"koopaFirebar","koopaFirebar"):
            id==ObjectId::WLFirebarGC?find(assets,"WLfirebarGC","WLfirebarGC"):Tracks{};
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
    }else if(id==ObjectId::PuchiPakkun&&live){
        v.present=drawPuchiPakkun;
    }else if(id==ObjectId::PakkunF&&live){
        v.present=drawPakkun;v.clips[0]=clip(v.main,"attack");v.clips[1]=clip(v.main,"wait");v.clips[2]=clip(v.main,"damage");
    }else if(id==ObjectId::ParasolR&&live){
        v.present=drawParasol;
        v.main=find(assets,"r_parasol","r_parasol");
        v.shadow=find(assets,"r_parasol","r_parasol-shadow");
    }else if(id==ObjectId::Crab&&live){
        v.present=drawCrab;v.clips={clip(v.main,"walk_l"),clip(v.main,"walk_r"),clip(v.main,"wait"),-1,-1,-1,-1,-1};
    }else if(id==ObjectId::Poihana&&live){
        v.present=drawPoihana;v.clips[0]=clip(v.main,"walk");v.clips[1]=pattern(v.main,"body_color");v.clips[2]=clip(v.main,"throw");v.clips[3]=clip(v.main,"overturn");
    }else if((id==ObjectId::Seagull||id==ObjectId::Basabasa)&&live){
        v.present=drawFlock;v.clips[0]=clip(v.main,id==ObjectId::Seagull?"flying":"fly");
    }else if(id==ObjectId::VolcanoBall&&live){
        v.present=drawVolcanoBalls;
    }else if(id==ObjectId::FlamePole&&live){
        v.present=drawFlamePole;v.main=find(assets,"FlamePole","FlamePole","FlamePole");
        v.extra[0]=find(assets,"FlamePole","FlamePoleEff","FlamePoleEff");
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
    }else if(id==ObjectId::Sanbo&&live){
        v.present=drawSanbo;v.clips[0]=clip(v.main,"move");v.clips[1]=clip(v.main,"damage");v.clips[2]=clip(v.main,"revive");
        v.extra[0]=find(assets,"sanbo","sanbo_head");v.extra[1]=find(assets,"sanbo","sanbo_body");
    }else if(K::Field::ObjectCannon::matches(v.name)&&live){
        v.present=drawCannon;
        v.clips[0]=clip(v.main,named(v,"StarRing")?"StarRing_shot":v.name);
        v.clips[1]=clip(v.main,"StarRing_wait");
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
    }else if(id==ObjectId::Woodbox&&live){
        const auto& box=*static_cast<const K::Field::ObjectWoodbox*>(v.object);
        v.main=find(assets,"woodbox",box.getKclName());v.low=v.shadow={};
    }else if(id==ObjectId::W_Woodbox&&live){
        v.present=drawWoodboxLine;
        const auto* box=v.parts.size()>1?static_cast<const K::Field::ObjectWoodboxWSub*>(v.parts[0]):nullptr;
        v.main=find(assets,"woodbox",box?box->getKclName():"woodbox");v.low=v.shadow={};
    }else if(id==ObjectId::ItemboxLine&&live){
        v.present=drawItemboxLine;v.main=find(assets,"Block","Block");
        v.extra[1]=find(assets,"Press","Press");v.extra[2]=find(assets,"Press","Press-shadow");v.clips[0]=clip(v.extra[1],"Press");
        v.low=v.shadow={};
    }else if(id==ObjectId::Press&&live){
        v.present=drawPress;v.clips[0]=clip(v.main,"Press");
    }else if(id==ObjectId::DCPillar&&live){
        v.present=drawPillar;v.extra[0]=find(assets,v.resource,"dc_pillar_base");
    }else if(named(v,"CarB")&&live){
        v.present=drawCarB;
        static const char* names[]={"CarB1","CarB2","CarB"};
        for(unsigned i=0;i<3;++i)v.extra[i]=find(assets,v.resource,names[i]);
    }else if(named(v,"BGteresaSFC")&&live){
        v.present=drawBGTeresa;
    }else if(named(v,"b_teresa")&&live){
        v.present=drawTeresa;
        static const char* names[]={"b_teresa","b_teresa2","b_teresa3"};
        for(unsigned i=0;i<3;++i)v.extra[i]=find(assets,v.resource,names[i]);
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
    }else if(named(v,"monte_a")||named(v,"mare_a")||named(v,"mare_b")){
        // These archives name their idle CHR0 "wait", not the model name.
        // Leaving clip=-1 displayed the bind pose even though the bank loaded.
        v.main=assets.tracks(v.main.model,"wait");
        if(v.low.model)v.low=assets.tracks(v.low.model,"wait");
    }else if(named(v,"peachtreeGC")){
        v.main=assets.tracks(v.main.model,"peachtreeGC");
    }else if(named(v,"InsekiA")||named(v,"InsekiB")){
        v.present=drawInseki;
    }else if(named(v,"StarRing")){
        v.main=assets.tracks(v.main.model,"StarRing_wait");
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
