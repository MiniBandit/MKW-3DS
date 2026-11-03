#include "ObjectPenguin.hh"
#include "game/kart/KartCollide.hh"
#include <algorithm>

namespace Kinoko::Field {

/// @addr{0x80775624} @addr{0x8077708C}
ObjectPenguin::ObjectPenguin(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}

/// @addr{0x80775670} @addr{0x807774A4}
ObjectPenguin::~ObjectPenguin() = default;

/// @addr{0x807756B0}
void ObjectPenguin::init() {
    m_railInterpolator->init(0.0f, 0);
    m_state = State::Walk;
    m_basis = EGG::Vector3f::ez;
    m_soundFrame = 0;
    m_animationSound = {};
    m_spinTicks = 0;
}

/// @addr{0x80775B1C}
void ObjectPenguin::calcRot() {
    constexpr f32 INTERP_RATE = 0.2f;

    m_basis = Interpolate(INTERP_RATE, m_basis, m_railInterpolator->curTangentDir());
    m_basis.normalise();
    setMatrixFromOrthonormalBasisAndPos(m_basis);
}

/// @addr{0x8077588C}
void ObjectPenguin::enterWalk() {
    m_state = State::Walk;
    calcTransform();
    setRotNoFlag(transform().base(2));
}

/// @addr{0x80775E60}
ObjectPenguinS::ObjectPenguinS(const System::MapdataGeoObj &params) : ObjectPenguin(params) {}

/// @addr{0x80776070}
ObjectPenguinS::~ObjectPenguinS() = default;

/// @addr{0x807760B0}
void ObjectPenguinS::init() {
    m_anmTimer = 0;
    m_walkRate = 1.0f;
    m_railInterpolator->init(0.0f, 0);
    m_basis = m_railInterpolator->curTangentDir();
    m_state = State::Walk;
    m_soundFrame = 0;
    m_animationSound = {};
    m_spinTicks = 0;
}

/// @addr{0x80776198}
void ObjectPenguinS::calc() {
    switch (m_state) {
    case State::Walk:
        calcWalk();
        break;
    case State::Dive:
        // USA80761f78: accelerate the walk CHR0 by .03 to3 before diving.
        m_walkRate = std::min(3.0f, m_walkRate + 0.03f);
        calcDive();
        break;
    case State::Slider:
        calcSlider();
        break;
    case State::SliderSlow:
        calcWalk();
        calcTransform();
        cueSound(0x34a, SoundCall::Hold); // Original belly-slide loop.
        break;
    case State::StandUp:
        calcStandUp();
        break;
    case State::Spin:
        calcSpin();
        break;
    }
    // Use the native dive/stand countdown and the same walk clock the model
    // draws. Intermediate sliding states have no BRASD point-event file.
    const char *clip = "slider";
    f32 period = 0;
    const State visualState = m_state == State::Spin ? m_beforeSpin : m_state;
    if (visualState == State::Walk || visualState == State::Dive) {
        clip = "walk";
        m_soundFrame += m_walkRate;
        period = mkw::ObjectAnimationSound::frames("penguin_s", clip);
    } else if (visualState == State::Slider || visualState == State::StandUp) {
        clip = visualState == State::Slider ? "dive" : "stand_up";
        if (m_state == State::Spin) ++m_soundFrame;
        else m_soundFrame = m_drawMdl->anmMgr()->activeAnim(Render::AnmType::Chr)->frameCount() - static_cast<f32>(m_anmTimer);
    } else { ++m_soundFrame; }
    m_animationSound.update(*this, "penguin_s", clip, m_soundFrame, period);
}

/// TODO: Maybe not needed
void ObjectPenguinS::loadAnims() {
    std::array<const char *, 4> names = {{
            "walk",
            "dive",
            "slider",
            "stand_up",
    }};

    std::array<Render::AnmType, 4> types = {{
            Render::AnmType::Chr,
            Render::AnmType::Chr,
            Render::AnmType::Chr,
            Render::AnmType::Chr,
    }};

    linkAnims(names, types);
}

/// @addr{0x80776498}
void ObjectPenguinS::calcSlider() {
    calcRail();
    calcPos();
    calcRot();

    if (--m_anmTimer == 0) {
        m_state = State::SliderSlow;
    }
}

/// @addr{0x80776670}
void ObjectPenguinS::calcStandUp() {
    calcRail();
    calcPos();
    calcRot();

    if (--m_anmTimer == 0) {
        enterWalk();
    }
}

/// @addr{0x807768A0}
void ObjectPenguinS::calcRail() {
    if (m_railInterpolator->calc() != RailInterpolator::Status::SegmentEnd) {
        return;
    }

    const auto &curPoint = m_railInterpolator->curPoint();
    u16 setting = curPoint.setting[0];

    if (setting == 0 && m_state == State::SliderSlow) {
        auto *anmMgr = m_drawMdl->anmMgr();
        anmMgr->playAnim(0.0f, 1.0f, 3);
        m_state = State::StandUp;
        m_anmTimer = anmMgr->activeAnim(Render::AnmType::Chr)->frameCount();
    } else if (setting == 1 && m_state == State::Walk) {
        auto *anmMgr = m_drawMdl->anmMgr();
        anmMgr->playAnim(0.0f, 1.0f, 1);
        m_state = State::Slider;
        m_anmTimer = anmMgr->activeAnim(Render::AnmType::Chr)->frameCount();
    } else if (setting == 2 && m_state == State::Walk) {
        m_state = State::Dive;
    }

    m_railInterpolator->setCurrVel(static_cast<f32>(curPoint.setting[1]));
}

void ObjectPenguin::calc() {
    if (m_state == State::Walk) calcWalk();
    else if (m_state == State::Spin) calcSpin();
    else return;
    ++m_soundFrame;
    // Large penguins have no walk BRASD resource; medium cues belong to
    // the cave model rather than every class derived from this controller.
    if (id() == ObjectId::PenguinM)
        m_animationSound.update(*this, "penguin_m", "walk", m_soundFrame,
                mkw::ObjectAnimationSound::frames("penguin_m", "walk"));
}

Kart::Reaction ObjectPenguin::onCollision(Kart::KartObject *, Kart::Reaction reactionOnKart,
        Kart::Reaction reactionOnObj, EGG::Vector3f &) {
    if (reactionOnObj == Kart::Reaction::UNK_3 || reactionOnObj == Kart::Reaction::UNK_5) spinOut();
    return reactionOnKart;
}

s16 ObjectPenguin::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    if (reactionOnObj == 3 || reactionOnObj == 5) spinOut();
    return reactionOnItem;
}

