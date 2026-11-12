#include <mkw/mii_face_layout.hpp>
#include <cassert>
#include <cstdio>
#include <bit>
#include <cmath>
int main(int argc,char** argv){
 assert(argc==2);FILE* input=std::fopen(argv[1],"rb");assert(input);unsigned cases=0;
 for(;;){std::uint32_t v[18];if(std::fread(v,sizeof(v),1,input)!=1)break;
  mkw::MiiFaceFeatures f{v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15],v[16]};
  std::array<mkw::MiiFacePart,8> parts{};assert(mkw::miiFaceLayout(f,v[17],parts));
  for(auto& part:parts){const float values[]={part.x,part.y,part.w,part.h,part.angle};
   for(unsigned k=0;k<6;++k){unsigned char b[4];assert(std::fread(b,4,1,input)==1);
    const std::uint32_t expected=std::uint32_t(b[0])<<24|std::uint32_t(b[1])<<16|std::uint32_t(b[2])<<8|b[3];
    const std::uint32_t actual=k==5?part.mirror:std::bit_cast<std::uint32_t>(values[k]);
    if(actual!=expected){std::printf("case %u component %u: %08x != %08x\n",cases,k,actual,expected);return 1;}
   }
  }++cases;
 }
 std::fclose(input);assert(cases==1000);std::array<mkw::MiiFacePart,8> out{};mkw::MiiFaceFeatures bad{};bad.eyeType=50;assert(!mkw::miiFaceLayout(bad,64,out));
 std::printf("PASS %u retail layouts, all 48 fields bit-exact; invalid style rejected\n",cases);
}
