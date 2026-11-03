#include <mkw/racer_lod.hpp>
#include <cassert>
#include <cstdio>
int main(){
 for(auto pair:{std::pair<float,unsigned>{1704,1},{1794,2},{2517,2},{3539,3},{11205,4}}){mkw::RacerLod l;assert(l.update(pair.first*pair.first,60,false)==pair.second);}
 mkw::RacerLod l;assert(l.update(1800*1800,60,false)==2);assert(l.update(1700*1700,60,false)==2);assert(l.update(1600*1600,60,false)==1);
 assert(l.update(1800*1800,60,false)==1);assert(l.update(1900*1900,60,false)==2);
 assert(l.update(20000.f*20000,60,true)==1);assert(l.update(20000.f*20000,60,false)==4);
 mkw::RacerLod narrow,wide;assert(narrow.update(2000*2000,35,false)==1);assert(wide.update(2000*2000,80,false)==2);
 puts("LOD sample, hysteresis, FOV, intro/results override PASS");
}
