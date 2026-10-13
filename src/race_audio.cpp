// Race actors' sounds on the audio thread. Addresses are USA StaticR
// (docs/AUDIO_SOURCE.md "Karts"); the per-kart state mirrors Audio::KartActor,
// Audio::EngineMgr and the sound side of Effects::Player field by field.
#include <mkw/race_audio.hpp>
#include <mkw/kart_wall_contact.hpp>
#include <new>
#include <mkw/race_voice_tables.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkw {
using namespace snd;
namespace {
// EngineMgr table 8089caa8: vehicle -> engine family's first sound (UP, LOOP,
// STR, OFF, DOWN, IDLE, FUKASHI), pitch multiplier, maximum pitch.
struct EngineSpec { u16 base; f32 pitch, max; };
constexpr EngineSpec Engines[36] = {
 {0x12f, 1.1f, 2.0f}, {0x12f, 1.0f, 2.0f}, {0x12f, 0.8f, 2.0f}, {0x12f, 1.2f, 2.0f}, {0x128, 1.0f, 2.4f}, {0x128, 0.9f, 2.6f},
 {0x136, 1.0f, 2.6f}, {0x136, 1.0f, 2.6f}, {0x136, 0.9f, 2.6f}, {0x128, 1.0f, 2.2f}, {0x128, 1.0f, 2.4f}, {0x128, 1.0f, 2.6f},
 {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f},
 {0x144, 1.1f, 2.4f}, {0x144, 1.0f, 2.2f}, {0x144, 0.9f, 2.0f}, {0x14b, 1.0f, 1.8f}, {0x144, 0.9f, 1.8f}, {0x144, 0.9f, 1.8f},
 {0x13d, 1.1f, 1.9f}, {0x13d, 1.1f, 1.9f}, {0x144, 1.4f, 2.0f}, {0x14b, 1.1f, 1.8f}, {0x144, 1.0f, 1.8f}, {0x14b, 0.9f, 1.8f},
 {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}, {0x128, 1.0f, 2.6f}};
// EngineMgr::Init class factors (8089c9f0 / 9fc / a08) by engine class.
constexpr f32 ClassAccel[3] = {0.63f, 0.81f, 1.0f}, ClassDecel[3] = {0.44f, 0.63f, 0.81f}, ClassBrake[3] = {0.13f, 0.31f, 0.5f};
// EngineMgr::Update braking volume by speed ratio (8089ca14).
constexpr f32 BrakeVolume[17] = {0, 0, 0.24f, 0.31f, 0.39f, 0.47f, 0.55f, 0.63f, 0.71f, 0.79f, 0.87f, 0.94f, 1, 1, 1, 1, 1};

// 8089cdb8..8089d338: GetDriftAndSSMTRoadSoundId (+0), GetLandingSoundId (+82),
// GetRoadSoundId (+164), GetBrakingSoundId (+246), PlayWallCollisionSound (+328).
// Generated from StaticR; a table is indexed past its end exactly as retail does.
constexpr u16 KclSound[352] = {
 0x153, 0x156, 0x158, 0x154, 0x15d, 0x164, 0x15f, 0x161, 0x157, 0x15e, 0x15b, 0x157,
 0x15b, 0x160, 0x156, 0x162, 0x15e, 0x157, 0x161, 0x160, 0x15e, 0x000, 0x159, 0x156,
 0x15c, 0x164, 0x15b, 0x159, 0x159, 0x156, 0x15c, 0x15b, 0x159, 0x15e, 0x158, 0x158,
 0x159, 0x156, 0x163, 0x15c, 0x15b, 0x159, 0x158, 0x15e, 0x159, 0x156, 0x163, 0x15b,
 0x15b, 0x164, 0x159, 0x156, 0x15a, 0x163, 0x15c, 0x15c, 0x15c, 0x15c, 0x157, 0x157,
 0x153, 0x153, 0x153, 0x153, 0x153, 0x153, 0x15d, 0x158, 0x15e, 0x156, 0x156, 0x162,
 0x153, 0x163, 0x15c, 0x159, 0x156, 0x164, 0x158, 0x15b, 0x152, 0x156, 0x166, 0x168,
 0x168, 0x168, 0x166, 0x16b, 0x16c, 0x166, 0x166, 0x170, 0x16a, 0x166, 0x16a, 0x169,
 0x168, 0x172, 0x171, 0x168, 0x166, 0x169, 0x170, 0x31e, 0x167, 0x168, 0x16e, 0x16b,
 0x16a, 0x167, 0x167, 0x168, 0x16e, 0x16a, 0x167, 0x170, 0x168, 0x166, 0x167, 0x168,
 0x16d, 0x16b, 0x16a, 0x167, 0x166, 0x170, 0x167, 0x168, 0x16d, 0x16a, 0x16a, 0x16b,
 0x167, 0x168, 0x169, 0x16d, 0x16e, 0x16e, 0x16e, 0x16e, 0x166, 0x166, 0x166, 0x166,
 0x166, 0x166, 0x166, 0x166, 0x166, 0x166, 0x170, 0x168, 0x166, 0x172, 0x166, 0x16d,
 0x16f, 0x167, 0x168, 0x16b, 0x166, 0x16a, 0x165, 0x168, 0x173, 0x174, 0x177, 0x177,
 0x17c, 0x17b, 0x173, 0x173, 0x173, 0x17a, 0x175, 0x175, 0x175, 0x173, 0x174, 0x173,
 0x17a, 0x173, 0x173, 0x173, 0x17a, 0x000, 0x178, 0x174, 0x176, 0x17b, 0x175, 0x178,
 0x178, 0x174, 0x176, 0x175, 0x178, 0x17a, 0x174, 0x177, 0x178, 0x174, 0x179, 0x176,
 0x175, 0x178, 0x177, 0x17a, 0x178, 0x174, 0x179, 0x175, 0x175, 0x17b, 0x178, 0x174,
 0x173, 0x179, 0x176, 0x176, 0x176, 0x176, 0x173, 0x173, 0x173, 0x173, 0x173, 0x173,
 0x173, 0x173, 0x17c, 0x177, 0x17a, 0x174, 0x173, 0x173, 0x173, 0x179, 0x176, 0x178,
 0x174, 0x17b, 0x177, 0x175, 0x173, 0x174, 0x189, 0x18b, 0x18c, 0x18c, 0x18b, 0x18b,
 0x192, 0x194, 0x189, 0x191, 0x18e, 0x18b, 0x18e, 0x193, 0x18b, 0x195, 0x191, 0x189,
 0x194, 0x193, 0x191, 0x000, 0x18b, 0x18b, 0x18f, 0x18b, 0x18e, 0x18b, 0x18b, 0x18b,
 0x18f, 0x18e, 0x18b, 0x191, 0x18b, 0x18c, 0x18b, 0x18b, 0x196, 0x18f, 0x18e, 0x18b,
 0x18c, 0x191, 0x18b, 0x18b, 0x196, 0x18e, 0x18e, 0x18b, 0x18b, 0x18b, 0x18d, 0x196,
 0x18f, 0x18f, 0x18f, 0x18f, 0x189, 0x189, 0x189, 0x189, 0x189, 0x189, 0x189, 0x189,
 0x18b, 0x18c, 0x191, 0x18b, 0x18b, 0x195, 0x189, 0x196, 0x18f, 0x18b, 0x18b, 0x18b,
 0x18c, 0x18e, 0x188, 0x18b, 0x180, 0x183, 0x186, 0x181, 0x184, 0x182, 0x171, 0x185,
 0x182, 0x183, 0x180, 0x000, 0x187, 0x182, 0x186, 0x31e, 0x180, 0x183, 0x186, 0x181,
 0x184, 0x182, 0x171, 0x000,
};
enum KclSet { DriftSet = 0, LandingSet = 82, RoadSet = 164, BrakingSet = 246, WallSet = 328 };
// UpdateKCLValues floor priority (8089cd28): the wheels' first match wins.
constexpr u8 KclPriority[16] = {0x8, 0x7, 0x6, 0x16, 0x15, 0x13, 0xb, 0x11, 0x5, 0x4, 0x3, 0x2, 0x1, 0x1d, 0x17, 0x0};
// The four Get*SoundId switches share one layout: KCL type -> table offset in
// words; type 0x15 and unlisted types read a fixed entry.
u32 kclSound(int set, int type, int variant) {
 int at = 0; bool fixed = false;
 switch (type) {
 case 0x00: at = 0; break;  case 0x17: at = 8; break;  case 0x1d: at = 16; break; case 0x01: at = 22; break;
 case 0x02: at = 28; break; case 0x03: at = 36; break; case 0x04: at = 44; break; case 0x05: at = 52; break;
 case 0x0b: at = 54; break; case 0x13: at = 58; break; case 0x15: at = 60; fixed = true; break; case 0x16: at = 66; break;
 default: fixed = true; break;
 }
 const int i = set + at + (fixed ? 0 : variant);
 return i >= 0 && i < int(sizeof KclSound / sizeof KclSound[0]) ? KclSound[i] : 0;
}

// RaceMgr::SortAndToggleActors: active karts by local player count (8089d4cc).
constexpr int KartsActive = 4;
constexpr f32 Unset = 4294967296.0f;      // EngineMgr's "no pitch yet" (8089ca70)
namespace sid { enum : u32 {
 StarState = 0x10e, MegaState = 0x112, KillerOff = 0x11b, KillerFly = 0x11c, KinokoJump = 0x212,
 SlipBoard = 0x15d, SlipSand = 0x159, GndFirst = 0x166, GndLast = 0x172, RunNone = 0x173, RunKibashi = 0x17c,
 ColCar = 0x17f, ColShigemi = 0x182, MiniJump = 0x197, JumpAction = 0x198, HibanaBlue = 0x19a, HibanaRed = 0x19b,
 Dash = 0x19c, DashJ = 0x19d, LDashJ = 0x19e, HornFirst = 0x1ab, Cannon = 0x1d0, CannonMove = 0x1d1, CoinBara = 0x1f4,
 WanwanAtk = 0x233, ChoropuGnd = 0x267, HpipeCheer = 0x2b9, HpipeCheerL = 0x2ba, RbwStarRing = 0x2f9, RbwStarRingMove = 0x2fa,
 SkateCheer = 0x30d, SkateActionCheer = 0x30e, GondolaDashJ = 0x303, RcGoal = 0xdb, RcBattleFinish = 0xed,
 // Item effects on karts and the item managers' sounds.
 BoxHitFx = 0x10c, BoxSmoke = 0x10d, StarBarrier = 0x10f, BigKinokoUse = 0x111, BigKinokoRet = 0x113,
 ThndrUse = 0x114, ThndrBig = 0x116, ThndrState = 0x117, KillerOn = 0x11a, Pow1 = 0x11d,
 KumoOn = 0x120, KumoAttach = 0x121, KumoOff = 0x122, KumoSign = 0x123, KumoThndr = 0x124,
 GesoStart = 0x125, GesoAtk = 0x126, GesoSumiOn = 0x127,
 DmgPress = 0x1a4, DmgPressRev = 0x1a5, SlashWind = 0x1a6, SlipStreamDash = 0x1a7, Spin = 0x1a8, PoihanaBlow = 0x261,
}; }
inline bool bit(const u32* w, int n) { return (w[n >> 5] >> (n & 31)) & 1; }
enum Status {
 Accelerate = 0, Brake = 1, DriftInput = 2, BeforeRespawn = 4, GroundStart = 9, AnyWheelCollision = 11, DriftAuto = 28,
 Wheelie = 29, InAction = 32, TriggerRespawn = 33, InCannon = 36, ChargingSSMT = 57, Star = 63, Shocked = 71, InRespawn = 77,
 Mega = 79, Crushed = 80, Bullet = 91,
};
f32 length(Vec3f v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// Audio::PitchModuler (8071036c): a slow deterministic pitch drift.
struct PitchModuler {
 f32 cur = 0.1f, limit = 0.1f, step = 0.005f, floor = 0.026303f;
 f32 update() {
  if (cur < limit) { cur += step; if (cur < limit) return cur; }
  else { cur -= step; if (cur > limit) return cur; }
  const f32 l = limit; cur = l;
  f32 next = l + l * l;
  if (next > 1.0f) next -= 1.0f;
  if (l < floor) next += floor;
  limit = next;
  return cur;
 }
};

// Audio::EngineMgr (806f5ef8 Init, 806f604c Update).
struct EngineMgr {
 EngineSpec spec{};
 f32 maxAccel = 0, maxDecel = 0, maxBrake = 0, cap = 0, kAccel = 0, kDecel = 0, kBrake = 0;
 f32 pitch = Unset, target = 0, delta = 0, baseSpeed = 1, volume = 0.8f;
 u32 sound = 0; bool wasAccel = false;
 int mode = 0;                  // +0x04: 0 start line, 1 braking, 2 accelerating, 3 off the throttle
 PitchModuler mod;
 void init(const RaceAudioKart& k, unsigned engineClass) {
  spec = Engines[std::min<unsigned>(k.vehicle, 35)];
  const unsigned c = std::min(engineClass, 2u);
  const f32 range = spec.max - 1.0f;
  maxAccel = 1.0f + range * ClassAccel[c]; maxDecel = 1.0f + range * ClassDecel[c]; maxBrake = 1.0f + range * ClassBrake[c];
  cap = 1.3f * maxAccel;
  baseSpeed = k.baseSpeed > 0 ? k.baseSpeed : 1.0f;
  kAccel = (maxAccel - 1.0f) / baseSpeed; kDecel = (maxDecel - 1.0f) / baseSpeed; kBrake = (maxBrake - 1.0f) / baseSpeed;
  pitch = Unset; target = 0; delta = 0; volume = 0.8f; sound = 0; wasAccel = false; mode = 0; mod = PitchModuler{};
 }
 // EngineMgr::CalcPitch (806f66d8): eases toward a new target over 8 frames
 // (25 above base speed), down over 40; clamped to [1, cap].
 void calcPitch(f32 goal, f32 speed) {
  f32 d = delta;
  if (pitch == Unset) { goal = 0.0f; target = 1.0f; d = 0.0f; }
  else if (std::fabs(pitch - goal) >= 0.005f) {
   if (std::fabs(target - goal) <= 0.005f) { if (d != 0.0f) goal = d + pitch; }
   else {
    target = goal;
    f32 div = 40.0f;
    if (pitch <= goal) div = baseSpeed < speed ? 25.0f : 8.0f;
    d = (goal - pitch) / div;
    goal = d + pitch;
   }
  } else { target = goal; d = 0.0f; }
  f32 p = cap;
  if (goal <= cap) { p = goal; if (goal < 1.0f) p = 1.0f; }
  delta = d; pitch = p;
 }
 // EngineMgr::CalcAccelerating (806f64dc).
 void accelerating(f32 speed, f32 ratio) {
  calcPitch(1.0f + kAccel * std::fabs(speed), speed);
  const u32 base = spec.base;
  if (sound == base + 6) sound = base + 1;
  else if (speed >= 0.0f) { if (!wasAccel) sound = ratio >= 0.2f ? base + 1 : base; }
  else sound = base;
  volume = ratio >= 0.7f ? (ratio < 1.0f ? 0.7f + (1.0f - ratio) : 0.7f) : 1.0f;
 }
 // EngineMgr::Update; returns the sound and the pitch to play it at.
 void update(const RaceAudioKart& k, int raceState, bool ssmtRoad, u32 brakingSound, f32& outPitch) {
  const u32* w = k.status;
  const bool accel = bit(w, Accelerate), brake = bit(w, Brake);
  const f32 speed = k.speed, ratio = k.speedRatioCapped;
  if (raceState < 4 || raceState > 7) {
   if (raceState == 3) {
    // EngineMgr::CalcSoundId (806f6394): revving on the start line.
    pitch = 1.0f + k.startBoostCharge;
    sound = spec.base + (k.startBoostCharge <= 0.2f ? 5 : 6);
    volume = 1.0f; mode = 0;
    goto play;
   }
   if (raceState != 1) goto play;
  }
  if (!ssmtRoad) {
   if (accel || bit(w, DriftInput) || !brake || speed <= 0.0f || !bit(w, AnyWheelCollision)) {
    if (accel || (brake && speed < 0.0f)) { accelerating(speed, ratio); mode = 2; }
    else {
     calcPitch(1.0f + kDecel * std::fabs(speed), speed);
     if (ratio >= 0.1f) { volume = 0.85f; sound = spec.base + (ratio >= 0.3f ? 3 : 4); }
     else { volume = 1.0f; sound = spec.base + 5; }
     mode = 3;
    }
   } else {
    pitch = std::min(1.0f + kBrake * speed, maxBrake);
    volume = BrakeVolume[std::clamp(int(16.0f * ratio), 0, 16)];
    sound = brakingSound; mode = 1;
   }
  } else { volume = 1.0f; pitch = 1.5f; sound = spec.base + 6; }
 play:
  outPitch = pitch * spec.pitch + mod.update() / 5.0f;
  wasAccel = accel;
 }
};

// One sound slot an actor owns, with its 3D history.
struct Slot {
 Handle h;
 bool flat = false;             // started without an actor (the 2D RSARPlayer)
 Sound3DHistory history;
 f32 lastAmbientVolume = 1;
 int lastAmbientPriority = 0;
};

struct KartAudio {
 bool present = false, local = false;
 bool muted = false;            // LinkedRaceActor+0x1d (KartActor+0xb1): toggled off, far from the listener
 Actor3D actor; bool placed = false;
 EngineMgr engine;
 Slot limited[4], engineSlot, driftSlot, mtSlot, roadSlot;   // +0x80..0x8c, +0xbc, +0xc0, +0xc4, +0xc8
 // KartActor fields.
 int roadType = 0, roadVariant = 0;   // +0xf0, +0xf2 (UpdateKCLValues)
 int roadState = 0;                   // +0xf8: 1 shallow water, 2-3 no road sound (SetRoadState)
 bool onGround = false, ssmtRoad = false, bullet = false, killerOffPending = false;   // +0xe8, +0xb4, +0xd8, +0xd9
 u8 driftState = 0;                   // +0xe9
 s16 plankDistance = 0; u8 plankAlt = 0;   // +0xd6, +0xd5
 s8 bumpFrames = 0; u8 hornCount = 0;      // +0xd3 (capped 126), +0xd4
 u16 jumpPadTimer = 0, jumpPadCount = 0;   // +0xcc, +0xce
 bool wasBullet = false;              // Bullet Bill status last frame (Kart::Killer on/off)
 // Effects::Player fields.
 bool fxBeforeRespawn = false;        // +0x96
 int thunderA = 690, thunderB = 540;  // +0x4c, +0x50: the shock effect counters (Effects::Player::Reset)
 f32 fxSpeed = 0;                     // +0x6e8: |velocity|
 s8 wallFrames = 0, bushFrames = 0, bumpFxFrames = 0;   // +0x7b4, +0x7b7, +0x7b8 (capped 100)
 template <class F> void eachSlot(F f) { for (auto& s : limited) f(s); f(engineSlot); f(driftSlot); f(mtSlot); f(roadSlot); }
};

// ---- Audio::CharacterActor (ctor 8085f4f8, Update 8085fbd0) ----
constexpr u32 VoDummy = 914;    // VO_DUMMY: a picker with no voice for this type
// A small deterministic generator per racer stands in for EGG::Random.
struct Rng {
 u32 x = 0x9e3779b9u;
 u32 next(u32 n) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return n ? x % n : 0; }
};
// Audio::RandomSoundPicker (80864d38): a random id of [first, last] without a
// repeat until every id has played (fn_1_356DE0 pick, fn_1_357118 note).
struct Picker {
 u32 first = VoDummy; u8 size = 1; s32 last = -1; u32 mask = 0; s16 used = 0;
 void set(u32 a, u32 z) { first = a; size = u8(z - a + 1); last = -1; mask = 0; used = 0; }
 s32 pick(Rng& rng) const {
  if (used >= size) return -1;
  if (size == 1) return s32(first);
  if (size == 2) return s32(first + rng.next(2));
  const int avail = size - used;
  s32 id = last;
  if (avail == 1) {
   for (int i = 0; i < size; ++i) if (!(mask & (1u << i))) id = s32(first) + i;
   return id;
  }
  do {
   const int r = int(rng.next(u32(avail)));
   for (int i = 0, n = 0; i < size; ++i) if (!(mask & (1u << i))) { if (n == r) id = s32(first) + i; ++n; }
  } while (id == last);
  return id;
 }
 void note(s32 id) {
  if (id == -1 || size < 2) return;
  const u32 b = 1u << u32(id - s32(first));
  if (!(mask & b)) { mask |= b; ++used; }
  if (used >= size) { mask = 0; used = 0; }
  last = id;
 }
};
// EGG::SimpleAudioTrack (80213ee8) on the voice handle's pitch: moves over a
// number of frames, applied only while moving (SetValueSmoothAsync, Calc).
struct PitchTrack {
 f32 current = 1, target = 1, step = 0; int frames = 0;
 bool set(f32 v, int over) {        // true: apply `current` now
  if (current == v) return false;
  target = v; frames = over;
  if (over == 0) { current = v; return true; }
  step = (current - v) / f32(over);
  return false;
 }
 bool calc(bool attached) {
  if (!attached) { frames = 0; return false; }
  if (!frames) return false;
  if (--frames == 0) current = target; else current -= step;
  return true;
 }
};
struct VoiceAudio {
 bool enabled = false, local = false;   // +0x6ff, +0x6fa
 u8 character = 0;                      // +0x9c
 Actor3D actor; bool placed = false;
 Picker picker[29];                     // +0x150
 Slot main, echo;                       // +0xf8, +0x104
 Slot limited[4];                       // +0x80..+0x8c (course cheers, Funky Kong's extras)
 int curType = -1, curFrames = -1;      // +0x6c8, +0x6d0
 int pendingType = -1, pendingDelay = 0;   // +0xfc, +0x100
 s32 echoId = -1; int echoDelay = 0; f32 echoVolume = 0; bool echoPosition = false, echo2D = false;   // +0x108..+0x118
 int state = 0;                         // +0x6cc: 0 none, 1 shocked, 2 Mega, 3 Star, 4 crushed
 int wobbleSide = 0, wobbleFrames = 0;  // +0x6f0, +0x6f4
 f32 basePitch = 1.0f;                  // +0x6e4
 PitchTrack pitch;                      // +0x138
 int slipFrames = 0; bool inSlip = false;   // +0x6d8, +0x6f8
 int overtakeCooldown = 0;              // +0x6dc
 int finishFrames = 0; bool finishDone = false;   // +0x6d4, +0x6f9
 bool echoNext = false, fellInWater = false;      // +0x6fe, +0x700
 u32 actionCount = 0; u16 airtime = 0, airtimeBefore = 0;   // action and driver-airtime trackers
 bool wasStar = false, wasMega = false, wasSlipBoost = false;
 Rng rng;
};

// ---- Audio::ItemActor (806fd738), one per item object (Item::Obj+0x180) ----
struct ItemObjAudio {
 Actor3D actor; bool placed = false, live = false, muted = false;
 u8 kind = 0xff;
 Slot limited[2];
};
// Item::ObjProperties +0x60 / +0x64 / +0x68 by retail object id (templates
// copied by Item::InitAllObjProperties 80787af0): broken by another object,
// broken against a racer, landed. 0: none.
constexpr u16 ItemObjSound[15][3] = {
 {0xfa, 0xfb, 0xfc}, {0xfa, 0xfb, 0xfc}, {0, 0, 0x101}, {0, 0, 0x102}, {0, 0, 0x102}, {0, 0, 0}, {0, 0, 0x102},
 {0, 0x10b, 0x10a}, {0, 0, 0}, {0, 0, 0x107}, {0, 0, 0x102}, {0, 0, 0x102}, {0, 0, 0}, {0, 0, 0x102}, {0, 0, 0}};
enum ItemCue : u8 {
 ItemThrow, ItemLand, ItemBounce, ItemWallBounce, ItemBreak, ItemHit, ItemExplode, ItemEquip, ItemDecide, ItemRelease,
 ItemFakeBox, ItemBarrier, ItemInk, ItemLightning, ItemStruck, ItemPow, ItemBlooperUse, ItemBlooperAttack,
 ItemCloudOn, ItemCloudAttach, ItemCloudStrike, ItemCloudOff, ItemBoxBreak, ItemHitSuccess,
};
// Item::ObjKumo (the Thunder Cloud) over its carrier: its ItemActor, the carrier,
// the frames since the strike (-1 before it) and the warning flash (+0x1d0).
struct CloudAudio { ItemObjAudio o; int carrier = -1, struck = -1; bool flash = false; };
// Audio::Ambience (806f86bc), three in RaceAmbienceMgr: a 2D loop and the local
// racer's volume (EGG::AudioTrack; Update 806f88b0 stops the loop at 0).
struct AmbienceAudio { Slot slot; AudioTrack track; };
// Audio::ObjectActor (807060e8): four sound slots on an object's position.
struct ObjActorAudio { Actor3D actor; bool placed = false; Slot limited[4]; };
// Audio::ObjectActor (807060e8) and Audio::ObjectActorAnim (806fc04c) on a
// course object (ObjFlow sound mode 1 / 3): four limited slots, and for the
// animated actors RaceAnimActor's StartSound handle (+0xf8).
struct ObjAudio {
 Actor3D actor; bool placed = false, live = false;
 bool muted = false;    // LinkedRaceActor +0x1d (+0xb1): toggled off
 bool freeze = false;   // +0xb0: the position is not followed
 u16 id = 0; u8 mode = 0, index = 0;
 Slot limited[4];
 Slot voice;
};
// DisableConditional (807077e4 / 806fd3c0): userParam bits 0-3 by object id;
// UpdateUserParam3 (80707c4c / 806fd55c): bits 4-7. Defaults 2 and 1.
u32 objectUserParam(u16 id, u8 mode, f32 scale, unsigned course) {
 u32 dist = 2, inner = 1;
 if (mode == 3) {
  switch (id) {
  case 0xf0: dist = 8; inner = 3; break;            // BossHanachan
  case 0xf2: dist = 3; break;                       // BossHanachanHead (mission mode aside)
  case 0x1ab: dist = 6; inner = 2; break;           // sanbo_big
  case 0x1478: dist = 0xe; inner = 0xb; break;
  case 0x20c: dist = 0xb; break;                    // quicksand
  case 0x171: dist = 8; break;                      // koopaFigure
  case 0xe2: dist = 3; break;                       // hanachan
  case 0xe9: dist = 4; break;                       // Hwanwan
  case 0xeb: dist = 0xc; break;                     // Twanwan
  case 0xdb: case 0xcb: dist = 4; break;            // dossunc, WLwallGC
  default: break;
  }
  return dist | inner << 4;
 }
 switch (id) {
 case 0x205: case 0xea: case 0x20d: dist = 9; break;
 case 0x1a1: case 0x1fd: case 0x219a: case 0x210: dist = 1; break;
 case 0x1a5: if (scale > 5.0f) dist = 7; break;     // koopaFirebar: object +0x3c = scale.x
 case 0x18b: dist = scale < 2.0f ? 3 : 9; break;    // KoopaFigure64
 case 0x1ff: dist = 0xe; inner = 4; break;          // TownBridgeDSc
 case 0x200: dist = 7; break;
 case 0x204: dist = 0xc; break;                     // aurora
 case 0x1f7: case 0x1fc: case 0xdd: dist = 6; break;
 case 0xd6: case 0x20b: dist = 0xa; break;
 case 0xcf: case 0xce: dist = 0xf; break;
 case 0x17a: dist = 4; break;
 case 0x140: dist = 8; break;
 case 0x165: case 0x185: case 0x2c6: case 0x2eb: case 0x2ec: case 0x2970: dist = 3; break;
 case 0x2c3: case 0x2bd: case 0x2be: case 0x2dc: case 0x2dd: dist = 3; break;   // 2 only in RaceData mode 0xb
 case 0x2c8: case 0x2ca: dist = 2; break;
 case 0x2c9: case 0x206: case 0x216: case 0x25c: case 0x25d: dist = 0xe; break;
 case 0x20a: dist = 0xe; inner = 7; break;
 case 0x212: dist = course == 0xc ? 3 : 6; break;   // FlamePole_v
 default: break;
 }
 if (id == 0x205) inner = 3;
 return dist | inner << 4;
}
// RaceMgr::SortAndToggleActors (8070b0b4), object list: these stay audible;
// the rest compete for the nearest four (8089d4dc, one local player).
bool objectAlwaysAudible(u16 id) {
 switch (id) {
 case 0x65: case 0x70: case 0x74: case 0x75: case 0x76: case 0xc9: case 0xce: case 0xd3: case 0xd5: case 0xeb: case 0xef:
 case 0xf0: case 0xf2: case 0x16a: case 0x16f: case 0x170: case 0x171: case 0x175: case 0x176: case 0x18c: case 0x19d:
 case 0x1a3: case 0x1ab: case 0x1ff: case 0x205: case 0x20a: case 0x20c: case 0x20d: case 0x211: case 0x2d8: case 0x2db:
 case 0x1478: case 0x2780: case 0x27e3:
  return true;
 default: return false;
 }
}
constexpr int ObjectsAudible = 4;
// Audio::GessoActor (806fe230): the Blooper on a local racer's screen, at its kart
// (GessoScreen::UpdateModel -> 806fe3e0). StartSoundLimited (806fe3f4) uses one slot.
struct GessoAudio { Actor3D actor; bool placed = false; Slot slot; };
// ObjKumo::Update (807d7990..807d7c9c): the cloud flashes before it strikes. Each
// second of its 600 frames has 0-4 groups of flash frames (USA 808a2f00, 808a2f28,
// 808a2f3c); from the ninth second the flashes repeat through the whole second.
inline bool cloudFlash(int timer) {
 static constexpr int Groups[10] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 4};
 static constexpr int Length[5] = {6, 6, 5, 5, 5}, Spacing[5] = {15, 15, 14, 13, 13};
 const int groups = std::min(Groups[std::min(timer / 60, 9)], 4);
 const int length = Length[groups], spacing = Spacing[groups], second = timer % 60;
 for (int g = 0; g < groups; ++g)
  for (int f = 0; f < length; ++f)
   if (second == g * spacing + f || (groups == 4 && f == second % spacing)) return true;
 return false;
}
enum ItemKindId : u8 { KindGreen = 0, KindRed = 1, KindBanana = 2, KindFakeBox = 3, KindBobomb = 6, KindBlue = 7 };
}

