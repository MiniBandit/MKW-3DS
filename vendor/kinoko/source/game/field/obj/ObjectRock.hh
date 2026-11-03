#pragma once

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

/// @brief The falling rocks on GCN DK Mountain
class ObjectRock : public ObjectCollidable {
public:
    ObjectRock(const System::MapdataGeoObj &params);
    ~ObjectRock() override;

    void init() override;
    void calc() override;

    /// @addr{0x8077037C}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x8077036C}
    [[nodiscard]] const EGG::Vector3f &getCollisionTranslation() const override {
        return m_colTranslate;
    }

    [[nodiscard]] Kart::Reaction onCollision(Kart::KartObject *kartObj,
            Kart::Reaction reactionOnKart, Kart::Reaction reactionOnObj,
            EGG::Vector3f &hitDepth) override;

    bool visible() const { return m_state == State::Tangible; }
    // Retail USA8075d4a4: the shadow follows position, never the rolling body.
    EGG::Matrix34f shadowTransform() const {
        EGG::Matrix34f m = EGG::Matrix34f::ident;
        m.setBase(3, pos());
        return m;
    }
    EGG::Vector3f shadowScale() const { return scale() * 0.8f; }
    u32 breakSerial() const { return m_breakSerial; }
    const EGG::Matrix34f &breakPose() const { return m_breakPose; }

    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) override;
    void onGlobalItemCollision(s16, s16 reactionOnObj) override;

private:
    enum class State {
        Tangible = 0,
        Intangible = 1,
    };

    void calcTangible();
    void calcIntangible();
    void calcTangibleSub();

    void checkSphereFull();
    void breakRock(const EGG::Vector3f &direction = EGG::Vector3f::zero);
    u32 m_breakSerial = 0;
    EGG::Matrix34f m_breakPose = EGG::Matrix34f::ident;

    State m_state;
    f32 m_startYPos;
    EGG::Vector3f m_colTranslate;
    f32 m_angSpd;
    int m_cooldownTimer;
    f32 m_angRad;

    static constexpr f32 INITIAL_ANGULAR_SPEED = 3.0f;
};

} // namespace Kinoko::Field
