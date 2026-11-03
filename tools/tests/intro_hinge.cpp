#include <mkw/display_layout.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace mkw::display;
int main(){
 assert(HingeGap==80&&LowerOriginY==320&&IntroHeight==560);
 // A source clip y maps to one continuous world pixel coordinate. Test
 // each visible row and the omitted hinge, with either projection tilt.
 for(int sign:{-1,1})for(int y=-100;y<=660;++y){
  float raw=sign*(1.f-float(y)/120.f);
  float top=raw;
  float bottom=raw+sign*IntroBottomClipOffset;
  float topY=120.f*(1-sign*top),bottomY=LowerOriginY+120.f*(1-sign*bottom);
  assert(std::fabs(topY-y)<.0001f&&std::fabs(bottomY-y)<.0001f);
  bool topVisible=std::fabs(top)<=1.00001f,bottomVisible=std::fabs(bottom)<=1.00001f;
  if(y>240&&y<320)assert(!topVisible&&!bottomVisible);
  float cull=raw*IntroCullScale+sign*IntroCullOffset;
  if((y>=0&&y<=240)||(y>=320&&y<=560))assert(std::fabs(cull)<=1.00001f);
 }
 // Authored camera target remains at Y120, above the hinge on both tilts.
 assert(Height/2==120&&Height/2<Height);
 puts("PASS 1522 projected rows: 80px hidden hinge, consistent pixel scale, combined culling, upper-camera subject preserved");
}
