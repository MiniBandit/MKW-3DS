#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
// Retail Ksticketc (0x164) and K_sticklift00 (0x148), USA80814344.
// No CHR: the whole gate rotates, with a stationary trigger at local (675,0,0).
class ObjectTollGate final : public ObjectCollidable {
public:
    explicit ObjectTollGate(const System::MapdataGeoObj &params);
    void init() override;
    void calc() override;
    void createCollision() override;
    void calcCollisionTransform() override;
    u32 loadFlags() const override { return 1; }
    f32 getCollisionRadius() const override { return ObjectCollidable::getCollisionRadius()*2.0f; }
    Kart::Reaction onCollision(Kart::KartObject *,Kart::Reaction kart,Kart::Reaction,EGG::Vector3f &) override {
        open(); return kart;
    }
    void open() { m_state=1; }
    unsigned angle() const { return m_angle; }
private:
    EGG::Vector3f m_initialRot;
    EGG::Matrix34f m_initialMatrix;
    unsigned m_state=0,m_angle=0;
    void applyAngle();
};
}
