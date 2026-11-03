#include <mkw/mii_face_color.hpp>
#include <cstdio>
#include <cassert>
int main(){
 unsigned kind,type,color,r,g,b,a;
 while(std::scanf("%u %u %u %u %u %u %u",&kind,&type,&color,&r,&g,&b,&a)==7){
  mkw::MiiPixel tex={r/255.f,g/255.f,b/255.f,a/255.f},out={-1,-1,-1,-1};
  bool ok=kind?mkw::miiMouthColor(tex,color,out):mkw::miiEyeColor(tex,type,color,out);
  std::printf("%u %.9g %.9g %.9g %.9g\n",ok,out[0],out[1],out[2],out[3]);
 }
 mkw::MiiPixel p={0,0,0,0},out={1,2,3,4},old=out;
 assert(!mkw::miiEyeColor(p,50,0,out)&&out==old);
 assert(!mkw::miiEyeColor(p,0,6,out)&&out==old);
 assert(!mkw::miiMouthColor(p,3,out)&&out==old);
}
