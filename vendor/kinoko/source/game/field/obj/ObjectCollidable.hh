#pragma once

#include "game/field/obj/ObjectBase.hh"

#include "game/field/ObjectCollisionBase.hh"

namespace Kinoko {

namespace Kart {

class KartObject;
enum class Reaction;

} // namespace Kart

namespace Field {

class ObjectCollidable : public ObjectBase {
public:
    ObjectCollidable(const System::MapdataGeoObj &params);
    ObjectCollidable(const char *name, const EGG::Vector3f &pos, const EGG::Vector3f &rot,
            const EGG::Vector3f &scale);
    ~ObjectCollidable() override;

    void load() override;
    void calcCollisionTransform() override;
    [[nodiscard]] f32 getCollisionRadius() const override;

    virtual void loadAABB(f32 maxSpeed);
    virtual void loadAABB(f32 radius, f32 maxSpeed);
    virtual void processKartReactions(Kart::KartObject *kartObj, Kart::Reaction &reactionOnKart,
            Kart::Reaction &reactionOnObj);

    /// @addr{0x8068179C}
    virtual Kart::Reaction onCollision(Kart::KartObject * /*kartObj*/,
            Kart::Reaction reactionOnKart, Kart::Reaction /*reactionOnObj*/,
            EGG::Vector3f & /*hitDepth*/) {
        return reactionOnKart;
    }

    virtual void onWallCollision(Kart::KartObject *, const EGG::Vector3f &);
    // GeoHitTableItem / GeoHitTableItemObj are independent of the kart tables.
    // Values are item response0/1/2 and object response0/3/4/5/6, not kart actions.
    virtual s16 onItemCollision(const EGG::Vector3f & /*position*/,
            const EGG::Vector3f & /*velocity*/, s16 reactionOnItem,
            s16 /*reactionOnObj*/, EGG::Vector3f & /*hitDepth*/) { return reactionOnItem; }
    virtual void onObjectCollision(Kart::KartObject *);
    virtual bool checkCollision(ObjectCollisionBase *lhs, EGG::Vector3f &dist);

    /// @addr{0x8068173C}
    virtual const EGG::Vector3f &getCollisionTranslation() const {
        return EGG::Vector3f::zero;
    }

    /// @addr{0x80573518}
    [[nodiscard]] virtual ObjectCollisionBase *collision() const {
        return m_collision;
    }

protected:
    void createCollision() override;

    /// @addr{0x806816B8}
    [[nodiscard]] virtual const EGG::Vector3f &collisionCenter() const {
        return EGG::Vector3f::zero;
    }

    void registerManagedObject();

    ObjectCollisionBase *m_collision;
};

} // namespace Field

} // namespace Kinoko
