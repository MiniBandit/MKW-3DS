#include <mkw/award_camera_data.hpp>
#include <mkw/award_results.hpp>
#include <cassert>
#include <cstring>
using namespace mkw;
int main(){
 static_assert(std::size(AwardCameraClips)==5);
 for(const auto& clip:AwardCameraClips){
  for(const auto& track:clip.tracks){
   assert(track.count);
   for(unsigned i=0;i<track.count;++i)
    assert(std::fabs(sampleAwardTrack(track,track.keys[i].frame)-track.keys[i].value)<.01f);
  }
  for(float t=0;t<clip.duration+10;t+=.5f){
   const auto c=sampleAwardCamera(clip,t);
   assert(std::isfinite(c.position.x)&&std::isfinite(c.rotation.y));
   assert(c.near>0&&c.far>c.near&&c.fovy>0&&c.fovy<180);
   const auto matrices=awardCameraMatrices(c);
   const auto& v=matrices.view;const auto& p=matrices.projection;
   const Vec3 right{v[0],v[1],v[2]},up{v[4],v[5],v[6]},back{v[8],v[9],v[10]};
   assert(std::fabs(length(right)-1)<.0001f&&std::fabs(dot(right,up))<.0001f);
   assert(length(cross(right,up)-back)<.0001f);
   assert(std::fabs(dot(right,c.position)+v[3])<.01f);
   assert(std::fabs((-c.near*p[10]+p[11])/c.near+1)<.0001f);
   assert(std::fabs((-c.far*p[10]+p[11])/c.far)<.0001f);
  }
 }
 const auto c=sampleAwardCamera(AwardCameraClips[0],0);
 AwardCameraSample axis;axis.rotation={0,90,0};
 const auto yaw=awardCameraMatrices(axis);
 assert(std::fabs(yaw.view[2]+1)<.0001f&&std::fabs(yaw.view[8]-1)<.0001f);
 axis.rotation={90,0,0};const auto pitch=awardCameraMatrices(axis);
 assert(std::fabs(pitch.view[6]-1)<.0001f&&std::fabs(pitch.view[9]+1)<.0001f);
 assert(std::fabs(c.position.x-3313.66943359375f)<.001f);
 // Hermite tangents are slopes per frame, including unequal key spacing.
 constexpr AwardCameraKey keys[]={{0,0,1},{10,0,-1}};
 assert(std::fabs(sampleAwardTrack({keys,2,true},5)-2.5f)<.001f);
 RaceResultsFlow flow;flow.finish();RaceFlowInput input;input.advance=true;
 const RaceResultEntry rows[]={{1,7,false,30,15,RaceTeam::Blue},{0,0,true,40,12,RaceTeam::Red}};
 for(unsigned t=0;t<1500&&!flow.totals();++t){if(flow.step(input)&RaceResultsFlow::EventFill)flow.fill(rows,2,true);}
 assert(flow.totals());
 std::array<uint8_t,12> vehicles{};vehicles[0]=3;vehicles[1]=4;
 AwardResults copy;copy.capture(flow,1,2,2,true,vehicles);
 flow.reset();
 assert(copy.valid&&copy.count==2&&copy.mirror&&copy.teams);
 assert(copy.players[0].racer==0&&copy.players[0].points==52&&copy.players[0].vehicle==3);
 assert(copy.players[1].points==45&&copy.teamScores[0].total()==52);
 copy.capture(flow,1,0,2,false,vehicles);assert(!copy.valid);
}
