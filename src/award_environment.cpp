#include <mkw/award_environment_draw.hpp>
#include <mkw/award_effect_anchors.hpp>
#include <mkw/course_object_kinds.hpp>
#include <cstring>
#include <cmath>

namespace mkw {
namespace {
bool controlled(const AwardEnvironmentObject& object){return object.route>=0||object.id==731||object.id==537;}
}
void AwardEnvironmentJob::run(){
 if(cancelled())return;
 auto load=[&](unsigned index){
  auto model=std::make_unique<AwardModel>();failedAsset_=AwardModelAssets[index].list;
  if(!model->load(AwardModelAssets[index]))model.reset();
  return model;
 };
 auto course=load(def_.course);if(!course||cancelled())return;
 auto sky=load(def_.sky);if(!sky||cancelled())return;
 std::vector<Entry> entries;std::vector<int> slots(def_.count,-1);
 std::vector<C3D_Mtx> transforms(def_.count);
 for(unsigned i=0;i<def_.count;++i){
  if(cancelled())return;
  const auto& object=def_.objects[i];
  if(object.model<0||!(object.presence&1))continue;
  int slot=-1;
  // Independent scene clocks must not skin the same vertex buffer twice in
  // one queued frame. Only synchronized decorative instances may share it.
  if(!controlled(object))for(unsigned j=0;j<entries.size();++j)
   if(!entries[j].controlled&&entries[j].asset==unsigned(object.model)){slot=int(j);break;}
  if(slot<0){
   auto model=load(object.model);if(!model)return;
   if(object.clip){
    failedAsset_=AwardModelAssets[object.model].animation;
    if(!model->play(object.clip))return;
    model->update(0,0);
   }
   slot=int(entries.size());entries.push_back({unsigned(object.model),object.clip,std::move(model),controlled(object)});
  }
  slots[i]=slot;
  Kinoko::EGG::Matrix34f matrix;
  const auto& r=object.rotation;const auto& p=object.position;const auto& s=object.scale;
  matrix.makeRT({r.x*0.01745329252f,r.y*0.01745329252f,r.z*0.01745329252f},{p.x,p.y,p.z});
  transforms[i]=objectMatrix(matrix,{s.x,s.y,s.z});
 }
 std::vector<Mover> movers;
 for(unsigned i=0;i<def_.count;++i){
  const auto& object=def_.objects[i];
  if(object.id!=218||slots[i]<0)continue;
  failedAsset_="castleballoon1 route";
  if(object.route<0||unsigned(object.route)>=def_.routeCount)return;
  const auto* raw=reinterpret_cast<const Kinoko::System::MapdataPointInfo::SData*>(def_.routes[object.route].data);
  Kinoko::System::MapdataPointInfo info(raw);
  if(info.pointCount()<2||info.setting(0)!=1)return;
  Mover mover; mover.object=i;
  mover.rail=std::make_unique<Kinoko::Field::RailSpline>(object.route,&info);
  mover.interpolator=std::make_unique<Kinoko::Field::RailSmoothInterpolator>(float(object.settings[0]),*mover.rail);
  mover.interpolator->init(0.f,0);
  movers.push_back(std::move(mover));
 }
 if(cancelled())return;
 std::unique_ptr<RaceEffects> effects;
 std::vector<Texture> textures;
 int tail=-1;
 for(unsigned i=0;i<def_.count;++i)if(def_.objects[i].id==731&&slots[i]>=0){
  effects=std::make_unique<RaceEffects>();
  failedAsset_="textures/award-effects.bin";
  if(!effects->load(failedAsset_.c_str()))return;
  tail=effects->find("rk_JugemTail");if(tail<0)return;
  confettiSpec_=effects->find("rk_kamifubuki");
  confettiLastSpec_=effects->find("rk_kamifubukiLast");
  fireworksSpec_=effects->find("rk_demoHanabi");
  if(confettiSpec_<0||confettiLastSpec_<0||fireworksSpec_<0)return;
  textures.resize(effects->textures.size());
  for(unsigned t=0;t<textures.size();++t){
   failedAsset_=std::string("textures/")+effects->textures[t].name+".t3x";
   if(!textures[t].load(failedAsset_.c_str())||cancelled())return;
  }
  break;
 }
 effects_=std::move(effects);effectTextures_=std::move(textures);tailSpec_=tail;
 movers_=std::move(movers);
 course_=std::move(course);sky_=std::move(sky);entries_=std::move(entries);
 slots_=std::move(slots);transforms_=std::move(transforms);failedAsset_.clear();loaded=true;
}
void AwardEnvironmentJob::updateDecorations(float frame,unsigned tick){
 if(!loaded)return;
 for(auto& entry:entries_)if(entry.clip&&!entry.controlled)entry.model->update(frame,tick);
 for(auto& mover:movers_){
  if(!mover.started||tick<mover.tick){mover.started=true;mover.tick=tick;}
  for(unsigned n=tick-mover.tick;n;--n){mover.interpolator->calc();++mover.frames;}
  mover.tick=tick;
  const auto& object=def_.objects[mover.object];
  auto p=mover.interpolator->curPos();
  p.y+=float(int16_t(object.settings[1]))*std::sin(6.28318530718f*float(mover.frames?mover.frames-1:0)/500.f);
  auto& transform=transforms_[mover.object];
  transform.r[0].w=p.x;transform.r[1].w=p.y;transform.r[2].w=p.z;
 }
}
void AwardEnvironmentJob::prepareVram(Renderer& renderer){
 if(!loaded)return;
 course_->prepareVram(renderer);sky_->prepareVram(renderer);
 for(auto& entry:entries_)entry.model->prepareVram(renderer);
}
void AwardEnvironmentJob::drawBackground(Renderer& renderer)const{
 if(!loaded)return;
 C3D_Mtx identity;Mtx_Identity(&identity);
 sky_->draw(renderer,identity,true);course_->draw(renderer,identity);
}
void AwardEnvironmentJob::drawDecorations(Renderer& renderer)const{
 if(!loaded)return;
 for(unsigned i=0;i<def_.count;++i)
  if(slots_[i]>=0&&(!controlled(def_.objects[i])||def_.objects[i].id==218))entries_[slots_[i]].model->draw(renderer,transforms_[i]);
}
void AwardEnvironmentJob::drawObject(Renderer& renderer,unsigned index,const C3D_Mtx& transform,float frame,unsigned tick){
 if(!loaded||index>=slots_.size()||slots_[index]<0)return;
 auto& model=*entries_[slots_[index]].model;
 model.update(frame,tick);model.draw(renderer,transform);
}
bool AwardEnvironmentJob::drawIntroPerformers(Renderer& renderer,float frame,unsigned tick){
 if(!loaded||frame<0)return false;
 bool visible=false;
 for(unsigned i=0;i<def_.count;++i){
  if(def_.objects[i].id!=731||slots_[i]<0)continue;
  auto& model=*entries_[slots_[i]].model;
  // The source non-looping o_jg-demowin ends after its 401-frame flight.
  // Do not repeat the entrance or freeze the actor when the result page opens.
  if(frame>=model.duration())continue;
  drawObject(renderer,i,transforms_[i],frame,tick);visible=true;
 }
 return visible;
}
bool AwardEnvironmentJob::stepIntroEffects(float frame,bool active,unsigned sceneTick){
 if(!effects_)return false;
 bool fireworks=false;
 // Camera manager +0x28 selects the original intro/result confetti bank
 // (USA 806e25d4..806e26b0). Intro is created at camera activation;
 // the result emitter is kept alive after the camera changes, including skip.
 for(unsigned i=0;i<def_.count;++i){
  const auto& object=def_.objects[i];
  if(!(object.presence&1))continue;
  EffectFrame placement;placement.origin=object.position;
  const unsigned slot=1+3*i;
  // DemoEf waits for RaceManager::m_time >= 1320 (806e2468..24f4),
  // independent of skipping the first camera. Its effect belongs to group 14.
  if(object.id==728&&sceneTick>=1320){
   effects_->create(slot+2,fireworksSpec_,placement,14);
   effects_->move(slot+2,placement);fireworks=true;
  }
  if(object.id!=741)continue;
  if(active){
   if(!confettiStarted_)effects_->create(slot,confettiSpec_,placement);
   effects_->move(slot,placement);
  }else{
   effects_->fade(slot);
   effects_->create(slot+1,confettiLastSpec_,placement);
   effects_->move(slot+1,placement);
  }
 }
 confettiStarted_=true;
 active=active&&frame>=0&&frame<float(std::size(AwardJugemuRoots));
 if(active){
  // USA 807758f0..80775948 copies only the root world translation.
  const auto p=AwardJugemuRoots[unsigned(frame)];
  for(unsigned i=0;i<def_.count;++i)if(def_.objects[i].id==731&&slots_[i]>=0){
   const auto& m=transforms_[i];EffectFrame placement;
   placement.origin={m.r[0].x*p.x+m.r[0].y*p.y+m.r[0].z*p.z+m.r[0].w,
                     m.r[1].x*p.x+m.r[1].y*p.y+m.r[1].z*p.z+m.r[1].w,
                     m.r[2].x*p.x+m.r[2].y*p.y+m.r[2].z*p.z+m.r[2].w};
   if(!tailActive_)effects_->create(0,tailSpec_,placement);
   else effects_->move(0,placement);
   break;
  }
 }else if(tailActive_)effects_->fade(0);
 tailActive_=active;
 effects_->step();
 return fireworks;
}
void AwardEnvironmentJob::drawEffects(Renderer& renderer,const float* camera){
 if(!effects_)return;
 float view[3][4];std::memcpy(view,camera,sizeof(view));effects_->setCamera(view);
 renderer.drawEffects(*effects_,effectTextures_.data());
}
}
