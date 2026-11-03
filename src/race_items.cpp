#include "mkw/race_items.hpp"
#include "mkw/bullet_bill.hpp"
#include "mkw/blooper.hpp"
#include "mkw/item_box.hpp"
#include "mkw/item_kart_model.hpp"
#include "mkw/item_rules.hpp"
#include "mkw/item_visual.hpp"
#include "mkw/race_item_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Item holders: inventory, roulette, held (tethered) groups and item use. Retail
// ItemHolderPlayer (RMCU 0x80797928 update); every effect on a kart goes through ItemKarts.
namespace mkw {
using namespace items;

const char* itemName(ItemKind kind){
    constexpr const char* names[]={"Green Shell","Red Shell","Banana","Fake Item Box","Mushroom","Triple Mushrooms","Bob-omb","Spiny Shell","Lightning","Star","Golden Mushroom","Mega Mushroom","Blooper","POW Block","Thunder Cloud","Bullet Bill","Triple Green Shells","Triple Red Shells","Triple Bananas"};
    const unsigned id=unsigned(kind);return id<19?names[id]:"No item";
}
RaceItems::RaceItems():ownKarts_(std::make_unique<ItemKartModel>()),karts_(ownKarts_.get()){}
RaceItems::~RaceItems(){unhookNative();}
void RaceItems::setKarts(ItemKarts* karts){karts_=karts?karts:ownKarts_.get();}
void RaceItems::reset(const std::vector<ItemBox>& boxes,unsigned racers,uint32_t seed){
    boxes_=boxes;for(auto& box:boxes_){box.respawn=0;box.active=true;box.appearance=ItemBoxAppearanceFrames+1;}
    movers_.clear();lines_.clear();presses_.clear();roadBoxes_.clear();
    shellRoute_.clear();shellCourse_=0xffff;actors_={};reclaimOrder_={};reclaimPressure_=0;reclaimPhase_=0;nativeReclaimPhase_=false;freeAtStepStart_.fill(true);hud_={};racers_={};itemPointPending_=0;inputs_={};held_={};heldDropTicks_={};inventoryDrops_={};cloudTransfer_={};cloudAge_={};cloudSlot_={};itemLock_={};bullets_={};bulletNoEnd_=0;
    inkReadyFrame_={};rouletteChoice_.fill(ItemKind::None);roulettes_={};rouletteRank_={};rouletteRng_=seed^0x72756c65u;
    // Retail ItemSlotData_updateTimers (USA2a0f58) initializes these during
    // countdown; the Blue timer has no reset call in the offline controller.
    itemSpawnTimers_={1800,1800,900,1200};
    blooperAttack_={};blooperEnd_={};blooperEndPose_={};
    blooperCaster_=MaxRacers;blooperSelectionFrame_=blooperStartFrame_=blooperBusyUntil_=0;
    powTimer_=0;powUser_=MaxRacers;powTargets_={};
    deployments_={};mushroomUses_={};
    karts_->resetKarts();
    successfulHits_.fill(0);events_.clear();events_.reserve(64);sounds_.clear();sounds_.reserve(32);stepSounds_=0;count_=std::min(racers,MaxRacers);rng_=seed?seed:1;frame_=0;active_=contactsDone_=false;
}
uint32_t RaceItems::random(){rng_=rng_*1664525u+1013904223u;return rng_;}
unsigned RaceItems::rank(unsigned player) const{
    if(player>=count_)return count_;
    unsigned result=1;for(unsigned i=0;i<count_;++i)if(racers_[i].progress>racers_[player].progress||(racers_[i].progress==racers_[player].progress&&i<player))++result;
    return result;
}
BlooperVisual RaceItems::blooperVisual(unsigned player,unsigned weight)const{
    if(player>=count_||!blooperBusyUntil_)return {};
    auto visual=mkw::blooperVisual(frame_,blooperStartFrame_,blooperAttack_[player],blooperEnd_[player],weight);
    // The entry follows the caster; the attack follows this screen's victim.
    visual.player=visual.clip&&!std::strcmp(visual.clip,"start")?blooperCaster_:player;
    return visual;
}
bool RaceItems::invincible(unsigned player) const{
    if(player>=count_)return false;const auto& r=racers_[player];
    return r.star||r.mega||r.bullet||hud_[player].bullet;
}
uint16_t RaceItems::bulletPlayers() const{
    uint16_t mask=0;for(unsigned i=0;i<count_;++i)if(hud_[i].bullet)mask|=uint16_t(1u<<i);return mask;
}
bool RaceItems::give(unsigned player,ItemKind kind){
    if(player>=count_||unsigned(kind)>=19||hud_[player].item!=ItemKind::None||hud_[player].roulette)return false;
    auto& h=hud_[player];h.item=kind;h.charges=triple(kind)?3:1;
    if(kind==ItemKind::ThunderCloud){
        // RMCU ItemHolderPlayer_useTc 0x807d6c10: the cloud spawns at once (0x807d6c88: transfer
        // cooldown 5, timer 0); its icon stays in the slot, unusable, for 60 frames (0x807d71c0).
        h.charges=0;h.cloud=CloudDuration;cloudTransfer_[player]=CloudSpawnCooldown;cloudAge_[player]=0;cloudSlot_[player]=CloudSlotFrames;
        karts_->setThunderCloud(player,true);sound(ItemSoundCue::CloudOn,player);
    }
    return true;
}
bool RaceItems::startRoulette(unsigned player){
    if(player>=count_)return false;
    // fn_1_2A9BFC (0x807ab550) also needs the slot lock (+0x78) clear.
    auto& h=hud_[player];if(h.item!=ItemKind::None||h.roulette||h.bullet||h.slotLock!=ItemKind::None)return false;
    // ItemSlot decides at contact. Passing racers during the roulette
    // animation must not change the selected prize.
    ItemKind choice=choose(player);
    // RMCU 0x807ab550 -> 0x80790bdc: the whole use must fit the type's capacity, else a
    // Mushroom (a Triple Mushroom stays).
    if(unsigned(choice)<19&&choice!=ItemKind::TripleMushroom){
        const auto use=ItemObjects[unsigned(choice)];
        if(itemObjectCapacity(use.type)<objectCount(use.type)+use.count)choice=ItemKind::Mushroom;
    }
    rouletteChoice_[player]=choice;rouletteRank_[player]=uint8_t(oddsRank(player));
    roulettes_[player]={};h.roulette=1;h.rouletteIcon=nextRouletteIcon(player,ItemKind::None);return true;
}
unsigned RaceItems::objectCount(unsigned type) const{
    unsigned n=0;
    auto add=[&](ItemKind kind,unsigned copies){if(unsigned(kind)<19&&ItemObjects[unsigned(kind)].type==type)n+=copies;};
    for(const auto& a:actors_)if(a.active)add(a.kind,1);
    for(unsigned i=0;i<count_;++i){
        const auto& h=hud_[i];
        if(h.item!=ItemKind::None&&!(h.item==ItemKind::ThunderCloud&&!h.charges))add(h.item,h.charges);
        if(h.deployed)add(h.heldKind,h.heldCharges);
        if(h.cloud)add(ItemKind::ThunderCloud,1);
        if(h.bullet)add(ItemKind::Bullet,1);
        if(h.pow)add(ItemKind::Pow,i==powUser_?1:0);
    }
    if(frame_<blooperBusyUntil_)add(ItemKind::Blooper,1);
    return n;
}
unsigned RaceItems::courseDropWeight(unsigned slot)const{
    if(slot!=6||!woodboxOdds_.ready())return 0;
    auto w=woodboxOdds_.weights();
    if(objectCount(ItemObjects[unsigned(ItemKind::Banana)].type)>=ItemObjectLimits[2])w[0]=0;
    if(objectCount(ItemObjects[unsigned(ItemKind::Mushroom)].type)>=ItemObjectLimits[3])w[1]=0;
    return w[0]+w[1];
}
bool RaceItems::createCourseDrop(unsigned slot,Vec3 position,Vec3 velocity,uint8_t owner,
        uint32_t randomWord,bool timeTrial){
    if(!active_||slot!=6||!woodboxOdds_.ready())return false;
    auto w=woodboxOdds_.weights();
    if(objectCount(2)>=ItemObjectLimits[2])w[0]=0;
    if(objectCount(3)>=ItemObjectLimits[3])w[1]=0;
    const unsigned total=w[0]+w[1];
    // Original NextLimited scales the high word; zero-sum falls back to Mushroom.
    const unsigned draw=unsigned((uint64_t(randomWord)*total)>>32);
    const ItemKind kind=total&&draw<w[0]?ItemKind::Banana:ItemKind::Mushroom;
    if(owner>=count_||owner>=12)return false;
    if(objectCount(ItemObjects[unsigned(kind)].type)>=itemObjectCapacity(ItemObjects[unsigned(kind)].type))return false;
    auto it=std::find_if(actors_.begin(),actors_.end(),[](const ItemActor& a){return !a.active;});
    if(it==actors_.end())return false; // direct generation never evicts a live actor
    forgetReclaimSlot(unsigned(it-actors_.begin()));
    auto& a=*it;a={};a.active=true;a.kind=kind;a.owner=int8_t(owner);
    reclaimOrder_[ItemObjects[unsigned(kind)].type].add(unsigned(it-actors_.begin()));
    a.position=a.previous=position;a.velocity=velocity+Vec3{0,timeTrial?35.f:25.f,0};
    a.scattered=true;a.up={0,1,0};a.yaw=std::atan2(velocity.x,velocity.z);
    const auto prop=itemProperties(unsigned(kind));
    a.scale=.7f;a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
    a.ownerImmunity=7;a.lifetime=0;
    return true;
}
ItemKind RaceItems::choose(unsigned player){
    // Original offline VS tables, with separate human and CPU distributions
    // (RMCU ItemSlotData_decideItem 0x807ac9cc).
    if(!odds_.ready())return ItemKind::None;
    auto weights=odds_.weights(player!=0,oddsRank(player));
    // Spawn timers (Lightning, Blue, Blooper, POW).
    constexpr ItemKind delayed[]={ItemKind::Lightning,ItemKind::BlueShell,ItemKind::Blooper,ItemKind::Pow};
    for(unsigned i=0;i<4;++i)if(itemSpawnTimers_[i])weights[unsigned(delayed[i])]=0;
    const auto& h=hud_[player];const auto& r=racers_[player];
    // No triple shells or bananas while holding one (0x8078fdb0).
    if(unsigned(h.item)>=16&&unsigned(h.item)<=18)for(unsigned i=16;i<19;++i)weights[i]=0;
    if(h.deployed&&unsigned(h.heldKind)>=16&&unsigned(h.heldKind)<=18)for(unsigned i=16;i<19;++i)weights[i]=0;
    // No Mega or Thunder Cloud during a Mega, no Thunder Cloud during a Star.
    if(r.mega){weights[unsigned(ItemKind::MegaMushroom)]=0;weights[unsigned(ItemKind::ThunderCloud)]=0;}
    if(r.star)weights[unsigned(ItemKind::ThunderCloud)]=0;
    // One Bullet Bill on the course at a time; every type under its object limit.
    bool bullet=false;for(unsigned i=0;i<count_;++i)bullet|=hud_[i].bullet||racers_[i].bullet;
    std::array<unsigned,ItemObjectLimits.size()> live{};for(unsigned t=0;t<live.size();++t)live[t]=objectCount(t);
    for(unsigned i=0;i<19;++i){
        const auto type=ItemObjects[i].type;
        if((type==BulletObjectType&&bullet)||live[type]>=ItemObjectLimits[type])weights[i]=0;
    }
    unsigned total=0;for(auto weight:weights)total+=weight;
    if(!total)return ItemKind::Mushroom; // zero-sum fallback
    unsigned draw=random()%total;
    for(unsigned i=0;i<19;++i){if(draw<weights[i])return ItemKind(i);draw-=weights[i];}
    return ItemKind::None;
}
ItemKind RaceItems::nextRouletteIcon(unsigned player,ItemKind previous){
    // Retail's roulette pictures are sampled uniformly from the nonzero row
    // entries, separately from the already selected weighted prize.
    const auto& weights=odds_.weights(player!=0,rouletteRank_[player]);
    std::array<ItemKind,19> options{};unsigned count=0;
    for(unsigned i=0;i<19;++i)if(weights[i])options[count++]=ItemKind(i);
    if(!count)return ItemKind::Mushroom;
    rouletteRng_=rouletteRng_*1664525u+1013904223u;
    unsigned index=rouletteRng_%count;
    if(options[index]==previous)index=(index+1)%count;
    return options[index];
}
ItemInput RaceItems::autoItemInput(unsigned player) const{
    if(player>=count_||racers_[player].unavailable||racers_[player].controlLocked)return {};
    const auto& h=hud_[player];
    const unsigned cadence=h.golden?12:90; // QA autoplay taps the Golden every 12 frames
    const auto kind=h.deployed?h.heldKind:h.item;
    return {kind!=ItemKind::None&&((frame_+player*17)%cadence==0),kind==ItemKind::Banana||kind==ItemKind::FakeBox||kind==ItemKind::TripleBanana};
}
bool RaceItems::segmentHits(Vec3 from,Vec3 to,Vec3 center,float radius){
    const Vec3 delta=to-from;const float d=dot(delta,delta);
    const float t=d>.0001f?std::clamp(dot(center-from,delta)/d,0.f,1.f):0;
    const Vec3 near=from+delta*t-center;return dot(near,near)<=radius*radius;
}
int RaceItems::targetAhead(unsigned player,bool leader) const{
    int target=-1;float best=leader?-std::numeric_limits<float>::infinity():std::numeric_limits<float>::infinity();
    for(unsigned i=0;i<count_;++i){
        if((i==player&&!leader)||racers_[i].finished||racers_[i].unavailable)continue;
        const float progress=racers_[i].progress;
        if(leader){if(progress>best){best=progress;target=int(i);}}
        else if(progress>racers_[player].progress&&progress<best){best=progress;target=int(i);}
    }
    return target;
}

// ---- Effects on karts -----------------------------------------------------------------

bool RaceItems::strike(unsigned player,int action,unsigned owner,Vec3 direction,bool force,ItemKind item){
    if(player>=count_||action<0)return false;
    const int started=karts_->startAction(player,action,force,owner,direction);
    // RMCU: the start function reports item loss to the holder at once.
    applyLoss(player,karts_->takeLoss(player));
    if(started<0)return false;
    action=started;
    const ItemEffect effect=action==KartAction::Explosion||action==KartAction::HighLaunch?ItemEffect::Explosion:
        action==KartAction::Launch||action==KartAction::LaunchSpin?ItemEffect::Launch:
        action==KartAction::StarHit||action==KartAction::Knockback||action==KartAction::BulletHit?ItemEffect::Knockback:
        action==KartAction::MegaCrush?ItemEffect::Crush:action==KartAction::Pow?ItemEffect::Pow:ItemEffect::Spin;
    events_.push_back({effect,uint8_t(player),0,direction,int8_t(action),1.f,uint8_t(owner<MaxRacers?owner:0xff),item});
    if(owner<count_&&owner!=player){++successfulHits_[owner];sound(ItemSoundCue::HitSuccess,owner);}
    return true;
}
bool RaceItems::shrink(unsigned player,uint16_t frames,bool spinShrink,bool cloud){
    // PlayerSub10_applyLightningEffect: a Mega racer only loses the Mega (no shrink event).
    const bool mega=racers_[player].mega;
    const bool struck=karts_->lightning(player,frames,spinShrink,cloud);
    applyLoss(player,karts_->takeLoss(player));
    if(!struck)return false;
    events_.push_back({ItemEffect::ElectricHit,uint8_t(player)});
    if(!mega)events_.push_back({ItemEffect::Shrink,uint8_t(player),frames,{},int8_t(spinShrink?KartAction::SpinShrink:cloud?KartAction::CloudStrike:KartAction::Lightning)});
    return true;
}
void RaceItems::applyLoss(unsigned player,const ItemKartLoss& loss){
    if(player>=count_||!loss.any())return;
    auto& h=hud_[player];
    if(loss.strong){
        // RMCU fn_28867C: eject the inventory unless it is the Thunder Cloud or a loss is
        // already pending (2AC25C), stop the roulette (2AA224), then drop every tethered item.
        ItemKind kind=h.item==ItemKind::TripleGreenShell?ItemKind::GreenShell:h.item==ItemKind::TripleRedShell?ItemKind::RedShell:h.item==ItemKind::TripleBanana?ItemKind::Banana:h.item==ItemKind::TripleMushroom?ItemKind::Mushroom:h.item;
        if((kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::Mushroom||kind==ItemKind::Star||trapItem(kind))&&h.charges&&!inventoryDrops_[player].ticks){
            auto& drop=inventoryDrops_[player];drop={};drop.kind=kind;drop.count=h.charges;drop.ticks=10;
            // 2a18bc: table directions, random rotation and per-axis [1,1.2)
            // multipliers. Physical release occurs at 7,5,3 remaining.
            const float angle=float(random()&65535)*(2*Pi/65536.f);
            const Vec3 forward=normalized(racers_[player].forward,{0,0,1}),right=normalized(cross(racers_[player].up,forward),{1,0,0});
            for(unsigned n=0;n<drop.count;++n){
                const float theta=drop.count==1?0:drop.count==2?(n? -2*Pi/3:2*Pi/3):n*2*Pi/3;
                const float x=std::cos(theta)*(1+float(random()>>8)*(.2f/16777216.f));
                const float z=std::sin(theta)*(1+float(random()>>8)*(.2f/16777216.f));
                const float magnitude=kind==ItemKind::Star?15.f:20.f;
                drop.velocity[n]=right*(magnitude*(x*std::cos(angle)-z*std::sin(angle)))+forward*(magnitude*(z*std::cos(angle)+x*std::sin(angle)));
            }
        }
        if(h.item!=ItemKind::ThunderCloud){h.item=ItemKind::None;h.charges=0;h.golden=0;}
        if(h.roulette){h.roulette=0;rouletteChoice_[player]=ItemKind::None;roulettes_[player]={};}
    }
    // RMCU fn_288774: weak loss drops tethered items; with its flag set only a single
    // trailing item (PlayerObj use type TRAIL) is dropped.
    const bool all=loss.strong||loss.tethered;
    if(h.deployed&&(all||singleHeld(h.heldKind))&&!heldDropTicks_[player])heldDropTicks_[player]=10;
}
void RaceItems::takeKartLosses(){
    for(unsigned i=0;i<count_;++i)applyLoss(i,karts_->takeLoss(i));
}
void RaceItems::syncKart(unsigned player){
    // The kart owns the Star and Mega timers (RMCU KartMove +0x18a / +0x194).
    auto& h=hud_[player];const auto& r=racers_[player];
    h.star=r.star;h.mega=r.mega;
}

// ---- Held (tethered) items ------------------------------------------------------------

Vec3 RaceItems::heldItemPosition(unsigned player,unsigned slot)const{
    return player<count_&&slot<3?deployments_[player].positions[slot]:Vec3{};
}
HeldItemVisual RaceItems::heldItemVisual(unsigned player,unsigned slot)const{
    if(player>=count_||slot>=3)return {};
    const auto& s=deployments_[player];const auto& r=racers_[player];
    const Vec3 z=normalized(s.forwards[slot],r.forward);
    const Vec3 x=normalized(cross(s.ups[slot],z),{1,0,0});
    return {x,cross(z,x),z,r.scale*s.scales[slot]};
}
float RaceItems::heldItemContactRadius(unsigned player,unsigned slot)const{
    if(player>=count_||slot>=3)return 0;
    const auto& s=deployments_[player];
    return itemProperties(unsigned(hud_[player].heldKind)).heldContact*s.scales[slot]*racers_[player].scale.z;
}
void RaceItems::updateHeldItems(unsigned player,ItemWorldQuery query){
    auto& h=hud_[player];if(!h.deployed)return;
    auto& state=deployments_[player];const auto& racer=racers_[player];
    const auto prop=itemProperties(unsigned(h.heldKind));
    state.previous=state.positions;
    if(state.age<65535)++state.age;
    // The source computes positions using the previous tick's terrain volume,
    // then grows the object and updates its separate contact volume.
    if(trailing(h.heldKind)){
        const float terrain=prop.terrain*state.scales[0]*racer.scale.y;
        const float oldHop=state.trailing.hop;
        state.trailing.step(trailingPose(racer,terrain),h.heldCharges);
        for(unsigned slot=0;slot<h.heldCharges;++slot){
            Vec3 position=state.trailing.positions[slot];
            const float slotTerrain=prop.terrain*state.scales[slot]*racer.scale.y;
            if(slot)position+=racer.up*(slotTerrain-terrain);
            state.trailing.positions[slot]=position;
            const auto surface=query&&!racer.inCannon?query(state.previous[slot],position,slotTerrain):ItemSurface{};
            // Cannon KCL destroys released actors, not items tethered to the
            // kart. Native PlayerObj and Player lockout remain distinct.
            if(surface.kill){removeHeld(player,slot,true);break;}
            state.trailing.correct(slot,surface.correction,length(surface.correction)>.0001f);
            state.positions[slot]=state.trailing.positions[slot];
            // 27d9b8/27da58: rear-facing traps, forward-facing shells, spring
            // tilt from hop displacement, and .08 up-vector response.
            Vec3 facing=normalized(racer.forward+state.trailing.backward*2+Vec3{0,-.0007f*state.trailing.hop*(state.trailing.hop-oldHop),0},-racer.forward);
            if(h.heldKind==ItemKind::GreenShell||h.heldKind==ItemKind::RedShell)facing=-facing;
            state.ups[slot]=normalized(lerp(state.ups[slot],racer.up,.08f),racer.up);
            state.forwards[slot]=facing;
            const float before=state.scales[slot];
            state.scales[slot]=heldGrowth(state.scales[slot],prop.heldScale,state.age,singleHeld(h.heldKind),slot);
            // Item::PlayerObj::TrailUpdate 80789cd8: SE_ITM_EQUIP as a single item grows past .8.
            if(singleHeld(h.heldKind)&&slot==0&&before<.8f&&state.scales[slot]>=.8f)
                sound(ItemSoundCue::Equip,player,h.heldKind,state.positions[slot]);
        }
        return;
    }
    // 27f654: slot orbit and model spin are independent uint16 angles.
    if(state.angularSpeed<1200)state.angularSpeed+=70;
    state.angle+=state.angularSpeed;state.spin+=2000;
    state.radiusX+=.2f*((racer.itemGeometry[1]+30)*racer.scale.z-state.radiusX);
    state.radiusZ+=.2f*((racer.itemGeometry[0]+30)*racer.scale.x-state.radiusZ);
    state.hop=(state.hop+.4f*racer.hop)*.8f;
    const Vec3 forward=normalized(racer.forward,{0,0,1}),up=normalized(racer.up,{0,1,0});
    const Vec3 right=normalized(cross(up,forward),{1,0,0});
    const Vec3 center=racer.position+forward*(racer.itemGeometry[3]*racer.scale.z)+up*(racer.itemGeometry[2]*racer.scale.y+prop.terrain*state.scales[0]*racer.scale.y+state.hop-racer.hop);
    for(unsigned slot=0;slot<3;++slot)if(h.heldMask&(1u<<slot)){
        const float angle=float(state.angle)*(2*Pi/65536.f)+slot*(2*Pi/3.f);
        state.positions[slot]=center+forward*(state.radiusX*std::cos(angle))+right*(state.radiusZ*std::sin(angle));
        const auto surface=query&&!racer.inCannon?query(state.previous[slot],state.positions[slot],prop.terrain*state.scales[slot]*racer.scale.y):ItemSurface{};
        if(surface.kill){removeHeld(player,slot,false);continue;}
        auto& correction=state.trailing.corrections[slot];
        correction=length(surface.correction)>.0001f?lerp(correction,surface.correction,.2f):correction*.9f;
        state.positions[slot]+=correction;
        const float spin=float(uint16_t(state.spin+slot*0x350b))*(2*Pi/65536.f);
        state.forwards[slot]=forward*std::cos(spin)+right*std::sin(spin);state.ups[slot]=up;
        state.scales[slot]=heldGrowth(state.scales[slot],prop.heldScale,state.age,false,0);
    }
}
void RaceItems::scatterHeld(unsigned player,unsigned slot){
    auto& h=hud_[player];auto& d=deployments_[player];
    const ItemKind kind=baseKind(h.heldKind);
    auto& a=spawn(player,kind,true,&d.positions[slot]);a.scattered=true;a.target=-1;a.guidance={};
    // Preserve the held model's current facing, including a triple shell's
    // independent spin phase, when damage turns it into a dropped object.
    a.up=d.ups[slot];a.yaw=std::atan2(d.forwards[slot].x,d.forwards[slot].z);
    if(singleHeld(h.heldKind)&&spinningShell(kind))a.yaw+=shellVisualSpin(frame_);
    // Shared dropped-object initializer 291f34: tangent velocity, 25-unit
    // up impulse plus clamped inherited vertical motion and radial 13*index.
    const auto& r=racers_[player];const Vec3 up=normalized(r.up,{0,1,0});
    a.velocity=tangent(d.positions[slot]-d.previous[slot],up)+up*(25+std::clamp(dot(itemMotion(r),up),0.f,1.2f))+
        normalized(d.positions[slot]-r.position,{})*(13.f*(slot+1));
    a.scale=std::max(.8f,d.scales[slot]);a.lifetime=0;a.ownerImmunity=7;
    const auto prop=itemProperties(unsigned(kind));a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
    removeHeld(player,slot,false);
}
void RaceItems::destroyHeld(unsigned player,unsigned slot){
    // A held Bob-omb that is struck detonates where it trails.
    if(hud_[player].heldKind!=ItemKind::Bobomb){
        events_.push_back({ItemEffect::Break,uint8_t(player),uint16_t(hud_[player].heldKind),deployments_[player].positions[slot],-1,deployments_[player].scales[slot]});
        sound(ItemSoundCue::Break,player,baseKind(hud_[player].heldKind),deployments_[player].positions[slot]);
        removeHeld(player,slot,true);return;
    }
    const Vec3 at=deployments_[player].positions[slot],up=deployments_[player].ups[slot];
    const float scale=std::max(.8f,deployments_[player].scales[slot]);
    removeHeld(player,slot,false);
    auto& a=spawn(player,ItemKind::Bobomb,true,&at);a.velocity={};a.scale=scale;a.up=up;
    a.terrainRadius=itemProperties(unsigned(ItemKind::Bobomb)).terrain*scale;explode(a);
}
void RaceItems::removeHeld(unsigned player,unsigned slot,bool breakChain){
    auto& h=hud_[player];if(!(h.heldMask&(1u<<slot)))return;
    // Retail 27ef34: a destroyed banana releases every link behind it.
    if(breakChain&&h.heldKind==ItemKind::TripleBanana){
        for(unsigned n=slot+1;n<3;++n)if(h.heldMask&(1u<<n)){
            spawn(player,ItemKind::Banana,true,&deployments_[player].positions[n]);
            h.heldMask&=uint8_t(~(1u<<n));--h.heldCharges;
        }
    }
    h.heldMask&=uint8_t(~(1u<<slot));
    if(--h.heldCharges==0){h.deployed=false;h.heldKind=ItemKind::None;}
}
void RaceItems::releaseHeld(unsigned player,bool backwards){
    auto& h=hud_[player];if(!h.deployed)return;
    const ItemKind kind=baseKind(h.heldKind);
    for(int slot=2;slot>=0;--slot)if(h.heldMask&(1u<<slot)){
        auto& a=spawn(player,kind,backwards,&deployments_[player].positions[slot]);
        const auto prop=itemProperties(unsigned(kind));
        // Item::Player::OnObjTetherEnd 8078f91c -> fn_1_2C23E4: the holder's put / throw voice.
        sound(ItemSoundCue::Release,player,kind,a.position,!backwards);
        a.scale=std::max(.8f,deployments_[player].scales[slot]);
        a.radius=prop.releasedContact*a.scale;a.terrainRadius=prop.terrain*a.scale;
        removeHeld(player,unsigned(slot),false);break;
    }
}

// ---- Item use -------------------------------------------------------------------------

void RaceItems::useLightning(unsigned player){
    // RMCU activateLightning 0x807a91c0: every other racer, through the kart's
    // applyLightning (rank table) and applyLightningEffect gates. Respawning and
    // cannon racers are skipped first (fn_288374).
    for(unsigned i=0;i<count_;++i){
        if(i==player||sameRaceTeam(racers_[player].team,racers_[i].team))continue;const auto& r=racers_[i];
        if(r.damageProtected||r.unavailable||r.inCannon||r.finished)continue;
        // fn_1_288374 restarts the struck racer's bolt (KartEffect +0x4c) whatever the
        // kart's own immunity decides.
        events_.push_back({ItemEffect::Bolt,uint8_t(i),0});
        shrink(i,lightningShrinkDuration(rank(i),count_),false,false);
        sound(ItemSoundCue::Struck,i);
    }
    // ApplyToPlayers807a9298 broadcasts object type6 even if every kart
    // resisted the strike. This has no physical item or collision depth.
    karts_->globalCourseItem(6);
    sound(ItemSoundCue::Lightning,player);
    itemSpawnTimers_[0]=1800; // ItemSlotData_resetLightningTimer
    // Every local screen flashes (0x8054a558 per screen), the user's included.
    events_.push_back({ItemEffect::LightningFlash,uint8_t(player)});
}
void RaceItems::use(unsigned player,bool backwards){
    auto& h=hud_[player];
    if(heldDropTicks_[player])return;
    if(h.deployed){
        if(!singleHeld(h.heldKind))releaseHeld(player,backwards);
        return;
    }
    if(h.item==ItemKind::None||!h.charges||h.roulette)return;
    const ItemKind kind=h.item;
    if(kind==ItemKind::Blooper)itemSpawnTimers_[2]=900;
    if(kind==ItemKind::Pow)itemSpawnTimers_[3]=1200;
    // ItemHolderPlayerb4_use (USA 27c8d0) creates the held group first;
    // throwing an individual object is a later input, not the deployment.
    if(singleHeld(kind)||kind==ItemKind::TripleGreenShell||kind==ItemKind::TripleRedShell||kind==ItemKind::TripleBanana){
        h.deployed=true;h.heldKind=kind;h.heldCharges=h.charges;h.heldMask=uint8_t((1u<<h.charges)-1);
        h.item=ItemKind::None;h.charges=0;deployments_[player]={};
        auto& state=deployments_[player];
        if(trailing(kind)){state.trailing.init(trailingPose(racers_[player]));state.positions=state.trailing.positions;}
        else state.positions.fill(racers_[player].position);
        // TripleTrailPrepare 8078a534 / SpinPrepare 8078b664: SE_ITM_EQUIP for the triples.
        if(!singleHeld(kind))sound(ItemSoundCue::Equip,player,kind,racers_[player].position);
        state.previous=state.positions;state.ups.fill(racers_[player].up);state.forwards.fill(racers_[player].forward);return;
    }
    switch(kind){
    case ItemKind::Mushroom:case ItemKind::TripleMushroom:
        // RMCU useMushroom 0x8079f1a8 -> KartMove::activateMushroom.
        karts_->activateMushroom(player);events_.push_back({ItemEffect::Mushroom,uint8_t(player)});h.ink=h.inkDelay=0;++mushroomUses_[player];break;
    case ItemKind::GoldenMushroom:
        // RMCU useGolden 0x8079f2bc: a 450-frame window of mushrooms every 12 frames.
        // Each use is a new press (no repeat while held).
        if(!h.golden)h.golden=GoldenDuration;
        karts_->activateMushroom(player);events_.push_back({ItemEffect::Mushroom,uint8_t(player)});h.ink=h.inkDelay=0;++mushroomUses_[player];return;
    case ItemKind::Star:
        // RMCU useStar 0x807a860c: the kart may refuse (action, Bullet); the item is spent
        // either way. An accepted Star clears Blooper ink (activateStar bit 92).
        if(karts_->activateStar(player)){h.ink=h.inkDelay=0;inkReadyFrame_[player]=0;events_.push_back({ItemEffect::Cure,uint8_t(player)});}
        break;
    case ItemKind::MegaMushroom:
        karts_->activateMega(player);events_.push_back({ItemEffect::Cure,uint8_t(player)});break;
    case ItemKind::Bullet:
        // The cloud leaves a Bullet racer (ObjKumo::Update 807d75b4: SE_ITM_KUMO_OFF).
        if(h.cloud)sound(ItemSoundCue::CloudOff,player);
        // ItemHolderPlayer_useBullet 0x807d68bc: the Bullet holds the slot (+0x78 = 15).
        h.slotLock=ItemKind::Bullet;
        karts_->setBullet(player,true);h.bullet=BulletDuration;bullets_[player]={0,0,uint8_t(bulletTargetRank(rank(player),count_))};h.cloud=h.ink=0;events_.push_back({ItemEffect::Cure,uint8_t(player)});break;
    case ItemKind::Lightning:useLightning(player);break;
    case ItemKind::Blooper:
        // The original manager selects targets after its70-frame arrival and
        //30-frame pause, using their rank then. It admits one attack at a time.
        if(frame_>=blooperBusyUntil_){
            sound(ItemSoundCue::BlooperUse,player);
            // Early LED warning only: the original manager still selects the
            // actual ink targets 100 frames later using their rank then.
            if(player==0||(rank(0)<rank(player)&&!sameRaceTeam(racers_[player].team,racers_[0].team)&&!invincible(0)&&!racers_[0].unavailable&&!racers_[0].finished))
                events_.push_back({ItemEffect::BlooperLaunch,0});
            blooperAttack_={};blooperEnd_={};blooperEndPose_={};
            blooperCaster_=player;blooperStartFrame_=frame_;
            blooperSelectionFrame_=frame_+BlooperSelectionDelay;
            blooperBusyUntil_=frame_+BlooperManagerDuration;
        }
        break;
    case ItemKind::Pow:
        // RMCU ItemHolderPlayer_usePow 0x807d9580 -> PowMgr deploy 0x807d9808: ignored while
        // another POW is running; targets everyone ranked ahead of the user, whatever their
        // state (the kart's action gates decide later); 807d98bc excludes teammates.
        if(powTimer_<1){
            powTimer_=PowDuration;powUser_=uint8_t(player);
            for(unsigned i=0;i<count_;++i)powTargets_[i]=i!=player&&rank(i)<rank(player)&&!sameRaceTeam(racers_[player].team,racers_[i].team);
        }
        break;
    case ItemKind::ThunderCloud:h.cloud=CloudDuration;karts_->setThunderCloud(player,true);sound(ItemSoundCue::CloudOn,player);break;
    default:
        spawn(player,kind,backwards);
        // ItemHolderPlayer_useBlueShell 0x807a3c00 flags the tethered Blue (+0x154), so
        // ItemHolderPlayerb4_use sets +0x78 = 7 until the next update releases it (0x80789c5c).
        if(kind==ItemKind::BlueShell)h.slotLock=ItemKind::BlueShell;
        break;
    }
    if(--h.charges==0)h.item=ItemKind::None;
}
void RaceItems::updateCloud(unsigned i){
    // RMCU ItemObjKumo update 0x807d71c0: after 30 updates a carrier with a Star, Mega or Bullet,
    // in a cannon or respawning drops the cloud harmlessly; otherwise it strikes at update 600
    // (0x80579d88: action 17 and a 612-frame shrink, through the kart's lightning gates).
    auto& h=hud_[i];const auto& r=racers_[i];
    if(++cloudAge_[i]>CloudImmunityAge&&(r.star||r.mega||r.bullet||h.bullet||r.inCannon||r.unavailable)){
        // ObjKumo::Update 807d75b4: SE_ITM_KUMO_OFF as the cloud leaves.
        h.cloud=0;karts_->setThunderCloud(i,false);sound(ItemSoundCue::CloudOff,i);return;
    }
    if(!--h.cloud){
        // The strike (0x80579d88) restarts the cloud bolt (KartEffect +0x50) before the shrink.
        events_.push_back({ItemEffect::Bolt,uint8_t(i),1});
        karts_->setThunderCloud(i,false);shrink(i,CloudShrinkDuration,false,true);sound(ItemSoundCue::CloudStrike,i);
    }
    tick(cloudTransfer_[i]);
}
void RaceItems::cloudBump(unsigned attacker,unsigned victim,const ItemRacer& target){
    // RMCU 0x8079156c -> 0x807d85a0 -> 0x807d8b5c: the carrier's cloud passes once its transfer
    // cooldown is over and before update 598, to a racer without a Star, Mega or Bullet, not in
    // a cannon and not respawning. The cooldown restarts at 45 and the timer is held to at most
    // 480, so the new carrier always gets 120 frames. (One cloud per racer here.)
    if(attacker>=count_||victim>=count_||attacker==victim||!active_)return;
    auto& from=hud_[attacker];auto& to=hud_[victim];
    if(!from.cloud||cloudTransfer_[attacker]||CloudDuration-from.cloud>=CloudLastTransfer)return;
    if(target.star||target.mega||target.bullet||to.bullet||target.inCannon||target.unavailable||to.cloud)return;
    to.cloud=std::max<uint16_t>(from.cloud,CloudDuration-CloudTransferTimerCap);from.cloud=0;
    cloudTransfer_[victim]=CloudTransferCooldown;cloudAge_[victim]=cloudAge_[attacker];to.cloudFrom=uint8_t(attacker);
    karts_->setThunderCloud(attacker,false);karts_->setThunderCloud(victim,true);
    sound(ItemSoundCue::CloudAttach,victim,uint8_t(attacker));
}
void RaceItems::updatePow(){
    // RMCU PowMgr update 0x807d9d4c: stomps when the timer reaches 143, 95 and 45.
    if(powTimer_>0){
        --powTimer_;
        if(powTimer_==PowStompTimers[0])powStomp(1);
        else if(powTimer_==PowStompTimers[1])powStomp(2);
        else if(powTimer_==PowStompTimers[2])powStomp(3);
    }
    // The block shows on the screens of the user and its targets (PowScreen, 0x807d9940).
    for(unsigned i=0;i<count_;++i)hud_[i].pow=uint16_t(powTimer_>0&&(i==powUser_||powTargets_[i])?powTimer_:0);
}
void RaceItems::powStomp(unsigned stomp){
    // RMCU 0x807d9b10: every racer's target flag is refreshed; the user and the targets get
    // the stomp's camera shake; targets are lifted at stomps 1 and 2; stomp 3 opens the
    // window in which a grounded target starts the POW action (KartMove 0x8057a844).
    for(unsigned i=0;i<count_;++i){
        karts_->setPowTarget(i,powTargets_[i]);
        if(i==powUser_||powTargets_[i])events_.push_back({ItemEffect::PowStomp,uint8_t(i),uint16_t(stomp),{},int8_t(powTargets_[i]?1:0)});
        if(powTargets_[i]&&stomp<3)karts_->powHop(i,stomp==1?PowStompLift[0]:PowStompLift[1]);
    }
    sound(ItemSoundCue::Pow,powUser_,uint8_t(stomp));
    if(stomp==3){
        karts_->openPowWindow();
        // PowMgr807d9e08 notifies course objects once at its third stomp.
        karts_->globalCourseItem(11);
    }
}
void RaceItems::updateHolder(unsigned i){
    auto& h=hud_[i];
    // The tethered Blue Shell leaves on the update after its use and frees the slot.
    if(h.slotLock==ItemKind::BlueShell)h.slotLock=ItemKind::None;
    tick(h.ink);
    if(racers_[i].unavailable||racers_[i].finished||invincible(i)||racers_[i].inkCleaningBoost){
        if(frame_<blooperAttack_[i])blooperAttack_[i]=0;
        if(blooperAttack_[i]&&!blooperEnd_[i]&&frame_>=blooperAttack_[i]&&frame_<blooperAttack_[i]+BlooperAttackLead){
            blooperEnd_[i]=frame_;blooperEndPose_[i]=frame_-blooperAttack_[i];
            // GessoMgr stays busy until every screen's active clip ends.
            blooperBusyUntil_=std::max(blooperBusyUntil_,frame_+BlooperEndFrames);
        }
        h.ink=h.inkDelay=0;inkReadyFrame_[i]=0;
    }
    else if(h.inkDelay){
        // GessoMgr::Update 807d62fc: the attack on this racer's screen, 148 frames before the ink.
        if(frame_+BlooperAttackLead==inkReadyFrame_[i])sound(ItemSoundCue::BlooperAttack,i);
        if(frame_>=inkReadyFrame_[i]){h.inkDelay=0;inkReadyFrame_[i]=0;h.ink=BlooperInkDuration;events_.push_back({ItemEffect::Ink,uint8_t(i),BlooperInkDuration});sound(ItemSoundCue::Ink,i);}
        else h.inkDelay=uint16_t(inkReadyFrame_[i]-frame_);
    }
    if(h.bullet){
        auto& bullet=bullets_[i];
        // Retail KartKiller a5c14: 180f minimum, target-rank or 480f
        // normal duration, then 70f end phase. ITPT no-end sections and
        // cannons defer both transitions; this is why fixed timers fail.
        const bool noEnd=(bulletNoEnd_&(1u<<i))!=0;
        // Retail a583c: four-frame wall-contact memory, cancel after 120
        // consecutive obstructed frames rather than reversing the Bullet.
        if(racers_[i].wallCollision)bullet.wallMemory=4;
        if(bullet.wallMemory){if(bullet.wallTicks<120)++bullet.wallTicks;--bullet.wallMemory;}
        else bullet.wallTicks=0;
        if((racers_[i].unavailable&&!racers_[i].inCannon)||racers_[i].finished||bullet.wallTicks>=120)h.bullet=0;
        else if(bullet.ending){
            if(bullet.ending<=BulletEndDuration+1)++bullet.ending;
            if(bullet.ending>BulletEndDuration+1&&!noEnd)h.bullet=0;
        }else{
            bullet.elapsed=std::min<unsigned>(BulletDuration+1,bullet.elapsed+1);
            const bool passed=rank(i)<=bullet.targetRank;
            if((bullet.elapsed>BulletDuration||(bullet.elapsed>BulletMinimumDuration&&passed))&&!racers_[i].inCannon&&!noEnd)bullet.ending=1;
        }
        if(h.bullet)h.bullet=uint16_t(std::max(1u,BulletDuration-std::min<unsigned>(bullet.elapsed,BulletDuration)));
        else{bullet={};karts_->setBullet(i,false);if(h.slotLock==ItemKind::Bullet)h.slotLock=ItemKind::None;}
    }
    if(cloudSlot_[i]&&!--cloudSlot_[i]&&h.item==ItemKind::ThunderCloud&&!h.charges)h.item=ItemKind::None;
    if(h.cloud)updateCloud(i);
    if(h.golden&&!--h.golden){h.item=ItemKind::None;h.charges=0;}
    // Respawning (fn_1_288494) clears the inventory, the roulette and the slot lock.
    if(racers_[i].unavailable&&!racers_[i].inCannon){applyLoss(i,{true,true,false});h.slotLock=ItemKind::None;}
    auto& drop=inventoryDrops_[i];
    if(drop.ticks&&--drop.ticks&&(drop.ticks==7||drop.ticks==5||drop.ticks==3)&&drop.count){
        const auto velocity=drop.velocity[--drop.count];
        auto& a=spawn(i,drop.kind,true,&racers_[i].position);a.scattered=true;a.guidance={};a.target=-1;
        const Vec3 up=normalized(racers_[i].up,{0,1,0}),inherited=itemMotion(racers_[i]);
        a.velocity=tangent(velocity+inherited*.7f,up)+up*((drop.kind==ItemKind::Star?35.f:25.f)+std::clamp(dot(inherited,up),0.f,1.2f));
        a.scale=.05f;a.radius=itemProperties(unsigned(a.kind)).releasedContact*a.scale;a.terrainRadius=itemProperties(unsigned(a.kind)).terrain*a.scale;
        // RMCU ItemHolder_spawnDirect: ejected items spare their owner for 7 frames.
        a.lifetime=0;a.ownerImmunity=7;if(!drop.count)drop={};
    }
    if(heldDropTicks_[i]){
        --heldDropTicks_[i];
        if((heldDropTicks_[i]==8||heldDropTicks_[i]==4||heldDropTicks_[i]==0)&&h.heldCharges)
            for(int slot=2;slot>=0;--slot)if(h.heldMask&(1u<<slot)){scatterHeld(i,unsigned(slot));break;}
        if(!h.heldCharges)heldDropTicks_[i]=0;
    }
    const ItemInput input=inputs_[i];const auto& r=racers_[i];
    // RMCU ItemHolderPlayer_update 0x8078e91c: input counts only on a frame the racer was free
    // before and still is (lock flag 0x400); the frame a lock ends is skipped as well. A press
    // edge or a release then also needs no action and no start burnout.
    bool canAct=false;
    if(itemLock_[i]){if(!(r.itemLocked||r.unavailable)&&!r.afterCannon)itemLock_[i]=false;}
    else if(r.itemLocked||r.unavailable)itemLock_[i]=true;
    else canAct=!r.finished;
    const bool canPress=canAct&&!r.controlLocked;
    const bool press=canPress&&input.use&&!held_[i];
    const bool wasRoulette=h.roulette;
    if(h.roulette){
        switch(roulettes_[i].advance(press,h.rouletteIcon==rouletteChoice_[i])){
        case ItemRoulette::Step::None:break;
        case ItemRoulette::Step::NextIcon:h.rouletteIcon=nextRouletteIcon(i,h.rouletteIcon);break;
        case ItemRoulette::Step::PrizeIcon:h.rouletteIcon=rouletteChoice_[i];break;
        case ItemRoulette::Step::Finished:
            // Item::Player::Update 8078f01c / 8078f140: the decide sound, a miss for the cloud.
            sound(ItemSoundCue::Decide,i,rouletteChoice_[i],racers_[i].position,rouletteChoice_[i]==ItemKind::ThunderCloud);
            h.roulette=0;give(i,rouletteChoice_[i]);rouletteChoice_[i]=ItemKind::None;break;
        }
    }
    if(canPress){
        const auto kind=h.deployed?h.heldKind:h.item;
        // Neutral drops traps; stick-up explicitly throws them forward.
        const bool backwards=kind==ItemKind::Banana||kind==ItemKind::TripleBanana||kind==ItemKind::FakeBox?!input.forwards:input.backwards;
        // A dragged single item is thrown on the first such frame the button is up (flag 0x100
        // re-armed by the trail update each frame), so a release during a spin or the start
        // burnout throws once it ends. Inventory use needs the press edge (0x2000).
        if(h.deployed&&!heldDropTicks_[i]&&singleHeld(h.heldKind)&&!input.use)releaseHeld(i,backwards);
        else if((h.deployed||!wasRoulette)&&press)use(i,backwards);
    }
    held_[i]=input.use;
}

// ---- Step -----------------------------------------------------------------------------

void RaceItems::beforeStep(const ItemRacer* racers,const ItemInput* inputs,unsigned count,bool raceActive,uint16_t bulletNoEndPlayers){
    count_=std::min(count,MaxRacers);events_.clear();dropStepSounds();active_=raceActive;bulletNoEnd_=bulletNoEndPlayers;contactsDone_=false;stepOpen_=true;
    for(auto& h:hud_)h.cloudFrom=MaxRacers;
    for(unsigned i=0;i<count_;++i){racers_[i]=racers[i];inputs_[i]=inputs?inputs[i]:ItemInput{};}
    karts_->bind(racers_.data(),count_);
    for(unsigned i=0;i<count_;++i)karts_->overlay(i,racers_[i]);
    trackShellRacers();
    for(unsigned i=0;i<MaxActors;++i)freeAtStepStart_[i]=!actors_[i].active;
    // Course objects animate during the starting countdown as on Wii. Their
    // presentation clock is independent of item roulette/RNG and draw rate.
    for(auto& box:boxes_)box.phase=(box.phase+1)%181;
    stepMovers();
    if(!active_)return;
    for(auto& box:boxes_){
        if(box.respawn){tick(box.respawn);if(!box.respawn){box.active=true;box.appearance=0;}}
        if(box.active&&box.appearance<=ItemBoxAppearanceFrames)++box.appearance;
    }
}
void RaceItems::afterStep(const ItemRacer* racers,unsigned count,ItemWorldQuery query){
    count_=std::min(count,MaxRacers);
    // Events span beforeStep, the kart pass and afterStep; a lone afterStep starts afresh.
    if(!stepOpen_){events_.clear();dropStepSounds();}
    stepOpen_=false;
    // A host model advances where the native kart step ran, before the item phase.
    if(active_)karts_->advance();
    for(unsigned i=0;i<count_;++i)racers_[i]=racers[i];
    karts_->bind(racers_.data(),count_);
    for(unsigned i=0;i<count_;++i)karts_->overlay(i,racers_[i]);
    if(!active_){for(unsigned i=0;i<count_;++i){held_[i]=inputs_[i].use;syncKart(i);}stepSounds_=sounds_.size();return;}
    trackShellRacers();
    // Host models resolve the kart pass here; the native kart did it inside its pass. Racer
    // contact stands in for the native bump (both orders, as the kart pass calls it).
    if(!karts_->contactsInKartPass()&&!contactsDone_){
        for(unsigned i=0;i<count_;++i)for(unsigned j=i+1;j<count_;++j){
            if(racers_[i].unavailable||racers_[j].unavailable)continue;
            const Vec3 d=racers_[i].position-racers_[j].position;const float reach=racers_[i].radius+racers_[j].radius;
            if(dot(d,d)<reach*reach){cloudBump(i,j,racers_[j]);cloudBump(j,i,racers_[i]);}
        }
        for(unsigned i=0;i<count_;++i)kartContacts(i);
    }
    takeKartLosses();
    // RMCU ItemHolder_update 0x80790844: spawn timers, the Blooper and POW managers, every
    // item object (by type), then each player's holder, which positions its tethered items.
    ++frame_;
    for(auto& timer:itemSpawnTimers_)tick(timer);
    if(blooperCaster_<count_&&frame_>=blooperSelectionFrame_){
        for(unsigned i=0;i<count_;++i)if(i!=blooperCaster_&&rank(i)<rank(blooperCaster_)&&!sameRaceTeam(racers_[blooperCaster_].team,racers_[i].team)&&!invincible(i)&&!racers_[i].unavailable&&!racers_[i].finished){
            // Absolute due frames prevent player update order from changing
            // the source's three-frame per-player staggering.
            inkReadyFrame_[i]=blooperStartFrame_+blooperHitDelay(i);
            blooperAttack_[i]=inkReadyFrame_[i]-BlooperAttackLead;
            hud_[i].inkDelay=uint16_t(inkReadyFrame_[i]-frame_);
        }
        blooperCaster_=MaxRacers;
    }
    updatePow();
    updateActors(query);
    for(unsigned i=0;i<count_;++i){updateHolder(i);updateHeldItems(i,query);resolveHeldCourseObjects(i);}
    // Re-read the kart's power-up state after this step's item effects (host models overlay it
    // on the racers the caller supplied).
    for(unsigned i=0;i<count_;++i){
        ItemRacer fresh=racers[i];karts_->overlay(i,fresh);
        racers_[i].star=fresh.star;racers_[i].mega=fresh.mega;racers_[i].shocked=fresh.shocked;
        syncKart(i);
    }
    // Requested colour fade: 15 ticks in, 29 ticks out, including early cures.
    for(unsigned i=0;i<count_;++i){auto& h=hud_[i];
        h.inkShade=uint8_t(h.ink&&!h.inkDelay?std::min(255,int(h.inkShade)+17):std::max(0,int(h.inkShade)-9));}
    stepSounds_=sounds_.size();
}
}
