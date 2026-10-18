#include <mkw/race_flow_draw.hpp>
#include <mkw/race_flow_layout.hpp>
#include <mkw/race_flow_pose.hpp>
#include <mkw/race_results.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
// Race overlays share the HUD's mapping: 456 layout units span the 240-pixel
// screen height, uniformly, about the screen centre (y up in the layout).
constexpr float Unit=240.f/456.f;

uint32_t pack(const std::array<float,4>& c){
 auto b=[](float v){return uint32_t(std::clamp(std::lround(v),0l,255l));};
 return (b(c[0])<<24)|(b(c[1])<<16)|(b(c[2])<<8)|b(c[3]);
}
uint32_t withAlpha(uint32_t rgba,float alpha){
 return (rgba&0xffffff00u)|uint32_t(std::lround(std::clamp(alpha,0.f,1.f)*float(rgba&0xff)));
}
const FlowGlyph* glyph(const FlowFont& font,char c){
 for(unsigned i=0;i<font.glyphCount;++i)if(font.glyphs[i].code==char16_t(uint8_t(c)))return &font.glyphs[i];
 return nullptr;
}

// A control instance on screen: layout position (x, y) and scale.
struct Placement {float x,y,sx,sy;};


void place(const FlowPanePose& pane,const Placement& at,float lx,float ly,float& sx,float& sy){
 const float x=pane.m[0]*lx+pane.m[1]*ly+pane.m[2],y=pane.m[3]*lx+pane.m[4]*ly+pane.m[5];
 sx=200.f+(at.x+x*at.sx)*Unit;sy=120.f-(at.y+y*at.sy)*Unit;
}

// NW4R picture pane: origin-relative rectangle, texcoords through the
// material's texture SRT (about the texture centre).
template<class Batch>
void pictureQuad(Batch& batch,const FlowPane& def,const FlowPanePose& pane,const FlowMaterialPose& material,const Placement& at,
                 const float* uvRect=nullptr){
 auto* q=batch.add();if(!q)return;
 const float left=-float(def.origin%3)*pane.w*.5f,top=float(def.origin/3)*pane.h*.5f;
 const float lx[4]={left,left+pane.w,left,left+pane.w},ly[4]={top,top,top-pane.h,top-pane.h};
 const auto& srt=material.srt;
 const float r=srt[2]*3.14159265358979323846f/180.f,c=std::cos(r),s=std::sin(r);
 for(unsigned k=0;k<4;++k){
  place(pane,at,lx[k],ly[k],q->x[k],q->y[k]);
  float u=def.uv[k*2],v=def.uv[k*2+1];
  if(uvRect){u=uvRect[0]+u*(uvRect[2]-uvRect[0]);v=uvRect[1]+v*(uvRect[3]-uvRect[1]);}
  else{
   const float du=u-.5f,dv=v-.5f;
   u=srt[3]*c*du-srt[4]*s*dv+srt[0]+.5f;v=srt[3]*s*du+srt[4]*c*dv+srt[1]+.5f;
  }
  q->u[k]=u;q->v[k]=v;q->color[k]=withAlpha(pane.vertex[k],pane.alpha);
 }
}