struct RaceAudioLayer::Impl {
 RaceAudioLayer* owner = nullptr;
 Engine3D e3d;
 Listener3D listener; bool listenerPlaced = false;
 std::array<KartAudio, 12> karts{};
 std::array<VoiceAudio, 12> voices{};
 std::array<ItemObjAudio, 96> items{};
 std::array<ItemObjAudio, 12> heldItems{};   // tethered objects (their own ItemActors in retail)
 Slot roulette, alarm;                       // RaceRSARPlayer 2D: SE_RC_ITEM_ROULETTE, ItemWarning
 Slot global;                                // RaceAmbienceMgr +0x18: PlayGlobalSound
 std::array<AmbienceAudio, 3> ambiences{};   // RaceAmbienceMgr +0x1c
 Slot oneShot;                               // RaceAmbienceMgr +0x178 (fn_1_1ED62C)
 ObjActorAudio lakitu;                       // Lakitu::Player +0x214
 std::array<ObjAudio, 128> objects{};        // course objects by registry slot
 std::array<ObjAudio, 4> boxActors{};        // item boxes just broken
 u8 nextBoxActor = 0;
 std::array<CloudAudio, 12> clouds{};
 std::array<GessoAudio, 12> gesso{};
 bool itemToggleOdd = false;
 RaceAudioFrame frame{};
 bool toggleOdd = false;

