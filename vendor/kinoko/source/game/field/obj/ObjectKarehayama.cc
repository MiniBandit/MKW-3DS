#include "ObjectKarehayama.hh"
#include "game/kart/KartObject.hh"
#include "game/kart/KartMove.hh"
#include "game/system/RaceConfig.hh"
#include "game/system/RaceManager.hh"
#include "game/field/ObjectDirector.hh"
#include "game/item/ItemDirector.hh"
namespace Kinoko::Field {
void ObjectKarehayama::init(){m_state=1;timer_=0;soft_=hard_=0;scatter_=25;}
void ObjectKarehayama::calc(){
 // ItemDropper::Update USA8077b1f4; Karehayama::Respawn80776f84.
 if(m_state==2&&timer_>0&&!--timer_){m_state=1;if(m_collision)ObjectBase::enableCollision();}
}
void ObjectKarehayama::scatter(bool soft,float speed){
 m_state=2;timer_=138;if(m_collision)disableCollision();
 if(soft){++soft_;scatter_=25.f+speed*(70.f-25.f);}
 ++hard_;cueSound(0x2e7,SoundCall::Start);
}
void ObjectKarehayama::drop(u8 owner,const EGG::Vector3f& motion){
 auto& random=System::RaceManager::Instance()->random();
 // Karehayama adds an original +/-5 degree random yaw before ItemDropper.
 const float yaw=(random.getF32(10.f)-5.f)*0.01745329238474369f;
 EGG::Matrix34f turn;turn.makeR({0,yaw,0});auto velocity=turn.multVector(motion)*2.f;
 const float chance=random.getF32(100.f);
 if(chance>=static_cast<s16>(m_mapObj->setting(2))||System::RaceConfig::Instance()->raceScenario().itemMode==3)return;
 velocity.y=2.f+random.getF32(1.f);
 EGG::Matrix34f rotation;rotation.makeR({rot().x,0,0});velocity=rotation.multVector(velocity);
 Item::ItemDirector::CreateCourseDrop(m_mapObj->setting(1),pos()+EGG::Vector3f::ey*300.f,velocity,owner);
}
Kart::Reaction ObjectKarehayama::onCollision(Kart::KartObject* kart,Kart::Reaction result,Kart::Reaction object,EGG::Vector3f& depth){
 const auto reaction=static_cast<s16>(object);
 if(!shown()||!kart||(reaction!=3&&reaction!=5))return result;
 if(reaction==5&&kart->status().onBit(Kart::eStatus::AfterCannon))return result;
 const float speed=kart->move()->speedRatio();
 if(speed>.4f)drop(kart->param()->playerIdx(),kart->velocity());
 scatter(true,speed);depth=EGG::Vector3f::zero;return result;
}
s16 ObjectKarehayama::onItemCollision(const EGG::Vector3f&,const EGG::Vector3f& velocity,s16 result,s16 object,EGG::Vector3f& depth){
 if(!shown()||(object!=3&&object!=4&&object!=5&&object!=6))return result;
 if(object==3||object==5)drop(ObjectDirector::Instance()->itemOwner(),-depth);
 scatter(false,velocity.length());depth=EGG::Vector3f::zero;return result;
}
}
