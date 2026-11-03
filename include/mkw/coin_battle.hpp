#pragma once

#include <mkw/battle_rules.hpp>
#include <mkw/battle_ranking.hpp>
#include <mkw/coin_drop_packet.hpp>
#include <mkw/math.hpp>
#include <mkw/race_items.hpp>
#include <mkw/race_teams.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace mkw {

// CoinUnit::calcMotion (RMCU 80868040..80868688). Position is the
// collision/body origin; the authored model offset is presentation only.
struct CoinBody {
    static constexpr float Radius = 150.f;
    static constexpr float NoWater = -std::numeric_limits<float>::infinity();
    Vec3 position{}, previous{}, velocity{};
    Vec3 floorNormal{0,1,0};
    const void* floorObject=nullptr;
    float boundCentreY=0,boundObjectY=0;
    enum class Contact { Air, Lost, Hazard, Floor, Wall };

    // 80865f70: Chain Chomp Wheel alone adds50 to the collision origin.
    static constexpr float visualOffset(unsigned course){return course==34?50.f:0.f;}
    Vec3 visualPosition(unsigned course)const{return position+Vec3{0,visualOffset(course),0};}
    bool belowWater(unsigned course,float height)const{return visualPosition(course).y<height-Radius;}

    static CoinBody launch(Vec3 at,Vec3 forward,unsigned index,unsigned count){
        // CoinManager 80873098..808731ac spaces directions across 2pi;
        // CoinUnit 808679b4 adds150+100 height, tilts up .8 and scales80.
        return launchYaw(at,std::atan2(forward.x,forward.z),index,count);
    }
    static CoinBody launchPacket(const CoinDropPacket& packet,unsigned index,unsigned count){
        return launchYaw(packet.position,packet.yaw,index,count);
    }
    static CoinBody launchYaw(Vec3 at,float initialYaw,unsigned index,unsigned count){
        constexpr float tau=6.2831853071795864769f;
        const float step=tau/float(std::max(1u,count));
        const float yaw=initialYaw+step*float(index);
        const float horizontal=80.f*std::cos(.8f);
        CoinBody body;body.previous=body.position=at+Vec3{0,250,0};
        body.velocity={std::sin(yaw)*horizontal,80.f*std::sin(.8f),std::cos(yaw)*horizontal};
        return body;
    }

    Contact step(ItemWorldQuery query, bool underwater = false, unsigned course = 0,float waterHeight=NoWater,
                 ItemTimedWorldQuery timedQuery=nullptr,unsigned timeOffset=0) {
        previous = position;
        // Keep the original positive velocity term and interpolation order:
        // lerp(v, v + (gravity + rate*v), .99), not a guessed friction model.
        const Vec3 acceleration = Vec3{0,-3.4f,0} + velocity*((underwater||belowWater(course,waterHeight))?.15f:.01f);
        Vec3 next = velocity + acceleration*.99f;
        const float speed = length(next);
        if (speed > 200.f) next = next*(200.f/speed);
        position += next;
        const auto surface = timedQuery ? timedQuery(previous,position,Radius,timeOffset) :
            query ? query(previous,position,Radius) : ItemSurface{};
        const bool floor = (surface.floorTypes&0x20e80fffu) != 0;
        const bool wall = (surface.floorTypes&0xd010f000u) != 0;
        // Retail reflects against the combined push vector, then applies the
        // original floor/wall coefficient. Normals are a fallback for exact
        // tangency, where no positive penetration push remains.
        Vec3 push = surface.correction;
        position += push+surface.floorMotion;
        floorObject=floor?surface.floorObject:nullptr;
        if(floor){floorNormal=surface.floorNormal;boundCentreY=position.y;boundObjectY=surface.floorObjectY;}
        Vec3 normal = length(push) > .00001f ? normalized(push) :
            length(surface.floorNormal)>.1f ? normalized(surface.floorNormal) :
            length(surface.wallNormal)>.1f ? normalized(surface.wallNormal) : Vec3{};
        const float into = dot(next,normal);
        if (into < 0.f) next = next-normal*(2.f*into);
        if (floor) next = next*((surface.floorTypes&0x1eu)?.4f:.7f);
        else if (wall) next = next*.7f;
        velocity = next;
        // Original kill handling precedes the ordinary floor/wall result.
        if (surface.kill || visualPosition(course).y<0.f || belowWater(course,waterHeight)) return Contact::Lost;
        if (surface.floorTypes&((1u<<9)|(1u<<10))) return Contact::Hazard;
        if (course==36 && std::hypot(position.x,position.z)<1000.f) return Contact::Hazard;
        return floor ? Contact::Floor : wall ? Contact::Wall : Contact::Air;
    }
};