 // nw4r Sound3DActor::SetPosition: velocity is the change since the last frame.
 static void place(Actor3D& a, bool& placed, Vec3f p) {
  a.velocity = placed ? Vec3f{p.x - a.position.x, p.y - a.position.y, p.z - a.position.z} : Vec3f{};
  a.position = p; placed = true;
 }
 Ambient ambientFor(Engine& e, u32 id, const Actor3D& a, Sound3DHistory* h) {
  const SoundEntry* info = e.archive() ? e.archive()->sound(id) : nullptr;
  return info ? e3d.update(*info, id, a, listener, h) : Ambient{};
 }
 // RaceActor::detail_UpdateAmbientArg + BasicSound's ambient update: every
 // actor sound follows its actor from the previous frame's position.
 // QA: a kart sound started, by family.
 void note(u32 id) {
  using L = RaceAudioLayer;
  int f = L::Other;
  if (id >= 0x152 && id <= 0x164) f = L::Slip;
  else if (id >= 0x165 && id <= 0x172) f = L::Landing;
  else if (id >= 0x173 && id <= 0x17d) f = L::Road;
  else if (id == sid::ColCar) f = L::Car;
  else if (id >= 0x17e && id <= 0x187) f = L::Wall;
  else if (id >= 0x188 && id <= 0x196) f = L::Brake;
  else if (id == sid::MiniJump) f = L::Hop;
  else if (id == sid::JumpAction) f = L::Trick;
  else if (id == sid::HibanaBlue || id == sid::HibanaRed) f = L::Spark;
  else if (id == sid::Dash) f = L::Dash;
  else if (id == sid::DashJ) f = L::DashJ;
  else if (id == sid::LDashJ || id == sid::GondolaDashJ || id == sid::KinokoJump) f = L::JumpPad;
  else if (id >= sid::HornFirst && id < sid::HornFirst + 36) f = L::Horn;
  else if (id == sid::CannonMove || id == sid::RbwStarRingMove) f = L::Cannon;
  else if ((id >= sid::BoxHitFx && id <= sid::GesoSumiOn) || (id >= sid::DmgPress && id <= sid::Spin) || id == sid::PoihanaBlow) f = L::Effect;
  if (owner) owner->started[f].fetch_add(1, std::memory_order_relaxed);
 }
 void updateAmbient(Engine& e, KartAudio& k) {
  k.eachSlot([&](Slot& s) {
   if (!e.attached(s.h)) { s.history = {}; return; }
   const Ambient a = ambientFor(e, e.soundId(s.h), k.actor, &s.history);
   e.setAmbient(s.h, a);
   s.lastAmbientVolume = a.volume; s.lastAmbientPriority = a.priority;
  });
 }

 Slot* limitedWith(Engine& e, KartAudio& k, u32 id) {
  for (auto& s : k.limited) if (e.attached(s.h) && e.soundId(s.h) == id) return &s;
  return nullptr;
 }
 // fn_1_1F2494: the slot whose sound has the lowest archive player priority,
 // if the new sound's is higher.
 static Slot* replaceable(Engine& e, Slot* slots, int n, u32 id) {
  const auto* arc = e.archive();
  const SoundEntry* nw = arc ? arc->sound(id) : nullptr;
  if (!nw) return nullptr;
  int lowest = 0x7f; Slot* pick = nullptr;
  for (int i = 0; i < n; ++i) {
   const SoundEntry* o = arc->sound(e.soundId(slots[i].h));
   const int p = o ? o->playerPriority : 0;
   if (p < lowest) { lowest = p; pick = &slots[i]; }
  }
  if (!pick || nw->playerPriority <= lowest) return nullptr;
  e.stop(pick->h, 0);
  return pick;
 }
 static Slot* freeIn(Engine& e, Slot* slots, int n, u32 id) {
  for (int i = 0; i < n; ++i) if (!e.attached(slots[i].h)) return &slots[i];
  return replaceable(e, slots, n, id);
 }
 Slot* freeLimited(Engine& e, KartAudio& k, u32 id) { return freeIn(e, k.limited, 4, id); }
 // KartActor::HoldSoundLimited (8070169c) over the four shared slots.
 Slot* holdLimited(Engine& e, KartAudio& k, u32 id) {
  if (k.muted && id != sid::StarState && id != sid::MegaState && id != sid::KillerFly) return nullptr;
  if (Slot* s = limitedWith(e, k, id)) { e.holdSound(s->h, id); return s; }
  Slot* slot = freeLimited(e, k, id);
  if (!slot) return nullptr;
  slot->history = {};
  Ambient a = ambientFor(e, id, k.actor, &slot->history);
  if (e.holdSound(slot->h, id, nullptr, &a)) note(id);
  return slot;
 }
 // KartActor::StartSoundLimited (80701934).
 Slot* startLimited(Engine& e, KartAudio& k, u32 id, f32 param = 0) {
  using namespace sid;
  if (k.muted && id != Cannon && id != CannonMove && id != RbwStarRing && id != RbwStarRingMove && id != RcGoal && id != RcBattleFinish)
   return nullptr;
  if (id == HpipeCheer || id == HpipeCheerL || id == SkateCheer || id == SkateActionCheer || id == JumpAction || id == MiniJump) {
   if (!k.local) return nullptr;       // MiniJump also needs RaceConfig game type 0 (a live race)
  }
  switch (id) {
  case ChoropuGnd: case WanwanAtk: case RbwStarRing: case CoinBara:
   if (limitedWith(e, k, id)) return nullptr;
   break;
  case JumpAction: if (frame.raceState == 3) return nullptr; break;
  case KillerOff: if (k.actor.userParam & 0x400) k.killerOffPending = true; break;
  case Dash: case DashJ: case LDashJ:
   for (auto& s : k.limited) if (e.attached(s.h) && e.soundId(s.h) == id) e.stop(s.h, 0);
   break;
  case SkateCheer: case SkateActionCheer: return nullptr;
  default: break;
  }
  Slot* slot = freeLimited(e, k, id);
  if (!slot) return nullptr;
  slot->history = {};
  Ambient a = ambientFor(e, id, k.actor, &slot->history);
  if (!e.startSound(slot->h, id, nullptr, &a)) return nullptr;
  note(id);
  if (id == RbwStarRing) e.setSeqVariable(slot->h, 0, s16(int(2.0f * param)));
  else if (id == MiniJump && bit(frame.kart[&k - karts.data()].status, Shocked)) { e.setPitch(slot->h, 2.0f); e.setVolume(slot->h, 0.8f); }
  return slot;
 }
 // KartActor::StartNewSoundLimited (80702fdc): only if not already playing.
 Slot* startNewLimited(Engine& e, KartAudio& k, u32 id) {
  return limitedWith(e, k, id) ? nullptr : startLimited(e, k, id);
 }
 // KartActor::HoldSound / StartSound (80702994 / 80702910) into a dedicated slot.
 void holdIn(Engine& e, KartAudio& k, Slot& s, u32 id) {
  if (id == 0 || id == sid::RunNone) return;
  if (e.attached(s.h) && e.soundId(s.h) == id) { e.holdSound(s.h, id); return; }
  s.history = {};
  Ambient a = ambientFor(e, id, k.actor, &s.history);
  if (e.holdSound(s.h, id, nullptr, &a)) note(id);
 }
 void startIn(Engine& e, KartAudio& k, Slot& s, u32 id) {
  if (id == 0) return;
  s.history = {};
  Ambient a = ambientFor(e, id, k.actor, &s.history);
  if (e.startSound(s.h, id, nullptr, &a)) note(id);   // a sound already in the slot plays on, detached
 }

 // KartActor::UpdateKCLValues (80704aec): the wheels' floor by priority, and
 // the highest variant among the wheels on it.
 static void updateKcl(KartAudio& k, const RaceAudioKart& in) {
  int best = 0xffff;
  for (int w = 0; w < in.wheels && w < 4; ++w)
   for (int j = 0; j < 16; ++j) if (KclPriority[j] == in.floorType[w]) { best = std::min(best, j); break; }
  k.roadType = best == 0xffff ? 0 : KclPriority[best];
  s16 v = 0;
  for (int w = 0; w < in.wheels && w < 4; ++w)
   if (in.floorType[w] == k.roadType && s16(in.floorVariant[w]) > v) v = s16(in.floorVariant[w]);
  k.roadVariant = v;
 }
 // KartActor::GetRoadSoundId / GetDriftAndSSMTRoadSoundId / GetBrakingSoundId /
 // GetLandingSoundId (807051c8 / 80704f4c / 80705318 / 8070509c).
 static u32 roadSound(const KartAudio& k) {
  if (k.roadState == 1) return 0x176;
  if (k.roadState == 2 || k.roadState == 3) return 0;
  return kclSound(RoadSet, k.roadType, k.roadVariant);
 }
 static u32 driftSound(const KartAudio& k) {
  if (k.roadState == 1) return 0x15c;
  if (k.roadState == 2 || k.roadState == 3) return 0;
  return kclSound(DriftSet, k.roadType, k.roadVariant);
 }
 static u32 brakingSound(const KartAudio& k) {
  if (k.roadState == 1) return 0x18f;
  if (k.roadState == 2 || k.roadState == 3) return 0;
  return kclSound(BrakingSet, k.roadType, k.roadVariant);
 }
 static u32 landingSound(const KartAudio& k) {
  return k.roadState == 1 ? 0x16e : kclSound(LandingSet, k.roadType, k.roadVariant);
 }
 // Wooden planks: alternate two clacks every 460 units driven (PlayRoadSound /
 // PlayMTSounds share the distance).
 void plank(Engine& e, KartAudio& k, f32 speed) {
  bool hit = false;
  const s16 d = s16(k.plankDistance + s16(int(speed)));
  k.plankDistance = d;
  if (d >= 460) { k.plankDistance = s16(d - 460); hit = true; }
  else if (d < -459) { k.plankDistance = s16(d + 460); hit = true; }
  if (!hit) return;
  startIn(e, k, k.roadSlot, sid::RunKibashi + k.plankAlt);
  k.plankAlt = k.plankAlt ? 0 : 1;
 }
 // KartActor::PlayMTSounds (80702a20): drift slip on the surface, MT sparks.
 void playMT(Engine& e, KartAudio& k, const RaceAudioKart& in) {
  if (in.finished || !k.onGround) return;
  if (bit(in.status, DriftAuto)) k.driftState = 1;
  if (k.driftState && owner) owner->driftFrames.fetch_add(1, std::memory_order_relaxed);
  if (!(in.moveFlags & 4)) {
   if (!k.driftState) return;
   if (!k.ssmtRoad) {
    const u32 id = driftSound(k);
    if (id == sid::SlipBoard) plank(e, k, in.speed);
    else holdIn(e, k, k.driftSlot, id);
   }
   if (k.driftState == 2) holdIn(e, k, k.mtSlot, sid::HibanaBlue);
   else if (k.driftState == 3) holdIn(e, k, k.mtSlot, sid::HibanaRed);
  } else {
   // A charged stand-still mini-turbo.
   u32 id = driftSound(k);
   if (id == sid::SlipBoard) id = sid::SlipSand;
   holdIn(e, k, k.driftSlot, id);
   holdIn(e, k, k.mtSlot, sid::HibanaBlue);
  }
 }
 // KartActor::PlayRoadSound (80702c6c): rolling on the surface.
 void playRoad(Engine& e, KartAudio& k, const RaceAudioKart& in) {
  if (!k.onGround || k.driftState || k.ssmtRoad || k.engine.mode == 1) return;
  for (u32 id = sid::GndFirst; id <= sid::GndLast; ++id) if (limitedWith(e, k, id)) return;   // a landing thud plays
  if (std::fabs(in.speed) < 3.0f) return;
  const u32 id = roadSound(k);
  if (!id) return;
  if (id == sid::RunKibashi) plank(e, k, in.speed);
  else holdIn(e, k, k.roadSlot, id);
 }
 // KartActor::PlayBoostSound / PlayRampSound (80702208 / 807023a4): restart
 // the dash only once it has played 48 sequence ticks.
 void playDash(Engine& e, KartAudio& k, u32 id) {
  if (Slot* s = limitedWith(e, k, id)) {
   if (e.seqTick(s->h) <= 0x30) return;
   e.stop(s->h, 0);
  }
  startLimited(e, k, id);
 }
 // fn_1_1F8B30 (80702540): jump pads. The bouncy mushrooms rise 0.1 in pitch
 // per bounce within 140 frames, up to four.
 void playJumpPad(Engine& e, KartAudio& k, bool mushroom) {
  if (mushroom) {
   if (limitedWith(e, k, sid::KinokoJump)) return;
   f32 pitch = 1.0f;
   if (k.jumpPadTimer) { if (k.jumpPadCount < 4) ++k.jumpPadCount; pitch = 1.0f + 0.1f * f32(k.jumpPadCount); }
   k.jumpPadTimer = 0x8c;
   if (Slot* s = startLimited(e, k, sid::KinokoJump)) e.setPitch(s->h, pitch);
   return;
  }
  const u32 id = k.roadVariant == 7 ? sid::GondolaDashJ : sid::LDashJ;
  if (!limitedWith(e, k, id)) startLimited(e, k, id);
 }
 // KartActor::PlayHornSound (80702808): this racer bumped another; every other
 // bump after 120 quiet frames honks.
 void playHorn(Engine& e, KartAudio& k, const RaceAudioKart& in) {
  if (bit(in.status, InCannon) || bit(in.status, InAction)) return;
  if (k.bumpFrames < 120) return;
  if (++k.hornCount == 2) { k.hornCount = 0; return; }
  startNewLimited(e, k, sid::HornFirst + std::min<u32>(in.vehicle, 35));
  k.bumpFrames = 0;
 }
 // KartActor::PlayWallCollisionSound (80704628) by the body's wall KCL.
 void playWall(Engine& e, KartAudio& k, const RaceAudioKart& in) {
  if (!in.wallType) return;
  const int table = in.wallType == 0x1e ? WallSet + 8 : in.wallType == 0x14 ? WallSet + 16 : WallSet;
  const int i = table + in.wallVariant;
  if (i < int(sizeof KclSound / sizeof KclSound[0]) && KclSound[i]) startLimited(e, k, KclSound[i]);
 }
 // Effects::Player::UpdateFromKartCollision (8069d1d8): which wall contacts count.
 static bool wallHit(const RaceAudioKart& in) { return countedWallContact(in.wallContact, in.wallType, in.wallVariant); }

