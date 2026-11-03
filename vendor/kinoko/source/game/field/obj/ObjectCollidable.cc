#include "ObjectCollidable.hh"

#include "game/field/ObjectCollisionBox.hh"
#include "game/field/ObjectCollisionCylinder.hh"
#include "game/field/ObjectCollisionSphere.hh"
#include "game/field/ObjectDirector.hh"

#include "game/kart/KartObject.hh"
#include "game/kart/KartMove.hh"
#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {

/// @addr{0x8081EFEC}
ObjectCollidable::ObjectCollidable(const System::MapdataGeoObj &params)
    : ObjectBase(params), m_collision(nullptr) {}

/// @addr{0x8081F064}
ObjectCollidable::ObjectCollidable(const char *name, const EGG::Vector3f &pos,
        const EGG::Vector3f &rot, const EGG::Vector3f &scale)
    : ObjectBase(name, pos, rot, scale), m_collision(nullptr) {}

/// @addr{0x8067E384}
ObjectCollidable::~ObjectCollidable() {
    EGG::egg_delete(m_collision);
}

/// @addr{0x8081F0A0}
void ObjectCollidable::load() {
    loadGraphics();
    loadAnims();
    createCollision();

    if (m_collision) {
        loadAABB(0.0f);
    }

    loadRail();

    ObjectDirector::Instance()->addObject(this);
}

/// @addr{0x8081F7C8}
void ObjectCollidable::calcCollisionTransform() {
    calcTransform();
    m_collision->transform(transform(), scale(), getCollisionTranslation());
}

/// @addr{0x806815A0}
/// @brief Finds the radius that fits fully in a BoxColUnit.
/// @details We refer to the collision parameters as a box due to its use of axes.
/// This does not imply that all collidable objects are boxes!
f32 ObjectCollidable::getCollisionRadius() const {
    const auto &flowTable = ObjectDirector::Instance()->flowTable();
    const auto *collisionSet = flowTable.set(flowTable.slot(id()));
    // mkw-3ds: objects without an ObjFlow collision set (queried by the CPU AI
    // obstacle probe on Moonview Highway; retail reads slot -1) use the ObjectBase
    // default instead of dereferencing null. Every object Kinoko itself measures has a set.
    if (!collisionSet) {
        return ObjectBase::getCollisionRadius();
    }

    f32 zRadius = scale().z * static_cast<f32>(parse<s16>(collisionSet->params.box.z));
    f32 xRadius = scale().x * static_cast<f32>(parse<s16>(collisionSet->params.box.x));

    return std::max(xRadius, zRadius);
}

/// @addr{0x806816D8}
void ObjectCollidable::loadAABB(f32 maxSpeed) {
    loadAABB(getCollisionRadius(), maxSpeed);
}

/// @addr{0x8081F180}
void ObjectCollidable::loadAABB(f32 radius, f32 maxSpeed) {
    auto *boxColMgr = BoxColManager::Instance();
    const EGG::Vector3f &pos = getPosition();
    bool alwaysRecalc = loadFlags() & 0x5;
    m_boxColUnit = boxColMgr->insertObject(radius, maxSpeed, &pos, alwaysRecalc, this);
}

