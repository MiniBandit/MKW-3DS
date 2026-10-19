#pragma once
#include <mkw/gp_rank_times.hpp>
#include <algorithm>
#include <array>
#include <optional>

namespace mkw {
struct GpRacePerformance {
 unsigned frames=0,firstPlaceFrames=0;
 unsigned miniTurbos=0,itemHits=0,offroadFrames=0,wallHits=0,objectHits=0,respawns=0,aiBonus=0;
 bool rocketStart=false,wiiWheel=false,automaticDrift=false;
};
// RaceManagerPlayer::updateGpRankScore (PAL 805368f8). Statistics are
// event counts supplied by the race; never infer them from visual effects.
inline std::optional<int> gpRaceScore(const GpRacePerformance& s,unsigned retailCourse,unsigned engineClass){
 if(retailCourse>=32||engineClass>=4)return {};
 const int reference=GpRankTimes[retailCourse][engineClass];
 if(!reference)return {};
 int score=int(1000.f*(float(reference)-float(s.frames))/float(reference));
 score+=int(uint64_t(s.firstPlaceFrames)*150/reference);
 score+=s.rocketStart?25:0;
 score+=int(s.miniTurbos)*2+int(s.itemHits)*5-int(s.offroadFrames/3);
 score-=int(s.wallHits)*20+int(s.objectHits)*30+int(s.respawns)*70;
 score+=(s.wiiWheel?10:0)+(s.automaticDrift?25:0)+int(s.aiBonus);
 return std::clamp(score,-50,250);
}
// RaceConfig::Player::computeGpRank, USA 80528fa8..8052907c.
// 0..2 = three/two/one stars; 3..7 = A/B/C/D/E.
inline unsigned gpGrade(int accumulatedPerformance,unsigned points){
 constexpr int performance[]={1000,700,400,200,0};
 constexpr unsigned totals[]={60,52,40,20};
 unsigned a=5,b=4;
 for(unsigned i=0;i<5;++i)if(accumulatedPerformance>=performance[i]){a=i;break;}
 for(unsigned i=0;i<4;++i)if(points>=totals[i]){b=i;break;}
 return std::min(a+b,7u);
}
// AIRankManager speed-advantage phase (USA 80733394), table 808c5ec4.
// Uses the race manager's absolute frame, not elapsed boost duration.
inline bool gpAiBonusTick(unsigned engineClass,unsigned frame){
 constexpr int interval[]={-1,90,52};
 return engineClass<3&&interval[engineClass]>0&&frame%unsigned(interval[engineClass])==0;
}
inline std::optional<unsigned> gpCupGrade(const std::array<GpRacePerformance,4>& races,
 const std::array<bool,4>& captured,const std::array<unsigned,4>& courses,unsigned engineClass,unsigned points){
 int score=0;
 for(unsigned i=0;i<4;++i){
  if(!captured[i])return {};
  const auto raceScore=gpRaceScore(races[i],courses[i],engineClass);
  if(!raceScore)return {};
  score+=*raceScore;
 }
 return gpGrade(score,points);
}
inline unsigned gpGradeSound(int grade){
 // AwardResults::AfterControlUpdate, USA 80622434..806224a0.
 return grade>=0&&grade<3?0x219u-unsigned(grade):0;
}
}
