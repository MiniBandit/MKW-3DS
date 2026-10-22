#include "mkw/race_items.hpp"
#include "mkw/bobomb.hpp"
#include "mkw/item_box.hpp"
#include "mkw/race_item_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

// Item objects: spawn, movement, blasts, object/object responses, and each kart's contact
// pass (RMCU ItemHolder_checkPlayerCollision 0x80790ca0 + PlayerSub18_checkItemCollision
// 0x8056d72c).
namespace mkw {
using namespace items;
namespace {
// RMCU section 4 1515c, indexed by retail object ID: 0 ignore, 1 bump, 2 destroy.
constexpr uint8_t ItemPairResponses[15][15]={
    {2,2,2,0,0,2,0,0,0,2,0,0,0,0,0},
    {2,2,2,0,0,2,0,0,0,2,0,0,0,0,0},
    {2,2,1,1,1,2,0,1,0,2,0,0,0,0,0},
    {1,1,1,1,1,0,0,1,0,1,0,0,0,0,0},
    {1,1,1,1,1,0,0,1,0,1,0,0,0,0,0},
    {2,2,2,0,0,2,0,0,0,2,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,1,0,0,1,0,1,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,2,1,1,2,0,1,0,2,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};
bool blueFlight(const ItemActor& a){return a.kind==ItemKind::BlueShell&&!a.explosion&&!a.diving();}
uint8_t pairResponse(ItemKind source,ItemKind target){
    return ItemPairResponses[retailObjectId(source)][retailObjectId(target)];
}
void bumpItem(ItemActor& actor,Vec3 normal,Vec3 otherVelocity){
    normal=normalized(normal,{1,0,0});
    const Vec3 relative=actor.velocity-otherVelocity;
    const float into=dot(relative,normal);
    if(into<0)actor.velocity=actor.velocity-normal*(1.45f*into);
    else if(std::fabs(into)<.001f)actor.velocity+=normal*13.f;
    actor.position+=normal*.25f;
}
/// PlayerSub18 item handlers: the action an item object requests from the racer it touches,
/// -1 for none. Mega ignores Bananas and shells; Star, Mega and Bullet ignore Fake Item Boxes.
int handlerAction(const ItemActor& a,const ItemRacer& r){
    switch(a.kind){
    case ItemKind::GreenShell:case ItemKind::RedShell:return r.mega?-1:KartAction::Launch;   // 8056e340
    case ItemKind::Banana:return r.mega?-1:KartAction::Spin;                                 // 8056e328
    case ItemKind::FakeBox:return r.mega||r.star||r.bullet?-1:KartAction::Launch;            // 8056e40c
    case ItemKind::BlueShell:case ItemKind::Bobomb:                                           // 8056e360 / 8056e378
        return a.explosion?bombBlastAction(a.age):KartAction::Explosion;
    default:return -1;
    }
}
}

void RaceItems::courseMushroom(Vec3 position,Vec3 velocity,unsigned owner){
    // fn_1_289C08 (PAL 0x80799FBC, item kind 3): no Mushroom for owner 12 (an item no racer owns).
    if(owner>=count_)return;
    auto& a=spawn(owner,ItemKind::Mushroom,false,&position);
    a.owner=int8_t(owner);
    // ItemHolder_spawnDirect (0x8079A12C) sets scale 0.7 and runs fn_1_296BC0 (0x807A6F74):
    // the scatter state (fn_1_296F9C) and a pop up of 25 with property +0x58, else 35 (Goombas
    // drop Mushrooms only outside Time Trials, so 25), then adds the launch velocity.
    // Its hitter is spared 7 frames.
    a.position=a.previous=position;a.velocity=velocity+Vec3{0,groundItem(a.kind,timeTrial_)?25.f:35.f,0};a.scattered=true;a.guidance={};a.target=-1;
    a.scale=.7f;a.radius=itemProperties(unsigned(a.kind)).releasedContact*a.scale;a.terrainRadius=itemProperties(unsigned(a.kind)).terrain*a.scale;
    a.lifetime=1800;a.ownerImmunity=7;
}
ItemActor& RaceItems::spawn(unsigned player,ItemKind kind,bool backwards,const Vec3* origin){
    // Retail allocates from per-type pools, so a slot freed this step is never reused at once;
    // prefer slots that were already free when the step began.
    auto it=actors_.end();
    for(unsigned i=0;i<MaxActors&&it==actors_.end();++i)if(!actors_[i].active&&freeAtStepStart_[i])it=actors_.begin()+i;
    if(it==actors_.end())it=std::find_if(actors_.begin(),actors_.end(),[](const ItemActor& a){return !a.active;});
    if(it==actors_.end())it=std::max_element(actors_.begin(),actors_.end(),[](const ItemActor& a,const ItemActor& b){return a.age<b.age;});
    auto& a=*it;a={};a.kind=kind;a.owner=int8_t(player);a.active=true;a.lifetime=1800;
    // The Blue Shell's launch (fn_1_29E338 0x807AE6EC) never reads the backward input.
    if(kind==ItemKind::BlueShell)backwards=false;
    Vec3 direction=normalized(racers_[player].forward,{0,0,1});
    const bool trap=kind==ItemKind::Banana||kind==ItemKind::FakeBox;
    if(backwards||trap)direction=-direction;
    a.position=racers_[player].position+direction*(racers_[player].radius+90.f)+Vec3{0,35,0};
    a.previous=a.position;a.yaw=std::atan2(direction.x,direction.z);a.backwards=backwards;
    a.up=normalized(racers_[player].up,{0,1,0});
    const auto prop=itemProperties(unsigned(kind));a.scale=.8f;
    a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
    if(shell(kind)){a.velocity=direction*(kind==ItemKind::BlueShell?150.f:115.f);a.lifetime=480;}
    // Green update 297f88 expires only after age 1500. Its launch starts a
    // bounce counter at 9 forward / 4 backward and destroys it below zero.
    if(kind==ItemKind::GreenShell){
        a.lifetime=1501;a.bounceLimit=backwards?5:10;
        // fn_1_29E7E4 (0x807AEB98) -> fn_1_2A4DC4: the speed and direction from the held frame.
        a.flightDirection=backwards?backwardThrow(player,a.travelSpeed):forwardThrow(player,85.f,a.travelSpeed);
        a.velocity=a.flightDirection*a.travelSpeed;
        // fn_1_2A4DC4 (0x807B5178): +0x1b0 starts as the direction; a throw from a half-pipe ramp
        // or over a zipper (status 41 / 42) starts the 30-frame slope timer +0x1d8.
        a.shellDirection=a.flightDirection;a.guidance.slopeTicks=racers_[player].zipper?30:0;
    }
    if(trapItem(kind)){
        const auto& r=racers_[player];const Vec3 motion=itemMotion(r);
        const float speed=length(motion);const bool movingForward=dot(motion,r.forward)>0;
        const float base=kind==ItemKind::Banana?85:kind==ItemKind::FakeBox?70:100;
        const float lift=kind==ItemKind::Banana?40:kind==ItemKind::FakeBox?45:50;
        // 29c670 shared ballistic launch: signed inheritance, original .125
        // speed contribution and asymmetric up/down vertical-motion gain.
        float travel=backwards?(movingForward?std::max(14.f,.15f*dot(motion,r.forward))-speed:14.f+speed):
            (movingForward?base+1.7f*speed:base-speed);
        travel+=3*(1-r.speedRatio);
        if(!backwards&&r.hop>0)travel+=1.3f*r.hop;
        Vec3 aim=normalized((backwards?-r.forward:r.forward)*70+(movingForward?(backwards?-motion:motion):Vec3{}),direction);
        if(r.speed<30){ // Retail low-speed horizontal scatter: [-.03,.03).
            aim.x+=float(random()>>8)*(0.06f/16777216.f)-.03f;
            aim.z+=float(random()>>8)*(0.06f/16777216.f)-.03f;
        }
        const float dropLift=kind==ItemKind::Bobomb?std::clamp(15.f+10*r.speed,15.f,25.f):10.f;
        const float vertical=backwards?dropLift+motion.y*(motion.y>0?.4f:1.2f):
            lift+.125f*r.speed+std::clamp(motion.y*(motion.y>0?2.f:.5f),-20.f,20.f);
        a.velocity=aim*travel+normalized(r.up,{0,1,0})*vertical;
        a.floorBounces=kind==ItemKind::Banana?0:kind==ItemKind::FakeBox?(backwards?1:3):(backwards?2:3);
        a.bounceImpulse=kind==ItemKind::Bobomb?std::min(.8f,.3f+.007f*r.speed):0;
        if(kind==ItemKind::Bobomb){a.fuse=backwards?BombDropFuse:BombFuse;a.lifetime=65535;}
        // RMCU 0x8079e934: a trap throw spares its thrower for 15 frames, not 30.
        a.ownerImmunity=15;
    }
    if(origin)a.position=*origin;
    if(kind==ItemKind::RedShell||kind==ItemKind::BlueShell)launchShell(a,player);
    a.previous=a.position;
    // ObjBanana/Bomb/FIB::SetInitialPosition (8079ac34): a forward throw whooshes.
    if(trapItem(kind)&&!backwards)sound(ItemSoundCue::Throw,a);
    return a;
}
void RaceItems::explode(ItemActor& actor){
    // OnKill (Blue 0x807a376c / Bob-omb 0x8079cf60): the centre drops by the object's terrain
    // radius (+0xb4) along its up axis; blast state, collision registered at the current
    // scale, owner immunity cleared so the blast also reaches its thrower.
    actor.position=actor.position-normalized(shell(actor.kind)?actor.guidance.up:actor.up,{0,1,0})*actor.terrainRadius;
    actor.previous=actor.position;
    actor.explosion=true;actor.age=0;actor.velocity={};actor.hitMask=0;actor.ownerImmunity=0;
    actor.blastInitialScale=actor.kind==ItemKind::Bobomb?actor.scale:1;
    actor.radius=bombBlastRadius(0,actor.blastInitialScale);
    actor.lifetime=BombDespawnAge;
    actor.scale=1.2f; // bombCore model scale, independent of blast volume.
    if(actor.kind==ItemKind::BlueShell){actor.guidance.phase=6;actor.guidance.pending=-1;actor.guidance.ticks=0;}
    sound(ItemSoundCue::Explode,actor);
    events_.push_back({ItemEffect::Explosion,uint8_t(actor.owner<0?12:actor.owner),uint16_t(actor.kind),actor.position,-1,actor.scale});
}
void RaceItems::shatter(ItemActor& a){
    // A shell's OnKill against a wall or after its last bounce (Red 0x807a0ae0 rk_kouraBrkR,
    // Green rk_kouraBrkG): the object ends with its break effect.
    a.active=false;
    sound(ItemSoundCue::Break,a);
    events_.push_back({ItemEffect::Break,uint8_t(a.owner<0?12:a.owner),uint16_t(a.kind),a.position,-1,a.scale});
}
void RaceItems::updateScattered(ItemActor& a,ItemWorldQuery query){
    // Shared source scatter state 292310: advance, XZ damping, gravity.
    // Complex moving-floor/sloped-wall callbacks remain in the source audit.
    a.position+=a.velocity;a.velocity.x*=.995f;a.velocity.z*=.995f;
    const auto surface=query?query(a.previous,a.position,a.terrainRadius,ItemKclMask):ItemSurface{};
    if(surface.kill||surface.cannon){a.active=false;return;}
    a.position+=surface.correction;
    // PAL fn_1_296F9C 0x807A7350: after the push, an object without property +0x58 (every
    // object in a Time Trial) is destroyed on its first KCL contact of types 0xF0F8FFFF,
    // road and walls (fn_1_296860 0x807A6C14 -> fn_1_296644 0x807A69F8). Below y 0 it dies
    // over 30 frames, otherwise over 6 with its model hidden (fn_1_28D56C 0x8079D920).
    const bool ground=groundItem(a.kind,timeTrial_);
    if(!ground&&(surface.types&0xF0F8FFFFu)){
        const bool fell=a.position.y<0.f;
        a.dying=fell?30:6;a.radius=0;if(!fell)a.scale=0;
        return;
    }
    const bool bounced=a.objectBounce;a.objectBounce=false;
    if(length(surface.floorNormal)>.1f){
        const auto n=normalized(surface.floorNormal);a.up=n;
        // With a floor tracker (ItemObj_init 0x8079E2D8: property +0x58, or a Banana, Fake Item
        // Box or Bob-omb), floor under the object lands it (fn_1_291F98 0x807A234C) unless an
        // object bounced it since the last step, and fn_1_2A655C (0x807B6910) stops it dead.
        const bool tracked=trapItem(a.kind)||ground;
        // fn_1_2A655C, called here at 0x807A778C, plays the object's land sound (ObjProperties +0x68)
        // as it comes to rest; its rest state (0x807B6BFC) plays nothing.
        if(tracked&&!bounced&&!a.rested){a.rested=true;sound(ItemSoundCue::Land,a);}
        if(tracked&&!bounced){a.velocity={};a.settled=true;}
        else{a.velocity=tangent(a.velocity,n);a.settled=!tracked;}
    }else a.velocity.y=std::max(-100.f,a.velocity.y-2.2f);
    if(length(surface.wallNormal)>.1f){const auto n=normalized(surface.wallNormal);float into=dot(a.velocity,n);if(into<0)a.velocity=a.velocity-n*(2*into);}
}
void RaceItems::updateActors(ItemWorldQuery query){
    // RMCU ItemHolderItem_update: one object type at a time (retail object IDs), each object
    // moving and then resolving its own contacts with other objects (ItemObj_update ->
    // CheckOtherObjsCollision 0x80798c88), including tethered ones.
    std::array<uint8_t,MaxActors> order{},types{};unsigned n=0;
    for(unsigned type=0;type<15;++type)for(unsigned i=0;i<MaxActors;++i)
        if(actors_[i].active&&retailObjectId(actors_[i].kind)==type){types[n]=uint8_t(type);order[n++]=uint8_t(i);}
    unsigned k=0;
    for(unsigned type=0;type<15;++type){
        for(;k<n&&types[k]==type;++k){
            auto& a=actors_[order[k]];if(!a.active)continue;
            updateActor(a,query);if(!a.active||a.dying)continue;
            for(unsigned owner=0;owner<count_&&a.active;++owner)
                for(unsigned slot=0;slot<3&&a.active;++slot)resolveActorHeld(a,owner,slot);
            for(unsigned other=0;other<MaxActors&&a.active;++other)
                if(other!=order[k]&&actors_[other].active&&!actors_[other].dying)resolveActorPair(a,actors_[other]);
            // Then the course objects (fn_1_291B24 after fn_1_2918E0 in ItemObj_update).
            objectContacts(a);
        }
        // Tethered objects (ItemObj +0x78 bit 0) are objects of their type too.
        heldObjectContacts(type);
    }
}
void RaceItems::objectContacts(ItemActor& a){
    // ItemObj_update (PAL 0x8079EFEC): with its collision registered, an object that has not
    // come to rest (+0x78 bit 3, fn_1_2A655C) checks the course objects, and a Bob-omb
    // (type 9) even at rest. A Blue registers collision only for its dive (0x807a326c); its
    // blast is a separate retail object type the item tables here do not cover.
    if(!a.active||a.radius<=0||blueFlight(a))return;
    if(a.explosion&&a.kind!=ItemKind::Bobomb)return;
    if(a.settled&&a.kind!=ItemKind::Bobomb)return;
    const unsigned type=retailObjectId(a.kind);
    if(type>9)return;
    std::array<ItemObjectHit,8> hits;
    const unsigned n=karts_->objectContacts(type,a.owner<0?12u:unsigned(a.owner),a.position,a.velocity,a.radius,false,hits.data(),unsigned(hits.size()));
    // fn_1_291B24 (0x807A1ED8): every touched object adds its push-out to the bounds that
    // move the item at the end; response 1 bounces it off (fn_1_291548 0x807A18FC) and
    // clears the young-owner flag 0x20000, response 2 destroys it with its break effect.
    // A blast (Bob-omb OnKill) stays: the objects react, its own state is the blast's.
    Vec3 low{},high{};
    for(unsigned i=0;i<n&&a.active;++i){
        const Vec3 depth=hits[i].depth;
        // Only a contact with a response pushes (fn_1_291B24: reaction != 0); an object the item
        // passes through (response 0) leaves its path alone.
        if(hits[i].response){
            low={std::min(low.x,depth.x),std::min(low.y,depth.y),std::min(low.z,depth.z)};
            high={std::max(high.x,depth.x),std::max(high.y,depth.y),std::max(high.z,depth.z)};
        }
        if(a.explosion)continue;
        if(hits[i].response==1){
            bumpItem(a,normalized(Vec3{depth.x,0,depth.z},normalized(-a.velocity,{1,0,0})),{});
            a.ownerImmunity=0;a.objectBounce=true;
        }else if(hits[i].response==2){
            if(blastItem(a.kind))explode(a);
            else{a.active=false;sound(ItemSoundCue::Break,a);events_.push_back({ItemEffect::Break,uint8_t(a.owner<0?12:a.owner),uint16_t(a.kind),a.position,-1,a.scale});}
            return;
        }
    }
    if(a.active&&!a.explosion)a.position+=low+high;
}
void RaceItems::heldObjectContacts(unsigned type){
    // A tethered item checks the course objects like a free one; only response 2 counts for
    // it (the search skips bounces while held): fn_1_291548 detaches it from its holder
    // (fn_1_284DF8) and kills it with its break effect; a Bob-omb detonates where it trails.
    for(unsigned owner=0;owner<count_;++owner){
        const auto& h=hud_[owner];
        if(!h.deployed||retailObjectId(h.heldKind)!=type||racers_[owner].unavailable)continue;
        auto& state=deployments_[owner];
        for(unsigned slot=0;slot<3;++slot){
            if(!(h.heldMask&(1u<<slot))||state.scales[slot]<HeldContactMinimumScale)continue;
            std::array<ItemObjectHit,8> hits;
            const unsigned n=karts_->objectContacts(type,owner,state.positions[slot],state.positions[slot]-state.previous[slot],
                                                    heldItemContactRadius(owner,slot),true,hits.data(),unsigned(hits.size()));
            for(unsigned i=0;i<n;++i)if(hits[i].response==2){destroyHeld(owner,slot);break;}
            if(!h.deployed)break;
        }
    }
}
void RaceItems::updateActor(ItemActor& a,ItemWorldQuery query){
    {
        a.previous=a.position;++a.age;
        if(a.dying){
            // Dying state fn_1_29352C (0x807A38E0): an object that died below y 0 keeps its
            // velocity and its scale falls by (t-1)/t (min 0.001, 0x808A5668) each frame;
            // ItemHolderItem_despawn once the count reaches 0.
            if(a.scale>0){a.position+=a.velocity;a.scale=std::max(.001f,a.scale*float(a.dying-1)/float(a.dying));}
            if(!--a.dying)a.active=false;
            return;
        }
        if(a.explosion){
            // Blast update: grow, then end collision and despawn on the retail ticks.
            a.radius=bombBlastRadius(a.age,a.blastInitialScale);
            if(a.age>=a.lifetime)a.active=false;
            return;
        }
        const auto prop=itemProperties(unsigned(a.kind));
        a.scale=std::min(prop.releasedScale,a.scale+(a.backwards?prop.dropGrowth:prop.growth));
        a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
        if(a.age>=a.lifetime){
            // Green's update kills an old shell through KillFromOtherCollision (807a4014).
            if(a.kind==ItemKind::GreenShell)sound(ItemSoundCue::Break,a);
            a.active=false;return;
        }
        if(a.kind==ItemKind::Bobomb){
            // 28fd70 checks before decrement; proximity only shortens a fuse.
            if(a.fuse<0){explode(a);return;}
            --a.fuse;
            if(a.floorBounces<3&&(!a.backwards||a.floorBounces<2)){
                float nearest=std::numeric_limits<float>::infinity();
                for(unsigned i=0;i<count_;++i)if(int(i)!=a.owner&&!racers_[i].unavailable&&!racers_[i].finished&&!invincible(i)){
                    const Vec3 d=a.position-racers_[i].position;nearest=std::min(nearest,dot(d,d));
                }
                a.fuse=std::min<int>(a.fuse,bombProximityFuse(nearest));
            }
        }
        if(a.scattered){
            updateScattered(a,query);
        }else if(a.kind==ItemKind::RedShell||a.kind==ItemKind::BlueShell){
            updateShell(a,query);
        }else if(a.kind==ItemKind::GreenShell){
            // fn_1_29E8B0 (0x807AEC64): after 900 frames the next bounce breaks it (+0x1c8 = 0),
            // then the shared shell movement toward 105 (0x808A5DC0).
            if(a.age>900)a.bounces=uint8_t(a.bounceLimit-1);
            moveShell(a,query,a.up,105.f);
            if(!a.active||a.dying)return;
            a.yaw=std::atan2(a.velocity.x,a.velocity.z);
            if(a.position.y<-100000){a.active=false;return;}
        }else{
            if(trapItem(a.kind)&&!a.settled){
                const float drag=a.kind==ItemKind::Banana?(a.backwards?.97f:.999f):a.kind==ItemKind::FakeBox?(a.backwards?.97f:.996f):(a.backwards?.973f:.999f);
                a.velocity=a.velocity*drag;
            }
            a.position+=a.velocity;a.yaw=std::atan2(a.velocity.x,a.velocity.z);
            if(query){
                const auto surface=query(a.previous,a.position,a.terrainRadius,ItemKclMask);a.position+=surface.correction;
                if(surface.kill||surface.cannon){a.active=false;return;}
                if(length(surface.floorNormal)>.1f){
                    const Vec3 normal=normalized(surface.floorNormal);const float into=dot(a.velocity,normal);
                    a.up=normal;
                    if(trapItem(a.kind)){
                        // fn_1_294794 (0x807A4BB8): a Bob-omb thuds on a bounce (+0x74 bit 0x10 from
                        // fn_1_2A71BC 0x807B7570); its landing plays the land sound below, never both.
                        if(a.kind==ItemKind::Bobomb&&a.floorBounces)sound(ItemSoundCue::Bounce,a);
                        if(a.floorBounces){
                            --a.floorBounces;const Vec3 component=normal*into;
                            const float restitution=a.kind==ItemKind::Banana?0:a.kind==ItemKind::Bobomb?(a.backwards?a.bounceImpulse:.45f):a.backwards?0:.45f;
                            a.velocity=(a.velocity-component)*.55f-component*restitution;
                            if(a.kind==ItemKind::Bobomb&&a.backwards)a.bounceImpulse*=.8f;
                        }else{
                            // ObjThrowable::OnFinishThrow (807a8078) = PAL fn_1_2A655C 0x807B6910, from the
                            // Banana (0x807A3F0C), Fake Item Box (0x807A7A44) and Bob-omb (0x807A4BF4) states:
                            // the land sound once; the rest state it enters (0x807B6BFC) plays nothing.
                            if(!a.rested){a.rested=true;sound(ItemSoundCue::Land,a);}
                            a.velocity={};a.settled=true;
                        }
                    }else if(into<0)a.velocity=a.velocity-normal*into;
                }else if(trapItem(a.kind)){
                    a.settled=false;
                    const float gravity=a.kind==ItemKind::Banana?(a.backwards?1.8f:2.2f):a.kind==ItemKind::FakeBox?(a.backwards?1.8f:2.1f):(a.backwards?2.8f:2.3f);
                    const float terminal=std::clamp(100.f+150.f*(float(a.age)-150.f)/(30.f-150.f),100.f,250.f);
                    a.velocity.y=std::max(-terminal,a.velocity.y-gravity);
                }
                if(length(surface.wallNormal)>.1f){const Vec3 normal=normalized(surface.wallNormal);const float into=dot(a.velocity,normal);
                    if(a.kind==ItemKind::RedShell){shatter(a);return;}
                    if(into<0){
                        a.velocity=a.velocity-normal*(2*into);a.flightDirection=normalized(a.flightDirection-normal*(2*dot(a.flightDirection,normal)),a.flightDirection);
                        if(++a.bounces>=a.bounceLimit){shatter(a);return;}
                        if(shell(a.kind))sound(ItemSoundCue::WallBounce,a);   // fn_1_2A5140: SE_ITM_KAME_HANSHA
                    }
                }
            }else if(trapItem(a.kind)){
                const float gravity=a.kind==ItemKind::Banana?(a.backwards?1.8f:2.2f):a.kind==ItemKind::FakeBox?(a.backwards?1.8f:2.1f):(a.backwards?2.8f:2.3f);
                const float terminal=std::clamp(100.f+150.f*(float(a.age)-150.f)/(30.f-150.f),100.f,250.f);
                a.velocity.y=std::max(-terminal,a.velocity.y-gravity);
            }
            if(a.position.y<-100000){a.active=false;return;}
        }
    }
}
void RaceItems::resolveActorPair(ItemActor& a,ItemActor& b){
    if(!a.active||!b.active||(a.explosion&&a.radius<=0)||(b.explosion&&b.radius<=0))return;
    // A Blue registers collision only for its dive (0x807a326c) and blast; Red's callback
    // state has dedicated handling. The recovered dispatcher applies this matrix otherwise.
    if(blueFlight(a)||blueFlight(b))return;
    if((a.kind==ItemKind::RedShell&&a.guidance.phase==5&&!a.scattered)||
       (b.kind==ItemKind::RedShell&&b.guidance.phase==5&&!b.scattered))return;
    // Entity overlap at the moment the updating object checks (static, like the kart test).
    {const Vec3 d=a.position-b.position;const float reach=a.radius+b.radius;if(dot(d,d)>=reach*reach)return;}
    // Retail obtains both directional results before either callback mutates
    // state (28be74-28bfbc). This preserves asymmetric shell/Mushroom and
    // Fake Box/trap interactions.
    const uint8_t responseA=pairResponse(a.kind,b.kind);
    const uint8_t responseB=pairResponse(b.kind,a.kind);
    if(responseA==0&&responseB==0)return;
    const Vec3 delta=a.position-b.position;
    const Vec3 normal=normalized(delta,normalized(a.velocity-b.velocity,{1,0,0}));
    const Vec3 oldAVelocity=a.velocity,oldBVelocity=b.velocity;
    auto apply=[&](ItemActor& victim,uint8_t response,Vec3 away,Vec3 otherVelocity){
        if(response==1)bumpItem(victim,away,otherVelocity);
        else if(response==2){
            if(victim.explosion)return; // The source has separate blast phase overrides.
            if(blastItem(victim.kind))explode(victim);
            else{victim.active=false;sound(ItemSoundCue::Break,victim);events_.push_back({ItemEffect::Break,uint8_t(victim.owner<0?12:victim.owner),uint16_t(victim.kind),victim.position,-1,victim.scale});}
        }
    };
    apply(a,responseA,normal,oldBVelocity);
    apply(b,responseB,-normal,oldAVelocity);
}
void RaceItems::resolveActorHeld(ItemActor& actor,unsigned owner,unsigned slot){
    if(!actor.active||owner>=count_||slot>=3)return;
    auto& h=hud_[owner];auto& state=deployments_[owner];
    if(!(h.heldMask&(1u<<slot))||state.scales[slot]<HeldContactMinimumScale)return;
    if(actor.owner==int(owner)||racers_[owner].unavailable)return;
    if(blueFlight(actor)||(actor.kind==ItemKind::RedShell&&actor.guidance.phase==5&&!actor.scattered))return;
    {const Vec3 d=actor.position-state.positions[slot];const float reach=actor.radius+heldItemContactRadius(owner,slot);if(dot(d,d)>=reach*reach)return;}
    const ItemKind heldKind=h.heldKind;
    const uint8_t actorResponse=pairResponse(actor.kind,heldKind);
    const uint8_t heldResponse=pairResponse(heldKind,actor.kind);
    const Vec3 normal=normalized(actor.position-state.positions[slot],
                                 normalized(actor.velocity-racers_[owner].velocity,{1,0,0}));
    if(actorResponse==1)bumpItem(actor,normal,{});
    else if(actorResponse==2&&!actor.explosion){
        if(blastItem(actor.kind))explode(actor);
        else{actor.active=false;sound(ItemSoundCue::Break,actor);events_.push_back({ItemEffect::Break,uint8_t(actor.owner<0?12:actor.owner),uint16_t(actor.kind),actor.position,-1,actor.scale});}
    }
    if(heldResponse==2&&(h.heldMask&(1u<<slot)))destroyHeld(owner,slot);
    else if(heldResponse==1)state.positions[slot]=state.positions[slot]-normal*.25f;
}

// ---- Kart pass -------------------------------------------------------------------------

bool RaceItems::itemTouchesKart(const ItemActor& a,unsigned player)const{
    // RMCU ItemObj_checkPlayerCollision 0x807984c8: the thrower is skipped while the item is
    // young; a coarse kart-radius + item-radius sphere, then the kart centre inside the item
    // or the item sphere against the kart's collision hull.
    const auto& r=racers_[player];
    if(a.dying)return false; // destroyed: fn_1_296644 removed its BoxColManager entry (0x8079EECC)
    if(int(player)==a.owner&&a.age<=a.ownerImmunity)return false;
    const Vec3 d=a.position-r.position;const float distance2=dot(d,d);
    const float reach=r.radius+a.radius;
    if(distance2>=reach*reach)return false;
    if(distance2<a.radius*a.radius)return true;
    return karts_->touches(player,a.position,a.radius);
}
// The racer's own contact sounds: Kart::Collision::CheckItemCollision (8056d8e8) for a Star,
// Mega or Bullet racer meeting a shell, Banana or Fake Item Box, then the Fake Item Box
// handler (HandleFibCollision 8056e40c).
void RaceItems::contactSounds(ItemKind kind,unsigned player){
    const auto& r=racers_[player];const bool power=r.star||r.mega||r.bullet;
    if(power&&(kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::Banana||kind==ItemKind::FakeBox))sound(ItemSoundCue::Barrier,player);
    if(kind==ItemKind::FakeBox)sound(ItemSoundCue::FakeBox,player,kind,r.position,power);
}
void RaceItems::actorKartContact(ItemActor& a,unsigned player){
    const auto& r=racers_[player];
    const Vec3 away=normalized(r.position-a.position,normalized(a.velocity,r.forward));
    // Pickups: a dropped Mushroom or Star activates for whoever touches it (8056e390/8056e3d4).
    if(a.kind==ItemKind::Mushroom||a.kind==ItemKind::Star){
        if(a.scattered&&a.scale<HeldContactMinimumScale)return;
        if(a.kind==ItemKind::Mushroom){
            // PlayerSub18_handleMushroomCollision (PAL 0x805731E0) -> activateMushroom: +0x238 too.
            karts_->activateMushroom(player);mushroomPulse_[player]=true;events_.push_back({ItemEffect::Mushroom,uint8_t(player)});
            hud_[player].ink=hud_[player].inkDelay=0;
        }else if(karts_->activateStar(player)){
            hud_[player].ink=hud_[player].inkDelay=0;inkReadyFrame_[player]=0;events_.push_back({ItemEffect::Cure,uint8_t(player)});
        }
        a.active=false;return;
    }
    if(a.scattered&&a.scale<HeldContactMinimumScale)return;
    if(blueFlight(a))return; // Kart collision registers at the dive (0x807a326c).
    contactSounds(a.kind,player);
    const int action=handlerAction(a,r);
    const unsigned owner=a.owner<0?12u:unsigned(a.owner);
    // The object's own fate does not depend on whether the racer takes the hit
    // (Obj::OnPlayerCollision 0x8079a784): shells, traps and boxes break, a Bob-omb or a
    // diving Blue explodes.
    if(a.kind==ItemKind::RedShell&&!a.scattered&&a.guidance.phase!=5){
        // RMCU 0x807a09ac: a Red at least kart radius + 1.5 shell radius away in XZ keeps
        // pursuing in special state 5 instead of breaking; the contact still requests the hit.
        const float close=r.radius+1.5f*a.radius;
        if(shellXZ2(a.position-r.position)>=close*close){
            a.target=int8_t(player);a.guidance.pending=5;a.guidance.hasRoute=false;
            a.guidance.targetBack=-normalized(r.forward,{0,0,1});a.guidance.homing={};
        }
    }
    if(!a.explosion){
        if(blastItem(a.kind))explode(a);
        else if(a.guidance.pending!=5){a.active=false;sound(ItemSoundCue::Hit,a);events_.push_back({ItemEffect::Break,uint8_t(owner),uint16_t(a.kind),a.position,-1,a.scale});}
    }
    // PlayerSub18_checkItemCollision (0x805727B4-0x80572AD0): once the action starts, the
    // owner's driver cheers unless it hit itself or the object was dropped on a hit (+0x78
    // bit 0x8000, the scatter state).
    if(action>=0&&strike(player,action,owner,away)&&hitVoiceItem(a.kind)&&!a.scattered&&owner<count_&&owner!=player)
        sound(ItemSoundCue::HitSuccess,owner);
}
void RaceItems::heldKartContacts(unsigned player){
    // Tethered items of other racers: the same handlers, never against their holder or a
    // racer already in an action (ItemObj flag 0x78&1). The struck object leaves its holder.
    const auto& r=racers_[player];
    if(r.inAction||r.damageAction>=0)return;
    for(unsigned owner=0;owner<count_;++owner){
        if(owner==player)continue;
        auto& h=hud_[owner];auto& state=deployments_[owner];
        for(unsigned slot=0;slot<3;++slot){
            if(!(h.heldMask&(1u<<slot))||state.scales[slot]<HeldContactMinimumScale)continue;
            const float radius=heldItemContactRadius(owner,slot);
            const Vec3 d=state.positions[slot]-r.position;const float reach=r.radius+radius;
            if(dot(d,d)>=reach*reach)continue;
            if(dot(d,d)>=radius*radius&&!karts_->touches(player,state.positions[slot],radius))continue;
            const ItemKind kind=baseKind(h.heldKind);
            ItemActor probe{};probe.kind=kind;
            const int action=kind==ItemKind::Bobomb?-1:handlerAction(probe,r);
            const Vec3 away=normalized(r.position-state.positions[slot],racers_[owner].forward);
            contactSounds(kind,player);
            destroyHeld(owner,slot); // A Bob-omb detonates where it trailed; its blast hits.
            if(action>=0&&strike(player,action,owner,away)&&hitVoiceItem(kind))sound(ItemSoundCue::HitSuccess,owner);
        }
    }
}
void RaceItems::boxContacts(unsigned player){
    // RMCU ObjItemBox kart collision (0x808151a4): any racer breaks the box; it only starts a
    // roulette when the inventory is empty and no roulette or locked item (Bullet) is pending.
    const auto& r=racers_[player];
    if(r.unavailable||r.finished)return;
    for(auto& box:boxes_)if(box.active&&box.appearance>=ItemBoxCollisionDelay){
        const Vec3 d=box.position-r.position;const float reach=r.radius+itemBoxPickupRadius(box);
        if(dot(d,d)>=reach*reach)continue;
        box.active=false;box.respawn=ItemBoxRespawn;
        sound(ItemSoundCue::BoxBreak,player,ItemKind::None,box.position);
        const auto& h=hud_[player];
        if(h.item==ItemKind::None&&!h.roulette&&!h.bullet&&!r.controlLocked)startRoulette(player);
    }
}
void RaceItems::kartContacts(unsigned player){
    if(!active_||player>=count_)return;
    boxContacts(player);
    // No finish gate: PlayerSub_update2 (PAL 0x80596CFC) runs the item pass for every kart,
    // gated only by the fade/vanish bits 82/83 (Kinoko KartSub::calcPass1 then skips this
    // hook). A finish sets them only for a ghost, 70 frames after its goal (0x80578968).
    // ItemHolder_checkPlayerCollision (0x80799CAC) itself returns at once in a Time Trial or
    // ghost race (flag 0x809C38BC, RaceGlobals_init 0x807BD1D0): no item object touches a kart.
    // Otherwise neither it, 0x807A14D4 nor KartAction::start (VS) tests a finish.
    if(timeTrial_)return;
    // Actor pool order stands in for the retail entity search order.
    for(auto& a:actors_)if(a.active&&itemTouchesKart(a,player))actorKartContact(a,player);
    heldKartContacts(player);
}
}
