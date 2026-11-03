#pragma once
#include <mkw/award_results.hpp>
#include <mkw/race_grid.hpp>
namespace Kinoko::Kart { class KartObject; }
#include <game/system/RaceConfig.hh>
#include <egg/core/SceneManager.hh>
namespace mkw {
// Main-thread ownership only. Graphics jobs must be drained before release.
// Holds no pointer into the completed race's child heap.
class AwardPhysicsScene {
public:
 explicit AwardPhysicsScene(Kinoko::EGG::SceneManager& scenes):scenes_(scenes){}
 ~AwardPhysicsScene(){release();}
 AwardPhysicsScene(const AwardPhysicsScene&)=delete;
 AwardPhysicsScene& operator=(const AwardPhysicsScene&)=delete;
 bool create(const AwardResults&);
 bool createEnding(const AwardResults&);
 void release();
 bool step();
 unsigned count()const{return count_;}
 bool visible(unsigned i)const{return active()&&i<count_&&actors_[i].phase==Phase::Run;}
 bool celebrating(unsigned i)const{return active()&&i<count_&&actors_[i].celebrating;}
 Kinoko::Kart::KartObject* kart(unsigned i)const;
 bool active()const{return child_!=nullptr;}
private:
 enum class Phase {Start,Run,End};
 struct Actor {Phase phase=Phase::Start;bool enterRun=false,enterEnd=false,celebrating=false;int delay=0;};
 std::array<Actor,12> actors_{};
 bool ending_=false;
 bool createScene(const AwardResults&,bool ending);
 EnemyRoute route_;
 CpuAi ai_;
 unsigned count_=0,frame_=0;
 Kinoko::EGG::SceneManager& scenes_;
 Kinoko::EGG::Scene* child_=nullptr;
 Kinoko::System::RaceConfig::Scenario previous_{};
};
}
