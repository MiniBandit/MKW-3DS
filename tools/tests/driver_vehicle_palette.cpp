#include <mkw/driver_look_palette.hpp>
#include <array>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 std::array<float,63> p{};
 for(unsigned i=0;i<3;++i){auto*m=p.data()+21*i;m[0]=m[5]=m[10]=m[12]=m[16]=m[20]=1;m[3]=10;m[7]=20;m[11]=30;}
 p[24]=11; // Descendant one unit along X from head.
 const auto base=p;uint32_t slots[]={0,1};
 // Z(+90), X(+90), original Y(-90): X -> Y -> Z -> -X.
 // Taking Y after the vehicle tilt would give a different result.
 applyDriverLookPalette(p.data(),0,slots,2,90,90,90);
 assert(std::fabs(p[0]+1)<1e-5f&&std::fabs(p[4])<1e-5f&&std::fabs(p[8])<1e-5f);
 assert(std::fabs(p[12]+1)<1e-5f&&std::fabs(p[15])<1e-5f&&std::fabs(p[18])<1e-5f);
 assert(std::fabs(p[24]-9)<1e-5f&&std::fabs(p[28]-20)<1e-5f&&std::fabs(p[32]-30)<1e-5f);
 for(unsigned j=42;j<63;++j)assert(p[j]==base[j]);
 for(unsigned j:{3u,7u,11u})assert(p[j]==base[j]);
 p=base;
 // Body look follows the tilted local X (now Z), so the X column stays Z.
 applyDriverBodyLookPalette(p.data(),0,slots,2,90,90,90);
 assert(std::fabs(p[0])<1e-5f&&std::fabs(p[4])<1e-5f&&std::fabs(p[8]-1)<1e-5f);
 // Initial Y -> -X under tilt, then -Y under local-X look.
 assert(std::fabs(p[1])<1e-5f&&std::fabs(p[5]+1)<1e-5f&&std::fabs(p[9])<1e-5f);
 for(unsigned j=42;j<63;++j)assert(p[j]==base[j]);
 puts("PASS vehicle roll/pitch/look order, saved head Y, transformed body X, normals, pivots and subtree");
}
