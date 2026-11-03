#include "mkw/race_items.hpp"
#include "mkw/bobomb.hpp"
#include "mkw/item_box.hpp"
#include "mkw/race_item_internal.hpp"
#include "mkw/item_ground.hpp"
#include "mkw/item_ground_reclaim.hpp"
#include "mkw/item_rules.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
void traceLanding(const ItemActor& a,const ItemSurface& surface){
#ifndef MKW_RELEASE
    static unsigned count=0;
    if(a.owner!=0||(!(a.kind==ItemKind::Banana)&&!a.scattered)||a.age%32||count>=16)return;
    ++count;
    if(FILE* log=std::fopen("sdmc:/3ds/mkw-3ds/item-ground-state.txt","a")){
        std::fprintf(log,"kind=%u age=%u scattered=%u settled=%u pos=%.2f,%.2f,%.2f velocity=%.2f,%.2f,%.2f floor=%.3f,%.3f,%.3f correction=%.2f,%.2f,%.2f\n",unsigned(a.kind),a.age,a.scattered,a.settled,a.position.x,a.position.y,a.position.z,a.velocity.x,a.velocity.y,a.velocity.z,surface.floorNormal.x,surface.floorNormal.y,surface.floorNormal.z,surface.correction.x,surface.correction.y,surface.correction.z);
        std::fclose(log);
    }
#endif
}
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

ItemActor& RaceItems::spawn(unsigned player,ItemKind kind,bool backwards,const Vec3* origin){
    // Retail allocates from per-type pools, so a slot freed this step is never reused at once;
    // prefer slots that were already free when the step began.
    auto it=actors_.end();
    for(unsigned i=0;i<MaxActors&&it==actors_.end();++i)if(!actors_[i].active&&freeAtStepStart_[i])it=actors_.begin()+i;
    if(it==actors_.end())it=std::find_if(actors_.begin(),actors_.end(),[](const ItemActor& a){return !a.active;});
    if(it==actors_.end())it=std::max_element(actors_.begin(),actors_.end(),[](const ItemActor& a,const ItemActor& b){return a.age<b.age;});
    forgetReclaimSlot(unsigned(it-actors_.begin()));
    auto& a=*it;a={};a.kind=kind;a.owner=int8_t(player);a.active=true;a.lifetime=kind==ItemKind::Banana?0:1800;
    reclaimOrder_[retailObjectId(kind)].add(unsigned(it-actors_.begin()));
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
        // 297ebc -> 29a6e4 -> 27fe48/27fff0: ordinary straight driving
        // inherits signed speed, including the source backward subtraction.
        // Drift-side aim/special movement predicates remain in the audit.
        const Vec3 motion=itemMotion(racers_[player]);const float speed=length(motion);
        const bool movingForward=dot(motion,racers_[player].forward)>0;
        a.travelSpeed=backwards?(movingForward?30.f-speed:30.f+speed):(movingForward?85.f+speed:85.f-speed);
        a.flightDirection=normalized(direction*(backwards?40.f:35.f)+(movingForward?(backwards?-motion:motion):Vec3{}),direction);
        a.velocity=a.flightDirection*a.travelSpeed;
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
    // ObjFIB::SetInitialPosition 8079e934 -> 807a890c..8930: a forward
    // throw starts at the kart's position, not the trailing object's position.
    // Keeping a rear tether origin lets the first terrain sweep hit geometry
    // behind the kart, especially while stopped/turning around in Battle.
    if(kind==ItemKind::FakeBox&&!backwards)a.position=racers_[player].position;
    if(kind==ItemKind::RedShell||kind==ItemKind::BlueShell)launchShell(a,player);
    if(kind==ItemKind::BlueShell)events_.push_back({ItemEffect::BlueLaunch,uint8_t(player)});
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
    const auto surface=groundContact(query,a);
    a.floorTypes=surface.floorTypes;
    traceLanding(a,surface);
    if(surface.kill||surface.cannon){a.active=false;return;}
    a.position+=surface.correction+surface.floorMotion;
    if(length(surface.floorNormal)>.1f){
        const auto n=normalized(surface.floorNormal);
        if(!a.settled)sound(ItemSoundCue::Land,a);
        a.velocity=scatteredGroundVelocity(a.velocity,n);a.settled=true;a.up=n;
    }else {a.settled=false;a.velocity.y=std::max(-100.f,a.velocity.y-2.2f);}
    if(length(surface.wallNormal)>.1f){const auto n=normalized(surface.wallNormal);float into=dot(a.velocity,n);if(into<0)a.velocity=a.velocity-n*(2*into);}
}
void RaceItems::resolveCourseObjects(ItemActor& actor){
    if(!actor.active||actor.radius<=0||blueFlight(actor))return;
    struct Context {RaceItems* items;ItemActor* actor;} context{this,&actor};
    karts_->courseContacts(retailObjectId(actor.kind),actor.owner<0?12u:unsigned(actor.owner),actor.position,actor.velocity,actor.radius,false,
        [](void* data,uint8_t response,Vec3 penetration){
            auto& c=*static_cast<Context*>(data);auto& a=*c.actor;
            if(response==1&&!a.explosion)bumpItem(a,penetration,{});
            else if(response==2&&!a.explosion){
                if(blastItem(a.kind))c.items->explode(a);
                else c.items->shatter(a);
                return false;
            }
            return a.active;
        },&context);
}

