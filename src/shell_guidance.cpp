#include "mkw/race_items.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mkw {
namespace {
constexpr float Pi=3.14159265358979323846f;
Vec3 motion(const ItemRacer& r){return r.haveVelocity?r.velocity:r.forward*r.speed;}
Vec3 project(Vec3 v,Vec3 axis){return axis*dot(v,axis);}
float sinAngle(uint32_t angle){return std::sin(float(uint16_t(angle))*(2*Pi/65536.f));}
float cosAngle(uint32_t angle){return std::cos(float(uint16_t(angle))*(2*Pi/65536.f));}
bool missing(const ItemRacer& r){return r.shellTargetUnavailable||(r.unavailable&&!r.inCannon);}
}
void RaceItems::trackShellRacers(){
    for(unsigned i=0;i<count_;++i){const uint16_t bit=uint16_t(1u<<i);
        if(racers_[i].itemPoint>=shellRoute_.size()){itemPointFrom_[i]=racers_[i].position;itemPointPending_|=bit;}else itemPointPending_&=uint16_t(~bit);
    }
}
uint16_t RaceItems::itemPoint(unsigned racer){
    const uint16_t bit=uint16_t(1u<<racer);
    if(itemPointPending_&bit){racers_[racer].itemPoint=shellRoute_.nearest(itemPointFrom_[racer]).point;itemPointPending_&=uint16_t(~bit);}
    return racers_[racer].itemPoint;
}
int RaceItems::blueTarget(const ItemActor& a)const{
    // 295718: ascending race rank, skip finishers until the owner has led;
    // the owner is eligible only in first place. Immunity does not change rank.
    int result=-1;unsigned best=MaxRacers+1;
    const RaceTeam ownerTeam=a.owner>=0&&unsigned(a.owner)<count_?racers_[unsigned(a.owner)].team:RaceTeam::None;
    if(ownerTeam!=RaceTeam::None){
        // RMCU 807A1680: first eligible opponent in race order. If all
        // opponents are excluded, retain the highest eligible own-team racer.
        int own=-1;unsigned ownRank=MaxRacers+1;
        for(unsigned i=0;i<count_;++i){
            if(racers_[i].finished&&!a.guidance.ownerLed)continue;
            const unsigned position=rank(i);
            if(sameRaceTeam(ownerTeam,racers_[i].team)){if(position<ownRank){ownRank=position;own=int(i);}}
            else if(position<best){best=position;result=int(i);}
        }
        return result>=0?result:own;
    }
    for(unsigned i=0;i<count_;++i){unsigned r=rank(i);
        if(racers_[i].finished&&!a.guidance.ownerLed)continue;
        if(int(i)==a.owner&&r!=1)continue;
        if(r<best){best=r;result=int(i);}
    }
    return result;
}
int RaceItems::redTarget(const ItemActor& a,bool ranked)const{
    // 298d88: nearest XZ within 4000, forward cone (sin=.95), signed
    // floor-normal height [-1500,1000], source rank and protection filters.
    int result=-1;float best=16000000.f;unsigned ownerRank=rank(unsigned(a.owner));
    for(unsigned i=0;i<count_;++i){const auto& r=racers_[i];unsigned candidateRank=rank(i);
        // RMCU 807a4eec calls 8079a3d4 before acquiring: ignore same-team racers.
        if(int(i)==a.owner||r.finished||missing(r)||(a.owner>=0&&unsigned(a.owner)<count_&&sameRaceTeam(racers_[unsigned(a.owner)].team,r.team)))continue;
        if(ranked&&ownerRank<=candidateRank){if(ownerRank!=1||candidateRank>3||(candidateRank==1&&count_>1))continue;}
        if(candidateRank!=1&&(r.shellTargetProtected||r.controlLocked||r.inCannon||invincible(i)||hud_[i].mega))continue;
        Vec3 d=r.position-a.position;float xz=shellXZ2(d);if(xz>best)continue;
        float f=dot(a.velocity,d),squared=dot(a.velocity,a.velocity)*dot(d,d);
        if(f<=0||f*f<=(1-.95f*.95f)*squared)continue;
        float vertical=dot(a.guidance.up,d);if(vertical>1000||vertical<-1500)continue;
        best=xz;result=int(i);
    }
    return result;
}
int RaceItems::redRouteTarget(unsigned player)const{
    if(count_<2)return -1;unsigned desired=rank(player)-1;if(!desired)desired=count_;
    for(unsigned i=0;i<count_;++i)if(rank(i)==desired)return int(i);
    return -1;
}
void RaceItems::launchShell(ItemActor& a,unsigned player){
    auto& s=a.guidance;const auto& r=racers_[player];const bool blue=a.kind==ItemKind::BlueShell;
    const Vec3 v=motion(r);float speed=length(v);bool forward=dot(v,r.forward)>0;
    Vec3 direction=normalized(a.backwards?-r.forward:r.forward,{0,0,1});
    const float base=blue?60.f:a.backwards?30.f:75.f;
    a.travelSpeed=a.backwards?(forward?base-speed:base+speed):(forward?base+speed:base-speed);
    a.flightDirection=normalized(direction*(a.backwards?40.f:35.f)+(forward?(a.backwards?-v:v):Vec3{}),direction);
    a.velocity=a.flightDirection*a.travelSpeed;a.velocity.y=0;s.up=normalized(r.up,{0,1,0});a.target=-1;
    a.lifetime=blue?65535:2401;a.bounceLimit=1;
    s.current={itemPoint(player),true};if(!shellRoute_.valid(s.current))s.current=shellRoute_.nearest(r.position);
    s.hasRoute=shellRoute_.valid(s.current)&&shellRoute_.valid(shellRoute_.edge(s.current))&&!a.backwards;
    int target=blue?blueTarget(a):redRouteTarget(player);s.next=shellRoute_.select(s.current,target>=0?&racers_[target].position:nullptr,target>=0?ShellRoute::Cursor{itemPoint(unsigned(target)),true}:ShellRoute::Cursor{},blue);
    if(!shellRoute_.valid(s.next))s.hasRoute=false;
    s.phase=blue?(s.hasRoute?1:2):(a.backwards?1:s.hasRoute?3:2);
    s.pending=int8_t(s.phase); // First state update is tick zero, as in the retail dispatcher.
    s.launchTicks=blue?20:45;s.visualY=a.position.y;s.approachSpeed=260;
    if(blue){a.target=int8_t(target);s.ownerLed=target==a.owner;if(target>=0){s.lastTarget=racers_[target].position;s.leaderMask|=uint16_t(1u<<target);}if(!s.hasRoute){a.travelSpeed=85+(forward?speed:-speed);a.velocity=a.flightDirection*a.travelSpeed;s.approachSpeed=length(a.velocity);}}
}
void RaceItems::shellTerrain(ItemActor& a,ItemWorldQuery query,bool blue,bool route,bool approach){
    auto& s=a.guidance;ItemSurface surface=query?query(a.previous,a.position,a.terrainRadius):ItemSurface{};
    // 29a558 / 29b494: cannon KCL destroys ordinary shells; object ID 5
    // (Spiny) is explicitly exempt. It follows the authored airborne route.
    if(surface.cannon&&!blue){a.active=false;return;}
    if(surface.kill&&!blue){a.active=false;return;}
    if(blue){a.position.y+=std::max(0.f,surface.correction.y);}else a.position+=surface.correction+surface.floorMotion;
    bool floor=length(surface.floorNormal)>.1f;
    if(floor){s.up=normalized(surface.floorNormal);if(s.up.y<.00300002f)s.slopeTicks=30;else if(s.slopeTicks)--s.slopeTicks;
        if(!route){
            if(approach)a.velocity=tangent(a.velocity,s.up);
            else{a.flightDirection=normalized(tangent(a.flightDirection,s.up),a.flightDirection);a.velocity=a.flightDirection*a.travelSpeed;}
        }else a.velocity=tangent(a.velocity,s.up);
    }
    if(length(surface.wallNormal)>.1f&&!blue){
        // Acquired/route reds break against ordinary walls. Unacquired and
        // backwards reds retain the shared shell reflection controller.
        if(route||approach){shatter(a);return;}
        Vec3 n=normalized(surface.wallNormal);float into=dot(a.flightDirection,n);
        if(into<0){
            a.flightDirection=normalized(a.flightDirection-n*(2*into),a.flightDirection);a.velocity=a.flightDirection*a.travelSpeed;
            if(++a.bounces>=a.bounceLimit){shatter(a);return;}
            sound(ItemSoundCue::WallBounce,a);   // fn_1_2A5F04: SE_ITM_KAME_HANSHA
        }
    }
    if(floor&&!s.slopeTicks)a.velocity=a.velocity-s.up*2.5f;
    else{a.velocity.y-=a.velocity.y>0?4.2f:2.f;
        if(blue){if(a.velocity.y>0)a.velocity.y*=a.velocity.y>100?.125f:.9f;else a.velocity.y=std::max(-100.f,a.velocity.y);}
    }
}
void RaceItems::moveShellRoute(ItemActor& a,ItemWorldQuery query,int target,bool blue){
    auto& s=a.guidance;const Vec3* targetPos=target>=0?&racers_[target].position:nullptr;
    ShellRoute::Cursor targetPoint=target>=0?ShellRoute::Cursor{itemPoint(unsigned(target)),true}:ShellRoute::Cursor{};
    if(!shellRoute_.valid(s.next)){s.hasRoute=false;s.pending=blue?2:1;a.position+=a.velocity;shellTerrain(a,query,blue,false);return;}
    if(s.launchTicks>=0){
        // 29a050 initial straight travel; turn into the path over 45/20 ticks.
        a.position+=a.velocity;shellTerrain(a,query,blue,false);if(!a.active)return;
        if(!s.slopeTicks)a.travelSpeed+=.01f*((blue?260.f:130.f)-a.travelSpeed);
        Vec3 next=shellRoute_.point(s.next).position,current=shellRoute_.point(s.current).position;
        if(shellXZ2(next-a.position)<shellXZ2(current-a.position)){s.current=s.next;s.next=shellRoute_.select(s.current,targetPos,targetPoint,blue);}
        if(!shellRoute_.valid(s.next)){s.pending=blue?2:1;s.hasRoute=false;return;}
        Vec3 delta=shellRoute_.point(s.next).position-a.position;
        Vec3 desired=normalized(delta,a.flightDirection);bool behind=dot(a.flightDirection,delta)<0;
        if(behind){Vec3 side=cross(a.flightDirection,s.up);desired=dot(side,delta)>0?side:-side;}
        float blend=(behind?.15f:.08f)*(1-float(s.launchTicks)/(blue?20.f:45.f));
        a.flightDirection=normalized(lerp(a.flightDirection,desired,blend),a.flightDirection);
        a.velocity=a.flightDirection*a.travelSpeed;
        if(s.launchTicks>0){--s.launchTicks;return;}
        if(dot(normalized(delta,a.flightDirection),a.flightDirection)<=.8660254f&&!blue)return;
        delta=delta*.25f;
        Vec3 projection=project(delta,a.flightDirection);
        s.spline.start(a.position-projection,a.position,a.position+delta+projection,shellRoute_.point(s.next).position);
        s.splineReady=true;s.launchTicks=-1;s.blend=1;return;
    }
    a.travelSpeed+=(blue?.008f:.01f)*((blue?260.f:130.f)-a.travelSpeed);
    s.spline.t+=a.travelSpeed*s.spline.inverseLength;
    // Retail loops until residual distance fits. Bound corrupt/zero-length
    // cycles to the graph size so malformed KMP never hangs a source tick.
    unsigned advances=0;
    while(s.spline.t>=1&&advances++<=shellRoute_.size()){
        s.current=s.next;s.next=shellRoute_.select(s.current,targetPos,targetPoint,blue);
        if(!shellRoute_.valid(s.next)){s.hasRoute=false;s.pending=blue?2:1;break;}
        s.spline.shift(shellRoute_.point(s.next).position);
    }
    if(!s.hasRoute||advances>shellRoute_.size()+1){s.hasRoute=false;s.pending=blue?2:1;return;}
    Vec3 p=s.spline.sample(s.spline.t);float oldY=a.position.y;
    a.position.x+=(p.x-a.position.x)*s.blend;a.position.z+=(p.z-a.position.z)*s.blend;
    if(blue){float y=s.spline.control[0].y+(s.spline.control[3].y-s.spline.control[0].y)*s.spline.t;a.position.y=std::max(a.position.y,y);}
    a.position.y+=a.velocity.y;s.blend=shellMoveToward(s.blend,a.travelSpeed/(1000+a.travelSpeed),.01f);
    shellTerrain(a,query,blue,true);a.velocity.x=a.position.x-a.previous.x;a.velocity.z=a.position.z-a.previous.z;
    if(!blue&&a.position.y-oldY>35&&s.slopeTicks==0)a.velocity.y=std::min(a.velocity.y,35.f);
}
void RaceItems::updateShell(ItemActor& a,ItemWorldQuery query){
    auto& s=a.guidance;const bool blue=a.kind==ItemKind::BlueShell;
    // Acquisition already excludes finished racers. Retire an existing red lock
    // as well: finished racers reject contact, so retaining it pins the shell
    // to a target it can never hit. Keep Blue Shell leader selection separate.
    if(!blue&&a.target>=0&&unsigned(a.target)<count_&&racers_[a.target].finished){
        a.target=-1;shatter(a);return;
    }
    if(s.pending>=0){s.phase=uint8_t(s.pending);s.pending=-1;s.ticks=0;
        if(blue){
            if(s.phase==2){s.approachSpeed=length(a.velocity);s.orbitRadius=300;}
            if(s.phase==3){s.height=450;s.amplitude=50;s.orbitStep=1000;s.orbitAngle=0;}
            if(s.phase==4){a.position.y=s.visualY;s.height=300;s.amplitude=.2f;s.dodge=false;}
            if(s.phase==5){s.height=400;s.stretch=a.scale;}
        }
    }else ++s.ticks;
    if(!blue){
        if(s.phase==3){
            moveShellRoute(a,query,redRouteTarget(unsigned(a.owner)),false);
            if(s.ticks>1800){s.pending=1;s.hasRoute=false;}
        }else if((s.phase==4||s.phase==5)&&a.target>=0){
            const auto& r=racers_[a.target];
            if(s.phase==5&&(missing(r)||s.ticks>60)){a.active=false;return;}
            if(missing(r)){a.velocity+=s.homing;s.homing={};s.pending=6;a.target=-1;}
            else{
                // 2984c0 (0x807a44f4): retain kart-follow velocity and a separate capped
                // pursuit velocity. The aim (+0x288) lies behind the target along its
                // back axis (+0x294 eases toward -zAxis: componentZAxis 0x8058a4c0
                // times -1, 0.05 per update, 0.01 while it trick-rotates), up to
                // 2000*speedRatio by 4000 away, so a Red closes in from behind and
                // meets a trailing item before the kart.
                s.targetBack=lerp(s.targetBack,-normalized(r.forward,{0,0,1}),r.trickRotating?.01f:.05f);
                Vec3 aim=r.position+s.targetBack*(std::clamp(length(r.position-a.position)/4000.f,0.f,1.f)*2000*r.speedRatio);
                Vec3 d=aim-a.position;
                if((dot(s.up,d)>1050||dot(s.up,d)<-1550)&&shellXZ2(d)<22500){a.velocity+=s.homing;s.homing={};s.pending=6;a.target=-1;}
                else{
                    float pursuit=s.phase==5?160.f:(r.shellTargetProtected||r.controlLocked||r.inCannon||invincible(unsigned(a.target)))?25.f:50.f;
                    s.homing=lerp(s.homing,normalized(d,{})*pursuit,.2f);
                    s.homing=shellLimit(s.homing,std::max(0.f,75-50*r.speedRatio));
                    Vec3 follow=(motion(r)-a.velocity)*.2f;follow.y=0;
                    // Do not accelerate back through the owner at close range.
                    Vec3 fromOwner=a.position-racers_[unsigned(a.owner)].position;float ownerDistance=length(fromOwner);
                    if(dot(follow,fromOwner)<0)follow=follow-project(follow,normalized(fromOwner,{}))*std::clamp((ownerDistance-2000.f)/(500.f-2000.f),0.f,1.f);
                    a.velocity+=follow;a.position+=a.velocity+s.homing;shellTerrain(a,query,false,false,true);
                    if(s.phase==5){float close=r.radius+1.5f*a.radius;if(shellXZ2(a.position-r.position)<close*close)shatter(a);}
                }
            }
        }else{
            a.position+=a.velocity;shellTerrain(a,query,false,false);if(!s.slopeTicks)a.travelSpeed+=.01f*((s.phase==6?140.f:105.f)-a.travelSpeed);
        }
        if(a.active&&(s.phase==2||s.phase==3)&&s.pending<0&&s.ticks>4){
            int target=redTarget(a,s.phase==3);
            // Acquisition (0x807a4dbc) seeds +0x294 with the target's back axis.
            if(target>=0){a.target=int8_t(target);s.pending=4;s.targetBack=-normalized(racers_[target].forward,{0,0,1});s.homing={};}
        }
    }else{
        if(s.phase==1){
            int leader=blueTarget(a);a.target=int8_t(leader);if(leader==a.owner)s.ownerLed=true;if(leader>=0)s.leaderMask|=uint16_t(1u<<leader);
            moveShellRoute(a,query,leader,true);
            if(leader>=0&&!racers_[leader].shellAcquireUnavailable&&!missing(racers_[leader])){
                Vec3 d=racers_[leader].position-a.position;bool same= s.next.point==itemPoint(unsigned(leader));
                if(same)s.routeMatchedTarget=int8_t(leader);same=same||s.routeMatchedTarget==leader;
                float acquireRange=shellCourse_==0?5000.f:8000.f;
                bool range=shellXZ2(d)<acquireRange*acquireRange&&d.y<=1000&&d.y>=-1500;
                Vec3 path=shellRoute_.valid(s.next)?shellRoute_.point(s.next).position-a.position:a.velocity;
                float dp=dot(path,d);bool cone=dp>0&&dp*dp>(1-.5f*.5f)*dot(path,path)*dot(d,d);
                // Outside 1000, acquisition requires leaving the route's cone.
                if((same||range)&&(dot(d,d)<1000.f*1000.f||!cone))s.pending=2;
            }
            if(s.ticks>1200&&leader>=0)s.pending=2;
            if(leader>=0)s.lastTarget=racers_[leader].position;
        }else if(s.phase>=2&&s.phase<=5){
            if(a.target<0||unsigned(a.target)>=count_){a.active=false;return;}
            const auto& r=racers_[a.target];
            if(missing(r))s.targetLive=false;if(s.targetLive)s.lastTarget=r.position;
            // USA807a55d8..807a5734 reads KartMove::vel1Dir (+0x74),
            // then crosses it with smoothedUp (+0x38). This is an orbit
            // in the road plane, not a vertical circle around the kart.
            const Vec3 forward=r.movementDirection;
            const Vec3 side=cross(forward,r.smoothedUp);
            if(s.phase==2||s.phase==3){
                s.approachSpeed=shellMoveToward(s.approachSpeed,130,8);
                if(s.phase==3){s.height-=7.5f;s.orbitStep=uint16_t(s.orbitStep+35);s.orbitRadius-=s.orbitAngle<0x8000?4:8;s.orbitAngle+=s.orbitStep;}
                s.orbitOffset=(side*cosAngle(s.orbitAngle)+forward*sinAngle(s.orbitAngle))*s.orbitRadius;
                Vec3 d=s.lastTarget+forward*300+s.orbitOffset-a.position;
                bool cannonOrbit=s.phase==3&&r.inCannon&&shellCourse_!=29;
                Vec3 desired=d*(s.phase==2||cannonOrbit?.5f:.3f);
                if(s.phase==2&&a.velocity.x*desired.x+a.velocity.z*desired.z<0){
                    float sign=a.velocity.z*desired.x-a.velocity.x*desired.z;
                    desired.x=sign>0?a.velocity.z:-a.velocity.z;desired.z=sign>0?-a.velocity.x:a.velocity.x;
                }
                desired=shellLimit(desired,cannonOrbit?600.f:s.approachSpeed);
                float response=s.phase==2?.125f:.5f;if(!s.hasRoute&&a.age<30)response*=float(a.age)/30;
                a.velocity=lerp(a.velocity,desired,response);a.position+=a.velocity;
                if(query){auto surface=query(a.previous,a.position,a.terrainRadius);a.position.y+=surface.correction.y;}
                if(s.phase==2&&shellXZ2(d)<640000)s.pending=3;
                if(s.phase==3&&s.orbitAngle>=0x11f40)s.pending=4;
            }else{
                s.height+=s.phase==4?3:-70;
                float response=r.inCannon&&shellCourse_!=29?.5f:s.phase==4?s.amplitude:(s.dodge?.25f:.7f);
                Vec3 destination=s.lastTarget+forward*210+Vec3{0,s.height,0};a.velocity=(destination-a.position)*response;a.position+=a.velocity;
                // 296f80 moves with the old response, updates it, then
                // detects a dodge for the next tick (150cc response floor .25).
                if(s.phase==4){
                    s.amplitude=s.dodge?std::max(.25f,s.amplitude-.05f):std::min(.7f,s.amplitude+.005f);
                    if(s.ticks>8&&(r.mushroomBoost||(r.inCannon&&shellCourse_==29)))s.dodge=true;
                    if(s.ticks>24)s.pending=5;
                }
                if(s.phase==5){
                    // 807a32fc: the course probe uses the terrain radius of the last stretch.
                    const auto prop=itemProperties(unsigned(a.kind));
                    a.terrainRadius=prop.terrain*s.stretch;
                    bool impact=s.height<(r.inCannon?-900.f:-100.f);
                    if(query){auto surface=query(a.previous,a.position,a.terrainRadius);a.position+=surface.correction;impact=impact||length(surface.floorNormal)>.1f||length(surface.wallNormal)>.1f;}
                    if(impact){explode(a);return;}
                    // fn_28F9D0: the model stretches toward 1.5 tall while its sides follow
                    // 2*scale-height; the kart sphere registered at the dive's start
                    // (fn_28EA7C, releasedContact) shrinks with the sides.
                    s.stretch+=.2f*(prop.releasedScale+.5f-s.stretch);
                    a.radius=prop.releasedContact*(2*a.scale-s.stretch);a.terrainRadius=prop.terrain*s.stretch;
                }
            }
        }else{a.position+=a.velocity;shellTerrain(a,query,true,false);}
        if(s.phase<=3){
            if(s.phase<3){
                if(s.hasRoute)s.height=std::min(450.f,s.height+15);
                else{
                    auto rise=[](float distance){return distance>8000?1.f:1-std::exp(-distance/500);};
                    float ownerDistance=std::sqrt(shellXZ2(a.position-racers_[unsigned(a.owner)].position));
                    float targetDistance=std::sqrt(shellXZ2(a.position-s.lastTarget));
                    s.height=std::min(2000*rise(ownerDistance),450+2000*rise(std::max(0.f,targetDistance-800)));
                }
                s.amplitude=std::min(150.f,s.amplitude+1);
            }
            s.bobAngle=uint16_t(s.bobAngle+1500);s.visualY+=.5f*a.velocity.y;
            float error=a.position.y+s.height+s.amplitude*sinAngle(s.bobAngle)-s.visualY;
            float ageResponse=std::clamp(.1f+.6f*(float(a.age)-10)/10,.1f,.7f);
            float response=std::clamp(.1f+(ageResponse-.1f)*(error-150)/100,.1f,ageResponse);
            s.visualY+=error*response;
        }
    }
    a.yaw=std::atan2(a.position.x-a.previous.x,a.position.z-a.previous.z);
    if(!std::isfinite(a.position.x)||!std::isfinite(a.position.y)||!std::isfinite(a.position.z)||a.position.y<-100000)a.active=false;
}
}
