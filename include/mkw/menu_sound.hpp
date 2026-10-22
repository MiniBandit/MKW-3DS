#pragma once
// Retail menu audio (docs/AUDIO_SOURCE.md "Menus"): per-section groups and
// BGM (Audio::MenuRSARPlayer, SinglePlayer, StreamsMgr), page-layer stem
// mixes for SEQ_O_SELECT, and the UI sounds PushButton/Page/MessageBox play.
// Runs on the main thread and only posts SoundSystem commands.
#include <mkw/menu_ui.hpp>
#include <mkw/menu_voice.hpp>
#include <cstdint>

namespace mkw {
class MenuSound {
public:
 // After MenuTransition::input: the logical state before and after this
 // frame's accepted input, and the input itself. Do not call while the
 // transition blocks input: terminal OK actions keep the logical page.
 void input(const MenuState& before, const MenuState& after, const UiInput& key, UiAction action);
 // Every menu frame (60 Hz): the page on screen drives sections and layers.
 // steps: 60 Hz game frames since the last call; fades and the audio thread's
 // per-frame updates advance once per step (retail calls once per frame).
 void frame(const MenuState& visual, bool revealed, unsigned steps);
 // The race takes over (RaceRSARPlayer); menus resume with frame().
 void leave();
 // A section change's screen fade-out has started (MenuRSARPlayer::Stop,
 // PAL 0x80715C3C, runs on the same frame): music and voices fade over 20 frames.
 void fadeOut();
 // The engine was torn down: reload groups/BGM even on the same page. The title
 // voice state lives in retail singletons (menu character manager, section context).
 void resetAfterEngineRestart(){const TitleVoice title=title_;*this=MenuSound{};title_=title;}
 // Retail EGG::AudioTrack: a value that moves linearly to a target over frames.
 struct Track {
  float value = 1, target = 1, step = 0; int frames = 0;
  void set(float to, int over) { if (to == value && frames == 0) return; target = to; frames = over; if (!over) value = to; else step = (value - to) / float(over); }
  bool calc() { if (!frames) return false; if (--frames == 0) value = target; else value -= step; return true; }
 };
 enum class Section : std::uint8_t { None, Title, Single, Race };
 static Section sectionOf(UiPage page);
 // Section_GetAudioStreamIdx (USA 80601300) for the page's retail page id.
 static int layerOf(UiPage page);
private:
 void enterSection(Section section, UiPage page);
 void startBgm(UiPage page);
 void applyLayer(int layer, int frames);
 Section section_ = Section::None, loaded_ = Section::None;
 int pendingStart_ = -1, layer_ = 6;
 std::uint32_t bgm_ = 0xFFFFFFFF;
 std::uint16_t slot_ = 1;            // alternates 1/2 like SinglePlayer's two handles
 Track volume_, stems_[4]; float postedVolume_ = -1;
 Track popup_;                       // ValuesMgr track 5: the "Not available" duck
 bool unavailable_ = false;
 bool confirmPopup_ = false;         // the no-ghost ConfirmPage is on screen
 MenuVoice voice_;
 // Title screen voice, as retail with unlock flag 0x38 set (every unlock in this port):
 // TitlePage_onInit (PAL 0x8063AB6C) variant, fn_1_3583E0 (0x80868794) pick,
 // fn_1_358284 (0x80868638) play; offsets are the menu character manager's.
 struct TitleVoice {
  std::uint32_t sound = 0xFFFFFFFF;  // +0x18
  int delay = 0;                     // +0x1c
  bool armed = false;                // +0x24
  bool rare = false;                 // +0x10: the last pick was the rare line
  int rareWait = 0;                  // +0x28: plays before another rare line may come
  int variant = 0;                   // TitlePage +0x34c
  int lastVariant = -1;              // section context +0x4c8 (init 0x805E31A4: -1)
  bool active = false;               // TitlePage_onActivate ran for the page on screen
  bool wasTitle = false;             // +0x20 == page 0x57, kept while the voice plays
  Track track;                       // +0x2440: the voice's track volume
  std::uint32_t rng = 0x2545F491u;
  unsigned random(unsigned n) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return n ? rng % n : 0; }
 } title_;
 void titleActivate();
 void titleFrame(UiPage page);
};
}
