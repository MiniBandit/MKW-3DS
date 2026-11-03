// Retail race audio. Addresses are USA StaticR (docs/AUDIO_SOURCE.md "Race").
#include <mkw/race_sound.hpp>
#include <mkw/battle_round_result.hpp>
#include <mkw/race_damage_residency.hpp>
#include <mkw/race_voice_tables.hpp>
#include <mkw/sound_ids.hpp>
#include <mkw/sound_system.hpp>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace mkw {
namespace {
using namespace snd;
constexpr std::uint32_t None = 0xFFFFFFFFu;
constexpr std::uint16_t FanfareSlot = 5;      // RaceMgr+0x2c
constexpr std::uint16_t AlterationSlot = 6;   // ItemAlterationMgr+0x18
constexpr std::uint16_t ResultPointSlot = 8; // independent of the goal voice/fanfare
constexpr std::uint16_t GoalSlot = 7;         // the local KartActor's SE_RC_GOAL

// ItemAlterationMgr::GetCourseSoundId (80708750): table 8089d378, course -> N
// stream; the final-lap stream is N + 1.
constexpr std::uint16_t CourseStream[0x2a] = {
 0x7d, 0x77, 0x79, 0x8b, 0x7b, 0x7f, 0x81, 0x83, 0x75, 0x87, 0x8d, 0x8f, 0x91, 0x93, 0x89, 0x85,
 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0xb1, 0xb3, 0xaf, 0x99, 0x97, 0x9f, 0x9d, 0xa3, 0xa1, 0x9b, 0x95,
 0xb5, 0xb7, 0xbb, 0xb9, 0xbd, 0xbf, 0xc1, 0xc3, 0xc5, 0xc7};
// CharacterActor::GetCharacterGroupId (80861f58): table 808ab4a0, character
// -> GRP_VO_xx_PC (NPC = +1, cannon = +2/+3, goal voices from +4). Miis: none.
constexpr std::int16_t VoiceGroup[24] = {
 0x46, 0xe0, 0x12f, 0x143, 0xed, 0x119, 0xbc, 0x56, 0x109, 0x150, 0x6a, 0x11b,
 0xce, 0xfa, 0x11a, 0x86, 0x77, 0x93, 0xaf, 0x15d, 0xa0, 0x16b, 0x15e, 0x178};
// Dry Bones, Koopa Troopa and King Boo have one voice group for every racer.
bool singleVoiceGroup(unsigned c) { return c == 5 || c == 0xe || c == 0x13; }
// EngineMgr table 8089caa8: vehicle -> first of its engine family's seven
// sounds (UP, LOOP, STR, OFF, DOWN, IDLE, FUKASHI).
constexpr std::uint16_t EngineBase[36] = {
 0x12f, 0x12f, 0x12f, 0x12f, 0x128, 0x128, 0x136, 0x136, 0x136, 0x128, 0x128, 0x128,
 0x128, 0x128, 0x128, 0x128, 0x128, 0x128, 0x144, 0x144, 0x144, 0x14b, 0x144, 0x144,
 0x13d, 0x13d, 0x144, 0x14b, 0x144, 0x14b, 0x128, 0x128, 0x128, 0x128, 0x128, 0x128};
constexpr std::uint16_t EngineFamilies[6] = {0x128, 0x12f, 0x136, 0x13d, 0x144, 0x14b};
// KartActor::PlayHornSound (80702808): sound 0x1ab + vehicle.
constexpr std::uint32_t HornBase = 0x1ab;
// RaceRSARPlayer::LoadCharacterGroups: courses with cannons load cannon voices.
bool cannonCourse(unsigned c) { return c == 6 || c == 0xd || c == 0xb || c == 0x1d || c == 0x13; }
bool battleCourse(unsigned c) { return c >= 0x20 && c < 0x2a; }
// Section::GetSoundId (80600db4) for the intro and race sections of a mode
// (RaceMgr::SetRaceState 2: 0x1e GP/TT, 0x20 VS, 0x28 battle, ...).
std::uint32_t introFanfare(unsigned course) { return battleCourse(course) ? 0x57 : 0x56; }   // STRM_O_(B_)CRS_IN_FAN
std::uint32_t startFanfare(unsigned mode) {
 switch (mode) { case 0: case 1: case 2: return 0x59; default: return 0x5a; }            // STRM_O_START(2)_FAN
}
// fn_1_201E0C (8070a71c): goal music row by mode (table 8089d490), column by
// result: 1st; up to the threshold (table 808cd0b8 by racer count); worse.
std::uint32_t goalMusic(unsigned mode, unsigned rank, unsigned racers) {
 static constexpr std::uint16_t Table[5][3] = {{0x5c, 0x5e, 0x60}, {0x68, 0x69, 0x6a}, {0x62, 0x64, 0x66}, {0x6b, 0x6c, 0x6d}, {0x70, 0x71, 0x72}};
 static constexpr std::uint8_t Threshold[12] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 5};
 const unsigned row = mode == 0 ? 0 : mode == 1 || mode == 7 || mode == 8 ? 1 : mode == 2 || mode == 5 ? 2 : mode == 3 || mode == 9 || mode == 10 ? 3 : 4;
 const unsigned col = rank <= 1 ? 0 : rank <= Threshold[std::clamp(racers, 1u, 12u) - 1] ? 1 : 2;
 return Table[row][col];
}
void post(SoundCommand::Type type, std::uint16_t slot, std::uint32_t id, float value, int frames = 0) {
 SoundCommand c; c.type = type; c.slot = slot; c.id = id; c.value = value; c.frames = frames;
 SoundSystem::instance().post(c);
}
// Goal voice variant per character and result: retail draws one of three
// groups without repeating (VoiceGroupTable); the last pick is kept per run.
std::uint8_t lastGoal[24][3] = {};
std::uint32_t goalVariant(unsigned character, unsigned result) {
 static std::uint32_t seed = 0x2545F491u;
 seed = seed * 1103515245u + 12345u;
 std::uint8_t& last = lastGoal[character][result];
 std::uint8_t pick = std::uint8_t((seed >> 16) % 3);
 if (last && pick + 1 == last) pick = std::uint8_t((pick + 1) % 3);
 last = std::uint8_t(pick + 1);
 return pick;
}
// LINEAR left free for what loads mid-race: course object skin copies (made
// only while over 2 MB is free, course_objects.cpp SkinReserve) and the pause
// and result pages' textures (the menu cache adapts to what is free). Below
// the soft reserve the optional tiers move to the wave cache. The fallback
// cache uses the hard floor: pause releases scenery before loading its UI.
constexpr std::uint32_t LinearReserve = 2048u * 1024, HardReserve = 1024u * 1024;
constexpr std::uint32_t CacheMin = 192u * 1024, CacheMax = 640u * 1024;
}

