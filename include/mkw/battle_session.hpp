#pragma once
#include <mkw/race_teams.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
// RMCU8083ab38/8083ad40: sum this round's individual scores by team;
// increment only the winner's match count. A draw increments neither.
// RaceMenu::IsLastMatch8083a1e0 ends when either reaches the selected goal.
class BattleSession {
public:
    static constexpr std::array<unsigned,6> WinCounts{2,3,4,5,8,10}; //808a6d58
    void reset(unsigned target=2){
        target_=std::find(WinCounts.begin(),WinCounts.end(),target)!=WinCounts.end()?target:2;
        wins_={};roundScore_={};previous_={};totals_={};individual_={};round_=1;sealed_=false;
    }
    bool seal(const std::array<uint16_t,12>& scores,const std::array<RaceTeam,12>& teams,unsigned count){
        if(sealed_)return false;
        roundScore_={};
        previous_=totals_;individual_={};
        for(unsigned i=0;i<std::min(count,12u);++i){
            individual_[i]=scores[i];
            // RaceConfig::Scenario finalization8052a0d8..0fc adds the
            // Battle score to the old halfword and stores a halfword.
            totals_[i]=uint16_t(unsigned(previous_[i])+scores[i]);
            if(unsigned(teams[i])<2)roundScore_[unsigned(teams[i])]+=scores[i];
        }
        const auto winner=roundWinner();if(winner!=RaceTeam::None)++wins_[unsigned(winner)];
        sealed_=true;return true;
    }
    void restartRound(){
        // A pause can be accepted in the finish transition, after seal().
        // Undo only this round; earlier rounds and chosen goal stay intact.
        if(sealed_){const auto team=roundWinner();if(team!=RaceTeam::None&&wins_[unsigned(team)])--wins_[unsigned(team)];totals_=previous_;}
        sealed_=false;roundScore_={};individual_={};
    }
    bool nextRound(){if(!sealed_||complete())return false;++round_;sealed_=false;roundScore_={};return true;}
    RaceTeam roundWinner()const{return roundScore_[0]>roundScore_[1]?RaceTeam::Red:roundScore_[0]<roundScore_[1]?RaceTeam::Blue:RaceTeam::None;}
    bool complete()const{return wins_[0]>=target_||wins_[1]>=target_;}
    RaceTeam winner()const{return complete()?(wins_[0]>=target_?RaceTeam::Red:RaceTeam::Blue):RaceTeam::None;}
    unsigned round()const{return round_;}
    unsigned wins(RaceTeam team)const{return unsigned(team)<2?wins_[unsigned(team)]:0;}
    unsigned target()const{return target_;}
    unsigned previousScore(unsigned racer)const{return racer<12?previous_[racer]:0;}
    unsigned totalScore(unsigned racer)const{return racer<12?totals_[racer]:0;}
    unsigned roundScore(unsigned racer)const{return racer<12?individual_[racer]:0;}
private:
    std::array<unsigned,2> wins_{},roundScore_{};
    std::array<uint16_t,12> previous_{},totals_{},individual_{};
    unsigned target_=2,round_=1;
    bool sealed_=false;
};
}
