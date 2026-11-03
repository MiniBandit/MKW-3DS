#pragma once

#include "game/field/StateManager.hh"
#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

/// @brief Shy guys on DK Summit.
class ObjectHeyho : public ObjectCollidable, public StateManager {
public:
    ObjectHeyho(const System::MapdataGeoObj &params);
    ~ObjectHeyho() override;

    void init() override;
    void calc() override;
    void loadAnims() override;
    void calcCollisionTransform() override;
    Kart::Reaction onCollision(Kart::KartObject *, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) override;
    void onGlobalItemCollision(s16, s16 reactionOnObj) override;

    /// 1 move, 2 jump, 3 jumped (landing).
    [[nodiscard]] s32 currentAnim() const {
        return static_cast<s32>(m_currentAnim);
    }

    /// @addr{0x806D02B4}
    [[nodiscard]] u32 loadFlags() const override {
        return 3;
    }

private:
    enum class Animation {
        Move = 1,
        Jump = 2,
        Jumped = 3,
    };

    void changeAnimation(Animation anim) {
        m_drawMdl->anmMgr()->playAnim(0.0f, 1.0f, static_cast<size_t>(anim));
        m_currentAnim = anim;
    }

    // State methods

    /// @addr{0x806CF4CC}
    void enterMove() {}

    /// @addr{0x806CF714}
    void enterJump() {
        m_spinFrame = 0;
        cueSound(0x2b6, SoundCall::Start); // SE_CN_BDX_HEYHO_JUMP
    }

    void calcMove();
    void calcJump();

    void calcStateTransition();
    void calcMotion();
    void calcInterp();
    void beginHit();
    void calcHit();
    bool m_hit = false;
    u16 m_hitFrame = 0;
    f32 m_hitLastAngle = 0.0f;

    const s32 m_color;
    f32 m_apex;
    EGG::Vector3f m_midpoint;
    EGG::Vector3f m_transformOffset;
    f32 m_currentVel;
    f32 m_accel;
    f32 m_maxVelSq;
    EGG::Vector3f m_up;
    EGG::Vector3f m_forward;
    EGG::Vector3f m_floorNrm;
    bool m_floorCollision;
    Animation m_currentAnim;
    bool m_freeFall;
    f32 m_launchVel;
    s16 m_spinFrame;

    static constexpr std::array<StateManagerEntry, 2> STATE_ENTRIES = {{
            {StateEntry<ObjectHeyho, &ObjectHeyho::enterMove, &ObjectHeyho::calcMove>(0)},
            {StateEntry<ObjectHeyho, &ObjectHeyho::enterJump, &ObjectHeyho::calcJump>(1)},
    }};

    static constexpr EGG::Vector3f COLLISION_OFFSET = EGG::Vector3f(0.0f, 10.0f, 0.0f);
    static constexpr f32 COLLISION_RADIUS = 100.0f;
};

} // namespace Kinoko::Field
