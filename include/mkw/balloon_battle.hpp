#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mkw/battle_rules.hpp>
#include <mkw/battle_ranking.hpp>
#include <mkw/race_teams.hpp>

namespace mkw {

class BalloonBattle {
public:
    static constexpr unsigned MaxRacers = 12;
    static constexpr unsigned StartingBalloons = 3;
    static constexpr unsigned DurationTicks = 180 * 60;

    void configure(const BattleRules& rules, const std::array<RaceTeam,MaxRacers>& teams) {
        rules_ = rules;
        teams_ = teams;
    }

    void reset(unsigned racers) {
        count_ = std::clamp(racers, 1u, MaxRacers);
        ranking_.reset(count_);
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
        const bool attributed = attacker < count_ && attacker != victim;
        const bool friendly = attributed && sameRaceTeam(teams_[attacker], teams_[victim]);
        if (attributed)
            addScore(attacker, friendly ? rules_.teammateHit : rules_.enemyHit);
        addScore(victim, friendly ? rules_.hitByTeammate : rules_.hitByEnemy);
        if (!balloons_[victim]) {
            if (attributed && !friendly) addScore(attacker, rules_.enemyDefeat);
            if (!friendly) addScore(victim, rules_.allBalloonsLost);
        }
        ++hits_;
        return true;
    }

    // RMCU 805342b8, called when KartMove enters respawn with no balloons.
    // Logical count changes now; BalloonManager schedules the three attachments
    // at 0/20/40 ticks (presentation consumes the refill event separately).
    bool replenish(unsigned racer) {
        if (racer >= count_ || balloons_[racer]) return false;
        balloons_[racer] = StartingBalloons;
        return true;
    }

    // RMCU 80533e4c / BalloonManager::move: the victim loses a balloon even
    // when the thief already carries the maximum five.
    bool stealBalloon(unsigned thief, unsigned victim) {
        // Original onMoveHit rejects friendly transfers and an empty thief.
        if (thief >= count_ || victim >= count_ || thief == victim || !balloons_[thief] ||
            sameRaceTeam(teams_[thief], teams_[victim]) || !loseBalloon(victim, thief)) return false;
        balloons_[thief] = uint8_t(std::min<unsigned>(BattleRules::MaxBalloons, balloons_[thief] + 1));
        return true;
    }

    unsigned racers() const { return count_; }
    unsigned balloons(unsigned racer) const { return racer < count_ ? balloons_[racer] : 0; }
    unsigned score(unsigned racer) const { return racer < count_ ? scores_[racer] : 0; }
    unsigned hits() const { return hits_; }
    unsigned durationTicks() const { return rules_.durationTicks; }

    void updateRanks(){ranking_.step(scores_);}
    unsigned rank(unsigned racer) const {return ranking_.rank(racer);}

private:
    void addScore(unsigned racer, int delta) {
        // Original score is u16 and signed rule deltas clamp at zero.
        scores_[racer] = uint16_t(std::max(0, int(scores_[racer]) + delta));
    }
    BattleRules rules_{};
    BattleRanking ranking_;
    std::array<RaceTeam,MaxRacers> teams_ = [] {
        std::array<RaceTeam,MaxRacers> teams;
        teams.fill(RaceTeam::None);
        return teams;
    }();
    std::array<uint8_t, MaxRacers> balloons_{};
    std::array<uint16_t, MaxRacers> scores_{};
    unsigned count_ = 0;
    unsigned hits_ = 0;
};

} // namespace mkw
