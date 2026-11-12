#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
class ObjectFactoryEffect final : public ObjectCollidable {
public:
    explicit ObjectFactoryEffect(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void load() override;
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    void calcCollisionTransform() override {}
    bool steam() const { return m_steam; }
    bool emitting() const { return m_emitting; }
private:
    bool m_steam = false, m_active = false, m_emitting = false;
    s32 m_timer = 0;
};
}