// Section::Draw keeps LEQUAL depth with writes: an earlier pane with a larger
// z hides this one inside its rectangle. For axis-aligned picture panes the
// visible part is the pane's rectangle minus those occluders, drawn as
// sub-rectangles whose texcoords and colours interpolate the pane's corners.
template<class Batch>
void occludedPicture(Batch& batch,const FlowPane& def,const FlowPanePose& pane,const FlowMaterialPose& material,const Placement& at,
                     const FlowOccluder* occluders,unsigned count,float z,FlowOccluder& self){
 struct Whole {FlowQuad quad;unsigned count=0;FlowQuad* add(){return count?nullptr:(++count,&quad);}} whole;
 pictureQuad(whole,def,pane,material,at);
 const auto& q=whole.quad;
 const float x0=std::min(q.x[0],q.x[1]),x1=std::max(q.x[0],q.x[1]),y0=std::min(q.y[0],q.y[2]),y1=std::max(q.y[0],q.y[2]);
 self={x0,y0,x1,y1,z};
 const bool axisAligned=std::fabs(q.x[0]-q.x[2])<.01f&&std::fabs(q.y[0]-q.y[1])<.01f;
 struct Box {float x0,y0,x1,y1;};
 std::array<Box,16> parts{};unsigned n=0;parts[n++]={x0,y0,x1,y1};
 for(unsigned o=0;o<count&&axisAligned;++o){
  const auto& c=occluders[o];if(c.z<=z)continue; // LEQUAL: equal depths still pass
  std::array<Box,16> next{};unsigned m=0;
  for(unsigned i=0;i<n;++i){
   const Box b=parts[i];
   if(c.x1<=b.x0||c.x0>=b.x1||c.y1<=b.y0||c.y0>=b.y1){if(m<next.size())next[m++]=b;continue;}
   const float ix0=std::max(b.x0,c.x0),ix1=std::min(b.x1,c.x1),iy0=std::max(b.y0,c.y0),iy1=std::min(b.y1,c.y1);
   const Box pieces[4]={{b.x0,b.y0,b.x1,iy0},{b.x0,iy1,b.x1,b.y1},{b.x0,iy0,ix0,iy1},{ix1,iy0,b.x1,iy1}};
   for(const auto& piece:pieces)if(piece.x1-piece.x0>.01f&&piece.y1-piece.y0>.01f&&m<next.size())next[m++]=piece;
  }
  parts=next;n=m;
 }
 // Corner values of the pane (TL, TR, BL, BR) at normalized (s, t).
 const bool flipX=q.x[1]<q.x[0],flipY=q.y[2]<q.y[0];
 auto lerp4=[&](const float* v,float s,float t){return v[0]*(1-s)*(1-t)+v[1]*s*(1-t)+v[2]*(1-s)*t+v[3]*s*t;};
 for(unsigned i=0;i<n;++i){
  auto* out=batch.add();if(!out)return;
  const Box& b=parts[i];
  const float xs[4]={b.x0,b.x1,b.x0,b.x1},ys[4]={b.y0,b.y0,b.y1,b.y1};
  for(unsigned k=0;k<4;++k){
   float s=x1>x0?(xs[k]-x0)/(x1-x0):0,t=y1>y0?(ys[k]-y0)/(y1-y0):0;
   if(flipX)s=1-s;if(flipY)t=1-t;
   out->x[k]=xs[k];out->y[k]=ys[k];out->u[k]=lerp4(q.u,s,t);out->v[k]=lerp4(q.v,s,t);
   uint32_t colour=0;
   for(unsigned shift:{24u,16u,8u,0u}){
    const float c[4]={float((q.color[0]>>shift)&255),float((q.color[1]>>shift)&255),float((q.color[2]>>shift)&255),float((q.color[3]>>shift)&255)};
    colour|=uint32_t(std::clamp(std::lround(lerp4(c,s,t)),0l,255l))<<shift;
   }
   out->color[k]=colour;
  }
 }
}

