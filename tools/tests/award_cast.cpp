#include <mkw/award_cast.hpp>
#include <mkw/award_placement.hpp>
#include <cassert>
#include <cstring>
using namespace mkw;
int main(){
 for(unsigned id=0;id<24;++id)for(unsigned vehicle:{0u,18u,35u}){
  const auto* asset=awardActorAsset(awardCharacterArchive(id,vehicle));
  assert(asset&&asset->animation);
 }
 assert(!std::strcmp(awardCharacterArchive(15,18),"ds3"));
 assert(!std::strcmp(awardCharacterArchive(16,0),"pc"));
 assert(!std::strcmp(awardCharacterArchive(23,18),"rs3"));
 AwardResults results;results.valid=true;results.count=12;
 for(unsigned i=0;i<12;++i)results.players[i]={uint8_t(i),uint8_t(i),0,i==5,i%2?RaceTeam::Red:RaceTeam::Blue,100-i};
 auto cast=makeAwardCast(results);
 assert(cast.valid&&cast.podiumCount==3&&cast.audienceCount==9);
 assert(!std::strcmp(cast.stand->model,"v_stand1"));
 assert(cast.podium[0].player.racer==0&&cast.podium[2].player.racer==2);
 auto positions=placeAwardCast(cast,false,{10,20,30});
 assert(positions.podium[0].position.y==160&&positions.podium[1].position.x==-215&&positions.podium[2].position.x==235);
 for(unsigned i=1;i<cast.audienceCount;++i)
  assert(awardWeight(cast.audience[i-1].player.character)<=awardWeight(cast.audience[i].player.character));
 assert(positions.audience[0].position.x==210&&positions.audience[1].position.x==-190);
 assert(positions.audience[0].yaw<0&&positions.audience[1].yaw>0);
 assert(!std::strcmp(cast.podium[0].clip,"demowin")&&!std::strcmp(cast.audience[0].clip,"demooth"));
 results.teams=true;
 assert(!makeAwardCast(results).valid);
 cast=makeAwardCast(results,RaceTeam::Red);
 assert(cast.valid&&cast.podiumCount==6&&cast.audienceCount==6);
 assert(!std::strcmp(cast.stand->model,"v_stand4"));
 assert(cast.podium[0].player.racer==5&&cast.podium[1].player.racer==1&&cast.podium[2].player.racer==3);
 positions=placeAwardCast(cast,true,{});
 assert(positions.podium[0].position.x==120&&positions.podium[1].position.x==-120);
 assert(positions.podium[2].position.z< -145); // Original six-winner overlap adjustment.
 results.players[0].character=24;assert(!makeAwardCast(results,RaceTeam::Red).valid);
 const auto soloStand=placeAwardStand(false,{10,20,30},{4,8,12},{0,1,0});
 assert(soloStand.scale==1&&soloStand.position.x==10&&soloStand.position.y==20&&soloStand.position.z==30);
 const auto teamStand=placeAwardStand(true,{10,20,30},{4,8,12},{0,1,0});
 assert(teamStand.scale==1.25f&&teamStand.position.x==9&&teamStand.position.y==-7&&teamStand.position.z==27);
}
