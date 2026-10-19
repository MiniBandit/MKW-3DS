#include <mkw/gp_rank.hpp>
#include <cassert>
int main(){
 using namespace mkw;
 assert(gpGrade(1000,60)==0&&gpGrade(999,60)==1);
 assert(gpGrade(700,60)==1&&gpGrade(699,60)==2);
 assert(gpGrade(400,52)==3&&gpGrade(200,40)==5);
 assert(gpGrade(-1,0)==7&&gpGrade(1000,59)==1);
 assert(gpGradeSound(0)==0x219&&gpGradeSound(2)==0x217);
 assert(gpGradeSound(-1)==0&&gpGradeSound(3)==0);
 GpRacePerformance s;s.frames=GpRankTimes[0][2];
 assert(gpRaceScore(s,0,2)==0);
 s.firstPlaceFrames=s.frames;assert(gpRaceScore(s,0,2)==150);
 s.rocketStart=true;s.automaticDrift=true;s.miniTurbos=10;s.itemHits=2;
 assert(gpRaceScore(s,0,2)==230);
 s.objectHits=1;s.wallHits=1;s.respawns=1;s.offroadFrames=30;
 assert(gpRaceScore(s,0,2)==100);
 s.respawns=100;assert(gpRaceScore(s,0,2)==-50);
 s={};assert(gpRaceScore(s,0,2)==250);
 assert(!gpRaceScore(s,32,2)&&!gpRaceScore(s,0,4));
 assert(!gpAiBonusTick(0,0)&&!gpAiBonusTick(0,180));
 assert(gpAiBonusTick(1,180)&&!gpAiBonusTick(1,181));
 assert(gpAiBonusTick(2,104)&&!gpAiBonusTick(2,105)&&!gpAiBonusTick(3,104));
 std::array<GpRacePerformance,4> races{};
 std::array<bool,4> captured{true,true,true,true};
 std::array<unsigned,4> courses{8,1,2,4};
 assert(gpCupGrade(races,captured,courses,2,60)==0);
 captured[1]=false;assert(!gpCupGrade(races,captured,courses,2,60));
 captured[1]=true;courses[1]=32;assert(!gpCupGrade(races,captured,courses,2,60));
 courses[1]=1;
 for(unsigned i=0;i<4;++i)races[i].frames=GpRankTimes[courses[i]][2];
 assert(gpCupGrade(races,captured,courses,2,60)==4);
 for(auto& race:races)race.aiBonus=100;
 assert(gpCupGrade(races,captured,courses,2,60)==2);
}
