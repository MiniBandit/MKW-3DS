#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
// TruckChimSmk and TruckChimSmkW share authored smoke resources, but not
// their collision timing or per-player contact callbacks.
class ObjectChimney final : public ObjectCollidable {
public:
    explicit ObjectChimney(const System::MapdataGeoObj &);
    u32 loadFlags() const override { return 1; }
    void init() override;
    void calc() override;
    void loadGraphics() override;
    void calcCollisionTransform() override;
    Kart::Reaction onCollision(Kart::KartObject *, Kart::Reaction, Kart::Reaction,
            EGG::Vector3f &) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &, s16 item, s16,
            EGG::Vector3f &depth) override { depth = EGG::Vector3f::zero; return item; }
    bool wide() const { return m_wide; }
    unsigned variant() const { return m_variant; }
    bool modelVisible() const { return m_modelVisible; }
    bool emitting() const { return m_emitting; }
    bool pauseTrack() const { return m_pauseTrack; }
    u32 hitSerial(unsigned player) const { return m_hits[player]; }
    u32 elapsed() const { return m_tick; }
    u32 delay() const { return m_delay; }
    void setEffectSuppressed(bool value) { m_effectSuppressed = value; }
    bool effectSuppressed() const { return m_effectSuppressed; }
protected:
    void createCollision() override;
    const EGG::Vector3f &collisionCenter() const override { return m_center; }
private:
    const bool m_wide;
    const unsigned m_variant;
    const u32 m_delay;
    EGG::Vector3f m_center = EGG::Vector3f::zero;
    u32 m_tick = 0, m_hitTicks = 10;
    bool m_touched = false, m_previous = false;
    bool m_modelVisible = false, m_emitting = false, m_pauseTrack = false;
    bool m_effectSuppressed = true; // Original ClipInfo initial flags3.
    std::array<u32,12> m_hits{};
};
}
