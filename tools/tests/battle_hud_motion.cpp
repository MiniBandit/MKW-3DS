#include <mkw/battle_hud_motion.hpp>
#include <mkw/battle_round_result.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace mkw;
int main(){
    BattleHudMotion h;h.reset();
    for(unsigned t=0;t<80;++t){h.step(0,0,0,t);assert(!h.takeCues()&&!h.popupVisible());}
    assert(h.team(0).animation==3&&h.teamFrame(0,2).white==0xffff80ffu&&h.teamFrame(1,0).white==0x80ffffffu);
    h.step(1,1,0,80);assert(h.takeCues()==1&&h.pending()==1&&!h.popupVisible()&&h.team(0).animation==0);
    for(unsigned t=81;t<85;++t){h.step(1,1,0,t);assert(!h.popupVisible()&&!h.takeCues());}
    h.step(1,1,0,85);assert(h.popupVisible()&&h.popupDelta()==1&&h.popupFrame()==0&&h.pending()==0);
    for(unsigned t=86;t<160;++t)h.step(1,1,0,t);
    assert(h.popupVisible());h.step(1,1,0,160);assert(!h.popupVisible());
    h.step(0,0,1,161);assert(h.takeCues()==6); // red loss and blue gain
    for(unsigned t=162;t<=166;++t)h.step(0,0,1,t);
    assert(h.popupVisible()&&h.popupDelta()==-1);
    h.reset();h.step(1,999,999,0);assert(h.takeCues()==3);
    h.step(2,1000,1001,1);assert(!h.takeCues()&&h.team(0).score==999&&h.team(1).score==999);
    // Per-frame signed deltas queue in chronological order. Original100
    // capacity replaces the newest pending entry once full, and speeds a
    // backlog up to5 without merging away its individual score changes.
    h.reset();for(unsigned t=0;t<150;++t)h.step(t+1,t+1,0,t);
    assert(h.pending()==100);unsigned popped=0;
    for(unsigned t=150;t<3500;++t){h.step(150,150,0,t);
        if(h.popupVisible()&&h.popupFrame()==0){++popped;assert(h.popupDelta()==1&&h.popupSpeed()>=1&&h.popupSpeed()<=5);}}
    assert(popped==100&&h.pending()==0&&!h.popupVisible());
    for(unsigned team=0;team<2;++team)for(unsigned d=0;d<10;++d)assert(BattleGlyphMetrics[team][d].advance==46);
    assert(BattleGlyphMetrics[0][10].advance==32&&BattleGlyphMetrics[1][10].advance==31);
    assert(BattleHudPersonalDigits[0][3].w!=BattleGlyphMetrics[0][3].advance);
    assert(BattlePopupFrames[0].sx>0&&BattlePopupFrames[10].y>100&&BattlePopupFrames[74].alpha==0);
    for(unsigned animation=0;animation<4;++animation)for(unsigned team=0;team<2;++team)for(unsigned d=0;d<3;++d)for(unsigned t=0;t<47;++t){
        const auto& f=BattleTeamFrames[animation][team][d][t];assert(std::isfinite(f.sx)&&std::isfinite(f.sy)&&std::isfinite(f.angle));}
    std::array<RaceTeam,12> teams;teams.fill(RaceTeam::None);teams[0]=teams[1]=teams[2]=RaceTeam::Red;teams[3]=RaceTeam::Blue;
    std::array<uint16_t,12> scores{};scores[0]=scores[1]=8;scores[2]=1;scores[3]=9;
    auto outcome=battleRoundResult(scores,teams,4);assert(outcome.winner==RaceTeam::Red&&outcome.points[0]==17);
    assert(outcome.kind(teams[0],8)==0&&outcome.kind(teams[1],8)==0&&outcome.kind(teams[2],1)==1&&outcome.kind(teams[3],9)==2);
    scores[3]=17;outcome=battleRoundResult(scores,teams,4);assert(outcome.kind(teams[0],8)==3&&outcome.kind(teams[3],17)==3);
    scores={1000,0,0,1001};outcome=battleRoundResult(scores,teams,4);assert(outcome.winner==RaceTeam::Blue); // HUD both999
    for(unsigned kind=0;kind<4;++kind)assert(battleGoalMusic(kind)==107+kind);
    std::puts("PASS native score delta queue/phase/cap/speed/transition chains, per-team cues/clamp, original colour/motion and46-unit glyph advances, raw team outcome/tied MVP/win/loss/draw fanfare selection; target rendering/audio pending");
}
