#pragma once

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

class ObjectItemboxPress;

// Retail W_Itemboxline::Block has its own rail and collision, separate from the box.
class ObjectItemboxBlock final : public ObjectCollidable {
public:
    explicit ObjectItemboxBlock(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void init() override;
    void calc() override;
    void start();
    u32 loadFlags() const override { return 1; }
    const char *getResources() const override { return "Block"; }
    const char *getKclName() const override { return "Block"; }
    Kart::Reaction onCollision(Kart::KartObject *, Kart::Reaction kart, Kart::Reaction object,
            EGG::Vector3f &) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &, s16 item, s16 object,
            EGG::Vector3f &) override;
    void onGlobalItemCollision(s16, s16 object) override { react(object); }
    bool shown() const { return m_shown; }
    bool broken() const { return m_broken; }
    u32 breakCount() const { return m_breakCount; }
    EGG::Vector3f breakPosition() const { return m_breakPosition; }
    u32 state() const { return m_state; }
private:
    void react(s16 reaction);
    u32 m_state = 0;
    bool m_shown = false;
    bool m_broken = false;
    u32 m_breakCount = 0;
    EGG::Vector3f m_breakPosition = EGG::Vector3f::zero;
};


/// @brief Object which represents a line of itemboxes, a brick block which may be pressed into an
/// itembox, and a stomper. The stomper is held and managed by @ref ObjectItemboxPress.
class ObjectItemboxLine final : public ObjectCollidable {
public:
    ObjectItemboxLine(const System::MapdataGeoObj &params);
    ~ObjectItemboxLine() override;

    void init() override;
    void calc() override;
    // Retail PAL8076e9b0/8076e9b8: only the child blocks and press own these.
    void createCollision() override {}
    void loadGraphics() override {}

    /// @addr{0x8076E9BC}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

private:
    owning_span<ObjectItemboxPress *> m_press;
    owning_span<ObjectItemboxBlock *> m_blocks;
    u32 m_stompCooldown; ///< Number of frames in between stomps
    u32 m_curPressIdx;
};

} // namespace Kinoko::Field
