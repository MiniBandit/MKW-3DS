#include <mkw/depth_layer.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
// Exact f32tof24 transfer in installed libctru gpu.o disassembly:
// exponent rebias -64, fraction >>7, sign at bit23.
uint32_t pica(float x){
 const auto bits=std::bit_cast<uint32_t>(x);const int exponent=int((bits>>23)&255)-64;
 return (bits>>31)<<23|(exponent<0?0:exponent>127?0x7f0000u:uint32_t(exponent)<<16|((bits&0x7fffff)>>7));
}
int main(){
 unsigned collapsed=0;
 for(unsigned i=1;i<=200000;++i){
  const float near=.01f*i,scale=-1.f/near;
  const auto base=pica(scale),old=pica(scale*(1.f+32.f/16777215.f));
  const auto fixed=pica(mkw::layerDepthScale(scale));
  collapsed+=old==base;
  assert(fixed>base&&fixed-base==1); // minimum representable PICA change
  assert(mkw::layerDepthScale(scale)<scale&&std::isfinite(mkw::layerDepthScale(scale)));
 }
 assert(collapsed>100000);
 printf("PASS 200000 near planes: %u previous biases collapsed; new bias survives with exactly one float24 ULP\n",collapsed);
}
