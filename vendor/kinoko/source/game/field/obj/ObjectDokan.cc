#include "ObjectDokan.hh"

#include "game/field/CollisionDirector.hh"

#include "game/kart/KartCollide.hh"

namespace Kinoko::Field {

/// @addr{0x807787F0}
ObjectDokan::ObjectDokan(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}

/// @addr{0x80778FEC}
ObjectDokan::~ObjectDokan() = default;

/// @addr{0x80778830}
void ObjectDokan::init() {
    m_b0 = false;
    m_animationFrame = -1.0f;
    m_animationSound = {};
}

/// @addr{0x807788C8}
void ObjectDokan::calc() {
    constexpr f32 ACCEL = 2.0f;

    if (id() == ObjectId::PuchiPakkun) {
        ++m_animationFrame;
        m_animationSound.update(*this, "puchi_pakkun", "puchi_pakkun", m_animationFrame,
                mkw::ObjectAnimationSound::frames("puchi_pakkun", "puchi_pakkun"));
    }

    if (!m_b0) {
        return;
    }

    m_velocity.y -= ACCEL;
    addPos(m_velocity);

    calcFloor();
}

/// @addr{0x80778D50}
void ObjectDokan::calcCollisionTransform() {
    if (m_id == ObjectId::DokanSFC) {
        ObjectCollidable::calcCollisionTransform();
    } else {
        // rMR piranhas
        calcTransform();
        EGG::Matrix34f mat = transform();
        mat.setBase(3, mat.translation() + EGG::Vector3f::ey * 300.0f);
        m_collision->transform(mat, scale(), getCollisionTranslation());
    }
}

/// @addr{0x80778C0C}
Kart::Reaction ObjectDokan::onCollision(Kart::KartObject * /*kartObj*/,
        Kart::Reaction reactionOnKart, Kart::Reaction reactionOnObj, EGG::Vector3f & /*hitDepth*/) {
    launch(static_cast<s16>(reactionOnObj));
    return reactionOnKart;
}

// Retail USA8077d1e4: real and null/global items share the same 3/5 gate.
s16 ObjectDokan::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    launch(reactionOnObj);
    return reactionOnItem;
}

void ObjectDokan::onGlobalItemCollision(s16, s16 reactionOnObj) {
    launch(reactionOnObj);
}

void ObjectDokan::launch(s16 reactionOnObj) {
    if ((reactionOnObj == 3 || reactionOnObj == 5) && !m_b0) {
        m_b0 = true;
        m_velocity = 100.0f * EGG::Vector3f::ey;
    }
}

/// @addr{0x807789BC}
void ObjectDokan::calcFloor() {
    constexpr f32 PIPE_RADIUS = 100.0f;
    constexpr f32 PIPE_SQRT_RADIUS = 10.0f;
    constexpr f32 ACCELERATION = 0.2f;

    CollisionInfo colInfo;
    EGG::Vector3f colPos = pos();
    colPos.y += PIPE_RADIUS;
    KCLTypeMask typeMask;

    if (!CollisionDirector::Instance()->checkSphereFull(PIPE_RADIUS, colPos, EGG::Vector3f::inf,
                KCL_TYPE_64EBDFFF, &colInfo, &typeMask, 0)) {
        return;
    }

    addPos(EGG::Vector3f(0.0f, colInfo.tangentOff.y, 0.0f));

    if (typeMask & KCL_TYPE_FLOOR) {
        m_velocity.y *= -ACCELERATION;
        if (m_velocity.length() < ACCELERATION * PIPE_SQRT_RADIUS) {
            m_b0 = false;
        }

        // Retail 8077cedc: a thud on each bounce, by object.
        if (id() == ObjectId::PuchiPakkun) {
            cueSound(0x278, SoundCall::StartNew); // SE_CC_PAKKUN_GND
        } else if (id() == ObjectId::DokanSFC) {
            cueSound(0x1d7, SoundCall::StartNew); // SE_CC_DOKAN_GND
        }
    }
}

} // namespace Kinoko::Field
