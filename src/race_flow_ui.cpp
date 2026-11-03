#include <mkw/race_flow_ui.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/race_flow_layout.hpp>
#include <mkw/race_intro_layout.hpp>
#include <mkw/race_results.hpp>
#include <mkw/battle_result_assets.hpp>
#include <mkw/battle_result_panel.hpp>
#include <cstdio>
#include <mkw/renderer.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace mkw {
namespace {
GPU_TEXTURE_WRAP_PARAM wrapMode(uint8_t gx){return gx==1?GPU_REPEAT:gx==2?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE;}
static_assert(sizeof(FlowQuad)==sizeof(Renderer::MaterialQuad)&&std::is_standard_layout_v<FlowQuad>&&std::is_standard_layout_v<Renderer::MaterialQuad>,
              "FlowQuad mirrors Renderer::MaterialQuad");
}

struct RaceFlowUi::Impl {
 Texture wipe;
 std::array<Texture,2> intro;unsigned introIcon=~0u;bool introLoaded=false;
 std::array<Texture,FlowResultTextures.size()> results;unsigned loaded=0;bool failed=false;
 FlowDrawer drawer;
 std::array<Texture,5> battleFont;unsigned battleLoaded=0;bool battle=false;BattleResultPanel battlePanel;
 Renderer* renderer=nullptr;
 const Texture* miiPortrait=nullptr; // borrowed from RaceBottomScreen; no allocation
 static void submit(const FlowBatch& batch,void* context){
  auto& m=*static_cast<Impl*>(context);
  const Texture* t=batch.texture==FlowMiiPortraitTexture?m.miiPortrait:batch.texture>=0&&unsigned(batch.texture)<m.results.size()?&m.results[batch.texture]:nullptr;
  if(batch.texture!=-1&&(!t||!t->ready))return;
  // The sampler wrap is set where the quads are drawn (the render thread in races).
  m.renderer->drawScreenMaterialQuads(t,reinterpret_cast<const Renderer::MaterialQuad*>(batch.quads),batch.count,batch.color0,batch.color1,
                                     wrapMode(batch.wrapS),wrapMode(batch.wrapT));
 }
 static void submitIntro(const FlowBatch& batch,void* context){
  auto& m=*static_cast<Impl*>(context);
  if(batch.texture<0||unsigned(batch.texture)>=m.intro.size())return;
  auto& t=m.intro[batch.texture];if(!t.ready)return;
  m.renderer->drawScreenMaterialQuads(&t,reinterpret_cast<const Renderer::MaterialQuad*>(batch.quads),batch.count,batch.color0,batch.color1,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
 }
 static void submitBattle(const FlowBatch& batch,void* context){
  auto& m=*static_cast<Impl*>(context);if(batch.texture<0||unsigned(batch.texture)>=m.battleFont.size()||!m.battleFont[batch.texture].ready)return;
  m.renderer->drawScreenMaterialQuads(&m.battleFont[batch.texture],reinterpret_cast<const Renderer::MaterialQuad*>(batch.quads),batch.count,batch.color0,batch.color1,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
 }

};

RaceFlowUi::RaceFlowUi():impl_(std::make_unique<Impl>()){}
RaceFlowUi::~RaceFlowUi()=default;

bool RaceFlowUi::load(){
 auto& m=*impl_;
 if(!m.wipe.ready){
  if(!m.wipe.load("textures/flow_wipe.t3x"))return false;
  C3D_TexSetWrap(&m.wipe.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE); // sampler 0x1c04: clamp, clamp
 }
 return true;
}

void RaceFlowUi::unload(){impl_->wipe.reset();unloadResults();unloadIntro();}

bool RaceFlowUi::loadIntro(const RaceIntroInfo& info){
 auto& m=*impl_;const unsigned icon=mkw::introIcon(info);
 // Only the current mode/cup texture is loaded; no full menu cache or all-cup atlas.
 if(!m.intro[0].ready&&!m.intro[0].load(IntroTextures[0]))return false;
 if(info.mode!=2&&(!m.intro[1].ready||m.introIcon!=icon)){
  m.intro[1].reset();if(!m.intro[1].load(IntroTextures[icon+1]))return false;
 }
 m.introIcon=icon;m.introLoaded=true;return true;
}
void RaceFlowUi::unloadIntro(){for(auto& t:impl_->intro)t.reset();impl_->introLoaded=false;impl_->introIcon=~0u;}
void RaceFlowUi::drawIntro(Renderer& renderer,const RaceIntroInfo& info,unsigned frame){
 auto& m=*impl_;if(!m.introLoaded)return;
 m.renderer=&renderer;m.drawer.drawIntro(info,frame,&Impl::submitIntro,&m);
}

bool RaceFlowUi::loadResultsStep(bool battle){
 auto& m=*impl_;m.battle=battle;
 if(m.failed)return false;
 if(m.loaded<m.results.size()){
  if(!m.results[m.loaded].load(FlowResultTextures[m.loaded])){m.failed=true;return false;}
  ++m.loaded;
 }
 if(m.loaded==m.results.size()&&battle&&m.battleLoaded<m.battleFont.size()){if(!m.battleFont[m.battleLoaded].load(BattleResultTextures[m.battleLoaded])){m.failed=true;return false;}++m.battleLoaded;}
 return m.loaded==m.results.size()&&(!battle||m.battleLoaded==m.battleFont.size());
}
void RaceFlowUi::unloadResults(){
 auto& m=*impl_;for(auto& t:m.results)t.reset();
 m.loaded=0;m.failed=false;m.miiPortrait=nullptr;m.drawer.setMiiPortrait(false);m.drawer.reset();for(auto& t:m.battleFont)t.reset();m.battleLoaded=0;m.battle=false;m.battlePanel.reset();
}
bool RaceFlowUi::resultsLoaded()const{return impl_->loaded==impl_->results.size()&&(!impl_->battle||impl_->battleLoaded==impl_->battleFont.size());}
bool RaceFlowUi::resultsFailed()const{return impl_->failed;}
unsigned RaceFlowUi::resultsBytes()const{unsigned n=0;for(const auto& t:impl_->battleFont)n+=t.bytes();for(const auto& t:impl_->results)n+=t.bytes();return n;}

void RaceFlowUi::drawWipe(Renderer& renderer,float progress){
 auto& m=*impl_;
 if(progress<0||!m.wipe.ready)return;
 // AnimationGroup::PlayAnimationAtPercent: the frame is percent x the last frame.
 const float frame=std::clamp(progress,0.f,1.f)*float(FlowWipeTranslateT.size()-1);
 const unsigned i=std::min(unsigned(frame),unsigned(FlowWipeTranslateT.size()-2));
 const float t=FlowWipeTranslateT[i]+(FlowWipeTranslateT[i+1]-FlowWipeTranslateT[i])*(frame-float(i));
 // NW4R texture SRT about the centre: v' = scale (v - 0.5) + translate + 0.5.
 const float v0=(FlowWipeScaleT*-.5f+t+.5f)*FlowWipeTextureV,v1=(FlowWipeScaleT*.5f+t+.5f)*FlowWipeTextureV;
 // The pane spans the whole 832x456 screen; colour register 0 is black.
 renderer.drawScreenSpriteRegion(m.wipe,0,0,400,240,0,v0,1,v1,1.f,0x000000ffu);
}

void RaceFlowUi::step(const RaceResultsFlow& flow){auto& m=*impl_;m.drawer.step(flow);m.battlePanel.step(flow);}

void RaceFlowUi::drawResults(Renderer& renderer,const RaceResultsFlow& flow,const Texture* miiPortrait){
 auto& m=*impl_;
 if(!resultsLoaded())return;
 m.renderer=&renderer;
 m.miiPortrait=miiPortrait&&miiPortrait->ready?miiPortrait:nullptr;
 m.drawer.setMiiPortrait(m.miiPortrait!=nullptr);
 m.drawer.draw(flow,&Impl::submit,&m);
 if(flow.battle()&&m.battleLoaded==m.battleFont.size())m.battlePanel.draw(flow,m.drawer,&Impl::submitBattle,&m);
}
}
