#include <mkw/ceremony_vehicle.hpp>
#include <mkw/mii_garage.hpp>
#include <cstring>

namespace mkw {
bool CeremonyVehicle::load(unsigned character,unsigned vehicle,RaceTeam team,std::string* failure){
 if(!assets_.load(character,vehicle,team,failure))return false;
 vehicle_.reset();posed_=false;return true;
}
bool CeremonyVehicle::prepareMii(const NativeMii& identity){
 if(!assets_.choice||assets_.choice->character<24)return true;
 if(head_)return true;
 MiiAppearance appearance;
 if(!decodeMiiAppearance(identity,appearance)||!miiBodySpec(appearance,(assets_.choice->character-24)%6/2,mii_)||mii_.retailCharacter!=assets_.choice->character)return false;
 auto head=std::make_unique<NativeMiiHead>();if(!head->load(identity))return false;
 const auto rgba=mii_.clothingRGBA,clothing=(rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba&255)<<24);
 const auto name=std::string(assets_.choice->prefix)+"driver";
 for(const auto& role:MiiRaceMaterials)if(name==role.model){
  auto sub=std::find_if(assets_.driver->submeshes.begin(),assets_.driver->submeshes.end(),[&](const auto& s){return s.first==role.first&&s.count==role.count;});
  if(sub==assets_.driver->submeshes.end())return false;
  sub->constant=role.skin?head->skinConstant():clothing;
 }
 head_=std::move(head);return true;
}
void CeremonyVehicle::freezeLighting(){
 auto freeze=[](auto& model,auto& animation){if(!model)return;
  model->freezeLighting(animation&&animation->gpuSkinned()?animation->palette():nullptr);
  if(animation&&animation->gpuSkinned()){animation->shareDrawVertices(*model);model->enableSkinCulling(animation->matrixCount());}
  else if(!animation)model->enableStaticCulling();
 };
 freeze(assets_.driver,assets_.animation);freeze(assets_.body,assets_.bodyAnimation);
 for(unsigned i=0;i<assets_.extras.size();++i)freeze(assets_.extras[i],assets_.extraAnimation[i]);
 for(auto& wheel:assets_.wheels)if(wheel){wheel->freezeLighting();wheel->enableStaticCulling();}
}
bool CeremonyVehicle::pose(const VehicleAnimation::Snapshot& source,const DriverPose& driver,unsigned tick){
 if(!assets_.choice||source.vehicle!=assets_.choice->vehicle||!driver.clipName)return false;
 const int clip=assets_.animation->find(driver.clipName);
 if(clip<0||!vehicle_.setSnapshot(source))return false;
 seated_=vehicle_.body();Mtx_Translate(&seated_,0,assets_.choice->seatY,assets_.choice->seatZ,true);
 assets_.animation->setBraidWorld(seated_);
 int expression=assets_.animation->face(driver.clipName);float expressionFrame=driver.frame;
 if(expression<0&&driver.faceName){expression=assets_.animation->face(driver.faceName);expressionFrame=driver.faceFrame;}
 assets_.animation->apply(*assets_.driver,clip,driver.frame,tick,expression,expressionFrame);
 auto animate=[&](DriverAnimation* bank,Model* model,bool handle){
  if(!bank||!model)return;
  const int c=handle&&bank->handleDriveClip()>=0?bank->handleDriveClip():
   !std::strcmp(driver.clipName,"dash")&&bank->dashClip()>=0?bank->dashClip():bank->shakeClip();
  if(c>=0)bank->apply(*model,c,handle?vehicle_.driveFrame()*(bank->duration(c)-1)/30.f:float(tick),tick);
 };
 animate(assets_.bodyAnimation.get(),assets_.body.get(),false);
 for(unsigned i=0;i<assets_.extras.size();++i)animate(assets_.extraAnimation[i].get(),assets_.extras[i].get(),i==0);
 expression_=NativeMiiHead::expression(driver);posed_=true;return true;
}
bool CeremonyVehicle::faceHeight(float& height)const{
 if(!posed_||!assets_.animation)return false;
 C3D_Mtx face,world;if(!assets_.animation->headTransform(face))return false;
 Mtx_Multiply(&world,&seated_,&face);
 const float candidate=world.r[1].w-vehicle_.position().y;
 if(!std::isfinite(candidate))return false;
 height=candidate;return true;
}
float CeremonyVehicle::clipDuration(const char* name)const{
 if(!assets_.animation)return 0;
 const int clip=assets_.animation->find(name);
 return clip<0?0:assets_.animation->duration(clip);
}
Renderer::ShadowVolume CeremonyVehicle::shadow()const{
 return {posed_?assets_.shadow.get():nullptr,vehicle_.body()};
}
void CeremonyVehicle::draw(Renderer& renderer)const{
 if(!posed_)return;
 auto draw=[&](const Model& model,const C3D_Mtx& transform,const DriverAnimation* bank=nullptr){
  if(bank&&bank->gpuSkinned())renderer.setSkinPalette(bank->palette(),bank->matrixCount(),21);
  else renderer.setSkinPalette(nullptr,0,12);
  renderer.draw(model,transform);
 };
 draw(*assets_.body,vehicle_.body(),assets_.bodyAnimation.get());
 draw(*assets_.driver,seated_,assets_.animation.get());
 if(head_){C3D_Mtx joint,local,attached,world;if(assets_.animation->headTransform(joint)){
  Mtx_Identity(&local);Mtx_Translate(&local,mii_.headX,mii_.headY,0,true);Mtx_RotateZ(&local,3.141592741f,true);Mtx_RotateY(&local,-1.570796371f,true);Mtx_Scale(&local,mii_.headScale,mii_.headScale,mii_.headScale);
  Mtx_Multiply(&attached,&joint,&local);Mtx_Multiply(&world,&seated_,&attached);renderer.setSkinPalette(nullptr,0,12);head_->draw(renderer,world,expression_);
 }}
 for(unsigned i=0;i<vehicle_.wheelCount();++i)draw(*assets_.wheels[vehicle_.modelIndex(i)],vehicle_.wheel(i));
 for(unsigned i=0;i<assets_.extras.size();++i)if(assets_.extras[i])
  for(unsigned n=0;n<vehicle_.accessoryCount(GarageExtraNames[i]);++n)
   draw(*assets_.extras[i],vehicle_.accessory(GarageExtraNames[i],assets_.extraAnimation[i]&&assets_.extraAnimation[i]->handleDriveClip()>=0,n),assets_.extraAnimation[i].get());
 renderer.setSkinPalette(nullptr,0,12);
}
}
