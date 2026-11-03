#pragma once

#include "game/field/obj/ObjectBreakable.hh"

namespace Kinoko::Field {

class ObjectWoodbox : public ObjectBreakable {
public:
    ObjectWoodbox(const System::MapdataGeoObj &params);
    ~ObjectWoodbox() override;

    /// @addr{0x8077ED7C}
    [[nodiscard]] const char *getKclName() const override;
    void init() override;
    void calc() override;
    void enableCollision() override;
    Kart::Reaction onCollision(Kart::KartObject *, Kart::Reaction kart, Kart::Reaction object,
            EGG::Vector3f &) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &, s16 item, s16 object,
            EGG::Vector3f &) override;
    // Retail ItemDropper returns immediately for a null/global item.
    void onGlobalItemCollision(s16, s16) override {}
    bool shown() const { return m_state == 1 || m_state == 3; }
    u32 state() const { return m_state; }
    u32 breakSerial() const { return m_breakSerial; }
    EGG::Vector3f breakPosition() const { return m_breakPosition; }

protected:
    bool breakBox(s16 reaction);
    void dropItem(u8 owner, const EGG::Vector3f &motion);
    f32 m_initialY = 0.0f;
    f32 m_yVelocity = 0.0f;
    s32 m_respawnTimer = 0;
    u32 m_breakSerial = 0;
    EGG::Vector3f m_breakPosition = EGG::Vector3f::zero;

    void calcCollisionTransform() override;
};

} // namespace Kinoko::Field