void RaceItems::resolveHeldCourseObjects(unsigned player){
    const auto& h=hud_[player];if(!h.deployed||racers_[player].inCannon)return;
    const auto kind=baseKind(h.heldKind);
    for(unsigned slot=0;slot<3;++slot)if((h.heldMask&(1u<<slot))&&
            deployments_[player].scales[slot]>=HeldContactMinimumScale){
        struct Context {RaceItems* items;unsigned player,slot;} context{this,player,slot};
        const auto& d=deployments_[player];
        karts_->courseContacts(retailObjectId(kind),player,d.positions[slot],d.positions[slot]-d.previous[slot],
            heldItemContactRadius(player,slot),true,
            [](void* data,uint8_t response,Vec3){
                auto& c=*static_cast<Context*>(data);
                if(response==2){c.items->destroyHeld(c.player,c.slot);return false;}
                return true;
            },&context);
    }
}

void RaceItems::forgetReclaimSlot(unsigned slot){
    for(auto& holder:reclaimOrder_)holder.remove(slot);
}
void RaceItems::syncReclaimOrder(){
    for(unsigned type=0;type<15;++type){
        auto& h=reclaimOrder_[type];
        for(unsigned i=0;i<h.count;){
            const auto slot=h.slots[i];const auto& a=actors_[slot];
            if(!a.active||retailObjectId(a.kind)!=type)h.remove(slot);else ++i;
        }
    }
    for(unsigned i=0;i<MaxActors;++i){
        const auto& a=actors_[i];if(!a.active)continue;
        auto& h=reclaimOrder_[retailObjectId(a.kind)];h.add(i);
        if(a.settled&&(a.scattered||a.kind==ItemKind::Banana))h.land(i);
    }
}
void RaceItems::updateActors(ItemWorldQuery query){
    syncReclaimOrder();
    const unsigned phase=nativeReclaimPhase_?reclaimPhase_:(frame_&7);
    const auto eligible=[&](unsigned slot){const auto& a=actors_[slot];
        return a.active&&groundReclaimEligible(a.age,a.scattered||a.kind==ItemKind::Banana,a.settled,a.explosion);};
    // USA8078d464 uses the gameplay limit, not allocated capacity. USA80790844
    // computes the global pressure AFTER all holders, so every holder reads the
    // previous step's value. All fifteen retail +0x5c weights are one.
    for(unsigned type=0;type<15;++type){
        syncReclaimOrder();
        auto& h=reclaimOrder_[type];
        const int free=int(ItemObjectLimits[type])-int(objectCount(type));
        if(ItemObjectLimits[type]>1&&(free<2||reclaimPressure_>-5)){
            h.sort(phase,type==2,eligible,
                [&](unsigned slot){return actors_[slot].floorTypes;},
                [&](unsigned slot){return actors_[slot].explosion;});
            if(phase==0&&(free<1||reclaimPressure_>0)&&h.count&&eligible(h.slots[0])){
                const auto slot=h.slots[0];actors_[slot].active=false;h.remove(slot);
            }
        }
        // Keep holder order for movement and contacts, too. Copy the slot list
        // because a contact can remove another actor during this holder's step.
        const auto order=h.slots;const unsigned n=h.count;
        for(unsigned k=0;k<n;++k){
            auto& a=actors_[order[k]];if(!a.active)continue;
            updateActor(a,query);
            if(!a.active){h.remove(order[k]);continue;}
            if(a.settled&&(a.scattered||a.kind==ItemKind::Banana))h.land(order[k]);
            resolveCourseObjects(a);if(!a.active)continue;
            for(unsigned owner=0;owner<count_&&a.active;++owner)
                for(unsigned slot=0;slot<3&&a.active;++slot)resolveActorHeld(a,owner,slot);
            for(unsigned other=0;other<MaxActors&&a.active;++other)
                if(other!=order[k]&&actors_[other].active)resolveActorPair(a,actors_[other]);
        }
    }
    syncReclaimOrder();
    reclaimPressure_=-30;
    for(unsigned type=0;type<15;++type)reclaimPressure_+=int(objectCount(type));
}

