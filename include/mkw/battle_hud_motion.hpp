#pragma once
#include <mkw/battle_hud_motion_assets.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
namespace mkw {
// CtrlRaceBattleAddPoint807e31e0 and TotalPoint807e3ab8. Simulation-time
// state; drawing either LCD/eye never advances a score or popup animation.
class BattleHudMotion {
public:
    struct Team {unsigned score=0,animation=1;float frame=0;};
    void reset(){points_=phase_=pending_=cues_=0;popup_=0;popupDelta_=0;popupFrame_=0;popupSpeed_=1;teams_={};}
    void step(unsigned points,unsigned red,unsigned blue,unsigned tick){
        cues_=0;
        for(auto& t:teams_){t.frame+=1;
            if(t.frame>=BattleTeamDurations[t.animation]){
                if(t.animation==0){t.animation=1;t.frame=0;}
                else if(t.animation==1){t.animation=3;t.frame=0;}
                else t.frame=float(BattleTeamDurations[t.animation]);}}
        // Original binding order is Off0/On1 (808a3f64), even though the
        // resource table lists On first. On's authored successor is Off.
        if(popup_){popupFrame_+=popupSpeed_;if(popupFrame_>=75){popup_=0;popupFrame_=0;}}
        const unsigned values[2]={std::min(red,999u),std::min(blue,999u)};
        for(unsigned i=0;i<2;++i){auto& t=teams_[i];if(values[i]!=t.score){
            cues_|=uint8_t(1u<<(values[i]>t.score?i:i+2));t.score=values[i];t.animation=0;t.frame=0;}}
        if(points!=points_){
            if(pending_>=queue_.size())pending_=unsigned(queue_.size()-1);
            unsigned byte=(points-points_)&255u;queue_[pending_++]=int8_t(byte<128?int(byte):int(byte)-256);
            points_=points;phase_=tick%5;return;
        }
        if(tick%5!=phase_||popup_)return;
        if(pending_){popupDelta_=queue_[0];std::move(queue_.begin()+1,queue_.begin()+pending_,queue_.begin());
            --pending_;popup_=1;popupFrame_=0;}
        popupSpeed_=float(std::min(pending_+1,5u));
    }
    const Team& team(unsigned i)const{return teams_[i];}
    const BattleTeamFrame& teamFrame(unsigned team,unsigned slot)const{
        const auto& t=teams_[team];return BattleTeamFrames[t.animation][team][slot][std::min(unsigned(t.frame),46u)];}
    bool popupVisible()const{return popup_!=0;}
    int popupDelta()const{return popupDelta_;}
    float popupFrame()const{return popupFrame_;}
    float popupSpeed()const{return popupSpeed_;}
    unsigned pending()const{return pending_;}
    const BattlePopupFrame& popupPose()const{return BattlePopupFrames[std::min(unsigned(popupFrame_),75u)];}
    uint8_t takeCues(){auto c=cues_;cues_=0;return c;}
private:
    unsigned points_=0,phase_=0,pending_=0,popup_=0;
    int popupDelta_=0;float popupFrame_=0,popupSpeed_=1;
    uint8_t cues_=0;std::array<int8_t,100> queue_{};std::array<Team,2> teams_{};
};
}
