#include "ObjectQuicksand.hh"
#include "game/field/CollisionDirector.hh"
#include "game/field/ObjectDirector.hh"
#include "game/kart/KartObject.hh"
#include <cmath>
#include "game/system/RaceManager.hh"
#include <algorithm>
#include <type_traits>
namespace Kinoko::Field {
// RMCU807fceb0, globals808d05f0: wait1200, wave800, reset600;
// amplitude200, spatial frequency1.5, depth360, period40, radius9500.
ObjectQuicksand::ObjectQuicksand(const System::MapdataGeoObj& p):ObjectDrivable(p),m_origin(pos()){
    if(p.setting(0)!=1){m_thwomp=EGG::egg_new<ObjectQuicksandThwomp>(p);m_thwomp->load();}
}
unsigned ObjectQuicksand::state(u32 frame){const u32 p=frame%2600;return p<1200?1:p<2000?2:0;}
unsigned ObjectQuicksand::stateFrame(u32 frame){const u32 p=frame%2600;return p<1200?p:p<2000?p-1200:p-2000;}
f32 ObjectQuicksand::waveAmplitude(u32 frame)const{
    if(state(frame)!=2)return 0.f;
    const f32 t=static_cast<f32>(stateFrame(frame));f32 degrees;
    if(t<165.f)degrees=t;
    else if(t<500.f)degrees=15.f;
    else degrees=15.f*(800.f-t)/300.f;
    return 200.f*EGG::Mathf::sin(degrees*DEG2RAD);
}
f32 ObjectQuicksand::surfaceOffset(f32 radius,u32 frame)const{
    // 807fdd8c..e028; this same radial function drives all20 mesh joints
    // and the original analytic floor collision807fe21c (vertical normal).
    const f32 phase=F_TAU-F_PI*(2.f*radius)/9500.f;
    const f32 wave=waveAmplitude(frame)*phase*EGG::Mathf::sin(phase*1.5f+F_PI*static_cast<f32>(frame)/40.f);
    return wave-360.f*phase;
}
EGG::Vector3f ObjectQuicksand::flowVelocity(const EGG::Vector3f& p,u32 frame)const{
    const f32 r=EGG::Mathf::sqrt(p.x*p.x+p.z*p.z);
    EGG::Vector3f direction=r>0.f?EGG::Vector3f(p.x/r,0.f,p.z/r):EGG::Vector3f::zero;
    const unsigned s=state(frame),t=stateFrame(frame);f32 strength=-1.f;
    if(s==0)strength=std::clamp(-10.f*static_cast<f32>(t)/1200.f,-1.f,0.f);
    if(s==2){strength=10.f-10.f*static_cast<f32>(t)/1200.f;if(t<10)direction=EGG::Vector3f::ey;}
    return direction*(strength*13.f*2000.f/std::clamp(r,3000.f,9500.f));
}
void ObjectQuicksand::calc(){
    const auto t=System::RaceManager::Instance()->timer();
    const auto phase=state(t),age=stateFrame(t);
    //807fda34..dcf8: reverse the sand while sinking; ease through the
    // last60 wave ticks and the first240 reset ticks, retaining SRT phase.
    f32 rate=-3.f;
    if(phase==2)rate=age<=740?3.f:3.f*(800.f-age)/60.f;
    else if(phase==0)rate=-3.f*std::min(1.f,age/240.f);
    m_scrollFrame+=rate;
    if(m_scrollFrame<0.f)m_scrollFrame+=240.f;
    else if(m_scrollFrame>=240.f)m_scrollFrame-=240.f;
    cueSound(phase==2?0x311:0x310,SoundCall::Hold);
}
template<class T> bool ObjectQuicksand::collide(f32 radius,const EGG::Vector3f& p,KCLTypeMask mask,T* info,KCLTypeMask* out,u32 offset,bool push){
    if(!(mask&(KCL_TYPE_FLOOR|KCL_TYPE_BIT(COL_TYPE_FALL_BOUNDARY))))return false;
    const auto local=p-m_origin;const f32 r=EGG::Mathf::sqrt(local.x*local.x+local.z*local.z);
    if(r>9500.f)return false;
    const u32 t=System::RaceManager::Instance()->timer()-offset;
    const f32 dist=radius-(local.y-surfaceOffset(r,t));if(dist<=0.f)return false;
    const bool sink=push&&r<.4f*900.f&&state(t)!=2&&t!=0;
    const auto tangent=EGG::Vector3f::ey*dist;
    // The centre reports a non-solid fall trigger, not a supporting floor.
    // Kinoko applies every returned bbox displacement directly to the body
    // and tyres, so it must not carry sand support with this trigger.
    if(info&&!sink){info->bbox.min=info->bbox.min.minimize(tangent);info->bbox.max=info->bbox.max.maximize(tangent);
        if constexpr(std::is_same_v<T,CollisionInfo>){
            info->updateFloor(dist,EGG::Vector3f::ey);
            if(dist>info->movingFloorDist){info->movingFloorDist=dist;info->roadVelocity=flowVelocity(local,t);info->floorObject=this;}
        }
    }
    if(out){
        // Retail USA80800864..87c selects slippery inner sand / weak
        // off-road outer sand, not ordinary road variant3. Only the wave
        // phase is trickable (80800b44..b54).
        const auto type=r>3000.f?COL_TYPE_SLIPPERY_ROAD:COL_TYPE_WEAK_OFF_ROAD;
        if(push){
            auto* cd=CollisionDirector::Instance();
            // USA80800ae4..b28: the still centre is a sink, except frame0.
            if(sink){
                cd->pushCollisionEntry(dist,out,KCL_TYPE_BIT(COL_TYPE_FALL_BOUNDARY),CollisionDirector::CollisionAttribute(COL_TYPE_FALL_BOUNDARY));
                cd->setCurrentCollisionVariant(6);
            }else{
                cd->pushCollisionEntry(dist,out,KCL_TYPE_BIT(type),CollisionDirector::CollisionAttribute(type));
                cd->setCurrentCollisionTrickable(state(t)==2);
            }
        }else *out|=KCL_TYPE_BIT(type);
    }
    return true;
}
ObjectQuicksandThwomp::ObjectQuicksandThwomp(const System::MapdataGeoObj& p):ObjectDossunNormal(p){
    // Original807fcf94..fc8. Use dossunc's existing sphere/reaction entry
    // for this original dossun child, retaining its owning KMP placement.
    m_groundDuration=800;m_id=ObjectId::Dossunc;setPos(EGG::Vector3f(pos().x,11000.f,pos().z));setScale(EGG::Vector3f(3.f,3.f,3.f));
}
void ObjectQuicksandThwomp::init(){ObjectDossunNormal::init();m_stillTimer=10000;}
void ObjectQuicksandThwomp::calc(){
    const auto* race=System::RaceManager::Instance();
    const u32 t=race->isStageReached(System::RaceManager::Stage::Race)?race->timer():0;
    const unsigned s=ObjectQuicksand::state(t),age=ObjectQuicksand::stateFrame(t);
    if(s==1)m_stillTimer=std::max<s32>(1,1200-static_cast<s32>(age+7+m_fallDuration+10));
    else m_stillTimer=800000;
    if(s==2&&age==0)cueSound(0x312,SoundCall::Start);
    ObjectDossunNormal::calc();
}
Kart::Reaction ObjectQuicksandThwomp::onCollision(Kart::KartObject* kart,Kart::Reaction onKart,Kart::Reaction,EGG::Vector3f& depth){
    //807fcd54: top contact within29 degrees, excluding the rising phase.
    const f32 angle=std::fabs(std::atan2(depth.cross(EGG::Vector3f::ey).length(),depth.dot(EGG::Vector3f::ey)))*RAD2DEG;
    if(angle<29.f&&m_anmState!=AnmState::Rising){
        cueSound(0x245,SoundCall::Voice);
        const auto& table=ObjectDirector::Instance()->hitTableKart();
        return table.reaction(table.slot(ObjectId::DossuncSoko),ObjectHitTable::KartColumn(kart));
    }
    return onKart;
}
}