void RaceItems::updateActor(ItemActor& a,ItemWorldQuery query){
    {
        a.previous=a.position;++a.age;
        if(a.explosion){
            // Blast update: grow, then end collision and despawn on the retail ticks.
            a.radius=bombBlastRadius(a.age,a.blastInitialScale);
            if(a.age>=a.lifetime)a.active=false;
            return;
        }
        const auto prop=itemProperties(unsigned(a.kind));
        a.scale=std::min(prop.releasedScale,a.scale+(a.backwards?prop.dropGrowth:prop.growth));
        a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
        const bool groundLifetime=a.scattered||a.kind==ItemKind::Banana;
        if(!groundLifetime&&a.lifetime&&a.age>=a.lifetime){
            // Ordinary Green/Red expiry calls KillFromOtherCollision -> OnKill
            // (807a4014 / 8079fc18), including the shell break effect and sound.
            // Ground-dropped items are retired by holder pressure above.
            if(!a.scattered&&(a.kind==ItemKind::GreenShell||a.kind==ItemKind::RedShell)){
                shatter(a);return;
            }
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
        }else{
            // Green's retail update moves first, projects at KCL contact, then
            // applies gravity/speed convergence for the following frame.
            if(trapItem(a.kind)&&!a.settled){
                const float drag=a.kind==ItemKind::Banana?(a.backwards?.97f:.999f):a.kind==ItemKind::FakeBox?(a.backwards?.97f:.996f):(a.backwards?.973f:.999f);
                a.velocity=a.velocity*drag;
            }
            a.position+=a.velocity;a.yaw=std::atan2(a.velocity.x,a.velocity.z);
            if(query){
                const auto surface=groundContact(query,a);a.position+=surface.correction+surface.floorMotion;a.floorTypes=surface.floorTypes;
                traceLanding(a,surface);
                if(surface.kill||surface.cannon){a.active=false;return;}
                if(length(surface.floorNormal)>.1f){
                    const Vec3 normal=normalized(surface.floorNormal);const float into=dot(a.velocity,normal);
                    a.up=normal;
                    if(a.kind==ItemKind::GreenShell){
                        a.flightDirection=normalized(a.flightDirection-normal*dot(a.flightDirection,normal),a.flightDirection);
                        a.velocity=a.flightDirection*a.travelSpeed-normal*2.5f;
                    }else if(trapItem(a.kind)){
                        // fn_1_294794: a Bob-omb thuds on every floor hit.
                        if(a.kind==ItemKind::Bobomb&&!a.settled)sound(ItemSoundCue::Bounce,a);
                        if(a.floorBounces){
                            --a.floorBounces;const Vec3 component=normal*into;
                            const float restitution=a.kind==ItemKind::Banana?0:a.kind==ItemKind::Bobomb?(a.backwards?a.bounceImpulse:.45f):a.backwards?0:.45f;
                            a.velocity=(a.velocity-component)*.55f-component*restitution;
                            if(a.kind==ItemKind::Bobomb&&a.backwards)a.bounceImpulse*=.8f;
                        }else{
                            // ObjThrowable::OnFinishThrow (807a8078): the object's land sound once.
                            if(!a.settled)sound(ItemSoundCue::Land,a);
                            a.velocity={};a.settled=true;
                        }
                    }else if(into<0)a.velocity=a.velocity-normal*into;
                }else if(a.kind==ItemKind::GreenShell)a.velocity.y-=a.velocity.y>0?4.2f:2.f;
                else if(trapItem(a.kind)){
                    a.settled=false;
                    const float gravity=a.kind==ItemKind::Banana?(a.backwards?1.8f:2.2f):a.kind==ItemKind::FakeBox?(a.backwards?1.8f:2.1f):(a.backwards?2.8f:2.3f);
                    const float terminal=std::clamp(100.f+150.f*(float(a.age)-150.f)/(30.f-150.f),100.f,250.f);
                    a.velocity.y=std::max(-terminal,a.velocity.y-gravity);
                }
                if(length(surface.wallNormal)>.1f){const Vec3 normal=normalized(surface.wallNormal);const float into=dot(a.velocity,normal);
                    if(a.kind==ItemKind::RedShell){shatter(a);return;}
                    if(into<0){
                        a.velocity=a.velocity-normal*(2*into);a.flightDirection=normalized(a.flightDirection-normal*(2*dot(a.flightDirection,normal)),a.flightDirection);
                        if(++a.bounces>=a.bounceLimit||(a.kind==ItemKind::GreenShell&&a.age>900)){shatter(a);return;}
                        if(shell(a.kind))sound(ItemSoundCue::WallBounce,a);   // fn_1_2A5140: SE_ITM_KAME_HANSHA
                    }
                }
            }else if(trapItem(a.kind)){
                const float gravity=a.kind==ItemKind::Banana?(a.backwards?1.8f:2.2f):a.kind==ItemKind::FakeBox?(a.backwards?1.8f:2.1f):(a.backwards?2.8f:2.3f);
                const float terminal=std::clamp(100.f+150.f*(float(a.age)-150.f)/(30.f-150.f),100.f,250.f);
                a.velocity.y=std::max(-terminal,a.velocity.y-gravity);
            }
            if(a.kind==ItemKind::GreenShell){
                if(!query)a.velocity.y-=a.velocity.y>0?4.2f:2.f;
                a.travelSpeed+=.01f*(105.f-a.travelSpeed);
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
            karts_->activateMushroom(player);events_.push_back({ItemEffect::Mushroom,uint8_t(player)});++mushroomUses_[player];
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
    if(action>=0)strike(player,action,owner,away,false,a.kind);
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
            if(action>=0)strike(player,action,owner,away,false,kind);
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
    const auto& r=racers_[player];
    boxContacts(player);
    if(r.finished)return;
    // Actor pool order stands in for the retail entity search order.
    for(auto& a:actors_)if(a.active&&itemTouchesKart(a,player))actorKartContact(a,player);
    heldKartContacts(player);
}
}
