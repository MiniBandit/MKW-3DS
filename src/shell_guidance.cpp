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
    // No finish filter: acquisition (0x807B3BAC -> 0x807B381C) also locks onto
    // racers who have crossed the line.
    int result=-1;float best=16000000.f;unsigned ownerRank=rank(unsigned(a.owner));
    for(unsigned i=0;i<count_;++i){const auto& r=racers_[i];unsigned candidateRank=rank(i);
        if(int(i)==a.owner||missing(r))continue;
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
    if(!blue)a.flightDirection=a.backwards?backwardThrow(player,a.travelSpeed):forwardThrow(player,base,a.travelSpeed);
    a.velocity=a.flightDirection*a.travelSpeed;a.velocity.y=0;s.up=normalized(r.up,{0,1,0});a.target=-1;
    // fn_1_2A4DC4 (0x807B5178): +0x1b0 starts as the direction; a throw from a half-pipe ramp
    // or over a zipper (status 41 / 42) starts the 30-frame slope timer +0x1d8.
    if(!blue){a.shellDirection=a.flightDirection;s.slopeTicks=r.zipper?30:0;}
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
namespace {
// fn_1_276F0C (0x807872C0): normalised when |v|^2 > 0 (0x808A4808), else `keep`.
Vec3 retailUnit(Vec3 v,Vec3 keep){const float q=dot(v,v);return q>0?v*(1/std::sqrt(q)):keep;}
}
// ItemHolderPlayer_update (PAL 0x80797928), every step before the holder's input, unless the
// racer has vanished (status 83). +0x23c follows the stick X by at most 1/15 (0x808A5188).
// +0x40: the slope of the kart's last move (clamped to +-0.85 after 20 air frames, 0x808A518C
// / 5190) blended from the kart's Z slope (0 in an action or a trick, fn_1_2AD3D8 0x807BD78C) by
// the speed ratio, times 0.9 (0x808A5194 / 5198), eased 0.05 (0x808A519C) toward. A kart that
// has not moved leaves the last slope (retail reads an uninitialised value there; at speed
// ratio 0 the blend ignores it).
void RaceItems::updateThrowPitch(unsigned i){
    const auto& r=racers_[i];if(r.vanished)return;
    auto& t=throwPitch_[i];
    constexpr float AimStep=.0666666701f;
    t.aim=r.stickX<=t.aim?std::max(r.stickX,t.aim-AimStep):std::min(r.stickX,t.aim+AimStep);
    const Vec3 d=t.placed?r.position-t.last:Vec3{};t.last=r.position;t.placed=true;
    if(dot(d,d)>0)t.slope=d.y/length(d);
    const float slope=r.airtimeOver20?std::clamp(t.slope,-.85f,.85f):t.slope;
    const float base=r.inAction||r.trickRotating?0.f:r.forward.y;
    t.pitch+=.05f*(.9f*(base+r.speedRatio*(slope-base))-t.pitch);
}
// fn_1_281AA8 (0x80791E5C), each rebuild of the held frame: the anchor's change since the
// last one, the axes, the speed ratio +0x98 and +0xac (Z along the kart's velocity, or in an
// action or a trick).
void RaceItems::noteHeldFrame(unsigned player,Vec3 anchor,Vec3 right,Vec3 up,Vec3 forward){
    auto& m=heldMotion_[player];const auto& r=racers_[player];
    m.delta=anchor-m.anchor;m.anchor=anchor;m.right=right;m.up=up;m.forward=forward;m.ratio=r.speedRatio;
    m.ahead=dot(forward,r.haveVelocity?r.velocity:r.forward*r.speed)>0||r.inAction||r.trickRotating;
}
// A backward throw's speed and direction: fn_1_284C7C (0x80795030) with 30 / 1 / 40
// (0x808A6124 / 611C / 6128) from the held frame.
Vec3 RaceItems::backwardThrow(unsigned player,float& speed)const{
    const auto& m=heldMotion_[player];const auto& r=racers_[player];
    float moved=length(m.delta);Vec3 dir;
    if(!m.ahead){speed=30+moved;dir=-m.forward;}
    else{moved=std::min(moved,r.baseSpeed);speed=30-moved;dir=-(m.forward*40+Vec3{m.delta.x,0,m.delta.z});}
    return finishThrow(player,dir);
}
// A forward throw's: fn_1_284AD4 (0x80794E88) with the shell's base (Green 85 0x808A5DBC,
// Red 75 0x808A5AB0), 1 and 35 (0x808A611C / 6120). On the move: 35 Z, plus the anchor's whole
// change when it points along it, plus X times -20 (0x808A4D38) x speed ratio x +0x23c.
Vec3 RaceItems::forwardThrow(unsigned player,float base,float& speed)const{
    const auto& m=heldMotion_[player];
    const float moved=length(m.delta);Vec3 dir;
    if(!m.ahead){speed=base-moved;dir=m.forward;}
    else{
        speed=base+moved;dir=m.forward*35;
        if(dot(dir,m.delta)>0)dir=dir+m.delta;
        dir=dir+m.right*(20*-m.ratio*throwPitch_[player].aim);
    }
    return finishThrow(player,dir);
}
// fn_1_284968 (0x80794D1C, its parameter 0): a near-flat direction becomes the frame's Y
// (0x808A4D88); on sticky road (status 16) it is projected onto the plane of KartMove +0x38
// (smoothed up) plus 0.2 Z (0x808A4CE8); otherwise fn_1_2CF2DC (0x807DF690) gives it the
// holder's +0x40 as its vertical part and scales the horizontal part to a unit vector.
Vec3 RaceItems::finishThrow(unsigned player,Vec3 dir)const{
    const auto& m=heldMotion_[player];const auto& r=racers_[player];
    if(dir.x*dir.x+dir.z*dir.z<.0001f)dir=m.up;
    if(r.stickyRoad){
        const Vec3 n=retailUnit(r.smoothedUp+m.forward*.2f,r.smoothedUp+m.forward*.2f);
        dir=tangent(dir,n);
    }else{
        const float p=throwPitch_[player].pitch,xz=dir.x*dir.x+dir.z*dir.z;
        if(xz<=0)dir={0,p>0?1.f:-1.f,0};
        else{const float k=(1-p*p)/xz,scale=k>0?std::sqrt(k):0.f;dir={dir.x*scale,p,dir.z*scale};}
    }
    return retailUnit(dir,dir);
}
// fn_1_2A5140 (PAL 0x807B54F4): the Green Shell's movement (0x807AEC64, toward 105) and the
// unguided Red's (states 1 and 2 toward 105, 0x807AAB88 / 0x807AAD78; state 6 toward 140,
// 0x807AB37C). `up` is the object's up +0xc8. The bounce count +0x1c8 is bounceLimit - 1 -
// bounces (broken below 0); +0x1d8 is guidance.slopeTicks.
void RaceItems::moveShell(ItemActor& a,ItemWorldQuery query,Vec3& up,float target){
    constexpr uint32_t Floors=0x20E80FFF,Walls=0xD010F000,HalfPipe=0x80000,Cannon=0x20000,FallBoundary=0x10000,SolidOob=0x400;
    auto& s=a.guidance;
    // fn_1_296860 -> fn_1_296644 (0x807A69F8): destroyed, as the scatter state does it.
    auto destroy=[&]{const bool fell=a.position.y<0.f;a.dying=fell?30:6;a.radius=0;if(!fell)a.scale=0;};
    // A reflection off a horizontal normal (fn_1_2CE504 twice, with SE_ITM_KAME_HANSHA through
    // fn_1_292B64), its rise damped 0.7 (0x808A6144) toward the old fall.
    auto reflect=[&](Vec3 n){
        const float fall=a.flightDirection.y;const Vec3 along=n*dot(a.flightDirection,n);
        a.flightDirection=a.flightDirection-along-along;sound(ItemSoundCue::WallBounce,a);
        float& y=a.flightDirection.y;if(y>0&&y>-fall)y+=.7f*(-fall-y);
        a.flightDirection=retailUnit(a.flightDirection,a.flightDirection);a.shellDirection=a.flightDirection;
    };
    if(a.airFrames<6)++a.airFrames;   // ItemObj_update +0x15c, before the state
    a.position+=a.velocity;
    ItemSurface hit=query?query(a.previous,a.position,a.terrainRadius,ItemKclMask):ItemSurface{};
    if(hit.types&FallBoundary){a.active=false;return;}   // fall boundary, as the port handled it before
    bool collided=hit.types!=0,steep=false,pushed=false,bounced=false;   // bounced: +0x74 bit 0x20
    // A near-vertical floor (normal y < 0.003, 0x808A6138) or a half-pipe ramp starts the slope
    // timer; after 300 frames it also costs a bounce.
    if((hit.types&Floors)&&(hit.floorNormal.y<.00300002f||(hit.types&HalfPipe))){
        if(a.age>300&&s.slopeTicks<1)++a.bounces;
        s.slopeTicks=30;steep=true;
    }
    if(s.slopeTicks>0&&query){
        // While it runs, walls (0xD010F000) push the shell like a floor unless they face down
        // (normal y < -0.98, 0x808A613C), which turns the shell's rise into a fall and costs a bounce.
        const ItemSurface w=query(a.previous,a.position,a.terrainRadius,Walls);
        if(w.types){
            collided=pushed=true;
            const float d=length(w.correction);const Vec3 n=d>0?w.correction*(1/d):Vec3{};
            hit.low={std::min(hit.low.x,w.correction.x),std::min(hit.low.y,w.correction.y),std::min(hit.low.z,w.correction.z)};
            hit.high={std::max(hit.high.x,w.correction.x),std::max(hit.high.y,w.correction.y),std::max(hit.high.z,w.correction.z)};
            if(w.wallNormal.y>=-.98f){if(hit.floorDist<d){hit.floorDist=d;hit.floorNormal=n;}}
            else{
                pushed=false;
                if(a.shellDirection.y>0)a.shellDirection.y=-a.shellDirection.y;
                a.ownerImmunity=0;++a.bounces;bounced=true;
            }
            hit.correction=hit.low+hit.high;
        }
    }
    if(collided){
        a.position+=hit.correction;
        if((hit.types&Floors)||pushed){
            const Vec3 n=hit.floorNormal;
            if(s.slopeTicks<1){
                a.shellDirection=retailUnit(tangent(a.flightDirection,n),tangent(a.flightDirection,n));a.flightDirection=a.shellDirection;
            }else if(a.bounces<a.bounceLimit){
                // During the timer the direction follows the velocity along the floor.
                a.shellDirection=retailUnit(tangent(a.velocity,n),tangent(a.velocity,n));
                if(shellXZ2(a.shellDirection)>=.0001f)a.flightDirection=a.shellDirection;   // 0x808A6134
            }else{reflect({n.x,0,n.z});bounced=true;}
            if(n.y>=-.98f)up=n;
        }
        if(s.slopeTicks<1&&(hit.types&Walls)){
            Vec3 w=hit.wallNormal;
            if(w.y>=.98f){destroy();return;}   // 0x808A6148
            w.y=0;
            if(dot(a.flightDirection,w)<0){
                reflect(w);a.ownerImmunity=0;++a.bounces;bounced=true;
                if(a.solidFallTimer)a.bounces=a.bounceLimit;   // +0x15e set: the bounce breaks it
            }
        }
        if(hit.types&(Floors|Walls))a.velocity=a.shellDirection*a.travelSpeed;
        // On a floor without a wall the rise keeps at least this step's climb, at most 50
        // (0x808A614C) without the timer.
        if(a.age>2&&(hit.types&Floors)&&!(hit.types&Walls)){
            float climb=a.position.y-a.previous.y;
            if(s.slopeTicks<1&&climb>50)climb=50;
            if(a.velocity.y<climb)a.velocity.y=climb;
        }
        if(a.bounces>=a.bounceLimit){shatter(a);return;}
        if(hit.types&Cannon){destroy();return;}
    }
    if(s.slopeTicks<1)a.travelSpeed+=.01f*(target-a.travelSpeed);   // 0x808A6150
    if(collided&&!steep&&!pushed&&s.slopeTicks>0)--s.slopeTicks;
    if(!(hit.types&Floors)||s.slopeTicks>0){
        // Gravity 2 falling, 4.2 rising (0x808A6168 / 6158); airborne for 5 updates, the fall
        // stops at -100 and the rise is damped 0.9 (0.125 above 100).
        float& vy=a.velocity.y;
        if(vy<=0){vy-=2;if(a.airborne&&vy<-100)vy=-100;}
        else{vy-=4.2f;if(a.airborne)vy*=vy>100?.125f:.9f;}
    }else a.velocity=a.velocity-up*2.5f;   // 0x808A6154
    // ItemObj_update after the state: a floor clears +0x15c; from 5 on bit 0x20 is set and the
    // up eases 0.05 (0x808A5248) toward world up while its y is under 0.98 (0x808A5244).
    if(hit.types&Floors)a.airFrames=0;
    // The solid-fall timer +0x15e: a floor contact with solid-OOB KCL (0x400) and no bounce this
    // update starts 60 frames; they run while no other floor (0x20E80BFF) is touched and destroy
    // the shell at 0.
    if((hit.types&Floors)&&(hit.types&SolidOob)&&!bounced&&!a.solidFallTimer)a.solidFallTimer=60;
    if(a.solidFallTimer&&!(hit.types&0x20E80BFF)&&!--a.solidFallTimer){destroy();return;}
    a.airborne=a.airFrames>=5;
    if(a.airborne&&up.y<.98f)up=retailUnit(up*.95f+Vec3{0,.05f,0},up*.95f+Vec3{0,.05f,0});
}
void RaceItems::shellTerrain(ItemActor& a,ItemWorldQuery query,bool blue,bool route,bool approach){
    auto& s=a.guidance;ItemSurface surface=query?query(a.previous,a.position,a.terrainRadius,a.kind==ItemKind::BlueShell?BlueShellKclMask:ItemKclMask):ItemSurface{};
    // 29a558 / 29b494: cannon KCL destroys ordinary shells; object ID 5
    // (Spiny) is explicitly exempt. It follows the authored airborne route.
    if(surface.cannon&&!blue){a.active=false;return;}
    if(surface.kill&&!blue){a.active=false;return;}
    if(blue){a.position.y+=std::max(0.f,surface.correction.y);}else a.position+=surface.correction;
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
            moveShell(a,query,s.up,s.phase==6?140.f:105.f);   // 0x808A5AB4 / 0x808A5AD0
        }
        if(a.active&&!a.dying&&(s.phase==2||s.phase==3)&&s.pending<0&&s.ticks>4){
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
            // fn_1_2A3C28 (0x807B3FDC) / fn_1_2A407C (0x807B4430) copy the target's
            // KartMove +0x74 (vel1Dir, its direction of travel) into +0x284 every
            // update; the orbit's side axis is vel1Dir x KartMove +0x38 (smoothedUp).
            if(s.phase==2||s.phase==3){
                s.approachSpeed=shellMoveToward(s.approachSpeed,130,8);
                if(s.phase==3){s.height-=7.5f;s.orbitStep=uint16_t(s.orbitStep+35);s.orbitRadius-=s.orbitAngle<0x8000?4:8;s.orbitAngle+=s.orbitStep;}
                // A horizontal circle about the point 300 (0x808A60B0) ahead of the leader.
                s.orbitOffset=(cross(r.moveDir,r.smoothedUp)*cosAngle(s.orbitAngle)+r.moveDir*sinAngle(s.orbitAngle))*s.orbitRadius;
                Vec3 d=s.lastTarget+r.moveDir*300+s.orbitOffset-a.position;
                bool cannonOrbit=s.phase==3&&r.inCannon&&shellCourse_!=29;
                Vec3 desired=d*(s.phase==2||cannonOrbit?.5f:.3f);
                if(s.phase==2&&a.velocity.x*desired.x+a.velocity.z*desired.z<0){
                    float sign=a.velocity.z*desired.x-a.velocity.x*desired.z;
                    desired.x=sign>0?a.velocity.z:-a.velocity.z;desired.z=sign>0?-a.velocity.x:a.velocity.x;
                }
                desired=shellLimit(desired,cannonOrbit?600.f:s.approachSpeed);
                float response=s.phase==2?.125f:.5f;if(!s.hasRoute&&a.age<30)response*=float(a.age)/30;
                a.velocity=lerp(a.velocity,desired,response);a.position+=a.velocity;
                if(query){auto surface=query(a.previous,a.position,a.terrainRadius,BlueShellKclMask);a.position.y+=surface.correction.y;}
                if(s.phase==2&&shellXZ2(d)<640000)s.pending=3;
                if(s.phase==3&&s.orbitAngle>=0x11f40)s.pending=4;
            }else{
                s.height+=s.phase==4?3:-70;
                // fn_1_29D854 (0x807ADC08): the dodge response floor by engine class
                // (RaceConfig +0xB6C): .22 at 100cc, .25 at 150cc, .2 otherwise (.15
                // online, bss 61DB, which an offline port never sets).
                const float dodgeFloor=engineClass_==1?.22f:engineClass_==2?.25f:.2f;
                float response=r.inCannon&&shellCourse_!=29?.5f:s.phase==4?s.amplitude:(s.dodge?dodgeFloor:.7f);
                // States 4/5 (0x807ADC5C / 0x807ADFA4): 210 (0x808A60C8) ahead of the
                // leader along its direction of travel, plus the height.
                Vec3 destination=s.lastTarget+r.moveDir*210+Vec3{0,s.height,0};a.velocity=(destination-a.position)*response;a.position+=a.velocity;
                // 0x807ADC5C moves with the old response, updates it, then detects a
                // dodge for the next tick.
                if(s.phase==4){
                    if(s.dodge)s.amplitude=std::max(dodgeFloor,s.amplitude-.05f);
                    else if(s.amplitude<.7f)s.amplitude+=.005f;
                    // The target's ItemHolderPlayer +0x238: set only for the update in
                    // which it activated a Mushroom (use, Golden use or a dropped one).
                    if(s.ticks>8&&(mushroomPulse_[unsigned(a.target)]||(r.inCannon&&shellCourse_==29)))s.dodge=true;
                    if(s.ticks>24)s.pending=5;
                }
                if(s.phase==5){
                    // 807a32fc: the course probe uses the terrain radius of the last stretch.
                    const auto prop=itemProperties(unsigned(a.kind));
                    a.terrainRadius=prop.terrain*s.stretch;
                    bool impact=s.height<(r.inCannon?-900.f:-100.f);
                    if(query){auto surface=query(a.previous,a.position,a.terrainRadius,BlueShellKclMask);a.position+=surface.correction;impact=impact||length(surface.floorNormal)>.1f||length(surface.wallNormal)>.1f;}
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
    // The Blue keeps the thrower's pose from its launch (fn_1_29E338 0x807AE6EC sets its
    // quaternion +0x10 from the kart pose; no Blue state writes it again).
    if(!blue)a.yaw=std::atan2(a.position.x-a.previous.x,a.position.z-a.previous.z);
    if(!std::isfinite(a.position.x)||!std::isfinite(a.position.y)||!std::isfinite(a.position.z)||a.position.y<-100000)a.active=false;
}
}
