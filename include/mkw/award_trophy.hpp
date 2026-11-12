#pragma once
#include <mkw/award_model.hpp>
#include <mkw/award_trophy_motion.hpp>
#include <mkw/race_effects.hpp>
#include <cstring>

namespace mkw {
inline const AwardModelAsset* awardTrophyAsset(unsigned cup){
 // Original trophy loader switch, USA 8076d2f8..8076d4e8.
 constexpr const char* names[]={"kinoko_trophy","flower_trophy","star_trophy","special_trophy",
  "koura_trophy","banana_trophy","konoha_trophy","thunder_trophy","battle_trophy"};
 if(cup>=std::size(names))return nullptr;
 for(const auto& asset:AwardModelAssets)
  if(!std::strncmp(asset.archive,"Demo/Trophy/",12)&&!std::strcmp(asset.model,names[cup]))return &asset;
 return nullptr;
}
class AwardTrophyJob final:public AssetJob {
public:
 AwardTrophyJob(unsigned cup,unsigned rank):cup_(cup),rank_(rank){label="award trophy";}
 void run()override{
  if(cancelled()||rank_<1||rank_>3)return;
  const auto* asset=awardTrophyAsset(cup_);if(!asset)return;
  auto trophy=std::make_unique<AwardModel>();
  if(!trophy->load(*asset,true)||!trophy->play(asset->model))return;
  // Original 8076d7f8..8076d830 freezes CLR0 on these medal frames.
  constexpr float medalFrames[]={0,5,9};
  if(!trophy->setColorFrame(asset->model,medalFrames[rank_-1]))return;
  trophy->update(0,0);
  auto effects=std::make_unique<RaceEffects>();
  if(!effects->load("textures/award-trophy-effects.bin"))return;
  constexpr const char* names[]={"rk_trophyGold","rk_trophySilver","rk_trophyCopper"};
  const int spec=effects->find(names[rank_-1]);if(spec<0)return;
  std::vector<Texture> textures(effects->textures.size());
  for(unsigned i=0;i<textures.size();++i){
   const auto path=std::string("textures/")+effects->textures[i].name+".t3x";
   if(cancelled()||!textures[i].load(path.c_str()))return;
  }
  if(!effectBuffer_.init())return;
  effects->setCamera({0,0,1000},{0,0,0},{0,1,0});
  effects_=std::move(effects);textures_=std::move(textures);effectSpec_=spec;
  if(!cancelled())model=std::move(trophy);
 }
 std::unique_ptr<AwardModel> model;
 // Called only after the completed asset job has been published to the scene.
 unsigned show(){return model?motion.show():0;}
 void leave(){motion.leave();}
 void step(unsigned /*tick*/,bool paused=false){
  motion.step(paused);
  // AwardCupModel::Update 8076db14..68 keeps the medal emitter alive,
  // with translation and scale only; it does not spin the effect with the cup.
  if(effects_&&motion.visible()&&!paused){
   EffectFrame frame;frame.origin=motion.position();frame.scale=3;
   effects_->create(0,effectSpec_,frame);effects_->move(0,frame);effects_->step();
  }
  // The scene updates mutable model palettes after FrameBegin retires the
  // preceding GPU frame; simulation ticks must not write them here.
 }
 void draw(Renderer& renderer)const{
  if(!model||!motion.visible())return;
  const auto p=motion.position();C3D_Mtx transform;
  Mtx_Identity(&transform);Mtx_Translate(&transform,p.x,p.y,p.z,true);
  Mtx_RotateY(&transform,motion.yaw(),true);
  // AwardCupModel ctor sets scale +0x2c to (3,3,3), constant 8089f6c0;
  // Prepare passes it to the model's scale setter (8076d7c8..8076d7dc).
  Mtx_Scale(&transform,3,3,3);model->draw(renderer,transform,false,true);
  if(effects_)renderer.drawMenuEffects(*effects_,textures_.data(),effectBuffer_);
 }
 AwardTrophyMotion motion;
private:
 unsigned cup_,rank_;
 std::unique_ptr<RaceEffects> effects_;
 std::vector<Texture> textures_;
 mutable EffectLayerBuffer effectBuffer_;
 int effectSpec_=-1;
};
}