 // During the kart's physics step: KartActor calls from Kinoko's cue points
 // and Effects::Player's collision callbacks (last frame's Effects values).
 void kartCues(Engine& e, int index) {
  KartAudio& k = karts[index];
  const RaceAudioKart& in = frame.kart[index];
  const u32 c = in.cues;
  if (c & cue::Boost) playDash(e, k, sid::Dash);
  if (c & cue::BoostRamp) playDash(e, k, sid::DashJ);
  if (c & (cue::JumpPad | cue::JumpPadMushroom)) playJumpPad(e, k, c & cue::JumpPadMushroom);
  if (!in.finished && (c & cue::Hop)) startLimited(e, k, sid::MiniJump);
  if ((c & cue::Cannon) && k.local) startLimited(e, k, frame.course == 0xd ? sid::RbwStarRingMove : sid::CannonMove);
  if (c & cue::BushWall) startLimited(e, k, sid::ColShigemi);
  if (c & cue::Horn) playHorn(e, k, in);
  if (c & cue::StartFail) { startLimited(e, k, 0x1a8); startLimited(e, k, 0x1a9); }   // fn_1_78EB8: SPIN, START_FAIL
  if (c & cue::StarHit) startNewLimited(e, k, 0x110);   // CheckKartCollision 8056b0b8: SE_ITM_STAR_HIT
  // The driver model starts its trick animation (fn_1_18BE08 0x80697D34, local kart).
  if (c & cue::JumpAction) startLimited(e, k, sid::JumpAction);
  // A half-pipe trick (fn_1_64AAC 0x805AF9E8): the course crowd on DK Summit
  // (SE_CN_BDX_HPIPE_CHEER_L) and course 0x23 (SE_CB_SKT_JUMP_ACTION_CHEER, which
  // StartSoundLimited declines).
  if (c & cue::ZipperTrick) {
   if (frame.course == 0x6) startLimited(e, k, sid::HpipeCheerL);
   else if (frame.course == 0x23) startLimited(e, k, sid::SkateActionCheer);
  }
  // Item effects (Kart::Movement, Kart::Damage), in ActivateMega's order.
  if (c & cue::MegaUse) startLimited(e, k, sid::BigKinokoUse);
  if (c & cue::Grow) startLimited(e, k, sid::ThndrBig);
  if (c & cue::PressEnd) startLimited(e, k, sid::DmgPressRev);
  if (c & cue::MegaEnd) startLimited(e, k, sid::BigKinokoRet);
  if (c & cue::Press) startLimited(e, k, sid::DmgPress);
  if (c & cue::Spin) startLimited(e, k, sid::Spin);
  if (c & cue::BlowSide) startNewLimited(e, k, sid::PoihanaBlow);
  // The Bullet Bill: Kart::Movement::ActivateBullet resets the kart (vf_0x2c 8057d11c:
  // SE_ITM_THNDR_BIG), Kart::Killer::Activate (805b08e4) and cancelBullet (805b11d0).
  const bool bullet = bit(in.status, Bullet);
  if (bullet && !k.wasBullet) { startLimited(e, k, sid::ThndrBig); startLimited(e, k, sid::KillerOn); }
  else if (!bullet && k.wasBullet) startLimited(e, k, sid::KillerOff);
  k.wasBullet = bullet;
  // Kart::Movement::UpdateSlipStream (80580aa0, 80580c64): the draft's wind while it
  // charges, then the slipstream boost.
  if (in.slipCharging) holdLimited(e, k, sid::SlashWind);
  else if (in.slipBoost) holdLimited(e, k, sid::SlipStreamDash);
  // Effects::Player OnKartCollision (806a1fa0), local racers only.
  if ((c & cue::Bumped) && k.local && !k.fxBeforeRespawn) {
   for (int p = 0; p < 12; ++p) {
    if (!(in.bumpedBy & (1u << p))) continue;
    const bool fast = k.fxSpeed > 30.0f || length(frame.kart[p].velocity) > 30.0f;
    if (!fast || k.bumpFxFrames <= 10) continue;
    // KartActor::PlayOtherKartCollisionSound (80705f40).
    if (!bit(in.status, Star) && !bit(in.status, Mega) && !bit(in.status, Bullet)) startNewLimited(e, k, sid::ColCar);
    k.bumpFxFrames = 0;
   }
  }
  // Effects::Player::OnBushCollision (806a221c).
  if (bushContact(in.wallContact, in.wallType, in.wallVariant) && k.local && k.bushFrames > 30 && in.speedRatioCapped >= 0.5f) {
   k.bushFrames = 0;
   startLimited(e, k, sid::ColShigemi);
  }
 }
 // Effects::Player::UpdateValues and the wall hit of UpdateNonItemEffects (80692654).
 void effects(Engine& e, int index) {
  KartAudio& k = karts[index];
  const RaceAudioKart& in = frame.kart[index];
  k.fxBeforeRespawn = bit(in.status, BeforeRespawn);
  k.fxSpeed = length(in.velocity);
  k.wallFrames = s8(std::min(k.wallFrames + 1, 100));
  k.bushFrames = s8(std::min(k.bushFrames + 1, 100));
  k.bumpFxFrames = s8(std::min(k.bumpFxFrames + 1, 100));
  if (!k.fxBeforeRespawn && wallHit(in) && k.fxSpeed > 30.0f && k.wallFrames > 30) {
   voiceRequest(e, index, 0x11);  // Kart::Link::PlayCharacterSound, then the wall
   playWall(e, k, in);
   k.wallFrames = 0;
  }
  // Effects::Player::UpdateThunderEffects (8069971c): a Lightning strike restarts counter A
  // (fn_1_288374); from frame 71 + the player's index until 690 a shocked racer's sparks
  // hold SE_ITM_THNDR_STATE (fn_1_199318). Counter B runs the same way (fn_1_199794), but
  // nothing in retail restarts it.
  if (k.fxBeforeRespawn) k.thunderA = k.thunderB = 690;
  const bool shocked = bit(in.status, Shocked);
  const bool atA = k.thunderA == 70;
  if (atA) k.thunderB = 540;
  const bool atB = k.thunderB == 70;
  if (atB) k.thunderA = 690;
  if (shocked && !atA && k.thunderB == 540 && k.thunderA >= index + 71 && k.thunderA < 690) holdLimited(e, k, sid::ThndrState);
  k.thunderA = std::min(k.thunderA + 1, 690);
  if (shocked && !atB && k.thunderA == 690 && k.thunderB >= index + 71 && k.thunderB < 540) holdLimited(e, k, sid::ThndrState);
  k.thunderB = std::min(k.thunderB + 1, 540);
 }