/// @addr{0x8081F66C}
void ObjectCollidable::processKartReactions(Kart::KartObject *kartObj,
        Kart::Reaction &reactionOnKart, Kart::Reaction &reactionOnObj) {
    // Process the reaction on kart
    if (kartObj->speedRatioCapped() < 0.5f) {
        if (reactionOnKart == Kart::Reaction::SpinSomeSpeed) {
            reactionOnKart = Kart::Reaction::WallAllSpeed;
        } else if (reactionOnKart == Kart::Reaction::SpinHitSomeSpeed) {
            reactionOnKart = Kart::Reaction::None;
        }
    } else {
        if (reactionOnKart == Kart::Reaction::SpinHitSomeSpeed) {
            reactionOnKart = Kart::Reaction::SpinSomeSpeed;
        }
    }

    // USA8080bbd0..bc4c: ordinary offline modes convert5/6 to3/4;
    // TT/Ghost/special modes clear3/4 instead. The old unconditional clear
    // discarded the native power-up destruction response on ordinary races.
    const auto mode = static_cast<unsigned>(System::RaceConfig::Instance()->raceScenario().gameMode);
    if (mode < 7 && mode != 2 && mode != 5) {
        if (reactionOnObj == Kart::Reaction::UNK_5) reactionOnObj = Kart::Reaction::UNK_3;
        else if (reactionOnObj == static_cast<Kart::Reaction>(6)) reactionOnObj = Kart::Reaction::UNK_4;
    } else if (reactionOnObj == Kart::Reaction::UNK_3 || reactionOnObj == Kart::Reaction::UNK_4) {
        reactionOnObj = Kart::Reaction::None;
    }
}

void ObjectCollidable::onObjectCollision(Kart::KartObject *kart) {
    // Original OnNormalCollision -> KartActor::OnObjectNormalCollision
    // (8080ba34 ->80703164). A zero damage response is still a real contact.
    if (kart->status().onBit(Kart::eStatus::Star, Kart::eStatus::Mega)) {
        kart->move()->cuePowerObjectContact(false);
        kart->move()->cueObjectContact(kart->pos());
    }
}

void ObjectCollidable::onWallCollision(Kart::KartObject *kart, const EGG::Vector3f &) {
    // USA807033e8: only these two wall-object families request STAR_HIT.
    if (kart->status().onBit(Kart::eStatus::Star, Kart::eStatus::Mega) &&
            (static_cast<u16>(id()) == 0xdb || static_cast<u16>(id()) == 0x191))
        kart->move()->cuePowerObjectContact(true);
}

/// @addr{0x80681748}
bool ObjectCollidable::checkCollision(ObjectCollisionBase *lhs, EGG::Vector3f &dist) {
    return lhs->check(*collision(), dist);
}

/// @addr{0x8081F224}
void ObjectCollidable::createCollision() {
    const auto &flowTable = ObjectDirector::Instance()->flowTable();
    const auto *collisionSet = flowTable.set(flowTable.slot(id()));

    if (!collisionSet) {
        PANIC("Invalid object ID when creating primitive collision! ID: %zu",
                static_cast<size_t>(id()));
    }

    // Retail USA8080b768..b784 skips primitive construction for mode 0.
    // PenguinL uses this valid non-colliding mode; it still loads its model
    // and rail and remains in the update list.
    if (parse<s16>(collisionSet->mode) == 0) return;

    switch (static_cast<CollisionMode>(parse<s16>(collisionSet->mode))) {
    case CollisionMode::Sphere:
        m_collision = EGG::egg_new<ObjectCollisionSphere>(
                parse<s16>(collisionSet->params.sphere.radius), collisionCenter());
        break;
    case CollisionMode::Cylinder:
        m_collision = EGG::egg_new<ObjectCollisionCylinder>(
                parse<s16>(collisionSet->params.cylinder.radius),
                parse<s16>(collisionSet->params.cylinder.height), collisionCenter());
        break;
    case CollisionMode::Box:
        m_collision = EGG::egg_new<ObjectCollisionBox>(parse<s16>(collisionSet->params.box.x),
                parse<s16>(collisionSet->params.box.y), parse<s16>(collisionSet->params.box.z),
                collisionCenter());
        break;
    default:
        PANIC("Invalid collision mode when creating primitive collision! ID: %zu; Mode: %d",
                static_cast<size_t>(id()), parse<s16>(collisionSet->mode));
        break;
    }
}

/// @addr{0x8081F170}
void ObjectCollidable::registerManagedObject() {
    ObjectDirector::Instance()->addManagedObject(this);
}

} // namespace Kinoko::Field
