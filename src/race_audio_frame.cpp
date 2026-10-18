// Copies the race state the audio thread needs out of Kinoko into a
// RaceAudioFrame, once per simulation tick on the main thread. Only the
// audio-only sound cues are taken (cleared); the simulation state is read.
#include <mkw/race_audio.hpp>
#include <mkw/race_item_internal.hpp>
#include <mkw/race_items.hpp>
#include <game/kart/CollisionGroup.hh>
#include <game/kart/KartDynamics.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartParam.hh>
#include <game/kart/KartState.hh>
#include <game/kart/KartSuspensionPhysics.hh>
#include <game/kart/Status.hh>
#include <game/kart/KartAction.hh>
#include <game/field/ObjectDirector.hh>
#include <game/field/ObjectDrivableDirector.hh>
#include <game/field/obj/ObjectBase.hh>
#include <game/field/obj/ObjectId.hh>
#include <game/system/CourseMap.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkw {
namespace K = Kinoko;
namespace EGG = Kinoko::EGG;
// Kinoko numbers its status flags exactly as retail's KartStatus bitfields;
// the flag set is a plain array of those 32-bit words.
static_assert(sizeof(K::Kart::Status) == 5 * sizeof(std::uint32_t));
using Cue = K::Kart::KartMove::SoundCue;
static_assert(std::uint32_t(Cue::Boost) == cue::Boost && std::uint32_t(Cue::BoostRamp) == cue::BoostRamp &&
              std::uint32_t(Cue::JumpPad) == cue::JumpPad && std::uint32_t(Cue::JumpPadMushroom) == cue::JumpPadMushroom &&
              std::uint32_t(Cue::Hop) == cue::Hop && std::uint32_t(Cue::Cannon) == cue::Cannon &&
              std::uint32_t(Cue::Landing) == cue::Landing && std::uint32_t(Cue::Horn) == cue::Horn &&
              std::uint32_t(Cue::Bumped) == cue::Bumped && std::uint32_t(Cue::BushWall) == cue::BushWall &&
              std::uint32_t(Cue::BoostVoice) == cue::BoostVoice && std::uint32_t(Cue::Trick) == cue::Trick &&
              std::uint32_t(Cue::Respawn) == cue::Respawn && std::uint32_t(Cue::StartFail) == cue::StartFail &&
              std::uint32_t(Cue::Wheelie) == cue::Wheelie && std::uint32_t(Cue::PowerHit) == cue::PowerHit &&
              std::uint32_t(Cue::StarHit) == cue::StarHit && std::uint32_t(Cue::Bump) == cue::Bump &&
              std::uint32_t(Cue::Fall) == cue::Fall && std::uint32_t(Cue::MegaUse) == cue::MegaUse &&
              std::uint32_t(Cue::MegaEnd) == cue::MegaEnd && std::uint32_t(Cue::Grow) == cue::Grow &&
              std::uint32_t(Cue::Press) == cue::Press && std::uint32_t(Cue::PressEnd) == cue::PressEnd &&
              std::uint32_t(Cue::Spin) == cue::Spin && std::uint32_t(Cue::BlowSide) == cue::BlowSide &&
              std::uint32_t(Cue::SoundTrigger) == cue::SoundTrigger);
static_assert(std::uint32_t(K::Kart::eStatus::ZipperTrick) == 47);
// The presentation cues must not collide with Kinoko's.
static_assert(cue::JumpAction > std::uint32_t(Cue::SoundTrigger) && cue::ZipperTrick > cue::JumpAction && cue::Vanish > cue::ZipperTrick);
static_assert(std::uint32_t(K::Kart::eStatus::Vanishing) == 82);

