#pragma once
#include <mkw/math.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/cpu_speed.hpp>
#include <mkw/cpu_ai.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kinoko::System { class RaceConfig; }
namespace mkw {
// KMP ENPT/ENPH data with bounded parsing. The route is Nintendo-authored;
// pursuit steering and recovery are adapted; the speed kernel is retail-derived.
class EnemyRoute {
public:
    static constexpr unsigned Invalid=0xffff;
    // ENPT: setting1 u16, setting2 u8 (drift: 1 end, 2 forbid, 3 force), setting3 u8 flags.
    struct Point { Vec3 position; float width=0; uint16_t setting1=0,setting2=0; uint8_t driftSetting=0,flags=0; std::array<unsigned,12> next{}; unsigned nextCount=0; bool requiresBoost=false; };
    bool load(const void* bytes,size_t size,bool itemPath=false,bool battle=false);
    void clear(){points_.clear();itemPath_=false;}
    unsigned size() const{return unsigned(points_.size());}
    const Point& point(unsigned index) const{return points_[index];}
    unsigned successor(unsigned index,unsigned racer) const;
    unsigned nearest(Vec3 position,Vec3 forward,unsigned racer) const;
    Vec3 target(unsigned segment,unsigned racer,Vec3 position,float lookahead,bool horizontalRecovery=false) const;
private:
    std::vector<Point> points_;
    bool itemPath_=false;
};

class RaceGrid {
public:
    static constexpr unsigned Capacity=12;
    // Call within the RaceConfig init callback. Count=1 keeps a solo race.
    void configure(Kinoko::System::RaceConfig& config,unsigned localGarageChoice,unsigned count=Capacity,const std::array<unsigned,Capacity>* roster=nullptr);
    // Call after every scene creation/restart. Owns copies, never scene pointers.
    bool reset();
    // Call immediately before scene.calc. Player zero remains human unless
    // its bit in bulletPlayers is set. Bullet guidance applies to every racer;
    // CPU inputs never press the item key.
    void beforeStep(uint16_t bulletPlayers=0);
    void afterStep();
    void setBattleRanks(const std::array<unsigned,Capacity>& ranks){if(battleArena_)ranks_=ranks;}
    unsigned count() const{return count_;}
    bool routeReady() const{return route_.size()!=0;}
    // Fixed sun reference from drivable paths, never decorative ocean/sky bounds.
    bool routeCenter(Vec3& center) const {
        if(!route_.size())return false;
        Vec3 lo=route_.point(0).position,hi=lo;
        for(unsigned i=1;i<route_.size();++i){const Vec3 p=route_.point(i).position;
            lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
            hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        }
        center=(lo+hi)*.5f;return true;
    }
    uint16_t bulletNoEndPlayers() const;
    unsigned rank(unsigned player=0) const{return player<count_?ranks_[player]:0;}
    bool finished(unsigned player=0) const{return player<count_&&finished_[player];}
    unsigned lap(unsigned player=0) const{return player<count_?laps_[player]:0;}
    unsigned garageChoice(unsigned player) const{return player<count_?choices_[player]:0;}
    unsigned routePoint(unsigned player) const{return player<count_?drivers_[player].segment:EnemyRoute::Invalid;}
    static float steeringTo(Vec3 forward,Vec3 offset);
    // QA autopilot: player zero follows the CPU route like the other racers.
    bool driveLocal=false;
    CpuAi& ai(){return ai_;}
private:
    void forwardBumps();
    struct Driver {BulletGravity bulletGravity{};CpuSpeedState speedPolicy{};float nativeSpeed=70,desiredSpeed=70;unsigned segment=EnemyRoute::Invalid,bulletSegment=EnemyRoute::Invalid,stuck=0,reverse=0,recoveries=0,routeTicks=0;Vec3 last{};float bulletTurn=.03f;bool respawning=false,bullet=false;};
    EnemyRoute route_,bulletRoute_;
    CpuAi ai_;
    std::array<Driver,Capacity> drivers_{};
    std::array<unsigned,Capacity> choices_{},ranks_{},laps_{};
    std::array<bool,Capacity> finished_{};
    CpuSpeedParam speedParam_{};bool speedParamReady_=false;float lapDistance_=0;
    unsigned count_=1,engineClass_=2,difficulty_=1;bool battleArena_=false;
};
}
