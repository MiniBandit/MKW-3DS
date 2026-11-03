#include <mkw/mii_shape_instance.hpp>
#include <cstdio>
#include <cassert>
int main(){
 std::array<float,9> a{};unsigned ns,ny,gs,gy,flip;
 while(std::scanf("%f %f %f %f %f %f %f %f %f %u %u %u %u %u",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8],&ns,&ny,&gs,&gy,&flip)==14){
  std::array<mkw::MiiShapePlacement,9> p;
  assert(mkw::miiShapePlacements(a,ns,ny,gs,gy,flip,p));
  for(auto& t:p)std::printf("%u %u %.9g %.9g %.9g %.9g ",t.transform,t.mirrorX,t.scale,t.translation[0],t.translation[1],t.translation[2]);
  std::puts("");
 }
 // Signed fixed-point boundaries: mirrored minimum, negative shift rounding.
 mkw::MiiShapePlacement p;p.transform=true;p.scale=1.f/256;p.translation={0,0,0};
 auto v=mkw::miiPlacePosition({-1,-257,257},p);assert(v[0]==-1&&v[1]==-2&&v[2]==1);
 p.transform=false;p.mirrorX=true;assert(mkw::miiPlacePosition({-32768,1,2},p)[0]==-32768);
 auto n=mkw::miiPlaceNormal({-32768,100,-100},p);assert(n[0]==-32768&&n[1]==100&&n[2]==-100);
}