namespace {
// Kinoko objects whose ObjFlow entry gives them a sound actor; a slot is the
// object's index here for the rest of the race. Course objects Kinoko does not
// build but retail gives a sound actor are kept by their GOBJ entry: the
// ObjectActor idle loops whose objects have no collision (flags, the Peach
// Beach fountain) and the KMP sound_* emitters (positioned on their route).
struct ObjectRegistry {
 std::size_t scanned = 0, scannedDrivable = 0;
 bool courseScanned = false;
 std::uint8_t pocha = 0;
 std::uint8_t count = 0;
 std::array<const K::Field::ObjectBase*, 128> object{};
 std::array<const K::System::MapdataGeoObj*, 128> geo{};
 std::array<std::uint16_t, 128> id{};
 std::array<std::uint8_t, 128> mode{}, index{};
 int find(const K::Field::ObjectBase* o) const { for (int i = 0; i < count; ++i) if (object[i] == o) return i; return -1; }
 int add(const K::Field::ObjectBase* o, const K::System::MapdataGeoObj* g, std::uint16_t objectId, int m) {
  if (count >= object.size()) return -1;
  std::uint8_t same = 0;   // the object's index among those of its kind (bats alternate by it)
  for (int i = 0; i < count; ++i) same += id[i] == objectId;
  object[count] = o; geo[count] = g; id[count] = objectId; mode[count] = std::uint8_t(m); index[count] = same;
  return count++;
 }
};
ObjectRegistry registry;
// The object's sound actor slot (Object +0x18): its registry slot, registered
// now if its ObjFlow entry has an actor; -1 (0xff) without one.
std::uint8_t objectSlot(const K::Field::ObjectBase* o) {
 int slot = registry.find(o);
 if (slot < 0) {
  const auto* dir = K::Field::ObjectDirector::Instance();
  const int m = dir ? dir->flowTable().soundMode(o->id()) : 0;
  if (m == 1 || m == 3) slot = registry.add(o, nullptr, std::uint16_t(o->id()), m);
 }
 return slot < 0 ? 0xff : std::uint8_t(slot);
}
// ObjectActor::Update idle loops (PAL fn_1_1FDA94) on objects Kinoko leaves out.
bool idleLoopObject(std::uint16_t id) {
 switch (id) {
 case 0x141: case 0x152: case 0x153: case 0x154: case 0x155: case 0x156: case 0x180: case 0x182: return true;
 default: return false;
 }
}
// The sound_* emitters' table (PAL 0x808C8CB8): GOBJ ids 5-0x10 and 0x15.
bool emitterObject(std::uint16_t id) { return (id >= 5 && id <= 0x10 && id != 7) || id == 0x15; }
float dot(const EGG::Vector3f& a, const EGG::Vector3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float dist(const EGG::Vector3f& a, const EGG::Vector3f& b) {
 const EGG::Vector3f d{a.x - b.x, a.y - b.y, a.z - b.z};
 return std::sqrt(dot(d, d));
}
// fn_1_1A43A8 (PAL 0x806B475C): the point of a POTI route nearest `p`, as
// retail finds it (a segment pair's XZ side test picks the segment; outside
// the pair's wedge the half-way split overwrites the best point unconditionally).
EGG::Vector3f nearestOnRoute(const K::System::MapdataPointInfo& route, const EGG::Vector3f& p) {
 const auto& pts = route.points();
 const std::size_t n = pts.size();
 auto P = [&](std::size_t i) -> const EGG::Vector3f& { return pts[i].pos; };
 auto lerp = [](const EGG::Vector3f& a, const EGG::Vector3f& b, float t) { return EGG::Vector3f{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; };
 auto param = [&](const EGG::Vector3f& a, const EGG::Vector3f& b) {
  const EGG::Vector3f ab{b.x - a.x, b.y - a.y, b.z - a.z}, ap{p.x - a.x, p.y - a.y, p.z - a.z};
  return dot(ab, ap) / dot(ab, ab);
 };
 if (n == 2) {
  const float t = param(P(0), P(1));
  if (t < 0.0f || t >= 1.0f) return dist(p, P(1)) < dist(p, P(0)) ? P(1) : P(0);
  return lerp(P(0), P(1), t);
 }
 float best = -1.0f, bt = 0.0f; int seg = -1, off = -1;
 for (std::size_t i = 0; i + 2 < n; ++i) {
  const EGG::Vector3f &A = P(i), &B = P(i + 1), &C = P(i + 2);
  const float cross = (C.x - A.x) * (B.z - A.z) - (B.x - A.x) * (C.z - A.z);
  const float s1 = (B.x - A.x) * (p.z - A.z) - (p.x - A.x) * (B.z - A.z);
  const float s2 = (C.x - B.x) * (p.z - B.z) - (p.x - B.x) * (C.z - B.z);
  const bool inside = cross > 0.0f ? (s1 > 0.0f || s2 > 0.0f) : (s1 <= 0.0f || s2 <= 0.0f);
  if (inside) {
   const float t1 = param(A, B), t2 = param(B, C);
   if (t1 < 0.0f || t1 >= 1.0f) {
    if (t2 < 0.0f || t2 >= 1.0f) {
     const float t3 = param(A, C);
     if (t3 >= 0.0f && t3 < 1.0f) {
      const float d = dist(p, B);
      if (best == -1.0f || d < best) { seg = int(i); off = 1; bt = 0.0f; best = d; }
     }
    } else {
     const float d = dist(p, lerp(B, C, t2));
     if (best == -1.0f || d < best) { seg = int(i); off = 1; bt = t2; best = d; }
    }
   } else {
    const float d = dist(p, lerp(A, B, t1));
    if (best == -1.0f || d < best) { seg = int(i); off = 0; bt = t1; best = d; }
   }
  } else {
   const float t = param(A, C);
   if (t < 0.0f || t >= 0.5f) {
    if (t >= 0.5f && t < 1.0f) { bt = 2.0f * (t - 0.5f); best = dist(p, lerp(B, C, bt)); seg = int(i); off = 1; }
   } else { bt = 2.0f * t; best = dist(p, lerp(A, B, bt)); seg = int(i); off = 0; }
  }
 }
 if (best < 0.0f) return dist(p, P(n - 1)) < dist(p, P(0)) ? P(n - 1) : P(0);
 return lerp(P(std::size_t(seg + off)), P(std::size_t(seg + off + 1)), bt);
}
// Effects::Player's GCN Peach Beach sea (PAL 0x8069C7F0 -> SetRoadState 0x8070B8DC),
// with the inputs the port's water effects use (main.cpp kartFx).
std::uint8_t seaState(const K::Kart::KartObject& kart) {
 const auto* dir = K::Field::ObjectDirector::Instance();
 if (!dir || !dir->psea() || !kart.status().onBit(K::Kart::eStatus::Diving)) return 0;
 const float sea = -dir->distAboveRisingWater(0);
 const float body = kart.pos().y + kart.pose()[1, 1] * 100.0f * kart.param()->stats().kclRot[30];
 const unsigned wheels = kart.tireCount(), rearL = kart.isBike() ? 1 : wheels - 2, rearR = wheels - 1;
 const float rear = std::min(kart.wheelEdgePos(std::uint16_t(rearL)).y, kart.wheelEdgePos(std::uint16_t(rearR)).y);
 bool deep = kart.pos().y - kart.bsp().initialYPos + 50.0f < sea;
 for (unsigned w : {rearL, rearR}) {
  const auto& c = kart.collisionData(std::uint16_t(w));
  deep |= c.bFloor && (std::uint32_t(c.closestFloorFlags) & (1u << 11));
 }
 if (body >= sea && rear < sea) return deep ? 2 : 1;
 return 3;
}
}

void fillRaceAudioFrame(RaceAudioFrame& f, snd::Vec3f eye, snd::Vec3f target, snd::Vec3f up, bool preview) {
 f.eye = eye; f.target = target; f.up = up;
 // RaceMgr::UpdatePlayerMatrix (PAL fn_1_200DE4 0x80711198) scales eye -> target by
 // the camera mode RaceMgr::Init (0x80710A00) copies from RaceConfig +0xB74: 0.99
 // (Audio::Manager +0x8BC) in mode 0, 0.0 in 7-12, else 0.2 (0x808A1F00). Mode 5 is
 // only the Demo section (the course preview): RaceInfo_update (0x80533374) writes 0
 // before the race section's initRace, and Time Trial, Restart (0x8085A0F4 case 2)
 // and Watch Replay (GhostManagerPage_setupGhostReplay 0x805E1D5C) start in 0.
 f.listenerScale = preview ? 0.2f : 0.99f;
 const auto* manager = K::Kart::KartObjectManager::Instance();
 const auto& scenario = K::System::RaceConfig::Instance()->raceScenario();
 f.karts = std::uint8_t(std::min<unsigned>(scenario.playerCount, 12));
 for (unsigned i = 0; i < f.karts; ++i) {
  RaceAudioKart& k = f.kart[i];
  auto* kart = manager->object(i);
  const auto& player = scenario.players[i];
  k.present = kart != nullptr;
  if (!kart) continue;
  const auto p = kart->pos();
  k.position = {p.x, p.y, p.z};
  const auto& v = kart->dynamics()->velocity();
  k.velocity = {v.x, v.y, v.z};
  auto* move = kart->move();
  // EngineMgr::Init (PAL fn_1_1EAAAC 0x806FAE60) reads KartObjectProxy::getBaseSpeed
  // (0x805910B0, KartMove +0x14): the stat speed, never the AI speed (+0x1C) that
  // Kinoko's baseSpeed() returns for CPUs and for the player after the goal.
  k.speed = move->speed(); k.speedRatioCapped = move->speedRatioCapped(); k.baseSpeed = move->statBaseSpeed();
  k.startBoostCharge = kart->state()->startBoostCharge();
  // A half-pipe trick start (retail fn_1_64AAC, Kinoko KartHalfPipe::activateTrick
  // setting ZipperTrick): Kinoko cues no sound there, so take the rising edge.
  const bool zipperTrick = kart->status().onBit(K::Kart::eStatus::ZipperTrick);
  if (zipperTrick && !(k.status[47 / 32] & (1u << (47 % 32)))) k.cues |= cue::ZipperTrick;
  // fn_1_71034 (0x805813E8), the bit-82 step: on its first frame (KartMove +0x160
  // still 1.0) it plays SE_VCL_BTL_NO_BALLOON on the kart's KartActor, the frame
  // bit 82 is set. Kinoko sets it for a finished ghost 70 frames after its goal.
  const bool vanishing = kart->status().onBit(K::Kart::eStatus::Vanishing);
  if (vanishing && !(k.status[82 / 32] & (1u << (82 % 32)))) k.cues |= cue::Vanish;
  std::memcpy(k.status, &kart->status(), sizeof k.status);
  k.moveFlags = move->ssmtCharged() ? 4 : 0;
  k.driftState = std::uint8_t(move->driftState());
  k.wheels = std::uint8_t(std::min<unsigned>(kart->tireCount(), 4));
  for (unsigned w = 0; w < k.wheels; ++w) {
   const auto& c = kart->tirePhysics(std::uint16_t(w))->hitboxGroup()->collisionData();
   const std::uint32_t mask = std::uint32_t(c.closestFloorFlags);
   std::uint8_t type = 0;
   for (int b = 0; b < 32; ++b) if (mask & (1u << b)) { type = std::uint8_t(b); break; }
   k.floorType[w] = type; k.floorVariant[w] = std::uint16_t(c.closestFloorSettings);
  }
  // The body's wall contact as Effects::Player::UpdateFromKartCollision sees it.
  const auto& body = kart->collisionData();
  k.wallType = std::uint8_t(body.closestWallFlags); k.wallVariant = std::uint8_t(body.closestWallSettings);
  k.wallContact = (body.bWall || body.bWall3) && !body.bInvisibleWallOnly;
  const auto taken = move->takeSoundCues();
  k.cues |= taken.cues;
  k.bumpedBy |= taken.bumpedBy;
  if (taken.cues & cue::Bump) k.bump = taken.bump;
  if (taken.cues & cue::Fall) k.fallVoice = taken.fallVoice;
  if (taken.cues & cue::SoundTrigger) k.soundZone = taken.soundZone;
  // The object contact callbacks in order (ObjectDirector::checkKartObjectCollision).
  for (unsigned n = 0; n < taken.objectSoundCount && k.objectHits < 4; ++n) {
   const auto* o = static_cast<const K::Field::ObjectBase*>(taken.objectSounds[n].object);
   if (!o) continue;
   RaceAudioObjectHit& h = k.objectHit[k.objectHits++];
   h.id = std::uint16_t(o->id()); h.wall = taken.objectSounds[n].wall;
   h.variant = o->mapObj() ? o->mapObj()->setting(0) : 0;
   h.slot = objectSlot(o);
  }
  if (taken.fallBoundary != 0xff) k.fallBoundary = taken.fallBoundary;
  for (unsigned n = 0; n < taken.kartSoundCount && k.kartSounds < 4; ++n) k.kartSound[k.kartSounds++] = taken.kartSounds[n];
  k.seaState = seaState(*kart);
  k.boostRampType = kart->state()->boostRampType();
  k.airtime = std::uint16_t(std::min<std::uint32_t>(kart->state()->airtime(), 0xffff));
  k.slipCharging = move->slipstreamCharging(); k.slipBoost = move->slipstreamBoost();
  k.actionCount = kart->action()->startCount(); k.action = std::int8_t(kart->action()->lastStarted());
  if (const auto* race = K::System::RaceManager::Instance()) {   // one RaceManager player per scenario racer
   const auto& p = race->player(i);
   k.finished = p.finished(); k.rank = std::uint8_t(p.finishOrder());
  }
  k.vehicle = std::uint8_t(player.vehicle); k.character = std::uint8_t(player.character);
  k.ghost = player.type == K::System::RaceConfig::Player::Type::Ghost;
  // Watch the Replay: the ghost driving player 0 is the one the camera follows.
  k.local = player.type == K::System::RaceConfig::Player::Type::Local || (i == 0 && player.type == K::System::RaceConfig::Player::Type::Ghost);
 }
}

void fillRaceAudioItems(RaceAudioFrame& f, const RaceItems& items) {
 f.items = 0;
 const auto& actors = items.actors();
 for (unsigned i = 0; i < actors.size() && f.items < f.item.size(); ++i) {
  const ItemActor& a = actors[i];
  if (!a.active) continue;
  RaceAudioItem& o = f.item[f.items++];
  const Vec3 p = a.visualPosition();
  o.position = {p.x, p.y, p.z}; o.index = std::uint8_t(i); o.kind = std::uint8_t(a.kind);
  o.object = std::uint8_t(items::retailObjectId(a.kind));
  o.phase = a.guidance.phase; o.target = a.target;
  o.explosion = a.explosion; o.scattered = a.scattered; o.settled = a.settled;
 }
 for (const ItemSound& s : items.sounds()) {
  if (f.itemSounds >= f.itemSound.size()) break;
  RaceAudioItemSound& o = f.itemSound[f.itemSounds++];
  o.position = {s.position.x, s.position.y, s.position.z}; o.cue = std::uint8_t(s.cue);
  o.player = s.player; o.actor = s.actor; o.kind = std::uint8_t(s.kind); o.flag = s.flag; o.value = s.value;
  o.object = std::uint8_t(items::retailObjectId(s.kind));
 }
 f.roulette = 0;
 for (unsigned i = 0; i < 12 && i < f.karts; ++i) {
  const ItemHud& h = items.hud(i);
  if (h.roulette) f.roulette |= std::uint16_t(1u << i);
  const bool spin = h.deployed && (h.heldKind == ItemKind::TripleGreenShell || h.heldKind == ItemKind::TripleRedShell);
  f.orbit[i] = spin ? h.heldCharges : 0;
  f.cloud[i] = h.cloud;
 }
}

void resetRaceAudioObjects() {
 registry = ObjectRegistry{};
 K::Field::ObjectBase::SoundCue drop[96];
 while (K::Field::ObjectBase::TakeSoundCues(drop, 96) == 96) {}
}

void fillRaceAudioObjects(RaceAudioFrame& f) {
 const auto* dir = K::Field::ObjectDirector::Instance();
 if (!dir) { f.objects = 0; return; }
 // Both directors: drivable objects (escalators, belts, bridges) live apart.
 const auto& objects = dir->objects();
 const auto* drivables = K::Field::ObjectDrivableDirector::Instance();
 const std::size_t drivableCount = drivables ? drivables->objects().size() : 0;
 if (objects.size() < registry.scanned || drivableCount < registry.scannedDrivable) registry = ObjectRegistry{};
 auto scan = [&](const K::Field::ObjectBase* o) {
  const int m = dir->flowTable().soundMode(o->id());
  if ((m == 1 || m == 3) && registry.find(o) < 0) registry.add(o, nullptr, std::uint16_t(o->id()), m);
 };
 for (std::size_t i = registry.scanned; i < objects.size(); ++i) scan(objects[i]);
 for (std::size_t i = registry.scannedDrivable; i < drivableCount; ++i) scan(drivables->objects()[i]);
 registry.scanned = objects.size();
 registry.scannedDrivable = drivableCount;
 // The course's own GOBJ list, once (objects of the race's single local player).
 const auto* map = K::System::CourseMap::Instance();
 if (!registry.courseScanned && map) {
  registry.courseScanned = true;
  for (std::uint16_t i = 0; i < map->getGeoObjCount(); ++i) {
   const auto* g = map->getGeoObj(i);
   if (!g || !(g->presenceFlag() & 1)) continue;
   const std::uint16_t id = g->id();
   // EffectsMgr (PAL 0x8067C8F8): the splash effects the course's objects bring.
   if (id == 0x7) registry.pocha |= 1; else if (id == 0x11) registry.pocha |= 2; else if (id == 0x12) registry.pocha |= 4;
   const bool idle = idleLoopObject(id) && K::Field::IsObjectBlacklisted(id);
   const bool emitter = emitterObject(id) && g->pathId() >= 0 && map->getPointInfo(std::uint16_t(g->pathId())) &&
                        map->getPointInfo(std::uint16_t(g->pathId()))->pointCount() > 0;
   if (idle || emitter) registry.add(nullptr, g, id, 1);
  }
 }
 f.pocha = registry.pocha;
 K::Field::ObjectBase::SoundCue cues[96];
 const std::size_t n = K::Field::ObjectBase::TakeSoundCues(cues, 96);
 for (std::size_t i = 0; i < n; ++i) {
  const auto& c = cues[i];
  int slot = registry.find(c.object);
  if (slot < 0) { const int m = dir->flowTable().soundMode(c.object->id()); slot = registry.add(c.object, nullptr, std::uint16_t(c.object->id()), m == 3 ? 3 : 1); }
  if (slot < 0 || f.objectSounds >= f.objectSound.size()) continue;
  f.objectSound[f.objectSounds++] = {std::uint16_t(slot), c.sound, std::uint8_t(c.call), c.value};
 }
 // The emitters follow the listener (RaceMgr::UpdatePlayerMatrix's position, RaceMgr +0x144).
 const float scale = f.listenerScale;
 const EGG::Vector3f listener{f.eye.x + (f.target.x - f.eye.x) * scale, f.eye.y + (f.target.y - f.eye.y) * scale,
                              f.eye.z + (f.target.z - f.eye.z) * scale};
 f.objects = registry.count;
 for (int i = 0; i < registry.count; ++i) {
  if (const auto* o = registry.object[i]) {
   const auto& p = o->getPosition();
   f.object[i] = {{p.x, p.y, p.z}, o->scale().x, registry.id[i], registry.mode[i], registry.index[i]};
   continue;
  }
  const auto* g = registry.geo[i];
  EGG::Vector3f p = g->pos();
  if (emitterObject(registry.id[i]) && map) {
   if (const auto* route = map->getPointInfo(std::uint16_t(g->pathId()))) p = nearestOnRoute(*route, listener);
  }
  f.object[i] = {{p.x, p.y, p.z}, g->scale().x, registry.id[i], registry.mode[i], registry.index[i]};
 }
}
}
