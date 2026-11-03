#pragma once
#include <mkw/coin_battle.hpp>
#include <mkw/coin_presentation.hpp>
#include <memory>

namespace mkw {
class Renderer;
// Owns only this mode's coin pool and GPU resources. Reset after native scene
// creation; clear with the GPU idle before unloading a race or changing mode.
class CoinBattleRuntime {
public:
    CoinBattleRuntime();
    ~CoinBattleRuntime();
    bool reset(const BattleRules&,unsigned racers,const std::array<RaceTeam,12>& teams,unsigned course);
    void clear();
    void step(bool racing,unsigned tick,Vec3 cameraEye,RaceEffects&);
    bool hit(unsigned victim,bool fall);
    bool steal(unsigned thief,unsigned victim);
    void draw(Renderer&,unsigned tick);
    void drawShadows(Renderer&,unsigned tick);
    unsigned score(unsigned racer) const;
    unsigned rank(unsigned racer) const;
    unsigned positions(Vec3* destination,unsigned capacity) const;
    unsigned targetCount()const;
    bool targetPosition(unsigned unit,Vec3& position)const;
    void failedRegistration(unsigned unit)const;
    uint16_t takeCues();
    uint16_t takeArrivalCues();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mkw
