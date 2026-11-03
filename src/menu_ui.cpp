#include <mkw/menu_ui.hpp>
#include <mkw/display_layout.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/menu_unlock_texts.hpp>
#include <mkw/menu_movie_assets.hpp>
#include <mkw/menu_animations.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_index.hpp>
#include <mkw/menu_text_motion.hpp>
#include <mkw/menu_text_styles.hpp>
#include <mkw/cup_record_icons.hpp>
#include <mkw/menu_model_motion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
constexpr float XScale=display::MenuScaleX,YScale=display::MenuScaleY;
constexpr const char* CupNames[]={"Mushroom Cup","Flower Cup","Star Cup","Special Cup","Shell Cup","Banana Cup","Leaf Cup","Lightning Cup"};
constexpr const char* CourseNames[]={"Luigi Circuit","Moo Moo Meadows","Mushroom Gorge","Toad's Factory","Mario Circuit","Coconut Mall","DK Summit","Wario's Gold Mine","Daisy Circuit","Koopa Cape","Maple Treeway","Grumble Volcano","Dry Dry Ruins","Moonview Highway","Bowser's Castle","Rainbow Road","GCN Peach Beach","DS Yoshi Falls","SNES Ghost Valley 2","N64 Mario Raceway","N64 Sherbet Land","GBA Shy Guy Beach","DS Delfino Square","GCN Waluigi Stadium","DS Desert Hills","GBA Bowser Castle 3","N64 DK's Jungle Parkway","GCN Mario Circuit","SNES Mario Circuit 3","DS Peach Gardens","GCN DK Mountain","N64 Bowser's Castle"};
struct Pose {
 float x=0,y=0,sx=1,sy=1,delay=0,parentScaleX=1;
 bool moving=false;UiMotionKind kind=UiMotionKind::Slide;
 const char* source=nullptr;
 float alpha=1;
 unsigned focusSlot=~0u;
 bool unclipped=false;
};
float sourceAlpha(float base,float animation){
 const unsigned a=unsigned(std::clamp(std::lround(base*255.f),0l,255l));
 const unsigned b=unsigned(std::clamp(std::lround(animation*255.f),0l,255l));
 return float(((a+1)*(b+1)-1)>>8)/255.f;
}
// Short derived resource names (base + suffix) without heap allocation.
struct Name {
 char text[64];
 Name(const char* a,const char* b=""){std::snprintf(text,sizeof text,"%s%s",a,b);}
 operator const char*()const{return text;}
 const char* c_str()const{return text;}
};
Pose control(const char* name,const char* variant,unsigned focusSlot=~0u){
 Pose value;value.source=name;value.focusSlot=focusSlot;
 if(const auto* p=uiFindControlPose(name,variant)){value.x=p->x;value.y=p->y;value.sx=p->sx;value.sy=p->sy;value.alpha=p->alpha;}
 if(const auto* motion=uiFindControlMotion(name,variant)){value.delay=motion->delay;value.moving=motion->enabled;}
 if(!std::strcmp(name,"Back"))value.kind=UiMotionKind::Back;
 if(!std::strcmp(name,"MenuObiTitleText"))value.kind=UiMotionKind::PageTitle;
 if(!std::strcmp(name,"MenuObiInstructionText"))value.kind=UiMotionKind::Instruction;
 if(!std::strcmp(name,"MenuObiTop")||!std::strcmp(name,"MenuObiTopRaceConfirm"))value.kind=UiMotionKind::ObiTop;
 if(!std::strcmp(name,"MenuObiBottomRaceConfirm"))value.kind=UiMotionKind::ObiBottom;
 if(!std::strcmp(name,"RaceConfirmBack")||!std::strcmp(name,"BlackBack"))value.kind=UiMotionKind::BlackBack;
 return value;
}
Pose child(Pose parent,Pose p){
 p.x=parent.x+p.x*parent.sx;p.y=parent.y+p.y*parent.sy;
 p.sx*=parent.sx;p.sy*=parent.sy;p.parentScaleX=parent.sx;
 p.delay=std::max(0.f,parent.delay+p.delay);p.alpha=sourceAlpha(parent.alpha,p.alpha);return p;
}
Pose motionOnly(const char* name,const char* variant){Pose p=control(name,variant);p.x=p.y=0;p.sx=p.sy=1;return p;}
Pose numbered(const char* name,unsigned i,const char* prefix="Button"){
 char key[64];std::snprintf(key,sizeof key,"%s%u",prefix,i);return control(name,key,i);
}
struct Lookup {
 std::array<unsigned,std::size(UiSprites)> keys{},texts{};
 Lookup(){for(unsigned i=0;i<keys.size();++i)keys[i]=texts[i]=i;
  auto order=[](auto& a,bool text){std::sort(a.begin(),a.end(),[text](unsigned a,unsigned b){int c=std::strcmp(text?UiSprites[a].text:UiSprites[a].key,text?UiSprites[b].text:UiSprites[b].key);return c<0||(c==0&&a<b);});};order(keys,false);order(texts,true);
 }
 unsigned find(const char* key,bool text=false,unsigned font=0,unsigned style=0)const{
  const auto& a=text?texts:keys;
  auto it=std::lower_bound(a.begin(),a.end(),key,[text](unsigned i,const char* k){return std::strcmp(text?UiSprites[i].text:UiSprites[i].key,k)<0;});
  if(it==a.end()||std::strcmp(text?UiSprites[*it].text:UiSprites[*it].key,key))return unsigned(std::size(UiSprites));
  unsigned fallback=*it,styled=unsigned(std::size(UiSprites));
  if(text)for(;it!=a.end()&&!std::strcmp(UiSprites[*it].text,key);++it){
   const auto& r=UiSprites[*it];if(r.style==style){if(r.font==font)return *it;if(r.font==0)styled=*it;}
  }
  if(styled<std::size(UiSprites))return styled;
  return fallback;
 }
};
const Lookup& lookup(){static const Lookup value;return value;}
class Composer {
public:
 const MenuState& state;std::vector<UiQuad>& out;const UiTransitionView* transition;
 UiMenuLayout layout{};
 Composer(const MenuState& s,std::vector<UiQuad>& q,const UiTransitionView* t):state(s),out(q),transition(t){layout.page=s.page;}
 void group(UiMenuRole role,unsigned index=0,Pose p={},const char* panel=nullptr){
  layout={state.page,role,index,0,0,400,240,false};
  const unsigned selectedGroup=state.page==UiPage::MainMenu?(state.cursor==0?0:state.cursor<4?1:state.cursor<6?2:3):state.cursor;
  layout.selected=index==selectedGroup;
  layout.columns=uint8_t(state.columns());
  if(const auto* box=panel?uiFindPanel(panel):nullptr){
   float x=box->x,y=box->y,w=box->w,h=box->h;
   // Reserve the selected state's real envelope for every cell. The
   // envelope must not change when focus moves; the source focus animation
   // then grows inside it without overlapping the neighbouring choice.
   Name selected(panel);if(char* free=std::strstr(selected.text,"_free")){
    std::strcpy(free,"_select");if(const auto* active=uiFindPanel(selected)){
     const float right=std::max(x+w,active->x+active->w),bottom=std::max(y+h,active->y+active->h);
     x=std::min(x,active->x);y=std::min(y,active->y);w=right-x;h=bottom-y;
    }
   }
   layout.x=200+(p.x+x*p.sx)*XScale;layout.y=120+(-p.y+y*p.sy)*YScale;
   layout.w=w*p.sx*XScale;layout.h=h*p.sy*YScale;
   // These corner buttons include a caption to the left of the icon.
   if(!std::strcmp(panel,"drift_help_free")||!std::strcmp(panel,"license_edit_free")){
    layout.x-=155*p.sx*XScale;layout.w+=155*p.sx*XScale;
   }
  }
 }
 void groupPane(UiMenuRole role,Pose p,const char* panel,const char* name){
  group(role);if(const auto* box=uiFindPane(panel,name)){
   layout.x=200+(p.x+box->x*p.sx)*XScale;layout.y=120+(-p.y+box->y*p.sy)*YScale;
   layout.w=box->w*p.sx*XScale;layout.h=box->h*p.sy*YScale;
  }
 }
 UiTextMotion focusMotion(const UiPaneInfo& pane,Pose p)const{
  if(transition&&transition->preload)return {};
  if(!p.source||p.focusSlot>=UiFocusControlCount||std::strstr(pane.panel,"_ok")||std::strstr(pane.panel,"_selectin"))return {};
  const auto focus=uiFocusAnimation(state,p.focusSlot);
  if(std::strcmp(focus.control,p.source)||!focus.clip)return {};
  const auto* reference=uiFindAnimationClip(p.source,"Select",std::strstr(pane.panel,"_free")?"Free":"Select");
  return uiTextMotion(pane,{{focus.clip,focus.frame,0,reference}});
 }
 void applyMotion(float& x,float& y,float& w,float& h,float& alpha,Pose p,const UiTextMotion& m,float baseAlpha=1)const{
  if(!m.visible){alpha=0;return;}
  if(p.sx!=0&&p.sy!=0&&std::fabs(m.xy)<1e-5f&&std::fabs(m.yx)<1e-5f){
   const float localX=(x-p.x)/p.sx,localY=-(y+p.y)/p.sy;
   x=p.x+(localX*m.sx+m.tx)*p.sx;y=-p.y-(localY*m.sy+m.ty)*p.sy;
   w*=m.sx;h*=m.sy;
  }
  alpha=m.absoluteAlpha?baseAlpha*m.alpha:alpha*m.alpha;
 }
 UiTextMotion panelFocus(const char* key,Pose p)const{
  Name family(key);char* suffix=std::strstr(family.text,"_underlay");if(!suffix)suffix=std::strstr(family.text,"_overlay");if(suffix)*suffix=0;
  const auto* root=pane(family,"pause_w_null");if(!root)root=pane(family,"pause_w_fade_n");
  if(!root)root=pane(family,"null_all");
  return root?focusMotion(*root,p):UiTextMotion{};
 }
 void quad(unsigned i,float x,float y,float w,float h,float alpha=1,uint32_t tint=0xffffffff,Pose p={},bool dynamic=false){
  if((i>=std::size(UiSprites)&&!dynamic)||w<=0||h<=0)return;
  float controlAlpha=p.alpha,screenSlide=0;
  if(transition&&transition->preload)controlAlpha=1;
  if(transition&&transition->active&&p.moving&&(!transition->section||transition->entering)){
   const auto& t=*transition;
   const float progress=uiPageTransitionProgress(t.elapsed,p.delay,t.maxDelay,t.entering,t.back,t.exitHold);
   const auto motion=uiControlTransition(p.kind,progress,state.page==UiPage::MainMenu,p.parentScaleX);
   const bool nativeSlide=p.kind==UiMotionKind::Slide&&state.page!=UiPage::Title&&state.page!=UiPage::Strap&&state.page!=UiPage::Intro&&state.page!=UiPage::Pause&&state.page!=UiPage::QuitConfirm&&state.page!=UiPage::Race&&state.page!=UiPage::Results;
   const bool gpPreview=state.page==UiPage::Cup&&state.selection.mode==UiMode::GrandPrix&&
       (layout.role==UiMenuRole::Preview||layout.role==UiMenuRole::PreviewLabel);
   if(gpPreview){
    // Dad requests opacity transitions for the GP film and course names,
    // preserving the native page clock/delays without horizontal motion.
    const float fade=std::clamp(1.f-std::fabs(progress),0.f,1.f);
    controlAlpha=sourceAlpha(controlAlpha,fade*fade*(3.f-2.f*fade));
   }else{
    if(nativeSlide)screenSlide=-progress;else x+=motion.x;
    y-=motion.y;controlAlpha=sourceAlpha(controlAlpha,motion.alpha);
   }
  }
  alpha=sourceAlpha(alpha,controlAlpha);
  if(alpha<=0)return;
  x=200+x*XScale;y=120+y*YScale;w*=XScale;h*=YScale;
  // Source layouts deliberately bleed beyond the projection; crop their UVs,
  // never squeeze a complete pane into the remaining visible screen space.
  // Reflow happens after composition. Preserve the complete authored pane:
  // an offscreen Wii control can be onscreen after its new group transform.
  const bool reflow=state.page!=UiPage::Title&&state.page!=UiPage::Strap&&state.page!=UiPage::Intro&&state.page!=UiPage::Pause&&state.page!=UiPage::QuitConfirm;
  const bool unclipped=p.unclipped||reflow;
  float l=unclipped?x:std::max(x,0.f),t=unclipped?y:std::max(y,0.f),r=unclipped?x+w:std::min(x+w,display::Width),b=unclipped?y+h:std::min(y+h,display::Height);
  if(r<=l||b<=t)return;
  out.push_back({i,l,t,r-l,b-t,alpha,tint,false,(l-x)/w,(t-y)/h,(r-x)/w,(b-y)/h});
  out.back().menu=layout;out.back().screenSlide=screenSlide;
 }
 void sprite(const char* key,float x,float y,float w,float h,float a=1,uint32_t tint=0xffffffff,Pose p={}){quad(uiFindSpriteKey(key),x,y,w,h,a,tint,p);}
 void region(const char* key,float x,float y,float w,float h,float u0,float v0,float u1,float v1,Pose p,float alpha,const char* focusKey=nullptr){
  x=p.x+x*p.sx;y=-p.y+y*p.sy;w*=p.sx;h*=p.sy;
  applyMotion(x,y,w,h,alpha,p,panelFocus(focusKey?focusKey:key,p));
  const auto first=out.size();sprite(key,x,y,w,h,alpha,0xffffffff,p);
  if(out.size()>first){auto& q=out.back();q.u0=u0+(u1-u0)*q.u0;q.u1=u0+(u1-u0)*q.u1;q.v0=v0+(v1-v0)*q.v0;q.v1=v0+(v1-v0)*q.v1;}
 }
 void focusLayers(const char* key,Pose p,float opacity){
  Name family(key);unsigned part=0;
  if(char* suffix=std::strstr(family.text,"_underlay")){*suffix=0;part=1;}
  else if(char* suffix=std::strstr(family.text,"_overlay")){*suffix=0;part=2;}
  const bool selected=std::strstr(family.text,"_select");
  for(const char* suffix:{"_free","_select"}){
   const auto length=std::strlen(family.text),end=std::strlen(suffix);
   if(length>=end&&!std::strcmp(family.text+length-end,suffix))family.text[length-end]=0;
  }
  unsigned depthMask=0;
  for(const auto& layer:UiFocusSprites){
   if(std::strcmp(layer.panel,family))continue;
   const auto* image=uiFindPanel(layer.key);
   const auto* reference=uiFindPane(Name(layer.panel,"_select"),layer.pane);
   if(!image||!reference)continue;
   const auto* referenceClip=p.source?uiFindAnimationClip(p.source,"Select","Select"):nullptr;
   const auto focus=uiFocusAnimation(state,p.focusSlot);
   const bool bound=p.source&&p.focusSlot<UiFocusControlCount&&!std::strcmp(focus.control,p.source);
   const auto* clip=bound?focus.clip:(p.source?uiFindAnimationClip(p.source,"Select",selected?"Select":"Free"):nullptr);
   // PushButton initializes its independent SelectIn/OK groups with stop
   // clips. Those groups can override a Select-group preview glow (the
   // confirmation button's waku_light is such a pane).
   const auto* selectStop=p.source?uiFindAnimationClip(p.source,"SelectIn","SelectStop"):nullptr;
   const auto* okStop=p.source?uiFindAnimationClip(p.source,"OK","OKStop"):nullptr;
   const auto motion=uiTextMotion(*reference,{{clip,bound?focus.frame:0.f,0,referenceClip},
                                             {selectStop,0,0},{okStop,0,0}});
   float x=p.x+image->x*p.sx,y=-p.y+image->y*p.sy,w=image->w*p.sx,h=image->h*p.sy,alpha=opacity*reference->alpha;
   applyMotion(x,y,w,h,alpha,p,motion,opacity);
   // Source picture panes write depth even during a nearly transparent fade.
   // Keep this mask across movie underlay/overlay passes: an underlay film
   // can reject a picture submitted later in the overlay.
   if(motion.visible&&alpha>0)depthMask|=layer.depthBit;
   if(transition&&transition->preload){x=p.x+image->x*p.sx;y=-p.y+image->y*p.sy;w=image->w*p.sx;h=image->h*p.sy;alpha=1;}
   if(!part||part==layer.part){
    const auto first=out.size();sprite(layer.key,x,y,w,h,alpha,0xffffffff,p);
    for(auto at=first;at<out.size();++at)out[at].additive=layer.blend!=0;
   }
   for(const auto& tail:UiPanelTails){
    if(std::strcmp(tail.panel,family)||std::strcmp(tail.after,layer.pane)||(part&&part!=tail.part))continue;
    auto drawTail=[&](unsigned mask){
     const auto* tailImage=uiFindPanel(tail.keys[selected?1:0][mask]);if(!tailImage)return;
     float tx=p.x+tailImage->x*p.sx,ty=-p.y+tailImage->y*p.sy,tw=tailImage->w*p.sx,th=tailImage->h*p.sy,ta=opacity;
     applyMotion(tx,ty,tw,th,ta,p,panelFocus(key,p));
     sprite(tailImage->key,tx,ty,tw,th,ta,0xffffffff,p);
    };
    // Request every future depth variant before reveal; changing focus must
    // not turn an already resident button into a late texture load.
    if(transition&&transition->preload){for(unsigned mask=0;mask<4;++mask)drawTail(mask);}
    else drawTail(depthMask&3);
   }
  }
 }
 void panel(const char* key,Pose p={},float alpha=1){
  const auto* found=uiFindPanel(key);if(!found)return;const auto& r=*found;
  if(const auto* border=uiFindBorderLoop(key)){const auto& loop=*border;
   region(key,loop.x,loop.y,loop.w,loop.h,(loop.x-r.x)/r.w,(loop.y-r.y)/r.h,(loop.x+loop.w-r.x)/r.w,(loop.y+loop.h-r.y)/r.h,p,alpha);
   const auto* clip=uiFindAnimationClip(loop.control,"Loop","Loop");
   const float sourceFrame=clip&&clip->duration>0?std::fmod(float(state.age)*clip->rate,clip->duration):float(state.age);
   const unsigned frame=unsigned(sourceFrame)%loop.frames;
   for(unsigned at=loop.firstSlice;at<loop.firstSlice+loop.sliceCount;++at){const auto& slice=UiBorderSlices[at];
    // Residency is keyed by page/selection, not loop frame. Every atlas a
    // future frame can use must be requested before the page is revealed.
    const bool resident=transition&&transition->preload&&(at==loop.firstSlice||std::strcmp(slice.key,UiBorderSlices[at-1].key));
    if(slice.frame==frame||resident)region(slice.key,slice.x,slice.y,slice.w,slice.h,slice.u0,slice.v0,slice.u1,slice.v1,p,alpha,key);
   }
   focusLayers(key,p,alpha);selectionGlow(key,p);return;
  }
  float x=p.x+r.x*p.sx,y=-p.y+r.y*p.sy,w=r.w*p.sx,h=r.h*p.sy;
  // These flattened button panels share an animated parent. Per-pane
  // submissions (movies, icons, text) sample their full ancestry separately.
  float baseAlpha=alpha;
  applyMotion(x,y,w,h,baseAlpha,p,panelFocus(key,p));
  sprite(key,x,y,w,h,baseAlpha,0xffffffff,p);
  focusLayers(key,p,alpha);selectionGlow(key,p);
 }
 void graph(float fraction,Pose p){
  fraction=std::clamp(fraction,0.f,1.f);if(fraction<=0)return;
  if(const auto* r=uiFindPanel("vehicle_ability_graph"))sprite(r->key,p.x+r->x*p.sx,-p.y+r->y*p.sy,r->w*p.sx*fraction,r->h*p.sy,1,0xffffffff,p);
 }
 const UiPaneInfo* pane(const char* base,const char* name)const{return uiFindPane(base,name);}
 Name buttonState(const char* base,unsigned slot)const{return Name(base,state.cursor==slot?"_select":"_free");}
 void movieOverlay(const char* base,Pose p){panel(Name(base,"_overlay"),p);}
 void movieButton(const char* base,Pose p){panel(Name(base,"_underlay"),p);}
 void inPane(const char* key,const char* base,const char* name,Pose p={},float a=1,bool movieImage=false){
  if(auto* r=pane(base,name);r&&r->visible){
   const auto before=out.size();
   float x=p.x+r->x*p.sx,y=-p.y+r->y*p.sy,w=r->w*p.sx,h=r->h*p.sy,alpha=a*r->alpha;
   applyMotion(x,y,w,h,alpha,p,focusMotion(*r,p),a);
   // The movie uses this pane for its rectangle, not the black backing
   // material opacity. Preserve control/focus fades on the video itself.
   if(movieImage&&r->alpha>0)alpha=std::clamp(alpha/r->alpha,0.f,1.f);
   sprite(key,x,y,w,h,alpha,0xffffffff,p);
   if(out.size()==before)return;
   auto& q=out.back();
   if(!std::strcmp(key,"locked_choice"))q.menu.preserveAspect=true;
   auto colorAt=[&](float u,float v){uint32_t color=0;
    for(unsigned shift:{24u,16u,8u,0u}){
     const float a=float((r->vertexColors[0]>>shift)&255),b=float((r->vertexColors[1]>>shift)&255),c=float((r->vertexColors[2]>>shift)&255),d=float((r->vertexColors[3]>>shift)&255);
     // Preserve the source gradient even when the transition clips the pane.
     const float value=u>=v?a+u*(b-a)+v*(d-b):a+u*(d-c)+v*(c-a);
     color|=uint32_t(std::clamp(std::lround(value),0l,255l))<<shift;
    }return color;
   };
   q.corners={colorAt(q.u0,q.v0),colorAt(q.u1,q.v0),colorAt(q.u0,q.v1),colorAt(q.u1,q.v1)};
  }
 }
 void label(const char* text,float cx,float y,float width,float height,uint32_t tint=0xffffffff,int align=0,unsigned font=0){
  unsigned i=lookup().find(text,true,font);if(i>=std::size(UiSprites))return;
  const auto& r=UiSprites[i];float h=height,w=r.w/r.h*h;
  if(w>width){h*=width/w;w=width;}
  float x=align<0?cx:align>0?cx-w:cx-w/2;
  quad(i,x,y,w,h,1,tint);
 }
 void textSprite(unsigned index,const UiPaneInfo& pane,Pose p,uint32_t color=0xffffffff){
  if(index>=std::size(UiSprites)||!pane.visible)return;
  if(layout.role==UiMenuRole::Description){
   const char* caption=UiSprites[index].text;
   for(unsigned i=0;i<std::size(UiSprites);++i)
    if(!std::strncmp(UiSprites[i].key,"footer_caption_",15)&&!std::strcmp(UiSprites[i].text,caption)){index=i;break;}
  }
  const auto& glyphs=UiSprites[index];const auto& cell=UiFontCells[glyphs.font];
  float w=glyphs.w*(pane.fontW>0?pane.fontW/cell[0]:pane.h/cell[1])*p.sx;
  float h=glyphs.h*(pane.fontH>0?pane.fontH/cell[1]:pane.h/cell[1])*p.sy;
  const float available=pane.w*p.sx;
  // CharWriter aligns the measured string within the pane; the pane is not
  // a box that rescales long headers or their expanding light copy.
  const bool reflowText=state.page!=UiPage::Title&&state.page!=UiPage::Strap&&state.page!=UiPage::Intro&&
      state.page!=UiPage::Race&&state.page!=UiPage::Results;
  if(state.page==UiPage::MainMenu&&layout.role==UiMenuRole::Choice){
   // Retail CharWriter keeps the authored font height. Long main-menu labels
   // may fit horizontally; they must not become a smaller font in both axes.
   // Lower-screen Channel heading uses the same authored glyph width as
   // its neighbours; its long run may extend beyond the narrow text pane.
   // Keep the pane centre, shadow and font height unchanged.
   const bool channelHeading=!std::strcmp(pane.panel,"main_free")&&!std::strcmp(glyphs.text,"Mario Kart Channel");
   if(!channelHeading)w=std::min(w,available);
   h=std::min(h,pane.h*p.sy);
  }else if(reflowText){
   // The new LCD cells have their own aspect. Fit the measured glyph run
   // uniformly instead of squeezing only its width into the old Wii pane.
   w=h*glyphs.w/glyphs.h*YScale/XScale;
   const float fit=layout.role==UiMenuRole::Description?1.f:std::min({1.f,available/w,pane.h*p.sy/h});w*=fit;h*=fit;
  }else if(std::strcmp(pane.panel,"main")){w=std::min(w,available);h=std::min(h,pane.h*p.sy);}
  float x=p.x+pane.x*p.sx+(available-w)*(layout.role==UiMenuRole::Statistics?0:pane.textAlign==0?0:pane.textAlign==2?1:.5f);
  float y=-p.y+pane.y*p.sy+(pane.h*p.sy-h)*(pane.textPosition/3==0?0:pane.textPosition/3==2?1:.5f);
  float alpha=pane.alpha;
  UiTextMotion fontMotion;
  bool affine=false;float edgeXX=0,edgeXY=0,edgeYX=0,edgeYY=0;
  // Sample the pane and its ancestors, including independent X/Y scaling.
  // OK glows already receive their explicit confirmation transform in glow().
  if(p.source&&!(transition&&transition->preload)&&!std::strstr(pane.panel,"_ok")&&!std::strstr(pane.panel,"_selectin")){
   auto frame=[](const UiAnimationClip* clip,float age){
    if(!clip)return 0.f;
    float f=age*clip->rate;
    if(clip->loop&&clip->duration>0)f=std::fmod(f,clip->duration);
    else f=std::clamp(f,0.f,clip->duration);
    return clip->reverse?clip->duration-f:f;
   };
   const auto* loop=uiFindAnimationClip(p.source,"Loop","Loop");
   if(!loop)loop=uiFindAnimationClip(p.source,"eAFLoop","Loop");
   const bool prompt=!std::strcmp(p.source,"PressStart");
   const auto* fade=prompt?uiFindAnimationClip(p.source,"eAFFade","Fadein"):nullptr;
   const bool pressed=prompt&&transition&&transition->active&&!transition->entering&&!transition->back;
   const auto* press=pressed?uiFindAnimationClip(p.source,"eAFPressStart","PressStart"):nullptr;
   const auto focus=uiFocusAnimation(state,p.focusSlot);
   const bool bound=p.focusSlot<UiFocusControlCount&&!std::strcmp(focus.control,p.source);
   const auto* reference=bound?uiFindAnimationClip(p.source,"Select",std::strstr(pane.panel,"_free")?"Free":"Select"):nullptr;
   const auto motion=uiTextMotion(pane,{{fade,frame(fade,float(state.age)),fade?fade->duration-1:0},
                                       {bound?focus.clip:nullptr,focus.frame,0,reference},
                                       {loop,frame(loop,float(state.age)),frame(loop,0)},
                                       {press,frame(press,pressed?transition->holdFrame:0),frame(press,0)}});
   fontMotion=motion;

   if(!motion.visible)return;
   if(p.sx!=0&&p.sy!=0){
    const float localX=(x-p.x)/p.sx,localY=-(y+p.y)/p.sy;
    x=p.x+(localX*motion.sx+localY*motion.xy+motion.tx)*p.sx;
    y=-p.y-(localX*motion.yx+localY*motion.sy+motion.ty)*p.sy;
    affine=std::fabs(motion.xy)>1e-5f||std::fabs(motion.yx)>1e-5f||motion.sx<0||motion.sy<0;
    if(affine){
     edgeXX=w*motion.sx;edgeXY=-w*motion.yx*p.sy/p.sx;
     edgeYX=-h*motion.xy*p.sx/p.sy;edgeYY=h*motion.sy;
    }else{w*=motion.sx;h*=motion.sy;}
   }
   alpha=motion.absoluteAlpha?motion.alpha:alpha*motion.alpha;
  }
  // Request white glyphs during preload too: animated colors cannot be
  // applied to a copy whose selected colors were already baked into pixels.
  const bool liveFont=layout.role!=UiMenuRole::Description&&(fontMotion.fontMaterial||pane.textStyle!=0||!std::strcmp(pane.panel,"main"));
  if(liveFont&&*glyphs.text&&std::strncmp(glyphs.key,"name_glyph_",11)){
   const unsigned white=lookup().find(glyphs.text,true,glyphs.font,0);
   if(white<std::size(UiSprites)&&UiSprites[white].style==0)index=white;
  }
  const auto first=out.size();
  // Keep the entire affine quad and let the GPU viewport clip it; cropping
  // an axis-aligned rectangle here would cut rotating glyphs and their UVs.
  if(affine||layout.role==UiMenuRole::Description)p.unclipped=true;
  quad(index,x,y,w,h,transition&&transition->preload?1.f:std::clamp(alpha,0.f,1.f),color,p);
  if(affine&&out.size()>first){
   auto& q=out.back();q.affine=true;
   q.positions={{{q.x,q.y},{q.x+edgeXX*XScale,q.y+edgeXY*YScale},
                 {q.x+edgeYX*XScale,q.y+edgeYY*YScale},
                 {q.x+(edgeXX+edgeYX)*XScale,q.y+(edgeXY+edgeYY)*YScale}}};
  }
  if(out.size()>first&&pane.textStyle&&UiSprites[index].style==0&&pane.textStyle<=std::size(UiTextStyles)){
   // A dynamic label can lack a pre-baked styled copy. Use its original
   // white glyph and the exact BRLYT font material instead of white text.
   const auto& style=UiTextStyles[pane.textStyle-1];auto& q=out.back();
   q.fontRemap=true;q.fontColor0=fontMotion.fontMaterial?fontMotion.color0:style.color0;q.fontColor1=fontMotion.fontMaterial?fontMotion.color1:style.color1;
   auto corner=[&](unsigned at)->uint32_t{return fontMotion.fontMaterial?fontMotion.corners[at]:style.corners[at];};
   auto at=[&](float u,float v){uint32_t rgba=0;
    for(unsigned shift:{24u,16u,8u,0u}){
     const float a=float((corner(0)>>shift)&255),b=float((corner(1)>>shift)&255),c=float((corner(2)>>shift)&255),d=float((corner(3)>>shift)&255);
     const float value=u>=v?a+u*(b-a)+v*(d-b):a+u*(d-c)+v*(c-a);
     rgba|=uint32_t(std::clamp(std::lround(value),0l,255l))<<shift;
    }return rgba;
   };
   q.corners={at(q.u0,q.v0),at(q.u1,q.v0),at(q.u0,q.v1),at(q.u1,q.v1)};
  }
  // Shared plain-caption buttons use the accepted PadEasy Automatic/Manual
  // ink. Classify their family rather than enumerating pages: this also
  // covers LicenseManagement, EraseLicense, Settings and future reuse.
  // Keep authored geometry, focus timing and independent light panes.
  const bool commonCaption=std::strncmp(pane.panel,"settings_button_",16)==0||
      std::strncmp(pane.panel,"license_management_",19)==0||
      std::strncmp(pane.panel,"button_",7)==0||
      std::strncmp(pane.panel,"record_next_",12)==0||
      std::strncmp(pane.panel,"unlock_next_",12)==0;
  if(((layout.role==UiMenuRole::Choice&&commonCaption)||
      (state.rulePage()&&layout.role==UiMenuRole::Auxiliary&&layout.group==state.ruleOkRow())||
      (layout.role==UiMenuRole::Auxiliary&&commonCaption))&&
     !std::strcmp(pane.name,"text")){
   const auto* reference=uiFindPane(layout.selected?"drift_select":"drift_free","text");
   if(reference&&reference->textStyle&&reference->textStyle<=std::size(UiTextStyles)){
    const auto& ink=UiTextStyles[reference->textStyle-1];
    for(auto at=first;at<out.size();++at){auto& q=out[at];q.fontRemap=true;
     q.fontColor0=ink.color0;q.fontColor1=ink.color1;
     std::copy(std::begin(ink.corners),std::end(ink.corners),q.corners.begin());
    }
   }
  }
  for(auto at=first;at<out.size();++at)out[at].menu.preserveAspect=true;
 }
 void number(std::uint64_t value,const char* base,Pose p,const char* name="score",const char* suffix=""){
  const auto* original=pane(base,name);if(!original)return;
  char digits[24];std::snprintf(digits,sizeof digits,"%llu%s",static_cast<unsigned long long>(value),suffix);
  if(transition&&transition->preload)std::strcpy(digits,"0123456789%");
  float total=0;unsigned indices[24]{};float widths[24]{};const unsigned count=std::strlen(digits);
  for(unsigned i=0;i<count;++i){char glyph[]={digits[i],0};indices[i]=lookup().find(glyph,true,original->font,original->textStyle);
   if(indices[i]<std::size(UiSprites)){const auto& sprite=UiSprites[indices[i]];widths[i]=sprite.w*original->fontW/UiFontCells[sprite.font][0];total+=widths[i];}}
  if(total<=0)return;const float fit=std::min(1.f,original->w/total);
  float x=original->x+(original->w-total*fit)*(original->textAlign==0?0:original->textAlign==2?1:.5f);
  for(unsigned i=0;i<count;++i){auto cell=*original;cell.x=x;cell.w=widths[i]*fit;cell.fontW*=fit;cell.fontH*=fit;cell.textAlign=0;
   textSprite(indices[i],cell,p);x+=cell.w;}
 }
 void namedText(const char* key,const char* base,const char* name,Pose p){
  if(const auto* r=pane(base,name))textSprite(lookup().find(key),*r,p);
 }
 // Existing native-font name glyphs allow runtime option values without
 // adding texture assets or using a platform/system font.
 void optionText(const char* value,const UiPaneInfo& original,Pose p){
  if(transition&&transition->preload)value="0123456789% Change Miis Erase License LED: Off Items + Events Drifts Dynamic Shadows On Brightness";
  unsigned indices[128]{},count=0;float widths[128]{},total=0;
  for(const unsigned char* ch=reinterpret_cast<const unsigned char*>(value);*ch&&count<128;++ch){
   char key[32];std::snprintf(key,sizeof key,"name_glyph_%04x",unsigned(*ch));
   const auto index=lookup().find(key);if(index>=std::size(UiSprites))continue;
   indices[count]=index;const auto& glyph=UiSprites[index];
   widths[count]=glyph.w*original.fontH/UiFontCells[0][1]*YScale/XScale;total+=widths[count++];
  }
  if(total<=0)return;
  // Fit the complete run to the LCD button, not the much narrower Wii
  // source pane. Its characters and their advances use the SAME scale.
  const float groupScale=std::min(296.f/layout.w,31.f/layout.h);
  const float fit=std::min(1.f,264.f/(total*p.sx*XScale*groupScale));
  float x=original.x+(original.w-total*fit)*.5f;
  for(unsigned i=0;i<count;++i){auto cell=original;cell.x=x;cell.w=widths[i]*fit;cell.fontW*=fit;cell.fontH*=fit;cell.textAlign=0;
   textSprite(indices[i],cell,p);x+=cell.w;}
 }
 void text(const char* text,const char* base,Pose p={},uint32_t tint=0xffffffff,const char* name="text"){
  const auto* r=pane(base,name);if(!r)return;
  if(layout.page==UiPage::LicenseSettings&&layout.role==UiMenuRole::Choice){optionText(text,*r,p);return;}
  const auto* shadow=pane(base,"text_shadow");
  if(!shadow&&!std::strcmp(name,"text"))shadow=pane(base,"TextBox_sha");
  if(shadow)textSprite(lookup().find(text,true,shadow->font,shadow->textStyle),*shadow,p);
  textSprite(lookup().find(text,true,r->font,r->textStyle),*r,p,tint);
  if(!std::strcmp(name,"text"))selectionGlow(base,p,text);
 }
 // Main-menu ink stays fixed. Confirmation deforms a separate light copy.
 void mainText(const char* value,const char* base,Pose p,const char* name="text"){
  const auto* r=pane(base,name);if(!r)return;
  const auto first=out.size();
  Pose stable=p;stable.source=nullptr;
  const auto* shadow=pane(base,"text_shadow");if(!shadow&&!std::strcmp(name,"text"))shadow=pane(base,"TextBox_sha");
  if(shadow)textSprite(lookup().find(value,true,shadow->font,shadow->textStyle),*shadow,stable);
  auto picture=*r;picture.alpha=1;
  textSprite(lookup().find(value,true,picture.font,picture.textStyle),picture,stable);
  // Waku has no OK group. Use its child PushButton's authored light-pane
  // scale/alpha tracks, but never apply those tracks to the heading ink.
  const unsigned slot=state.focus.mainConfirmSlot;
  if(std::strcmp(base,"main_free")||std::strcmp(name,"text")||slot>=7||(transition&&transition->preload))return;
  const unsigned tile=slot==0?0:slot<4?1:slot<6?2:3;
  if(p.focusSlot!=UiFocusMainParentBase+tile)return;
  const auto* clip=uiFindAnimationClip(uiFocusControlName(state,slot),"OK","OK");
  if(!clip)return;
  const float frame=state.focus.mainConfirmFrame*clip->rate;
  const float sx=uiAnimationValue(*clip,"text_light_01","RLPA",6,frame,1);
  const float sy=uiAnimationValue(*clip,"text_light_01","RLPA",7,frame,1);
  const auto* anchor=pane(base,"text");if(!anchor)return;
  const float cx=200+(p.x+(anchor->x+anchor->w*.5f)*p.sx)*XScale;
  const float cy=120+(-p.y+(anchor->y+anchor->h*.5f)*p.sy)*YScale;
  const float alpha=std::clamp(uiAnimationValue(*clip,"text_light_01","RLVC",16,frame,0.f)/255.f,0.f,1.f);
  const auto* light=pane(base,"text_light");
  if(alpha<=0||!light||!light->textStyle||light->textStyle>std::size(UiTextStyles))return;
  const auto& ink=UiTextStyles[light->textStyle-1];
  const auto end=out.size();
  for(auto at=first;at<end;++at){auto q=out[at];
   q.x=cx+(q.x-cx)*sx;q.y=cy+(q.y-cy)*sy;q.w*=sx;q.h*=sy;
   q.alpha*=alpha;q.fontRemap=true;q.fontColor0=ink.color0;q.fontColor1=ink.color1;
   std::copy(std::begin(ink.corners),std::end(ink.corners),q.corners.begin());
   out.push_back(q);
  }
 }
 void licensePortrait(const char* base,Pose p,unsigned slot){
  if(slot>=state.licenseOccupied.size()||!state.licenseOccupied[slot])return;
  const auto* original=pane(base,"mii");if(!original)return;
  // The authored head-only pane extends above the blue frame. Our complete
  // head/shoulders image is already composed, so bound it to mii_base instead.
  // Its own hierarchy keeps the crop aligned during focus/hinge animations.
  const auto* frame=pane(base,"mii_base");if(!frame)return;
  auto picture=*frame;picture.visible=true;
  float x=p.x+picture.x*p.sx,y=-p.y+picture.y*p.sy;
  float w=picture.w*p.sx,h=picture.h*p.sy,alpha=picture.alpha;
  applyMotion(x,y,w,h,alpha,p,focusMotion(picture,p));
  const auto first=out.size();quad(~0u,x,y,w,h,alpha,0xffffffff,p,true);
  if(out.size()>first)out.back().licensePortrait=slot;
 }
 void licenseName(const char* base,Pose p,unsigned slot){
  licensePortrait(base,p,slot);
  if(slot>=state.licenseNames.size())return;
  const auto& name=state.licenseNames[slot];
  if(!name[0]){char fallback[24];std::snprintf(fallback,sizeof fallback,"License %u",slot+1);text(fallback,base,p,0xffffffff,"player");return;}
  const auto* original=pane(base,"player");if(!original)return;
  unsigned indices[10]{},count=0;float widths[10]{},total=0;
  for(auto code:name){
   if(!code)break;
   char key[32];std::snprintf(key,sizeof key,"name_glyph_%04x",unsigned(code));
   auto index=lookup().find(key);
   if(index>=std::size(UiSprites))index=lookup().find("name_glyph_003f");
   if(index>=std::size(UiSprites))continue;
   const auto& glyph=UiSprites[index];indices[count]=index;
   widths[count]=glyph.w*original->fontH/UiFontCells[0][1]*YScale/XScale;
   total+=widths[count++];
  }
  if(total<=0)return;
  const float fit=std::min(1.f,original->w/total);
  float x=original->x+(original->w-total*fit)*(original->textAlign==0?0:original->textAlign==2?1:.5f);
  for(unsigned i=0;i<count;++i){
   auto cell=*original;cell.x=x;cell.w=widths[i]*fit;cell.fontW*=fit;cell.fontH*=fit;cell.textAlign=0;
   textSprite(indices[i],cell,p);x+=cell.w;
  }
 }
 void icon(const char* prefix,unsigned i,const char* base,const char* name,Pose p={},float a=1,uint32_t tint=0xffffffff){char key[64];std::snprintf(key,sizeof key,"%s%u",prefix,i);const auto first=out.size();inPane(key,base,name,p,a);for(auto at=first;at<out.size();++at){out[at].menu.preserveAspect=true;out[at].tint=tint;}}
 void licenseTrophies(const char* base,Pose p,unsigned slot){
  const auto& flags=state.licenseUnlocks[slot];
  const char* cupNames[]={"n_0_kinoko","n_1_flower","n_2_star","n_3_special","r_0_koura","r_1_banana","r_2_konoha","r_3_thunder"};
  const char* questions[]={nullptr,nullptr,"n_2_hatena","n_3_hatena",nullptr,nullptr,"r_2_hatena","r_3_hatena"};
  const bool mirror=licenseCompleted(flags,52);
  for(unsigned cup=0;cup<8;++cup){
   bool available=false;
   for(unsigned cc=0;cc<(mirror?4u:3u);++cc)available|=licenseCupUnlocked(flags,cup,cc);
   const char* field=available?cupNames[cup]:questions[cup];
   if(field){char key[64];std::snprintf(key,sizeof key,"license_icon_%s",field);inPane(key,base,field,p);}
  }
  const char* classPanes[]={"class_50","class_100","class_150","mirror"};
  const char* classNames[]={"50cc","100cc","150cc","Mirror"};
  for(unsigned cc=0;cc<(mirror?4u:3u);++cc)text(classNames[cc],base,p,0xffffffff,classPanes[cc]);
  const char* rows[]={"cc50_","cc100_","cc150_","cc_mirror_"};
  for(unsigned cc=0;cc<(mirror?4u:3u);++cc)for(unsigned cup=0;cup<8;++cup){
   if(!licenseCupUnlocked(flags,cup,cc))continue;
   char paneName[32];std::snprintf(paneName,sizeof paneName,"%s%02u",rows[cc],cup);
   const unsigned rank=state.licenseCups[slot][cc*8+cup]&255u;
   if(rank>=1&&rank<=3){char key[32];std::snprintf(key,sizeof key,"license_trophy_%u",rank);inPane(key,base,paneName,p);}
  }
  if(transition&&transition->preload)for(unsigned rank=1;rank<=3;++rank){char key[32];std::snprintf(key,sizeof key,"license_trophy_%u",rank);inPane(key,base,"cc50_00",p);}
 }
 void movie(const char* prefix,unsigned i,const char* base,const char* name,Pose p={},float a=1,float u0=0,float v0=0,float u1=1,float v1=1){
  char key[80];std::snprintf(key,sizeof key,"%s%u",prefix,i);
  unsigned frame=0;if(const auto* movie=uiFindMovie(key))frame=unsigned(double(state.age)*movie->fps*UiMovieFrameCount/(60.0*movie->frames))%UiMovieFrameCount;
  char sampled[96];std::snprintf(sampled,sizeof sampled,"%s_%u",key,frame);
  const auto before=out.size();
  inPane(sampled,base,name,p,a,true);
  if(out.size()>before){auto& q=out.back();q.u0=u0+(u1-u0)*q.u0;q.u1=u0+(u1-u0)*q.u1;q.v0=v0+(v1-v0)*q.v0;q.v1=v0+(v1-v0)*q.v1;
   if(const unsigned n=uiFindMovieRegion(key);n<std::size(UiMovieRegions))q.movie=n;
  }
 }
 void glowGroup(const char* family,Pose p,const char* caption,unsigned icon,bool pulse=false,bool textOnly=false){
  if(!p.source)return;
  const bool preload=transition&&transition->preload;
  const bool clicked=transition&&!transition->entering&&!transition->back&&
      (transition->active||transition->confirming)&&!std::strcmp(p.source,transition->selectedControl)&&
      (!pulse||p.focusSlot==state.cursor);
  const UiAnimationClip* clip=nullptr;float frame=0;
  if(pulse){
   const auto focus=uiFocusAnimation(state,p.focusSlot);
   if(preload||clicked){clip=uiFindAnimationClip(p.source,"SelectIn","SelectIn");frame=clicked?transition->holdFrame*(clip?clip->rate:1.f):0;}
   else if(!std::strcmp(focus.control,p.source)){clip=focus.selectInClip;frame=focus.selectInFrame;}
  }else{
   if(!preload&&!clicked)return;
   clip=uiFindAnimationClip(p.source,"OK","OK");frame=transition?transition->holdFrame*(clip?clip->rate:1.f):0;
  }
  if(!clip)return;
  // Every original SelectStop pulse alpha is zero. Idle buttons need
  // neither the glow-table traversal nor any additional draw commands.
  if(pulse&&!preload&&!std::strcmp(clip->name,"SelectStop"))return;
  if(frame>=clip->duration){
   if(!*clip->next)return;
   frame-=clip->duration;clip=uiFindAnimationClip(p.source,pulse?"SelectIn":"OK",clip->next);if(!clip)return;
   if(clip->loop&&clip->duration>0)frame=std::fmod(frame,clip->duration);
  }
  const Name base(family,pulse?"_selectin":"_ok");
  for(const auto& g:UiGlows){
   if(std::strcmp(g.panel,family)||std::strcmp(g.group,pulse?"SelectIn":"OK"))continue;
   if(pulse&&((*g.key==0)!=textOnly))continue;
   unsigned variant=g.variant;
   if(pulse&&!textOnly&&icon!=~0u){if(!std::strcmp(family,"character"))variant=1;else if(!std::strcmp(family,"cup"))variant=2;else if(!std::strcmp(family,"vehicle"))variant=3;}
   const auto* referencePane=pane(base,g.pane);if(!referencePane)continue;
   const auto focus=uiFocusAnimation(state,p.focusSlot);
   const auto* selected=uiFindAnimationClip(p.source,"Select","Select");
   const auto* pulseReference=uiFindAnimationClip(p.source,pulse?"SelectIn":"OK",pulse?"SelectIn":"OK");
   const auto* liveSelect=!std::strcmp(focus.control,p.source)&&focus.clip?focus.clip:selected;
   const auto motion=preload?UiTextMotion{}:uiTextMotion(*referencePane,
       {{liveSelect,focus.frame,0,selected},{clip,frame,0,pulseReference}},true);
   if(!motion.visible||motion.alpha<=0)continue;
   Pose pose=p;pose.unclipped=true;
   const auto first=out.size();
   if(variant==0&&!*g.key){if(caption)text(caption,base.c_str(),pose,0xffffffff,g.pane);}
   else if(variant==0)panel(g.key,pose);
   else if((variant==1||variant==2)&&icon!=~0u){
    // Retail substitutes the same icon texture into its light pane. The
    // baked glow already contains pane margins; fitting that image into a
    // second pane changes its silhouette. Map the source icon once instead.
    char key[64];std::snprintf(key,sizeof key,"%s%u",variant==1?"driver_":icon<8?"cup_":"battle_cup_",variant==1||icon<8?icon:icon-8);
    inPane(key,base.c_str(),g.pane,pose);
    for(auto at=first;at<out.size();++at){out[at].fontRemap=true;out[at].fontColor0=g.materialColors[0];out[at].fontColor1=g.materialColors[1];}
   }
   else if(variant==3&&icon<12){
    char key[64];std::snprintf(key,sizeof key,"vehicle_%c%u","sml"[state.driverWeight()],icon);
    inPane(key,base.c_str(),g.pane,pose);
    if(out.size()>first){auto& q=out.back();q.vehicleIcon=icon;q.vehicleColor0=g.materialColors[0];q.vehicleColor1=g.materialColors[1];}
   }
   for(auto at=first;at<out.size();++at){
    auto& q=out[at];float x=(q.x-200)/XScale,y=(q.y-120)/YScale,w=q.w/XScale,h=q.h/YScale;
    applyMotion(x,y,w,h,q.alpha,p,motion,referencePane->alpha);
    q.x=200+x*XScale;q.y=120+y*YScale;q.w=w*XScale;q.h=h*YScale;q.additive=g.blend!=0;
    if(variant!=0)q.menu.preserveAspect=true;
   }
  }
 }
 void glow(const char* family,Pose p,const char* caption=nullptr,unsigned icon=~0u){glowGroup(family,p,caption,icon);}
 void selectionGlow(const char* key,Pose p,const char* caption=nullptr){
  // Every button owns this pulse, including a just-deselected button.
  // Underlays wait for their overlay; glows themselves must not recurse.
  if(std::strstr(key,"_ok")||std::strstr(key,"_selectin")||std::strstr(key,"_underlay")||std::strstr(key,"focus_")||std::strstr(key,"panel_tail_"))return;
  Name family(key);
  if(char* tail=std::strstr(family.text,"_overlay"))*tail=0;
  for(const char* suffix:{"_free","_select"}){
   const auto length=std::strlen(family.text),end=std::strlen(suffix);
   if(length>=end&&!std::strcmp(family.text+length-end,suffix))family.text[length-end]=0;
  }
  if(!caption&&(!std::strcmp(family,"character")||!std::strcmp(family,"vehicle")||!std::strcmp(family,"cup")))return;
  glowGroup(family,p,caption,~0u,true,caption!=nullptr);
 }

