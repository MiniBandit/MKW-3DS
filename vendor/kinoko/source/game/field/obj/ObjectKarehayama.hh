#pragma once
#include "ObjectBreakable.hh"
namespace Kinoko::Field {
// Retail Karehayama / ItemDropper, USA80776c80..807775d0.
class ObjectKarehayama final : public ObjectBreakable {
public:
 explicit ObjectKarehayama(const System::MapdataGeoObj& p):ObjectBreakable(p){}
 void init()override;
 void calc()override;
 Kart::Reaction onCollision(Kart::KartObject*,Kart::Reaction,Kart::Reaction,EGG::Vector3f&)override;
 s16 onItemCollision(const EGG::Vector3f&,const EGG::Vector3f&,s16,s16,EGG::Vector3f&)override;
 bool shown()const{return m_state==1;}
 unsigned softSerial()const{return soft_;}
 unsigned hardSerial()const{return hard_;}
 float scatterVelocity()const{return scatter_;}
private:
 void scatter(bool soft,float speed);
 void drop(u8 owner,const EGG::Vector3f& motion);
 s32 timer_=0;unsigned soft_=0,hard_=0;float scatter_=25;
};
}
