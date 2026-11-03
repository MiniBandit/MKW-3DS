#include <mkw/award_board_motion.hpp>
#include <mkw/race_flow_layout.hpp>
#include <cassert>
int main(){
 mkw::AwardBoardMotion motion;assert(!motion.active());motion.enter();
 for(unsigned i=0;i<180;++i)motion.step();
 const auto title=mkw::FlowAwardTypeWinVariants[0].pose;
 const auto row=mkw::FlowAwardItemWinVariants[0].pose;
 assert(motion.offset(title.delay)==832&&motion.offset(row.delay)==832);
 for(unsigned i=0;i<5;++i)motion.step();
 assert(motion.offset(title.delay)==416&&motion.offset(row.delay)>416);
 for(unsigned i=0;i<20;++i)motion.step();
 assert(motion.finished(mkw::FlowAwardItemWinVariants[5].pose.delay));
 assert(motion.offset(title.delay)==0);
 motion.leave();assert(motion.leaving()&&motion.offset(0)==0);
 for(unsigned i=0;i<10;++i)motion.step();
 assert(motion.offset(0)==-832);
 motion.reset();assert(!motion.active());
}
