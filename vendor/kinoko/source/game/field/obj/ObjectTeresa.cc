#include "ObjectTeresa.hh"
#include "game/field/ObjectDirector.hh"
#include "game/system/CourseMap.hh"
#include "game/system/RaceManager.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/render/KartCamera.hh"
#include <algorithm>
#include <cmath>
namespace Kinoko::Field {
namespace {
class TeresaSoundSource final : public ObjectCollidable {
public:
    explicit TeresaSoundSource(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void load() override { ObjectDirector::Instance()->addObject(this); }
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    void calcCollisionTransform() override {}
};
void updateSoundPosition(ObjectTeresa::Ghost &ghost) {
    if (const auto *camera = Render::KartCamera::Instance()) {
        const auto &eye = camera->lookingBack() ? camera->rearPosition() : camera->position();
        auto position = eye + ghost.offset + ghost.displacement;
        position.y = std::max(3000.0f, position.y);
        ghost.soundSource->setPos(position);
    }
}
}
void ObjectTeresa::load() {
    // Like the retail children, every apparition owns its own sound handles.
    for (auto &source : m_soundSources) {
        source = EGG::egg_new<TeresaSoundSource>(*mapObj());
        source->load();
    }
    ObjectDirector::Instance()->addObject(this);
}
void ObjectTeresa::init() {
    // USA80770128: six children per local player; current port has one local player.
    m_next = 0;
    m_delay = 30;
    for (unsigned i = 0; i < m_ghosts.size(); ++i) {
        auto &ghost = m_ghosts[i];
        ghost = {};
        ghost.soundSource = m_soundSources[i];
        ghost.variant = System::RaceManager::Instance()->random().getU32(3);
    }
}
void ObjectTeresa::appear(Ghost &g) {
    // USA8076f85c: offsets use the kart's right/forward axes captured on appearance.
    const auto *kart = Kart::KartObjectManager::Instance()->object(0);
    auto right = kart->pose().base(0), forward = kart->pose().base(2);
    right.normalise();
    forward.normalise();
    auto &random = System::RaceManager::Instance()->random();
    const f32 x = random.getF32(2000) - 1000;
    const f32 y = random.getF32(250);
    const f32 z = random.getF32(1500) + 1500;
    g.offset = right * x + EGG::Vector3f{0, y, 0} + forward * z;
    const f32 ax = random.getF32(3000) - 1500;
    const f32 ay = random.getF32(1000) - 500;
    const f32 az = random.getF32(2000) - 1000;
    g.amplitude = right * ax + EGG::Vector3f{0, ay, 0} + forward * az;
    g.displacement = {};
    g.life = 180 + random.getU32(240);
    // Retail negates the existing texture basis, so subsequent appearances toggle it.
    if (random.getU32(2) == 0) g.mirrorU = !g.mirrorU;
    g.state = 0;
    updateSoundPosition(g);
    g.soundSource->cueSound(0x263, SoundCall::Start);
}
void ObjectTeresa::calc() {
    // Children are registered before their controller and update before new appearances.
    for (auto &g : m_ghosts) {
        if (g.state == 3) continue;
        if (g.state == 0) {
            g.alpha += 5;
            if (g.alpha > 250) {
                g.alpha = 255;
                g.state = 1;
                g.alphaPhase = 0;
                g.alphaSpeed = 1 + System::RaceManager::Instance()->random().getF32(2);
            }
        } else if (g.state == 1) {
            g.alphaPhase += g.alphaSpeed;
            if (g.alphaPhase > 360) g.alphaPhase = 0;
            g.alpha = u8(191 + 64 * std::sin(g.alphaPhase * 0.01745329252f));
        } else {
            g.alpha -= 5;
            if (g.alpha < 5) { g.alpha = 0; g.state = 3; }
        }
        g.phase += 1;
        if (g.phase > 360) g.phase = 0;
        g.displacement = g.amplitude * std::sin(g.phase * 0.01745329252f);
        updateSoundPosition(g);
        if (--g.life == 0) g.state = 2;
    }
    const auto *kart = Kart::KartObjectManager::Instance()->object(0);
    // USA80770000: no new apparitions while the local camera looks backwards.
    if (const auto *camera = Render::KartCamera::Instance(); camera && camera->lookingBack()) return;
    // USA80770000: count only inside AREA type7; keep the remaining delay outside.
    if (System::CourseMap::Instance()->getCurrentAreaID(-1, kart->pos(),
            static_cast<System::MapdataAreaBase::Type>(7)) < 0) return;
    if (--m_delay != 0) return;
    m_delay = 30;
    auto &ghost = m_ghosts[m_next];
    if (ghost.state == 3) appear(ghost);
    m_next = (m_next + 1) % m_ghosts.size();
}
}
