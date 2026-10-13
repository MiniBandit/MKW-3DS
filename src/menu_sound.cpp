// Retail menu audio. Addresses are USA StaticR (docs/AUDIO_SOURCE.md).
#include <mkw/menu_sound.hpp>
#include <mkw/sound_ids.hpp>
#include <mkw/sound_system.hpp>

namespace mkw {
namespace {
using namespace snd;
constexpr std::uint16_t BgmSlots[2] = {1, 2};
constexpr std::uint16_t VoiceSlot = 3;
// Retail page ids (Section::CreatePageById jump table, 805f24dc).
int retailPage(UiPage page) {
 switch (page) {
 case UiPage::Title: return 0x57;
 case UiPage::MainMenu: return 0x5a;
 case UiPage::License: return 0x65;
 case UiPage::Mode: return 0x69;
 case UiPage::EngineClass: return 0x6a;
 case UiPage::Character: return 0x6b;
 case UiPage::Vehicle: return 0x6c;
 case UiPage::Drift: return 0x6d;
 case UiPage::Cup: return 0x6e;
 case UiPage::Course: return 0x6f;
 case UiPage::Ghost: return 0x71;
 case UiPage::VSMode: return 0x72;
 case UiPage::BattleMode: return 0x75;
 case UiPage::BattleCup: return 0x78;
 case UiPage::BattleStage: return 0x79;
 default: return -1;
 }
}
// SEQ_O_SELECT stem volumes per layer (StreamsMgr tables 8089c7e8/f8/808).
constexpr float SelectStems[3][4] = {{1.0f, 0, 0, 0}, {0.93f, 1.0f, 0, 1.0f}, {0.93f, 1.0f, 1.0f, 0}};
void post(SoundCommand::Type type, std::uint16_t slot, std::uint32_t id, float value, int frames = 0) {
 SoundCommand c; c.type = type; c.slot = slot; c.id = id; c.value = value; c.frames = frames;
 SoundSystem::instance().post(c);
}
void ui(std::uint32_t id) { SoundSystem::instance().play(id, 0); }
// The Mii select voices (BNK_VO_MENU_MII) can never play: Mii slots are
// shown as unavailable in this port. Everything else in GRP_MENU loads.
constexpr std::uint32_t MenuSkip[] = {id::bankfile::BNK_VO_MENU_MII};
void loadSections(std::uint32_t a, std::uint32_t b, int keepMs) {
 std::uint32_t groups[3]; int n = 0;
 groups[n++] = id::group::GRP_CMN;
 if (a != 0xFFFFFFFF) groups[n++] = a;
 if (b != 0xFFFFFFFF && b != a) groups[n++] = b;
 SoundSystem::instance().loadScene(groups, n, keepMs, MenuSkip, 1);
}
std::uint32_t groupOf(MenuSound::Section s) {
 // Section::GetGroupId (80601044): title sections GRP_TITLE, single player GRP_MENU.
 return s == MenuSound::Section::Title ? std::uint32_t(id::group::GRP_TITLE) : s == MenuSound::Section::Single ? std::uint32_t(id::group::GRP_MENU) : 0xFFFFFFFFu;
}
}

MenuSound::Section MenuSound::sectionOf(UiPage page) {
 switch (page) {
 case UiPage::Strap: case UiPage::Intro: return Section::None;
 case UiPage::Title: case UiPage::License: case UiPage::MainMenu: return Section::Title;  // sections 0x3f-0x44
 case UiPage::Race: case UiPage::Pause: case UiPage::QuitConfirm: case UiPage::Results: return Section::Race;
 default: return Section::Single;                                                            // section 0x48
 }
}

int MenuSound::layerOf(UiPage page) {
 // Section_GetAudioStreamIdx (80601300) on the retail page id.
 switch (retailPage(page)) {
 case 0x57: case 0x69: case 0x6a: case 0x72: case 0x75: return 0;
 case 0x6b: case 0x6c: case 0x6d: return 1;
 case 0x6e: case 0x6f: case 0x71: case 0x78: case 0x79: return 2;
 case 0x5a: case 0x65: return 5;
 default: return 6;
 }
}

void MenuSound::input(const MenuState& before, const MenuState& after, const UiInput& key, UiAction action) {
 if (!SoundSystem::instance().available()) return;
 const bool pageChanged = before.page != after.page;
 if (after.unavailable && !before.unavailable) {
  // A "Not available" message: MessageBox BeforeEntranceAnimations (SE_UI_CTRL_WIN_POPUP).
  ui(id::SE_UI_CTRL_WIN_POPUP);
  volume_.set(0.55f,30); // MessageBoxTransparent805d8998: duck over30 frames.
  unavailable_ = true;
  return;
 }
 if (unavailable_ && !after.unavailable) {
  ui(id::SE_UI_CTRL_WIN_POPDOWN);unavailable_ = false;
  volume_.set(layerOf(after.page)==5?0.55f:1.f,30);
 } // AfterExitAnimations restores the underlying page volume.
 if(before.page==UiPage::EngineClass&&after.page==before.page&&
    before.selection.mixedEngineTypes!=after.selection.mixedEngineTypes){
  // GPClassSelect::OnStartPress (USA 80825200-24): toggle +0x6c4,
  // then sound 0x14 + the new boolean (sheet left/right).
  ui(after.selection.mixedEngineTypes?id::SE_UI_SHEET_RIGHT:id::SE_UI_SHEET_LEFT);
  return;
 }
 // Pages::RaceMenu::OnButtonClick (80838664): Continue has no button sound
 // (SE_RC_PAUSE_OFF at accepted input, before scenery restoration); leaving the race (restart,
 // quit, the end-of-race choices) plays SE_RC_PAUSE_EXIT_GAME
 // (PlayEndRaceMenuButtonClickSound, 8070e54c).
 if (before.page == UiPage::Pause || before.page == UiPage::QuitConfirm || before.page == UiPage::Results) {
  if (after.page == UiPage::Race) {
   // Resume feedback belongs to the accepted button, before scenery reload.
   ui(id::SE_RC_PAUSE_OFF);return;
  }
  const bool leaving = action == UiAction::RestartRace || action == UiAction::StartRace ||
                       (before.page==UiPage::Pause&&before.cursor==1&&key.accept&&!key.back) ||
                       (pageChanged && after.page != UiPage::Pause && after.page != UiPage::QuitConfirm && after.page != UiPage::Results && (key.accept || key.start));
  if (leaving) { ui(id::SE_RC_PAUSE_EXIT_GAME); return; }
 }
 if (pageChanged && key.back) {
  // Page::StartExit (805de224) back: transition sound 0x40, default SE_UI_PAGE_PREV.
  // Page defaults to {forward:0, back:6}. These page overrides explicitly
  // set both to zero (CourseSelect 80825918, BattleStageSelect 80852580,
  // YesNo 8061ea98, RaceMenu 80837f84, Title 8060999c).
  if (before.page != UiPage::Title && before.page != UiPage::Intro &&
      before.page != UiPage::Course && before.page != UiPage::BattleStage &&
      before.page != UiPage::QuitConfirm && before.page != UiPage::Results)
   ui(id::SE_UI_PAGE_PREV);
  return;
 }
 const bool samePageOk=before.page==UiPage::Confirm&&key.accept&&!key.back&&
                       !after.unavailable&&before.supportsRace();
 if ((pageChanged||samePageOk) && (key.accept || key.start)) {
  // PushButton::HandleClick (805b66b4): the button's selection sound,
  // SE_UI_BTN_OK unless the page set another. MainMenu (80831af8): SE_UI_TITLE_OUT.
  // Title (80609c28) and its press-A pages: SE_UI_BTN_OK.
  if (before.page == UiPage::Strap || before.page == UiPage::Intro) return;
  ui(before.page == UiPage::MainMenu ? id::SE_UI_TITLE_OUT :
     before.page == UiPage::Confirm ? id::SE_UI_RACE_OK : id::SE_UI_BTN_OK);
  const auto voice=voice_.input(before,after,key);
  if(voice!=MenuVoice::None){
   // Retail menu voice player (80864038): stop the preceding player voice
   // over two frames, then start at 0.85 volume on its own handle.
   auto& sound=SoundSystem::instance();
   sound.stopSlot(VoiceSlot,2);sound.play(voice,VoiceSlot);
   post(SoundCommand::Volume,VoiceSlot,0,0.85f);
   // Kart confirmation sets local sequence variable 12, including voices
   // that fall back to the character-selection range (80864154).
   if(before.page==UiPage::Vehicle)post(SoundCommand::SeqVariable,VoiceSlot,12,1.f);
  }
  return;
 }
 // PushButton::HandleSelect (805b6358): SE_UI_BTN_IN on the newly focused button.
 if (!pageChanged && before.cursor != after.cursor && sectionOf(after.page) != Section::None && after.page != UiPage::Title) ui(id::SE_UI_BTN_IN);
}

void MenuSound::applyLayer(int layer, int frames) {
 // MenuRSARPlayer::OnNewPageLayer (8070c8d0): layers 0-3 restore BGM volume
 // (1.0) and change stems over 30 frames; layer 5 ducks to 0.55.
 if (layer == 6) return;
 volume_.set(layer == 5 || unavailable_ ? 0.55f : 1.0f, frames);
 if (bgm_ == id::SEQ_O_SELECT && layer <= 2) for (int t = 0; t < 4; ++t) stems_[t].set(SelectStems[layer][t], frames);
}

void MenuSound::startBgm(UiPage page) {
 // MenuRSARPlayer::OnLoad (8070e130) -> SinglePlayer::PlayBGSound -> PlaySound(Section::GetSoundId).
 const std::uint32_t sound = section_ == Section::Title ? std::uint32_t(id::W_BGM_TITLE) : std::uint32_t(id::SEQ_O_SELECT);
 slot_ = slot_ == BgmSlots[0] ? BgmSlots[1] : BgmSlots[0];
 bgm_ = sound;
 SoundSystem::instance().play(sound, slot_);
 // SinglePlayer::StartSound (806f4180): PrepareStreams with the current layer's stems.
 const int layer = layerOf(page);
 if (sound == id::SEQ_O_SELECT) {
  const int l = layer == 1 || layer == 2 ? layer : 0;
  for (int t = 0; t < 4; ++t) { stems_[t].value = stems_[t].target = SelectStems[l][t]; stems_[t].frames = 0; post(SoundCommand::TrackVolume, slot_, 1u << t, stems_[t].value); }
 }
 // OnLoad: SinglePlayerValuesMgr::SetVolume(1.0, 3, 10); the page layer then applies.
 volume_.value = 0; volume_.set(1.0f, 10); postedVolume_ = -1;
 layer_ = layer;
 if (layer == 5 || unavailable_) volume_.set(0.55f, 30);
}

void MenuSound::enterSection(Section next, UiPage page) {
 auto& sound = SoundSystem::instance();
 // MenuRSARPlayer::Stop (8070e198): the old BGM fades over 20 frames.
 if (bgm_ != 0xFFFFFFFF) sound.stopSlot(slot_, 20);
 bgm_ = 0xFFFFFFFF;
 section_ = next;
 // GRP_CMN (UI sounds) stays resident; the old group is released after its
 // BGM's 20-frame fade.
 loadSections(groupOf(next), 0xFFFFFFFF, 400);
 loaded_ = next;
 pendingStart_ = next == Section::Title || next == Section::Single ? 1 : -1;
 (void)page;
}

void MenuSound::frame(const MenuState& visual, bool revealed) {
 auto& sound = SoundSystem::instance();
 if (!sound.available()) return;
 const Section next = sectionOf(visual.page);
 if (revealed && next != section_ && next != Section::Race) enterSection(next, visual.page);
 if (pendingStart_ > 0 && sound.sceneReady()) { pendingStart_ = -1; startBgm(visual.page); }
 const int layer = layerOf(visual.page);
 if (bgm_ != 0xFFFFFFFF && layer != layer_) { applyLayer(layer, 30); if (layer != 6) layer_ = layer; }
 // StreamsMgr::Calc / SinglePlayerValuesMgr::Calc (806f54b8 / 806f4d24): per frame.
 if (bgm_ != 0xFFFFFFFF) {
  volume_.calc();
  if (volume_.value != postedVolume_) { post(SoundCommand::Volume, slot_, 0, volume_.value); postedVolume_ = volume_.value; }
  if (bgm_ == id::SEQ_O_SELECT) for (int t = 0; t < 4; ++t) if (stems_[t].calc()) post(SoundCommand::TrackVolume, slot_, 1u << t, stems_[t].value);
 }
 // One game frame for the audio thread's BasicSound updates.
 SoundCommand c; c.type = SoundCommand::Layer; sound.post(c);
}

void MenuSound::leave() {
 auto& sound = SoundSystem::instance();
 if (!sound.available()) return;
 // MenuRSARPlayer::Stop: BGM 20-frame fade; the race RSARPlayer takes over.
 if (bgm_ != 0xFFFFFFFF) sound.stopSlot(slot_, 20);
 bgm_ = 0xFFFFFFFF; section_ = Section::Race; pendingStart_ = -1;
 // Menu groups go once the fade is over; the race loads its own.
 loadSections(0xFFFFFFFF, 0xFFFFFFFF, 400);
 loaded_ = Section::Race;
}
}
