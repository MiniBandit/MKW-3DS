#pragma once
#include <mkw/math.hpp>
#include <mkw/race_teams.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
// BalloonManager80865454/659c4/65ba0/65cac and BalloonUnit state1..4.
// Logical owner/slot changes immediately; unit state commands retain their
// authored due tick. A stolen balloon retains its original colour/model.
class BalloonPresentation {
public:
    static constexpr unsigned MaxRacers=12,MaxBalloons=5,MaxUnits=120,Invalid=255;
    enum class State:uint8_t {Hidden,Growing,Attached,Moving,Released};
    struct Command {unsigned tick=0,owner=Invalid,slot=0;State state=State::Hidden;bool audible=true,pop=true;};
    // Native16-entry state-event ring. Root attachment redirects pending
    // links; ordinary events require the authored tail-state transition.
    struct Sequence {
        static constexpr unsigned NoLink=16,Immediate=0xffffffffu;
        struct Node {Command command{};unsigned link=NoLink;};
        std::array<Node,16> nodes{};unsigned head=0,tail=0;bool done=true;
        bool empty()const{return head==tail;}
        bool push(Command command,bool root=false){
            const unsigned next=(tail+1)%nodes.size();if(next==head)return false;
            constexpr bool Allowed[5][5]={{0,1,0,0,0},{0,0,1,0,0},{0,0,0,1,1},{0,0,1,1,1},{1,0,0,0,0}};
            if(!root&&!Allowed[unsigned(nodes[tail].command.state)][unsigned(command.state)])return false;
            nodes[next]={command,NoLink};tail=next;
            if(root)for(unsigned i=head;i!=tail;i=(i+1)%nodes.size())nodes[i].link=tail;
            return true;
        }
        bool take(unsigned tick,Command& command){
            const unsigned link=nodes[head].link;
            if((empty()||!done)&&link==NoLink)return false;
            const unsigned next=link<NoLink?link:(head+1)%nodes.size();const auto& c=nodes[next].command;
            if(c.tick!=Immediate&&c.tick>tick)return false;
            head=next;done=false;command=c;return true;
        }
    };
    enum class Event {Enter,Calculate,Pop};
    struct Unit {
        unsigned color=0,owner=Invalid,slot=0,visualOwner=Invalid,visualSlot=0,age=0;
        State state=State::Hidden;float scale=.1f;
        Sequence sequence;
        Vec3 knot{},direction{0,1,0};bool posed=false,entered=true,releasePop=true;
        unsigned calcTick=0;float lag=0;
    };
    void clear(){
        racers_=tick_=0;counts_.fill(0);teams_.fill(RaceTeam::None);
        for(auto& slots:slots_)slots.fill(Invalid);
        for(auto& unit:units_)unit=Unit{};
    }
    void reset(unsigned racers,const std::array<RaceTeam,MaxRacers>& teams){
        clear();racers_=std::min(racers,MaxRacers);teams_=teams;
        for(auto& slots:slots_)slots.fill(Invalid);
        for(unsigned i=0;i<size();++i)units_[i].color=i/(racers_*MaxBalloons);
        // Offline normal Battle initial attach starts at manager frame120,
        // then three inflated balloons at20-tick intervals.
        for(unsigned i=0;i<racers_;++i)add(i,3,120,20);
    }
    unsigned size()const{return racers_*MaxBalloons*2;}
    unsigned tick()const{return tick_;}
    unsigned count(unsigned racer)const{return racer<racers_?counts_[racer]:0;}
    const Unit& unit(unsigned index)const{return units_[index];}
    Unit& unit(unsigned index){return units_[index];}
    unsigned slot(unsigned racer,unsigned index)const{return racer<racers_&&index<counts_[racer]?slots_[racer][index]:Invalid;}
    bool lose(unsigned racer,bool audible=true){
        if(racer>=racers_||!counts_[racer])return false;
        const unsigned index=slots_[racer][--counts_[racer]];slots_[racer][counts_[racer]]=Invalid;
        auto& u=units_[index];u.owner=Invalid;
        push(u,{tick_,Invalid,0,State::Released,audible});return true;
    }
    bool steal(unsigned thief,unsigned victim){
        if(thief>=racers_||victim>=racers_||thief==victim||!counts_[victim]||!counts_[thief]||
           sameRaceTeam(teams_[thief],teams_[victim]))return false;
        if(counts_[thief]==MaxBalloons)return lose(victim);
        const unsigned index=slots_[victim][--counts_[victim]];slots_[victim][counts_[victim]]=Invalid;
        auto& u=units_[index];u.owner=thief;u.slot=counts_[thief];slots_[thief][counts_[thief]++]=index;
        push(u,{tick_+10,thief,u.slot,State::Moving,true});return true;
    }
    void replenish(unsigned racer){
        if(racer>=racers_)return;
        // Original refill first detaches all manager slots silently, then
        // schedules a fresh team-coloured group at0/20/40.
        while(counts_[racer]){const unsigned index=slots_[racer][--counts_[racer]];
            slots_[racer][counts_[racer]]=Invalid;auto& u=units_[index];u.owner=Invalid;
            push(u,{tick_,Invalid,0,State::Released,false});}
        add(racer,3,0,20);
    }
    void step(){step([](unsigned,Event,const Command&){});}
    template<class Callback> void step(Callback callback){
        const unsigned now=tick_+1;
        for(unsigned i=0;i<size();++i){auto& u=units_[i];
            //8087ebd0: retain each state's calculation clock, repay late
            // events by .1 per frame, and run all due enter/calc callbacks.
            u.lag=std::max(0.f,u.lag-.1f);
            while(u.calcTick<now-unsigned(u.lag)){
                Command c;
                if(u.sequence.take(now,c)){
                    u.entered=false;
                    if(c.tick!=Sequence::Immediate)u.calcTick=c.tick;
                    u.lag=std::max(0.f,float(now-u.calcTick)-.1f);
                    continue;
                }
                c=u.sequence.nodes[u.sequence.head].command;
                if(!u.entered){
                    u.state=c.state;u.visualOwner=c.owner;u.visualSlot=c.slot;u.age=0;
                    if(c.state==State::Growing){u.scale=.1f;u.posed=false;}
                    if(c.state==State::Attached||c.state==State::Moving){u.scale=4.f;u.sequence.done=true;}
                    if(c.state==State::Hidden){u.scale=.1f;u.posed=false;u.sequence.done=true;}
                    if(c.state==State::Released){u.scale=4.f;u.releasePop=c.pop;}
                    callback(i,Event::Enter,c);u.entered=true;
                    if(c.state==State::Released&&!c.audible){
                        if(c.pop)callback(i,Event::Pop,c);
                        finishRelease(u);
                    }
                    continue;
                }
                if(u.state==State::Growing)u.scale=.1f+(4.f-.1f)*float(u.age)/40.f;
                callback(i,Event::Calculate,c);
                if(u.state==State::Growing){if(++u.age>=40)u.sequence.done=true;}
                else if(u.state==State::Released&&!u.sequence.done){
                    if(++u.age>=90){if(u.releasePop)callback(i,Event::Pop,c);finishRelease(u);}
                }
                ++u.calcTick;
            }
        }
        tick_=now;
    }

private:
    static void finishRelease(Unit& u){u.sequence.done=true;
        if(u.sequence.empty())push(u,{Sequence::Immediate,Invalid,0,State::Hidden,false,false});}
    static void push(Unit& u,Command command,bool root=false){u.sequence.push(command,root);}
    void add(unsigned owner,unsigned amount,unsigned delay,unsigned spacing){
        const unsigned color=teams_[owner]==RaceTeam::Blue?1:0;
        amount=std::min(amount,MaxBalloons-counts_[owner]);
        // Original first prefers unowned idle units, then any unowned unit
        // of the requested colour. Detached units can be reused immediately.
        std::array<unsigned,5> selected{};unsigned n=0;
        for(unsigned pass=0;pass<2&&n<amount;++pass)for(unsigned i=0;i<size()&&n<amount;++i){
            auto& u=units_[i];if(u.color!=color||u.owner!=Invalid||(pass==0&&u.state!=State::Hidden))continue;
            if(std::find(selected.begin(),selected.begin()+n,i)!=selected.begin()+n)continue;selected[n++]=i;
        }
        for(unsigned i=0;i<n;++i){auto& u=units_[selected[i]];u.owner=owner;u.slot=counts_[owner];
            slots_[owner][counts_[owner]++]=selected[i];
            // A newly allocated unit starts a fresh attachment sequence.
            // Normal pool capacity prevents queue pressure under legal hits.
            push(u,{tick_+delay+i*spacing,owner,u.slot,State::Growing,true},true);
            push(u,{Sequence::Immediate,owner,u.slot,State::Attached,true});}
    }
    unsigned racers_=0,tick_=0;
    std::array<Unit,MaxUnits> units_{};
    std::array<unsigned,MaxRacers> counts_{};
    std::array<std::array<unsigned,MaxBalloons>,MaxRacers> slots_{};
    std::array<RaceTeam,MaxRacers> teams_{};
};
// Native rest/initial pose from8087b5b4/8087b788/8087ba84. The vehicle
// adapter supplies body-pose axes and its authored Kart/BikeDisp offsets.
struct BalloonAttachment {
    Vec3 position{},right{1,0,0},up{0,1,0},front{0,0,1};
    Vec3 scale{1,1,1};float megaScale=1.7f,height=0,rear=0;
};
inline Vec3 balloonAnchor(const BalloonAttachment& p){
    Vec3 local{0,p.height,p.rear};const Vec3 scale=p.scale*(1.7f/p.megaScale);
    if(scale.x>1&&scale.y>1&&scale.z>1){local.x*=scale.x;local.y*=scale.y;local.z*=scale.z;}
    local.y-=160.f;
    //8058a4c0 returns KartPhysics+e4, the Z/front axis (y/up is+d8).
    return p.position+p.right*local.x+p.up*local.y+p.front*local.z+Vec3{0,std::max(0.f,p.front.y)*100.f,0};
}
inline Vec3 balloonFan(const BalloonAttachment& p,unsigned index,unsigned count){
    // Original rotates unit-up around the vehicle's local Z by38 degrees
    // per slot, centred on the currently assigned balloon count.
    const float angle=(float(index)-.5f*(float(count)-1.f))*38.f*.01745329251994329577f;
    Vec3 direction=p.right*(-std::sin(angle))+p.up*std::cos(angle);
    Vec3 axis=cross(Vec3{0,1,0},p.up);const float axisLength=length(axis);
    if(dot(axis,axis)>1.1920928955078125e-7f){
        axis=axis/axisLength;const float tilt=-std::atan2(std::sqrt(p.up.x*p.up.x+p.up.z*p.up.z),p.up.y);
        const float c=std::cos(tilt),s=std::sin(tilt);
        direction=direction*c+cross(axis,direction)*s+axis*(dot(axis,direction)*(1-c));
    }
    return direction;
}
}
