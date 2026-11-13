// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#include "mkw/race_items.hpp"
#include "mkw/bullet_bill.hpp"
#include "mkw/item_rules.hpp"
#include "mkw/race_item_internal.hpp"
#include <algorithm>
#include <cmath>

// Items in an online race. Every console runs the whole item simulation, but
// each decides only for its own racer (racer 0): the item its roulette gives,
// the objects it throws or drops, and whether its kart is hit. It tells the
// others (ItemNetEvent, ItemNetState); they play what they are told on that
// racer and never roll, throw or hit for it themselves.
namespace mkw {
using namespace items;
namespace {
constexpr uint8_t HeldVetoFrames=90;
constexpr uint8_t Removed=3; // an object that goes without a break effect (a pickup)
unsigned bits(unsigned mask){unsigned n=0;for(;mask;mask&=mask-1)++n;return n;}
}

void RaceItems::netEmit(const ItemNetEvent& event){
    if(netOutCount_==NetQueue)return;
    netOut_[(netOutHead_+netOutCount_++)%NetQueue]=event;
}
bool RaceItems::takeNetEvent(ItemNetEvent& out){
    if(!netOutCount_)return false;
    out=netOut_[netOutHead_];netOutHead_=(netOutHead_+1)%NetQueue;--netOutCount_;return true;
}
ItemNetState RaceItems::netState()const{
    const auto& h=hud_[0];ItemNetState s;
    s.item=uint8_t(h.item);s.charges=h.charges;
    if(h.deployed){s.heldKind=uint8_t(h.heldKind);s.heldMask=h.heldMask;}
    s.flags=uint8_t((h.roulette?ItemNetState::Roulette:0)|(h.cloud?ItemNetState::Cloud:0)|(h.bullet?ItemNetState::Bullet:0));
    return s;
}

// ---- This console's racer -------------------------------------------------------------

void RaceItems::netAfterStep(){
    // Every object racer 0 put on the course since the last step.
    for(auto& a:actors_){
        if(a.active&&a.netGrace&&!--a.netGrace){shatter(a);continue;}
        if(!a.active||a.owner!=0||a.netId)continue;
        if(!++netNextId_)++netNextId_;
        a.netId=netNextId_;
        ItemNetEvent e;e.kind=ItemNetEvent::Spawn;e.a=uint8_t(a.kind);
        e.c=uint8_t((a.backwards?ItemNetEvent::Backwards:0)|(a.scattered?ItemNetEvent::Scattered:0)|(a.explosion?ItemNetEvent::Blast:0));
        e.d=uint8_t(std::clamp(a.scale*100.f,0.f,255.f));e.e=a.ownerImmunity;e.id=a.netId;
        e.v[0]=a.position.x;e.v[1]=a.position.y;e.v[2]=a.position.z;e.v[3]=a.velocity.x;e.v[4]=a.velocity.y;e.v[5]=a.velocity.z;
        netEmit(e);
    }
    // A hit the kart took from something other than an item contact here (a POW, a
    // Star or Mega racer, a course object). Lightning actions travel as Shrink.
    const int8_t action=racers_[0].damageAction;
    if(action!=netAction_){
        netAction_=action;
        if(action>=0&&action!=KartAction::Lightning&&action!=KartAction::SpinShrink&&action!=KartAction::CloudStrike){
            ItemNetEvent e;e.kind=ItemNetEvent::Hit;e.a=uint8_t(action);netEmit(e);
        }
    }
}
void RaceItems::netContact(unsigned player,const ItemActor& a,bool wasBlast,int started,Vec3 direction){
    if(!netSelf(player))return;
    const uint8_t owner=a.owner<0?255:uint8_t(a.owner);
    if((a.kind==ItemKind::Mushroom||a.kind==ItemKind::Star)&&!a.active){
        ItemNetEvent e;e.kind=ItemNetEvent::Take;e.a=uint8_t(a.kind);e.other=owner;e.id=a.netId;netEmit(e);return;
    }
    const uint8_t fate=!a.active?ItemNetEvent::Broken:a.explosion&&!wasBlast?ItemNetEvent::Exploded:ItemNetEvent::Kept;
    if(started<0&&(fate==ItemNetEvent::Kept||!a.netId))return;
    ItemNetEvent e;e.kind=ItemNetEvent::Hit;e.a=started<0?uint8_t(ItemNetEvent::NoAction):uint8_t(started);e.other=owner;
    e.c=uint8_t(a.kind);e.d=fate;e.id=a.netId;e.v[0]=direction.x;e.v[1]=direction.y;e.v[2]=direction.z;
    netEmit(e);
    if(started>=0)netAction_=int8_t(started);
}
void RaceItems::netHeldContact(unsigned player,unsigned owner,unsigned slot,ItemKind kind,int action,Vec3 direction){
    auto& h=hud_[owner];
    ItemNetEvent e;e.kind=ItemNetEvent::Hit;e.a=action<0?uint8_t(ItemNetEvent::NoAction):uint8_t(action);e.other=uint8_t(owner);
    e.c=uint8_t(kind);e.d=ItemNetEvent::Broken;e.id=uint16_t(ItemNetEvent::HeldSlot|slot);
    e.v[0]=direction.x;e.v[1]=direction.y;e.v[2]=direction.z;
    netEmit(e);
    if(action>=0&&netSelf(player))netAction_=int8_t(action);
    // Out of sight here until the owner's console has dealt with it. A chain of
    // Bananas lets go of every link behind the broken one.
    uint8_t gone=uint8_t(1u<<slot);
    if(h.heldKind==ItemKind::TripleBanana)for(unsigned n=slot+1;n<3;++n)gone|=uint8_t(1u<<n);
    gone&=h.heldMask;
    if(kind!=ItemKind::Bobomb){
        events_.push_back({ItemEffect::Break,uint8_t(owner),uint16_t(h.heldKind),deployments_[owner].positions[slot],-1,deployments_[owner].scales[slot]});
        sound(ItemSoundCue::Break,owner,kind,deployments_[owner].positions[slot]);
    }
    netHeldVeto_[owner]|=gone;netHeldVetoTicks_[owner]=HeldVetoFrames;
    h.heldMask&=uint8_t(~gone);h.heldCharges=uint8_t(bits(h.heldMask));
    if(!h.heldCharges){h.deployed=false;h.heldKind=ItemKind::None;}
}
void RaceItems::netActorChanged(const ItemActor& a,bool wasBlast){
    if(a.owner==0||a.owner<0||!a.netId)return;
    const uint8_t fate=!a.active?ItemNetEvent::Broken:a.explosion&&!wasBlast?ItemNetEvent::Exploded:ItemNetEvent::Kept;
    if(fate==ItemNetEvent::Kept)return;
    ItemNetEvent e;e.kind=ItemNetEvent::Item;e.a=fate;e.other=uint8_t(a.owner);e.id=a.netId;netEmit(e);
}

// ---- The other consoles' racers -------------------------------------------------------

void RaceItems::netHolder(unsigned player){
    held_[player]=false;
    if(netHeldVetoTicks_[player]&&!--netHeldVetoTicks_[player])netHeldVeto_[player]=0;
}
void RaceItems::applyNetState(unsigned player,const ItemNetState& s){
    if(player>=count_||!netRemote(player))return;
    auto& h=hud_[player];
    h.item=s.item<19?ItemKind(s.item):ItemKind::None;h.charges=h.item==ItemKind::None?0:s.charges;
    h.roulette=s.flags&ItemNetState::Roulette?1:0;
    // Tethered items: a new group starts small behind the kart, as on its own console.
    const uint8_t raw=s.heldKind<19?uint8_t(s.heldMask&7u):0;
    const ItemKind kind=raw?ItemKind(s.heldKind):ItemKind::None;
    const uint8_t before=netHeldRaw_[player];
    const bool fresh=raw&&(!before||(raw&~before)||(h.deployed&&kind!=h.heldKind));
    if(fresh)netHeldVeto_[player]=0;
    netHeldRaw_[player]=raw;netHeldVeto_[player]&=raw;
    const uint8_t mask=uint8_t(raw&~netHeldVeto_[player]);
    if(mask&&(fresh||!h.deployed))beginHeld(player,kind);
    h.deployed=mask!=0;h.heldKind=mask?kind:ItemKind::None;h.heldMask=mask;h.heldCharges=uint8_t(bits(mask));
    // The Thunder Cloud and the Bullet Bill ride with the kart.
    const bool cloud=s.flags&ItemNetState::Cloud,bullet=s.flags&ItemNetState::Bullet;
    if(cloud&&!h.cloud){h.cloud=CloudDuration;karts_->setThunderCloud(player,true);sound(ItemSoundCue::CloudOn,player);}
    else if(!cloud&&h.cloud){h.cloud=0;karts_->setThunderCloud(player,false);}
    if(bullet&&!h.bullet){
        if(h.cloud)sound(ItemSoundCue::CloudOff,player);
        h.slotLock=ItemKind::Bullet;karts_->setBullet(player,true);h.bullet=BulletDuration;h.ink=0;
        events_.push_back({ItemEffect::Cure,uint8_t(player)});
    }else if(!bullet&&h.bullet){h.bullet=0;karts_->setBullet(player,false);if(h.slotLock==ItemKind::Bullet)h.slotLock=ItemKind::None;}
}
ItemActor* RaceItems::netActor(unsigned owner,uint16_t id){
    if(!id||owner>=count_)return nullptr;
    for(auto& a:actors_)if(a.active&&a.netId==id&&a.owner==int(owner))return &a;
    return nullptr;
}
void RaceItems::netItemFate(unsigned owner,uint16_t id,uint8_t fate){
    auto* a=netActor(owner,id);if(!a||fate==ItemNetEvent::Kept)return;
    if(fate==ItemNetEvent::Exploded){if(!a->explosion&&blastItem(a->kind))explode(*a);return;}
    if(a->explosion)return;
    a->active=false;
    if(fate==ItemNetEvent::Broken){
        sound(ItemSoundCue::Hit,*a);
        events_.push_back({ItemEffect::Break,uint8_t(owner),uint16_t(a->kind),a->position,-1,a->scale});
    }
}
void RaceItems::netSpawn(unsigned player,const ItemNetEvent& e){
    const ItemKind kind=ItemKind(e.a);
    if(!(shell(kind)||trapItem(kind)||kind==ItemKind::Mushroom||kind==ItemKind::Star)||netActor(player,e.id))return;
    const bool backwards=e.c&ItemNetEvent::Backwards,scattered=e.c&ItemNetEvent::Scattered,blast=e.c&ItemNetEvent::Blast;
    const Vec3 position{e.v[0],e.v[1],e.v[2]},velocity{e.v[3],e.v[4],e.v[5]};
    auto& a=spawn(player,kind,backwards,&position);
    a.netId=e.id;a.position=a.previous=position;a.ownerImmunity=e.e;
    const auto prop=itemProperties(unsigned(kind));
    a.scale=float(e.d)*.01f;a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
    // A homing shell keeps the launch this console worked out for it.
    if(scattered||(kind!=ItemKind::RedShell&&kind!=ItemKind::BlueShell)){
        a.velocity=velocity;
        if(kind==ItemKind::GreenShell&&!scattered){
            const float speed=length(velocity);
            if(speed>1){a.travelSpeed=speed;a.flightDirection=velocity*(1/speed);}
        }
    }
    if(scattered){a.scattered=true;a.target=-1;a.guidance={};a.lifetime=0;}
    if(blast){a.velocity={};explode(a);a.position=a.previous=position;}
    else if(!scattered)sound(ItemSoundCue::Release,player,kind,position,!backwards);
}
void RaceItems::applyNetEvent(unsigned player,const ItemNetEvent& e){
    if(player>=count_||!netRemote(player))return;
    const unsigned other=e.other<count_?e.other:MaxRacers;
    switch(e.kind){
    case ItemNetEvent::Spawn:netSpawn(player,e);break;
    case ItemNetEvent::Use:
        if(e.a<19)useEffect(player,ItemKind(e.a));
        break;
    case ItemNetEvent::Hit:{
        // What the contact did to the object, then to the kart.
        if((e.id&ItemNetEvent::HeldSlot)==ItemNetEvent::HeldSlot){
            const unsigned slot=e.id&3u;
            if(other==0&&slot<3&&(hud_[0].heldMask>>slot&1u))destroyHeld(0,slot);
        }else if(other<MaxRacers)netItemFate(other,e.id,e.d);
        if(e.a==ItemNetEvent::NoAction||e.a>=KartAction::Count)break;
        netApplying_=true;
        strike(player,e.a,other,{e.v[0],e.v[1],e.v[2]},true,e.c<19?ItemKind(e.c):ItemKind::None);
        netApplying_=false;
        break;
    }
    case ItemNetEvent::Shrink:{
        const bool cloud=e.a&2;
        // The bolt restarts with the strike (fn_1_288374, 0x80579d88).
        events_.push_back({ItemEffect::Bolt,uint8_t(player),uint16_t(cloud?1:0)});
        netApplying_=true;shrink(player,e.id,e.a&1,cloud);netApplying_=false;
        sound(cloud?ItemSoundCue::CloudStrike:ItemSoundCue::Struck,player);
        break;
    }
    case ItemNetEvent::Take:
        if(other<MaxRacers)netItemFate(other,e.id,Removed);
        if(ItemKind(e.a)==ItemKind::Mushroom){
            karts_->activateMushroom(player);events_.push_back({ItemEffect::Mushroom,uint8_t(player)});++mushroomUses_[player];
        }else if(ItemKind(e.a)==ItemKind::Star&&karts_->activateStar(player))events_.push_back({ItemEffect::Cure,uint8_t(player)});
        break;
    case ItemNetEvent::Item:
        if(other<MaxRacers)netItemFate(other,e.id,e.a);
        break;
    case ItemNetEvent::Cloud:{
        // A cloud passed to this console's racer by its carrier.
        if(other!=0)break;
        auto& to=hud_[0];const auto& r=racers_[0];
        if(to.cloud||to.bullet||r.star||r.mega||r.bullet||r.inCannon||r.unavailable)break;
        to.cloud=std::clamp<uint16_t>(e.id,1,CloudDuration);cloudTransfer_[0]=CloudTransferCooldown;
        cloudAge_[0]=uint16_t(std::clamp(e.v[0],0.f,65535.f));to.cloudFrom=uint8_t(player);
        karts_->setThunderCloud(0,true);sound(ItemSoundCue::CloudAttach,0,uint8_t(player));
        break;
    }
    default:break;
    }
}
}
