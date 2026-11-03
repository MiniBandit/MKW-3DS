#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
// Retail EffectSpawner contact edge plus LeafEffect's two local-player roots.
class ObjectLeafEffect final : public ObjectCollidable {
public:
    explicit ObjectLeafEffect(const System::MapdataGeoObj &params);
    u32 loadFlags() const override { return 1; }
    void init() override;
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject *, Kart::Reaction, Kart::Reaction,
            EGG::Vector3f &) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &, s16 item, s16,
            EGG::Vector3f &depth) override { depth = EGG::Vector3f::zero; return item; }
    [[nodiscard]] u32 effectSerial(unsigned kind) const { return m_serial[kind]; }
    [[nodiscard]] u8 effectPlayer(unsigned kind) const { return m_player[kind]; }
    void setEffectSuppressed(bool value) { m_effectSuppressed = value; }
private:
    bool m_effectSuppressed = true; // ClipInfo::Init starts every screen with flags3.
    bool m_touched = false, m_previous = false;
    // Current menus have one local screen. CPU contacts update the shared edge
    // but never replace the local player's following effect/handle.
    std::array<u32, 2> m_serial{};
    std::array<u8, 2> m_player{};
};
}
