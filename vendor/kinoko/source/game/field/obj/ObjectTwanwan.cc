#include "ObjectTwanwan.hh"
#include "game/field/CollisionDirector.hh"
#include "game/field/ObjectCollisionSphere.hh"
#include "game/field/ObjectDirector.hh"
#include "game/kart/KartObject.hh"
#include "game/system/RaceManager.hh"
#include <algorithm>
#include <cmath>
namespace Kinoko::Field {
namespace {
using V=EGG::Vector3f;
V mix(const V& a,const V& b,f32 t){return a+(b-a)*t;}
V unit(V v,const V& fallback){return v.squaredLength()>1.1920928955078125e-7f?(v.normalise(),v):fallback;}
V reflected(const V& v,const V& normal,f32 restitution){const f32 dot=-v.dot(normal);return dot>0.f?v+normal*(dot+dot*restitution):v;}
}
ObjectTwanwan::ObjectTwanwan(const System::MapdataGeoObj& p):ObjectCollidable(p),
    m_radius(296.f*scale().x),m_speed(static_cast<s16>(p.setting(0))),
    m_jumpWidth(static_cast<s16>(p.setting(2))),m_jumpPeriod(p.setting(3)),
    m_moveStart(p.setting(5)),m_fallStart(p.setting(4)){}
void ObjectTwanwan::init(){
    ASSERT(m_railInterpolator);m_railInterpolator->init(0.f,0);m_railInterpolator->setCurrVel(0.f);
    calcTransform();m_forward=transform().base(1);m_right=transform().base(0);
    m_target=m_railInterpolator->curPos()+V::ey*m_radius;
    m_work=m_mapObj->setting(1)?pos()+V::ey*m_radius:m_target;
    m_velocity=V::ey;m_turnAxis=V::ey;m_pitch=m_yaw=m_hopVelocity=m_hopHeight=0.f;
    m_fullSphere=true;m_grounded=false;m_age=0;m_nextTimer=System::RaceManager::Instance()->timer();
    publish();
}
void ObjectTwanwan::step(u32 timer){
    // 8088744c..7728: absolute simulation timer,660-frame release,
    // gravity starts600, wheel-specific collision shrinks at900.
    if(m_age==m_fallStart+300){
        m_fullSphere=false;m_target=m_railInterpolator->curPos()+V::ey*200.f;
        m_work=pos()-(m_forward*(m_radius-200.f)+V::ey*m_hopHeight);
    }
    if(m_age>=m_moveStart){
        if(m_age==m_moveStart)m_railInterpolator->setCurrVel(m_speed);
        m_railInterpolator->calc();
        const f32 phase=timer<m_fallStart||!m_jumpPeriod?0.f:float((timer-m_fallStart)%m_jumpPeriod)/m_jumpPeriod;
        const V side=unit(V::ey.cross(m_railInterpolator->curTangentDir()),V::ex);
        const V target=m_railInterpolator->curPos()+side*(m_jumpWidth*EGG::Mathf::sin(F_TAU*phase));
        m_target=mix(m_target,target,.05f);
    }
    if(m_age>=m_fallStart){if(m_age==m_fallStart)cueSound(0x23a,SoundCall::Start);move();cueSound(0x237,SoundCall::Hold);}
    ++m_age;
}
void ObjectTwanwan::calc(){
    const auto timer=System::RaceManager::Instance()->timer();
    while(m_nextTimer<=timer){step(m_nextTimer);++m_nextTimer;}
}
void ObjectTwanwan::move(){
    // 808866d4: gravity/target steering, collision reflection, rolling and
    // bounce share the original working position; render and hazard cannot drift.
    V velocity=mix(m_velocity,m_velocity-V::ey*5.f,.08f);
    V proposed=mix(m_work,m_work+velocity,.7f);
    f32 hopVelocity=m_hopVelocity-5.f*.99f;
    f32 hopHeight=m_hopHeight+.99f*hopVelocity;
    if(hopHeight<0.f){hopHeight=0.f;if(hopVelocity<0.f)hopVelocity=-hopVelocity*.3f;}
    const f32 hopAcceleration=hopVelocity-m_hopVelocity;
    V forward=m_forward,right=m_right,axis=m_turnAxis;
    f32 pitch=m_pitch,yaw=m_yaw;
    CollisionInfo info{};KCLTypeMask mask=0;
    auto* cd=CollisionDirector::Instance();
    if(m_fullSphere){
        cd->checkCourseColNarrScLocal(m_radius+.1f,proposed,0x64ebdfff,0);
        m_grounded=cd->checkSphereCachedFullPush(m_radius,proposed,V::inf,0x64ebdfff,&info,&mask,0);
    }else m_grounded=cd->checkSphereFullPush(200.f,proposed,V::inf,0x64ebdfff,&info,&mask,0);
    if(m_grounded){
        proposed+=info.tangentOff;
        if(info.tangentOff.squaredLength()>1.1920928955078125e-7f){
            const V n=unit(info.tangentOff,V::ey);velocity=reflected(velocity,n,.6f);
            forward=unit(mix(m_forward,n,.05f),m_forward);
        }
        const V displacement=proposed-m_work;
        if(displacement.squaredLength()>1.1920928955078125e-7f){
            const V nextRight=unit(m_forward.cross(displacement),m_right);
            right=unit(mix(m_right,nextRight,.35f),m_right);
        }
        if(info.floorDist>-std::numeric_limits<f32>::min()){
            const V aim=(m_target-proposed).rej(forward);
            if(aim.squaredLength()>1.1920928955078125e-7f){
                const V aimDir=unit(aim,forward);
                const V travel=velocity.rej(forward);
                const V travelDir=unit(travel,aimDir);
                const f32 turn=-std::fabs(std::atan2(aimDir.cross(travelDir).length(),aimDir.dot(travelDir)))*.04f;
                const V turnAxis=unit(aimDir.cross(travelDir),forward);
                EGG::Matrix34f rot;rot.setAxisRotation(turn,turnAxis);
                const f32 targetSpeed=std::min(70.f,.3f*aim.length());
                const f32 speed=travel.length()+.1f*(targetSpeed-travel.length());
                velocity+=rot.ps_multVector(travelDir)*speed-travel;
            }
            pitch=m_pitch+.9f*(.0007f*displacement.rej(forward).length()-m_pitch);
            if(info.wallDist>-std::numeric_limits<f32>::min()){
                velocity+=info.wallNrm*.01f;
                const V n=unit(info.wallNrm,V::ey);
                axis=mix(m_turnAxis,forward.cross(n),.04f);
                yaw=m_yaw+.04f*(-.0012f*info.wallNrm.length()-m_yaw);
            }
            if((velocity-m_velocity).length()>=30.f||std::fabs(hopAcceleration)>=30.f)cueSound(0x239,SoundCall::Start);
            else if(hopHeight<5.f)cueSound(0x238,SoundCall::Hold);
        }
    }
    m_forward=forward;m_right=right;m_turnAxis=axis;m_pitch=pitch;m_yaw=yaw;
    m_velocity=velocity;m_work=proposed;m_hopVelocity=hopVelocity;m_hopHeight=hopHeight;
    if(m_hitAge<65)++m_hitAge;
    publish();
}
void ObjectTwanwan::publish(){
    const V centre=m_fullSphere?m_work+V::ey*m_hopHeight:m_work+m_forward*(m_radius-200.f)+V::ey*m_hopHeight;
    EGG::Matrix34f pitch,yaw,base=transform();base.setBase(3,V::zero);
    pitch.setAxisRotation(m_pitch,m_right);yaw.setAxisRotation(m_yaw,m_turnAxis);
    auto m=yaw.multiplyTo(pitch.multiplyTo(base));m.setBase(3,centre);setTransform(m);
}
void ObjectTwanwan::calcCollisionTransform(){
    if(m_collision){EGG::Matrix34f m; m.makeRT(V::zero,pos());m_collision->transform(m,V(299.f/330.f*scale().x,1.9f*scale().y,299.f/330.f*scale().z));}
}
void ObjectTwanwan::hit(s16 reaction){if((reaction==3||reaction==5)&&m_grounded&&m_hitAge>=65){m_hitAge=0;m_hopVelocity=170.f;}}
Kart::Reaction ObjectTwanwan::onCollision(Kart::KartObject* kart,Kart::Reaction onKart,Kart::Reaction onObj,V&){
    hit(static_cast<s16>(onObj));
    //80887800..b0: upper/airborne hits use the separate Twanwan_ue row.
    if(!m_grounded||m_forward.dot(kart->pos()-pos())>200.f-m_radius){
        const auto& table=ObjectDirector::Instance()->hitTableKart();
        return table.reaction(table.slot(static_cast<ObjectId>(0xef)),ObjectHitTable::KartColumn(kart));
    }
    return onKart;
}
s16 ObjectTwanwan::onItemCollision(const V&,const V&,s16 onItem,s16 onObj,V&){hit(onObj);return onItem;}
void ObjectTwanwan::onGlobalItemCollision(s16,s16 onObj){hit(onObj);}
}
