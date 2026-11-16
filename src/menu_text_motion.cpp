#include <mkw/menu_text_motion.hpp>
#include <mkw/diag.hpp> // also <3ds.h>, or host stand-ins for LightLock
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
struct Affine {float a=1,b=0,c=0,d=1,x=0,y=0;};
Affine multiply(const Affine& p,const Affine& q){
 return {p.a*q.a+p.b*q.c,p.a*q.b+p.b*q.d,p.c*q.a+p.d*q.c,p.c*q.b+p.d*q.d,
         p.a*q.x+p.b*q.y+p.x,p.c*q.x+p.d*q.y+p.y};
}
float value(std::initializer_list<UiTextClipFrame> clips,const char* node,const char* kind,unsigned target,float fallback,bool reference){
 for(const auto& entry:clips)if(entry.clip){
  const auto* clip=reference&&entry.referenceClip?entry.referenceClip:entry.clip;
  fallback=uiAnimationValue(*clip,node,kind,target,reference?entry.reference:entry.frame,fallback);
 }
 return fallback;
}
}
static UiTextMotion computeTextMotion(const UiPaneInfo& pane,std::initializer_list<UiTextClipFrame> clips,bool unitLeafReference){
 UiTextMotion result;
 bool tracks=false;for(const auto& entry:clips)tracks|=entry.clip&&entry.clip->trackCount;
 if(!tracks)return result;
 char panel[64];
 if(diag::menuFast){size_t n=0;for(;n<sizeof panel-1&&pane.panel[n];++n)panel[n]=pane.panel[n];panel[n]=0;} // snprintf("%s")'s truncation
 else std::snprintf(panel,sizeof panel,"%s",pane.panel);
 for(const char* suffix:{"_free","_select","_ok","_selectin"}){
  const auto length=std::strlen(panel),end=std::strlen(suffix);
  if(length>=end&&!std::strcmp(panel+length-end,suffix)){panel[length-end]=0;break;}
 }
 std::array<const UiPaneNode*,32> chain{};unsigned count=0;
 const auto* node=uiFindPaneNode(panel,pane.name);
 while(node&&count<chain.size()){
  chain[count++]=node;
  if(!*node->parent)break;
  node=uiFindPaneNode(panel,node->parent);
 }
 if(!count||!node||*chain[count-1]->parent)return result;
 const auto& font=*chain[0];
 for(const auto& entry:clips)if(entry.clip)for(unsigned i=0;i<entry.clip->trackCount;++i){
  const auto& t=UiAnimationTracks[entry.clip->firstTrack+i];
  result.fontMaterial|=(!std::strcmp(t.kind,"RLMC")&&!std::strcmp(t.element,font.material)&&t.target>=4&&t.target<12)||
                      (!std::strcmp(t.kind,"RLVC")&&!std::strcmp(t.element,font.name)&&t.target<16);
 }
 if(result.fontMaterial){
  auto byte=[&](const char* element,const char* kind,unsigned target,float fallback){
   return unsigned(std::clamp(std::lround(value(clips,element,kind,target,fallback,false)),0l,255l));
  };
  for(unsigned reg=0;reg<2;++reg){unsigned color=0;
   for(unsigned channel=0;channel<4;++channel)color=(color<<8)|byte(font.material,"RLMC",4+reg*4+channel,font.materialColors[reg][channel]);
   (reg?result.color1:result.color0)=color;
  }
  for(unsigned corner=0;corner<4;++corner){unsigned color=0;
   for(unsigned channel=0;channel<4;++channel)color=(color<<8)|byte(font.name,"RLVC",corner*4+channel,float((font.vertexColors[corner]>>(24-channel*8))&255));
   result.corners[corner]=color;
  }
 }
 // DIAG (menuFast): a node no clip track names keeps every authored value,
 // so its lookups are skipped. Same result: value() would return each fallback.
 std::array<bool,32> live{};
 for(unsigned i=0;i<count;++i){live[i]=!diag::menuFast;
  for(const auto& e:clips)for(const UiAnimationClip* c:{e.clip,e.referenceClip})if(c&&!live[i])
   for(unsigned k=0;k<c->trackCount;++k)if(!std::strcmp(UiAnimationTracks[c->firstTrack+k].element,chain[i]->name)){live[i]=true;break;}}
 Affine poses[2];float alpha[2]={1,1};bool visible=true;
 for(unsigned mode=0;mode<2;++mode){
  const bool reference=mode==0;
  for(unsigned i=count;i-->0;){
   const auto& n=*chain[i];
   auto v=[&](unsigned target,float fallback){return live[i]?value(clips,n.name,"RLPA",target,fallback,reference):fallback;};
   // cos(+-0)=1 and sin(+-0)=+-0 exactly: no library call for unrotated panes.
   const float radians=v(5,n.rz)*.017453292519943295f;
   const bool still=diag::menuFast&&radians==0.f;
   const float co=still?1.f:std::cos(radians),si=still?radians:std::sin(radians);
   float sx=v(6,n.sx),sy=v(7,n.sy);
   if(reference&&unitLeafReference&&i==0)sx=sy=1;
   if(n.flags&4)sx*=608.f/832.f;
   poses[mode]=multiply(poses[mode],{co*sx,-si*sy,si*sx,co*sy,v(0,n.x),v(1,n.y)});
   const float own=reference&&unitLeafReference&&i==0?1.f:
       std::clamp(live[i]?value(clips,n.name,"RLVC",16,n.alpha*255.f,reference):n.alpha*255.f,0.f,255.f)/255.f;
   // InfluencedAlpha controls propagation to children. A pane's own draw
   // always uses its alpha multiplied by the inherited ancestor alpha.
   // A non-propagating intermediary retains the inherited value for its
   // descendants; it does not erase the ancestors' fade.
   if(i==0||(n.flags&2))alpha[mode]*=own;
   if(!reference)visible&=(live[i]?value(clips,n.name,"RLVI",0,float(n.flags&1),false):float(n.flags&1))>0;
  }
 }
 const auto& r=poses[0];const auto& p=poses[1];const float det=r.a*r.d-r.b*r.c;
 result.absoluteAlpha=alpha[0]==0;
 result.alpha=alpha[0]>0?alpha[1]/alpha[0]:alpha[1];
 result.visible=visible;
 if(std::fabs(det)<1e-8f)return result;
 const Affine inverse{r.d/det,-r.b/det,-r.c/det,r.a/det,(r.b*r.y-r.d*r.x)/det,(r.c*r.x-r.a*r.y)/det};
 const auto delta=multiply(p,inverse);

 const auto& leaf=*chain[0];
 const float localX=(1.f-float(leaf.origin%3))*leaf.w*.5f;
 const float localY=(float(leaf.origin/3)-1.f)*leaf.h*.5f;
 const float offsetX=pane.x+pane.w*.5f-(r.a*localX+r.b*localY+r.x);
 const float offsetY=-pane.y-pane.h*.5f-(r.c*localX+r.d*localY+r.y);
 result.sx=delta.a;result.sy=delta.d;result.xy=delta.b;result.yx=delta.c;
 result.tx=delta.x+offsetX*(1.f-delta.a)-delta.b*offsetY;result.ty=delta.y+offsetY*(1.f-delta.d)-delta.c*offsetX;
 return result;
}
// DIAG (menuFast): uiTextMotion is a pure function of its arguments and the
// static layout tables, so results are kept by their exact arguments. The
// unfocused buttons of a settled page repeat theirs every frame.
namespace {
struct MotionKey {
 const UiPaneInfo* pane=nullptr;bool unit=false;unsigned count=0;std::array<UiTextClipFrame,4> clips{};
 bool operator==(const MotionKey& o)const{
  if(pane!=o.pane||unit!=o.unit||count!=o.count)return false;
  for(unsigned i=0;i<count;++i){const auto& a=clips[i];const auto& b=o.clips[i];
   if(a.clip!=b.clip||a.referenceClip!=b.referenceClip||std::memcmp(&a.frame,&b.frame,4)||std::memcmp(&a.reference,&b.reference,4))return false;}
  return true;
 }
};
struct MotionCache {
 struct Entry {MotionKey key;UiTextMotion value;bool used=false;};
 std::array<Entry,512> entries{};LightLock lock;
 MotionCache(){LightLock_Init(&lock);}
};
MotionCache& motionCache(){static MotionCache cache;return cache;}
uint32_t motionHash(const MotionKey& k){
 uint32_t h=2166136261u;auto mix=[&](const void* p,size_t n){for(size_t i=0;i<n;++i){h^=static_cast<const uint8_t*>(p)[i];h*=16777619u;}};
 mix(&k.pane,sizeof k.pane);mix(&k.unit,1);
 for(unsigned i=0;i<k.count;++i){const auto& c=k.clips[i];mix(&c.clip,sizeof c.clip);mix(&c.referenceClip,sizeof c.referenceClip);mix(&c.frame,4);mix(&c.reference,4);}
 return h;
}
}
UiTextMotion uiTextMotion(const UiPaneInfo& pane,std::initializer_list<UiTextClipFrame> clips,bool unitLeafReference){diag::ComposeTimer diagTimer_(20);
 if(!diag::menuFast||clips.size()>4)return computeTextMotion(pane,clips,unitLeafReference);
 MotionKey key;key.pane=&pane;key.unit=unitLeafReference;
 for(const auto& c:clips)key.clips[key.count++]=c;
 auto& cache=motionCache();auto& entry=cache.entries[motionHash(key)&(cache.entries.size()-1)];
 LightLock_Lock(&cache.lock);
 if(entry.used&&entry.key==key){const auto value=entry.value;LightLock_Unlock(&cache.lock);return value;}
 LightLock_Unlock(&cache.lock);
 const auto value=computeTextMotion(pane,clips,unitLeafReference);
 LightLock_Lock(&cache.lock);entry.key=key;entry.value=value;entry.used=true;LightLock_Unlock(&cache.lock);
 return value;
}
}
