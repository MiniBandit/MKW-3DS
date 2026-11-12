#include <mkw/race_flow_draw.hpp>
#include <mkw/race_intro_layout.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace mkw;
struct Probe {unsigned batches=0,icons=0,glyphs=0;float right=-10000,left=10000;};
void sink(const FlowBatch& b,void* context){
 auto& p=*static_cast<Probe*>(context);++p.batches;
 assert(b.texture==0||b.texture==1);assert(b.wrapS==0&&b.wrapT==0);
 if(b.texture==1)p.icons+=b.count;else p.glyphs+=b.count;
 for(unsigned i=0;i<b.count;++i)for(unsigned k=0;k<4;++k){
  const auto& q=b.quads[i];assert(std::isfinite(q.x[k])&&std::isfinite(q.y[k]));
  assert(q.u[k]>=0&&q.u[k]<=1&&q.v[k]>=0&&q.v[k]<=1);
  p.right=std::max(p.right,q.x[k]);p.left=std::min(p.left,q.x[k]);
 }
}
int main(){
 assert(!std::strcmp(IntroCourseNames[0],"Luigi Circuit"));
 assert(!std::strcmp(IntroCourseNames[16],"GCN Peach Beach"));
 assert(!std::strcmp(IntroGPTemplates[3],"%s %s Final Race"));
 assert(!std::strcmp(IntroVSTemplate,"%s VS %u/%u"));
 assert(!std::strcmp(IntroTeamVSTemplate,"%s Team VS Race %u/%u"));
 assert(IntroCourseControl.delay==20&&IntroCupControl.delay==10);
 auto* drawer=new FlowDrawer;unsigned cases=0;
 for(unsigned course=0;course<32;++course)for(unsigned engine=0;engine<4;++engine)for(unsigned mode=0;mode<5;++mode){
  RaceIntroInfo in{course,engine,course/4,course%4,4,mode,false};
  assert(introIcon(in)==(mode==0?course/4:mode==3?9:mode==4?10:8));
  Probe p;drawer->drawIntro(in,60,sink,&p);
  assert(p.batches==(mode==2?2:4));assert(p.icons==(mode==2?0:1));assert(p.glyphs>10);
  // Right-aligned course labels must not run off the top LCD; left-edge
  // cup icons must be on screen after the source delayed slide finishes.
  assert(p.right<=400&&p.left>=0);
  if(mode==1){in.teams=true;Probe team;drawer->drawIntro(in,60,sink,&team);assert(team.glyphs>p.glyphs);}
  Probe start;drawer->drawIntro(in,0,sink,&start);assert(start.left>400);
  ++cases;
 }
 delete drawer;std::printf("PASS %u course/class/mode layouts, original source messages, TT visibility, delayed slides, finite glyph UVs and LCD bounds\n",cases);
}