// USA80761a98 /80762afc: ignore a repeated hit during the same180-tick
// spin, preserving the previous walk/slide state for its source recovery.
void ObjectPenguin::spinOut() {
    if (m_state == State::Spin) return;
    m_beforeSpin = m_state;
    m_state = State::Spin;
    m_spinTicks = 180; m_spinSpeed = 60.0f; m_spinAngle = 0.0f;
    calcTransform(); m_spinPose = transform();
    cueSound(id() == ObjectId::PenguinM ? 0x345 : 0x34e, SoundCall::Voice);
}

void ObjectPenguin::applySpin() {
    // USA807616bc: subtract1degree per tick down to12, then add that angle
    // to the object's yaw. The rail position keeps advancing underneath it.
    m_spinSpeed = std::max(12.0f, m_spinSpeed - 1.0f);
    m_spinAngle += m_spinSpeed * 0.017453292519943295f;
    EGG::Matrix34f rotation; rotation.setAxisRotation(m_spinAngle, EGG::Vector3f::ey);
    EGG::Matrix34f pose = m_spinPose;
    for (u32 axis = 0; axis < 3; ++axis) pose.setBase(axis, rotation.multVector33(m_spinPose.base(axis)));
    pose.setBase(3, pos());
    setTransform(pose);
}

void ObjectPenguin::calcSpin() {
    m_railInterpolator->calc(); calcPos(); applySpin();
    if (m_spinTicks && --m_spinTicks == 0) {
        m_basis = transform().base(2);
        enterWalk();
    }
}

void ObjectPenguinS::calcSpin() {
    calcRail(); calcPos(); applySpin();
    if (m_spinTicks && --m_spinTicks == 0) {
        m_basis = transform().base(2);
        if (m_beforeSpin == State::Slider || m_beforeSpin == State::SliderSlow) {
            auto *manager = m_drawMdl->anmMgr();
            manager->playAnim(0.0f, 1.0f, 3);
            m_anmTimer = manager->activeAnim(Render::AnmType::Chr)->frameCount();
            m_soundFrame = 0.0f; m_state = State::StandUp;
        } else enterWalk();
    }
}

} // namespace Kinoko::Field
