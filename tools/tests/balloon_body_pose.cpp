#include <mkw/balloon_body_pose.hpp>
#include <mkw/balloon_presentation.hpp>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
using namespace mkw;
namespace {
bool close(float a,float b){return std::abs(a-b)<.0002f;}
void equal(Vec3 a,Vec3 b){assert(close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z));}
float sine(float index){return std::sin(index*(6.2831853071795864769f/256.f));}
}
int main(int argc,char** argv){
    assert(argc==2);const std::filesystem::path bsp=argv[1];
    constexpr std::array<const char*,6> names={"sa_kart","ma_kart","la_kart","sa_bike","ma_bike","la_bike"};
    constexpr std::array<float,6> heights={.9f,.9f,1.2f,1.f,.9f,1.1f};
    constexpr std::array<unsigned,6> speeds={13000,6000,10000,8000,12000,11000};
    // Read the six actual Battle standard vehicle BSPs, not a second copy
    // of the adapter's parameters. The tilted basis checks local-Y versus
    // a world-Y displacement; display and attachment share that same pose.
    for(unsigned i=0;i<names.size();++i){
        std::ifstream f(bsp/(std::string(names[i])+".bsp"),std::ios::binary);assert(f);
        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(f),{}};assert(bytes.size()>=0x25a);
        unsigned bits=0;for(unsigned n=0;n<4;++n)bits=(bits<<8)|bytes[0x254+n];
        const float height=std::bit_cast<float>(bits);const unsigned speed=(unsigned(bytes[0x258])<<8)|bytes[0x259];
        assert(close(height,heights[i])&&speed==speeds[i]);
        for(float scale: {1.f,.5f,1.7f}){
            BalloonBodyPose pose;const Vec3 origin{100,200,300},up{.6f,.8f,0};
            const auto p=pose.step(origin,up,scale,height,uint16_t(speed),sine);
            assert(pose.phase==speed);const float dy=heights[i]*scale*sine(float(speeds[i])/256.f);
            equal(p,{100+.6f*dy,200+.8f*dy,300});
            BalloonAttachment a;a.position=p;a.up=up;a.right={.8f,-.6f,0};a.height=220;a.rear=-90;a.scale={scale,scale,scale};
            const auto anchor=balloonAnchor(a);a.position=origin;
            equal(anchor-balloonAnchor(a),p-origin); // body displacement must not get scaled a second time
            pose.phase=65530;pose.step(origin,up,scale,height,20,sine);assert(pose.phase==14);
        }
    }
    BalloonBodyPose quarter;
    equal(quarter.step({}, {0,1,0},2,3,16384,sine),{0,6,0});
    equal(quarter.step({}, {0,1,0},2,3,16384,sine),{});
    equal(quarter.step({}, {0,1,0},2,3,16384,sine),{0,-6,0});
    equal(quarter.step({}, {0,1,0},2,3,16384,sine),{});assert(quarter.phase==0);
    std::puts("PASS six real Battle BSP rumble parameters, local-Y body/balloon shared translation, normal/shrink/Mega scale applied once, native phase wrap and quarter-turn extrema. Absolute retail initial phase and target visuals are not claimed.");
}
