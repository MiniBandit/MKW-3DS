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
 static constexpr std::uint16_t RaceConfirmSlot=31;
 // After MenuTransition::input: the logical state before and after this
 // frame's accepted input, and the input itself. Do not call while the
 // transition blocks input: terminal OK actions keep the logical page.
 void input(const MenuState& before, const MenuState& after, const UiInput& key, UiAction action);
 // Every menu frame (60 Hz): the page on screen drives sections and layers.
 void frame(const MenuState& visual, bool revealed);
 // The race takes over (RaceRSARPlayer); menus resume with frame().
 void leave();
 // The engine was torn down: reload groups/BGM even on the same page.
 void resetAfterEngineRestart(){*this=MenuSound{};}
 // Retail EGG::AudioTrack: a value that moves linearly to a target over frames.
 struct Track {
  float value = 1, target = 1, step = 0; int frames = 0;
  void set(float to, int over) { if (to == value && frames == 0) return; target = to; frames = over; if (!over) value = to; else step = (value - to) / float(over); }
  bool calc() { if (!frames) return false; if (--frames == 0) value = target; else value -= step; return true; }
 };
 enum class Section : std::uint8_t { None, Title, Single, Race, Settings, Present };
 static Section sectionOf(UiPage page);
 // Section_GetAudioStreamIdx (USA 80601300) for the page's retail page id.
 static int layerOf(UiPage page);
private:
 void enterSection(Section section, UiPage page);
 void playVoice(std::uint32_t voice,bool mii,bool kart);
 std::uint32_t pendingMiiVoice_=MenuVoice::None;bool pendingMiiKart_=false;
 void startBgm(UiPage page);
 void applyLayer(int layer, int frames);
 Section section_ = Section::None, loaded_ = Section::None;
 int pendingStart_ = -1, layer_ = 6;
 std::uint32_t bgm_ = 0xFFFFFFFF;
 std::uint16_t slot_ = 1;            // alternates 1/2 like SinglePlayer's two handles
 Track volume_, stems_[4]; float postedVolume_ = -1;
 unsigned unlockCondition_=~0u,unlockBit_=~0u;
 bool unavailable_ = false;
 bool raceConfirmCue_ = false;
 MenuVoice voice_;
};
}