// NW4R TextBox, one line: fontSize / BRFNT width,height scales each glyph;
// the text position places the line in the box; glyphs carry the top and
// bottom vertex colours (per-glyph vertical gradient).
template<class Batch>
void textQuads(Batch& batch,const FlowPane& def,const FlowPanePose& pane,const Placement& at,const char* text,const FlowFont& font=FlowKanjiFont){
 if(!text||!*text)return;
 const float scaleX=def.fontW/float(font.width),scaleY=def.fontH/float(font.height);
 float width=0;unsigned n=0;
 for(const char* p=text;*p;++p)if(const auto* g=glyph(font,*p)){width+=float(g->advance)*scaleX;++n;}
 if(n>1)width+=def.charSpace*float(n-1);
 const float lineH=float(font.linefeed)*scaleY,cellH=float(font.cellH)*scaleY;
 const float left=-float(def.origin%3)*pane.w*.5f,top=float(def.origin/3)*pane.h*.5f;
 static constexpr float Align[3]={0,.5f,1};
 float x=left+(pane.w-width)*Align[def.textPosition%3];
 const float y=top-(pane.h-lineH)*Align[std::min(def.textPosition/3,2)];
 const uint32_t topColor=withAlpha(pane.vertex[0],pane.alpha),bottomColor=withAlpha(pane.vertex[2],pane.alpha);
 for(const char* p=text;*p;++p){
  const auto* g=glyph(font,*p);if(!g)continue;
  if(g->w>0){
   auto* q=batch.add();if(!q)return;
   const float gx=x+float(g->left)*scaleX,gw=float(g->w)*scaleX;
   const float lx[4]={gx,gx+gw,gx,gx+gw},ly[4]={y,y,y-cellH,y-cellH};
   const float u0=float(g->x)/float(font.atlasW),u1=float(g->x+g->w)/float(font.atlasW);
   const float v0=float(g->y)/float(font.atlasH),v1=float(g->y+font.cellH)/float(font.atlasH);
   const float us[4]={u0,u1,u0,u1},vs[4]={v0,v0,v1,v1};
   for(unsigned k=0;k<4;++k){place(pane,at,lx[k],ly[k],q->x[k],q->y[k]);q->u[k]=us[k];q->v[k]=vs[k];q->color[k]=k<2?topColor:bottomColor;}
  }
  x+=float(g->advance)*scaleX+def.charSpace;
 }
}
}


void FlowDrawer::reset(){boardFrame=0;menuStarted=false;lastCursor=0;chosen=-1;batch.count=0;}

void FlowDrawer::flush(FlowSink sink,void* context,int texture,uint8_t wrapS,uint8_t wrapT,uint32_t c0,uint32_t c1){
 if(!batch.count)return;
 sink({texture,wrapS,wrapT,c0,c1,batch.quads.data(),batch.count},context);
 batch.count=0;
}

void FlowDrawer::step(const RaceResultsFlow& flow){
 auto& m=*this;
 const auto& def=FlowButtonLayout;
 m.boardFrame=flow.boardVisible()?m.boardFrame+1:0;
 if(!flow.menuVisible()){m.menuStarted=false;m.chosen=-1;return;}
 const unsigned count=flow.buttonCount(),cursor=flow.cursor();
 if(!m.menuStarted){
  // RaceMenu::OnInit: PushButton::SelectInitial(0); the others stay free.
  for(unsigned b=0;b<count;++b){
   auto& p=m.buttons[b].plays;const bool selected=b==cursor;
   p[0]=flowPlay(def,"Loop","Loop");
   p[1]=flowPlay(def,"Select",selected?"FreeToSelect":"Free");
   p[2]=flowPlay(def,"SelectIn",selected?"SelectIn":"SelectStop");
   p[3]=flowPlay(def,"OK","OKStop");
  }
  m.menuStarted=true;m.lastCursor=cursor;m.chosen=-1;
 }else if(cursor!=m.lastCursor&&cursor<count){
  auto& old=m.buttons[m.lastCursor].plays;auto& now=m.buttons[cursor].plays;
  old[1]=flowPlay(def,"Select","SelectToFree");old[2]=flowPlay(def,"SelectIn","SelectStop");
  now[1]=flowPlay(def,"Select","FreeToSelect");now[2]=flowPlay(def,"SelectIn","SelectIn");
  m.lastCursor=cursor;
 }
 if(flow.chosen()>=0&&m.chosen<0&&unsigned(flow.chosen())<count){
  m.chosen=flow.chosen();m.buttons[m.chosen].plays[3]=flowPlay(def,"OK","OK");
 }
 for(unsigned b=0;b<count;++b)for(auto& p:m.buttons[b].plays)flowAdvance(def,p,1);
}

