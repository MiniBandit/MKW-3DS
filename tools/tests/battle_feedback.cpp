#include <mkw/balloon_body.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/race_intro_layout.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace mkw;
struct Capture {unsigned text=0,icons=0;};
void sink(const FlowBatch& b,void* p){auto& c=*static_cast<Capture*>(p);(b.texture==0?c.text:c.icons)+=b.count;}
int main(){
    static_assert(std::size(IntroCourseNames)==42);
    FlowDrawer drawer;
    for(unsigned mode:{3u,4u})for(unsigned course=32;course<42;++course){
        Capture c;RaceIntroInfo in;in.mode=mode;in.course=course;drawer.drawIntro(in,100,sink,&c);
        assert(c.text>=std::char_traits<char>::length(IntroCourseNames[course])+10&&c.icons>0);
    }
    // Original8087d050..d060 retains positive projected kart velocity and
    // replaces negative projection with0. The former min sign left a120-
    // unit/tick kart thousands of units away from its balloon within180ticks.
    for(float speed:{0.f,80.f,120.f,-80.f}){
        BalloonAttachment p;p.height=180;p.rear=-50;BalloonBody body;body.init(p,0,1,4);
        float maximumError=0,maximumRelativeStep=0;
        for(unsigned t=0;t<180;++t){const auto old=body.knot;p.position.z+=speed;
            body.step(&p,0,1,4,{0,0,speed},true,nullptr,0,[]{return .5f;});
            const float error=length(body.knot-(balloonAnchor(p)+Vec3{0,160,0}));
            const float relative=length(body.knot-old-Vec3{0,0,speed});
            assert(std::isfinite(error)&&error<60&&relative<19);
            maximumError=std::max(maximumError,error);maximumRelativeStep=std::max(maximumRelativeStep,relative);body.publish(4);
        }
        std::printf("speed%.0f: anchor error max%.5f, relative step max%.5f\n",speed,maximumError,maximumRelativeStep);
    }
    std::puts("PASS20 actual Battle intro mode/course combinations emit original name/mode glyphs and icon; stationary/forward/boost-speed/backward180tick balloon motion stays attached with native velocity-projection sign. GPU appearance remains pending.");
}