// ------------------------------------------------------------ SinglePlayer
// StreamsMgr's stem tables (8089c8f8, 8089c908, 8089c918, 8089c948, 8089c958,
// 8089c968): each multi-track course stream's track volumes, one row per state
// (row 0 at PrepareStreams, the others through ChangeStream).
static const float* stemRow(std::uint32_t sound, int row, int& stems) {
 static const float Two[2][2] = {{1, 0}, {0, 1}};
 static const float Water[3][4] = {{1, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 1}};
 static const float Koopa[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
 switch (sound) {
 case 0x7b: case 0x7c: case 0x83: case 0x84: case 0x89: case 0x8a: case 0x8d: case 0x8e:
  stems = 2; return row < 2 ? Two[row] : nullptr;
 case 0x85: case 0x86: stems = 4; return row < 3 ? Water[row] : nullptr;
 case 0x91: case 0x92: stems = 3; return row < 3 ? Koopa[row] : nullptr;
 default: stems = 0; return nullptr;
 }
}
bool RaceSound::Bgm::attached(int i) const { return i >= 0 && SoundSystem::instance().slotAttached(slot[i]); }
void RaceSound::Bgm::setupActive() {
 // SinglePlayer::SetupActiveHandle (806f4380): StreamsMgr::PrepareStreams
 // track volumes for the multi-track course streams.
 if (active < 0) return;
 int n = 0;
 const float* v = stemRow(sound[active], 0, n);
 if (!v) return;
 for (int t = 0; t < n; ++t) post(SoundCommand::StreamTrackVolume, slot[active], 1u << t, v[t]);
}
void RaceSound::Bgm::play(std::uint32_t id, int frames) {
 // SinglePlayer::PlaySound (806f3bc0).
 if (id == None || activeSound() == id) return;
 if (prepared >= 0 && sound[prepared] == id && attached(prepared)) {
  active = prepared; prepared = -1; delay = frames;
  setupActive();
  if (delay < 1) { post(SoundCommand::StartPrepared, slot[active], 0, 0); delay = -1; }
  return;
 }
 int free = -1;
 for (int i = 0; i < 2; ++i) if (!attached(i) && i != prepared) { free = i; break; }
 if (free < 0) { free = prepared >= 0 ? prepared : (active == 0 ? 1 : 0); SoundSystem::instance().stopSlot(slot[free], 0); }
 if (free == prepared) prepared = -1;
 active = free; sound[free] = id; delay = frames;
 if (frames < 1) { SoundSystem::instance().play(id, slot[free]); delay = -1; }
 else post(SoundCommand::Prepare, slot[free], id, 0, 0);
}
void RaceSound::Bgm::prepare(std::uint32_t id, std::uint32_t startSample) {
 // SinglePlayer::PrepareSound (806f3e10) and the Toad's Factory variant
 // fn_1_1E8B24 (806f3f1c), which starts the stream at a sample offset.
 int free = -1;
 for (int i = 0; i < 2; ++i) if (!attached(i) && i != active) { free = i; break; }
 if (free < 0) { free = prepared >= 0 ? prepared : (active == 0 ? 1 : 0); SoundSystem::instance().stopSlot(slot[free], 0); }
 prepared = free; sound[free] = id;
 post(SoundCommand::Prepare, slot[free], id, 0, int(startSample));
}
void RaceSound::Bgm::playPrepared(int frames) {
 // SinglePlayer::PlayPreparedSound (806f4078).
 if (prepared < 0) return;
 active = prepared; prepared = -1; delay = frames;
 setupActive();
 if (delay < 1) { post(SoundCommand::StartPrepared, slot[active], 0, 0); delay = -1; }
}
void RaceSound::Bgm::stop(int frames) {
 // SinglePlayer::StopSound (806f4108).
 if (active >= 0) SoundSystem::instance().stopSlot(slot[active], frames);
 active = -1; delay = -1;
}
void RaceSound::Bgm::calc() {
 // SinglePlayer::Calc (806f3aa8): a delayed start runs when the count ends.
 if (delay > 0) --delay;
 else if (delay == 0) { if (active >= 0) post(SoundCommand::StartPrepared, slot[active], 0, 0); delay = -1; }
}

// ------------------------------------------------------------ RaceRSARPlayer
void RaceSound::begin(const Setup& s) {
 auto& sys = SoundSystem::instance();
 if (active_) quit();
 setup_ = s;
 budget_ = {};
 if (!sys.available()) return;
 active_ = true; paused_ = false; state_ = 0; countdown_ = 0; tick_ = 0; maxLap_ = 1; finished_ = false; rank_ = 1; battleResult_=3;
 goalVoice_[0] = goalVoice_[1] = goalVoice_[2] = 0;
 alteration_ = 3; wasMega_ = wasStar_ = false; wobble_ = 0;
 alterVolume_ = fanfareVolume_ = bgmVolume_ = bgmPitch_ = 1; postedBgmVolume_ = postedBgmPitch_ = -1; postedSlot_ = -1;
 bgm_ = Bgm{};
 triggers_.reset(s.course, s.mirror);
 introLoaded_ = false; introShot_ = -1;
 for (auto& t : stems_) t = AudioTrack{};
 stemCount_ = 0; stemSound_ = 0xFFFFFFFFu;
 bgmValue_ = AudioTrack{}; bgmValue_.value = bgmValue_.target = 1;
 rankShown_ = 0; rankAnim_ = 2; rankFrame_ = 0;

 planPending_ = true; lastFree_ = std::uint32_t(linearSpaceFree()); stableTicks_ = waitTicks_ = 0;
 // RaceRSARPlayer::OnLoad -> PlayersVolumeMgr::Reset (80716c6c): BGM and UI
 // at full volume, every other player from silence over 120 frames.
 for (int p = 0; p < 11; ++p) { players_[p].value = p < 2 ? 1.0f : 0.0f; players_[p].set(1.0f, p < 2 ? 0 : 0x78); postedPlayer_[p] = -1; }
 race_ = Track{};
 // RaceMgr::Init (80708f5c): the intro section's state (2), or the race
 // section's (3) when there is no course preview.
 setRaceState(s.preview ? 2 : 3);
 postVolumes(true);
 sys.setLayer(&layer_);
}

bool RaceSound::prepareScene(){
 if(!SoundSystem::instance().available())return false;
 planScene();
 return budget_.cache>=CacheMin;
}
bool RaceSound::resourcesReady() const{
 const auto& sys=SoundSystem::instance();
 return sys.available()&&!planPending_&&budget_.cache>=CacheMin&&sys.sceneReady()&&sys.stats().cacheBytes>=budget_.cache;
}
void RaceSound::planScene() {
 planPending_ = false;
 auto& sys = SoundSystem::instance();
 const Setup& s = setup_;
 // RaceRSARPlayer::Prepare (8070e584) + LoadCharacterGroups (8070e6cc).
 std::vector<std::uint32_t> groups{id::group::GRP_CMN, id::group::GRP_RACE}, resident, onDemand;
 const unsigned course = s.course;
 if (course < 0x2a) groups.push_back(course + 0xf);
 std::vector<std::uint32_t> voiceGroups;
 bool vehicles[36] = {};
 for (unsigned i = 0; i < s.racers && i < s.racer.size(); ++i) {
  const auto& r = s.racer[i];
  if (r.vehicle < 36) vehicles[r.vehicle] = true;
  if(r.character>=24&&r.character<42){
   const unsigned base=392+4*r.miiVoice;voiceGroups.push_back(base+(r.local?0:1));
   if(cannonCourse(course))voiceGroups.push_back(base+2+(r.local?0:1));
   if(r.local)for(unsigned k=0;k<3;++k)goalVoice_[k]=4021+3*k+118*r.miiVoice+goalVariant(0,k);
   continue;
  }
  if (r.character >= 24 || VoiceGroup[r.character] < 0) continue;
  const std::uint32_t base = std::uint32_t(VoiceGroup[r.character]);
  const bool single = singleVoiceGroup(r.character);
  const std::uint32_t own = base + (r.local || single ? 0 : 1);
  voiceGroups.push_back(own);
  // The result voices: one variant group of each (fn_1_356110); King Boo's
  // are in his own group, Dry Bones and Koopa Troopa use other voices.
  if (r.local && voice::Goal[r.character][0])
   for (unsigned k = 0; k < 3; ++k) {
    const std::uint32_t variant = goalVariant(r.character, k);
    if (!single) voiceGroups.push_back(base + 4 + k * 3 + variant);
    goalVoice_[k] = std::uint16_t(voice::Goal[r.character][k] + variant);
   }
  if (cannonCourse(course) && !single) voiceGroups.push_back(base + 2 + (r.local ? 0 : 1));
 }
 for (auto g : voiceGroups) if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
 // Engine families and horns of vehicles not in this race load on demand.
 for (auto f : EngineFamilies) {
  bool used = false;
  for (unsigned v = 0; v < 36; ++v) used |= vehicles[v] && EngineBase[v] == f;
  if (!used) for (unsigned k = 0; k < 7; ++k) onDemand.push_back(f + k);
 }
 for (unsigned v = 0; v < 36; ++v) if (!vehicles[v]) onDemand.push_back(HornBase + v);

 // Residency under the LINEAR left after the race's graphics (docs/AUDIO_SOURCE.md
 // "Race memory"): the engines and horns of the racers' vehicles and the race
 // UI sounds always stay resident; then, as they fit above the reserve, the
 // race bank, the course group and the player's voices. Every other wave
 // (other racers' voices included) loads into the wave cache when needed.
 std::vector<std::uint32_t> must;
 for (auto f : EngineFamilies) {
  bool used = false;
  for (unsigned v = 0; v < 36; ++v) used |= vehicles[v] && EngineBase[v] == f;
  if (used) for (unsigned k = 0; k < 7; ++k) must.push_back(f + k);
 }
 for (unsigned v = 0; v < 36; ++v) if (vehicles[v]) must.push_back(HornBase + v);
 for (std::uint32_t id = id::SE_UI_TITLE_OUT; id <= id::SE_UI_CTRL_REJECT; ++id) must.push_back(id);   // race UI bank
 appendRaceDamageEffects(must);
 must.push_back(id::SE_ITM_HANE_BOMB_ECHO); // both blast kinds: keep the original echo ready with damage waves
 must.push_back(id::SE_RC_GOAL);
 must.push_back(id::SE_VCL_FALL_WATER);
 if(battleCourse(course)){must.push_back(id::SE_RC_BTL_FINISH);for(unsigned sound=231;sound<=234;++sound)must.push_back(sound);}
 if(s.mode==3&&!s.coinBattle)for(unsigned sound:{0x1f5u,0x1f6u,0x1f7u})must.push_back(sound);
 if(s.coinBattle){must.push_back(0x1f1);must.push_back(0x1f2);} // SE_CC_COIN_GET, reachable only in Coin Runners
 for(auto goal:goalVoice_)if(goal)must.push_back(goal);
 // Rainbow's collision bank belongs to its course group, not GRP_RACE.
 if(course==0x0d)must.push_back(id::SE_VCL_COL_RAINBOW);
 for(unsigned i=0;i<s.racers&&i<s.racer.size();++i)
  if(s.racer[i].local)appendRaceDamageVoices(must,s.racer[i].character);
 SoundSystem::Scene scene;
 scene.groups = groups.data(); scene.groupCount = int(groups.size());
 scene.onDemandSounds = onDemand.data(); scene.onDemandCount = int(onDemand.size());
 scene.residentSounds = must.data(); scene.residentSoundCount = int(must.size());
 scene.keepMs = 400;
 resident = {id::group::GRP_CMN, id::group::GRP_RACE};
 if (course < 0x2a) resident.push_back(course + 0xf);
 for (unsigned i = 0; i < s.racers; ++i) if (s.racer[i].local) resident.push_back(s.racer[i].character<24?std::uint32_t(VoiceGroup[s.racer[i].character]):392+4*s.racer[i].miiVoice);
 // The course stream and the next one (prepared) play from block rings.
 const std::uint32_t stream = course < 0x2a ? std::max(sys.streamBytes(CourseStream[course]), sys.streamBytes(CourseStream[course] + 1)) : 0;
 const std::uint32_t streams = std::max<std::uint32_t>(stream, 64u * 1024) * 2;
 const std::uint32_t free = std::uint32_t(linearSpaceFree());
 budget_.linearFree = free;
 const auto graphicsReserve=std::max(LinearReserve,s.graphicsReserve);
 budget_.budget = free > graphicsReserve + streams ? free - graphicsReserve - streams : 0;
 auto measure = [&]() {
  scene.residentGroups = resident.data(); scene.residentCount = int(resident.size());
  budget_.resident = sys.residentBytes(scene);
 };
 // Demotion order: the player's voices, the course group, the race bank (GRP_CMN stays).
 for (measure(); budget_.resident + CacheMin > budget_.budget && resident.size() > 1; measure()) { resident.pop_back(); ++budget_.demoted; }
 if (budget_.budget >= budget_.resident + CacheMin) budget_.cache = std::min(CacheMax, budget_.budget - budget_.resident);
 else {
  // The measured pause UI is a soft target, not a hard race allocation:
  // entering pause first frees track/sky, and resume frees the UI first.
  // Keep the minimum wave cache when it fits above the hard race floor;
  // otherwise all demoted tyre/drift/boost sounds wait forever for waves.
  // Use remaining headroom up to CacheMax: CacheMin is a floor, not a cap.
  // Coconut car loops plus race waves can pin almost all of a 192 KiB arena.
  const std::uint32_t used = budget_.resident + streams;
  const auto hardReserve=HardReserve;
  budget_.cache = free > used + hardReserve ? std::min(CacheMax, free - used - hardReserve) : 0;
 }
 scene.cacheBytes = budget_.cache;
 sys.loadScene(scene);

}

void RaceSound::trace(const char* what, unsigned value) {
 if (!trace_) return;
 char line[96];

#ifndef MKW_RELEASE
 std::snprintf(line, sizeof line, "race audio tick=%u %s=%u state=%d bgm=%u", tick_, what, value, state_, unsigned(bgm_.active >= 0 ? bgm_.sound[bgm_.active] : 0xFFFF));
 trace_(line, traceContext_);
#endif

}
void RaceSound::setRaceState(int n) {
 // RaceMgr::SetRaceState (8070a020).
 if (n == 5 && state_ == 6) {
  // The final lap reached again over the fast music: the fanfare plays on its own handle.
  SoundSystem::instance().stopSlot(FanfareSlot, 0);
  SoundSystem::instance().play(id::W_BGM_FINALLAP_FAN, FanfareSlot);
  state_ = n;
  return;
 }
 if (bgm_.activeSound() != id::W_BGM_TITLE) bgm_.stop(0);
 const unsigned course = setup_.course;
 const std::uint32_t stream = course < 0x2a ? CourseStream[course] : None;
 auto rampPlayers = [&]() {
  // Players 2-6 silent, then RaceMgr+0x54 brings them to 1.0 over 120 frames.
  race_.value = 0; race_.set(1.0f, 0x78);
 };
 switch (n) {
 case 2:
  rampPlayers();
  bgm_.play(introFanfare(course), 0);
  bgm_.prepare(startFanfare(setup_.mode));
  break;
 case 3:
  // The race section's groups are in (fn_1_1EF700): SoundTriggerMgr::Init.
  triggers_.init();
  rampPlayers();
  bgm_.play(startFanfare(setup_.mode), 0);
  if (stream != None) {
   // Toad's Factory starts its course stream at sample 0x37955 (fn_1_1E8B24).
   bgm_.prepare(stream, course == 4 ? 0x37955 : 0);
  }
  break;
 case 4:
  bgm_.playPrepared(0x24);
  if (course == 4) bgm_.prepare(0x7b);
  break;
 case 5:
  bgm_.play(id::W_BGM_FINALLAP_FAN, 0);
  if (stream != None) {
   bgm_.prepare(stream + 1, course == 4 ? 0x2ce6c : 0);
  }
  break;
 case 6:
  bgm_.playPrepared(0);
  if (course == 4) bgm_.prepare(0x7c);
  break;
 case 7:
  SoundSystem::instance().stopSlot(FanfareSlot, 0);
  fanfareVolume_ = 1;
  bgm_.play(setup_.mode==3?battleGoalMusic(battleResult_):goalMusic(setup_.mode, rank_, setup_.racers), 0x1e);
  break;
 default: break;
 }
 state_ = n;
 trace("state", unsigned(n));
}

void RaceSound::applyMusic(const MusicOp& op) {
 switch (op.kind) {
 case MusicOp::Stems: {
  // StreamsMgr::ChangeStream (806f5bc4): the active stream's stems to a table row.
  const std::uint32_t sound = bgm_.activeSound();
  int n = 0;
  const float* row = stemRow(sound, op.row, n);
  if (!row) break;
  for (int t = 0; t < n; ++t) stems_[t].set(row[t], op.frames);
  break;
 }
 case MusicOp::Volume:
  // SinglePlayerValuesMgr::SetVolume (806f4ebc): a target equal to the value moves from +0.01.
  if (bgmValue_.value == op.value) bgmValue_.value += 0.01f;
  bgmValue_.set(op.value, op.frames);
  break;
 case MusicOp::FactoryRestart:
  // SoundTriggerMgr::ApplyTrigger, Toad's Factory zone 5: the stream prepared
  // after GO starts over; on the first lap another copy is prepared.
  bgm_.stop(0);
  bgm_.playPrepared(op.frames);
  if (op.row) bgm_.prepare(id::STRM_N_FACTORY_N);
  break;
 }
}
void RaceSound::calcStems() {
 // StreamsMgr::Calc (806f54b8): each stem moves, clamped to [0, 1], onto the
 // active stream. A newly active stream starts from row 0 (PrepareStreams,
 // posted by Bgm::setupActive).
 const std::uint32_t sound = bgm_.activeSound();
 if (sound != stemSound_) {
  stemSound_ = sound;
  const float* row = stemRow(sound, 0, stemCount_);
  for (int t = 0; t < stemCount_; ++t) { stems_[t].value = row[t]; postedStem_[t] = row[t]; }
 }
 if (!stemCount_ || bgm_.active < 0) return;
 for (int t = 0; t < stemCount_; ++t) {
  stems_[t].calc(); stems_[t].clamp(0, 1);
  if (stems_[t].value != postedStem_[t]) {
   post(SoundCommand::StreamTrackVolume, bgm_.slot[bgm_.active], 1u << t, stems_[t].value);
   postedStem_[t] = stems_[t].value;
  }
 }
}
void RaceSound::rankHud(unsigned rank) {
 // CtrlRaceRankNum::OnUpdate (807ea3e0), with the HUD's animation timing
 // (race_hud.cpp: close 16 frames, open 11): a new position closes the
 // number, and SE_RC_RANK_UP / _DOWN play as the new number is shown.
 auto show = [&]() {
  SoundSystem::instance().play(rank < rankShown_ ? id::SE_RC_RANK_UP : id::SE_RC_RANK_DOWN);
  rankShown_ = rank;
 };
 if (!rankShown_) rankShown_ = rank;
 if (rank != rankShown_) { if (rankAnim_ == 2) { rankAnim_ = 0; rankFrame_ = 0; } else if (rankAnim_ == 1) show(); }
 ++rankFrame_;
 if (rankAnim_ == 0 && rankFrame_ >= 16) { rankAnim_ = 1; rankFrame_ = 0; if (rank != rankShown_) show(); }
 else if (rankAnim_ == 1 && rankFrame_ >= 11) { rankAnim_ = 2; rankFrame_ = 0; }
}

void RaceSound::previewEnded() {
 if (!active_ || state_ != 2) return;
 // The race section starts: RaceRSARPlayer::Prepare -> RaceMgr::Init -> state 3.
 setRaceState(3);
}

void RaceSound::checkRaceState(const Frame& f) {
 // RaceMgr::CheckRaceState (8070ab30), race modes.
 if (state_ == 1 || state_ == 2 || state_ == 7) return;
 if (f.stage == 4) { setRaceState(7); return; }
 if (state_ == 5) { if (!bgm_.attached(bgm_.active)) setRaceState(6); return; }
 if (state_ == 6 && f.finished) setRaceState(7);
}

void RaceSound::updateAlteration(const Frame& f) {
 // ItemAlterationMgr::Update (8070826c) + UpdateStatus (80708424); the
 // RaceMgr::IsAPlayerIn* tests only count in race states 4 and 6.
 if (bgm_.active < 0) return;
 const bool racing = state_ == 4 || state_ == 6;
 const bool mega = racing && f.mega, star = racing && f.star, small = racing && f.shocked;
 int next;
 if (alteration_ == 0) next = !star ? (!mega ? (!small ? 3 : 2) : 0) : (!mega ? 1 : (!wasStar_ ? 1 : 0));
 else if (alteration_ == 1) next = !mega ? (!star ? (!small ? 3 : 2) : 1) : (!star ? 1 : (!wasMega_ ? 0 : 1));
 else next = !mega ? (!star ? (!small ? 3 : 2) : 1) : 0;
 alteration_ = next; wasMega_ = mega; wasStar_ = star;
 switch (alteration_) {
 case 0: alterVolume_ = 0.01f; bgmPitch_ = 1; SoundSystem::instance().hold(id::SE_ITM_BIG_KINOKO_STATE, AlterationSlot); break;
 case 1: alterVolume_ = 0.01f; bgmPitch_ = 1; SoundSystem::instance().hold(id::SE_ITM_STAR_STATE, AlterationSlot); break;
 case 2:
  // Pitch wobble: 1 + sin(phase) * (0.1 in race state 4, else 0.07), phase += 2pi/15.
  wobble_ += 0.41887903f;
  alterVolume_ = 1; bgmPitch_ = 1.0f + std::sin(wobble_) * (state_ == 4 ? 0.1f : 0.07f);
  break;
 default: alterVolume_ = 1; bgmPitch_ = 1; wobble_ = 0; break;
 }
}

void RaceSound::postVolumes(bool force) {
 // PlayersVolumeMgr::Calc (80716ab8), then RaceMgr::Calc's override of
 // players 2-6 while its start fade runs.
 for (int p = 0; p < 11; ++p) {
  float v = players_[p].value;
  if (race_.moving() && p >= 2 && p <= 6) v = race_.value;
  if (force || v != postedPlayer_[p]) { post(SoundCommand::PlayerVolume, 0, std::uint32_t(p), v); postedPlayer_[p] = v; }
 }
 // SinglePlayerValuesMgr::Calc (806f4d24): product of the volume values, pitch.
 bgmVolume_ = alterVolume_ * fanfareVolume_ * bgmValue_.value;
 const int slot = bgm_.active >= 0 ? bgm_.slot[bgm_.active] : -1;
 if (slot < 0) return;
 if (force || slot != postedSlot_ || bgmVolume_ != postedBgmVolume_) { post(SoundCommand::Volume, std::uint16_t(slot), 0, bgmVolume_); postedBgmVolume_ = bgmVolume_; }
 if (force || slot != postedSlot_ || bgmPitch_ != postedBgmPitch_) { post(SoundCommand::Pitch, std::uint16_t(slot), 0, bgmPitch_); postedBgmPitch_ = bgmPitch_; }
 postedSlot_ = slot;
}

void RaceSound::step(const Frame& f, RaceAudioFrame* actors) {
 if (!active_ || paused_) return;
 auto& sys = SoundSystem::instance();
 for(unsigned i=0;i<4;++i)if(f.battlePointCues&(1u<<i))sys.play(231+i);
 if(f.resultPointsChanged)sys.hold(id::SE_RSLT_POINT_UP,ResultPointSlot);
 if(f.resultSlideSound>=0)sys.play(unsigned(f.resultSlideSound));
 ++tick_;
#ifndef MKW_RELEASE
 // Bounded release diagnostics for silent race actors; no per-frame file I/O.
 if (tick_ == 300 || tick_ == 900 || tick_ == 1800) {
  const auto st = sys.stats();
  if (FILE* log = std::fopen("sdmc:/3ds/mkw-3ds/audio-race-status.txt", "a")) {
   std::fprintf(log, "tick=%u course=%u ready=%d actor_frames=%lu actor_sounds=%lu voices=%lu missing=%lu failed=%lu last=%lu/%lu/%lu cache=%lu/%lu waves=%lu linear=%lu\n",
    tick_, setup_.course, int(sys.sceneReady()), (unsigned long)layer_.framesUsed.load(),
    (unsigned long)layer_.actorSounds.load(), (unsigned long)st.voices, (unsigned long)st.missing,
    (unsigned long)st.failed, (unsigned long)st.lastMissingSound, (unsigned long)st.lastMissingFile,
    (unsigned long)st.lastMissingWave, (unsigned long)st.cacheUsed, (unsigned long)st.cacheBytes,
    (unsigned long)sys.residentWaveBytes(), (unsigned long)linearSpaceFree());
   std::fclose(log);
  }
 }
#endif
 if (planPending_) {
  // LINEAR settled (45 unchanged ticks) or 150 ticks into the race: plan now.
  const std::uint32_t free = std::uint32_t(linearSpaceFree());
  stableTicks_ = free == lastFree_ ? stableTicks_ + 1 : 0; lastFree_ = free;
  if (stableTicks_ >= 45 || ++waitTicks_ >= 150) {
   planScene();
   if (trace_) {
    char line[160];

#ifndef MKW_RELEASE
    std::snprintf(line, sizeof line, "race sound plan tick=%u linear_free=%lu budget=%lu resident=%lu cache=%lu demoted=%u", tick_,
                  (unsigned long)budget_.linearFree, (unsigned long)budget_.budget, (unsigned long)budget_.resident, (unsigned long)budget_.cache, budget_.demoted);
    trace_(line, traceContext_);
#endif

   }
  }
 }
 // KartActor::UpdateLapSounds (USA 80704980): crossing the line plays
 // SE_RC_GOAL once. Keep feeding the actor layer: CharacterActor::Update
 // owns the delayed result voice and still runs during the finish camera.
 if (f.finished && !finished_) {
  finished_ = true; rank_ = f.rank; battleResult_=f.battleResultKind;
  sys.stopSlot(AlterationSlot, 0);
  // Normal races deliver the cue with the persistent actor frame.
  if(!actors)sys.play(battleCourse(setup_.course) ? id::SE_RC_BTL_FINISH : id::SE_RC_GOAL, GoalSlot);
  triggers_.ambience.clear(); triggers_.music.clear();
  bgmValue_ = AudioTrack{}; bgmValue_.value = bgmValue_.target = 1;
  setRaceState(7);
  trace("goal", f.rank);
 }
 // RaceHUD::AfterControlUpdate (80835a28): a beep each second of the
 // countdown, GO at 240; battle courses use the _BTL variants (RaceRSARPlayer::PlaySound).
 if (!finished_ && f.stage >= 1 && countdown_ < 4 && f.timer == unsigned(countdown_ + 1) * 60) {
  const bool go = countdown_ == 3;
  const bool battle = battleCourse(setup_.course);
  sys.play(go ? (battle ? id::SE_RC_GO_BTL : id::SE_RC_GO) : (battle ? id::SE_RC_321_BTL : id::SE_RC_321));
  trace(go ? "go" : "count", f.timer);
  if (go) setRaceState(4);   // RaceMgr::StartRace (80709974)
  ++countdown_;
 }
 // KartActor::UpdateLapSounds (807048ac): Raceinfo maxLap of the local player.
 int lap = maxLap_;
 if (!finished_ && f.currentLap > lap && lap < int(setup_.lapCount)) lap = f.currentLap;
 if (lap != maxLap_) {
  maxLap_ = lap;
  if (lap == int(setup_.lapCount)) { if (state_ == 4 || state_ == 6) setRaceState(5); }
  else sys.play(id::SE_RC_LAP);
  trace("lap", unsigned(lap));
 }
 // RaceAmbienceMgr::PlayIntroAmbience: once the race groups are in
 // (fn_1_1EF700), then at each course-preview shot change (fn_1_9D214).
 if (state_ == 2) {
  if (!introLoaded_ && !planPending_ && sys.sceneReady()) { introLoaded_ = true; triggers_.intro(); }
  if (introShot_ >= 0 && f.introShot > introShot_) triggers_.intro();
 }
 introShot_ = f.introShot;
 // KartActor::ApplyKCLSoundTrigger (807021a0): the local racer's sound zone.
 if (actors && !finished_)
  for (int i = 0; i < actors->karts && i < 12; ++i) {
   const RaceAudioKart& k = actors->kart[i];
   if (k.local && (k.cues & cue::SoundTrigger) && k.soundZone <= 7) triggers_.apply(k.soundZone, f.finished, state_, unsigned(maxLap_));
  }
 for (const MusicOp& op : triggers_.music) applyMusic(op);
 triggers_.music.clear();
 if (!finished_ && f.hudRank) rankHud(f.hudRank);
 // RaceRSARPlayer::Calc (8070e9c8): SinglePlayer::Calc, then RaceMgr::Calc.
 bgm_.calc();
 updateAlteration(f);
 for (auto& p : players_) p.calc();
 race_.calc();
 calcStems();
 bgmValue_.calc(); bgmValue_.clamp(0, 1);
 checkRaceState(f);
 fanfareVolume_ = sys.slotAttached(FanfareSlot) ? 0.0f : 1.0f;
 postVolumes(false);
 // The race actors' state for this frame, then one game frame for the audio
 // thread (RaceAudioLayer, then the BasicSound updates).
 if (actors) {
  RaceAudioFrame& a = *actors;
  // RaceAmbienceMgr requests, in order; any that do not fit wait a tick.
  std::size_t moved = 0;
  while (moved < triggers_.ambience.size() && a.ambienceOps < a.ambienceOp.size()) a.ambienceOp[a.ambienceOps++] = triggers_.ambience[moved++];
  triggers_.ambience.erase(triggers_.ambience.begin(), triggers_.ambience.begin() + std::ptrdiff_t(moved));
  a.tick = tick_; a.course = std::uint8_t(setup_.course); a.mode = std::uint8_t(setup_.mode);
  a.soundZone = std::int8_t(triggers_.zone()); a.introCount = std::uint8_t(std::min(triggers_.introCount(), 255u));
  for (int k = 0; k < 3; ++k) a.goalVoice[k] = goalVoice_[k];
  // Retail loads the race groups before the scene runs; here course object
  // calls wait until they are in rather than failing each frame.
  if (!introLoaded_) a.objectSounds = 0;
  a.raceState = std::uint8_t(state_); a.alteration = std::uint8_t(alteration_); a.engineClass = std::uint8_t(std::min(setup_.engineClass, 2u));
  // A frame dropped by a full ring keeps its cues for the next one.
  if (layer_.push(a)) { for (auto& k : a.kart) { k.cues = 0; k.bumpedBy = 0; k.powerObjectContact = 0; k.coinArrival = false; } a.itemSounds = 0; a.ambienceOps = 0; a.objectSounds = 0; a.balloonSounds = 0; }
 }
 SoundCommand c; c.type = SoundCommand::Layer; sys.post(c);
}

void RaceSound::pause(bool paused) {
 // RaceRSARPlayer::Pause (8070e428) / Resume (8070e4bc).
 if (!active_ || paused == paused_) return;
 paused_ = paused;
 auto& sys = SoundSystem::instance();
 // Resume cue is posted at accepted menu input; this only resumes players.
 if(paused)sys.play(id::SE_RC_PAUSE_ON);
 if (bgm_.delay < 1 && bgm_.active >= 0) post(SoundCommand::Pause, bgm_.slot[bgm_.active], 0, paused ? 1.0f : 0.0f, 0);
 // PlayersVolumeMgr::PauseAllButTrackUI: every player but BGM and menu UI.
 for (std::uint32_t p = 2; p < 11; ++p) post(SoundCommand::PausePlayer, 0, p, paused ? 1.0f : 0.0f, 10);
}

void RaceSound::carryResultMusic(){
 if(!active_||!finished_||bgm_.active<0)return;
 auto& sys=SoundSystem::instance();
 const int i=bgm_.active;
 if(bgm_.delay>=0)post(SoundCommand::StartPrepared,bgm_.slot[i],0,0);
 sys.carryMusic(bgm_.sound[i],bgm_.slot[i]);
 bgm_.active=-1;bgm_.delay=-1;
 // Removing the layer resets every actor-owned loop on the audio thread.
 // quit leaves the transferred music handle alone and stops race players.
 quit();
 // The load starts now: no released engine/item/voice tail accompanies it.
 sys.stopSlot(FanfareSlot,0);sys.stopSlot(AlterationSlot,0);
 for(std::uint32_t player=2;player<11;++player)post(SoundCommand::StopPlayer,0,player,0,0);
}

void RaceSound::quit() {
 // RaceRSARPlayer::Stop (8070e3c0): BGM out over 20 frames; MuteAllButTrackUI.
 if (!active_) return;
 auto& sys = SoundSystem::instance();
 // A paused sound stops at once (BasicSound::Stop).
 bgm_.stop(0x14);
 if (bgm_.prepared >= 0) sys.stopSlot(bgm_.slot[bgm_.prepared], 0);
 sys.stopSlot(FanfareSlot, 0x14); sys.stopSlot(AlterationSlot, 0x14);
 for (std::uint32_t p = 2; p < 11; ++p) { post(SoundCommand::StopPlayer, 0, p, 0, 0x14); post(SoundCommand::PausePlayer, 0, p, 0.0f, 0); }
 sys.setLayer(nullptr);
 active_ = false; paused_ = false;
}
}
