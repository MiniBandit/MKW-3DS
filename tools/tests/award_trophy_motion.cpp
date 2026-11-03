#include <mkw/award_trophy_motion.hpp>
#include <cassert>
#include <cmath>

int main(){
 mkw::AwardTrophyMotion trophy;
 assert(!trophy.visible());
 assert(trophy.show()==0x21a&&trophy.position().x==700);
 float last=trophy.position().x;
 for(unsigned i=0;i<50;++i){
  trophy.step();assert(trophy.position().x<=last);last=trophy.position().x;
 }
 assert(!trophy.entering()&&trophy.position().x==0);
 const auto yaw=trophy.yaw(),frame=trophy.frame();
 for(unsigned i=0;i<60;++i)trophy.step(true);
 assert(trophy.yaw()==yaw&&trophy.frame()==frame);
 trophy.step();
 assert(trophy.position().x==0&&trophy.yaw()<yaw);
 assert(std::abs(trophy.yaw()-yaw+0.2f*0.01745329252f)<1e-5f);
 trophy.show();assert(trophy.entering()&&trophy.frame()==0&&trophy.position().x==700);
 trophy.reset();trophy.step();assert(!trophy.visible()&&trophy.frame()==0);
}
