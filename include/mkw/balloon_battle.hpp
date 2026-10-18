#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {

class BalloonBattle {
public:
    static constexpr unsigned MaxRacers = 12;
    static constexpr unsigned StartingBalloons = 3;
    static constexpr unsigned MaxBalloons = 5; // fn_1_359D28: `count < 5`
    static constexpr unsigned DurationTicks = 180 * 60;

    void reset(unsigned racers) {
        count_ = std::clamp(racers, 1u, MaxRacers);
        balloons_.fill(0);
        scores_.fill(0);
        for (unsigned i = 0; i < count_; ++i)
            balloons_[i] = StartingBalloons;
        hits_ = 0;
    }

    bool loseBalloon(unsigned victim, unsigned attacker) {
        if (victim >= count_ || balloons_[victim] == 0)
            return false;
        --balloons_[victim];
        if (attacker < count_ && attacker != victim && balloons_[attacker] && scores_[attacker] < 999)
            ++scores_[attacker];
        if (!balloons_[victim]) {
            scores_[victim] /= 2;
            balloons_[victim] = StartingBalloons;
        }
        ++hits_;
        return true;
    }

    /// Star contact and mushroom bumps (retail fn_1_285E0 -> fn_1_359D28): the attacker takes
    /// one of the victim's balloons, up to MaxBalloons (a sixth one just pops). Only a racer
    /// that still has balloons can steal.
    bool stealBalloon(unsigned victim, unsigned attacker) {
        if (attacker >= count_ || attacker == victim || balloons_[attacker] == 0)
            return false;
        const bool gain = balloons_[attacker] < MaxBalloons;
        if (!loseBalloon(victim, attacker))
            return false;
        if (gain)
            ++balloons_[attacker];
        return true;
    }

    unsigned racers() const { return count_; }
    unsigned balloons(unsigned racer) const { return racer < count_ ? balloons_[racer] : 0; }
    unsigned score(unsigned racer) const { return racer < count_ ? scores_[racer] : 0; }
    unsigned hits() const { return hits_; }

    unsigned rank(unsigned racer) const {
        if (racer >= count_)
            return count_;
        unsigned result = 1;
        for (unsigned i = 0; i < count_; ++i) {
            if (scores_[i] > scores_[racer] ||
                (scores_[i] == scores_[racer] && balloons_[i] > balloons_[racer]) ||
                (scores_[i] == scores_[racer] && balloons_[i] == balloons_[racer] && i < racer))
                ++result;
        }
        return result;
    }

private:
    std::array<uint8_t, MaxRacers> balloons_{};
    std::array<uint16_t, MaxRacers> scores_{};
    unsigned count_ = 0;
    unsigned hits_ = 0;
};

} // namespace mkw
