#pragma once
// Race actors' sounds on the audio thread (docs/AUDIO_SOURCE.md "Karts"):
// MKW's Audio::KartActor / EngineMgr / RaceMgr::SortAndToggleActors over the
// engine, positioned by Engine3D (snd_3d.hpp). The main thread hands it one
// RaceAudioFrame per simulation tick (read-only copies of Kinoko state and
// the camera); each SoundCommand::Layer then runs one game frame here.
#include <mkw/race_ambience.hpp>
#include <mkw/snd_3d.hpp>
#include <mkw/sound_system.hpp>
#include <array>
#include <atomic>
#include <cstdint>

namespace mkw {
// Kinoko::Kart::KartMove::SoundCue: where retail kart code calls Audio::KartActor.
namespace cue { enum : std::uint32_t {
 Boost = 1u << 0, BoostRamp = 1u << 1, JumpPad = 1u << 2, JumpPadMushroom = 1u << 3, Hop = 1u << 4,
 Cannon = 1u << 5, Landing = 1u << 6, Horn = 1u << 7, Bumped = 1u << 8, BushWall = 1u << 9,
 BoostVoice = 1u << 10, Trick = 1u << 11, Respawn = 1u << 12, StartFail = 1u << 13, Wheelie = 1u << 14,
 PowerHit = 1u << 15, StarHit = 1u << 16, Bump = 1u << 17, Fall = 1u << 18,
 MegaUse = 1u << 19, MegaEnd = 1u << 20, Grow = 1u << 21, Press = 1u << 22, PressEnd = 1u << 23,
 Spin = 1u << 24, BlowSide = 1u << 25, SoundTrigger = 1u << 26,
 // Presentation cues added on the main thread (not Kinoko's):
 JumpAction = 1u << 27,   // the local driver model starts its trick (fn_1_18BE08: SE_VCL_JUMP_ACTION)
 ZipperTrick = 1u << 28,  // a half-pipe trick starts (fn_1_64AAC 0x805AF9E8: voice 0xb, stadium cheers)
 CoinGet = 1u << 31, // CoinUnit pickup: SE_CC_COIN_GET.
 LavaSplash = 1u << 30, // Original lava impact (USA 806a7a5c), separate from driver voice.
 WaterSplash = 1u << 29, // Effects::Player::CreatePochaEffect (806a7870)
}; }
struct RaceAudioKart {
 snd::Vec3f position;
 snd::Vec3f velocity;           // KartDynamics velocity (Kart::Link::GetSpeed)
 float speed = 0;               // KartMove::speed (Kart::Link::GetEngineSpeed)
 float speedRatioCapped = 0, baseSpeed = 0, startBoostCharge = 0;
 std::uint32_t status[5]{};     // KartState bits = retail KartStatus bitfields 0-4
 std::uint16_t moveFlags = 0;   // KartMove flags (retail Movement+0x254; bit 2 SSMT charged)
 std::uint8_t driftState = 0;   // 0 none, 1 MT charging, 2 MT, 3 SMT
 std::uint8_t wheels = 0;
 std::uint8_t floorType[4]{};   // lowest set KCL type bit of each wheel's closest floor
 std::uint16_t floorVariant[4]{};
 std::uint8_t wallType = 0, wallVariant = 0;   // body's closest wall (Kart::Link wallKclType)
 bool wallContact = false;      // body touched a wall this frame (Effects UpdateFromKartCollision)
 // KartMove::SoundCue bits since the last frame the audio thread took, and the
 // racers whose contact cued Bumped. Accumulated until a frame is delivered.
 std::uint32_t cues = 0;
 std::uint16_t bumpedBy = 0;
 std::uint8_t powerObjectContact = 0; // normal1/wall2 native object callback, even with zero damage.
 bool coinArrival = false; // CoinUnit state6 frame45, retained until push succeeds.
 float bump = 0;                // cue::Bump: the bump's signed magnitude
 std::uint8_t fallVoice = 0;    // cue::Fall: the voice type (0x16-0x19)
 std::uint8_t soundZone = 0;    // cue::SoundTrigger: the course sound zone (0-7)
 std::uint16_t airtime = 0;     // KartState airtime: frames off the ground
 bool slipCharging = false, slipBoost = false;   // drafting, slipstream boost (retail status 64, 65)
 std::uint32_t actionCount = 0; // KartAction starts so far, and the last one started
 std::int8_t action = -1;
 bool finished = false; std::uint8_t rank = 0;   // RaceManager: finished, finishing position
 std::uint8_t vehicle = 0, character = 0, miiVoice = 0;
 bool local = false, present = false;
};
// An item object (RaceItems actor) as Audio::ItemActor sees it.
struct RaceAudioItem {
 snd::Vec3f position;
 std::uint8_t index = 0, kind = 0;   // RaceItems actor slot, ItemKind
 std::uint8_t object = 0;            // retail object id (0 green .. 14 cloud)
 std::uint8_t phase = 0;             // shell guidance state (retail table index)
 std::int8_t target = -1;            // a Red or Blue Shell's target racer
 bool explosion = false, scattered = false, settled = false;
};
// RaceItems::sounds(): ItemSoundCue, holder, actor slot (0xff: none), ItemKind.
struct RaceAudioItemSound { snd::Vec3f position; std::uint8_t cue = 0, player = 0xff, actor = 0xff, kind = 0, object = 0, value = 0; bool flag = false; };
// A course object with a sound actor (ObjFlow sound mode 1 ObjectActor, 3
// ObjectActorAnim), by registry slot, and one of its actor calls this tick.
struct RaceAudioObject { snd::Vec3f position; float scale = 1; std::uint16_t id = 0; std::uint8_t mode = 0; std::uint8_t index = 0; };
struct RaceAudioObjectSound {
 enum Call : std::uint8_t { Start, Hold, StartNew, Voice, Animation };
 std::uint16_t slot = 0, sound = 0; std::uint8_t call = Start; float value = 0;
 float animationPitch = 1;
};
// BalloonUnit has an ObjectActor of its own, outside the course registry.
struct RaceAudioBalloonSound {std::uint8_t unit=0;std::uint16_t sound=0;};
class RaceItems;
struct RaceAudioFrame {
 std::uint32_t tick = 0;
 std::uint8_t course = 0;        // retail course id
 bool mirror = false;
 bool traceObjects = false; // QA only; drained on the main thread, never audio-thread file I/O
 snd::Vec3f eye, target;         // camera view; listener = eye + (target - eye) * 0.99
 std::uint8_t raceState = 0;     // RaceMgr state from RaceSound
 std::uint8_t alteration = 3;    // ItemAlterationMgr state (0 Mega, 1 Star, 2 shrunk, 3 none)
 std::uint8_t engineClass = 2;   // 0 50cc, 1 100cc, 2 150cc
 std::uint8_t karts = 0;
 std::uint8_t mode = 1;          // RaceConfig game mode (0 GP, 1 VS, 3 battle)
 std::uint16_t goalVoice[3]{};   // the local racer's goal voices (TOP, GOD, BAD), 0: none
 std::array<RaceAudioKart, 12> kart{};
 // Items: active objects, this tick's sound cues (kept until delivered), the
 // racers whose roulette spins, and each racer's orbiting triple shells.
 std::uint8_t items = 0, itemSounds = 0;
 std::array<RaceAudioItem, 96> item{};
 std::array<RaceAudioItemSound, 32> itemSound{};
 // Last presented chase icon: original screen-space pan and playback rate.
 bool chaseWarning=false;float chasePan=0,chaseRate=0;
 std::uint16_t roulette = 0;
 std::uint8_t orbit[12]{};
 std::uint16_t cloud[12]{};      // frames left on each racer's Thunder Cloud (0: none)
 // RaceAmbienceMgr requests from RaceSound's SoundTriggers, kept until delivered.
 std::uint8_t ambienceOps = 0;
 std::array<AmbienceOp, 24> ambienceOp{};
 // Lakitu's held sound (0: none) and position.
 std::uint16_t lakituSound = 0;
 snd::Vec3f lakituPosition;
 // Course objects with sound actors (positions every tick) and their calls,
 // kept until delivered.
 std::int8_t soundZone = -1;     // the local racer's SoundTriggerMgr zone (-1 none)
 std::uint8_t introCount = 0;    // RaceAmbienceMgr's intro shot counter
 std::uint8_t objects = 0, objectSounds = 0;
 std::array<RaceAudioObject, 128> object{};
 std::array<RaceAudioObjectSound, 48> objectSound{};
 std::uint8_t balloons=0;std::uint16_t balloonSounds=0;
 std::array<snd::Vec3f,120> balloon{};
 // Kept across a full audio ring just like course object calls.
 std::array<RaceAudioBalloonSound,360> balloonSound{};
};

// Main thread (src/race_audio_frame.cpp): Kinoko's karts and the camera view.
// Sound cues are added to the ones still pending in `frame`; RaceSound clears
// them once a frame reaches the audio thread.
void fillRaceAudioFrame(RaceAudioFrame& frame, snd::Vec3f eye, snd::Vec3f target);
// The item objects and this tick's item sound cues (src/race_audio_frame.cpp).
void fillRaceAudioItems(RaceAudioFrame& frame, const RaceItems& items);
// Course objects with sound actors and their sound cues (src/race_audio_frame.cpp);
// the registry restarts with each race scene.
void fillRaceAudioObjects(RaceAudioFrame& frame);
void resetRaceAudioObjects();

class RaceAudioLayer final : public SoundLayer {
public:
 // Main thread: queue this tick's state (dropped if the audio thread is
 // four frames behind); post a SoundCommand::Layer after it.
 bool push(const RaceAudioFrame& frame);
 void gameFrame(snd::Engine& engine) override;
 void reset(snd::Engine& engine) override;
 // QA: frames consumed and actor sounds playing (audio thread writes), and
 // kart sounds started by family.
 std::atomic<std::uint32_t> framesUsed{0}, actorSounds{0};
 // Effect: item effects on the kart (Mega, shocks, crushes, spins, Bullet, slipstream, ink...).
 enum Family { Road, Slip, Spark, Brake, Landing, Wall, Car, Hop, Dash, DashJ, JumpPad, Horn, Cannon, Effect, Trick, Other, Families };
 std::array<std::atomic<std::uint32_t>, Families> started{};
 std::atomic<std::uint32_t> driftFrames{0};   // audible karts drifting on the ground, summed per frame
 std::atomic<std::uint32_t> voiceStarts{0};   // character voices started
 std::atomic<std::uint32_t> resultVoice{0};   // the last result voice played (sound id)
 std::atomic<std::uint32_t> itemSounds{0};    // item sounds started (all kinds below)
 // ... by kind: one-shots on item objects, object loops, the roulette decide,
 // global (Lightning, POW), the Blooper's screen actor, the Thunder Cloud.
 // QaAmbience counts the course ambience loops and one-shots, QaObject the
 // sounds of object actors (Lakitu).
 enum ItemQa { QaShot, QaLoop, QaDecide, QaGlobal, QaScreen, QaCloud, QaAmbience, QaObject, QaKinds };
 std::array<std::atomic<std::uint32_t>, QaKinds> itemStarted{};
 struct ObjectTrace {
  std::uint32_t tick=0, sound=0, userParam=0;
  std::uint16_t object=0;
  std::uint8_t course=0, index=0, mode=0;
  bool initial=false, started=false, muted=false;
  snd::Vec3f position{}, listener{};
  float distance=0, volume=0, pan=0; // Engine ambient gain, before sequence/envelope/player gain
 };
 bool takeObjectTrace(ObjectTrace& out); // sole consumer: main thread
 std::atomic<std::uint32_t> objectTraceDropped{0};
 struct Impl;
 RaceAudioLayer();
 ~RaceAudioLayer() override;
private:
 static constexpr int Ring = 4;
 std::array<RaceAudioFrame, Ring> ring_{};
 std::atomic<std::uint32_t> head_{0}, tail_{0};
 static constexpr unsigned TraceRing=64;
 std::array<ObjectTrace,TraceRing> objectTrace_{};
 std::atomic<std::uint32_t> traceHead_{0}, traceTail_{0};
 void recordObjectTrace(const ObjectTrace& trace); // sole producer: audio thread
 Impl* impl_;
};
}
