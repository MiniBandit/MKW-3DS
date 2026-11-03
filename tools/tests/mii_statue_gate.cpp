#include <mkw/course_mii_statue.hpp>
#include <array>
#include <cassert>
struct Player {unsigned type,character;};
int main(){
 std::array<Player,12> racers{};
 for(unsigned first=0;first<24;++first){
  for(unsigned i=0;i<12;++i)racers[i]={i?1u:0u,(first+i)%24};
  assert(!mkw::miiStatuePlayersEligible(racers,12));
 }
 for(unsigned character=24;character<=44;++character){
  racers.fill({1,0});racers[11]={0,character};
  assert(mkw::miiStatuePlayersEligible(racers,12));
  assert(!mkw::miiStatuePlayersEligible(racers,11));
  assert(mkw::miiStatuePlayersEligible(racers,255));
  racers[11].type=2;assert(mkw::miiStatuePlayersEligible(racers,12));
  for(unsigned type: {1u,3u,4u,5u}){racers[11].type=type;assert(!mkw::miiStatuePlayersEligible(racers,12));}
 }
 for(unsigned character: {0u,23u,45u,~0u})assert(!mkw::miiStatuePlayerEligible(0,character));
 assert(!mkw::miiStatuePlayersEligible(racers,0));
}
