#include <mkw/battle_result_panel.hpp>
#include <mkw/battle_result_assets.hpp>
#include <mkw/race_results.hpp>
#include <mkw/coin_hud_stack.hpp>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
using namespace mkw;
struct Capture {
 std::vector<std::string> strings;
 static void collect(const FlowBatch& batch,void* context){
  auto& out=*static_cast<Capture*>(context);assert(batch.texture==0);std::string text;
  for(unsigned i=0;i<batch.count;++i){const auto& q=batch.quads[i];
   for(unsigned k=0;k<4;++k){assert(std::isfinite(q.x[k])&&std::isfinite(q.y[k]));assert(q.u[k]>=0&&q.u[k]<=1&&q.v[k]>=0&&q.v[k]<=1);}
   bool found=false;
   for(const auto& g:FlowBattleWinGlyphs)if(std::fabs(q.u[0]-float(g.x)/FlowBattleWinFont.atlasW)<1e-6f&&std::fabs(q.v[0]-float(g.y)/FlowBattleWinFont.atlasH)<1e-6f){
    text+=char(g.code);found=true;break;}
   assert(found);
  }
  out.strings.push_back(text);
 }
 bool contains(const char* text)const{for(const auto& s:strings)if(s==text)return true;return false;}
};
int main(){
 for(unsigned winner=0;winner<3;++winner){
  RaceResultsFlow flow;BattleResultPanel panel;FlowDrawer drawer;flow.finish();
  RaceResultEntry rows[]={{0,0,true,0,uint16_t(winner==1?1:5),RaceTeam::Red},{1,1,false,0,uint16_t(winner==0?1:5),RaceTeam::Blue}};
  bool titleChecked=false,entryChecked=false,exitChecked=false;
  const char* labels[]={"Theredteamwins!","Theblueteamwins!","Tie"};
  for(unsigned tick=0;tick<1500;++tick){
   const bool advance=flow.phase()==RaceFlowPhase::Update&&flow.pageFrame()>=100;
   const auto events=flow.step({advance,false});
   if(events&RaceResultsFlow::EventFill){flow.fill(rows,2,false);flow.setBattle(2,1,3);}
   panel.step(flow);
   if(flow.phase()==RaceFlowPhase::UpdateIn){
    Capture capture;panel.draw(flow,drawer,Capture::collect,&capture);
    if(flow.pageFrame()<20){assert(capture.strings.empty());entryChecked=true;}
    if(flow.pageFrame()==30){assert(capture.contains(labels[winner]));assert(!capture.contains("wins"));titleChecked=true;}
   }
   if(flow.phase()==RaceFlowPhase::UpdateOut&&flow.pageFrame()==0){
    Capture capture;panel.draw(flow,drawer,Capture::collect,&capture);
    assert(capture.contains(labels[winner]));exitChecked=true; // parent clock does not restart on exit
   }
   if(flow.phase()==RaceFlowPhase::Total)break;
  }
  assert(flow.phase()==RaceFlowPhase::Total&&flow.battle()&&titleChecked&&entryChecked&&exitChecked);
  for(unsigned tick=0;tick<15;++tick){flow.step({});panel.step(flow);}
  Capture capture;panel.draw(flow,drawer,Capture::collect,&capture);
  // textQuads emits no quad for spaces, hence comparison without spaces.
  for(const auto* label:labels)assert(!capture.contains(label));
  assert(capture.contains("wins")&&capture.contains("2")&&capture.contains("1"));
  assert(!capture.contains("3")); // current-round points never replace set wins
  panel.reset();capture.strings.clear();panel.draw(flow,drawer,Capture::collect,&capture);assert(capture.strings.empty());
 }
 assert(FlowBattleColumns[0].x==-150&&FlowBattleColumns[1].x==150&&FlowBattleColumns[0].y==-20);
 assert(FlowBattleColumns[0].delay==45&&FlowBattleColumns[1].delay==50&&FlowBattleWinControl.delay==10);
 // Native held-coin presentation partitions into ten-coin columns and caps
 // only its visual count. Exact model-space positions/scales are independent
 // of the future UI-camera adapter; no racer receives a fake world stack.
 CoinHudStack stack;
 for(bool wide:{false,true})for(unsigned score=0;score<=300;++score){
  stack.update(score,wide);assert(stack.shown()==std::min(score,100u));unsigned total=0;
  for(unsigned i=0;i<10;++i){const auto& c=stack.column(i);
   if(i<stack.size()){assert(c.coins>=1&&c.coins<=10&&c.srtFrame()==c.coins-1);total+=c.coins;
    assert(c.scale.x>0&&c.scale.y>0&&c.scale.z>0&&std::isfinite(c.position.x)&&c.position.z>=1000);
   }else assert(c.coins==0);
  }
  assert(total==stack.shown());if(stack.size()){
   assert(stack.column(0).scale.x*140*stack.size()<=(wide?105.f:95.f)+.001f);
   assert(std::fabs((stack.column(0).position.x+stack.column(stack.size()-1).position.x)*.5f+290)<.001f);}
 }
 stack.update(1);assert(stack.size()==1&&stack.column(0).scale.x==.36f&&stack.column(0).srtFrame()==0);
 stack.update(11);assert(stack.size()==2&&stack.column(0).coins==10&&stack.column(1).coins==1);
 stack.update(100);assert(stack.size()==10&&stack.column(9).coins==10);
 std::puts("PASS production Battle result panel title/material/text quads for red/blue/draw, native BT columns and independent set wins, held-coin original visual partition/scale/pose/SRT selection; target presentation pending");
}
