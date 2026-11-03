#include "ObjectWoodbox.hh"
#include "game/field/CollisionDirector.hh"
#include "game/system/RaceConfig.hh"
#include "game/system/RaceManager.hh"
#include "game/item/ItemDirector.hh"
#include "game/field/ObjectDirector.hh"
#include "game/kart/KartObject.hh"

namespace Kinoko::Field {

/// @addr{0x8077E5E4}
ObjectWoodbox::ObjectWoodbox(const System::MapdataGeoObj &params) : ObjectBreakable(params) {}

/// @addr{0x8077E620}
ObjectWoodbox::~ObjectWoodbox() = default;

namespace {
bool specialWoodboxMode() {
    const auto mode = static_cast<unsigned>(System::RaceConfig::Instance()->raceScenario().gameMode);
    return mode >= 7 || mode == 2 || mode == 5;
}
}

const char *ObjectWoodbox::getKclName() const {
    return specialWoodboxMode() ? "ironbox" : "woodbox";
}

void ObjectWoodbox::init() {
    m_initialY = pos().y;
    m_yVelocity = 0.0f;
    m_respawnTimer = 0;
    m_breakSerial = 0;
    enableCollision();
}

void ObjectWoodbox::enableCollision() {
    m_state = 1;
    if (m_collision) ObjectBase::enableCollision();
}

bool ObjectWoodbox::breakBox(s16 reaction) {
    if (reaction != 3 && (reaction != 5 || specialWoodboxMode())) return false;
    if (!shown()) return false;
    m_breakPosition = pos();
    ++m_breakSerial;
    m_state = 2;
    if (m_collision) disableCollision();
    // USA80768bf0: zero setting3 overrides the timer with the original 300.
    m_respawnTimer = m_mapObj->setting(3) == 0 ? 300 : 0;
    cueSound(0x1e0, SoundCall::Start);
    return true;
}

// Retail ItemDropper::DropItem USA8077b2bc: chance is sampled even when items are disabled.
void ObjectWoodbox::dropItem(u8 owner, const EGG::Vector3f &motion) {
    auto &random = System::RaceManager::Instance()->random();
    const float chance = random.getF32(100.0f);
    if (chance >= static_cast<s16>(m_mapObj->setting(2)) ||
            System::RaceConfig::Instance()->raceScenario().itemMode == 3) return;
    auto velocity = motion * 2.0f;
    velocity.y = 2.0f + random.getF32(1.0f);
    EGG::Matrix34f rotation;
    rotation.makeR({rot().x, 0.0f, 0.0f});
    velocity = rotation.multVector(velocity);
    Item::ItemDirector::CreateCourseDrop(m_mapObj->setting(1),
            pos() + EGG::Vector3f::ey * 300.0f, velocity, owner);
}

Kart::Reaction ObjectWoodbox::onCollision(Kart::KartObject *vehicle, Kart::Reaction kart,
        Kart::Reaction object, EGG::Vector3f &) {
    if (breakBox(static_cast<s16>(object)) && vehicle)
        dropItem(vehicle->param()->playerIdx(), vehicle->velocity());
    return kart;
}

s16 ObjectWoodbox::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 item, s16 object, EGG::Vector3f &depth) {
    if (breakBox(object)) dropItem(ObjectDirector::Instance()->itemOwner(), -depth);
    return item;
}

void ObjectWoodbox::calc() {
    if (m_state == 2) {
        if (m_respawnTimer <= 0 || --m_respawnTimer != 0) return;
        enableCollision();
        m_state = 3;
        auto p = pos();p.y = m_initialY + 5000.0f;setPos(p);
        m_yVelocity = 0.0f;
        return;
    }
    if (m_state != 3) return;
    m_yVelocity -= 2.0f;
    addPos(EGG::Vector3f::ey * m_yVelocity);
    CollisionInfo info;
    KCLTypeMask mask;
    if (CollisionDirector::Instance()->checkSphereFull(100.0f,
                pos() + EGG::Vector3f::ey * 100.0f, EGG::Vector3f::inf,
                KCLTypeMask(0x20e80fff), &info, &mask, 0)) {
        m_yVelocity *= -0.1f;
        addPos(info.tangentOff);
        if (EGG::Mathf::abs(m_yVelocity) < 1.0f) m_state = 1;
        cueSound(0x1e1, SoundCall::StartNew);
    }
}

/// @addr{0x8077EBB8}
void ObjectWoodbox::calcCollisionTransform() {
    constexpr f32 HALF_SIZE = 100.0f;
    constexpr EGG::Vector3f POS_OFFSET = EGG::Vector3f(0.0f, HALF_SIZE, 0.0f);

    if (!m_collision) {
        return;
    }

    calcTransform();
    EGG::Matrix34f mat = transform();
    mat.setBase(3, pos() + POS_OFFSET);
    m_collision->transform(mat, scale(), getCollisionTranslation());
}

} // namespace Kinoko::Field