 void button(const char* base,unsigned slot,Pose p,const char* caption=nullptr){
  p.focusSlot=slot;
  auto key=buttonState(base,slot);panel(key.c_str(),p);
  if(caption){text(caption,key.c_str(),p);if(state.cursor==slot)glow(base,p,caption);}
 }
 void chrome(const char* title,const char* description=""){
  group(UiMenuRole::Header);panel("header",motionOnly("MenuObiTop","MenuObiTop"));
  group(UiMenuRole::Footer);panel("footer",control("MenuObiBottom","MenuObiBottom"));
  const auto titlePose=control("MenuObiTitleText","MenuObiTitleText");groupPane(UiMenuRole::Title,titlePose,"header_text","text");text(title,"header_text",titlePose);
  Pose back=control("Back","ButtonBack");groupPane(UiMenuRole::Back,back,"back_free","touch");panel("back_free",back);inPane("button_back","back_free","text",back);
  if(*description){const auto p=control("MenuObiInstructionText","MenuObiInstructionText");groupPane(UiMenuRole::Description,p,"footer_help","text");text(description,"footer_help",p,0xffffffff);}
  group(UiMenuRole::Content);
 }
 void timeTrialBest(){
  group(UiMenuRole::TimeTrialRecord);
  const float slide=transition?uiTimeTrialRecordSlide(*transition):0.f;
  auto draw=[&](unsigned index,float x,float y,float w,float h){
   const auto first=out.size();quad(index,(x+slide-200.f)/XScale,(y-120.f)/YScale,w/XScale,h/YScale,1,0xffffffffu,{},true);
   if(out.size()>first)out.back().menu.preserveAspect=true;
  };
  const unsigned title=lookup().find("menu_record_best");
  if(title<std::size(UiSprites)){const auto& r=UiSprites[title];const float h=18.f,w=h*r.w/r.h;draw(title,200-w*.5f,84,w,h);}
  const unsigned slot=std::min(state.selection.license,3u),course=state.selection.course;
  const unsigned ms=course<32?state.licenseBestTimes[slot][course]:0;
  char time[24];if(ms)std::snprintf(time,sizeof time,"%02u:%02u.%03u",ms/60000,ms/1000%60,ms%1000);else std::strcpy(time,"--:--.---");
  // All atlas entries share one texture, already requested by this page.
  constexpr float h=30.f,advance=23.f;const float width=advance*float(std::strlen(time)-1)+h;
  float x=200.f-width*.5f;
  for(const char* p=time;*p;++p){
   char key[40];std::snprintf(key,sizeof key,"menu_record_digit_%u",*p>='0'&&*p<='9'?unsigned(*p-'0'):*p==':'?10u:*p=='.'?11u:12u);
   const auto i=lookup().find(key);const auto first=out.size();draw(i,x,116,h,h);
   if(out.size()>first){auto& q=out.back();const unsigned n=*p>='0'&&*p<='9'?unsigned(*p-'0'):*p==':'?10u:*p=='.'?11u:12u;
    q.u0=float(n%8*26+1)/256.f;q.v0=float(n/8*26+1)/64.f;
    q.u1=q.u0+24.f/256.f;q.v1=q.v0+24.f/64.f;q.tint=0xffe040ffu;}
   x+=advance;
  }
 }
 void cupRecord(unsigned cup){
  if(state.selection.mode!=UiMode::GrandPrix||cup>=8)return;
  const unsigned slot=std::min(state.selection.license,3u),cc=std::min(unsigned(state.selection.engineClass),3u);
  const auto result=state.licenseCups[slot][cc*8+cup];
  const unsigned trophy=result&255,grade=result>>8;
  const char* engines[]={"50cc","100cc","150cc","Mirror"};
  char label[32];std::snprintf(label,sizeof label,"%s Trophy: ",engines[cc]);
  // The normal footer uses complete 51px BRFNT cells, at LCD y222/h14.
  // Whole fragments preserve CWDH advances and their shared baseline.
  constexpr float height=14.f,icon=14.f,y=222.f;
  auto fragment=[&](const char* value){char key[40];
   if(!std::strcmp(value,label))std::snprintf(key,sizeof key,"menu_record_class_%u",cc);
   else std::snprintf(key,sizeof key,"%s",!std::strcmp(value,"--")?"menu_record_none":"menu_record_rank");
   return lookup().find(key);};
  auto width=[&](const char* value){const auto i=fragment(value);return i<std::size(UiSprites)?height*UiSprites[i].w/UiSprites[i].h:0.f;};
  float x=200.f-(width(label)+icon+width(" Rank: ")+icon)*.5f;
  group(UiMenuRole::CupRecord);const auto firstRecord=out.size();
  auto sprite=[&](unsigned index,float w,float h,uint32_t tint=0xffffffffu){
   const auto begin=out.size();quad(index,(x-200.f)/XScale,(y-120.f+(height-h)*.5f)/YScale,w/XScale,h/YScale,1,tint,{},true);
   if(out.size()>begin)out.back().menu.preserveAspect=true;
   x+=w;
  };
  auto text=[&](const char* value){sprite(fragment(value),width(value),height);};
  auto recordIcon=[&](unsigned n){
   const auto& rect=CupRecordIconRects[n];const auto first=out.size();
   sprite(lookup().find("cup_record_rank_atlas"),icon,icon);
   if(out.size()>first){auto& q=out.back();const float width=q.h*float(rect[2])/float(rect[3]);q.x+=(q.w-width)*.5f;q.w=width;
    q.u0=float(rect[0])/CupRecordAtlasWidth;q.v0=float(rect[1])/CupRecordAtlasHeight;
    q.u1=float(rect[0]+rect[2])/CupRecordAtlasWidth;q.v1=float(rect[1]+rect[3])/CupRecordAtlasHeight;
    // These original glyphs already carry gold/silver/bronze and star colours.
    q.fontRemap=false;
   }
  };
  text(label);
  if(trophy>=1&&trophy<=3)recordIcon(trophy-1);
  else{const float next=x+icon;text("--");x=next;}
  text(" Rank: ");
  if(grade>=1&&grade<=8)recordIcon(grade+2);else text("--");
  // Centre actual emitted artwork too: a missing record's dashes and a
  // narrow rank glyph need not occupy the complete nominal icon slot.
  float left=400.f,right=0.f;
  for(auto i=firstRecord;i<out.size();++i){left=std::min(left,out[i].x);right=std::max(right,out[i].x+out[i].w);}
  if(firstRecord<out.size())for(auto i=firstRecord;i<out.size();++i)out[i].x+=200.f-(left+right)*.5f;
 }
 void notice(){
  if(!state.unavailable&&!state.saveFailed)return;
  group(UiMenuRole::Notice,0,{},"drift_message");panel("drift_message");
  const auto first=out.size();label(state.saveFailed?"Save failed":"Not available",0,-35,344,70,0x303030ff);
  for(auto i=first;i<out.size();++i)out[i].menu.preserveAspect=true;
 }
};
}
std::vector<UiQuad> composeMenu(const MenuState& state,const UiRaceView& race){std::vector<UiQuad> out;composeMenu(state,race,out);return out;}
static void composeMenuImpl(const MenuState& s,const UiRaceView& race,std::vector<UiQuad>& q,const UiTransitionView* transition){
 // The race and its end-of-race menu draw themselves (RaceFlowUi draws the
 // retail result boards and buttons over the running race).
 q.clear();q.reserve(256);if(s.page==UiPage::Race||s.page==UiPage::Results)return;
 if(s.page==UiPage::Confirm||s.page==UiPage::DriftHelp){
  MenuState base=s;
  if(transition&&transition->underlay)base=*transition->underlay;
  else{
   base.page=s.selection.mode==UiMode::GrandPrix?UiPage::Cup:s.selection.mode==UiMode::Battle?UiPage::BattleStage:UiPage::Course;
   base.cursor=base.page==UiPage::Cup?s.selection.course/4:base.page==UiPage::BattleStage?s.selection.battleStage%5:s.selection.course%4;
  }
  if(s.page==UiPage::DriftHelp){base.page=UiPage::Drift;base.cursor=2;}
  UiTransitionView underlayView;
  underlayView.preload=transition&&transition->preload;
  if(transition&&transition->underlay){
   underlayView.confirming=true;
   underlayView.holdFrame=std::max(0.f,transition->underlayHoldFrame);
   underlayView.selectedControl=transition->underlaySelectedControl;
  }
  composeMenuImpl(base,race,q,underlayView.confirming||underlayView.preload?&underlayView:nullptr);
  if(s.page==UiPage::Confirm&&base.page==UiPage::Cup&&s.selection.mode==UiMode::GrandPrix){
   const bool opening=transition&&transition->active&&transition->entering&&
       transition->from==UiPage::Cup&&transition->to==UiPage::Confirm;
   const bool returning=transition&&transition->active&&
       transition->from==UiPage::Confirm&&transition->to==UiPage::Cup;
   float fade=transition?std::clamp(std::max(0.f,transition->elapsed)/UiPageMotionFrames,0.f,1.f):1.f;
   fade=fade*fade*(3.f-2.f*fade);
   const float full=returning?1.f-fade:opening?fade:1.f;
   for(auto& quad:q)if(quad.menu.page==UiPage::Cup&&
       (quad.menu.role==UiMenuRole::Preview||quad.menu.role==UiMenuRole::PreviewLabel)){
    quad.previewFullAlpha=full;quad.screenSlide=0;
   }
  }
  if(s.page==UiPage::Confirm&&(base.page==UiPage::Course||base.page==UiPage::BattleStage)){
   const auto coursePage=base.page;
   // Confirm retains the Course underlay. Carry its selected cup badge
   // through the same ten-frame page slide instead of dropping it when
   // the logical page changes. The full movie remains stationary.
   const bool opening=transition&&transition->active&&transition->entering&&
       transition->from==coursePage&&transition->to==UiPage::Confirm;
   const bool returning=transition&&transition->active&&
       transition->from==UiPage::Confirm&&transition->to==coursePage;
   const float progress=transition?std::clamp(std::max(0.f,transition->elapsed)/UiPageMotionFrames,0.f,1.f):1.f;
   // OK starts a new Confirm exit clock. That clock must not replay the
   // already-finished Course badge exit; keep it hidden through race loading.
   const float hidden=returning?1.f-progress:opening?progress:1.f;
   for(auto& quad:q)if(quad.menu.page==coursePage&&quad.menu.role==UiMenuRole::Name)
    quad.screenSlide=-hidden;
  }
 }
 Composer c(s,q,transition);const unsigned driver=std::min(s.selection.driver,unsigned(UiCharacters.size()-1));
 const bool preload=transition&&transition->preload;
 const unsigned course=std::min(s.selection.course,31u),weight=s.driverWeight();
 const char* modes[]={"Grand Prix","Time Trials","VS","Battle"};
 // The first quad is always the background, allowing the original 3D menu
 // scene to be inserted between it and the authored controls.
 c.group(UiMenuRole::Background);
 if(s.page!=UiPage::Confirm&&s.page!=UiPage::DriftHelp){
  char artwork[48];std::snprintf(artwork,sizeof artwork,"title_%s_%u",
      s.page==UiPage::MainMenu||s.page==UiPage::License?"blur_scene":"scene",std::min(s.titleArtwork,5u));
  c.panel(s.page==UiPage::Title||s.page==UiPage::Intro||s.page==UiPage::MainMenu||s.page==UiPage::License?artwork:"background");
 }
 if(s.page==UiPage::License||s.page==UiPage::MainMenu)c.panel("background");
 c.group(UiMenuRole::Content);
 switch(s.page){
 case UiPage::Unlock:{
  const UiUnlockText* notice=nullptr;
  for(const auto& row:UiUnlockTexts)if(row.condition==s.unlockCondition&&row.bit==s.unlockBit){notice=&row;break;}
  if(!notice)break;
  const char* kind=!std::strcmp(notice->kind,"Course")?"course":!std::strcmp(notice->kind,"Driver")?"driver":!std::strcmp(notice->kind,"Vehicle")?"vehicle":"mii";
  c.group(UiMenuRole::Header);c.panel("header",motionOnly("MenuObiTop","MenuObiTop"));
  c.group(UiMenuRole::Footer);c.panel("footer",control("MenuObiBottom","MenuObiBottom"));
  const Name title("unlock_title_",kind),body("unlock_",kind);
  const Name titleControl(notice->kind,"_MenuObiTitleText"),bodyControl(notice->kind,"_MessageWindowHalf");
  auto titlePose=control(titleControl,"MenuObiTitleText");
  c.groupPane(UiMenuRole::Title,titlePose,title,"text");c.text(notice->title,title,titlePose);
  auto bodyPose=control(bodyControl,"MessageWindowHalf");
  c.group(UiMenuRole::Dialog,0,bodyPose,body);c.panel(body,bodyPose);c.text(notice->body,body,bodyPose);
  if(!std::strcmp(notice->kind,"Course")&&notice->retailId>=0){
   c.group(UiMenuRole::Preview);
   c.layout.x=200-64*XScale;c.layout.y=120-64*YScale;c.layout.w=128*XScale;c.layout.h=128*YScale;
   char icon[32];if(notice->retailId==8)std::snprintf(icon,sizeof icon,"cup_mirror");else std::snprintf(icon,sizeof icon,"cup_%d",notice->retailId);
   c.sprite(icon,-64,-64,128,128);
  }
  auto next=control("NextButton","Next",0);c.group(UiMenuRole::Choice,0,next,"unlock_next_free");c.button("unlock_next",0,next,"Next");
  break;
 }
 case UiPage::Intro:
  if(!q.empty())if(const unsigned i=uiFindMovieRegion("intro_movie");i<std::size(UiMovieRegions))q.back().movie=i;
  break;
 case UiPage::Strap:
  c.sprite(s.age<180?"strap_a":"strap_b",-304,-228,608,456);
  // Residency covers every age-dependent variant this page can show.
  if(preload)c.sprite(s.age<180?"strap_b":"strap_a",-304,-228,608,456);
  break;
 case UiPage::Title:
  {
   const Pose p=control("PressStart","press_start");
   c.text("Press the A Button","press_start",p,0xffffffff,"text_00");
   const auto first=q.size();
   c.text("Press the A Button","press_start",p,0xffffffff,"text_light");
   for(auto i=first;i<q.size();++i)q[i].additive=UiPressStartLightAdditive;
  }break;
 case UiPage::RecordsGeneral:case UiPage::RecordsFavorites:case UiPage::RecordsOther:{
  const bool favorites=s.page==UiPage::RecordsFavorites,other=s.page==UiPage::RecordsOther;
  const char* general[]={"Overall Ranking","Distance Traveled","Total Race Count","Total Battle Count","Wii Wheel %","Tricks"};
  const char* favoriteLabels[]={"Favorite Character","Favorite Vehicle","Favorite Course","Favorite Stage"};
  const char* otherLabels[]={"First-Place Appearances","% Race Time in 1st Place","Item Hits Delivered","Item Hits Received","Ghost Races","Tournaments Played"};
  auto favorite=[](const auto& values){unsigned best=~0u;for(unsigned i=0;i<values.size();++i)if(values[i]&&(best==~0u||values[i]>values[best]))best=i;return best;};
  const auto& records=s.licenseRecords;
  for(unsigned i=0;i<(favorites?4u:6u);++i){char variant[24];std::snprintf(variant,sizeof variant,"Record_%u",i);auto row=control("Record",variant);c.group(UiMenuRole::Information,i,row,"record_row");
   c.panel("record_row",row);c.text(favorites?favoriteLabels[i]:other?otherLabels[i]:general[i],"record_row",row,0xffffffff,"menu_text");
   const char* value="--";
   if(favorites){
    if(i==0){const auto n=favorite(records.characters);if(n<24)value=UiCharacters[n].name;}
    if(i==1){const auto n=favorite(records.vehicles);constexpr unsigned choices[]={0,1,2,6,7,8,3,4,5,9,10,11};if(n<36)value=UiVehicleNames[n%3][choices[n/3]];}
    if(i==2){const auto n=favorite(records.courses);if(n<32)value=CourseNames[n];}
    if(i==3){const auto n=favorite(records.battleStages);if(n<10)value=UiBattleStageNames[n];}
   }else if(other&&i==0){c.number(std::uint64_t(records.firstPlaces[0])+records.firstPlaces[1],"record_row",row);continue;}
   else if(!other&&i==2){c.number(std::uint64_t(records.finishes[0])+records.finishes[1]+records.finishes[2],"record_row",row);continue;}
   else if(!other&&i==3){c.number(records.battles,"record_row",row);continue;}
   else if(!other&&i==5){c.number(records.tricks,"record_row",row);continue;}
   else if(!other&&i==4){
    const std::uint64_t races=std::uint64_t(records.finishes[0])+records.finishes[1]+records.finishes[2]+records.battles;
    const std::uint64_t wheel=std::uint64_t(records.wheelRaces)+records.wheelBattles;
    c.number(races?std::min<std::uint64_t>(100,wheel*100/races):0,"record_row",row,"score","%");continue;
   }else if(other&&i>=2){
    const std::uint64_t values[]={records.hitsDelivered,records.hitsReceived,std::uint64_t(records.ghostWins)+records.ghostLosses,records.tournaments};
    c.number(values[i-2],"record_row",row);continue;
   }
   c.text(value,"record_row",row,0xffffffff,"score");
  }
  c.group(UiMenuRole::Auxiliary,0,control("NextRecordButton","Next",0),"record_next_free");c.button("record_next",0,control("NextRecordButton","Next",0),"Next");
  c.chrome(favorites?"Favorites":other?"Other Records":"General");break;
 }
 case UiPage::LicenseSettings:{
  // Reuse the original control and game-font text, including the two port options.
  const char* variants[]={"ChangeMii","Delete","ChangeMii","ChangeMii","ChangeMii"};
  const char* modes[]={"Off","Items","Items + Events","Items + Events + Drifts"};
  char ledLabel[64],shadowLabel[64],brightnessLabel[48];
  std::snprintf(brightnessLabel,sizeof brightnessLabel,"LED Brightness: %u%%",s.selection.ledBrightness);
  std::snprintf(ledLabel,sizeof ledLabel,"LED: %s",modes[std::min(s.selection.ledMode,3u)]);
  std::snprintf(shadowLabel,sizeof shadowLabel,"Dynamic Shadows: %s",s.selection.dynamicShadows?"On":"Off");
  const char* labels[]={"Change Miis","Erase License",ledLabel,shadowLabel,brightnessLabel};
  for(unsigned i=0;i<5;++i){const auto row=control("LicenseManagementButton",variants[i],i);
   c.group(UiMenuRole::Choice,i,row,"license_management_free");c.button("license_management",i,row,labels[i]);}
  // The card displays this license's saved cups; its identity stays separate
  // from whichever driver is currently selected for the next race.
  const auto p=control("LicenseManagement","License");c.group(UiMenuRole::Preview,0,p,"license_center_used");
  c.panel("license_center_used",p);c.licenseTrophies("license_center_used",p,s.selection.license);c.licenseName("license_center_used",p,s.selection.license);
  c.chrome("License Settings");break;
 }
 case UiPage::Settings:{
  const char* variants[]={"Rumble","Network","ChannelInstall"};
  const char* captions[]={"Nintendo GameCube Controller","Network Settings","Mario Kart Channel"};
  for(unsigned i=0;i<3;++i){auto p=control("OptionTopButton",variants[i],i);c.group(UiMenuRole::Choice,i,p,"settings_button_free");c.button("settings_button",i,p,captions[i]);}
  c.chrome("Settings");break;
 }
 case UiPage::License:{
  c.chrome("Select License");
  for(unsigned i=0;i<4;++i){char name[32];std::snprintf(name,sizeof name,"License%u",i);Pose p=control(i<2?"LicenseSelectT":"LicenseSelectB",name,i);const char* card=s.licenseOccupied[i]?(i<2?"license_top_used":"license_bottom_used"):(i<2?"license_top_empty":"license_bottom_empty");auto base=c.buttonState(card,i);c.group(UiMenuRole::Choice,i,p,(i<2?"license_top_empty_free":"license_bottom_empty_free"));c.panel(base.c_str(),p);if(s.licenseOccupied[i])c.licenseTrophies(base.c_str(),p,i);if(s.licenseOccupied[i])c.licenseName(base.c_str(),p,i);else c.text("NEW",base.c_str(),p,0xffffffff,"new");if(i==s.cursor)c.glow(card,p);
  }
  // Wii-only Settings (GC rumble, WFC, Channel install) are hidden on 3DS.
  break;
 }
 case UiPage::MainMenu:{
  const char* titles[]={"Single Player","Multiplayer","Nintendo WFC","Mario Kart Channel"};
  const char* groups[]={"TopMenuSingleWaku","TopMenuMultiWaku","TopMenuWifiWaku","TopMenuChannelWaku"};
  for(unsigned i=0;i<4;++i){Pose p=control(groups[i],groups[i],UiFocusMainParentBase+i);c.group(UiMenuRole::Choice,i,p,"main_free");c.panel("main_free",p);for(const char* field:{"text_sha","text"})c.mainText(titles[i],"main_free",p,field);
   if(i==0){auto b=child(p,control("TopMenuSingle","ButtonSingle",0));auto base=c.buttonState("main_button",0);c.movieButton(base.c_str(),b);c.movie("main_movie_",0,base.c_str(),"black_base",b);c.movieOverlay(base.c_str(),b);c.mainText("1\nPlayer",base.c_str(),b);}
   if(i==1||i==2){unsigned n=i==1?3:2;for(unsigned k=0;k<n;++k){char v[48];std::snprintf(v,sizeof v,i==1?"ButtonMulti%uP":"ButtonWifi%uP",k+(i==1?2:1));Pose b=child(p,control(i==1?"TopMenuMulti":"TopMenuWifi",v,i==1?1+k:4+k));const auto slot=i==1?1+k:4+k;auto base=c.buttonState(i==1?"main_multi":"main_wifi",slot);c.movieButton(base.c_str(),b);c.movie("main_movie_",slot,base.c_str(),"black_base",b);c.movieOverlay(base.c_str(),b);char caption[20];const unsigned players=k+(i==1?2:1);std::snprintf(caption,sizeof caption,"%u\n%s",players,players==1?"Player":"Players");c.mainText(caption,base.c_str(),b);}}
   if(i==3){Pose b=child(p,control("TopMenuChannel","ButtonChannel",6));auto base=c.buttonState("main_button",6);c.movieButton(base.c_str(),b);c.movie("main_movie_",6,base.c_str(),"black_base",b);c.movieOverlay(base.c_str(),b);}
  }
  c.chrome("Main Menu",s.cursor==7?"":s.cursor==0?UiHelpMain[0]:s.cursor<4?UiHelpMainMulti[s.cursor-1]:s.cursor<6?UiHelpWfc[s.cursor-4]:UiHelpMain[3]);
  // License Settings overlaps the top banner and draws above it.
  {Pose edit=control("FileAdmin","ButtonFileAdmin",7);c.group(UiMenuRole::Auxiliary,0,edit,"license_edit_free");auto editBase=c.buttonState("license_edit",7);c.panel(editBase.c_str(),edit);c.text("License Settings",editBase.c_str(),edit);if(s.cursor==7)c.glow("license_edit",edit,"License Settings");}break;
 }
 case UiPage::Mode:{
  const char* variants[]={"ButtonGP","ButtonTA","ButtonVS","ButtonBT"};
  for(unsigned i=0;i<4;++i){Pose p=control("SingleTop",variants[i],i);c.group(UiMenuRole::Choice,i,p,"mode_free");auto base=c.buttonState("mode",i);c.movieButton(base.c_str(),p);
   // The native page reconstructs ONE selected movie across four rows and
   // four narrow panes. The wide pane at the right is the caption backing.
   const char* panes[]={"black_base","black_base_00","black_base_01","black_base_02"};
   for(unsigned j=0;j<4;++j)c.movie("mode_movie_",std::min(s.cursor,3u),base.c_str(),panes[j],p,1,j*.25f,i*.25f,(j+1)*.25f,(i+1)*.25f);
   c.movieOverlay(base.c_str(),p);
   c.text(modes[i],base.c_str(),p,0xffffffff);if(s.cursor==i)c.glow("mode",p,modes[i]);}
  c.chrome("Single Player",UiHelpMode[std::min(s.cursor,3u)]);break;
 }
 case UiPage::EngineClass:{
  const char* names[]={"50cc","100cc","150cc","Mirror"};const char* variants[]={"Button50cc","Button100cc","Button150cc","ButtonMirror"};
  for(unsigned i=0;i<4;++i){if(!s.engineUnlocked(i))continue;Pose p=control("ClassSelect2",variants[i],i);c.group(UiMenuRole::Choice,i,p,"class_free");auto base=c.buttonState("class",i);c.movieButton(base.c_str(),p);c.movie("class_movie_",s.engineClassMovie(i),base.c_str(),"black_base",p);c.movieOverlay(base.c_str(),p);c.icon("class_",i,base.c_str(),"kinoko_set_p",p);char caption[64];std::snprintf(caption,sizeof caption,"%s\n%s",names[i],i==0&&(!s.selection.mixedEngineTypes||!s.mixedTypesUnlocked(0))?"Karts Only":i==1&&(!s.selection.mixedEngineTypes||!s.mixedTypesUnlocked(1))?"Bikes Only":"Karts and Bikes");c.text(caption,base.c_str(),p);if(s.cursor==i)c.glow("class",p,caption);}
  if(s.mixedTypesUnlocked(0)||s.mixedTypesUnlocked(1)){Pose toggle=control("ClassChange","ClassChange");c.groupPane(UiMenuRole::Auxiliary,toggle,"class_switch","close");char key[48];
   for(unsigned blink=0;blink<(preload?2u:1u);++blink){const unsigned frame=((s.age/30)+blink)%2;
    std::snprintf(key,sizeof key,"button_switch_shadow_%u",frame);c.namedText(key,"class_switch","close_sha",toggle);
    std::snprintf(key,sizeof key,"button_switch_%u",frame);c.namedText(key,"class_switch","close",toggle);}}
  c.chrome("Engine Classes",s.selection.mixedEngineTypes&&s.cursor<2?UiHelpEngineClassesAllKartBike[s.cursor]:UiHelpEngineClasses[std::min(s.cursor,3u)]);break;
 }
 case UiPage::Character:{
  Pose parent=control("CharacterSelectNULL","CharacterSelectNULL");constexpr unsigned within[]={0,1,4,6,2,3,5,7};
  for(unsigned i=0;i<s.count();++i){if(!s.choiceVisible(i))continue;char group[48];std::snprintf(group,sizeof group,"CharacterSelect4_%u",i<24?i/8:3);Pose p=child(parent,numbered(group,i<24?within[i%8]:i-24));p.focusSlot=i;c.group(UiMenuRole::Choice,i,p,"character_free");c.button("character",i,p);if(s.driverUnlocked(s.characterDriver(i))){c.icon("driver_",s.characterDriver(i),c.buttonState("character",i).c_str(),"chara",p);c.glowGroup("character",p,nullptr,s.characterDriver(i),true);if(s.cursor==i)c.glow("character",p,nullptr,s.characterDriver(i));}else c.inPane("locked_choice",c.buttonState("character",i).c_str(),"chara",p);}
  Pose name=control("CharacterSelectName","CharacterSelectName");c.group(UiMenuRole::Name,0,name,"character_name");c.panel("character_name",name);c.text(s.driverUnlocked(s.characterDriver(s.cursor))?UiCharacters[s.characterDriver(s.cursor)].name:"","character_name",name,0xffffffff,"mii_name");c.chrome("Characters");break;
 }
 case UiPage::Vehicle:{
  Pose parent=control("MachineSelectNULL6","MachineSelectNULL");
  for(unsigned i=0;i<s.count();++i){
   if(!s.choiceVisible(i))continue;
   unsigned row=i/s.columns();Pose p=child(parent,numbered("MachineSelectNULL6",row,"MachineSelectNULL"));
   p=child(p,numbered(s.columns()==2?"MachineSelect2_6":"MachineSelect1_6",i%s.columns()));p.focusSlot=i;c.group(UiMenuRole::Choice,i,p,"vehicle_free");
   c.button("vehicle",i,p);const auto base=c.buttonState("vehicle",i);
   if(!s.vehicleUnlocked(s.vehicleChoice(i))){c.inPane("locked_choice",base.c_str(),"hatena",p);continue;}
   char key[48];std::snprintf(key,sizeof key,"vehicle_%c%u","sml"[weight],s.vehicleChoice(i));
   // Original common_w030_machine_12 pane submission order and C0/C1.
   // The free-state grey overlay keeps its authored50/255 pane opacity.
   const char* layers[]={"chara_shadow","chara","chara_c_down"};
   for(unsigned layer=0;layer<3;++layer){
    const auto first=q.size();c.inPane(key,base.c_str(),layers[layer],p);
    if(q.size()>first){auto& icon=q.back();icon.menu.preserveAspect=true;icon.vehicleIcon=s.vehicleChoice(i);
     icon.vehicleColor0=0;icon.vehicleColor1=layer==0?0x000000ffu:layer==1&&s.cursor==i?0xffffffffu:0xc8c8c8ffu;
    }
   }
   c.glowGroup("vehicle",p,nullptr,s.vehicleChoice(i),true);
   if(s.cursor==i)c.glow("vehicle",p,nullptr,s.vehicleChoice(i));
  }
  Pose name=control("MachineSelectName","MachineSelectName");c.group(UiMenuRole::Name,0,name,"vehicle_name");c.panel("vehicle_name",name);c.text(s.vehicleUnlocked(s.vehicleChoice(s.cursor))?UiVehicleNames[weight][s.vehicleChoice(s.cursor)]:"","vehicle_name",name,0xffffffff,"mii_name");
  if(!s.vehicleUnlocked(s.vehicleChoice(s.cursor))){c.chrome("Vehicles");break;}
  const char* stats[]={"Speed","Weight","Acceleration","Handling","Drift","Off-Road","Mini-Turbo"};const char* variants[]={"Speed","Weight","Accel","Handling","Drift","Dirt","Miniturbo"};
  Pose base=control("MachineAbilityNULL","MachineAbilityNULL");for(unsigned i=0;i<7;++i){Pose p=child(base,control("MachineAbility",variants[i]));c.group(UiMenuRole::Statistics,i,p,"vehicle_ability");c.layout.y-=16*p.sy*YScale;c.layout.h+=16*p.sy*YScale;c.panel("vehicle_ability",p);const unsigned value=UiDriverStats[driver<24?UiCharacters[driver].retailId:24+(weight==0?1:weight==1?0:2)][i]+UiVehicleStats[weight][s.vehicleChoice(s.cursor)][i];c.graph(float(value+(i==0?10:5))/(i==0?35:30),p);c.text(stats[i],"vehicle_ability",p,0xffffffff,"ability_text");}
  c.chrome("Vehicles");break;
 }
 case UiPage::Drift:
  // drift_select's lower clip is the automatic demo, the upper one manual.
  for(unsigned i=0;i<2;++i){Pose p=control("PadEasy",i==0?"ButtonEasy":"ButtonNormal",i);c.group(UiMenuRole::Choice,i,p,"drift_free");auto base=c.buttonState("drift",i);c.movieButton(base.c_str(),p);c.movie("drift_movie_",1-i,base.c_str(),"black_base",p);c.movieOverlay(base.c_str(),p);c.text(i==0?"Automatic":"Manual",base.c_str(),p);if(s.cursor==i)c.glow("drift",p,i==0?"Automatic":"Manual");}c.chrome("Drift",UiHelpDrift[std::min(s.cursor,1u)]);
  {Pose help=control("DriftHelp","DriftHelp",2);c.group(UiMenuRole::Auxiliary,0,help,"drift_help_free");auto helpBase=c.buttonState("drift_help",2);c.panel(helpBase.c_str(),help);c.text("Help",helpBase.c_str(),help);if(s.cursor==2)c.glow("drift_help",help,"Help");}break;
 case UiPage::VSMode:case UiPage::BattleMode:{
  bool battle=s.page==UiPage::BattleMode;const char* names[]={battle?"Balloon Battle":"Solo Race",battle?"Coin Runners":"Team Race"};
  for(unsigned i=0;i<2;++i){Pose p=control(battle?"BattleSelect":"VSSelect",battle?(i?"ButtonCoin":"ButtonBalloon"):(i?"ButtonTeam":"ButtonIndividual"),i);c.group(UiMenuRole::Choice,i,p,battle?"battle_free":"vs_free");auto base=c.buttonState(battle?"battle":"vs",i);c.movieButton(base.c_str(),p);c.movie(battle?"battle_mode_movie_":"vs_movie_",i,base.c_str(),"black_base",p);c.movieOverlay(base.c_str(),p);c.text(names[i],base.c_str(),p);if(s.cursor==i)c.glow(battle?"battle":"vs",p,names[i]);}
  c.chrome(battle?"Battle":"VS");
  {auto p=control("RuleSetting","ButtonRuleSetting",2);c.group(UiMenuRole::Auxiliary,0,p,"drift_help_free");auto base=c.buttonState("drift_help",2);c.panel(base.c_str(),p);c.text("Rules",base.c_str(),p);if(s.cursor==2)c.glow("drift_help",p,"Rules");}break;
 }
 case UiPage::TeamConfirm:{
  auto teams=offlineVsTeams(true,s.selection.humanTeam);unsigned slots[2]{};
  for(unsigned i=0;i<12;++i){
   const unsigned team=unsigned(teams[i]),who=s.cpuRoster.ready?s.cpuRoster.choices[i]/12:offlineRaceDriver(driver,i);
   const char* key=team==0?(i==0?"team_red_human":"team_red_cpu"):(i==0?"team_blue_human":"team_blue_cpu");
   char column[32],row[32];std::snprintf(column,sizeof column,"TeamConfirmNULL%u",team);std::snprintf(row,sizeof row,"BlueRed%u",slots[team]++);
   auto p=child(control("TeamConfirmNULL",column),control("TeamConfirmBlueRed",row));c.group(UiMenuRole::Information,team*6+slots[team]-1,p,key);
   // Original chara_icon_sha maps both colour endpoints to black; keep sampled alpha.
   c.panel(key,p);c.icon("driver_",who,key,"chara_icon_sha",p,1,0x000000ff);c.icon("driver_",who,key,"chara_icon",p);c.text(UiCharacters[who].name,key,p,0xffffffff,"mii_name");
  }
  c.group(UiMenuRole::Choice,0,control("TeamConfirmOK","ButtonOK",0),"button_free");c.button("button",0,control("TeamConfirmOK","ButtonOK",0),"OK");

  c.chrome("Teams");break;
 }
 case UiPage::BattleRules:case UiPage::VSRules:{
  const bool battle=s.page==UiPage::BattleRules;
  const unsigned first=battle?1:0,rows=battle?5:6;
  const char* variants[]={"RadioClass","RadioLevel","RadioMachine","RadioCourse","RadioItem","UpDownRaceNum"};
  const char* prefixes[]={"OptionClass","OptionLevel","OptionMachine","OptionCourse","OptionItem"};
  const char* labels[]={"Class","CPU","CPU Vehicles","Courses","Items","Race Count"};
  const char* values[5][4]={{"50cc","100cc","150cc","Mirror"},{"Easy","Normal","Hard",""},{"Karts","Bikes","All",""},{"Choose","Random","In Order",""},{"Balanced","Aggressive","Strategic","None"}};
  const unsigned counts[]={4,3,3,3,4};
  const unsigned selected[]={s.selection.vsClass,battle?s.selection.battleCpu:s.selection.vsCpu,battle?s.selection.battleVehicles:s.selection.vsVehicles,battle?s.selection.battleCourses:s.selection.vsCourses,battle?s.selection.battleItems:s.selection.vsItems};
  for(unsigned row=0;row<rows;++row){
   const unsigned sourceRow=first+row;
   auto p=control(sourceRow==5?(battle?"BattleSettingUpDownBase":"VSSettingUpDownBase"):(battle?"BattleSettingRadioBase":"VSSettingRadioBase"),variants[sourceRow],row);
   c.group(UiMenuRole::Choice,row,p,"rule_base_free");auto base=c.buttonState("rule_base",row);c.panel(base.c_str(),p);c.text(battle&&sourceRow==5?"Wins":battle&&sourceRow==3?"Stages":labels[sourceRow],base.c_str(),p);
   if(sourceRow<5)for(unsigned i=0;i<(battle&&sourceRow==4?3u:counts[sourceRow]);++i){
    char variant[48];std::snprintf(variant,sizeof variant,"%s%u",prefixes[sourceRow],i);
    auto option=child(p,control(battle?"BattleSettingRadioOption":"VSSettingRadioOption",variant));
    const char* family=i==selected[sourceRow]?"rule_chosen":"rule_option";
    Name key(family,s.cursor==row&&i==selected[sourceRow]?"_select":"_free");
    c.panel(key,option);c.text(values[sourceRow][i],key,option,0xffffffff,i==selected[sourceRow]?"active_text":"text");
   }else{
    auto value=child(p,control(battle?"BattleSettingUpDownValue":"VSSettingUpDownValue","Value"));
    auto vb=c.buttonState("rule_value",row);c.panel(vb.c_str(),value);
    auto text=child(value,control(battle?"BattleSettingUpDownText":"VSSettingUpDownText","Text"));auto tb=c.buttonState("rule_count",row);
    char caption[32];std::snprintf(caption,sizeof caption,battle?"Wins: %u":"Races: %u",battle?s.selection.battleWins:s.selection.vsRaces);c.text(caption,tb.c_str(),text);
    c.panel(s.cursor==row?"rule_left_select":"rule_left_free",child(p,control(battle?"BattleSettingUpDownButtonL":"VSSettingUpDownButtonL","LeftButton")));
    c.panel(s.cursor==row?"rule_right_select":"rule_right_free",child(p,control(battle?"BattleSettingUpDownButtonR":"VSSettingUpDownButtonR","RightButton")));
   }
  }
  c.group(UiMenuRole::Auxiliary,rows,control(battle?"BattleSetting":"VSSetting","ButtonOK",rows),"button_free");c.button("button",rows,control(battle?"BattleSetting":"VSSetting","ButtonOK",rows),"OK");
  const unsigned sourceFocus=first+s.cursor;
  const char* help=sourceFocus<5?UiVsRulesHelp[sourceFocus*4+selected[sourceFocus]]:sourceFocus==5?(battle?"":UiVsRulesHelp[20]):UiVsRulesHelp[21];
  c.chrome("Rules",help);break;
 }
 case UiPage::Cup:case UiPage::Course:{
  bool pickCup=s.page==UiPage::Cup;unsigned selectedCup=pickCup?std::min(s.cursor,7u):course/4;
  Pose cups=control("CupSelectNULL","CupSelectCupNULL"),courses=control("CupSelectNULL","CupSelectCourseNULL");
  // The chosen cup draws last (retail lifts it to z+200) over its neighbours.
  for(unsigned n=0;n<8;++n){const unsigned i=n<selectedCup?n:n==7?selectedCup:n+1;
   if(!s.cupUnlocked(i))continue;
   Pose p=child(cups,numbered(pickCup?"CupSelectCup":"CourseSelectCup",i));const char* base=i==selectedCup?"cup_select":"cup_free";
   if(!pickCup){
    // CtrlMenuCourseSelectCupSub::calcSelf (USA 807de7d4): t advances 20 per
    // frame to 300. The chosen cup slides to the CupSelectCupNULL origin and
    // grows to 1.5x; the other cups fade out (alpha 300-t, clamped to 255).
    const float t=std::min(300.f,20.f*float(s.age+1));
    if(i==selectedCup){const float k=t/300.f;p.x+=(cups.x-p.x)*k;p.y+=(cups.y-p.y)*k;p.sx*=1+t/600.f;p.sy*=1+t/600.f;}
    else{p.alpha*=std::clamp(300.f-t,0.f,255.f)/255.f;if(p.alpha<=0)continue;}
   }
   c.group(pickCup?UiMenuRole::Choice:UiMenuRole::Name,i,p,"cup_select");c.layout.selected=i==selectedCup;c.panel(base,p);
   Pose iconPose=p;
   if(pickCup&&(i!=selectedCup||!s.cupUnlocked(i))){
    // Centre resting emblems; retain the original lifted selected pose.
    const auto* frame=uiFindPanel(base);const auto* emblem=uiFindPane(base,"icon");
    if(frame&&emblem){iconPose.x+=(frame->x+frame->w*.5f-emblem->x-emblem->w*.5f)*p.sx;
     iconPose.y-=(frame->y+frame->h*.5f-emblem->y-emblem->h*.5f)*p.sy;}
   }
   if(!s.cupUnlocked(i)){c.inPane("locked_choice",base,"icon",iconPose);continue;}
   c.icon("cup_",i,base,"icon",iconPose);
   c.glowGroup("cup",iconPose,nullptr,i,true);
   if(pickCup){
    if(i==selectedCup){
     c.glow("cup",p,nullptr,i);
     // Put the selected caption below its lifted emblem.
     c.groupPane(UiMenuRole::Auxiliary,p,"cup_select","text");c.layout.group=i;
     const auto firstName=c.out.size();
     c.text(CupNames[i],"cup_select",p);
     float left=400,top=240,right=0,bottom=0;
     for(auto n=firstName;n<c.out.size();++n){const auto& q=c.out[n];
      left=std::min(left,q.x);top=std::min(top,q.y);
      right=std::max(right,q.x+q.w);bottom=std::max(bottom,q.y+q.h);
     }
     for(auto n=firstName;n<c.out.size();++n){auto& g=c.out[n].menu;
      g.x=left;g.y=top;g.w=right-left;g.h=bottom-top;
     }
    }
   }else if(i==selectedCup)c.text(CupNames[i],"cup_select",p);
  }
  if(pickCup&&!s.cupUnlocked(selectedCup)){c.chrome("Cups");break;}
  if(pickCup){
   Pose movie=child(cups,control("CourseSelectMovie","RaceCupMovie"));c.group(UiMenuRole::Preview,0,movie,"course_movie_frame");
   c.panel("course_movie_frame",movie);c.movie("cup_movie_",selectedCup,"course_movie_frame","black_base",movie);
   const int highlighted=s.previewCourse;
   for(unsigned i=0;i<4;++i){Pose p=child(courses,numbered("CupSelectCourse",i,"Course"));p.focusSlot=UiFocusMainParentBase+i;c.group(UiMenuRole::PreviewLabel,i,p,"cup_course_free");
    const char* base=i==highlighted?"cup_course_select":"cup_course_free";
    // CupSelectCourse owns the animated color_down film as well as text.
    // Drawing only its label drops the shade over the three inactive rows.
    c.panel(base,p);c.text(CourseNames[selectedCup*4+i],base,p);
    if(preload){
     const char* other=i==highlighted?"cup_course_free":"cup_course_select";
     c.panel(other,p);c.text(CourseNames[selectedCup*4+i],other,p);
    }
   }
  }else{
   for(unsigned i=0;i<4;++i){Pose p=child(courses,numbered("CourseSelectCourse",i));unsigned ci=selectedCup*4+i;c.group(UiMenuRole::Choice,i,p,"course_free");
    const char* base=s.cursor==i?"course_select":"course_free";c.movieButton(base,p);c.movie("course_preview_",ci,base,"black_base",p);c.movieOverlay(base,p);c.text(CourseNames[ci],base,p);if(s.cursor==i)c.glow("course",p,CourseNames[ci]);
   }
   Pose movie{};c.group(UiMenuRole::Preview,0,movie,"course_movie_frame");
   c.panel("course_movie_frame",movie);c.movie("course_preview_",selectedCup*4+std::min(s.cursor,3u),"course_movie_frame","black_base",movie);
  }
  c.chrome(pickCup?"Cups":"Courses");c.cupRecord(selectedCup);break;
 }
 case UiPage::BattleCup:case UiPage::BattleStage:{
  const bool pick=s.page==UiPage::BattleCup;const unsigned cup=pick?std::min(s.cursor,1u):std::min(s.selection.battleStage/5,1u);
  const char* names[]={"Wii Stages","Retro Stages"};
  const Pose cups=control("BattleCupSelectNULL","CupSelectCupNULL"),stages=control("BattleCupSelectNULL","CupSelectCourseNULL");
  for(unsigned i=0;i<2;++i){
   const Pose p=child(cups,numbered(pick?"BattleCupSelectCup":"BattleStageSelectCup",i));
   const char* base=i==cup?"cup_select":"cup_free";
   c.group(pick?UiMenuRole::Choice:UiMenuRole::Name,i,p,"cup_select");c.layout.selected=i==cup;c.panel(base,p);
   Pose iconPose=p;
   if(pick&&i!=cup){
    const auto* frame=uiFindPanel(base);const auto* emblem=uiFindPane(base,"icon");
    if(frame&&emblem){iconPose.x+=(frame->x+frame->w*.5f-emblem->x-emblem->w*.5f)*p.sx;
     iconPose.y-=(frame->y+frame->h*.5f-emblem->y-emblem->h*.5f)*p.sy;}
   }
   c.icon("battle_cup_",i,base,"icon",iconPose);c.glowGroup("cup",iconPose,nullptr,8+i,true);
   if(i==cup){
    if(pick){
     c.glow("cup",p,nullptr,8+i);
     c.groupPane(UiMenuRole::Auxiliary,p,"cup_select","text");c.layout.group=i;
     const auto firstName=c.out.size();c.text(names[i],"cup_select",p);
     float left=400,top=240,right=0,bottom=0;
     for(auto n=firstName;n<c.out.size();++n){const auto& q=c.out[n];left=std::min(left,q.x);top=std::min(top,q.y);right=std::max(right,q.x+q.w);bottom=std::max(bottom,q.y+q.h);}
     for(auto n=firstName;n<c.out.size();++n){auto& g=c.out[n].menu;g.x=left;g.y=top;g.w=right-left;g.h=bottom-top;}
    }else c.text(names[i],"cup_select",p);
   }
  }
  if(pick){
   const Pose movie=child(cups,control("CourseSelectMovie","BattleCupMovie"));c.group(UiMenuRole::Preview,0,movie,"course_movie_frame");
   c.panel("course_movie_frame",movie);c.movie("battle_cup_movie_",cup,"course_movie_frame","black_base",movie);
   const unsigned highlighted=(unsigned(double(s.age)*59.94/60.0)/120)%5;
   for(unsigned i=0;i<5;++i){
    const Pose p=child(stages,numbered("BattleCupSelectStage",i,"Course"));c.group(UiMenuRole::PreviewLabel,i,p,"battle_course_free");
    const char* base=i==highlighted?"battle_course_select":"battle_course_free";
    // The source row contains a shade film as well as its stage label,
    // just like CupSelectCourse. Both states must be resident before playback.
    c.panel(base,p);c.text(UiBattleStageNames[cup*5+i],base,p);
    if(preload){
     const char* other=i==highlighted?"battle_course_free":"battle_course_select";
     c.panel(other,p);c.text(UiBattleStageNames[cup*5+i],other,p);
    }
   }
  }else{
   // The selected stage's native atlas region is split across five controls,
   // matching BattleStage_ControlStage_OnSelect rather than five previews.
   for(unsigned i=0;i<5;++i){
    const Pose p=child(stages,numbered("BattleStageSelectStage",i));c.group(UiMenuRole::Choice,i,p,"course_free");
    const char* base=s.cursor==i?"course_select":"course_free";
    c.movieButton(base,p);c.movie("battle_preview_",cup*5+std::min(s.cursor,4u),base,"black_base",p,1,0,float(i)/5,1,float(i+1)/5);
    c.movieOverlay(base,p);c.text(UiBattleStageNames[cup*5+i],base,p);
    if(s.cursor==i)c.glow("course",p,UiBattleStageNames[cup*5+i]);
   }
   Pose movie{};c.group(UiMenuRole::Preview,0,movie,"course_movie_frame");
   c.panel("course_movie_frame",movie);c.movie("battle_preview_",cup*5+std::min(s.cursor,4u),"course_movie_frame","black_base",movie);
  }
  c.chrome(pick?"Cups":"Stages");break;
 }
 case UiPage::Ghost:{
  // No ghost enumeration yet: do not claim imported saves contain no ghosts.
  // Keep the supported solo action; deferred race/replay actions stay hidden.
  c.timeTrialBest();
  auto solo=control("TimeAttackGhostList","Alone",0);solo.y=0;
  c.group(UiMenuRole::Choice,0,solo,"ghost_free");c.button("ghost",0,solo,"Solo Time Trials");
  c.chrome("Time Trials");break;
 }
 case UiPage::DriftHelp:{
  c.group(UiMenuRole::Shade);c.panel("confirm_back",control("RaceConfirmBack","BlackBack"));
  auto window=control("MessageWindow","MessageWindow");c.group(UiMenuRole::Dialog,0,window,"drift_message");
  c.panel("drift_message",window);c.text(UiDriftHelpMessage[0],"drift_message",window);
  c.group(UiMenuRole::Auxiliary,0,control("OKKey","OKKey"),"message_ok");c.panel("message_ok",control("OKKey","OKKey"));
  c.text("A","message_ok",control("OKKey","OKKey"),0xffffffff,"a_button");break;
 }
 case UiPage::Confirm:{
  c.group(UiMenuRole::Shade);c.panel("confirm_back",control("RaceConfirmBack","BlackBack"));
  c.group(UiMenuRole::Header);c.panel("confirm_top",control("MenuObiTopRaceConfirm","MenuEndConfirmObiTop"));
  c.group(UiMenuRole::Footer);c.panel("confirm_bottom",control("MenuObiBottomRaceConfirm","MenuEndConfirmObiBottom"));
  c.groupPane(UiMenuRole::Title,control("MenuObiTitleText","MenuEndConfirmObiTitleText"),"header_text","text");c.text("Start Race","header_text",control("MenuObiTitleText","MenuEndConfirmObiTitleText"));
  Pose back=control("Back","ButtonBackMenuEndConfirm");c.groupPane(UiMenuRole::Back,back,"back_free","touch");c.panel("back_free",back);c.inPane("button_back","back_free","text",back);
  c.group(UiMenuRole::Choice,0,control("MenuEndConfirm","ButtonOK"),"confirm_free");c.button("confirm",0,control("MenuEndConfirm","ButtonOK"),"OK");break;
 }
 case UiPage::Pause:{
  c.chrome("Pause");
  for(unsigned i=0;i<s.pauseCount();++i){
   auto p=control(uiPauseMenuControl(s),s.pauseVariant(i));
   // Match the main project's three-action pause layout. TT retains its
   // five authored slots, including course/character changes.
   if(s.pauseCount()==3)p.y=90.f-float(i)*85.f;
   else if(s.selection.mode==UiMode::TimeTrials){
    // Five entries fit between the header and Back footer, including focus growth.
    p.y=128.f-float(i)*64.f;p.sx*=.82f;p.sy*=.82f;
   }
   // Use the common menu button's Select/SelectIn/OK font material and
   // tracks, retaining the authored pause positions/delays above.
   p.source="OptionTopButton";
   c.group(UiMenuRole::Choice,i,p,"settings_button_free");c.button("settings_button",i,p,s.pauseLabel(i));
  }break;
 }
 case UiPage::EraseLicense:case UiPage::QuitConfirm:{
  c.chrome(s.page==UiPage::EraseLicense?"Erase License?":"Quit this race?");auto yes=control("YesNoButton","Option0"),no=control("YesNoButton","Option1");
  yes.x=no.x=0;yes.y=50;no.y=-55;
  const bool quit=s.page==UiPage::QuitConfirm;
  if(quit)yes.source=no.source="OptionTopButton";
  const char* family=quit?"settings_button":"button";
  const char* panel=quit?"settings_button_free":"button_free";
  c.group(UiMenuRole::Choice,0,yes,panel);c.button(family,0,yes,"Yes");
  c.group(UiMenuRole::Choice,1,no,panel);c.button(family,1,no,"No");break;
 }
 case UiPage::Error:{c.chrome("Race unavailable");auto p=control("ConfirmOK","OK");p.x=p.y=0;
  c.group(UiMenuRole::Choice,0,p,"button_free");c.button("button",0,p,"Choose another course");break;}
 default:break;
 }
 c.notice();
 // The title uses the real last-selected license control, not a second card.
 // Its one draw list crosses the two viewports and ends exactly at the
 // license preview rectangle. Incoming license controls animate around it.
 if(s.page==UiPage::Title||(s.page==UiPage::License&&transition&&transition->active&&((transition->entering&&transition->from==UiPage::Title)||(!transition->entering&&transition->to==UiPage::Title)))){
  if(s.page==UiPage::Title){
   c.group(UiMenuRole::Background);c.panel("background");
   c.group(UiMenuRole::Header);c.panel("header",motionOnly("MenuObiTop","MenuObiTop"));
   c.group(UiMenuRole::Footer);c.panel("footer",control("MenuObiBottom","MenuObiBottom"));
  }
  MenuState license=s;license.enter(UiPage::License);license.cursor=std::min(s.page==UiPage::License?s.cursor:s.titleLicense<4?s.titleLicense:s.selection.license,3u);license.advance(120);
  std::vector<UiQuad> card;composeMenuImpl(license,{},card,nullptr);
  float phase=s.page==UiPage::License?1.f:0.f;
  if(s.page==UiPage::Title&&transition&&transition->active&&!transition->entering&&transition->to==UiPage::License)
   phase=std::abs(uiPageTransitionProgress(transition->elapsed,0,transition->maxDelay,false,false,transition->exitHold));
  if(s.page==UiPage::License&&transition&&!transition->entering&&transition->to==UiPage::Title)
   phase=1.f-std::abs(uiPageTransitionProgress(transition->elapsed,0,transition->maxDelay,false,true,transition->exitHold));
  for(auto item:card){if(item.menu.role!=UiMenuRole::Choice||item.menu.group!=license.cursor)continue;
   item.menu.role=UiMenuRole::TitleCard;item.screenSlide=0;item.titleTransfer=phase;q.push_back(item);
   // A narrow additive sweep samples the same card silhouette and UVs.
   if(s.page!=UiPage::Title||phase>0||(item.sprite<std::size(UiSprites)&&*UiSprites[item.sprite].text))continue;
   const float sweep=std::fmod(float(s.age),120.f);if(sweep>=30)continue;
   const float left=item.menu.x-item.menu.h*.65f-30+sweep/30.f*(item.menu.w+item.menu.h*.65f+60);
   // Slice the existing silhouette into narrow rows; UVs follow each row,
   // producing a diagonal light band without lighting transparent corners.
   for(float y=item.y;y<item.y+item.h;y+=2){
    const float h=std::min(2.f,item.y+item.h-y),stripe=left+(y-item.menu.y)*.65f;
    const float l=std::max(stripe,item.x),r=std::min(stripe+18,item.x+item.w);if(r<=l)continue;
    auto light=item;const float du=item.u1-item.u0,dv=item.v1-item.v0;
    light.u0=item.u0+du*(l-item.x)/item.w;light.u1=item.u0+du*(r-item.x)/item.w;
    light.v0=item.v0+dv*(y-item.y)/item.h;light.v1=item.v0+dv*(y+h-item.y)/item.h;
    light.x=l;light.y=y;light.w=r-l;light.h=h;light.alpha*=.55f;light.additive=true;q.push_back(light);
   }
  }
 }
}
void composeMenu(const MenuState& s,const UiRaceView& race,std::vector<UiQuad>& q){composeMenuImpl(s,race,q,nullptr);}
void composeMenu(const MenuState& s,const UiRaceView& race,std::vector<UiQuad>& q,const UiTransitionView& t){
 composeMenuImpl(s,race,q,&t);
 if(s.page==UiPage::Course||s.page==UiPage::BattleStage){
  const float frame=t.active?std::max(0.f,t.elapsed):float(s.age);
  const float alpha=std::clamp(frame/15.f,0.f,1.f);
  for(auto& movie:q)if(movie.menu.role==UiMenuRole::Preview&&movie.movie!=~0u){
   // The full preview stays at its settled rectangle. Only opacity follows
   // the page clock; button/label slide animations remain independent.
   movie.screenSlide=0;
   movie.alpha*=t.active&&!t.entering?1.f-alpha:alpha;
  }
 }
}
namespace {
struct MenuRect {float x,y,w,h;};
bool unchangedMenu(UiPage page){
 return page==UiPage::Strap||page==UiPage::Intro||page==UiPage::Pause||page==UiPage::QuitConfirm||page==UiPage::Race||page==UiPage::Results;
}
// Map one authored control group, not the whole page. Picture frames may
// change aspect to use the LCD; glyphs and character emblems retain theirs.
void placeMenuQuad(const UiQuad& source,MenuRect target,bool lower,std::vector<UiQuad>& out,bool leftAlign=false){
 auto q=source;const auto& g=source.menu;if(g.w<=0||g.h<=0)return;
 float sx=target.w/g.w,sy=target.h/g.h;
 float ox=target.x-g.x*sx,oy=target.y-g.y*sy;
 if(leftAlign){sx=sy=std::min(sx,sy);ox=target.x-g.x*sx;oy=target.y+(target.h-g.h*sy)*.5f-g.y*sy;}
 auto point=[&](float x,float y){return std::array<float,2>{x*sx+ox,y*sy+oy};};
 auto center=point(q.x+q.w*.5f,q.y+q.h*.5f);
 // Individually cooked native glyphs still form one string. Stretching
 // their centres with the button while preserving each glyph's aspect
 // produces tracking gaps; map the entire run about the button centre.
 if(g.page==UiPage::LicenseSettings&&g.role==UiMenuRole::Choice&&
    q.sprite<std::size(UiSprites)&&!std::strncmp(UiSprites[q.sprite].key,"name_glyph_",11))
  center[0]=target.x+target.w*.5f+(source.x+source.w*.5f-(g.x+g.w*.5f))*std::min(sx,sy);
 if(q.affine){for(auto& p:q.positions)p=point(p[0],p[1]);}
 q.x=q.x*sx+ox;q.y=q.y*sy+oy;q.w*=sx;q.h*=sy;
 if(g.preserveAspect&&!q.affine&&!leftAlign){
  const float scale=std::min(sx,sy);q.w=source.w*scale;q.h=source.h*scale;
  q.x=center[0]-q.w*.5f;q.y=center[1]-q.h*.5f;
 }
 q.bottom=lower;
 // Viewport crop after reflow, preserving UVs. Affine glyphs are left for
 // GPU clipping; clipping their AABB would cut the rotating corners.
 if(!q.affine&&q.screenSlide==0){
  const float l=std::max(0.f,q.x),t=std::max(0.f,q.y),r=std::min(lower?320.f:400.f,q.x+q.w),b=std::min(240.f,q.y+q.h);
  if(r<=l||b<=t)return;
  const float u0=(l-q.x)/q.w,u1=(r-q.x)/q.w,v0=(t-q.y)/q.h,v1=(b-q.y)/q.h;
  auto color=[&](float u,float v){uint32_t result=0;for(unsigned shift:{24u,16u,8u,0u}){
   const float a=float((q.corners[0]>>shift)&255),b=float((q.corners[1]>>shift)&255),c=float((q.corners[2]>>shift)&255),d=float((q.corners[3]>>shift)&255);
   const float value=u>=v?a+u*(b-a)+v*(d-b):a+u*(d-c)+v*(c-a);
   result|=uint32_t(std::clamp(std::lround(value),0l,255l))<<shift;
  }return result;};
  const std::array<uint32_t,4> corners={color(u0,v0),color(u1,v0),color(u0,v1),color(u1,v1)};
  const float oldU=q.u0,oldV=q.v0,du=q.u1-q.u0,dv=q.v1-q.v0;
  q.u0=oldU+du*u0;q.u1=oldU+du*u1;q.v0=oldV+dv*v0;q.v1=oldV+dv*v1;q.corners=corners;
  q.x=l;q.y=t;q.w=r-l;q.h=b-t;
 }
 out.push_back(q);
}
MenuRect menuChoiceRect(const UiQuad& q,const MenuState& state){
 const unsigned i=q.menu.group;
 if(q.menu.page==UiPage::Character||q.menu.page==UiPage::Vehicle||q.menu.page==UiPage::Cup||q.menu.page==UiPage::BattleCup||q.menu.page==UiPage::EngineClass){
  // Underlays/outgoing controls keep the layout of their own page.
  // The active page may already be Course/Confirm while these are Cup quads.
  MenuState choices=state;choices.page=q.menu.page;
  const unsigned n=choices.visibleChoiceCount(),k=choices.visibleChoiceIndex(i),columns=choices.columns();
  const bool cupCard=choices.page==UiPage::Cup||choices.page==UiPage::BattleCup;
  const float w=choices.page==UiPage::Character?71.f:choices.page==UiPage::Vehicle?(columns==1?156.f:126.f):cupCard?67.f:296.f;
  const float h=choices.page==UiPage::Character?23.f:choices.page==UiPage::Vehicle?28.f:cupCard?61.f:37.f;
  const float dx=choices.page==UiPage::Character?77.f:choices.page==UiPage::Vehicle?150.f:cupCard?77.f:300.f;
  const float dy=choices.page==UiPage::Character?25.f:choices.page==UiPage::Vehicle?28.f:cupCard?80.f:42.f;
  const unsigned rows=(n+columns-1)/columns,row=k/columns,items=std::min(columns,n-row*columns);
  // Original Mii entries occupy the right of the final character row.
  // Keep the same right-aligned compact row as MenuState::moveGrid.
  const float x=choices.page==UiPage::Character?160.f-(w+dx*float(columns-1))*.5f+dx*float(columns-items+k%columns):
      160.f-(w+dx*float(items-1))*.5f+dx*float(k%columns);
  return {x,120.f-(h+dy*float(rows-1))*.5f+dy*float(row),w,h};
 }
 switch(q.menu.page){
 case UiPage::License:return {34.9f+155.f*(i%2),41.3f+82.f*(i/2),95.2f,71.4f};
 case UiPage::MainMenu:return {8.f+156.f*(i%2),34.f+87.f*(i/2),148,80};
 case UiPage::Character:{
  // MenuState maps the two Mii browse entries to columns 2/3. Retain that
  // final-row alignment so moving vertically still lands in the same column.
  const float x=i<24?9.f+77.f*(i%4):163.f+77.f*(i-24);
  return {x,33.f+25.f*(i/4),71,23};
 }
 case UiPage::Vehicle:return q.menu.columns==1?MenuRect{82,35.f+28.f*i,156,28}:MenuRect{22.f+150.f*(i%2),35.f+28.f*(i/2),126,28};
 case UiPage::Mode:return {12,34.f+43.f*i,296,38};
 case UiPage::EngineClass:return {12,(state.engineUnlocked(3)?33.f:54.f)+42.f*i,296,37};
 case UiPage::Drift:return {22,39.f+79.f*i,276,70};
 case UiPage::VSMode:case UiPage::BattleMode:return {16,42.f+82.f*i,288,72};
 case UiPage::Cup:return {11.f+77.f*(i%4),48.f+80.f*(i/4),67,61};
 case UiPage::BattleCup:return {25.f+150.f*i,70,120,100};
 case UiPage::Course:return {12,32.f+42.f*i,296,36};
 case UiPage::BattleStage:return {12,34.f+35.f*i,296,30};
 case UiPage::VSRules:return {0,32.f+27.f*i,304,25};
 case UiPage::BattleRules:return {0,42.f+29.f*i,304,27};
 case UiPage::Settings:return {12,46.f+54.f*i,296,44};
 case UiPage::LicenseSettings:return {12,33.f+35.f*i,296,31};
 case UiPage::TeamConfirm:return {52,95,216,48};
 case UiPage::Confirm:return {72,83,176,80};
 case UiPage::EraseLicense:return {35,65.f+75.f*i,250,52};
 case UiPage::Ghost:return {25,89,270,54};
 case UiPage::Unlock:return {96,190,128,30};
 case UiPage::Error:return {15,93,290,48};
 default:return {10,35.f+39.f*i,300,34};
 }
}
}
void layoutMenuScreens(const MenuState& state,const std::vector<UiQuad>& source,std::vector<UiQuad>& top,std::vector<UiQuad>& bottom){
 top.clear();bottom.clear();top.reserve(source.size());bottom.reserve(source.size());
 if(unchangedMenu(state.page)){top=source;for(auto& q:top)q.bottom=false;return;}
 const bool fullMovie=state.page==UiPage::Course||state.page==UiPage::BattleStage||state.page==UiPage::Confirm;
 for(const auto& q:source){
  const auto& g=q.menu;const unsigned i=g.group;
  const bool upperConfirm=g.page==UiPage::Confirm;
  switch(g.role){
  case UiMenuRole::Background:
   if(!std::strcmp(UiSprites[q.sprite].key,"title_scene")||!std::strcmp(UiSprites[q.sprite].key,"title_blur_scene"))break;
   // main has already inserted this backdrop underneath the top 3D scene.
   placeMenuQuad(q,{0,0,320,240},true,bottom);break;
  case UiMenuRole::Header:case UiMenuRole::Footer:
   if(g.page==UiPage::Title){placeMenuQuad(q,{0,0,320,240},true,bottom);break;}
   placeMenuQuad(q,{0,0,400,240},false,top);if(!upperConfirm)placeMenuQuad(q,{0,0,320,240},true,bottom);break;
  case UiMenuRole::Title:
   if(g.page==UiPage::Unlock){
    auto title=q;const auto& sprite=UiSprites[q.sprite];
    const float h=std::min(12.f,372.f*sprite.h/sprite.w),w=h*sprite.w/sprite.h;
    title.x=200-w*.5f;title.y=6;title.w=w;title.h=h;title.affine=false;title.bottom=false;top.push_back(title);break;
   }
   placeMenuQuad(q,{14,6,372,20},false,top,true);if(!upperConfirm)placeMenuQuad(q,{12,6,296,20},true,bottom,true);break;
  case UiMenuRole::Description:
   {auto text=q;const auto& a=UiSprites[q.sprite];
    const float h=std::min(14.f,384.f*a.h/a.w),w=h*a.w/a.h;
    text.x=200-w*.5f;text.y=222;text.w=w;text.h=h;text.affine=false;
    text.bottom=false;top.push_back(text);break;}
  case UiMenuRole::Back:
   if(!upperConfirm)placeMenuQuad(q,{8,212,66,22},true,bottom);break;
  case UiMenuRole::Choice:{
   if(upperConfirm)placeMenuQuad(q,{112,83,176,80},false,top);
   else placeMenuQuad(q,menuChoiceRect(q,state),true,bottom);
   // The selected card/film supplies the upper page's preview without a
   // second asset set. The selection flag belongs to the composed page,
   // so opening a modal cannot accidentally pick a different underlay.
   if(g.selected&&g.page==UiPage::License&&!std::any_of(source.begin(),source.end(),[](const UiQuad& v){return v.menu.role==UiMenuRole::TitleCard;}))placeMenuQuad(q,{98,45.5f,204,153},false,top);
   if(g.selected&&g.page==UiPage::MainMenu)placeMenuQuad(q,{94,34,212,170},false,top);
   break;
  }
  case UiMenuRole::Auxiliary:{
   if(g.page==UiPage::MainMenu||g.page==UiPage::Drift||g.page==UiPage::VSMode||g.page==UiPage::BattleMode){
    // Retail round panes are authored for the widescreen Wii canvas.
    // Restore its source X/Y ratio rather than stretching the group to a box.
    const float sy=50.f/g.h,sx=sy*(92.f/68.f)*YScale/XScale;
    placeMenuQuad(q,{392-g.w*sx,3,g.w*sx,50},false,top);break;
   }
   const auto cell=menuChoiceRect(q,state);
   const MenuRect rect=(g.page==UiPage::Cup||g.page==UiPage::BattleCup)?MenuRect{cell.x+(cell.w-88.f)*.5f,cell.y+39.3f,88,18.4f}:
    (g.page==UiPage::VSRules||g.page==UiPage::BattleRules)?MenuRect{112,197,96,21}:
    g.page==UiPage::DriftHelp?MenuRect{136,194,48,30}:
    g.page==UiPage::EngineClass?MenuRect{180,216,128,21}:MenuRect{178,210,132,28};
   placeMenuQuad(q,rect,true,bottom);break;
  }
  case UiMenuRole::Statistics:
   placeMenuQuad(q,{14,37.f+22.f*i,132,20},false,top);break;
  case UiMenuRole::Name:
   if(g.page==UiPage::Character)placeMenuQuad(q,{120,180,160,22},false,top);
   else if(g.page==UiPage::Vehicle)placeMenuQuad(q,{180,180,180,23},false,top);

   break;
  case UiMenuRole::Preview:
   if(g.page==UiPage::Unlock){placeMenuQuad(q,{136,56,128,128},false,top,true);break;}
   if(q.previewFullAlpha>=0){
    // Crossfade two fixed rectangles, sharing the same movie stream/frame.
    // The frame and names leave with the small film; no size/position tween.
    auto small=q;small.screenSlide=0;small.alpha*=1.f-q.previewFullAlpha;
    placeMenuQuad(small,{18,54,193,130},false,top);
    if(q.movie<std::size(UiMovieRegions)){
     auto movie=q;movie.screenSlide=0;movie.alpha*=q.previewFullAlpha;
     movie.menu.x=q.x;movie.menu.y=q.y;movie.menu.w=q.w;movie.menu.h=q.h;
     std::vector<UiQuad> frame;placeMenuQuad(movie,{0,0,400,240},false,frame);
     top.insert(top.begin(),frame.begin(),frame.end());
    }
    break;
   }
   if(fullMovie&&q.movie<std::size(UiMovieRegions)){
    auto movie=q;movie.screenSlide=0;movie.menu.x=q.x;movie.menu.y=q.y;movie.menu.w=q.w;movie.menu.h=q.h;
    std::vector<UiQuad> frame;placeMenuQuad(movie,{0,0,400,240},false,frame);
    top.insert(top.begin(),frame.begin(),frame.end());break;
   }
   if(fullMovie)break;
   placeMenuQuad(q,g.page==UiPage::LicenseSettings?MenuRect{98,43,204,153}:
    g.page==UiPage::Course||g.page==UiPage::BattleStage?MenuRect{56,44,288,158}:MenuRect{18,54,193,130},false,top);break;
  case UiMenuRole::PreviewLabel:
   if(q.previewFullAlpha>=0){auto name=q;name.screenSlide=0;name.alpha*=1.f-q.previewFullAlpha;
    placeMenuQuad(name,{221,56.f+31.f*i,169,27},false,top);break;}
   if(state.page==UiPage::Confirm)break; // Full-screen movie replaces the cup summary.
   placeMenuQuad(q,{221,56.f+(g.page==UiPage::BattleCup?25.f:31.f)*i,169,27},false,top);break;
  case UiMenuRole::TimeTrialRecord:
  case UiMenuRole::CupRecord:{auto record=q;record.bottom=false;top.push_back(record);break;}
  case UiMenuRole::Information:
   if(g.page==UiPage::TeamConfirm)placeMenuQuad(q,{14.f+194.f*(i/6),37.f+28.f*(i%6),178,25},false,top);
   else placeMenuQuad(q,{14,38.f+27.f*i,372,24},false,top);
   break;
  case UiMenuRole::Shade:
   placeMenuQuad(q,{0,0,400,240},false,top);if(!upperConfirm)placeMenuQuad(q,{0,0,320,240},true,bottom);break;
  case UiMenuRole::Dialog:
   if(g.page==UiPage::Unlock&&*UiSprites[q.sprite].text){
    auto text=q;const auto& sprite=UiSprites[q.sprite];
    const float h=std::min(60.f,256.f*sprite.h/sprite.w),w=h*sprite.w/sprite.h;
    text.x=160-w*.5f;text.y=110-h*.5f;text.w=w;text.h=h;text.affine=false;text.bottom=true;bottom.push_back(text);break;
   }
   placeMenuQuad(q,g.page==UiPage::Unlock?MenuRect{16,40,288,140}:MenuRect{16,46,288,145},true,bottom);break;
  case UiMenuRole::Notice:
   placeMenuQuad(q,{36,82,248,76},true,bottom);break;
  case UiMenuRole::TitleCard:{
   const float phase=q.titleTransfer;
   const float startY=display::LowerOriginY+50.f;
   MenuRect r={90.f-10.f*phase,startY+(32.f-startY)*phase,220.f+20.f*phase,165.f+15.f*phase};
   r.x+=r.w*.075f;r.y+=r.h*.075f-12.5f*(1.f-phase);r.w*=.85f;r.h*=.85f;
   placeMenuQuad(q,r,false,top);
   placeMenuQuad(q,{r.x-40,r.y-display::LowerOriginY,r.w,r.h},true,bottom);break;
  }
  case UiMenuRole::Content:
   if(state.page==UiPage::Title){auto upper=q;upper.bottom=false;top.push_back(upper);break;}
   // Future untagged content remains visible, but does not move race UI.
   placeMenuQuad(q,{0,0,320,240},true,bottom);break;
  }
 }
 // Glyph runs share the graph's left anchor after aspect-preserving reflow.
 for(auto& q:top)if(q.menu.role==UiMenuRole::Statistics&&*UiSprites[q.sprite].text){const float dx=22-q.x;q.x+=dx;for(auto& p:q.positions)p[0]+=dx;}
 float descriptionLeft=400,descriptionRight=0;
 for(const auto& q:top)if(q.menu.role==UiMenuRole::Description){descriptionLeft=std::min(descriptionLeft,q.x);descriptionRight=std::max(descriptionRight,q.x+q.w);}
 if(descriptionRight>descriptionLeft){const float dx=200-(descriptionLeft+descriptionRight)*.5f;
  for(auto& q:top)if(q.menu.role==UiMenuRole::Description){q.x+=dx;for(auto& p:q.positions)p[0]+=dx;}
 }
 // Centre the visible emblem after the frame and emblem have received their
 // different aspect-preserving transforms. Authored pane centres alone do
 // not coincide on the LCD. Bounds are the nontransparent original 47x51
 // cup sprites; leaf/lightning have asymmetric transparent padding.
 constexpr float cupCenterX[]={23.5f,23.5f,23.5f,23.5f,23.5f,23.5f,24.f,23.5f};
 constexpr float cupCenterY[]={24.5f,25.5f,25.f,26.f,25.5f,25.5f,25.5f,25.5f};
 for(auto& icon:bottom){
  if(icon.sprite>=std::size(UiSprites))continue;
  const bool locked=!std::strcmp(UiSprites[icon.sprite].key,"locked_choice");
  if(icon.menu.page!=UiPage::Cup||icon.menu.role!=UiMenuRole::Choice||(icon.menu.selected&&!locked))continue;
  const unsigned i=icon.menu.group;if(i>=8)continue;
  char key[16];std::snprintf(key,sizeof key,"cup_%u",i);
  if(!locked&&std::strcmp(UiSprites[icon.sprite].key,key))continue;
  for(const auto& frame:bottom){
   if(frame.menu.page!=UiPage::Cup||frame.menu.group!=i||std::strcmp(UiSprites[frame.sprite].key,icon.menu.selected?"cup_select":"cup_free"))continue;
   icon.x=frame.x+frame.w*.5f-icon.w*(locked?.5f:cupCenterX[i]/47.f);
   icon.y=frame.y+frame.h*.5f-icon.h*(locked?.5f:cupCenterY[i]/51.f);
   break;
  }
 }
 // Centre the caption in the visible gap between emblem and button bottom.
 for(unsigned i=0;i<8;++i){
  const UiQuad* icon=nullptr;const UiQuad* frame=nullptr;float t=240,b=0;
  char key[16];std::snprintf(key,sizeof key,"cup_%u",i);
  for(const auto& q:bottom){if(q.menu.page!=UiPage::Cup||q.menu.group!=i)continue;
   if(q.menu.role==UiMenuRole::Choice&&q.menu.selected){if(!std::strcmp(UiSprites[q.sprite].key,key))icon=&q;if(!std::strcmp(UiSprites[q.sprite].key,"cup_select"))frame=&q;}
   if(q.menu.role==UiMenuRole::Auxiliary){t=std::min(t,q.y);b=std::max(b,q.y+q.h);}
  }
  if(!icon||!frame||b<=t)continue;
  constexpr float visibleBottom[]={48,51,50,49,47,49,49,50};
  const float gapTop=icon->y+icon->h*visibleBottom[i]/51.f,gapBottom=frame->y+frame->h*43.f/44.f;
  const float dy=(gapTop+gapBottom-t-b)*.5f;
  for(auto& q:bottom)if(q.menu.page==UiPage::Cup&&q.menu.group==i&&q.menu.role==UiMenuRole::Auxiliary){q.y+=dy;for(auto& p:q.positions)p[1]+=dy;}
 }
 // CourseSelect reuses the complete active CupSelect control, including its
 // caption spacing. Only its anchor changes; no separate mini-badge layout.
 const bool battleBadge=state.selection.mode==UiMode::Battle;
 const auto coursePage=battleBadge?UiPage::BattleStage:UiPage::Course,cupPage=battleBadge?UiPage::BattleCup:UiPage::Cup;
 const bool courseBadge=state.page==coursePage||(state.page==UiPage::Confirm&&
  std::any_of(source.begin(),source.end(),[&](const UiQuad& q){return q.menu.page==coursePage&&q.menu.role==UiMenuRole::Name&&q.menu.selected;}));
 if(courseBadge){
  MenuState cup=state;cup.enter(cupPage);cup.cursor=battleBadge?std::min(state.selection.battleStage/5,1u):std::min(state.selection.course/4,7u);cup.advance(120);
  std::vector<UiQuad> authored,cupTop,cupBottom;composeMenu(cup,{},authored);layoutMenuScreens(cup,authored,cupTop,cupBottom);
  UiQuad anchor{};anchor.menu.page=cupPage;anchor.menu.group=cup.cursor;
  const auto cell=menuChoiceRect(anchor,cup);
  const float dx=14-cell.x,dy=60-cell.y;
  float badgeSlide=0;for(const auto& q:source)if(q.menu.page==coursePage&&q.menu.role==UiMenuRole::Name&&q.menu.selected){badgeSlide=q.screenSlide;break;}
  // Signed native motion: forward enters from right/exits left; Back
  // enters from left/exits right. Each edge needs its own travel distance.
  float left=400,right=0;
  for(const auto& q:cupBottom)if(q.menu.page==cupPage&&q.menu.group==cup.cursor&&
    (q.menu.role==UiMenuRole::Choice||q.menu.role==UiMenuRole::Auxiliary)){
    left=std::min(left,q.x+dx);right=std::max(right,q.x+q.w+dx);
  }
  const float badgeTravel=(badgeSlide>0?400.f-left+12.f:right+12.f)/520.f;
  for(auto q:cupBottom){if(q.menu.page!=cupPage||q.menu.group!=cup.cursor)continue;
   if(q.menu.role!=UiMenuRole::Choice&&q.menu.role!=UiMenuRole::Auxiliary)continue;
   q.x+=dx;q.y+=dy;for(auto& p:q.positions){p[0]+=dx;p[1]+=dy;}q.bottom=false;
   q.menu.page=coursePage;q.menu.role=UiMenuRole::Name;q.screenSlide=badgeSlide*badgeTravel;top.push_back(q);
  }
 }
 // Finish reflow/centering first, then animate across the entire LCD. Only
 // the final viewport clips it; the old control envelope never clips travel.
 for(auto* list:{&top,&bottom}){
  std::vector<UiQuad> moved;moved.reserve(list->size());
  for(auto q:*list){if(q.screenSlide==0){moved.push_back(q);continue;}
   const float width=q.bottom?320.f:400.f,dx=q.screenSlide*(width+120.f);
   q.x+=dx;for(auto& p:q.positions)p[0]+=dx;
   const auto metadata=q.menu;q.screenSlide=0;
   q.menu.x=0;q.menu.y=0;q.menu.w=width;q.menu.h=240;q.menu.preserveAspect=false;
   const auto first=moved.size();placeMenuQuad(q,{0,0,width,240},q.bottom,moved);
   for(auto n=first;n<moved.size();++n)moved[n].menu=metadata;
  }
  list->swap(moved);
 }
}
std::uint32_t uiResidentTextureBytes(const std::vector<UiQuad>& quads){
 std::array<bool,std::size(UiSprites)> needed{};
 for(const auto& q:quads)if(q.vehicleIcon>=12&&q.movie>=std::size(UiMovieRegions)&&q.sprite<needed.size())needed[q.sprite]=true;
 std::uint32_t bytes=0;
 for(unsigned i=0;i<needed.size();++i)if(needed[i])bytes+=UiSprites[i].gpuBytes;
 return bytes;
}

}
