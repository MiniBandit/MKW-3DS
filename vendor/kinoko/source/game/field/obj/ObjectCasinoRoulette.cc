#include "ObjectCasinoRoulette.hh"
#include "game/system/RaceManager.hh"
#include "game/field/CollisionDirector.hh"
namespace Kinoko::Field {
ObjectCasinoRoulette* ObjectCasinoRoulette::s_instance=nullptr;
ObjectCasinoRoulette::~ObjectCasinoRoulette(){if(s_instance==this)s_instance=nullptr;}
void ObjectCasinoRoulette::init(){s_instance=this;setTransform(getUpdatedMatrix(0));}
void ObjectCasinoRoulette::carryCourseContact(CollisionInfo* info,KCLTypeMask mask,const EGG::Vector3f& p){
    if(!s_instance||!info)return;
    // USA80877990..9e8: the large deck lives in course.kcl. Its existing
    // collision flags, not this object's small centre KCL/broadphase, select
    // rotation. BoostPad variant1 belongs to the same deck.
    bool carries=(mask&0x20008180u)!=0;
    if(!carries&&(mask&KCL_TYPE_BIT(COL_TYPE_BOOST_PAD))){
        auto* collisions=CollisionDirector::Instance();
        carries=collisions->findClosestCollisionEntry(&mask,KCL_TYPE_BIT(COL_TYPE_BOOST_PAD))&&
            collisions->closestCollisionEntry()->variant()==1;
    }
    // USA80877b04 uses the original100.0 support priority, not penetration.
    if(carries&&info->movingFloorDist<100.f){
        info->movingFloorDist=100.f;info->roadVelocity=surfaceMotion(p);info->floorObject=s_instance;
    }
}
void ObjectCasinoRoulette::calc(){setTransform(getUpdatedMatrix(0));}
const EGG::Matrix34f& ObjectCasinoRoulette::getUpdatedMatrix(u32 offset){
    const u32 t=System::RaceManager::Instance()->timer()-offset;
    m_rotation.makeRT(EGG::Vector3f(0.f,static_cast<f32>(t)*.002f,0.f),EGG::Vector3f::zero);
    return m_rotation;
}
EGG::Vector3f ObjectCasinoRoulette::surfaceMotion(const EGG::Vector3f& p){
    //80877bc0: one-step matrix80877148, preserve the contact's XZ radius,
    // then subtract its old position. This is displacement, not angular speed
    // added to a kart's own steering/drive velocity.
    const f32 radius=EGG::Mathf::sqrt(p.x*p.x+p.z*p.z);
    if(radius==0.f)return EGG::Vector3f::zero;
    EGG::Matrix34f step;step.makeRT(EGG::Vector3f(0.f,.002f,0.f),EGG::Vector3f::zero);
    auto moved=step.ps_multVector33(EGG::Vector3f(p.x,0.f,p.z));
    const f32 length=moved.length();if(length>0.f)moved*=radius/length;
    return EGG::Vector3f(moved.x-p.x,0.f,moved.z-p.z);
}
void ObjectCasinoRoulette::transport(CollisionInfo* info,f32 before,const EGG::Vector3f& p){
    // The same closest moving-floor arbitration used by ObjColMgr. Wall-only
    // and missed queries must never drag entities standing on another floor.
    if(info&&info->movingFloorDist>before){info->roadVelocity=surfaceMotion(p);info->floorObject=this;}
}
#define ROULETTE_QUERY(NAME) \
bool ObjectCasinoRoulette::NAME(f32 r,const EGG::Vector3f& p,const EGG::Vector3f& prev,KCLTypeMask flags,CollisionInfo* info,KCLTypeMask* out,u32 offset){ \
 const f32 before=info?info->movingFloorDist:0.f; \
 const bool hit=ObjectKCL::NAME(r,p,prev,flags,info,out,offset); \
 if(hit)transport(info,before,p);return hit; \
}
ROULETTE_QUERY(checkSphereFull)
ROULETTE_QUERY(checkSphereCachedFull)
ROULETTE_QUERY(checkCollision)
ROULETTE_QUERY(checkCollisionCached)
#undef ROULETTE_QUERY
}
