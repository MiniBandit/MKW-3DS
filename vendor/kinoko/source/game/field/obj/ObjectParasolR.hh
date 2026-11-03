#pragma once

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

class ObjectParasolR : public ObjectCollidable {
public:
    ObjectParasolR(const System::MapdataGeoObj &params);
    ~ObjectParasolR() override;
    void init() override;
    void calc() override;
    void loadAnims() override;
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &hitDepth) override;
    void onGlobalItemCollision(s16 reactionOnItem, s16 reactionOnObj) override;
    [[nodiscard]] bool opened() const { return m_open; }
    [[nodiscard]] f32 animationFrame() const { return m_frame; }

    /// @addr{0x80779EF4}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }
private:
    EGG::Vector3f m_initialPos;
    f32 m_frame = 20.0f, m_velocityY = 0.0f;
    s32 m_lastPlayer = -1, m_bounces = 0;
    bool m_open = false, m_armed = true, m_airborne = false;
    s32 nearbyPlayer() const;
    void toggle(s32 player);
    void jump();
    void react(s16 reactionOnObj, s32 player);
};

} // namespace Kinoko::Field
