#pragma once
#include <array>
#include <cstdint>
namespace mkw {
// FaceController: USA807b7fc0 (state3, timer10), 807bcd84..ce7c.
// Static init807c55b8..55f8 sets the table bound to11 (REL BSS6+64f0).
class DriverBlink {
public:
    using RandomDraw=unsigned(*)(unsigned);
    void bindRandom(RandomDraw draw){draw_=draw;}
    void reset(uint32_t seed=1){timer_=10;frame_=0;playing_=false;state_=3;seed_=seed?seed:1;}
    void duration(unsigned frames){duration_=frames;}
    void step(unsigned state){
        if(state!=state_){
            state_=state;playing_=false;
            if(state==3||state==7)timer_=Wait[next()]+1;
        }
        const bool eligible=state==3||state==7;
        if(playing_&&++frame_>=duration_)playing_=false;
        if(!eligible)return;
        if(timer_==0){
            if(!playing_)timer_=Wait[next()];
        }else if(--timer_==0&&duration_){frame_=0;playing_=true;}
    }
    bool playing()const{return playing_;}
    unsigned frame()const{return frame_;}
private:
    unsigned next(){if(draw_)return draw_(unsigned(Wait.size()));seed_=seed_*0x690379b2b2e3d431ULL+0x508ebdULL;return unsigned(((seed_>>32)*Wait.size())>>32);}
    static constexpr std::array<unsigned,11> Wait{10,10,30,50,80,90,100,130,170,190,270};
    RandomDraw draw_=nullptr;
    uint64_t seed_=1;
    unsigned timer_=10,frame_=0,duration_=0,state_=3;
    bool playing_=false;
};
}