void FlowDrawer::drawDemo(const FlowDemoCard& card,FlowSink sink,void* context){
 auto& m=*this;
 // DemoPage's controls in insert order: course_name, then cup_name. Neither
 // animates, so each draws its layout's own pane values.
 auto control=[&](const FlowLayoutDef& def,const FlowControl& c){
  const Placement at{c.x,c.y,c.sx,c.sy};
  auto& pose=m.poses[0];pose.evaluate(def,nullptr,0);
  for(unsigned p=0;p<def.paneCount&&p<FlowPose::MaxPanes;++p){
   const auto& pane=def.panes[p];const auto& pp=pose.panes[p];
   if(pane.kind==0||!pp.visible||pp.alpha<=0)continue;
   const auto& material=pose.materials[pane.material];
   if(pane.kind==1){if(card.icon<0)continue;pictureQuad(m.batch,pane,pp,material,at);}
   else textQuads(m.batch,pane,pp,at,std::strcmp(pane.name,"cup_name")?card.course:card.cup,FlowDemoFont);
   const int texture=pane.kind==2?FlowDemoFont.texture:def.materials[pane.material].texture;
   m.flush(sink,context,texture,def.materials[pane.material].wrapS,def.materials[pane.material].wrapT,pack(material.c0),pack(material.c1));
  }
 };
 if(card.course)control(FlowCourseNameLayout,FlowCourseNameControl);
 // Section 0x1D hides cup_name (UIControl +0x80).
 if(card.cupVisible)control(FlowCupNameLayout,FlowCupNameControl);
}