// CoinManager's finite pool, original KMP placements and single ownership.
// The manager/scheduler supplies spawn waves and the native drop impulses;
// there are no synthetic 600-tick respawns, carry caps or fixed-three hits.
class CoinBattle {
public:
    static constexpr unsigned MaxRacers=12, NoOwner=12, MaxPlacements=255;
    struct Placement {
        Vec3 position{};
        uint16_t objectIndex=0;
        uint8_t kind=0;
        bool random=false;
    };
    enum class State : uint8_t { Dormant, Spawning, Available, Held, Dropped, Lost, Retiring };
    struct Coin {
        CoinBody body{};
        unsigned placement=0;
        uint8_t group=0, owner=NoOwner, requestedOwner=NoOwner;
        State state=State::Dormant;
        unsigned ticks=0;
        bool dynamic=false;
        unsigned quietTicks=0;
        unsigned appearanceTicks=0;
        // CoinManager placement+10 is independent of unit state: dormant
        // waves reserve a point; collecting/dropping frees that reservation.
        bool placementBound=true,registrationFailed=false;
        bool heldReady=false,clockedMotion=false,motionEntry=false;
        unsigned ownedAt=0,motionClock=0;
        float motionLag=0;
        unsigned transferBatch=0,pickupStartDelay=0,pickupTailDelay=0;
    };
    using Random = unsigned (*)(void*,unsigned);
    using Launch = CoinBody (*)(void*,unsigned,unsigned,unsigned);
    using Spawn = CoinBody (*)(void*,const Placement&);
    using CanRetire = bool (*)(void*,unsigned,const Coin&);
    using FollowGround = bool (*)(void*,CoinBody&);

