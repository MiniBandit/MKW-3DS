"""Run production Pakkun damage callbacks at the model/animation boundary."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'vendor/kinoko/source/game/field/obj/ObjectPakkunF.cc').read_text()
methods=source[source.index('void ObjectPakkunF::enterDamage()'):source.index('} // namespace Kinoko::Field')]
header=(root/'vendor/kinoko/source/game/field/obj/ObjectPakkunF.hh').read_text()
global_callback=header[header.index('void onGlobalItemCollision'):header.index('    [[nodiscard]] bool damaged')].replace(' override','')
boundary=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using u32=uint32_t;using s16=int16_t;
namespace EGG {struct Vector3f{};}
namespace Kart {enum class Reaction:int {};struct KartObject{};}
struct Model {int clip=-1,calls=0;Model* anmMgr(){return this;}void playAnim(float,float,int c){clip=c;++calls;}};
struct ObjectPakkunF {
 enum class State {Wait,Attack,Damage};State m_state=State::Attack;
 u32 m_damageFrame=0,m_waitAnimationFrame=90,m_waitFrames=0,m_waitDuration=75;
 Model model;Model* m_drawMdl=&model;
 void enterDamage();void calcDamage();
 Kart::Reaction onCollision(Kart::KartObject*,Kart::Reaction,Kart::Reaction,EGG::Vector3f&);
 s16 onItemCollision(const EGG::Vector3f&,const EGG::Vector3f&,s16,s16,EGG::Vector3f&);
'''
test=r'''
int main(){EGG::Vector3f v;
 for(int entry=0;entry<3;++entry){ObjectPakkunF p;
  auto hit=[&](int reaction){if(entry==0)assert(p.onCollision(nullptr,Kart::Reaction(9),Kart::Reaction(reaction),v)==Kart::Reaction(9));
   else if(entry==1)assert(p.onItemCollision(v,v,7,reaction,v)==7);else p.onGlobalItemCollision(0,reaction);};
  if(entry<2){hit(0);hit(4);assert(p.m_state==ObjectPakkunF::State::Attack);}
  hit(3);assert(p.model.clip==1&&p.m_damageFrame==0);
  for(int tick=1;tick<180;++tick){p.calcDamage();if(tick==90)hit(5);assert(p.m_state==ObjectPakkunF::State::Damage);}
  assert(p.model.calls==1);p.calcDamage();assert(p.m_state==ObjectPakkunF::State::Wait);
  assert(p.model.clip==2&&p.m_waitFrames==75&&p.m_waitAnimationFrame==0);
  hit(5);assert(p.m_state==ObjectPakkunF::State::Damage&&p.m_damageFrame==0);
 }
 puts("PASS production Pakkun kart/item/global damage callbacks, repeated hits, 180-tick recovery and clips");
}
'''
with tempfile.TemporaryDirectory(prefix='pakkun-damage-') as temp:
 cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
 cpp.write_text(boundary+global_callback+'};\n'+methods+test)
 # This probe requires a separately compiled executable.
 if not Path(str(exe)).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(exe)))
 subprocess.run([str(exe)],check=True)
