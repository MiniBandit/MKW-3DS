#include "ObjectPakkunF.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {
s32 ObjectPakkunF::s_recentPlayer = -1;

/// @addr{0x807743A4}
ObjectPakkunF::ObjectPakkunF(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_attackFrames(0), m_currAttackFrame(0),
      m_waitDuration(static_cast<s32>(m_mapObj->setting(0))) {}

/// @addr{0x807754FC}
ObjectPakkunF::~ObjectPakkunF() = default;

/// @addr{0x807743E4}
void ObjectPakkunF::init() {
    m_state = State::Wait;
    m_waitFrames = m_waitDuration;
    m_waitAnimationFrame = 0;
    m_damageFrame = 0;
    m_animationSound = {};
    const auto mode=static_cast<unsigned>(System::RaceConfig::Instance()->raceScenario().gameMode);
    m_scheduled=mode>=7||mode==2||mode==5;
    m_targetAngle=m_turnAngle=0.0f;m_targetCooldown=0;s_recentPlayer=-1;

    calcTransform();
    m_initialForward=transform().base(2);
}

/// @addr{0x80774754}
void ObjectPakkunF::calc() {
    switch (m_state) {
    case State::Wait:
        calcWait();
        break;
    case State::Attack:
        calcAttack();
        break;
    case State::Damage:
        calcDamage();
        break;
    default:
        break;
    }

    calcCollisionTransform();
    if(m_targetCooldown>0&&--m_targetCooldown==0)s_recentPlayer=-1;
    if (m_state == State::Attack) {
        m_animationSound.update(*this, "pakkun_f", "attack", m_currAttackFrame);
    } else if (m_state == State::Damage) {
        m_animationSound.update(*this, "pakkun_f", "damage", m_damageFrame,
                mkw::ObjectAnimationSound::frames("pakkun_f", "damage"));
    } else {
        ++m_waitAnimationFrame;
        m_animationSound.update(*this, "pakkun_f", "wait", m_waitAnimationFrame,
                mkw::ObjectAnimationSound::frames("pakkun_f", "wait"));
    }
}

/// @addr{0x80775464}
void ObjectPakkunF::loadAnims() {
    std::array<const char *, 3> names = {{
            "attack",
            "damage",
            "wait",
    }};

    std::array<Render::AnmType, 3> types = {{
            Render::AnmType::Chr,
            Render::AnmType::Chr,
            Render::AnmType::Chr,
    }};

    linkAnims(names, types);
}

/// @addr{0x80775108}
void ObjectPakkunF::calcCollisionTransform() {
    constexpr EGG::Vector3f INIT_POS = EGG::Vector3f(0.0f, 620.0f, 70.0f);
    constexpr EGG::Vector3f FINAL_POS = EGG::Vector3f(0.0f, 160.0f, 550.0f);

    if (!m_collision) {
        return;
    }

    EGG::Vector3f posOffset;

    if (m_state == State::Attack) {
        if (m_currAttackFrame <= 10) {
            posOffset = INIT_POS * scale().x;
        } else if (m_currAttackFrame <= 20) {
            f32 fVar1 = 0.1f * static_cast<f32>(m_currAttackFrame - 10);
            posOffset = FINAL_POS * scale().x * fVar1 + INIT_POS * scale().x * (1.0f - fVar1);
        } else if (m_currAttackFrame <= 30) {
            f32 fVar1 = 0.1f * static_cast<f32>(m_currAttackFrame - 20);
            posOffset = INIT_POS * scale().x * fVar1 + FINAL_POS * scale().x * (1.0f - fVar1);
        } else {
            posOffset = INIT_POS * scale().x;
        }
    } else {
        posOffset = INIT_POS * scale().x;
    }

    EGG::Matrix34f rotMat;
    rotMat.makeR(rot());

    EGG::Matrix34f damageRotMat = EGG::Matrix34f::ident;
    damageRotMat.setAxisRotation(m_targetAngle, EGG::Vector3f::ey);

    EGG::Matrix34f transformMat;
    transformMat.makeT(pos() + rotMat.multiplyTo(damageRotMat).ps_multVector(posOffset));

    m_collision->transform(transformMat, scale());
}

/// @addr{0x80774A00}
void ObjectPakkunF::calcWait() {
    if(m_scheduled){
        if(--m_waitFrames==0)enterAttack();
        return;
    }
    // USA8077e7e4 returns the first player in registry order strictly within
    // 2000 units in 3D; it is not a nearest-player or horizontal-only search.
    const auto* karts=Kart::KartObjectManager::Instance();
    for(size_t i=0;i<karts->count();++i)if((karts->object(i)->pos()-pos()).length()<2000.0f){
        if(static_cast<s32>(i)!=s_recentPlayer)enterAttack(static_cast<s32>(i));
        break;
    }
}

/// @addr{0x80774A84}
void ObjectPakkunF::calcAttack() {
    ++m_currAttackFrame;

    if (--m_attackFrames == 0) {
        m_state = State::Wait;
        m_waitFrames = m_waitDuration;
        m_waitAnimationFrame = 0;
    }
    // Preserve the retail strict <5 and 35<frame<=40 boundaries.
    if(m_currAttackFrame<5)m_turnAngle=m_targetAngle*(0.2f*static_cast<f32>(m_currAttackFrame));
    else if(m_currAttackFrame>35&&m_currAttackFrame<=40)
        m_turnAngle=m_targetAngle*(1.0f-0.2f*static_cast<f32>(m_currAttackFrame-35));
}

/// @addr{0x80774CB0}
void ObjectPakkunF::enterAttack(s32 player) {
    constexpr s32 LINGERING_FRAMES = 60; ///< Additional frames before switching back to idle state

    if(player>=0){
        const auto* kart=Kart::KartObjectManager::Instance()->object(static_cast<size_t>(player));
        EGG::Vector3f predicted=kart->pos()+kart->velocity()*20.0f;
        EGG::Vector3f toward=predicted-pos();toward.y=0.0f;toward.normalise();
        m_targetAngle=EGG::Mathf::atan2(toward.cross(m_initialForward).length(),toward.dot(m_initialForward));
        if(CheckPointAgainstLineSegment(predicted,pos(),pos()+m_initialForward)>0.0f)m_targetAngle=-m_targetAngle;
    }
    m_state = State::Attack;
    auto *anmMgr = m_drawMdl->anmMgr();
    anmMgr->playAnim(0.0f, 1.0f, 0);
    m_currAttackFrame = 0;
    auto *attackAnm = anmMgr->activeAnim(Render::AnmType::Chr);
    m_attackFrames = static_cast<s32>(attackAnm->frameCount()) + LINGERING_FRAMES;
}

// USA 80760c24: damage is animation 1 for 180 ticks. Another hit does
// not restart it; the original global-item path has a null item pointer.
void ObjectPakkunF::enterDamage() {
    if (m_state == State::Damage) return;
    m_state = State::Damage;
    m_damageFrame = 0;
    m_drawMdl->anmMgr()->playAnim(0.0f, 1.0f, 1);
}

void ObjectPakkunF::calcDamage() {
    if (++m_damageFrame < 180) return;
    m_state = State::Wait;
    m_waitFrames = m_waitDuration;
    m_waitAnimationFrame = 0;
    m_drawMdl->anmMgr()->playAnim(0.0f, 1.0f, 2);
}

Kart::Reaction ObjectPakkunF::onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
        Kart::Reaction reactionOnObj, EGG::Vector3f &) {
    if(static_cast<int>(reactionOnObj)==0&&kart){
        s_recentPlayer=kart->param()->playerIdx();
        const auto mode=static_cast<unsigned>(System::RaceConfig::Instance()->raceScenario().gameMode);
        m_targetCooldown=mode==4?300:600;
    }
    if (static_cast<int>(reactionOnObj) == 3 || static_cast<int>(reactionOnObj) == 5)
        enterDamage();
    return reactionOnKart;
}

s16 ObjectPakkunF::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    if (reactionOnObj == 3 || reactionOnObj == 5) enterDamage();
    return reactionOnItem;
}

} // namespace Kinoko::Field
