#pragma once
namespace mkw {
// USA805317e0..80531904: KTPT playerIndex 0..5 red, 6..11 blue;
// rotate both teams by the round's shared Random::NextLimited(6).
inline unsigned battleStartSlot(unsigned team,unsigned earlierTeammates,unsigned offset){
    return (team==1?6u:0u)+(offset+earlierTeammates)%6u;
}
}
