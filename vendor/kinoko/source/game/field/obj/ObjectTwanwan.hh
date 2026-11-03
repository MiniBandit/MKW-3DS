#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
// Chain Chomp Wheel's rolling giant (RMCU80885f80), distinct from Hwanwan.
class ObjectTwanwan final : public ObjectCollidable {
public:
    explicit ObjectTwanwan(const System::MapdataGeoObj& params);
    void init() override;
    void calc() override;
    u32 loadFlags() const override {return 1;}
    void calcCollisionTransform() override;
    Kart::Reaction onCollision(Kart::KartObject*,Kart::Reaction,Kart::Reaction,EGG::Vector3f&) override;
    s16 onItemCollision(const EGG::Vector3f&,const EGG::Vector3f&,s16,s16,EGG::Vector3f&) override;
    void onGlobalItemCollision(s16,s16) override;
    bool fullSphere() const {return m_fullSphere;}
    bool grounded() const {return m_grounded;}
    f32 hopHeight() const {return m_hopHeight;}
private:
    void step(u32 timer);
    void move();
    void publish();
    void hit(s16 reaction);
    EGG::Vector3f m_work,m_velocity=EGG::Vector3f::zero,m_target;
    EGG::Vector3f m_forward,m_right,m_turnAxis=EGG::Vector3f::ey;
    f32 m_radius,m_speed,m_jumpWidth,m_pitch=0.f,m_yaw=0.f;
    f32 m_hopVelocity=0.f,m_hopHeight=0.f;
    u32 m_jumpPeriod,m_moveStart,m_fallStart,m_nextTimer=0,m_age=0,m_hitAge=65;
    bool m_fullSphere=true,m_grounded=false;
};
}
