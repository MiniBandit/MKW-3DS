"""Production fire-snake callbacks and StateManager despawn/child cleanup."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'vendor/kinoko/source/game/field/obj/ObjectFireSnake.cc').read_text()
h=(root/'vendor/kinoko/source/game/field/obj/ObjectFireSnake.hh').read_text()
def method(start,end):return s[s.index(start):s.index(end,s.index(start))]
callbacks=method('Kart::Reaction ObjectFireSnake::onCollision','/// @addr{0x806C23C8}')
cleanup=method('void ObjectFireSnake::enterDespawned()','/// @addr{0x806C19E8}')
children=method('void ObjectFireSnake::calcChildren()','/// @brief Helper function')
enter=h[h.index('void enterDespawning()'):h.index('\n',h.index('void enterDespawning()'))]
manager=(root/'vendor/kinoko/source/game/field/StateManager.hh').read_text().replace('#pragma once','').replace('#include "game/field/obj/ObjectBase.hh"','')
pre=r'''
#include <array>
#include <span>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
using u16=uint16_t;using u32=uint32_t;using s16=int16_t;using s32=int32_t;
namespace EGG {
void* egg_alloc(size_t s){return std::malloc(s);}void egg_free(void*p){std::free(p);}
struct Vector3f{int x=1,y=2,z=3;};struct Matrix34f{void setBase(int,Vector3f){}};
}
namespace Kart {enum class Reaction:int{};struct KartObject{};}
'''
body=r'''
namespace Kinoko::Field {
struct Kid {bool registered=true;int removed=0;bool getUnit(){return registered;}void unregisterCollision(){registered=false;++removed;}void loadAABB(float){registered=true;}void setTransform(EGG::Matrix34f){}};
struct ObjectFireSnake:StateManager {
 ObjectFireSnake():StateManager(this,entries){};
 bool registered=true;int removed=0;EGG::Vector3f m_sunPos{9,8,7},m_visualPos{},position{};
 std::array<EGG::Matrix34f,21> m_prevTransforms{};Kid a,b;std::array<Kid*,2> m_kids{&a,&b};
 EGG::Matrix34f transform(){return {};}void calcTransform(){};EGG::Vector3f pos(){return position;}
 void setPos(EGG::Vector3f p){position=p;}bool getUnit(){return registered;}void unregisterCollision(){registered=false;++removed;}
 void empty(){};void enterDespawned();void calcChildren();
 Kart::Reaction onCollision(Kart::KartObject*,Kart::Reaction,Kart::Reaction,EGG::Vector3f&);
 s16 onItemCollision(const EGG::Vector3f&,const EGG::Vector3f&,s16,s16,EGG::Vector3f&);
 void onGlobalItemCollision(s16,s16);
 using StateManager::m_currentStateId;using StateManager::m_nextStateId;using StateManager::m_currentFrame;
 void tick(){StateManager::calc();calcChildren();}
'''
entries=r'''
 static constexpr std::array<StateManagerEntry,6> entries={{
 StateEntry<ObjectFireSnake,&ObjectFireSnake::enterDespawned,&ObjectFireSnake::empty>(0),
 StateEntry<ObjectFireSnake,&ObjectFireSnake::empty,&ObjectFireSnake::empty>(1),
 StateEntry<ObjectFireSnake,&ObjectFireSnake::empty,&ObjectFireSnake::empty>(2),
 StateEntry<ObjectFireSnake,&ObjectFireSnake::empty,&ObjectFireSnake::empty>(3),
 StateEntry<ObjectFireSnake,&ObjectFireSnake::empty,&ObjectFireSnake::empty>(4),
 StateEntry<ObjectFireSnake,&ObjectFireSnake::enterDespawning,&ObjectFireSnake::empty>(5)}};
};
'''
test=r'''
}
int main(){using namespace Kinoko::Field;
 for(int entry=0;entry<3;++entry)for(int state=0;state<6;++state)for(int response=-1;response<=8;++response){
  ObjectFireSnake s;s.m_currentStateId=state;s.m_currentFrame=17;
  EGG::Vector3f v;
  if(entry==0)assert(s.onCollision(nullptr,Kart::Reaction(11),Kart::Reaction(response),v)==Kart::Reaction(11));
  else if(entry==1)assert(s.onItemCollision(v,v,9,response,v)==9);
  else s.onGlobalItemCollision(9,response);
  bool hit=state!=5&&(entry==0?(response==3||response==5):(response>=3&&response<=6));
  assert(s.m_nextStateId==(hit?5:-1));assert(s.m_currentFrame==17&&v.x==1&&v.y==2&&v.z==3);
  if(hit){
   s.tick();assert(s.m_currentStateId==5&&s.m_nextStateId==0);
   s.onGlobalItemCollision(9,5);assert(s.m_nextStateId==0); // repeat must not cancel exit
   s.tick();assert(s.m_currentStateId==0&&s.m_nextStateId==-1);
   assert(!s.registered&&!s.a.registered&&!s.b.registered);
   assert(s.removed==1&&s.a.removed==1&&s.b.removed==1&&s.position.x==9);
   s.tick();assert(s.removed==1&&s.a.removed==1&&s.b.removed==1);
  }
 }
 puts("PASS production FireSnake callbacks + StateManager: item/global/kart gates, one-update hit state, repeated hits preserve exit, parent and two children unregister once");
}
'''
with tempfile.TemporaryDirectory(prefix='firesnake-hit-') as tmp:
 cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
 cpp.write_text(pre+manager+body+enter+entries+callbacks+cleanup+children+test)
 # This probe requires a separately compiled executable.
 if not Path(str(exe)).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(exe)))
 subprocess.run([str(exe)],check=True)
