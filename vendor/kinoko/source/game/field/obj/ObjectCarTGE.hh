/// TODO: Also need 806d2908

#pragma once

#include <array>

#include "game/field/StateManager.hh"
#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

class ObjectHighwayManager;

class ObjectCarTGE : public ObjectCollidable, public StateManager {
public:
    enum class CarType {
        Normal = 0,
        Truck = 1,
        BombCar = 2, ///< Never built in time trials (ObjectDirector::createObjects)
    };

    ObjectCarTGE(const System::MapdataGeoObj &params);
    ~ObjectCarTGE() override;

    void init() override;
    void calc() override;

    /// @addr{0x806DA7AC}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x806D68B0}
    [[nodiscard]] const char *getResources() const override {
        return m_carName;
    }

    /// @addr{0x806DA7A4}
    [[nodiscard]] const char *getKclName() const override {
        return m_mdlName;
    }

    void createCollision() override;
    void calcCollisionTransform() override;
    [[nodiscard]] f32 getCollisionRadius() const override;
    Kart::Reaction onCollision(Kart::KartObject *kartObj, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;
    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) override;
    void onGlobalItemCollision(s16 reactionOnItem, s16 reactionOnObj) override;
    void processItemReactions(s16 type, bool global, s16 &item, s16 &object) override;
    bool checkCollision(ObjectCollisionBase *lhs, EGG::Vector3f &dist) override;
    [[nodiscard]] const EGG::Vector3f &collisionCenter() const override;

    void setHighwayManager(const ObjectHighwayManager *highwayMgr) {
        m_highwayMgr = highwayMgr;
    }

    /// @addr{0x806D9A04}
    void reset() {
        m_squashed.fill(false);
    }

    /// Retail +0x1B1: this car squashed the player this frame (one flag per player).
    [[nodiscard]] bool squashed(size_t playerIdx) const {
        return playerIdx < m_squashed.size() && m_squashed[playerIdx];
    }

    [[nodiscard]] CarType carType() const {
        return m_carType;
    }

    /// Bomb car: exploded and not yet back on the road (retail +0x1BD).
    [[nodiscard]] bool exploded() const {
        return m_exploded;
    }

    /// Bomb car: the blast is on (retail +0x1D9), its collision grown to the blast radius.
    [[nodiscard]] bool blastActive() const {
        return m_blastActive;
    }

    /// Bomb car: its models are shown (retail +0x1D8; hidden from the blast until it regrows).
    [[nodiscard]] bool bombShown() const {
        return m_shown;
    }

    /// Bomb car: frames since it exploded (retail +0x1C0).
    [[nodiscard]] u32 explodeFrame() const {
        return m_explodeFrame;
    }

    /// Bomb car: the blast radius (retail +0x21C).
    [[nodiscard]] f32 blastRadius() const {
        return m_blastRadius;
    }

private:
    static constexpr f32 TOLL_BOOTH_ACCEL = 200.0f;

    void enterStateStub();
    void calcStateStub();
    void calcState1();
    void calcState2();

    void calcPos();
    void startHit();
    void calcHit();
    bool m_hitLanded = false;
    bool m_hitRecovered = false;
    bool m_hitReverse = false;
    bool m_hitByBomb = false;
    f32 m_hitStartY = 0.0f;
    f32 m_hitLandFrame = 0.0f;
    f32 m_hitRotationSpeed = 0.0f;
    void calcBombPos();
    void explode();
    void calcExploded();
    void calcRegrow();

    const ObjectHighwayManager *m_highwayMgr;
    ObjectCollisionBase *m_auxCollision;
    f32 m_highwayVel; ///< Speed while on the highway
    f32 m_localVel;   ///< Speed while off the highway
    char m_carName[32];
    char m_mdlName[32];
    CarType m_carType; ///< Car, truck, or bomb car
    ObjectId m_dummyId;
    EGG::Vector3f m_scaledTangentDir;
    f32 m_currSpeed;
    EGG::Vector3f m_up;
    EGG::Vector3f m_tangent;
    std::array<bool, 12> m_squashed;
    bool m_hasAuxCollision;
    f32 m_hitAngle;

    // Bomb cars (retail car type 2).
    bool m_exploded;        ///< +0x1BD
    bool m_blastActive;     ///< +0x1D9
    bool m_shown;           ///< +0x1D8
    u32 m_explodeFrame;     ///< +0x1C0
    u32 m_regrownFrame;     ///< +0x1D4: frame the car reached full size (0: still growing)
    f32 m_regrowHeight;     ///< +0x1CC
    f32 m_regrowWidth;      ///< +0x1D0
    f32 m_baseScale;        ///< +0x15C
    f32 m_blastRadius;      ///< +0x21C
    u32 m_blastPhase;       ///< +0x228: 1 after the blast's first 32 frames

    static constexpr std::array<StateManagerEntry, 3> STATE_ENTRIES = {{
            {StateEntry<ObjectCarTGE, &ObjectCarTGE::enterStateStub, &ObjectCarTGE::calcStateStub>(
                    0)},
            {StateEntry<ObjectCarTGE, &ObjectCarTGE::enterStateStub, &ObjectCarTGE::calcState1>(1)},
            {StateEntry<ObjectCarTGE, &ObjectCarTGE::enterStateStub, &ObjectCarTGE::calcState2>(2)},
    }};
};

} // namespace Kinoko::Field