 // KartActor::Update (80700d2c).
 void kartUpdate(Engine& e, int index) {
  KartAudio& k = karts[index];
  const RaceAudioKart& in = frame.kart[index];
  place(k.actor, k.placed, in.position);
  const u32* w = in.status;
  // Respawning: KartActor::Reset clears the actor velocity (no Doppler jump).
  if (bit(w, BeforeRespawn) || bit(w, TriggerRespawn) || bit(w, InRespawn)) k.actor.velocity = {};
  if (bit(w, BeforeRespawn)) return;
  if (bit(w, TriggerRespawn))
   for (u32 id = sid::Dash; id <= sid::LDashJ; ++id) if (Slot* s = limitedWith(e, k, id)) e.stop(s->h, 10);
  updateKcl(k, in);
  k.onGround = bit(w, AnyWheelCollision);
  k.driftState = in.finished ? 0 : in.driftState;
  k.bumpFrames = s8(std::min(k.bumpFrames + 1, 0x7e));
  k.bullet = bit(w, Bullet);
  if (k.bullet) holdLimited(e, k, sid::KillerFly);
  if (k.killerOffPending && !limitedWith(e, k, sid::KillerOff)) { k.killerOffPending = false; k.actor.userParam &= ~0x400u; }
  if (k.muted || k.bullet) return;
  k.ssmtRoad = false;
  if (!in.finished && k.onGround && bit(w, ChargingSSMT)) {
   k.ssmtRoad = true;
   u32 id = driftSound(k);
   if (id == sid::SlipBoard) id = sid::SlipSand;
   holdIn(e, k, k.driftSlot, id);
  }
  playMT(e, k, in);
  playRoad(e, k, in);
  // Engine (EngineMgr::Update -> KartActor::PlayEngineSound 80705bc0).
  f32 pitch = 1;
  k.engine.update(in, frame.raceState, k.ssmtRoad, brakingSound(k), pitch);
  if (k.engine.sound) {
   holdIn(e, k, k.engineSlot, k.engine.sound);
   f32 volume = k.engine.volume;
   if (!in.local) volume *= 0.8f;          // other racers' engines with a HUD on screen
   if (bit(w, Mega)) { e.setPitch(k.engineSlot.h, 0.5f * pitch); e.setVolume(k.engineSlot.h, volume); }
   else if (bit(w, Shocked)) { e.setPitch(k.engineSlot.h, 2.0f * pitch); e.setVolume(k.engineSlot.h, 0.8f * volume); }
   else { e.setPitch(k.engineSlot.h, pitch); e.setVolume(k.engineSlot.h, volume); }
  }
  // A hard landing (retail status bit 95 with GroundStart), not on the start line.
  if (frame.raceState != 3 && bit(w, GroundStart) && (in.cues & cue::Landing)) startLimited(e, k, landingSound(k));
  if (k.jumpPadTimer) --k.jumpPadTimer;
  else k.jumpPadCount = 0;
  if (!in.local) nonLocal(e, k, in);
 }
 // KartActor::SetNonLocalVolumeAndPriority (80705468).
 void nonLocal(Engine& e, KartAudio& k, const RaceAudioKart& in) {
  k.eachSlot([&](Slot& s) {
   if (!e.attached(s.h)) return;
   e.setVolume(s.h, 0.8f * s.lastAmbientVolume);
   e.setPlayerPriority(s.h, std::max(0, int(0.8 * double(e.playerPriority(s.h) + s.lastAmbientPriority))));
  });
  if (frame.raceState != 4 && frame.raceState != 6) return;
  const u32* w = in.status;
  // Other racers' Star and Mega themes, unless the player's own already plays.
  if (!bit(w, Star)) { if (bit(w, Mega) && frame.alteration != 0) holdLimited(e, k, sid::MegaState); }
  else if (frame.alteration != 1) holdLimited(e, k, sid::StarState);
 }
 // RaceMgr::SortAndToggleActors (8070b0b4): karts on alternate frames; the
 // nearest by Manhattan distance to the listener keep their sounds. Toggling
 // one back on resets its velocity (LinkedRaceActor::Toggle -> KartActor::Reset).
 void toggle() {
  toggleOdd = !toggleOdd;
  if (!toggleOdd) return;
  struct Near { f32 d; int i; };
  std::array<Near, 12> order{}; int n = 0;
  for (int i = 0; i < frame.karts && i < 12; ++i) {
   if (!frame.kart[i].present) continue;
   const auto& p = frame.kart[i].position;
   order[n++] = {std::fabs(listener.position.z - p.z) + std::fabs(listener.position.x - p.x) + std::fabs(listener.position.y - p.y), i};
  }
  std::stable_sort(order.begin(), order.begin() + n, [](const Near& a, const Near& b) { return a.d < b.d; });
  for (int j = 0; j < n; ++j) {
   KartAudio& k = karts[order[j].i];
   const bool muted = j >= KartsActive;
   if (!muted && k.muted) k.actor.velocity = {};
   k.muted = muted;
  }
 }
 // ---- Audio::CharacterActor ----
 // CharacterActor::Link (8085f66c) + fn_1_353B04: pickers by character and
 // local flag; LinkedRaceActor::Link's 3D params with UpdateUserParam(1) and
 // UpdateUserParam2(1): 0x11, 0x200 for a local racer.
 void voiceLink(int i) {
  VoiceAudio& v = voices[i];
  const RaceAudioKart& in = frame.kart[i];
  v = VoiceAudio{};
  v.character = in.character; v.local = in.local;
  v.enabled = in.character < 24;              // Mii voices are not used by this port
  v.finishDone = !v.local;
  v.actor.userParam = 0x11u | (v.local ? 0x200u : 0u);
  v.rng.x ^= 0x85ebca6bu * u32(i + 1);
  if (v.enabled)
   for (int t = 0; t < 29; ++t) { const auto& r = voice::Range[v.character][v.local ? 0 : 1][t]; v.picker[t].set(r[0], r[1]); }
  v.actionCount = in.actionCount;
 }
 bool voicePlaying(Engine& e, const VoiceAudio& v) const { return e.attached(v.main.h); }
 static void resetEcho(VoiceAudio& v) { v.echoId = -1; v.echoDelay = 0; v.echoVolume = 0; v.echoPosition = false; v.echo2D = false; }
 // CharacterActor::StopSound (80861554).
 void stopVoice(Engine& e, VoiceAudio& v, int fade) {
  v.curFrames = -1; v.pendingType = -1; v.pendingDelay = 0;
  if (e.attached(v.main.h)) e.stop(v.main.h, fade);
  if (e.attached(v.echo.h)) e.stop(v.echo.h, fade);
  resetEcho(v);
 }
 // fn_1_355A34 (808619b8): 0 keep the current voice, 1 play alongside, 3 interrupt.
 static int voicePriority(const VoiceAudio& v, int cur, int nw) {
  if (cur < 0 || cur > 31 || nw < 0 || nw > 31) return 1;
  const int pc = voice::Priority[cur][1], pn = voice::Priority[nw][1];
  if (!pc || !pn) return 1;
  if (unsigned(cur - 8) <= 3) {
   if (nw == 6) return cur == 0xb && v.curFrames < 0x5a ? 0 : 3;
  } else if (cur == 6) {
   if (unsigned(nw - 9) <= 1) { if (v.curFrames < 0x3c) return 0; }
   else if (nw == 8 && v.curFrames > 0x5a) return 3;
  } else if (unsigned(cur - 0x1a) <= 1 && nw == 6) return 3;
  if (pc < pn) return 3;
  if (pc > pn) return 0;
  const int k = voice::Priority[nw][0];
  return k == 0 ? 0 : k == 1 ? 3 : 1;
 }
 // fn_1_355820 (808617a4): after the finish only the result and damage voices.
 int voiceGate(Engine& e, int i, int type) {
  VoiceAudio& v = voices[i];
  if (type >= 0x1c && !v.local) return 0;
  int mode = 1;
  const bool playing = voicePlaying(e, v);
  if (frame.kart[i].finished) {
   if (!v.local) return 0;
   bool l900 = false;
   if (playing && unsigned(v.curType - 0x1d) <= 2) {
    if (unsigned(type - 0x12) <= 6) {
     stopVoice(e, v, 2);
     return 2;
    }
    l900 = true;
   } else if (unsigned(type - 0x12) <= 3) {
    if (v.finishDone) return 0;
    mode = 2; l900 = true;
   } else if (unsigned(type - 0x1a) <= 1 && playing && unsigned(v.curType - 0x16) <= 2) {
    stopVoice(e, v, 2);
    return 2;
   } else l900 = true;
   if (l900) {
    if (!v.finishDone && unsigned(type - 0x16) <= 2) v.finishDone = true;
    else if (unsigned(type - 0x12) > 3) return 0;
   }
  }
  if (playing) { const int r = voicePriority(v, v.curType, type); if (r != 1) mode = r; }
  else if (type != v.pendingType && v.pendingType != -1) { const int r = voicePriority(v, v.pendingType, type); if (r != 1) mode = r; }
  return mode;
 }
 // fn_1_3556E4 (80861668): the new voice's pitch by alteration state.
 void voicePitch(Engine& e, VoiceAudio& v, Slot& s) {
  f32 p = v.basePitch;
  if (v.state == 1) p *= 1.5f; else if (v.state == 2) p *= 0.87f; else if (v.state == 4) p *= 1.2f;
  p = p < 0.5f ? 0.5f : p > 2.0f ? 2.0f : p;
  if (e.attached(s.h)) e.setPitch(s.h, p);
  if (v.state == 1 || v.state == 4) {
   if (v.pitch.set(v.state == 1 ? 0.1f + 1.5f * v.basePitch : 0.2f + 1.2f * v.basePitch, 0) && e.attached(v.main.h)) e.setPitch(v.main.h, v.pitch.current);
   v.wobbleSide = 0; v.wobbleFrames = 0;
  }
 }
 // fn_1_355C2C (80861bb0): the delayed second copy (Mega, Star, power-up, fire).
 void voiceEcho(Engine& e, VoiceAudio& v, s32 id, int type, bool twoD) {
  if (e.attached(v.echo.h)) e.stop(v.echo.h, 0);
  resetEcho(v);
  if (v.local && v.echoNext) { v.echoId = id; v.echoDelay = 6; v.echoVolume = 0.6f; }
  else if (v.state == 2) { v.echoId = id; v.echoDelay = 7; v.echoVolume = 0.5f; }
  else if (v.state == 3) { v.echoId = id; v.echoDelay = 7; v.echoVolume = 0.6f; }
  else if (type == 2) { v.echoId = id; v.echoDelay = 7; v.echoVolume = 0.0f; }
  v.echoPosition = unsigned(type - 0x1d) <= 2;
  v.echo2D = twoD;
 }
 // fn_1_355318 (8086129c): a voice started; volume by character.
 void voiceStarted(Engine& e, int i, s32 id, int type, Slot& s, bool echo, bool result, bool twoD) {
  VoiceAudio& v = voices[i];
  voicePitch(e, v, s);
  if (type != 0x1c) v.slipFrames = 0;
  v.curFrames = 0; v.pendingType = -1; v.pendingDelay = 0;
  if (type == 4) v.overtakeCooldown = 0x78;
  f32 volume = 1.0f;
  if (!echo) { v.curType = type; voiceEcho(e, v, id, type, twoD); }
  else if (v.echoVolume <= 0.0f) volume = v.state == 2 ? 0.5f : v.state == 3 ? 0.6f : 1.0f;
  else volume = v.echoVolume;
  volume *= result ? voice::Volume2D[v.character] : voice::Volume3D[v.character];
  if (v.curType == 0x10) volume *= 0.75f; else if (v.curType == 0x1c) volume *= 0.85f;
  volume = std::min(volume, 1.0f);
  if (e.attached(s.h)) e.setVolume(s.h, volume);
  if (type == 0x18 && e.attached(v.main.h)) e.setVolume(v.main.h, 0.0f, 0x50);
 }
 // CharacterActor::StartSound (808611e8): other racers' voices on player 9.
 bool voiceStart(Engine& e, int i, Slot& s, s32 id) {
  VoiceAudio& v = voices[i];
  if (karts[i].muted || bit(frame.kart[i].status, Bullet) || id < 0 || u32(id) == VoDummy) return false;
  StartInfo si; si.playerId = 9;
  s.history = {};
  Ambient a = ambientFor(e, u32(id), v.actor, &s.history);
  if (!e.startSound(s.h, u32(id), v.local ? nullptr : &si, &a)) return false;
  s.flat = false;
  noteVoice();
  return true;
 }
 // RSARPlayer's 2D player: the local racer's damage and result voices.
 bool voiceStart2D(Engine& e, Slot& s, s32 id) {
  if (id < 0 || u32(id) == VoDummy) return false;
  s.history = {};
  if (!e.startSound(s.h, u32(id))) return false;
  s.flat = true;
  noteVoice();
  return true;
 }
 void noteVoice() { if (owner) owner->voiceStarts.fetch_add(1, std::memory_order_relaxed); }
 void noteItem(int kind) {
  if (!owner) return;
  owner->itemSounds.fetch_add(1, std::memory_order_relaxed);
  owner->itemStarted[kind].fetch_add(1, std::memory_order_relaxed);
 }
 // CharacterActor::PlayCharacterSound (808604e4).
 bool voicePlay(Engine& e, int i, int type) {
  VoiceAudio& v = voices[i];
  if (!v.enabled) return false;
  v.fellInWater = false;
  const int mode = voiceGate(e, i, type);
  if (!mode) return false;
  int t = type;
  if (type == 0x18) { t = 0x14; v.echoNext = true; }
  else if (type == 0x19) {
   if (!v.local) return false;
   t = v.character == 1 ? 0x12 : v.character == 4 ? 0x13 : 7;
  } else if (type == 0x1b && v.curType == 0x18) { type = 0x1a; t = 0x1a; }
  t = voice::Remap[v.character][v.local ? 1 : 0][t & 31];
  if (t < 0 || t >= 29) { v.echoNext = false; return false; }
  if (mode == 3) stopVoice(e, v, 2);
  Picker& pk = v.picker[t];
  const s32 id = pk.pick(v.rng);
  if (id == s32(VoDummy)) return false;
  bool twoD = false, ok;
  if (mode == 2 || (v.local && v.curType != 0x18 && unsigned(type - 0x16) < 3)) { ok = voiceStart2D(e, v.main, id); twoD = true; }
  else ok = voiceStart(e, i, v.main, id);
  if (ok) {
   voiceStarted(e, i, id, type, v.main, false, false, twoD);
   pk.note(id);
   // Funky Kong's extra trick and ... sounds (0xf68 / 0xf69): course-free SE.
   if (v.character == 0x17 && (type == 0xb || type == 0x13)) {
    if (Slot* s2 = startLimitedVoice(e, i, type == 0xb ? 0xf68 : 0xf69); s2 && !v.local) e.setVolume(s2->h, 1.0f);
   } else if (v.character == 0x15 && v.local) e.setSeqVariable(v.main.h, 7, 1);
  }
  v.echoNext = false;
  return ok;
 }
 // A StartSoundLimited on the character actor (RaceActor 806fa3a8).
 Slot* startLimitedVoice(Engine& e, int i, u32 id) {
  VoiceAudio& v = voices[i];
  if (karts[i].muted) return nullptr;
  Slot* slot = nullptr;
  for (auto& s : v.limited) if (!e.attached(s.h)) { slot = &s; break; }
  if (!slot) return nullptr;
  slot->history = {}; slot->flat = false;
  Ambient a = ambientFor(e, id, v.actor, &slot->history);
  return e.startSound(slot->h, id, nullptr, &a) ? slot : nullptr;
 }
 // CharacterActor::PlayCharacterSoundConditional (808602c0), the entry from
 // Kart::Link::PlayCharacterSound: falls and respawns wait, cannons check the course.
 bool voiceRequest(Engine& e, int i, int type) {
  VoiceAudio& v = voices[i];
  if (!v.enabled) return false;
  int delay = 0x3c;
  switch (type) {
  case 0xb: trickCheer(e, i); break;
  case 0xc: if (frame.course == 0x1d) return false; break;
  case 0x16: case 0x18: case 0x19:
   if (unsigned(v.curType - 0x16) < 3 || v.curType == 0x19) return false;
   break;
  case 0x17:
   if (unsigned(v.curType - 0x16) < 3 || v.curType == 0x19) return false;
   if (v.pendingType == -1 || voicePriority(v, v.pendingType, type)) { v.pendingType = type; v.pendingDelay = 10; }
   return true;
  case 0x1a:
   if (frame.course == 0x1b) delay = 0x14;
   if (v.fellInWater || v.curType == 0x17) type = 0x1b;
   stopVoice(e, v, 5);
   if (v.pendingType == -1 || voicePriority(v, v.pendingType, type)) { v.pendingType = type; v.pendingDelay = delay; }
   return true;
  case 0x1b:
   if (frame.course == 0x1b) { v.curType = 0x1b; return false; }
   if (!v.fellInWater && v.curType != 0x17) type = 0x1a;
   stopVoice(e, v, 5);
   if (v.pendingType == -1 || voicePriority(v, v.pendingType, type)) { v.pendingType = type; v.pendingDelay = 0x3c; }
   return true;
  default: break;
  }
  return voicePlay(e, i, type);
 }
 // fn_1_356274 (808621f8): the crowd cheers a local trick on Waluigi Stadium
 // and Funky Stadium, cutting the running cheer.
 void trickCheer(Engine& e, int i) {
  VoiceAudio& v = voices[i];
  if (karts[i].muted || frame.kart[i].finished || !v.local) return;
  u32 first, second;
  if (frame.course == 0x12) { first = 0x36a; second = 0x36b; }
  else if (frame.course == 0x23) { first = 0x30d; second = 0x30e; }
  else return;
  for (auto& s : v.limited) if (e.attached(s.h) && (e.soundId(s.h) == first || e.soundId(s.h) == second)) { e.stop(s.h, 0x1e); break; }
  startLimitedVoice(e, i, second);
 }
 // fn_1_354938 (808608bc): the result voice, 120 frames after the finish.
 void finishVoice(Engine& e, int i, int type) {
  VoiceAudio& v = voices[i];
  v.fellInWater = false;
  const u32* w = frame.kart[i].status;
  if (!frame.kart[i].finished || bit(w, Bullet) || !v.local || v.finishDone) return;
  const int result = type - 0x1d;
  s32 id;
  Picker* pk = nullptr;
  if (v.character == 0xe || v.character == 5) {
   const int t = voice::Remap[v.character][1][type & 31];
   if (t < 0 || t >= 29) return;
   pk = &v.picker[t]; id = pk->pick(v.rng);
  } else {
   id = frame.goalVoice[result] ? s32(frame.goalVoice[result]) : -1;
  }
  if (voicePlaying(e, v)) stopVoice(e, v, 2);
  if (!voiceStart2D(e, v.main, id)) return;
  voiceStarted(e, i, id, type, v.main, false, true, true);
  if (pk) pk->note(id);
  if (owner) owner->resultVoice.store(u32(id), std::memory_order_relaxed);
  if (v.character == 0x17) startLimitedVoice(e, i, type == 0x1d ? 0xf6a : type == 0x1f ? 0xf6c : 0xf6b);
  v.finishDone = true;
 }
 // fn_1_27CBF0 (80783f98): 0 first, 1 within the threshold, 2 below (GP / VS).
 unsigned resultOf(const RaceAudioKart& in) const {
  static constexpr u8 Threshold[12] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 5};
  const unsigned racers = std::clamp<unsigned>(frame.karts, 1, 12);
  if (in.rank <= 1) return 0;
  return in.rank <= Threshold[racers - 1] ? 1 : 2;
 }
 // Kart::Damage::SetDamage -> CharacterActor::PlayCharacterCollisionSound (80861018).
 static int damageVoice(int action) {
  switch (action) {
  case 0: case 1: return 0x15;
  case 2: case 3: case 5: case 0xb: case 0xc: case 0xd: return 0x12;
  case 4: case 6: case 7: case 8: case 0xa: return 0x13;
  case 9: return 0x14;
  default: return 0x11;
  }
 }
 // The kart's physics-time voice calls (Kinoko cues and state edges).
 void voiceCues(Engine& e, int i) {
  VoiceAudio& v = voices[i];
  const RaceAudioKart& in = frame.kart[i];
  if (!v.enabled) return;
  const u32 c = in.cues; const u32* w = in.status;
  if (c & cue::Respawn) voiceRequest(e, i, 0x1b);                 // Kart::Movement::Respawn
  if (c & cue::Fall) voiceRequest(e, i, in.fallVoice);             // Kart::Collision::ActivateOOB
  if (in.actionCount != v.actionCount) {                            // Kart::Damage::SetDamage
   v.actionCount = in.actionCount;
   if (in.action >= 0) voicePlay(e, i, damageVoice(in.action));
  }
  if (c & cue::PowerHit) voiceRequest(e, i, 5);                    // CheckKartCollision power-up contact
  if (c & cue::Bump) voicePlay(e, i, std::fabs(in.bump) > 24.0f ? 0x12 : 5);   // fn_1_355180 from fn_1_609F4
  const bool star = bit(w, Star), mega = bit(w, Mega);
  if ((star && !v.wasStar) || (mega && !v.wasMega)) voiceRequest(e, i, 2);   // ActivateStar / ActivateMega
  v.wasStar = star; v.wasMega = mega;
  if (c & cue::StartFail) voiceRequest(e, i, 7);                   // KartBurnout start
  if (c & (cue::Trick | cue::ZipperTrick)) voiceRequest(e, i, 0xb); // Kart::Trick::Start, half-pipe trick fn_1_64AAC
  if (c & cue::Cannon) voiceRequest(e, i, 0xc);                    // Kart::Movement::EnterCannon
  if (c & cue::BoostVoice) voiceRequest(e, i, 6);                  // a boost activated
  if (in.slipBoost && !v.wasSlipBoost) voiceRequest(e, i, 6);       // UpdateSlipStream: the slipstream boost
  v.wasSlipBoost = in.slipBoost;
  if (in.slipCharging) {                                            // fn_1_355200: drafting
   if (v.slipFrames < 0x3c) { if (v.slipFrames == 0x1e) voicePlay(e, i, 0x1c); ++v.slipFrames; }
   v.inSlip = true;
  }
  if (c & cue::Wheelie) {                                           // fn_1_354FDC from StartWheelie
   const int t = bit(w, Wheelie) ? 0xf : 0x10;
   if (v.pendingType == -1 || voicePriority(v, v.pendingType, t)) { v.pendingType = t; v.pendingDelay = t == 0xf ? 10 : 0x14; }
  }
  // fn_1_354D84 from Kart::Status::UpdateCollisions: landing (local racer), by
  // the driver's airtime two model updates back.
  if (bit(w, GroundStart) && v.local) landingVoice(e, i);
  v.airtimeBefore = v.airtime; v.airtime = in.airtime;
 }
 void landingVoice(Engine& e, int i) {
  VoiceAudio& v = voices[i];
  const RaceAudioKart& in = frame.kart[i];
  for (int w = 0; w < in.wheels && w < 4; ++w) if (in.floorType[w] == 8 && in.floorVariant[w] == 4) return;   // a mushroom jump pad
  const int air = int(v.airtimeBefore);
  if (air < 60) return;
  int t = 0xd;
  const int cur = v.curType;
  if (unsigned(cur - 0x16) <= 2 || unsigned(cur - 0x1a) <= 1) return;
  if (unsigned(cur - 9) <= 1 || cur == 0xc) { if (air >= 120) t = 0xe; }
  else { if (air < 120) return; if (air >= 180) t = 0xe; }
  if (t == 0xd && voicePlaying(e, v)) return;
  voicePlay(e, i, t);
 }
 // CharacterActor::Update (8085fbd0).
 void voiceUpdate(Engine& e, int i) {
  VoiceAudio& v = voices[i];
  const RaceAudioKart& in = frame.kart[i];
  if (!v.enabled) return;
  place(v.actor, v.placed, in.position);
  if (v.overtakeCooldown > 0) --v.overtakeCooldown;
  const u32* w = in.status;
  f32 p = v.basePitch; int frames = 5; bool changed = false;
  if (bit(w, Shocked)) {
   --v.wobbleFrames;
   if (v.state == 1) {
    if (v.wobbleFrames < 1) {
     p = v.wobbleSide ? 0.1f + 1.5f * p : 1.5f * p - 0.1f;
     v.wobbleSide = !v.wobbleSide; v.wobbleFrames = 4; frames = 0; changed = true;
    }
   } else { p *= 1.5f; v.state = 1; frames = 4; changed = true; v.wobbleSide = 0; v.wobbleFrames = 4; }
  } else if (bit(w, Crushed)) {
   --v.wobbleFrames;
   if (v.state == 4) {
    if (v.wobbleFrames < 1) {
     p = v.wobbleSide ? 0.2f + 1.2f * p : 1.2f * p - 0.2f;
     v.wobbleSide = !v.wobbleSide; v.wobbleFrames = 5; frames = 0; changed = true;
    }
   } else { p *= 1.2f; v.state = 4; frames = 5; changed = true; v.wobbleSide = 0; v.wobbleFrames = 5; }
  } else if (bit(w, Star)) { if (v.state != 3) { v.state = 3; changed = true; } }
  else if (bit(w, Mega)) { if (v.state != 2) { p *= 0.87f; v.state = 2; changed = true; } }
  else if (v.state != 0) {
   v.state = 0; changed = true;
   if (e.attached(v.echo.h)) e.stop(v.echo.h, 10);
   resetEcho(v);
  }
  if (changed) {
   if (v.pitch.set(p, frames) && e.attached(v.main.h)) e.setPitch(v.main.h, v.pitch.current);
   if (v.state < 2 || v.state == 4) { if (e.attached(v.echo.h)) e.stop(v.echo.h, 10); resetEcho(v); }
  }
  if (v.slipFrames > 0) {
   if (!v.inSlip) --v.slipFrames;
   if (v.slipFrames < 0x1e && v.curType == 0x1c && voicePlaying(e, v)) stopVoice(e, v, 0x14);
  }
  v.inSlip = false;
  v.curFrames = voicePlaying(e, v) ? v.curFrames + 1 : -1;
  if (v.local && in.finished && !v.finishDone && !bit(w, Bullet) && ++v.finishFrames > 0x78 && !bit(w, InAction)
      && (frame.mode == 0 || frame.mode == 1))
   finishVoice(e, i, 0x1d + int(resultOf(in)));
  if (v.pendingType != -1 && --v.pendingDelay < 1) { const int t = v.pendingType; v.pendingType = -1; v.pendingDelay = 0; voicePlay(e, i, t); }
  if (v.echoId != -1) {
   if (v.echoDelay < 0) { if (e.attached(v.echo.h)) e.stop(v.echo.h, 0); resetEcho(v); }
   else if (--v.echoDelay < 0) {
    bool ok;
    if (v.echo2D) ok = voiceStart2D(e, v.echo, v.echoId);
    else {
     StartInfo si; si.playerId = 10;
     v.echo.history = {};
     Ambient a = ambientFor(e, u32(v.echoId), v.actor, &v.echo.history);
     ok = e.startSound(v.echo.h, u32(v.echoId), &si, &a);
     if (ok) { v.echo.flat = false; noteVoice(); }
    }
    if (ok) voiceStarted(e, i, v.echoId, v.curType, v.echo, true, v.echoPosition, v.echo2D);
    resetEcho(v);
   }
  }
 }
 // fn_1_3581D8 (80863b08): each voice's pitch track (CharacterActor::HoldSound).
 void voicePitchTracks(Engine& e) {
  for (auto& v : voices) {
   if (!v.enabled) continue;
   if (v.pitch.calc(e.attached(v.main.h))) e.setPitch(v.main.h, v.pitch.current);
  }
 }
 void updateVoiceAmbient(Engine& e, VoiceAudio& v) {
  auto one = [&](Slot& s) {
   if (!e.attached(s.h)) { s.history = {}; return; }
   if (s.flat) return;
   e.setAmbient(s.h, ambientFor(e, e.soundId(s.h), v.actor, &s.history));
  };
  one(v.main); one(v.echo); for (auto& s : v.limited) one(s);
 }
 // ---- items ----
 // ItemActor::StartSoundLimited (806fd9c0): two slots; a muted actor keeps
 // only the cloud and the throw; a break cuts a wall glance, a glance restarts.
 Slot* itemStart(Engine& e, ItemObjAudio& o, u32 id) {
  if (!id) return nullptr;
  if (o.muted && id != 0x120 && id != 0x100) return nullptr;
  if (id == 0xfa || id == 0xf9)
   for (auto& s : o.limited) if (e.attached(s.h) && e.soundId(s.h) == 0xf9) { e.stop(s.h, 0); break; }
  Slot* slot = nullptr;
  for (auto& s : o.limited) if (!e.attached(s.h)) { slot = &s; break; }
  if (!slot) {
   const auto* arc = e.archive();
   const SoundEntry* n = arc ? arc->sound(id) : nullptr;
   int lowest = 0x7f;
   for (auto& s : o.limited) { const SoundEntry* x = arc ? arc->sound(e.soundId(s.h)) : nullptr; const int p = x ? x->playerPriority : 0; if (p < lowest) { lowest = p; slot = &s; } }
   if (!n || !slot || n->playerPriority <= lowest) return nullptr;
   e.stop(slot->h, 0);
  }
  slot->history = {}; slot->flat = false;
  Ambient a = ambientFor(e, id, o.actor, &slot->history);
  if (!e.startSound(slot->h, id, nullptr, &a)) return nullptr;
  noteItem(&o >= &clouds[0].o && &o <= &clouds[11].o ? RaceAudioLayer::QaCloud : RaceAudioLayer::QaShot);
  return slot;
 }
 // ItemActor::HoldSoundLimited (806fdcfc): a muted actor keeps the Blue Shell's wings.
 void itemHold(Engine& e, ItemObjAudio& o, u32 id) {
  if (o.muted && id != 0x103 && id != 0x104) return;
  for (auto& s : o.limited) if (e.attached(s.h) && e.soundId(s.h) == id) { e.holdSound(s.h, id); return; }
  Slot* slot = nullptr;
  for (auto& s : o.limited) if (!e.attached(s.h)) { slot = &s; break; }
  if (!slot) return;
  slot->history = {}; slot->flat = false;
  Ambient a = ambientFor(e, id, o.actor, &slot->history);
  if (e.holdSound(slot->h, id, nullptr, &a)) noteItem(RaceAudioLayer::QaLoop);
 }
 void itemAmbient(Engine& e, ItemObjAudio& o) {
  for (auto& s : o.limited) {
   if (!e.attached(s.h)) { s.history = {}; continue; }
   e.setAmbient(s.h, ambientFor(e, e.soundId(s.h), o.actor, &s.history));
  }
 }
 // One game frame of item sounds, after the karts (Item::ItemHolder_update order).
 void itemFrame(Engine& e) {
  for (auto& o : items) o.live = false;
  for (int n = 0; n < frame.items; ++n) {
   const RaceAudioItem& it = frame.item[n];
   ItemObjAudio& o = items[it.index % items.size()];
   if (o.kind != it.kind || !o.placed) {
    // A new object in this pool slot (its actor is the slot's, as in retail).
    o.actor = {}; o.placed = false; o.muted = false; o.kind = it.kind;
    o.actor.userParam = 0x12u;   // ItemActor::DisableConditional / UpdateUserParam3
   }
   place(o.actor, o.placed, it.position);
   o.live = true;
  }
  for (auto& o : items) if (!o.live) o.kind = 0xff;
  for (auto& o : items) itemAmbient(e, o);
  for (auto& o : heldItems) itemAmbient(e, o);
  cloudFrame(e);
  for (int i = 0; i < 12 && i < frame.karts; ++i) {
   GessoAudio& g = gesso[i];
   if (!frame.kart[i].local) continue;
   place(g.actor, g.placed, frame.kart[i].position);
   if (e.attached(g.slot.h)) e.setAmbient(g.slot.h, ambientFor(e, e.soundId(g.slot.h), g.actor, &g.slot.history));
   else g.slot.history = {};
  }
  // The step's cues, in emission order.
  for (int n = 0; n < frame.itemSounds; ++n) {
   const RaceAudioItemSound& c = frame.itemSound[n];
   if (c.cue >= ItemFakeBox) { itemEffect(e, c); continue; }
   const u8 object = c.object < 15 ? c.object : 14;
   ItemObjAudio* o = nullptr;
   if (c.actor != 0xff) o = &items[c.actor % items.size()];
   else if (c.player < 12) { o = &heldItems[c.player]; place(o->actor, o->placed, c.position); o->actor.userParam = 0x12u; }
   switch (c.cue) {
   case ItemThrow: if (o) itemStart(e, *o, 0x100); break;
   case ItemLand: if (o) itemStart(e, *o, ItemObjSound[object][2]); break;
   case ItemBounce: if (o) itemStart(e, *o, 0x107); break;
   case ItemWallBounce: if (o) itemStart(e, *o, 0xf9); break;
   case ItemBreak: if (o) itemStart(e, *o, ItemObjSound[object][0]); break;
   case ItemHit: if (o) itemStart(e, *o, ItemObjSound[object][1]); break;
   case ItemExplode:
    if (o) itemStart(e, *o, c.kind == KindBlue ? 0x105 : c.kind == KindBobomb ? 0x109 : 0);
    if (c.kind == KindBlue) globalSound(e, 0x106);   // 807a363c: SE_ITM_HANE_BOMB_ECHO, global
    break;
   case ItemEquip:
    if (c.player < 12 && karts[c.player].present && karts[c.player].local) startLimited(e, karts[c.player], 0xf6);
    break;
   case ItemDecide:
    if (c.player < 12 && frame.kart[c.player].local) {
     Handle h;
     if (e.startSound(h, c.flag ? 0xe4 : 0xe3)) noteItem(RaceAudioLayer::QaDecide);
    }
    break;
   case ItemRelease:
    if (c.player < 12 && voices[c.player].enabled) voiceRequest(e, c.player, c.flag ? 1 : 0);
    break;
   default: break;
   }
  }
  // The objects' loops (each object's Update).
  for (int n = 0; n < frame.items; ++n) {
   const RaceAudioItem& it = frame.item[n];
   ItemObjAudio& o = items[it.index % items.size()];
   if (it.explosion || it.scattered) continue;
   switch (it.kind) {
   case KindGreen: itemHold(e, o, 0xf8); break;     // ObjKouraGreen::Update 807a4044
   case KindRed: itemHold(e, o, 0xf7); break;       // ObjKouraRed::Update 8079fce4
   case KindBlue: itemHold(e, o, it.phase <= 2 ? 0x103 : 0x104); break;   // states 0-2 fly, 3-5 near
   case KindBobomb: itemHold(e, o, 0x108); break;   // fn_1_294048: the fuse
   default: break;
   }
  }
  // Item::PlayerObj::SpinUpdate (8078b8d8): orbiting triple shells, by count.
  for (int i = 0; i < 12 && i < frame.karts; ++i)
   if (frame.orbit[i] && karts[i].present && !karts[i].muted) holdLimited(e, karts[i], 0xfd + std::min<u32>(frame.orbit[i], 3) - 1);
  // RaceRSARPlayer::Calc -> fn_1_205340: the local racer's roulette.
  bool spinning = false;
  for (int i = 0; i < 12 && i < frame.karts; ++i) if ((frame.roulette >> i) & 1 && frame.kart[i].local) spinning = true;
  if (spinning) { roulette.flat = true; e.holdSound(roulette.h, 0xe2); e.setPan(roulette.h, 0.0f); }
  itemAlarm(e);
  itemToggle();
 }
 // The cues for racers' actors, the item managers and the Thunder Cloud.
 void itemEffect(Engine& e, const RaceAudioItemSound& c) {
  const int p = c.player < 12 && c.player < frame.karts ? c.player : -1;
  KartAudio* k = p >= 0 && karts[p].present ? &karts[p] : nullptr;
  switch (c.cue) {
  case ItemHitSuccess:
   // Kart collision handler 8056dc80 -> CharacterActor 808610e8:
   // the attacker's local CharacterActor plays type 3, ITM_SCES.
   if(p>=0&&voices[p].enabled&&voices[p].local)voicePlay(e,p,3);
   break;
  case ItemFakeBox: if (k) { if (c.flag) startNewLimited(e, *k, sid::BoxSmoke); else startLimited(e, *k, sid::BoxHitFx); } break;
  case ItemBarrier: if (k) startNewLimited(e, *k, sid::StarBarrier); break;
  case ItemInk: if (k) startLimited(e, *k, sid::GesoSumiOn); break;   // Effects::Player::UpdateInkEffect 8069bd4c
  case ItemStruck: if (k) k->thunderA = 0; break;
  case ItemLightning: globalSound(e, sid::ThndrUse); break;
  case ItemPow: if (c.value >= 1 && c.value <= 3) globalSound(e, sid::Pow1 + c.value - 1u); break;
  case ItemBlooperUse:   // Item::Player::UseBlooper 807d503c: every local screen
   for (int i = 0; i < frame.karts && i < 12; ++i) if (frame.kart[i].local) gessoStart(e, i, sid::GesoStart);
   break;
  case ItemBlooperAttack: if (p >= 0 && frame.kart[p].local) gessoStart(e, p, sid::GesoAtk); break;
  case ItemCloudOn: {   // ObjKumo spawn (fn_1_29EE80 807d6dd0)
   if (p < 0) break;
   CloudAudio* cl = nullptr;
   for (auto& x : clouds) if (x.carrier < 0) { cl = &x; break; }
   if (!cl) for (auto& x : clouds) if (x.struck >= 0 && (!cl || x.struck > cl->struck)) cl = &x;
   if (!cl) break;
   cl->carrier = p; cl->struck = -1; cl->flash = false;
   cl->o.actor = {}; cl->o.placed = false; cl->o.muted = false; cl->o.actor.userParam = 0x12u;
   const Vec3f k = frame.kart[p].position;
   place(cl->o.actor, cl->o.placed, {k.x, k.y + 600.0f, k.z});   // thunder_cloud.hpp: spawned 600 above
   itemStart(e, cl->o, sid::KumoOn);
   break;
  }
  case ItemCloudAttach:   // fn_1_2A0D54 807d8bf0: it passes to the racer it touched
   if (CloudAudio* cl = cloudOn(c.value); cl && p >= 0) { cl->carrier = p; itemStart(e, cl->o, sid::KumoAttach); }
   break;
  case ItemCloudStrike:   // ObjKumo::Update 807d77e4: the strike at frame 600
   if (CloudAudio* cl = cloudOn(p)) { itemStart(e, cl->o, sid::KumoThndr); cl->struck = 0; }
   break;
  case ItemCloudOff:      // ObjKumo::Update 807d75b4: the carrier can no longer hold it
   if (CloudAudio* cl = cloudOn(p)) { itemStart(e, cl->o, sid::KumoOff); cl->carrier = -1; }
   break;
  case ItemBoxBreak: {    // ObjItemBox kart collision (808151a4): Object::StartSoundLimited on the box
   // The box's ObjectActor (id 0x65, always audible) at the box, below its 180-unit hover.
   ObjAudio& b = boxActors[nextBoxActor++ % boxActors.size()];
   for (Slot* s : {&b.limited[0], &b.limited[1], &b.limited[2], &b.limited[3]}) if (e.attached(s->h)) e.stop(s->h, 0);
   b = ObjAudio{};
   b.live = true; b.id = 0x65; b.mode = 1;
   b.actor.userParam = objectUserParam(0x65, 1, 1.0f, frame.course);
   place(b.actor, b.placed, {c.position.x, c.position.y - 180.0f, c.position.z});
   objectStart(e, b, 0xf4, 0.0f);   // SE_ITM_BOX_BRK
   break;
  }
  default: break;
  }
 }
 CloudAudio* cloudOn(int carrier) {
  for (auto& x : clouds) if (carrier >= 0 && x.carrier == carrier && x.struck < 0) return &x;
  return nullptr;
 }
 // Each cloud's ObjKumo::Update: it follows its carrier, flashes before the strike
 // (SE_ITM_KUMO_SIGN as each flash starts) and fades 91 frames after it (807d808c).
 void cloudFrame(Engine& e) {
  for (auto& cl : clouds) {
   if (cl.carrier >= 0) {   // it settles 365 above its carrier (fn_2CB188, thunder_cloud.hpp)
    const Vec3f k = frame.kart[cl.carrier].position;
    place(cl.o.actor, cl.o.placed, {k.x, k.y + 365.0f, k.z});
   }
   itemAmbient(e, cl.o);
   if (cl.carrier < 0) continue;
   if (cl.struck >= 0) {
    if (++cl.struck > 90) { itemStart(e, cl.o, sid::KumoOff); cl.carrier = -1; }
    continue;
   }
   const int left = frame.cloud[cl.carrier];
   if (left <= 0 || left > 600) continue;
   const bool flash = cloudFlash(600 - left);
   if (flash && !cl.flash) itemStart(e, cl.o, sid::KumoSign);
   cl.flash = flash;
  }
 }
 // Audio::RaceAmbienceMgr::PlayGlobalSound (806f7c38): one 2D handle, at EchoMgr's
 // listener volume averaged over the local players.
 void globalSound(Engine& e, u32 id) {
  global.flat = true;
  if (!e.startSound(global.h, id)) return;
  e.setVolume(global.h, listener.volume);
  noteItem(RaceAudioLayer::QaGlobal);
 }
 // Audio::GessoActor::StartSoundLimited (806fe3f4): one slot, taken by a higher priority.
 void gessoStart(Engine& e, int i, u32 id) {
  GessoAudio& g = gesso[i];
  place(g.actor, g.placed, frame.kart[i].position);
  g.actor.userParam = 0x12u;   // LinkedRaceActor::Link: DisableConditional, UpdateUserParam3
  Slot* slot = freeIn(e, &g.slot, 1, id);
  if (!slot) return;
  slot->history = {}; slot->flat = false;
  Ambient a = ambientFor(e, id, g.actor, &slot->history);
  if (e.startSound(slot->h, id, nullptr, &a)) noteItem(RaceAudioLayer::QaScreen);
 }
 // ---- RaceAmbienceMgr (RaceSound's SoundTriggers ask; race_ambience.hpp) ----
 void ambienceOp(Engine& e, const AmbienceOp& op) {
  switch (op.kind) {
  case AmbienceOp::Play:
   // PlayAmbience (806f7e40): a loop already playing comes back to full if it
   // went silent; otherwise the first free ambience starts it at full.
   for (auto& a : ambiences)
    if (e.attached(a.slot.h) && e.soundId(a.slot.h) == op.id) { if (a.track.value <= 0) a.track.value = 1; return; }
   for (auto& a : ambiences)
    if (!e.attached(a.slot.h)) {
     a.slot.flat = true;
     if (e.startSound(a.slot.h, op.id)) { a.track.value = 1; noteItem(RaceAudioLayer::QaAmbience); }
     return;
    }
   break;
  case AmbienceOp::Volume:   // SetAmbienceVolume (806f7f94)
   for (auto& a : ambiences) if (e.attached(a.slot.h) && e.soundId(a.slot.h) == op.id) { a.track.set(op.value, op.frames); return; }
   break;
  case AmbienceOp::Global: globalSound(e, op.id); break;
  case AmbienceOp::GlobalStop: if (e.attached(global.h)) e.stop(global.h, op.frames); break;
  case AmbienceOp::GlobalVolume: if (e.attached(global.h)) e.setVolume(global.h, op.value, op.frames); break;
  case AmbienceOp::GlobalFadeIn: if (e.attached(global.h)) e.fadeIn(global.h, op.frames); break;
  case AmbienceOp::OneShot:
   // fn_1_1ED62C: Bowser's laughs are not restarted while they play.
   if ((op.id == 0x2e9 || op.id == 0x35b) && e.attached(oneShot.h) && e.soundId(oneShot.h) == op.id) break;
   oneShot.flat = true;
   if (!e.startSound(oneShot.h, op.id)) break;
   noteItem(RaceAudioLayer::QaAmbience);
   e.setPan(oneShot.h, 0.0f);
   if (op.var >= 0) e.setSeqVariable(oneShot.h, 0, op.var);   // Audio::Manager::WriteSeqVariable
   break;
  }
 }
 // Ambience::Update (806f88b0) for each ambience, one local racer.
 void ambienceUpdate(Engine& e) {
  for (auto& a : ambiences) {
   if (!e.attached(a.slot.h)) continue;
   a.track.calc(); a.track.clamp(0, 1);
   if (a.track.value == 0) e.stop(a.slot.h, 0);
   else e.setVolume(a.slot.h, a.track.value);
  }
 }
 // Audio::ObjectActor::HoldSoundLimited (80706fd4): the playing slot holds on,
 // else a free slot or the one with the lowest priority.
 void objHold(Engine& e, ObjActorAudio& o, u32 id) {
  for (auto& s : o.limited) if (e.attached(s.h) && e.soundId(s.h) == id) { e.holdSound(s.h, id); return; }
  Slot* slot = freeIn(e, o.limited, 4, id);
  if (!slot) return;
  slot->history = {}; slot->flat = false;
  Ambient a = ambientFor(e, id, o.actor, &slot->history);
  if (e.holdSound(slot->h, id, nullptr, &a)) noteItem(RaceAudioLayer::QaObject);
 }
 void objAmbient(Engine& e, ObjActorAudio& o) {
  for (auto& s : o.limited) {
   if (!e.attached(s.h)) { s.history = {}; continue; }
   e.setAmbient(s.h, ambientFor(e, e.soundId(s.h), o.actor, &s.history));
  }
 }
 // ---- Course objects ----
 bool playing(Engine& e, const ObjAudio& o, u32 sound) const {
  for (const auto& s : o.limited) if (e.attached(s.h) && e.soundId(s.h) == sound) return true;
  return false;
 }
 void stopPlaying(Engine& e, ObjAudio& o, u32 lo, u32 hi, int fade = 0) {
  for (auto& s : o.limited) if (e.attached(s.h) && e.soundId(s.h) >= lo && e.soundId(s.h) <= hi) e.stop(s.h, fade);
 }
 // SetupSound (80707cd0 / 806fd124): sounds gated on the local racer's zone.
 bool objectSetup(const ObjAudio& o, u32& sound) const {
  if (o.mode == 1) {
   if (o.id == 0xcf && frame.course == 0xc) {   // Bowser's Castle's fireballs
    if (frame.raceState == 2) return frame.introCount == 2;
    return frame.soundZone == 4;
   }
   return true;
  }
  if (o.id == 0xdb && frame.course == 0xc) return frame.soundZone == 3 || frame.soundZone == 6;
  if (o.id == 0x171 && frame.course == 0xc) {
   if (frame.raceState == 2) return frame.introCount == 2;
   if (frame.soundZone == 4) return true;
   if (frame.soundZone == 5) { ++sound; return true; }
   return false;
  }
  return true;
 }
 Slot* objectSlotStart(Engine& e, ObjAudio& o, u32 sound, bool hold) {
  if (!objectSetup(o, sound)) return nullptr;
  if (hold) for (auto& s : o.limited) if (e.attached(s.h) && e.soundId(s.h) == sound) { e.holdSound(s.h, sound); return &s; }
  Slot* slot = freeIn(e, o.limited, 4, sound);
  if (!slot) return nullptr;
  slot->history = {}; slot->flat = false;
  Ambient a = ambientFor(e, sound, o.actor, &slot->history);
  const bool ok = hold ? e.holdSound(slot->h, sound, nullptr, &a) : e.startSound(slot->h, sound, nullptr, &a);
  if (!ok) return nullptr;
  noteItem(RaceAudioLayer::QaObject);
  return slot;
 }
 // Object::StartSoundLimited: ObjectActor::StartNewSoundLimited (8070645c) or
 // ObjectActorAnim::StartSoundLimited (806fc15c), with their per-object rules.
 Slot* objectStart(Engine& e, ObjAudio& o, u32 sound, f32 value) {
  if (o.mode == 3) {
   if (o.muted) return nullptr;
   if ((o.id == 0xcd && sound == 0x23c) || (o.id == 0x193 && sound == 0x28f) || (o.id == 0x199 && sound == 0x249))
    if (playing(e, o, sound)) return nullptr;
   if (o.id == 0x199 && sound == 0x24a) stopPlaying(e, o, 0x249, 0x249);
   if (o.id == 0x196 && sound == 0x22c && frame.course == 0x11 && frame.raceState == 2) return nullptr;
   Slot* s = objectSlotStart(e, o, sound, false);
   if (!s) return nullptr;
   if (o.id == 0x1a3 && sound == 0x27e) e.setVolume(s->h, std::clamp(value, 0.0f, 1.0f));
   else if (o.id == 0x1ab && sound == 0x24e) e.setSeqVariable(s->h, 0, s16(int(value)));
   return s;
  }
  if (o.muted && o.id != 0xcf && o.id != 0xd6 && o.id != 0xea && o.id != 0x1fc) {
   const bool flame = (o.id == 0x212 || o.id == 0x216) && sound == 0x1da;
   if (!flame) return nullptr;
  }
  switch (o.id) {
  case 0xd6: if (sound == 0x2d6) stopPlaying(e, o, 0x2d5, 0x2d5); break;   // VolcanoBall
  case 0xd0:   // kart_truck: the move loop and the brake are exclusive; three variants
   if (sound == 0x207) { for (u32 v = 0x20a; v <= 0x20c; ++v) if (playing(e, o, v)) return nullptr; }
   else if (sound == 0x20a) stopPlaying(e, o, 0x207, 0x209);
   if (sound == 0x207 || sound == 0x20a) { sound += o.index % 3; if (playing(e, o, sound)) return nullptr; }
   break;
  case 0xcc: case 0xe7: case 0xe8: if (sound == 0x2b0 && playing(e, o, 0x2b0)) return nullptr; break;   // CarA
  case 0xd1:   // car_body: four variants
   if (sound == 0x1fd) { for (u32 v = 0x201; v <= 0x204; ++v) if (playing(e, o, v)) return nullptr; }
   else if (sound == 0x201) stopPlaying(e, o, 0x1fd, 0x200);
   if (sound == 0x1fd || sound == 0x201) { sound += o.index % 4; if (playing(e, o, sound)) return nullptr; }
   break;
  case 0x19b:   // TruckWagon
   if (sound == 0x2c0 && playing(e, o, 0x2c0)) return nullptr;
   if (sound == 0x2be || sound == 0x2c1) stopPlaying(e, o, sound, sound);
   break;
  default: break;
  }
  Slot* s = objectSlotStart(e, o, sound, false);
  if (!s) return nullptr;
  if (o.id == 0xea) o.freeze = sound == 0x336;
  else if (o.id == 0x198) {   // DKrockGC: pitch from the rolling speed
   e.setPitch(s->h, std::clamp(1.8f - value, 0.9f, 1.5f));
   o.freeze = sound == 0x374 || sound == 0x375;
  } else if (o.id == 0x17a && sound == 0x2f9) e.setSeqVariable(s->h, 0, s16(int(2.0f * value)));
  else if (o.id == 0x20a && sound == 0x308) { for (auto& x : o.limited) if (e.attached(x.h) && e.soundId(x.h) == 0x307) { e.stop(x.h, 0x14); break; } }
  return s;
 }
 // Object::HoldSoundLimited: ObjectActor::HoldSoundLimited (80706fd4) or
 // ObjectActorAnim::HoldSoundLimited (806fc700).
 void objectHold(Engine& e, ObjAudio& o, u32 sound, f32 value) {
  if (o.mode == 3) {
   if (o.muted) return;
   if (o.id == 0xcd && sound == 0x23b && o.index % 3 != 0) return;   // one bat in three flaps
   Slot* s = objectSlotStart(e, o, sound, true);
   if (!s || o.id != 0x1a3) return;
   // begoman_spike: louder and higher as it speeds up.
   if (value < 5.0f) e.setVolume(s->h, 0.0f);
   else if (playing(e, o, 0x280)) { e.setVolume(s->h, 0.1f); e.setPitch(s->h, 0.6f); }
   else if (value < 15.0f) { e.setVolume(s->h, 0.3f); e.setPitch(s->h, 0.7f); }
   else { e.setVolume(s->h, 0.5f); e.setPitch(s->h, 1.4f); }
   return;
  }
  if (o.muted && o.id != 0xea) return;
  if (o.id == 0x17a) return;
  if (o.id == 0x25e) o.freeze = true;   // escalator
  Slot* s = objectSlotStart(e, o, sound, true);
  if (sound == 0x20e) objectHold(e, o, 0x20f, 0.0f);
  if (!s) return;
  if (o.id == 0xea) o.freeze = false;
  else if (o.id == 0x198) e.setPitch(s->h, std::clamp(1.8f - value, 0.9f, 1.5f));
 }
 // Object::StartNewSoundLimited: ObjectActor::StartSoundLimited (807072e8) or
 // ObjectActorAnim::StartNewSoundLimited (806fcb20): not while it plays.
 void objectStartNew(Engine& e, ObjAudio& o, u32 sound, f32 value) {
  if (playing(e, o, sound)) return;
  Slot* s = objectStart(e, o, sound, o.mode == 3 ? 0.0f : value);
  if (s && o.id == 0x210 && sound == 0x31b) e.setSeqVariable(s->h, 0, s16(value));
 }
 // Object::StartSound: ObjectActorAnim::StartSound (806fcd28) on the voice
 // handle; a plain ObjectActor has none.
 void objectVoice(Engine& e, ObjAudio& o, u32 sound) {
  if (o.mode != 3) return;
  if (o.muted && o.id != 0xf2 && o.id != 0x171) return;
  const bool voicePlaying = e.attached(o.voice.h);
  const u32 current = voicePlaying ? e.soundId(o.voice.h) : 0;
  if (o.id == 0xcd && sound == 0x23e && (o.index % 3 != 1 || voicePlaying)) return;   // bats
  if (o.id == 0x192 && sound == 0x26a && voicePlaying && current == 0x26a) return;    // choropu
  if ((o.id == 0x193 || o.id == 0xf2) && (sound == 0x292 || sound == 0x293) && playing(e, o, sound)) return;
  o.voice.history = {}; o.voice.flat = false;
  Ambient a = ambientFor(e, sound, o.actor, &o.voice.history);
  if (e.startSound(o.voice.h, sound, nullptr, &a)) noteItem(RaceAudioLayer::QaObject);
 }
 void objectAmbient(Engine& e, ObjAudio& o) {
  for (Slot* s : {&o.limited[0], &o.limited[1], &o.limited[2], &o.limited[3], &o.voice}) {
   if (!e.attached(s->h)) { s->history = {}; continue; }
   e.setAmbient(s->h, ambientFor(e, e.soundId(s->h), o.actor, &s->history));
  }
 }
 // One game frame of course objects: ObjectActor::Update positions, the
 // ambient update, this tick's calls, then the object toggle.
 void objectFrame(Engine& e) {
  for (int n = 0; n < frame.objects && n < int(objects.size()); ++n) {
   const RaceAudioObject& in = frame.object[n];
   ObjAudio& o = objects[n];
   if (!o.live || o.id != in.id || o.mode != in.mode) {
    for (Slot* s : {&o.limited[0], &o.limited[1], &o.limited[2], &o.limited[3], &o.voice}) if (e.attached(s->h)) e.stop(s->h, 0);
    o = ObjAudio{};
    o.live = true; o.id = in.id; o.mode = in.mode; o.index = in.index;
    o.actor.userParam = objectUserParam(in.id, in.mode, in.scale, frame.course);
   }
   Vec3f p = in.position;
   if (o.mode == 1 && o.id == 0x204) p = {3800.0f, 18340.0f, -33000.0f};   // aurora: a fixed point
   else if (o.mode == 1 && (o.id == 0x212 || o.id == 0x216)) p.y += 2500.0f;  // FlamePole_v: above its base
   else if (o.freeze) p = o.actor.position;
   place(o.actor, o.placed, p);
   objectAmbient(e, o);
  }
  for (int n = 0; n < frame.objectSounds; ++n) {
   const RaceAudioObjectSound& c = frame.objectSound[n];
   if (c.slot >= frame.objects || c.slot >= objects.size()) continue;
   ObjAudio& o = objects[c.slot];
   switch (c.call) {
   case RaceAudioObjectSound::Start: objectStart(e, o, c.sound, c.value); break;
   case RaceAudioObjectSound::Hold: objectHold(e, o, c.sound, c.value); break;
   case RaceAudioObjectSound::StartNew: objectStartNew(e, o, c.sound, c.value); break;
   case RaceAudioObjectSound::Voice: objectVoice(e, o, c.sound); break;
   case RaceAudioObjectSound::Animation:
    if(auto* s=objectStart(e,o,c.sound,0.f))e.setVolume(s->h,c.value);
    break;
   default: break;
   }
  }
  for (auto& b : boxActors) if (b.live) objectAmbient(e, b);
  // SortAndToggleActors runs the objects on the frames the racers skip.
  if (toggleOdd) return;
  struct Near { f32 d; int i; };
  std::array<Near, 128> order{}; int count = 0;
  for (int n = 0; n < frame.objects && n < int(objects.size()); ++n) {
   ObjAudio& o = objects[n];
   if (objectAlwaysAudible(o.id)) { o.muted = false; continue; }
   const Vec3f& p = o.actor.position;
   order[count++] = {std::fabs(listener.position.z - p.z) + std::fabs(listener.position.x - p.x) + std::fabs(listener.position.y - p.y), n};
  }
  std::stable_sort(order.begin(), order.begin() + count, [](const Near& a, const Near& b) { return a.d < b.d; });
  for (int j = 0; j < count; ++j) {
   ObjAudio& o = objects[order[j].i];
   const bool muted = j >= ObjectsAudible;
   if (!muted && o.muted) o.actor.velocity = {};
   o.muted = muted;
  }
 }
 // Lakitu::Player::UpdateRespawn / UpdateBackwards: the held loop at Lakitu.
 void lakituFrame(Engine& e) {
  if (frame.lakituSound) {
   place(lakitu.actor, lakitu.placed, frame.lakituPosition);
   lakitu.actor.userParam = 0x12u;   // ObjectActor::DisableConditional / UpdateUserParam3 defaults
  }
  objAmbient(e, lakitu);
  if (frame.lakituSound) objHold(e, lakitu, frame.lakituSound);
 }
 // ItemWarningMgr::PlayTargetedWarning (80713adc): a Red or Blue Shell homing on
 // the local racer within 6000 units: louder and faster as it nears.
 void itemAlarm(Engine& e) {
  int local = -1;
  for (int i = 0; i < frame.karts && i < 12; ++i) if (frame.kart[i].local) { local = i; break; }
  if (local < 0 || frame.kart[local].finished) return;
  bool any = false; f32 dist = 0;
  // Retail objects update by type, then slot; the last caller's distance wins.
  for (u8 object : {u8(1), u8(5)})
   for (int n = 0; n < frame.items; ++n) {
    const RaceAudioItem& it = frame.item[n];
    if (it.object != object || it.target != local || it.explosion || it.scattered) continue;
    const Vec3f t = frame.kart[local].position;
    const f32 d = length(Vec3f{it.position.x - t.x, it.position.y - t.y, it.position.z - t.z});
    if (d > 6000.0f || d < 0.0f) continue;
    any = true; dist = d;
   }
  if (!any) return;
  alarm.flat = true;
  e.holdSound(alarm.h, 0x118);
  const f32 t = (6000.0f - dist) / 6000.0f;
  e.setPan(alarm.h, 0.0f);
  e.setVolume(alarm.h, 0.2f + 0.8f * t);
  e.setTempoRatio(alarm.h, 1.0f + 0.6f * t);
 }
 // RaceMgr::SortAndToggleActors, item list (fn_1_1F47C8 objects 0, 1 and 9):
 // the nearest two thrown shells and Bob-ombs stay audible.
 void itemToggle() {
  itemToggleOdd = !itemToggleOdd;
  if (!itemToggleOdd) return;
  struct Near { f32 d; int i; };
  std::array<Near, 96> order{}; int n = 0;
  for (int k = 0; k < frame.items; ++k) {
   const RaceAudioItem& it = frame.item[k];
   if ((it.object != 0 && it.object != 1 && it.object != 9) || it.explosion || it.scattered) continue;
   const auto& p = it.position;
   order[n++] = {std::fabs(listener.position.z - p.z) + std::fabs(listener.position.x - p.x) + std::fabs(listener.position.y - p.y), it.index};
  }
  std::stable_sort(order.begin(), order.begin() + n, [](const Near& a, const Near& b) { return a.d < b.d; });
  for (int j = 0; j < n; ++j) {
   ItemObjAudio& o = items[order[j].i % items.size()];
   const bool muted = j >= 2;
   if (!muted && o.muted) o.actor.velocity = {};
   o.muted = muted;
  }
 }
 void stopAll(Engine& e) {
  for (auto& k : karts) k.eachSlot([&](Slot& s) { if (e.attached(s.h)) e.stop(s.h, 0); s = Slot{}; });
  for (auto& v : voices) {
   for (Slot* s : {&v.main, &v.echo, &v.limited[0], &v.limited[1], &v.limited[2], &v.limited[3]}) { if (e.attached(s->h)) e.stop(s->h, 0); *s = Slot{}; }
  }
  auto stopItem = [&](ItemObjAudio& o) { for (auto& s : o.limited) { if (e.attached(s.h)) e.stop(s.h, 0); s = Slot{}; } };
  for (auto& o : items) stopItem(o);
  for (auto& o : heldItems) stopItem(o);
  for (auto& c : clouds) stopItem(c.o);
  for (auto& g : gesso) { if (e.attached(g.slot.h)) e.stop(g.slot.h, 0); g.slot = Slot{}; }
  for (auto& a : ambiences) { if (e.attached(a.slot.h)) e.stop(a.slot.h, 0); a = AmbienceAudio{}; }
  for (auto& s : lakitu.limited) { if (e.attached(s.h)) e.stop(s.h, 0); s = Slot{}; }
  for (auto& o : objects) for (Slot* s : {&o.limited[0], &o.limited[1], &o.limited[2], &o.limited[3], &o.voice}) { if (e.attached(s->h)) e.stop(s->h, 0); *s = Slot{}; }
  for (auto& o : boxActors) for (Slot* s : {&o.limited[0], &o.limited[1], &o.limited[2], &o.limited[3]}) { if (e.attached(s->h)) e.stop(s->h, 0); *s = Slot{}; }
  for (Slot* s : {&roulette, &alarm, &global, &oneShot}) { if (e.attached(s->h)) e.stop(s->h, 0); *s = Slot{}; }
 }
};

