#include "ObjectCrab.hh"

#include "game/field/CollisionDirector.hh"
#include "game/field/ObjectDirector.hh"

#include "game/system/RaceManager.hh"
#include "game/system/CourseMap.hh"
#include "game/kart/KartObject.hh"
#include <algorithm>
#include <cmath>

namespace Kinoko::Field {

/// @addr{0x8088344C}
ObjectCrab::ObjectCrab(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_vel(static_cast<f32>(static_cast<s16>(params.setting(0)))),
      m_backwards(!!params.setting(1)), m_introCalc(false) {}

/// @addr{0x808837B8}
ObjectCrab::~ObjectCrab() = default;

/// @addr{0x80883844}
void ObjectCrab::init() {
    m_railInterpolator->init(0.0f, 0);

    calcCurRot(INIT_ROT);

    m_railInterpolator->setCurrVel(m_vel);
    setPos(m_railInterpolator->curPos());
    calcTransMat(m_curRot);

    m_stillDuration = 0;
    m_stillFrame = 0;
    m_still = false;
    m_state = State::Walking;
    m_statePhase = StatePhase::Start;
    m_introCalc = false;
    m_animationSound = {};
    m_visible = true; m_landed = false;
    m_damageFrame = 0; m_hideFrame = 300; m_surfaceOffset = 0;
    m_effectSerial = {}; m_effectPos = {};
    m_hasStaticWater = false; m_staticWaterHeight = 0.0f;
    // Effects::Mgr::LoadObjectEffect (USA806781d4/8067847c): pocha
    // sets the water flag; its setting0 selects a height slot,0 being water.
    const auto *course = System::CourseMap::Instance();
    for (u16 i = 0; i < course->getGeoObjCount(); ++i) {
        const auto *object = course->getGeoObj(i);
        if (object->id() != 7 || !(object->presenceFlag() & 1)) continue;
        m_hasStaticWater = true;
        if (object->setting(0) == 0) m_staticWaterHeight = object->pos().y;
    }
    enableCollision();
    // USA8087f6ac starts wait before the first walking-state update.
    startAnimation(2, 1.0f);
}

void ObjectCrab::startAnimation(u32 clip, f32 rate) {
    m_animationClip = clip;
    m_animationFrame = 0.0f;
    m_animationRate = rate;
}

/// @addr{0x80883B98}
void ObjectCrab::calc() {
    // The original model animation advances independently of the intro's
    // path-motion gate and of the one-frame endpoint-wait transition.
    m_animationFrame += m_animationRate;
    calcMotion();
    static constexpr const char *names[] = {"walk_l", "walk_r", "wait"};
    const char *name = names[m_animationClip];
    m_animationSound.update(*this, "crab", name, m_animationFrame,
            mkw::ObjectAnimationSound::frames("crab", name));
}

void ObjectCrab::calcMotion() {
    if (System::RaceManager::Instance()->timer() == 0 && m_introCalc) {
        return;
    }

    m_introCalc = true;

    if (!calcRail()) {
        return;
    }

    if (m_state == State::Deactivated) {
        if (m_statePhase == StatePhase::End) startResurfacing();
        else {
            if (m_statePhase == StatePhase::Start) startDamage();
            calcDamage();
            return;
        }
    }
    if (m_state == State::Resurfacing) {
        if (m_statePhase != StatePhase::End) { calcResurfacing(); return; }
        m_state = State::Walking;
        m_statePhase = StatePhase::Start;
    }

    StateResult res = calcState();

    if (res == StateResult::Middle) {
        return;
    }

    if (res == StateResult::Walking) {
        if (m_statePhase == StatePhase::Start) {
            // USA8087f8fc..f978 selects the opposite walk when the rail reverses.
            // Original constants at808d8484/88 give rate = KMP speed /9.
            const bool forward = m_railInterpolator->isMovementDirectionForward();
            startAnimation(m_backwards == forward ? 1 : 0, m_vel / 9.0f);
            m_statePhase = StatePhase::Middle;
        }
    }

    if (m_statePhase == StatePhase::Middle) {
        setPos(m_railInterpolator->curPos());

        if (m_still) {
            m_statePhase = StatePhase::End;
        }

        return;
    } else {
        m_state = State::Still;
        m_statePhase = StatePhase::Start;

        calcState();
    }
}

void ObjectCrab::emit(unsigned index, const EGG::Vector3f &position) {
    m_effectPos[index] = position;
    ++m_effectSerial[index];
}

void ObjectCrab::hit(const EGG::Vector3f &source) {
    if (m_state == State::Deactivated) return;
    m_hitSource = source;
    m_state = State::Deactivated;
    m_statePhase = StatePhase::Start;
    m_damageFrame = 0;
}

Kart::Reaction ObjectCrab::onCollision(Kart::KartObject *kart, Kart::Reaction onKart,
        Kart::Reaction onObject, EGG::Vector3f &) {
    if (m_state == State::Deactivated) return Kart::Reaction::None;
    const auto reaction = static_cast<s16>(onObject);
    if (reaction == 3 || reaction == 5) hit(kart ? kart->pos() : pos());
    return onKart;
}

void ObjectCrab::processItemReactions(s16 type, bool direct, s16 &onItem, s16 &onObject) {
    ObjectBase::processItemReactions(type, direct, onItem, onObject);
    m_itemType = type;
}

s16 ObjectCrab::onItemCollision(const EGG::Vector3f &position,
        const EGG::Vector3f &velocity, s16 onItem, s16 onObject, EGG::Vector3f &) {
    if (m_state == State::Deactivated) return 0;
    // USA80881ef0: the two shell types pass pos-itemPos; other items pass velocity.
    if (onObject == 3 || onObject == 5)
        hit(m_itemType == 0 || m_itemType == 1 ? pos() - position : velocity);
    return onItem;
}

void ObjectCrab::onGlobalItemCollision(s16, s16 onObject) {
    if (onObject == 3 || onObject == 5) hit(pos());
}

void ObjectCrab::startDamage() {
    // USA80880400..80880870. The airborne body and continuing rail have
    // separate positions. Rotate about a point105 units above the model root.
    calcCurRot(INIT_ROT);
    calcTransMat(m_curRot);
    const auto up = transform().base(1);
    auto direction = (pos() - up * pos().dot(up)) -
            (m_hitSource - up * m_hitSource.dot(up));
    if (direction.squaredLength() <= 1.1920928955078125e-7f)
        direction = -transform().base(2);
    direction.normalise();
    m_hitBasis.makeOrthonormalBasisLocal(direction, up);
    EGG::Matrix34f launch;
    launch.setAxisRotation(-0.78f, m_hitBasis.base(0));
    m_hitVelocity = launch.multVector33(direction) * 74.0f;
    m_hitPivot = m_railInterpolator->curPos() + up * 105.0f;
    disableCollision();
    m_landed = false; m_visible = true;
    m_damageFrame = 0; m_hideFrame = 300;
    startAnimation(2, 0.0f);
    m_statePhase = StatePhase::Middle;
}

void ObjectCrab::calcDamage() {
    // USA8088087c..80880ba0: Lerp(v,v+gravity,.99), not velocity damping.
    m_hitVelocity.y += -2.4f * 0.99f;
    m_hitPivot += m_hitVelocity;
    EGG::Matrix34f turn;
    turn.makeR(EGG::Vector3f(0, 0, 0.5f *
            (1.0f - std::min(static_cast<f32>(m_damageFrame) / 45.0f, 1.0f))));
    const auto rotation = m_hitBasis.multiplyTo(turn).multiplyTo(m_hitBasis.transpose());
    auto pose = transform();
    pose.setBase(3, -pose.base(1) * 105.0f);
    pose = rotation.multiplyTo(pose);
    pose.setBase(3, pose.base(3) + m_hitPivot);
    setPos(pose.base(3));
    setTransform(pose);

    if (m_hitVelocity.y < 0.0f && !m_landed) {
        CollisionInfo info{}; info.reset();
        KCLTypeMask mask{};
        auto *collision = CollisionDirector::Instance();
        if (collision->checkSphereFull(10.0f, m_hitPivot, EGG::Vector3f::inf,
                KCLTypeMask(0x20e80fff), &info, &mask, 0)) {
            const f32 length = info.tangentOff.length();
            if (length > 1.1920928955078125e-7f) {
                const auto surface = m_hitPivot + info.tangentOff * ((length - 10.0f) / length)
                        - info.floorNrm * 50.0f;
                // Source mask4 (weak offroad), variant4 selects rk_crabPocha.
                const bool water = collision->findClosestCollisionEntry(&mask, KCLTypeMask(4)) &&
                        collision->closestCollisionEntry()->variant() == 4;
                emit(water ? 1 : 0, surface);
                m_landed = true;
            }
            m_hideFrame = std::min(m_hideFrame, m_damageFrame + 3);
        } else {
            const auto *director = ObjectDirector::Instance();
            if (director->psea() && director->distAboveRisingWater(m_hitPivot.y - 10.0f) < 0) {
                emit(1, EGG::Vector3f(m_hitPivot.x, -director->distAboveRisingWater(0), m_hitPivot.z));
                m_landed = true;
                m_hideFrame = std::min(m_hideFrame, m_damageFrame + 3);
            } else if (m_hasStaticWater && m_hitPivot.y - 10.0f < m_staticWaterHeight) {
                emit(1, EGG::Vector3f(m_hitPivot.x, m_staticWaterHeight, m_hitPivot.z));
                m_landed = true;
                m_hideFrame = std::min(m_hideFrame, m_damageFrame + 3);
            }
        }
    }
    if (m_damageFrame == m_hideFrame) m_visible = false;
    ++m_damageFrame;
    // Do not emerge at a path endpoint: enough path must remain for the
    //50-frame rise and20-frame untilt, as in USA808813ec..80881440.
    if (!m_still && m_damageFrame >= 300 &&
            (m_railInterpolator->curPos() - m_railInterpolator->nextPoint().pos).length() > 630.0f)
        m_statePhase = StatePhase::End;
}

void ObjectCrab::startResurfacing() {
    m_state = State::Resurfacing;
    m_statePhase = StatePhase::Middle;
    calcCurRot(INIT_ROT);
    m_curRot.z = m_backwards ? 1.0f : -1.0f;
    if (!m_railInterpolator->isMovementDirectionForward()) m_curRot.z = -m_curRot.z;
    m_surfaceOffset = -250.0f;
    m_visible = true;
    enableCollision();
    startAnimation(m_railInterpolator->isMovementDirectionForward() ? 0 : 1, m_vel / 9.0f);
    emit(2, m_railInterpolator->curPos());
}

void ObjectCrab::calcResurfacing() {
    const f32 rate = m_vel / 9.0f;
    m_surfaceOffset = m_surfaceOffset < -5.0f * rate ? m_surfaceOffset + 5.0f * rate : 0;
    setPos(m_railInterpolator->curPos() + EGG::Vector3f(0, m_surfaceOffset, 0));
    if (m_surfaceOffset >= -70.0f) {
        const f32 step = 0.05f * rate;
        if (m_curRot.z >= step) m_curRot.z -= step;
        else if (m_curRot.z <= -step) m_curRot.z += step;
        else m_curRot.z = 0;
    }
    // Source multiplies yaw then roll, not the generic EulerXYZ order.
    EGG::Matrix34f yaw, roll, basis;
    yaw.makeR(EGG::Vector3f(0, m_curRot.y, 0));
    roll.makeR(EGG::Vector3f(0, 0, m_curRot.z));
    basis.makeOrthonormalBasisLocal(m_railInterpolator->curTangentDir(), EGG::Vector3f::ey);
    auto pose = basis.multiplyTo(yaw).multiplyTo(roll);
    pose.setBase(3, pos()); setTransform(pose);
    m_effectPos[2] = m_railInterpolator->curPos();
    if (m_surfaceOffset >= 0 && m_curRot.z == 0) m_statePhase = StatePhase::End;
}

bool ObjectCrab::calcRail() {
    if (m_still) {
        if (m_stillDuration <= ++m_stillFrame) {
            m_railInterpolator->setCurrVel(m_vel);
            m_still = false;
            return false;
        }

        return true;
    }

    auto status = m_railInterpolator->calc();
    if (status == RailInterpolator::Status::SegmentEnd ||
            status == RailInterpolator::Status::ChangingDirection) {
        // USA8087f894 sign-extends this setting; negative values do not wait.
        s16 duration = static_cast<s16>(m_railInterpolator->curPoint().setting[0]);
        if (duration > 0) {
            m_railInterpolator->setCurrVel(0.0f);
            m_stillDuration = duration;
            m_stillFrame = 0;
            m_still = true;
        }
    }

    return true;
}

ObjectCrab::StateResult ObjectCrab::calcState() {
    if (m_state != State::Still) {
        return StateResult::Walking;
    }

    if (m_statePhase == StatePhase::Start) {
        setPos(m_railInterpolator->curPos());
        calcCurRot(INIT_ROT);
        calcTransMat(m_curRot);
        startAnimation(2, 1.0f);
        m_statePhase = StatePhase::Middle;
    }

    if (m_statePhase == StatePhase::Middle) {
        if (!m_still) {
            m_statePhase = StatePhase::End;
        }

        return StateResult::Middle;
    } else {
        m_state = State::Walking;
        m_statePhase = StatePhase::Start;
    }

    return StateResult::BeginWalking;
}

void ObjectCrab::calcCurRot(const EGG::Vector3f &rot) {
    m_curRot = rot;
    m_curRot = m_backwards ? -m_curRot : m_curRot;
    m_curRot.y = m_railInterpolator->isMovementDirectionForward() ? m_curRot.y : -m_curRot.y;
}

void ObjectCrab::calcTransMat(const EGG::Vector3f &rot) {
    EGG::Matrix34f rotMat;
    rotMat.makeR(rot);
    rotMat = rotMat.multiplyTo(EGG::Matrix34f::ident);

    EGG::Matrix34f mat;
    mat.makeOrthonormalBasisLocal(m_railInterpolator->curTangentDir(), EGG::Vector3f::ey);
    mat = mat.multiplyTo(rotMat);
    mat.setBase(3, pos());
    setTransform(mat);
}

} // namespace Kinoko::Field
