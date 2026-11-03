#include <mkw/award_parade.hpp>
#include <mkw/course_object_kinds.hpp>
#include <game/field/KColData.hh>
#include <cstdio>

namespace mkw {
bool AwardParadeJob::prepareMii(const NativeMii& identity){for(auto& a:actors_)if(!a.model->prepareMii(identity))return false;return true;}

namespace {
struct Collision {
 std::vector<unsigned char> bytes;
 std::unique_ptr<Kinoko::Field::KColData> data;
 bool load(const char* path){
  FILE* file=std::fopen(path,"rb");if(!file)return false;
  std::fseek(file,0,SEEK_END);const long size=std::ftell(file);std::rewind(file);
  if(size<0x3c){std::fclose(file);return false;}
  bytes.resize(size);const bool ok=std::fread(bytes.data(),1,bytes.size(),file)==bytes.size();std::fclose(file);
  if(!ok)return false;
  auto be=[&](unsigned offset){return uint32_t(bytes[offset])<<24|uint32_t(bytes[offset+1])<<16|uint32_t(bytes[offset+2])<<8|bytes[offset+3];};
  if(be(0)>=bytes.size()||be(4)>=bytes.size()||be(8)+16>=bytes.size()||be(12)>=bytes.size())return false;
  data=std::make_unique<Kinoko::Field::KColData>(bytes.data());return true;
 }
 void sample(const Kinoko::EGG::Vector3f& position,Kinoko::Field::CollisionInfo& info,uint32_t& flags){
  using namespace Kinoko::Field;
  data->lookupSphere(100.f,position,Kinoko::EGG::Vector3f::inf,KCL_ANY);
  float distance;Kinoko::EGG::Vector3f normal;uint16_t attribute;
  while(data->checkSphereCollision(&distance,&normal,&attribute)){
   const auto type=KCL_ATTRIBUTE_TYPE_BIT(attribute);flags|=type;
   if(type&KCL_TYPE_SOLID_SURFACE)info.update(distance,normal*distance,normal,type);
  }
 }
};
}
void AwardParadeJob::run(){
 using namespace Kinoko::Field;
 if(cancelled())return;
 // loser_demo's DemoCol has identity placement. It participates in the
 // original floor query even though its helper mesh is never drawn.
 Collision course,helper;
 failure_="models/award_loser_demo.kcl";if(!course.load(failure_.c_str()))return;
 failure_="models/award_loser_demo_DemoCol.kcl";if(!helper.load(failure_.c_str()))return;
 std::vector<AwardResults::Player> players;
 for(unsigned i=0;i<results_.count;++i){const auto& p=results_.players[i];
  if(results_.teams?p.team==results_.humanTeam:p.human)players.push_back(p);
 }
 std::stable_partition(players.begin(),players.end(),[](const auto& p){return p.human;});
 if(players.empty()||players.size()>environment_.startCount){failure_="ceremony vehicle placements";return;}
 std::vector<Actor> actors;
 for(unsigned i=0;i<players.size();++i){
  if(cancelled())return;
  // Awards camera 12 uses KTPT record i (USA 80531908), not its
  // player selector, which is only meaningful for ordinary starting grids.
  const auto* start=&environment_.starts[i];
  const auto& player=players[i];
  Actor actor;actor.model=std::make_unique<CeremonyVehicle>();
  if(!actor.model->load(player.character,player.vehicle,results_.teams?player.team:RaceTeam::None,&failure_))return;
  Kinoko::EGG::Vector3f position{start->position.x,start->position.y,start->position.z};
  Kinoko::Field::CollisionInfo info;info.reset();uint32_t flags=0;
  course.sample(position,info,flags);helper.sample(position,info,flags);
  if(flags&KCL_TYPE_FLOOR)
   position+=info.bbox.min+info.bbox.max+info.floorNrm*(VehicleAnimation::initialHeight(player.vehicle)-100.f);
  Kinoko::EGG::Matrix34f matrix;
  matrix.makeRT({start->rotation.x*.01745329252f,start->rotation.y*.01745329252f,start->rotation.z*.01745329252f},position);
  failure_="ceremony initial vehicle pose";
  if(!VehicleAnimation::initialSnapshot(player.vehicle,objectMatrix(matrix,{1,1,1}),actor.initial))return;
  failure_="ceremony driver bad animation";
  if(!actor.model->pose(actor.initial,{"bad",0,"bad",0},0))return;
  actors.push_back(std::move(actor));
 }
 if(cancelled())return;
 actors_=std::move(actors);failure_.clear();loaded=true;
}
void AwardParadeJob::update(unsigned tick){
 for(auto& actor:actors_)actor.model->pose(actor.initial,{"bad",float(tick),"bad",float(tick)},tick);
}
void AwardParadeJob::draw(Renderer& renderer)const{
 std::array<Renderer::ShadowVolume,12> shadows{};unsigned count=0;
 for(const auto& actor:actors_)shadows[count++]=actor.model->shadow();
 renderer.drawShadowVolumes(shadows.data(),count,.5f);
 for(const auto& actor:actors_)actor.model->draw(renderer);
}
}