    // Registration 8086ed74: type0; index < KMG initial -> wave0;
    // index -1 -> a reserved/random placement. Other indices are ignored.
    bool load(const void* bytes,size_t size,const BattleRules& rules,unsigned racers,
              const std::array<RaceTeam,MaxRacers>& teams,Random random=nullptr,void* context=nullptr) {
        if(!bytes||size<16||racers<2||racers>MaxRacers||!rules.maxCoins||rules.maxCoins>255) return false;
        const auto* b=static_cast<const uint8_t*>(bytes);
        auto u16=[&](size_t at){return unsigned(b[at])<<8|b[at+1];};
        auto u32=[&](size_t at){return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];};
        if(std::memcmp(b,"RKMD",4)||u32(4)>size||u32(4)<16) return false;
        size=u32(4);
        const unsigned sections=u16(8),header=u16(10);
        if(sections>64||header<16+sections*4||header>size) return false;
        size_t objects=0,end=size;
        for(unsigned i=0;i<sections;++i){
            const size_t at=header+size_t(u32(16+4*i));
            if(at>size||size-at<8) return false;
            if(!std::memcmp(b+at,"GOBJ",4)) objects=at;
        }
        if(!objects) return false;
        for(unsigned i=0;i<sections;++i){const size_t at=header+size_t(u32(16+4*i));if(at>objects)end=std::min(end,at);}
        const unsigned count=u16(objects+4);
        if(count>(end-objects-8)/60) return false;
        CoinBattle next;
        next.rules_=rules;next.racers_=racers;next.teams_=teams;next.ranking_.reset(racers);
        for(unsigned i=0;i<MaxRacers;++i)next.pickupPriority_[i]=uint8_t(i);
        auto scalar=[&](size_t at){const uint32_t value=u32(at);float f;std::memcpy(&f,&value,4);return f;};
        for(unsigned i=0;i<count;++i){
            const size_t at=objects+8+i*60;
            if((u16(at)&0x3ff)!=115) continue; // ObjFlow coin, 0x73
            const int type=int16_t(u16(at+42)),index=int16_t(u16(at+44)),kind=int16_t(u16(at+46));
            if(type!=0||(index!=-1&&(index<0||unsigned(index)>=rules.initialCoins))) continue;
            if(kind<0||kind>3||next.placements_.size()>=MaxPlacements) return false;
            Placement p{{scalar(at+4),scalar(at+8),scalar(at+12)},uint16_t(i),uint8_t(kind),index==-1};
            if(!std::isfinite(p.position.x)||!std::isfinite(p.position.y)||!std::isfinite(p.position.z)) return false;
            const unsigned placement=next.placements_.size();next.placements_.push_back(p);
            if(next.coins_.size()>=rules.maxCoins){
                const auto free=std::find_if(next.coins_.begin(),next.coins_.end(),[](const Coin& c){return c.group==255;});
                if(free==next.coins_.end()) continue;
                *free=next.coins_.back();next.coins_.pop_back();
            }
            Coin c;c.placement=placement;c.group=p.random?255:0;next.coins_.push_back(c);
        }
        if(next.coins_.empty()) return false;
        // Retail init 8086f2d8..f39c randomizes only reserved placements. A
        // collision with another reserved slot swaps; primary placements stay.
        if(random)for(unsigned i=0;i<next.coins_.size();++i){
            auto& c=next.coins_[i];if(c.group!=255)continue;
            const unsigned selected=random(context,unsigned(next.placements_.size()));
            if(selected>=next.placements_.size())return false;
            auto used=std::find_if(next.coins_.begin(),next.coins_.end(),[&](const Coin& other){return other.placement==selected;});
            if(used==next.coins_.end())c.placement=selected;
            else if(used->group==255)std::swap(c.placement,used->placement);
        }
        *this=std::move(next);
        return true;
    }

    // 8086f3b4..f468 assigns reserved slots to consecutive wave groups.
    // Caller passes KMG 54c (per-course/player count), not a guessed batch.
    void assignWaves() {
        const unsigned perWave=rules_.coinWaveSize;
        if(!perWave)return;
        unsigned group=1,inGroup=0;
        for(auto& c:coins_)if(c.group==255){
            c.group=uint8_t(group);
            if(++inGroup>=perWave){++group;inGroup=0;}
        }
    }
    //8086f4f0..55c: start with identity, then swap every racer with one
    // NextLimited(playerCount) draw. RequestGet80875f10..24 retains the
    // colliding racer with lower priority, independently of callback order.
    bool initializePickupPriority(Random random,void* context=nullptr){
        if(!random||!racers_)return false;
        auto priority=pickupPriority_;
        for(unsigned i=0;i<racers_;++i){const unsigned j=random(context,racers_);
            if(j>=racers_)return false;std::swap(priority[i],priority[j]);}
        pickupPriority_=priority;return true;
    }
    bool collectContacts(unsigned coin,uint16_t contacts){
        unsigned selected=NoOwner;
        for(unsigned i=0;i<racers_;++i)if(!finished_[i]&&(contacts&(1u<<i))&&
            (selected==NoOwner||pickupPriority_[i]<pickupPriority_[selected]))selected=i;
        return selected<racers_&&collect(coin,selected);
    }
    // RequestGet80875e90 stores +13; it does not change the current owner.
    // All requests arbitrate before the manager publishes ownership in
    // unit order (808724fc..729c4), independently of contact callback order.
    bool requestCollect(unsigned coin,unsigned racer){
        if(coin>=coins_.size()||racer>=racers_||finished_[racer])return false;
        auto& c=coins_[coin];
        if(c.state!=State::Available)return false;
        if(c.requestedOwner<racers_&&pickupPriority_[racer]>=pickupPriority_[c.requestedOwner])return false;
        c.requestedOwner=uint8_t(racer);return true;
    }
    unsigned resolveCollectRequests(){
        unsigned count=0;
        for(unsigned i=0;i<coins_.size();++i){
            const unsigned owner=coins_[i].requestedOwner;coins_[i].requestedOwner=NoOwner;
            if(owner<racers_&&collect(i,owner))++count;
        }
        return count;
    }
    // Offline participants all belong to the local manager. Native requests
    // still reject RaceManagerPlayer+38 bit1, with +1f50 retaining the
    // received finish flag. This is not damage immunity or a pickup timer.
    void finish(unsigned racer){if(racer<racers_)finished_[racer]=true;}
    void setHeldReady(unsigned unit,bool ready){if(unit<coins_.size())coins_[unit].heldReady=ready;}
    unsigned readyHeld(unsigned racer)const{
        unsigned n=0;for(const auto& c:coins_)if(c.state==State::Held&&c.owner==racer&&c.heldReady)++n;return n;
    }
    unsigned pendingEvents()const{return (eventWrite_-eventRead_)&63u;}
    //80876008/8087635c reserve selection+ceil(count/100) slots atomically.
    // Offline chunks are100; the same64-slot ring always leaves one empty.
    bool requestHit(unsigned victim,bool fall,const CoinDropPacket& packet){
        if(victim>=racers_||finished_[victim])return false;
        const unsigned count=rules_.coinsLost(readyHeld(victim),fall);
        return count&&enqueue(victim,NoOwner,count,packet);
    }
    bool requestSteal(unsigned thief,unsigned victim){
        if(thief>=racers_||victim>=racers_||finished_[thief]||finished_[victim]||
           thief==victim||sameRaceTeam(teams_[thief],teams_[victim]))return false;
        const unsigned count=std::min<unsigned>(rules_.coinStealCount,readyHeld(victim));
        return count&&enqueue(victim,thief,count,{});
    }
    // Selection precedes pickup publication. One body command per manager
    // update; subsequent hits/steals retain FIFO ordering and capture time.
    bool stepRequests(){
        if(eventRead_!=eventWrite_&&events_[(eventRead_+1)&63].selection){
            const auto command=popEvent();selectedCount_=0;
            for(unsigned i=0;i<coins_.size()&&selectedCount_<command.count;++i){auto& c=coins_[i];
                if(c.group<=activeGroup_&&c.state==State::Held&&c.owner==command.victim&&
                   c.heldReady&&c.ownedAt<=command.tick){
                    selected_[selectedCount_++]=uint8_t(i);c.requestedOwner=NoOwner;
                }
            }
        }
        resolveCollectRequests();
        if(eventRead_==eventWrite_)return false;
        const auto command=popEvent();
        const unsigned end=std::min(selectedCount_,command.first+100u);
        const unsigned batch=command.thief==NoOwner?0:++transferBatch_;
        for(unsigned k=command.first;k<end;++k){auto& c=coins_[selected_[k]];
            const unsigned oldOwner=c.owner;
            if(oldOwner<racers_&&scores_[oldOwner])--scores_[oldOwner];
            c.ticks=c.quietTicks=0;c.placementBound=false;c.registrationFailed=false;c.heldReady=false;
            if(command.thief==NoOwner){
                c.body=CoinBody::launchPacket(command.packet,k,selectedCount_);
                c.owner=NoOwner;c.state=State::Dropped;c.dynamic=true;
                c.clockedMotion=c.motionEntry=true;c.motionClock=command.tick;
            }else{
                c.owner=command.thief;c.state=State::Held;c.ownedAt=clock_;
                c.transferBatch=batch;c.pickupStartDelay=(k-command.first)*6;
                c.pickupTailDelay=(end-k-1)*6;
                ++scores_[command.thief];cues_|=uint16_t(1u<<command.thief);
            }
        }
        return true;
    }
    unsigned activate(unsigned group,Spawn spawn=nullptr,void* context=nullptr,unsigned delay=0) {
        unsigned count=0;
        for(auto& c:coins_)if(c.group==group&&c.state==State::Dormant){
            const auto& p=placements_[c.placement];
            c.body=spawn?spawn(context,p):CoinBody{p.position,p.position,{}};
            c.owner=NoOwner;c.appearanceTicks=p.kind<2?delay:0;
            c.state=c.appearanceTicks?State::Spawning:State::Available;
            c.dynamic=spawn&&p.kind>=2;c.ticks=c.quietTicks=0;++count;
            c.placementBound=true;c.registrationFailed=false;
        }
        return count;
    }
    // RMCU 80874c60..cb4: replenish by available-count threshold, not by
    // expiring each placement after a synthetic fixed number of seconds.
    unsigned stepWaves(Spawn spawn=nullptr,void* context=nullptr,unsigned delay=0) {
        unsigned available=0,last=0;
        for(const auto& c:coins_){
            if(c.state==State::Available)++available;
            if(c.group!=255)last=std::max(last,unsigned(c.group));
        }
        if(available>rules_.coinReplenishThreshold||activeGroup_>=last)return 0;
        return activate(++activeGroup_,spawn,context,delay);
    }
    bool recycle(unsigned coin,unsigned placement,Spawn spawn=nullptr,void* context=nullptr,unsigned delay=0,
                 float waterHeight=CoinBody::NoWater){
        if(coin>=coins_.size()||placement>=placements_.size())return false;
        auto& c=coins_[coin];
        if(c.state!=State::Lost||c.group>activeGroup_||!placements_[placement].random||
           placements_[placement].position.y<waterHeight)return false;
        for(const auto& other:coins_)if(other.placementBound&&other.placement==placement)return false;
        c.placement=placement;
        const auto& p=placements_[placement];c.body=spawn?spawn(context,p):CoinBody{p.position,p.position,{}};
        c.owner=NoOwner;c.appearanceTicks=p.kind<2?delay:0;
            c.state=c.appearanceTicks?State::Spawning:State::Available;
        c.dynamic=spawn&&p.kind>=2;c.ticks=c.quietTicks=0;
        c.placementBound=true;c.registrationFailed=false;
        return true;
    }
    // 80875f7c only records failure on a settled state3 with no queued work.
    // It never teleports a coin at the point where the graph rejects it.
    void failedRegistration(unsigned coin){
        if(coin>=coins_.size())return;
        auto& c=coins_[coin];
        if(c.state==State::Available&&!c.dynamic)c.registrationFailed=true;
    }
    // Offline 80875028..75550: one unit draw, at most one placement draw;
    // an event5 retirement occupies the event queue for this update. Do not
    // retry a rejected random point or recycle a later dormant wave.
    bool stepRecycling(Random random,void* randomContext,Spawn spawn=nullptr,void* spawnContext=nullptr,
                       unsigned delay=150,float waterHeight=CoinBody::NoWater,
                       CanRetire canRetire=nullptr,void* retireContext=nullptr){
        if(!random||coins_.empty()||!racers_)return false;
        const unsigned unit=random(randomContext,unsigned(coins_.size()));
        if(unit>=coins_.size())return false;
        // Original still consumes the unit draw while the command ring is
        // busy, but cannot append retirement/reuse into a pending batch.
        if(pendingEvents())return false;
        auto& c=coins_[unit];
        if(c.registrationFailed){
            c.registrationFailed=false;
            const bool fixedPoint=c.placementBound&&placements_[c.placement].kind==1;
            if(c.group<=activeGroup_&&c.owner==NoOwner&&c.state==State::Available&&!c.dynamic&&
               !fixedPoint&&canRetire&&canRetire(retireContext,unit,c)){
                // Event5 releases the point and requests state5. Its180
                // updates (8086b640..658, 808d77a0) precede state0/reuse.
                c.placementBound=false;c.state=State::Retiring;c.ticks=0;
                return true;
            }
        }
        if(c.group>activeGroup_||c.owner!=NoOwner||c.state!=State::Lost)return false;
        const unsigned placement=random(randomContext,unsigned(placements_.size()));
        return recycle(unit,placement,spawn,spawnContext,delay,waterHeight);
    }
    bool collect(unsigned coin,unsigned racer) {
        if(coin>=coins_.size()||racer>=racers_||finished_[racer])return false;
        auto& c=coins_[coin];
        if(c.state!=State::Available)return false;
        c.state=State::Held;c.owner=uint8_t(racer);c.requestedOwner=NoOwner;c.ticks=0;
        c.heldReady=false;c.ownedAt=clock_;c.clockedMotion=false;
        c.transferBatch=c.pickupStartDelay=c.pickupTailDelay=0;
        c.placementBound=false;c.registrationFailed=false;
        ++scores_[racer];cues_|=uint16_t(1u<<racer);
        return true;
    }
    // Each physical coin is retained and moves out of Held exactly once.
    // Native launch callback is required: do not invent circle positions or
    // convert N lost coins into a single value-bearing replacement object.
    unsigned hit(unsigned victim,bool fall,Launch launch,void* context=nullptr) {
        if(victim>=racers_||finished_[victim]||!launch)return 0;
        const unsigned loss=rules_.coinsLost(scores_[victim],fall);
        unsigned lost=0;
        for(auto& c:coins_)if(lost<loss&&c.state==State::Held&&c.owner==victim){
            c.body=launch(context,victim,lost,loss);c.owner=NoOwner;c.ticks=0;c.quietTicks=0;
            c.dynamic=true;c.state=State::Dropped;++lost;
            c.placementBound=false;c.registrationFailed=false;
        }
        scores_[victim]-=lost;
        return lost;
    }
    unsigned steal(unsigned thief,unsigned victim) {
        if(thief>=racers_||victim>=racers_||finished_[thief]||finished_[victim]||thief==victim||sameRaceTeam(teams_[thief],teams_[victim]))return 0;
        unsigned moved=0;
        for(auto& c:coins_)if(moved<rules_.coinStealCount&&c.state==State::Held&&c.owner==victim){c.owner=uint8_t(thief);++moved;}
        scores_[victim]-=moved;scores_[thief]+=moved;
        if(moved)cues_|=uint16_t(1u<<thief);
        return moved;
    }
    void followResting(FollowGround follow,void* context=nullptr){
        if(!follow)return;
        for(auto& c:coins_)if(!c.dynamic&&(c.state==State::Spawning||c.state==State::Available||c.state==State::Retiring))
            follow(context,c.body);
    }
    void stepDropped(ItemWorldQuery query,unsigned course,float waterHeight=CoinBody::NoWater,
                     ItemTimedWorldQuery timedQuery=nullptr) {
        auto advance=[&](Coin& c,unsigned offset){
            if(c.state==State::Retiring){
                const auto contact=c.dynamic?c.body.step(query,false,course,waterHeight,timedQuery,offset):CoinBody::Contact::Air;
                ++c.ticks;
                if(c.dynamic&&c.ticks>=2&&(contact==CoinBody::Contact::Floor||
                   contact==CoinBody::Contact::Wall||contact==CoinBody::Contact::Hazard)){
                    c.dynamic=false;c.ticks=0;
                }else if(c.ticks>=180){c.state=State::Lost;c.dynamic=false;}
                return;
            }
            if(c.state==State::Spawning){
                if(c.appearanceTicks&&!--c.appearanceTicks)c.state=State::Available;
                return;
            }
            if(c.state==State::Available&&!c.dynamic&&c.body.belowWater(course,waterHeight)){
                c.state=State::Retiring;c.ticks=0;c.registrationFailed=false;return;
            }
            if(!c.dynamic||(c.state!=State::Dropped&&c.state!=State::Available))return;
            const auto contact=c.body.step(query,false,course,waterHeight,timedQuery,offset);
            if(contact==CoinBody::Contact::Lost||contact==CoinBody::Contact::Hazard){
                c.state=State::Retiring;c.ticks=0;c.registrationFailed=false;
            }else{
                ++c.ticks;
                if(c.state==State::Dropped&&c.ticks>=30){c.state=State::Available;c.quietTicks=0;}
                if(length(c.body.velocity)<2.f)++c.quietTicks;else c.quietTicks=0;
                if(c.state==State::Available&&contact==CoinBody::Contact::Floor&&c.quietTicks>=2){c.dynamic=false;c.body.velocity={};}
            }
        };
        for(auto& c:coins_){
            if(!c.clockedMotion){advance(c,0);continue;}
            // CoinUnit8086d3e0: current unit frame advances1; lag decreases
            // .5 per manager frame (ctor80865f1c, constant808d7770).
            // State entry starts at the event's original timestamp. Motion
            // queries use previous current frame minus simulated frame.
            if(c.motionEntry){c.motionLag=std::max(0.f,float(clock_+1-c.motionClock)-.5f);c.motionEntry=false;}
            else c.motionLag=std::max(0.f,c.motionLag-.5f);
            const unsigned until=clock_+1-unsigned(c.motionLag);
            while(c.motionClock<until){
                const unsigned offset=clock_>c.motionClock?clock_-c.motionClock:0;
                advance(c,offset);++c.motionClock;
            }
        }
        ++clock_;
    }
    unsigned score(unsigned racer) const {return racer<racers_?scores_[racer]:0;}
    void updateRanks(){ranking_.step(scores_);}
    void updateReadyRanks(){
        std::array<uint16_t,MaxRacers> scores{};
        for(unsigned i=0;i<racers_;++i)scores[i]=uint16_t(readyHeld(i));
        ranking_.step(scores);
    }
    unsigned rank(unsigned racer)const{return ranking_.rank(racer);}
    unsigned durationTicks() const {return rules_.durationTicks;}
    uint16_t takeCues(){const auto value=cues_;cues_=0;return value;}
    const std::vector<Coin>& coins() const {return coins_;}
    const std::vector<Placement>& placements() const {return placements_;}