RaceAudioLayer::RaceAudioLayer() : impl_(new Impl()) { impl_->owner = this; }
RaceAudioLayer::~RaceAudioLayer() { delete impl_; }

bool RaceAudioLayer::push(const RaceAudioFrame& f) {
 const u32 head = head_.load(std::memory_order_relaxed);
 if (head - tail_.load(std::memory_order_acquire) >= Ring) return false;
 ring_[head % Ring] = f;
 head_.store(head + 1, std::memory_order_release);
 return true;
}

void RaceAudioLayer::reset(Engine& engine) {
 impl_->stopAll(engine);
 // Rebuilt in place: an Impl{} temporary is larger than the audio thread's
 // 32 KB stack (SoundSystem::start).
 impl_->~Impl();
 new (impl_) Impl();
 impl_->owner = this;
 tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
}

void RaceAudioLayer::gameFrame(Engine& e) {
 auto& m = *impl_;
 const u32 tail = tail_.load(std::memory_order_relaxed);
 if (tail == head_.load(std::memory_order_acquire)) return;
 m.frame = ring_[tail % Ring];
 tail_.store(tail + 1, std::memory_order_release);
 // Karts present now get their EngineMgr (KartActor::Link -> EngineMgr::Init).
 for (int i = 0; i < m.frame.karts && i < 12; ++i) {
  auto& k = m.karts[i];
  const auto& in = m.frame.kart[i];
  if (in.present && !k.present) {
   k = KartAudio{}; k.present = true; k.local = in.local; k.engine.init(in, m.frame.engineClass);
   // LinkedRaceActor::Link -> KartActor::DisableConditional / UpdateUserParam3:
   // full volume within Distance[2], interior Distance[1] (Audio::Manager
   // +0x8b0 = 2, +0x8ac = 1); KartActor::Link adds 0x300 for a local kart
   // (0x100: no Doppler on the player's own kart).
   k.actor.userParam = 0x12u | (in.local ? 0x300u : 0u);
   m.voiceLink(i);   // the racer's CharacterActor
  }
 }
 // RaceMgr::UpdatePlayerMatrix: the listener from the camera (scale 0.99).
 const Vec3f before = m.listener.position;
 m.listener.lookAt(m.frame.eye, m.frame.target, {0, 1, 0}, 0.99f);
 m.listener.velocity = m.listenerPlaced ? Vec3f{m.listener.position.x - before.x, m.listener.position.y - before.y, m.listener.position.z - before.z} : Vec3f{};
 m.listenerPlaced = true;
 // RaceMgr::Calc -> RaceAmbienceMgr::Update: this tick's zone and intro
 // requests, then the ambience volumes.
 for (int n = 0; n < m.frame.ambienceOps; ++n) m.ambienceOp(e, m.frame.ambienceOp[n]);
 m.ambienceUpdate(e);
 m.lakituFrame(e);
 // Sound update first (ambient from last frame's actor positions); then each
 // kart's physics-time calls, its effects and KartActor::Update. Nothing in
 // the course preview section (KartActor::Update returns at once).
 for (int i = 0; i < 12; ++i) if (m.karts[i].present) { m.updateAmbient(e, m.karts[i]); m.updateVoiceAmbient(e, m.voices[i]); }
 if (m.frame.raceState != 0 && m.frame.raceState != 2) {
  for (int i = 0; i < 12; ++i) if (m.karts[i].present) { m.kartCues(e, i); m.voiceCues(e, i); }
  for (int i = 0; i < 12; ++i) if (m.karts[i].present) m.effects(e, i);
  for (int i = 0; i < 12; ++i) if (m.karts[i].present) m.kartUpdate(e, i);
  for (int i = 0; i < 12; ++i) if (m.karts[i].present) m.voiceUpdate(e, i);
 }
 m.toggle();
 m.voicePitchTracks(e);
 if (m.frame.raceState != 0 && m.frame.raceState != 2) m.itemFrame(e);
 m.objectFrame(e);
 std::uint32_t playing = 0;
 for (auto& k : m.karts) k.eachSlot([&](Slot& s) { playing += e.attached(s.h); });
 framesUsed.fetch_add(1, std::memory_order_relaxed); actorSounds.store(playing, std::memory_order_relaxed);
}
}
