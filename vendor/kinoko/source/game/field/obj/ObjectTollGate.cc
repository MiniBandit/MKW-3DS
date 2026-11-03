#include "ObjectTollGate.hh"
#include "game/field/ObjectCollisionCylinder.hh"
#include "game/field/ObjectDirector.hh"
namespace Kinoko::Field {
ObjectTollGate::ObjectTollGate(const System::MapdataGeoObj &params)
    : ObjectCollidable(params),m_initialRot(params.rot()*DEG2RAD) {
    m_initialMatrix.makeR(m_initialRot);
}
void ObjectTollGate::createCollision() {
    const auto& table=ObjectDirector::Instance()->flowTable();
    const auto* set=table.set(table.slot(id())); ASSERT(set);
    m_collision=EGG::egg_new<ObjectCollisionCylinder>(1.05f*parse<s16>(set->params.cylinder.radius),
            parse<s16>(set->params.cylinder.height),EGG::Vector3f(675.f,0.f,0.f));
    setRot(m_initialRot+EGG::Vector3f(1.5707963705062866f,0.f,0.f));
    registerManagedObject();
}
void ObjectTollGate::init() {
    // Retail runs the opening/closing cycle to its zero-angle endpoint at init.
    m_state=0;m_angle=0;applyAngle();
    ObjectCollidable::calcCollisionTransform();
}
void ObjectTollGate::applyAngle() {
    EGG::Matrix34f rotation;rotation.makeR(EGG::Vector3f(0.f,0.f,float(m_angle)*DEG2RAD));
    auto matrix=m_initialMatrix.multiplyTo(rotation);matrix.setBase(3,pos());
    calcTransform(); // Consume pending Euler flags before installing the procedural pose.
    setTransform(matrix);
}
void ObjectTollGate::calc() {
    if(m_state==1){
        if(m_angle==0)cueSound(id()==static_cast<ObjectId>(0x148)?0x2b3:0x2dc,SoundCall::Start);
        if(m_angle<90){m_angle+=5;applyAngle();}else m_state=3;
    }else if(m_state==3&&m_angle){m_angle-=5;applyAngle();}
}
void ObjectTollGate::calcCollisionTransform() {
    // USA806c4878: once triggered, the original trigger volume stays fixed.
    if(m_state==0)ObjectCollidable::calcCollisionTransform();
}
}