private:
    struct Command {
        bool selection=false;
        unsigned victim=NoOwner,thief=NoOwner,count=0,first=0,tick=0;
        CoinDropPacket packet{};
    };
    bool enqueue(unsigned victim,unsigned thief,unsigned count,const CoinDropPacket& packet){
        const unsigned chunks=(count+99)/100,needed=1+chunks;
        if(needed>63-pendingEvents())return false;
        auto push=[&](bool selection,unsigned first){
            eventWrite_=(eventWrite_+1)&63;
            events_[eventWrite_]={selection,victim,thief,count,first,clock_,packet};
        };
        push(true,0);for(unsigned first=0;first<count;first+=100)push(false,first);
        return true;
    }
    Command popEvent(){eventRead_=(eventRead_+1)&63;return events_[eventRead_];}
    std::array<Command,64> events_{};
    unsigned eventRead_=0,eventWrite_=0,clock_=0,selectedCount_=0,transferBatch_=0;
    std::array<uint8_t,255> selected_{};
    BattleRules rules_{};
    unsigned racers_=0;
    std::array<RaceTeam,MaxRacers> teams_{};
    std::array<uint16_t,MaxRacers> scores_{};
    std::array<bool,MaxRacers> finished_{};
    std::vector<Placement> placements_;
    std::vector<Coin> coins_;
    uint16_t cues_=0;
    unsigned activeGroup_=0;
    BattleRanking ranking_;
    std::array<uint8_t,MaxRacers> pickupPriority_{};
};
} // namespace mkw
