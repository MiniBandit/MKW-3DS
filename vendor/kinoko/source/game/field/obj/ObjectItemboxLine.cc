#include "ObjectItemboxLine.hh"

#include "game/field/obj/ObjectItemboxPress.hh"
#include "game/field/obj/ObjectPress.hh"
#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {

/// @addr{0x8076D044}
ObjectItemboxLine::ObjectItemboxLine(const System::MapdataGeoObj &params)
    : ObjectCollidable(params) {
    constexpr u32 DEFAULT_PRESS_COUNT = 5;

    auto *senko = EGG::egg_new<ObjectPressSenko>(params);
    senko->load();

    u32 pressCount = params.setting(6);
    if (pressCount == 0) {
        pressCount = DEFAULT_PRESS_COUNT;
    }

    m_blocks = owning_span<ObjectItemboxBlock *>(pressCount);
    for (auto *&block : m_blocks) {
        block = EGG::egg_new<ObjectItemboxBlock>(params);
        block->load();
    }
    m_press = owning_span<ObjectItemboxPress *>(pressCount);

    u32 blockIndex = 0;
    for (auto *&press : m_press) {
        press = EGG::egg_new<ObjectItemboxPress>(params);
        press->load();
        press->setSenko(senko);
        press->setBlock(m_blocks[blockIndex++]);
    }
}

/// @addr{0x8076D558}
ObjectItemboxLine::~ObjectItemboxLine() = default;

/// @addr{0x8076D604}
void ObjectItemboxLine::init() {
    ASSERT(m_mapObj);
    u32 timer = static_cast<u32>(m_mapObj->setting(4));

    if (timer == 0) {
        timer = static_cast<u32>(m_mapObj->setting(5));
    }

    m_stompCooldown = timer;
    m_curPressIdx = 0;
}

/// @addr{0x8076D64C}
void ObjectItemboxLine::calc() {
    if (--m_stompCooldown > 0) {
        return;
    }

    m_stompCooldown = static_cast<u32>(m_mapObj->setting(5));

    m_blocks[m_curPressIdx]->start();
    m_press[m_curPressIdx]->startPress();
    m_curPressIdx = (m_curPressIdx + 1) % m_press.size();
}

// USA8075babc: hidden and intangible until the parent activates this slot.
void ObjectItemboxBlock::init() {
    m_state = 0;
    m_shown = m_broken = false;
    m_breakCount = 0;
    if (m_collision) disableCollision();
}

// USA8075afdc..8075b06c / 8075bb28.
void ObjectItemboxBlock::start() {
    m_state = 1;
    const auto mode = static_cast<unsigned>(System::RaceConfig::Instance()->raceScenario().gameMode);
    const bool ordinaryRace = mode < 7 && mode != 2 && mode != 5 && mode != 3;
    m_shown = ordinaryRace;
    if (m_collision && ordinaryRace) enableCollision();
    m_railInterpolator->init(0.0f, 0);
    m_railInterpolator->setPerPointVelocities(true);
}

// USA8075bbd8: the brick travels at rail height; the paired box is 180 units above it.
void ObjectItemboxBlock::calc() {
    if (m_state == 0) { m_shown = false; return; }
    const auto result = m_railInterpolator->calc();
    if (result == RailInterpolator::Status::SegmentEnd &&
            m_railInterpolator->curPoint().setting[1] == 2) {
        m_state = 2;
        m_shown = false;
        if (m_collision) disableCollision();
    } else if (result == RailInterpolator::Status::ChangingDirection) {
        m_state = 0;
        m_shown = m_broken = false;
        if (m_collision) disableCollision();
    }
    setPos(m_railInterpolator->curPos());
}

void ObjectItemboxBlock::react(s16 reaction) {
    if (reaction != 3 && reaction != 5) return;
    // The original effect is a separate EGG::Effect (+b8), not the paired item box.
    m_breakPosition = pos();
    ++m_breakCount;
    m_state = 2;
    m_broken = true;
    m_shown = false;
    if (m_collision) disableCollision();
}

Kart::Reaction ObjectItemboxBlock::onCollision(Kart::KartObject *, Kart::Reaction kart,
        Kart::Reaction object, EGG::Vector3f &) {
    react(static_cast<s16>(object));
    return kart;
}

s16 ObjectItemboxBlock::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 item, s16 object, EGG::Vector3f &) {
    react(object);
    return item;
}

} // namespace Kinoko::Field
