#include <mkw/menu_text_motion.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace mkw {
namespace {
struct Affine {float a=1,b=0,c=0,d=1,x=0,y=0;};
Affine multiply(const Affine& p,const Affine& q){
 return {p.a*q.a+p.b*q.c,p.a*q.b+p.b*q.d,p.c*q.a+p.d*q.c,p.c*q.b+p.d*q.d,
         p.a*q.x+p.b*q.y+p.x,p.c*q.x+p.d*q.y+p.y};
}
// The cooked pane tables never change: each pane's leaf node and each node's
// parent are looked up once (the same first-match rows as uiFindPaneNode).
// Entries hold row+1 (0: not yet looked up); the menu thread and the job
// worker (prefetch residency) may fill one at once, with the same value.
constexpr uint16_t Missing=0xfffe;
const UiPaneNode* nodeAt(uint16_t i){return i<std::size(UiPaneNodes)?&UiPaneNodes[i]:nullptr;}
uint16_t nodeRow(const UiPaneNode* node){return node?uint16_t(node-UiPaneNodes):Missing;}
const UiPaneNode* leafNode(const UiPaneInfo& pane){
 static std::atomic<uint16_t> leaf[std::size(UiPanes)];
 const auto at=uintptr_t(&pane)-uintptr_t(UiPanes);
 const bool cached=uintptr_t(&pane)>=uintptr_t(UiPanes)&&at/sizeof(UiPaneInfo)<std::size(UiPanes)&&at%sizeof(UiPaneInfo)==0;
 if(cached)if(const uint16_t row=leaf[at/sizeof(UiPaneInfo)].load(std::memory_order_relaxed))return nodeAt(row-1);
 char panel[64];const size_t n=std::min(std::strlen(pane.panel),sizeof panel-1);std::memcpy(panel,pane.panel,n);panel[n]=0;
 for(const char* suffix:{"_free","_select","_ok"}){
  const auto length=std::strlen(panel),end=std::strlen(suffix);
  if(length>=end&&!std::strcmp(panel+length-end,suffix)){panel[length-end]=0;break;}
 }
 const auto* node=uiFindPaneNode(panel,pane.name);
 if(cached)leaf[at/sizeof(UiPaneInfo)].store(uint16_t(nodeRow(node)+1),std::memory_order_relaxed);
 return node;
}
// A node's panel is the name it was found under, so its parent shares it.
const UiPaneNode* parentNode(const UiPaneNode& node){
 static std::atomic<uint16_t> parent[std::size(UiPaneNodes)];
 auto& cached=parent[&node-UiPaneNodes];
 uint16_t row=cached.load(std::memory_order_relaxed);
 if(!row){row=uint16_t(nodeRow(uiFindPaneNode(node.panel,node.parent))+1);cached.store(row,std::memory_order_relaxed);}
 return nodeAt(row-1);
}
float value(std::initializer_list<UiTextClipFrame> clips,const char* node,const char* kind,unsigned target,float fallback,bool reference){
 for(const auto& entry:clips)if(entry.clip){
  const auto* clip=reference&&entry.referenceClip?entry.referenceClip:entry.clip;
  fallback=uiAnimationValue(*clip,node,kind,target,reference?entry.reference:entry.frame,fallback);
 }
 return fallback;
}
// Whether any clip has a track for this pane; without one, every value()
// above returns its fallback unchanged.
bool animated(std::initializer_list<UiTextClipFrame> clips,const char* node,bool reference){
 for(const auto& entry:clips)if(entry.clip){
  const auto& clip=reference&&entry.referenceClip?*entry.referenceClip:*entry.clip;
  for(unsigned i=0;i<clip.trackCount;++i)if(!std::strcmp(UiAnimationTracks[clip.firstTrack+i].element,node))return true;
 }
 return false;
}
}
UiTextMotion uiTextMotion(const UiPaneInfo& pane,std::initializer_list<UiTextClipFrame> clips){
 UiTextMotion result;
 bool tracks=false;for(const auto& entry:clips)tracks|=entry.clip&&entry.clip->trackCount;
 if(!tracks)return result;
 std::array<const UiPaneNode*,32> chain{};unsigned count=0;
 const auto* node=leafNode(pane);
 while(node&&count<chain.size()){
  chain[count++]=node;
  if(!*node->parent)break;
  node=parentNode(*node);
 }
 if(!count||!node||*chain[count-1]->parent)return result;
 Affine poses[2];float alpha[2]={1,1};bool visible=true;
 for(unsigned mode=0;mode<2;++mode){
  const bool reference=mode==0;
  for(unsigned i=count;i-->0;){
   const auto& n=*chain[i];const bool live=animated(clips,n.name,reference);
   auto v=[&](unsigned target,float fallback){return live?value(clips,n.name,"RLPA",target,fallback,reference):fallback;};
   // cos(+-0) is exactly 1 and sin(+-0) is +-0: skip the libm calls then.
   const float radians=v(5,n.rz)*.017453292519943295f,co=radians==0?1.f:std::cos(radians),si=radians==0?radians:std::sin(radians);
   float sx=v(6,n.sx),sy=v(7,n.sy);
   if(n.flags&4)sx*=608.f/832.f;
   poses[mode]=multiply(poses[mode],{co*sx,-si*sy,si*sx,co*sy,v(0,n.x),v(1,n.y)});
   const float own=std::clamp(live?value(clips,n.name,"RLVC",16,n.alpha*255.f,reference):n.alpha*255.f,0.f,255.f)/255.f;
   // InfluencedAlpha controls propagation to children. A pane's own draw
   // always uses its alpha multiplied by the inherited ancestor alpha.
   // A non-propagating intermediary retains the inherited value for its
   // descendants; it does not erase the ancestors' fade.
   if(i==0||(n.flags&2))alpha[mode]*=own;
   if(!reference)visible&=(live?value(clips,n.name,"RLVI",0,float(n.flags&1),false):float(n.flags&1))>0;
  }
 }
 const auto& r=poses[0];const auto& p=poses[1];const float det=r.a*r.d-r.b*r.c;
 if(std::fabs(det)<1e-8f)return result;
 const Affine inverse{r.d/det,-r.b/det,-r.c/det,r.a/det,(r.b*r.y-r.d*r.x)/det,(r.c*r.x-r.a*r.y)/det};
 const auto delta=multiply(p,inverse);
 // UiQuad currently represents axis-aligned panes. Rotating text needs the
 // general corner submission path; leave that unsupported path unchanged.
 if(std::fabs(delta.b)>1e-5f||std::fabs(delta.c)>1e-5f)return result;
 const auto& leaf=*chain[0];
 const float localX=(1.f-float(leaf.origin%3))*leaf.w*.5f;
 const float localY=(float(leaf.origin/3)-1.f)*leaf.h*.5f;
 const float offsetX=pane.x+pane.w*.5f-(r.a*localX+r.b*localY+r.x);
 const float offsetY=-pane.y-pane.h*.5f-(r.c*localX+r.d*localY+r.y);
 result.sx=delta.a;result.sy=delta.d;
 result.tx=delta.x+offsetX*(1.f-delta.a);result.ty=delta.y+offsetY*(1.f-delta.d);
 result.absoluteAlpha=alpha[0]==0;
 result.alpha=alpha[0]>0?alpha[1]/alpha[0]:alpha[1];
 result.visible=visible;
 result.absolute=alpha[1];result.evaluated=true;
 return result;
}
}
