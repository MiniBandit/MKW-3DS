"""Exercise production cannonball callbacks with a host race-mode boundary."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root/'vendor/kinoko/source/game/field/obj/ObjectHeyhoBall.cc').read_text()
methods = source[source.index('Kart::Reaction ObjectHeyhoBall::onCollision'):source.index('/// @addr{0x806D10A4}')]
boundary = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using s16=int16_t;
namespace EGG {struct Vector3f {int x=1,y=2,z=3;void setZero(){x=y=z=0;}};}
namespace Kart {enum class Reaction:int {WallAllSpeed=8,SpinSomeSpeed=10,ExplosionLoseItem=32};struct KartObject{};}
namespace System {struct RaceConfig {
 struct Scenario {enum class GameMode {Grand_Prix,VS_Race,Battle,Time_Trial,Ghost_Race};GameMode gameMode;};
 Scenario scenario;static RaceConfig* Instance(){static RaceConfig r;return &r;}Scenario& raceScenario(){return scenario;}
};}
struct ObjectHeyhoBall {
 enum class ExplosionIntensity {ExplosionLoseItem,SpinSomeSpeed};
 int m_currentStateId=2,m_nextStateId=-1,m_currentFrame=17;
 ExplosionIntensity m_intensity=ExplosionIntensity::ExplosionLoseItem;
 Kart::Reaction onCollision(Kart::KartObject*,Kart::Reaction,Kart::Reaction,EGG::Vector3f&);
 s16 onItemCollision(const EGG::Vector3f&,const EGG::Vector3f&,s16,s16,EGG::Vector3f&);
 void onGlobalItemCollision(s16,s16);void reactToItem(s16);
};
'''
test=r'''
int main(){
 using Mode=System::RaceConfig::Scenario::GameMode;
 for(int mode=0;mode<5;++mode)for(int state=0;state<4;++state)
 for(int intensity=0;intensity<2;++intensity)for(int reaction=0;reaction<8;++reaction)
 for(int vehicleReaction:{0,8,10,32}){
  System::RaceConfig::Instance()->scenario.gameMode=Mode(mode);
  ObjectHeyhoBall b;b.m_currentStateId=state;b.m_intensity=ObjectHeyhoBall::ExplosionIntensity(intensity);
  EGG::Vector3f depth;
  auto result=b.onCollision(nullptr,Kart::Reaction(vehicleReaction),Kart::Reaction(reaction),depth);
  if(mode>=3){
   assert(int(result)==(state==3?(intensity?10:32):8));
   assert(b.m_nextStateId==-1);
   assert((depth.x==0)==(state==3&&intensity==1));
  }else{
   assert(int(result)==(intensity==1&&vehicleReaction==32?10:vehicleReaction));
   assert(b.m_nextStateId==((state!=3&&(reaction==3||reaction==5))?3:-1));
   assert((depth.x==0)==(intensity==1));
  }
  assert(b.m_currentFrame==17); // callbacks do not advance or restart the clock
 }
 for(int entry=0;entry<2;++entry)for(int state=0;state<4;++state)for(int response=-1;response<=8;++response){
  ObjectHeyhoBall b;b.m_currentStateId=state;EGG::Vector3f depth;
  for(int repeat=0;repeat<3;++repeat){
   if(entry) b.onGlobalItemCollision(7,response);
   else assert(b.onItemCollision(depth,depth,7,response,depth)==7);
   assert(b.m_nextStateId==((state!=3&&response>=3&&response<=6)?3:-1));
   assert(depth.x==1&&depth.y==2&&depth.z==3&&b.m_currentFrame==17);
  }
 }
 puts("PASS production HeyhoBall kart/item/global callbacks: modes, immunity, explosion phases, response table, repeated-hit clock preservation");
}
'''
with tempfile.TemporaryDirectory(prefix='heyho-ball-hit-') as tmp:
 cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
 cpp.write_text('#include <initializer_list>\n'+boundary+methods+test)
 # This probe requires a separately compiled executable.
 if not Path(str(exe)).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(exe)))
 subprocess.run([str(exe)],check=True)
