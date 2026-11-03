#pragma once
#include <mkw/race_teams.hpp>
#include <array>
#include <algorithm>
namespace mkw {
//807844c0 caches the winner and winning-team maximum from raw u16 points;
// ties for that maximum all receive MVP. The HUD's999 cap is irrelevant.
struct BattleRoundResult {
    std::array<unsigned,2> points{},maximum{};
    RaceTeam winner=RaceTeam::None;
    unsigned kind(RaceTeam team,unsigned score)const{
        if(winner==RaceTeam::None)return 3;
        if(team!=winner)return 2;
        return score>=maximum[unsigned(winner)]?0:1;
    }
};
inline BattleRoundResult battleRoundResult(const std::array<uint16_t,12>& scores,const std::array<RaceTeam,12>& teams,unsigned racers){
    BattleRoundResult r;
    for(unsigned i=0;i<std::min(racers,12u);++i)if(unsigned(teams[i])<2){auto t=unsigned(teams[i]);
        r.points[t]+=scores[i];r.maximum[t]=std::max(r.maximum[t],unsigned(scores[i]));}
    r.winner=r.points[0]>r.points[1]?RaceTeam::Red:r.points[1]>r.points[0]?RaceTeam::Blue:RaceTeam::None;
    return r;
}
//8070a7dc..a944: offline Battle uses per-local result kind and a separate
// draw branch, never personal race rank. This port has one local player.
inline constexpr unsigned battleGoalMusic(unsigned kind){return 107u+std::min(kind,3u);}
}
