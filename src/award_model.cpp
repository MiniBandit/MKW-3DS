#include <mkw/award_model.hpp>
#include <mkw/retail_lighting.hpp>
#include <mkw/mii_award_materials.hpp>
#include <mkw/staff_crowd_data.hpp>
#include <mkw/joint_billboard.hpp>
#include <mkw/normal_matrix.hpp>
#include <mkw/award_trophy.hpp>
#include <mkw/asset_pack.hpp>
#include <mkw/render_quality.hpp>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace mkw {
const AwardModelAsset* AwardModel::find(const char* archive,const char* model){
 if(!archive||!model)return nullptr;
 for(const auto& asset:AwardModelAssets)
  if(!std::strcmp(archive,asset.archive)&&!std::strcmp(model,asset.model))return &asset;
 return nullptr;
}
bool AwardModel::load(const AwardModelAsset& asset,bool menuLighting){
 std::vector<uint8_t> bytes;
 if(!pack::read(asset.list,bytes)){
  FILE* file=std::fopen(asset.list,"rb");if(!file)return false;
  char buffer[1024];size_t count;
  while((count=std::fread(buffer,1,sizeof buffer,file)))bytes.insert(bytes.end(),buffer,buffer+count);
  const bool failed=std::ferror(file);std::fclose(file);if(failed)return false;
 }
 std::istringstream list(std::string(bytes.begin(),bytes.end()));
 std::vector<std::unique_ptr<Model>> parts;
 std::string name;
 while(list>>name){
  if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)return false;
  auto model=std::make_unique<Model>();
  if(!model->load(("models/"+name+".gmdl").c_str(),menuLighting))return false;
  // Trophy meshes already carry the original GX light set and metal layers.
  // This independent UI camera must enable them even with world lighting off.
  if(menuLighting)for(auto& sub:model->submeshes)sub.lighting|=retail::Menu;
  parts.push_back(std::move(model));
 }
 if(parts.empty())return false;
 std::unique_ptr<DriverAnimation> animation;
 std::unique_ptr<ObjectBank> materials;
 if(asset.materials){
  if(parts.size()!=1)return false;
  materials=std::make_unique<ObjectBank>();
  if(!materials->load(asset.materials))return false;
  materials->markRegisterAlpha(*parts.front());
 }
 const bool staffCrowd=std::strstr(asset.archive,"unpacked/ending_demo/MiiObj")!=nullptr;
 if(asset.animation){
  if(parts.size()!=1)return false;
  animation=std::make_unique<DriverAnimation>();
  if(!animation->load(asset.animation,*parts.front())||!animation->count())return false;
  // A bank over the hardware palette limit retains the CPU skinning path.
  // Trophy banks have one rigid matrix but roughly twenty thousand source
  // vertices. Use the existing rigid draw path instead of skinning each one.
  if((animation->matrixCount()!=1&&!staffCrowd)||!animation->makeRigid(*parts.front())){
   animation->prepareLitSkinning(*parts.front());
   if(animation->gpuSkinned())parts.front()->enableSkinCulling(animation->matrixCount());
  }
  // Keep the podium cast and background spectators shaded, with a fixed
  // reference-pose light rather than animated per-vertex normal transforms.
  if(std::strstr(asset.archive,"/group_"))
   parts.front()->freezeLighting(animation->gpuSkinned()?animation->palette():nullptr);
 }else for(auto& model:parts)model->enableStaticCulling();
 if(staffCrowd&&(!animation||!animation->rigid()))return false;
 reset();if(std::strstr(asset.archive,"unpacked/Award/")&&std::strstr(asset.archive,"_mii_")){const char* file=std::strrchr(asset.archive,'/')+1;miiStem_.assign(file,8);}
 parts_=std::move(parts);animation_=std::move(animation);materials_=std::move(materials);
 if(staffCrowd){
  for(const auto& joint:StaffCrowdJoints)if(!std::strcmp(joint.model,asset.model)){
   if(joint.slot>=animation_->matrixCount())return false;
   billboards_.push_back({joint.slot,joint.mode});
  }
  if(billboards_.empty())return false;
  billboardPalette_.resize(animation_->matrixCount()*21);
 }
 // Scene choreography selects a named clip before the first animated draw;
 // do not choose an arbitrary clip merely because it was exported first.
 return true;
}
bool AwardModel::prepareMii(const NativeMii& identity){
 if(miiStem_.empty()||head_)return true;
 MiiAppearance appearance;if(!decodeMiiAppearance(identity,appearance)||!miiBodySpec(appearance,miiStem_[1]=='b',mii_)||miiStem_!=mii_.stem)return false;
 auto head=std::make_unique<NativeMiiHead>();if(!head->load(identity))return false;
 const auto rgba=mii_.clothingRGBA,clothing=(rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba&255)<<24);
 for(const auto& role:MiiAwardMaterials)if(miiStem_==role.stem){
  auto& model=*parts_.front();auto sub=std::find_if(model.submeshes.begin(),model.submeshes.end(),[&](const auto& s){return s.first==role.first&&s.count==role.count;});
  if(sub==model.submeshes.end())return false;sub->constant=role.skin?head->skinConstant():clothing;
 }
 head_=std::move(head);return true;
}
void AwardModel::freezeLighting(){
 for(auto& part:parts_)part->freezeLighting(animation_&&animation_->gpuSkinned()?animation_->palette():nullptr);
}
void AwardModel::reset(){head_.reset();miiStem_.clear();billboards_.clear();billboardPalette_.clear();overrides_.clear();materials_.reset();animation_.reset();parts_.clear();clip_=face_=-1;}
bool AwardModel::setColorFrame(const char* name,float frame){
 if(!materials_)return false;
 const int clip=materials_->color(name);if(clip<0)return false;
 overrides_.resize(parts_.front()->submeshes.size());
 return materials_->material(-1,0,-1,0,clip,frame,overrides_.data(),overrides_.size());
}
bool AwardModel::play(const char* name){
 if(!animation_)return false;
 const int clip=animation_->find(name);if(clip<0)return false;
 animation_->clearPoseTransition();clip_=clip;face_=animation_->face(name);
 return true;
}
void AwardModel::update(float frame,unsigned tick){
 if(animation_&&clip_>=0){
  if(animation_->rigid())animation_->pose(clip_,frame,tick);
  else animation_->apply(*parts_.front(),clip_,frame,tick,face_,frame);
 }
}
void AwardModel::prepareVram(Renderer& renderer){
 for(auto& part:parts_)for(auto& texture:part->textures)
  if(texture)renderer.moveToVram(*texture,RenderQuality::vramReserve);
}
void AwardModel::draw(Renderer& renderer,const C3D_Mtx& transform,bool backdrop,bool menuLayer)const{
 if(animation_&&clip_<0)return;
 RigidPose rigid;
 if(animation_&&animation_->rigid()){
  rigid.parts=&animation_->rigidParts();rigid.palette=animation_->palette();
  if(!billboards_.empty()){
   std::copy_n(rigid.palette,billboardPalette_.size(),billboardPalette_.data());
   C3D_Mtx mv;const auto view=renderer.viewFrustum();Mtx_Multiply(&mv,&view.view,&transform);
   float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=mv.r[r].c[3-c];
   for(const auto& node:billboards_){
    float* joint=billboardPalette_.data()+node.slot*21;
    if(billboardMiiJoint(joint,rows,node.mode)){
     Vec3 axes[3]={{joint[0],joint[1],joint[2]},{joint[4],joint[5],joint[6]},{joint[8],joint[9],joint[10]}},normals[3];
     normalMatrix(axes,normals);
     for(unsigned r=0;r<3;++r){joint[12+r*3]=normals[r].x;joint[13+r*3]=normals[r].y;joint[14+r*3]=normals[r].z;}
    }
   }
   rigid.palette=billboardPalette_.data();
  }
  renderer.setRigidPose(&rigid);
 }else renderer.setRigidPose(nullptr);
 if(animation_&&animation_->gpuSkinned())
  renderer.setSkinPalette(animation_->palette(),animation_->matrixCount(),21);
 else renderer.setSkinPalette(nullptr,0,21);
 renderer.setOverrides(overrides_.empty()?nullptr:overrides_.data(),overrides_.size());
 for(const auto& model:parts_){
  if(!backdrop&&!menuLayer&&!animation_&&!renderer.viewFrustum().visible(*model,transform))continue;
  if(menuLayer)renderer.drawMenuModelLayer(*model,transform);
  else renderer.draw(*model,transform,backdrop);
 }
 renderer.setOverrides(nullptr,0);
 renderer.setSkinPalette(nullptr,0,21);
 renderer.setRigidPose(nullptr);
 if(head_&&animation_){C3D_Mtx joint,local,attached,world;if(animation_->headTransform(joint)){
  Mtx_Identity(&local);Mtx_Translate(&local,mii_.headX,mii_.headY,0,true);Mtx_RotateZ(&local,3.141592741f,true);Mtx_RotateY(&local,-1.570796371f,true);Mtx_Scale(&local,mii_.headScale,mii_.headScale,mii_.headScale);
  Mtx_Multiply(&attached,&joint,&local);Mtx_Multiply(&world,&transform,&attached);head_->draw(renderer,world,NativeMiiHead::Smile);
 }}
}
void AwardCastJob::run(){
 if(cancelled()||!cast_.valid)return;
 auto stage=std::make_unique<AwardModel>();
 failedAsset_=cast_.stand->list;
 if(!stage->load(*cast_.stand))return;
 std::vector<std::unique_ptr<AwardModel>> winners,crowd;
 auto loadActors=[&](const auto& source,unsigned count,auto& target){
  for(unsigned i=0;i<count;++i){
   if(cancelled())return false;
   const auto& performer=source[i];failedAsset_=performer.asset->list;
   auto actor=std::make_unique<AwardModel>();
   if(!actor->load(*performer.asset))return false;
   failedAsset_=performer.asset->animation;
   if(!actor->play(performer.clip))return false;
   actor->update(0,0);if(!performer.player.human)actor->freezeLighting();target.push_back(std::move(actor));
  }
  return true;
 };
 if(!loadActors(cast_.podium,cast_.podiumCount,winners)||
    !loadActors(cast_.audience,cast_.audienceCount,crowd)||cancelled())return;
 stand=std::move(stage);podium=std::move(winners);audience=std::move(crowd);
 failedAsset_.clear();loaded=true;
}
void AwardCastJob::updateActors(float frame,unsigned tick){
 if(!loaded)return;
 for(auto& actor:podium)actor->update(frame,tick);
 for(auto& actor:audience)actor->update(frame,tick);
}
void AwardCastJob::drawStand(Renderer& renderer,const AwardStandPlacement& placement)const{
 if(!loaded||!stand)return;
 C3D_Mtx transform;Mtx_Identity(&transform);
 const auto& p=placement.position;
 Mtx_Translate(&transform,p.x,p.y,p.z,true);
 Mtx_Scale(&transform,placement.scale,placement.scale,placement.scale);
 stand->draw(renderer,transform);
}
void AwardCastJob::drawActors(Renderer& renderer,bool teams,Vec3 center)const{
 if(!loaded)return;
 const auto positions=placeAwardCast(cast_,teams,center);
 auto draw=[&](const auto& actors,const auto& placements){
  for(unsigned i=0;i<actors.size();++i){
   const auto& p=placements[i];C3D_Mtx transform;
   Mtx_Identity(&transform);Mtx_Translate(&transform,p.position.x,p.position.y,p.position.z,true);
   Mtx_RotateY(&transform,p.yaw,true);actors[i]->draw(renderer,transform);
  }
 };
 draw(podium,positions.podium);draw(audience,positions.audience);
}
}
