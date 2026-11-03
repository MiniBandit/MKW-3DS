#pragma once

#include "game/field/StateManager.hh"
#include "game/field/obj/ObjectCollidable.hh"

#include "game/kart/KartCollide.hh"
#include "game/kart/KartObject.hh"

namespace Kinoko::Field {

class ObjectHwanwan : public ObjectCollidable, public StateManager {
    friend class ObjectHwanwanManager;

public:
    ObjectHwanwan(const System::MapdataGeoObj &params);
    ~ObjectHwanwan() override;

    void init() override;
    void calc() override;

    /// @addr{0x806EC7B8
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x806EC7AC}
    [[nodiscard]] const char *getKclName() const override {
        return "wanwan";
    }

    /// @addr{0x806EC7A8}
    void loadRail() override {}

    Kart::Reaction onCollision(Kart::KartObject *, Kart::Reaction,
            Kart::Reaction, EGG::Vector3f &) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) override;
    void onGlobalItemCollision(s16, s16 reactionOnObj) override;
    const EGG::Matrix34f &modelTransform() const { return m_modelTransform; }
    f32 animationFrame() const { return m_animationFrame; }

private:
    void enterState0() { m_chompFrame = 10; }
    void enterState1() {}
    void enterState2();
    void calcState0();
    void calcState1();
    void calcState2();
    void chomp();
    void updateModelTransform();

    void checkFloorCollision();
    void calcUp();

    const EGG::Vector3f m_initPos;
    EGG::Vector3f m_workPos;
    EGG::Vector3f m_extVel;
    EGG::Vector3f m_bounceVel;
    EGG::Vector3f m_tangent;
    EGG::Vector3f m_up;
    EGG::Vector3f m_targetUp;
    bool m_touchingGround;
    u32 m_chompFrame = 0; ///< Presentation only: retail +0x3fc.
    EGG::Matrix34f m_modelTransform;
    f32 m_animationFrame = 0.0f;
    f32 m_hitPitch = 0.0f;
    f32 m_hitYaw = 0.0f;
    f32 m_pitchRate = 0.0f;
    f32 m_pitchAcceleration = 0.0f;
    f32 m_yawRate = 0.0f;
    s32 m_hitContactFrame = -1;
    u16 m_floorSound = 0; // Retail KCL attribute bits 8..10, not the surface variant.
    f32 m_initJumpVel;
    f32 m_targetY;

    static constexpr f32 DIAMETER = 400.0f;
    static constexpr EGG::Vector3f GRAVITY = EGG::Vector3f(0.0f, 2.5f, 0.0f);

    static constexpr std::array<StateManagerEntry, 3> STATE_ENTRIES = {{
            {StateEntry<ObjectHwanwan, &ObjectHwanwan::enterState0, &ObjectHwanwan::calcState0>(0)},
            {StateEntry<ObjectHwanwan, &ObjectHwanwan::enterState1, &ObjectHwanwan::calcState1>(1)},
            {StateEntry<ObjectHwanwan, &ObjectHwanwan::enterState2, &ObjectHwanwan::calcState2>(2)},
    }};
};

class ObjectHwanwanManager : public ObjectCollidable {
public:
    ObjectHwanwanManager(const System::MapdataGeoObj &params);
    ~ObjectHwanwanManager() override;

    void init() override;
    void calc() override;

    /// @addr{0x806C69B8}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x806C69B4}
    void loadGraphics() override {}

    /// @addr{0x806C69B0}
    void createCollision() override {}

private:
    void calcState();

    ObjectHwanwan *m_hwanwan;
};

} // namespace Kinoko::Field
