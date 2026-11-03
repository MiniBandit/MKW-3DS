"""Exercise the production query wrapper with a destructive collision backend."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'vendor/kinoko/source/game/field/CollisionDirector.cc').read_text()
body=s[s.index('bool CollisionDirector::checkCameraSphere('):s.index('/// @brief Finds the closest KCL triangle')]
prefix=r'''
#include <array>
#include <cassert>
#include <cstdio>
using f32=float;using KCLTypeMask=unsigned;
namespace EGG {struct Vector3f {float x=0,y=0,z=0;static const Vector3f zero;};const Vector3f Vector3f::zero{};}
constexpr unsigned KCL_TYPE_CAMERA_COLLIDABLE=0x62f8bdff,KCL_TYPE_WALL=0xd010f000;
struct CollisionInfo{EGG::Vector3f tangentOff{4,5,6};};
struct CourseColMgr{int* pending=nullptr;static CourseColMgr* Instance(){static CourseColMgr c;return &c;}auto noBounceWallInfo(){return pending;}void clearNoBounceWallInfo(){pending=nullptr;}void setNoBounceWallInfo(int* p){pending=p;}};
struct CollisionDirector{
 std::array<int,64> m_entries{};unsigned m_collisionEntryCount=0;const int* m_closestCollisionEntry=nullptr;bool hit=true;
 bool checkSphereFullPush(float radius,const EGG::Vector3f& pos,const EGG::Vector3f& prev,unsigned mask,CollisionInfo*,unsigned* result,unsigned time){
 assert(radius==65 && pos.x==1 && prev.x==2 && mask==0x62f8bdff && time==0);
 assert(CourseColMgr::Instance()->noBounceWallInfo()==nullptr);
 m_entries.fill(99);m_collisionEntryCount=64;m_closestCollisionEntry=&m_entries[63];*result=0x1000;return hit;}
 bool findClosestCollisionEntry(unsigned*,unsigned mask){assert(mask==KCL_TYPE_WALL);return true;}
 bool checkCameraSphere(float,const EGG::Vector3f&,const EGG::Vector3f&,EGG::Vector3f&,bool&);
};
'''
post=r'''
int main(){CollisionDirector d;d.m_entries[0]=7;d.m_collisionEntryCount=1;d.m_closestCollisionEntry=&d.m_entries[0];int pending=42;CourseColMgr::Instance()->pending=&pending;
for(bool hit:{true,false}){d.hit=hit;EGG::Vector3f push;bool wall=false;assert(d.checkCameraSphere(65,{1,0,0},{2,0,0},push,wall)==hit);assert(wall==hit);assert(push.x==(hit?4:0));assert(d.m_entries[0]==7 && d.m_collisionEntryCount==1 && d.m_closestCollisionEntry==&d.m_entries[0]);assert(CourseColMgr::Instance()->pending==&pending && pending==42);}
puts("PASS production camera query restores collision entries, closest pointer and pending no-bounce output on hit/miss");}
'''
with tempfile.TemporaryDirectory(prefix='mkw-camera-isolation-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(prefix+body+post)
 # This probe requires a separately compiled executable.
 if not Path(str(p / 'test')).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(p / 'test')))
 subprocess.run([str(p/'test')],check=True)
