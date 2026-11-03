#include <mkw/menu_vehicle_icons.hpp>
#include <mkw/mii_garage.hpp>
#include <mkw/menu_lighting.hpp>
#include <mkw/menu_icon_params.hpp>
#include <mkw/renderer.hpp>
#include <mkw/render_metrics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace mkw {
namespace {
constexpr float Radians=3.14159265358979323846f/180.f;
constexpr unsigned VehicleOrder[]={0,1,2,6,7,8,3,4,5,9,10,11};
void environment(Model& model,float yaw){
 if(model.staticLightingBaked)return;
 const float c=std::cos(yaw),s=std::sin(yaw);
 std::vector<bool> seen(model.vertexCount,false);bool modified=false;
 for(const auto& range:model.submeshes){
  if(!(range.flags&(1u<<25))||((range.flags>>12)&3)!=3)continue;
  for(unsigned i=range.first;i<range.first+range.count;++i){
   const auto index=model.indices[i];if(seen[index])continue;seen[index]=true;
   auto& vertex=model.vertices[index];
   const auto normal=normalized(vertex.normal);
   vertex.uv1[0]=.5f+.5f*(c*normal.x+s*normal.z);
   vertex.uv1[1]=.5f+.5f*normal.y;
  }
  modified=true;
 }
 if(modified)flushGpuData(model.vertices,model.vertexCount*sizeof(Vertex));
}
struct IconJob final:AssetJob {
 int driver;std::array<std::unique_ptr<Model>,12> models;std::array<C3D_Mtx,12> transforms{};bool complete=true;
 explicit IconJob(int d):driver(d){label="vehicle icons (12 models)";}
 void run()override{
  Model::setLightingCourse(&MenuLightings[0]);
  const unsigned weight=driver<24?UiCharacters[driver].weight:raceGarageChoice(288+miiGarageOffset(unsigned(driver-24))).vehicle%3;
  for(unsigned choice=0;choice<models.size();++choice){
   if(cancelled())return;
   const auto* pose=menuIconParam(192,44,weight,VehicleOrder[choice]*3+weight);
   if(!pose){complete=false;continue;}
   char path[96];if(driver<24)std::snprintf(path,sizeof path,"models/ui_machine_%d_%u_000.gmdl",driver,choice);
   else std::snprintf(path,sizeof path,"models/%smenu_000.gmdl",raceGarageChoice(288+miiGarageOffset(unsigned(driver-24))+choice).prefix);
   auto model=std::make_unique<Model>();
   if(!model->load(path)){complete=false;continue;}
   auto& matrix=transforms[choice];Mtx_Identity(&matrix);
   Mtx_Translate(&matrix,pose->x,pose->y,pose->z,true);
   Mtx_RotateZ(&matrix,pose->rz*Radians,true);
   Mtx_RotateY(&matrix,pose->ry*Radians,true);
   Mtx_RotateX(&matrix,pose->rx*Radians,true);
   Mtx_Scale(&matrix,pose->scale,pose->scale,pose->scale);
   environment(*model,pose->ry*Radians);
   models[choice]=std::move(model);
  }
 }
};
}
struct MenuVehicleIcons::Impl {
 struct Set {int driver=-1;std::shared_ptr<IconJob> job;std::array<std::unique_ptr<Model>,12> models;std::array<C3D_Mtx,12> transforms{};bool resident=false,complete=true;std::uint32_t used=0;};
 std::vector<Set> sets;int shown=-1;std::uint32_t clock=1;
 Set* find(int driver){for(auto& s:sets)if(s.driver==driver)return &s;return nullptr;}
 const Set* find(int driver)const{for(const auto& s:sets)if(s.driver==driver)return &s;return nullptr;}
};
MenuVehicleIcons::MenuVehicleIcons():impl_(std::make_unique<Impl>()){}
MenuVehicleIcons::~MenuVehicleIcons()=default;
unsigned MenuVehicleIcons::residentCount()const{
 unsigned count=0;for(const auto& s:impl_->sets)for(const auto& model:s.models)count+=bool(model);return count;
}
void MenuVehicleIcons::request(int driver,AssetPriority priority){
 if(driver<0||driver>=66||(driver>=24&&driver<48))return;
 auto& s=*impl_;auto* set=s.find(driver);
 if(!set){s.sets.push_back({});set=&s.sets.back();set->driver=driver;}
 set->used=s.clock;
 if(set->resident)return;
 if(!set->job||(set->job->done()&&set->job->cancelled()))set->job=std::make_shared<IconJob>(driver);
 if(!set->job->done())AssetLoader::instance().submit(set->job,priority);
}
bool MenuVehicleIcons::ready(int driver)const{
 if(driver<0)return true;
 const auto* set=impl_->find(driver);return set&&set->resident;
}
void MenuVehicleIcons::update(Renderer& renderer,const MenuState& state){
 auto& s=*impl_;
 for(auto& set:s.sets)if(set.job&&set.job->done()){
  auto job=std::move(set.job);
  if(job->cancelled())continue;
  set.models=std::move(job->models);set.transforms=job->transforms;set.complete=job->complete;set.resident=true;
 }
 const int driver=driverFor(state);
 s.shown=driver;
 if(auto* set=s.find(driver))set->used=s.clock;
 // Keep the shown set plus the most recently requested other set.
 for(;;){
  unsigned count=0;for(const auto& set:s.sets)count+=set.resident||set.job;
  if(count<=2)break;
  Impl::Set* victim=nullptr;
  for(auto& set:s.sets)if(set.driver!=s.shown&&(set.resident||set.job)&&(!victim||set.used<victim->used))victim=&set;
  if(!victim)break;
  if(victim->job){victim->job->cancel();victim->job.reset();}
  for(auto& model:victim->models)renderer.retire(std::move(model));
  victim->resident=false;
 }
 s.sets.erase(std::remove_if(s.sets.begin(),s.sets.end(),[&](const auto& set){return !set.resident&&!set.job&&set.driver!=s.shown;}),s.sets.end());
 ++s.clock;
}
bool MenuVehicleIcons::prepare(Renderer& renderer,const MenuState& state){
 const int driver=driverFor(state);
 if(driver<0){update(renderer,state);return true;}
 request(driver,AssetPriority::Urgent);
 auto* set=impl_->find(driver);
 if(set&&!set->resident){
  if(set->job)set->job->cancel();
  IconJob job(driver);job.run();
  set->models=std::move(job.models);set->transforms=job.transforms;set->complete=job.complete;set->resident=true;set->job.reset();
 }
 // update() may erase other sets and move this one: read it first.
 const bool complete=set&&set->complete;
 update(renderer,state);
 return complete;
}
void MenuVehicleIcons::reset(Renderer& renderer){
 for(auto& set:impl_->sets){if(set.job)set.job->cancel();for(auto& model:set.models)renderer.retire(std::move(model));}
 impl_->sets.clear();impl_->shown=-1;
}
bool MenuVehicleIcons::draw(Renderer& renderer,const UiQuad& quad)const{
 // The caller has selected the target LCD. Vehicle buttons now live on the
 // lower screen; drawMenuIcon uses that screen's width, crop and depth target.
 if(quad.alpha<=0||quad.vehicleIcon>=12)return false;
 const auto* set=impl_->find(impl_->shown);if(!set||!set->resident)return false;
 const auto& model=set->models[quad.vehicleIcon];if(!model)return false;
 renderer.drawMenuIcon(*model,set->transforms[quad.vehicleIcon],quad);
 return true;
}
}