void FlowDrawer::draw(const RaceResultsFlow& flow,FlowSink sink,void* context){
 auto& m=*this;
 char numbers[12][2][8];
 if(flow.boardVisible()){
  // CtrlRaceResult::Fill 0x807EB9C4: the local player's row plays selectOn and
  // its HUD slot colour (1P); CPU rows selectOff and Other. Fade stays on Main.
  const auto& def=FlowResultLayout;
  auto& poses=m.poses;
  for(unsigned human=0;human<2;++human){
   FlowPlay plays[5]={flowPlay(def,"Fade","Main"),flowPlay(def,"Loop","loop"),flowPlay(def,"Select",human?"selectOn":"selectOff"),
                      flowPlay(def,"Team","off"),flowPlay(def,"Player",human?"1P":"Other")};
   for(auto& p:plays)flowAdvance(def,p,float(m.boardFrame));
   poses[human].evaluate(def,plays,5);
  }
  const unsigned rows=flow.rowCount();
  std::array<Placement,12> at{};
  for(unsigned i=0;i<rows;++i){
   const auto& c=FlowResultRows[i];
   at[i]={c.x+flow.slideOffset(c.delay),c.y,c.sx,c.sy};
   std::snprintf(numbers[i][0],sizeof numbers[i][0],"+%u",unsigned(flow.row(i).gain));
   std::snprintf(numbers[i][1],sizeof numbers[i][1],"%d",flow.count(i).shown);
  }
  static const int iconPane=flowPaneIndex(def,"chara_icon"),shadowPane=flowPaneIndex(def,"chara_icon_sha");
  for(unsigned p=0;p<def.paneCount&&p<FlowPose::MaxPanes;++p){
   const auto& pane=def.panes[p];if(pane.kind==0)continue;
   for(unsigned human=0;human<2;++human){
    const auto& pose=poses[human].panes[p];
    if(!pose.visible||pose.alpha<=0)continue;
    const auto& material=poses[human].materials[pane.material];
    for(unsigned i=0;i<rows;++i){
     if(std::fabs(at[i].x)>680.f)continue; // the whole row is off screen (300 + 380 units)
     const auto& row=flow.row(i);if(row.human!=bool(human))continue;
     if(pane.kind==1){
      if(int(p)==iconPane||int(p)==shadowPane){
       const auto& r=FlowIconRects[row.character<24?row.character:24];
       const float rect[4]={float(r[0])/FlowIconAtlas[0],float(r[1])/FlowIconAtlas[1],float(r[0]+r[2])/FlowIconAtlas[0],float(r[1]+r[3])/FlowIconAtlas[1]};
       pictureQuad(m.batch,pane,pose,material,at[i],rect);
      }else pictureQuad(m.batch,pane,pose,material,at[i]);
     }else{
      const char* text=nullptr;const char* name=pane.name;
      if(!std::strcmp(name,"position"))text=FlowPositionText[std::min(i,11u)];
      else if(!std::strcmp(name,"player_name"))text=FlowCharacterNames[std::min<unsigned>(row.character,23)];
      else if(!std::strcmp(name,"get_point"))text=flow.totals()?nullptr:numbers[i][0];
      else if(!std::strcmp(name,"total_score"))text=numbers[i][1];
      else if(!std::strcmp(name,"total_point"))text=FlowPointText[flow.count(i).shown==1?0:1];
      textQuads(m.batch,pane,pose,at[i],text);
     }
    }
    const int texture=pane.kind==2?FlowKanjiFont.texture:def.materials[pane.material].texture;
    m.flush(sink,context,texture,def.materials[pane.material].wrapS,def.materials[pane.material].wrapT,pack(material.c0),pack(material.c1));
   }
  }
 }
 if(flow.menuVisible()&&m.menuStarted){
  // VSRaceEnd: AfterMenuVS (Next Race, Quit Game) or AfterMenuVSLast (Next).
  const auto& def=FlowButtonLayout;
  static const int fuchi=flowMaterialIndex(def,"fuchi_pattern"),base=flowMaterialIndex(def,"color_base");
  const unsigned count=flow.buttonCount();
  for(unsigned b=0;b<count;++b){
   const auto& c=FlowButtons[flow.finalRace()?2:b];
   const Placement at{c.x+flow.slideOffset(c.delay),c.y,c.sx,c.sy};
   auto& pose=m.buttonPose;pose.evaluate(def,m.buttons[b].plays.data(),4);
   // ControlManipulator palette: slot 1 when player 1 points at the button.
   const auto& palette=FlowButtonPalette[b==flow.cursor()?0:1];
   for(unsigned k=0;k<4;++k){
    if(fuchi>=0){pose.materials[fuchi].c0[k]=palette[0][k];pose.materials[fuchi].c1[k]=palette[1][k];}
    if(base>=0){pose.materials[base].c0[k]=palette[2][k];pose.materials[base].c1[k]=palette[2][k];}
   }
   const char* label=FlowButtonText[flow.finalRace()?2:b];
   auto& occluders=m.occluders;unsigned occluded=0;
   for(unsigned p=0;p<def.paneCount&&p<FlowPose::MaxPanes;++p){
    const auto& pane=def.panes[p];const auto& pp=pose.panes[p];
    if(pane.kind==0||!pp.visible||pp.alpha<=0)continue;
    const auto& material=pose.materials[pane.material];
    // Text glyphs write depth too, but no later pane of this layout lies
    // below a text box, so only picture panes occlude here.
    if(pane.kind==1)occludedPicture(m.batch,pane,pp,material,at,occluders.data(),occluded,pp.z,occluders[occluded]),++occluded;
    else textQuads(m.batch,pane,pp,at,label);
    const int texture=pane.kind==2?FlowKanjiFont.texture:def.materials[pane.material].texture;
    m.flush(sink,context,texture,def.materials[pane.material].wrapS,def.materials[pane.material].wrapT,pack(material.c0),pack(material.c1));
   }
  }
 }
}
}
