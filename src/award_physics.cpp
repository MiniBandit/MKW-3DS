#include <mkw/award_physics.hpp>
#include <cstdio>
#include <game/system/ResourceManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/map/MapdataStartPoint.hh>
#include <game/system/KPadDirector.hh>
#include <game/system/KPadController.hh>
#include <game/kart/KartObjectManager.hh>
namespace mkw {
bool AwardPhysicsScene::create(const AwardResults& results){return createScene(results,false);}
bool AwardPhysicsScene::createEnding(const AwardResults& results){return createScene(results,true);}
bool AwardPhysicsScene::createScene(const AwardResults& results,bool ending){
 using Config=Kinoko::System::RaceConfig;
 if(active()||!results.valid||results.teams||results.count==0||results.count>results.players.size())return false;
 auto* config=Config::Instance();auto* parent=scenes_.currentScene();
 // The caller has destroyed the completed race, leaving its root alive.
 if(!config||!parent||scenes_.currentSceneId()!=0||parent->child())return false;
 const unsigned count=std::min(results.count,ending?12u:3u);
 for(unsigned i=0;i<count;++i)if(results.players[i].character>=42||results.players[i].vehicle>=36)return false;
 // Missing ceremony archives must fail before any global scenario changes.
 FILE* archive=std::fopen(ending?"Race/Course/ending_demo.szs":"Race/Course/winningrun_demo.szs","rb");
 if(!archive)return false;std::fclose(archive);
 previous_=config->raceScenario();
 Config::Scenario scenario{};
 scenario.course=ending?Kinoko::Course::Ending_Demo:Kinoko::Course::Win_Demo;scenario.cameraMode=ending?7:0;scenario.gameMode=Config::Scenario::GameMode::Awards;
 scenario.playerCount=count;scenario.engineClass=results.engineClass<3?results.engineClass:2;
 for(unsigned i=0;i<count;++i){
  auto& p=scenario.players[i];const auto& result=results.players[i];
  p.character=static_cast<Kinoko::Character>(result.character);
  p.vehicle=static_cast<Kinoko::Vehicle>(result.vehicle);
  p.type=Config::Player::Type::CPU;p.driftIsAuto=false;
 }
 config->prepareNextScenario(scenario);
 scenes_.createChildScene(2,parent);child_=scenes_.currentScene();
 if(!child_||child_==parent){child_=nullptr;config->raceScenario()=previous_;return false;}
 size_t size=0;const auto* data=Kinoko::System::ResourceManager::Instance()->getFile(
     "course.kmp",&size,Kinoko::System::ArchiveId::Course);
 if(!route_.load(data,size)){release();return false;}
 count_=count;frame_=0;actors_={};ending_=ending;
 CpuAi::Setup setup;setup.route=&route_;setup.count=count_;setup.humanMask=0;setup.demo=true;
 setup.engineClass=scenario.engineClass;ai_.reset(setup);
 // Base initAfterManager copies the selected KTPT's signed +0x18 field
 // into previous-param1 (USA80723070 -> 8072e208 -> 80747e04).
 // Demo Start multiplies it by 30; all three winningrun records are zero.
 for(unsigned i=0;i<count_;++i){
  const auto* start=Kinoko::System::CourseMap::Instance()->getStartPoint(i);
  if(!start){release();return false;}
  actors_[i].delay=int(start->playerIndex())*30;
 }
 return ai_.ready();
}
Kinoko::Kart::KartObject* AwardPhysicsScene::kart(unsigned i)const{
 if(!active()||scenes_.currentScene()!=child_||i>=count_)return nullptr;
 return Kinoko::Kart::KartObjectManager::Instance()->object(i);
}
bool AwardPhysicsScene::step(){
 if(!active()||scenes_.currentScene()!=child_)return false;
 for(unsigned i=0;i<count_;++i){
  auto& actor=actors_[i];CpuCommand command{};
  // Util::StateSequencer enters a requested state on the following tick.
  if(actor.enterEnd){actor.phase=Phase::End;actor.enterEnd=false;}
  else if(actor.enterRun){actor.phase=Phase::Run;actor.enterRun=false;}
  if(actor.phase==Phase::Start){if(--actor.delay<0)actor.enterRun=true;}
  else if(actor.phase==Phase::Run){
   const auto event=ai_.stepDemoRun(i,frame_);command=ai_.command(i);
   actor.celebrating|=event.celebrate;actor.enterEnd=event.end;
  }
  Kinoko::System::KPadDirector::Instance()->hostController(i)->setInputs(
      command.buttons,command.stick,0,Kinoko::System::Trick::None);
 }
 scenes_.calc();++frame_;return true;
}
void AwardPhysicsScene::release(){
 count_=0;frame_=0;actors_={};route_.clear();
 if(!child_)return;
 // Do not destroy an unrelated replacement scene if the caller changed it.
 if(scenes_.currentScene()!=child_){child_=nullptr;return;}
 child_->heap()->enableAllocation();scenes_.destroyCurrentSceneNoIncoming(false);
 child_=nullptr;
 if(auto* config=Kinoko::System::RaceConfig::Instance())config->raceScenario()=previous_;
}
}
