#pragma once

#include "game/field/obj/ObjectCollidable.hh"

#include <array>

namespace Kinoko::Field {

/// @brief Rainbow Road's star rings on the cannon flight. Retail builds them with the cannon
/// class (ctor 0x806BFB14, type 3; DonkyCannon_wii, DonkyCannonGC and tree_cannon are types
/// 0-2 and stay ObjectNoImpl here). No collision (ObjFlow mode 0). While its idle clip plays,
/// a racer entering the cannon (CannonStart) plays the ring's shot clip and its sound.
/// Presentation only: nothing here is read by the simulation.
class ObjectStarRing final : public ObjectCollidable {
public:
    ObjectStarRing(const System::MapdataGeoObj &params);
    ~ObjectStarRing() override;

    void init() override;
    void calc() override;

    /// @addr{0x806C04FC}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    void loadAnims() override;

    /// @brief ObjFlow mode 0: the base 0x8081F224 creates no collision.
    void createCollision() override {}
    void calcCollisionTransform() override {}

    /// @brief SE_CN_RBW_STAR_RING (0x2F9) calls of the last calc, each with its count parameter
    /// (sequence variable 0 = 2 * count). Retail plays the object's own call only if no kart call
    /// started (+0xEE), which the audio thread decides.
    struct SoundCalls {
        u16 karts = 0;                  ///< Local racers whose KartActor vt+0xE0 is called
        std::array<u8, 12> kartCount{}; ///< Their count parameter
        bool ring = false;              ///< Object::StartSoundLimited (fn_1_310148)
        u8 ringCount = 0;
    };

    [[nodiscard]] const SoundCalls &soundCalls() const {
        return m_soundCalls;
    }

    /// @brief The CHR clip playing: StarRing_shot (true) or StarRing_wait (false).
    [[nodiscard]] bool shot() const {
        return m_shot;
    }

    /// @brief Frame of the playing CHR clip (advanced at rate 1 after each calc).
    [[nodiscard]] f32 animFrame() const {
        return m_frame;
    }

private:
    u8 m_index;         ///< +0xE8: the cannon class's construction order; only 0 plays its shot
    f32 m_shotLength;   ///< +0xE4: StarRing_shot's frame count
    bool m_shot;        ///< The active CHR clip (anim list index 0 shot, 1 wait)
    f32 m_frame;
    u16 m_window;       ///< +0xEA
    u16 m_count;        ///< +0xEC
    SoundCalls m_soundCalls;

    static u8 s_instanceCount; ///< 0x809C2248, reset by every destructor
};

} // namespace Kinoko::Field
