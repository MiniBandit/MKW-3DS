#include <mkw/menu_sound.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/sound_system.hpp>
#include <mkw/sound_ids.hpp>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace mkw;
using namespace mkw::snd;
static std::vector<SoundCommand> posted;
static std::vector<std::uint32_t> loadedGroups;
namespace mkw {
SoundSystem& SoundSystem::instance(){static SoundSystem sound;return sound;}
bool SoundSystem::start(){running_.store(true);return true;}
void SoundSystem::post(const SoundCommand& command){posted.push_back(command);}
void SoundSystem::loadScene(const std::uint32_t*,int,int,const std::uint32_t*,int){}
void SoundSystem::loadScene(const Scene& sc){loadedGroups.assign(sc.groups,sc.groups+sc.groupCount);bool menu=false;for(int i=0;i<sc.groupCount;++i)menu|=sc.groups[i]==id::group::GRP_MENU||sc.groups[i]==id::group::GRP_PRESENT;assert(sc.residentCount==(menu?1:-1));assert(sc.cacheBytes==(menu?1024u*1024u:0));}
bool SoundSystem::sceneReady()const{return true;}
}
static std::vector<unsigned> uiCommands(){std::vector<unsigned> ids;for(auto c:posted)if(c.type==SoundCommand::Start&&(!c.slot||c.slot==MenuSound::RaceConfirmSlot))ids.push_back(c.id);return ids;}
static unsigned checked=0;
static void event(UiPage page,unsigned cursor,UiInput key,std::vector<unsigned> expected){
 MenuSound sound;MenuState before;for(auto& flags:before.licenseUnlocks)flags.fill(~0u);before.enter(page);before.cursor=cursor;
 auto after=before;const auto action=after.input(key);posted.clear();sound.input(before,after,key,action);
 if(uiCommands()!=expected){fprintf(stderr,"menu page=%u cursor=%u actual",unsigned(page),cursor);for(auto i:uiCommands())fprintf(stderr," %u",i);fprintf(stderr," expected");for(auto i:expected)fprintf(stderr," %u",i);fprintf(stderr,"\n");std::abort();}++checked;
}
int main(){SoundSystem::instance().start();UiInput a;a.accept=true;UiInput b;b.back=true;UiInput y;y.switchType=true;UiInput down;down.down=true;
 for(auto page:{UiPage::Title,UiPage::License,UiPage::Mode,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::VSMode,UiPage::BattleMode,UiPage::BattleCup,UiPage::BattleStage,UiPage::Error})event(page,0,a,{id::SE_UI_BTN_OK});
 event(UiPage::Confirm,0,a,{id::SE_UI_RACE_OK});
 event(UiPage::EngineClass,2,a,{id::SE_UI_BTN_OK});
 event(UiPage::Drift,2,a,{id::SE_UI_CTRL_WIN_POPUP});
 event(UiPage::DriftHelp,0,a,{id::SE_UI_CTRL_WIN_POPDOWN});
 event(UiPage::DriftHelp,0,b,{id::SE_UI_CTRL_WIN_POPDOWN});
 event(UiPage::MainMenu,0,a,{id::SE_UI_TITLE_OUT});
 event(UiPage::EngineClass,0,y,{id::SE_UI_SHEET_RIGHT});
 for(auto page:{UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::VSMode,UiPage::BattleMode,UiPage::BattleCup,UiPage::Confirm})event(page,0,b,{id::SE_UI_PAGE_PREV});
 event(UiPage::Course,0,b,{id::SE_UI_PAGE_PREV});event(UiPage::BattleStage,0,b,{});event(UiPage::QuitConfirm,1,b,{});event(UiPage::Pause,0,b,{id::SE_RC_PAUSE_OFF});
 event(UiPage::Pause,0,a,{id::SE_RC_PAUSE_OFF});event(UiPage::Pause,1,a,{id::SE_RC_PAUSE_EXIT_GAME});event(UiPage::QuitConfirm,0,a,{id::SE_RC_PAUSE_EXIT_GAME});event(UiPage::QuitConfirm,1,a,{id::SE_UI_BTN_OK});
 for(auto page:{UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::VSMode,UiPage::BattleMode,UiPage::BattleCup,UiPage::BattleStage,UiPage::Pause,UiPage::QuitConfirm}){auto move=down;if(page==UiPage::BattleCup){move.down=false;move.right=true;}event(page,0,move,{id::SE_UI_BTN_IN});}
 event(UiPage::Ghost,0,down,{});
 // Exercise the actual transition wrapper/call gate: a same-page OK is
 // delivered once, although its action remains held and repeated A arrives.
 for(auto page:{UiPage::Confirm,UiPage::Pause}){
  MenuSound sound;MenuState state;if(page==UiPage::Pause)state.selection.mode=UiMode::TimeTrials;state.enter(page);state.cursor=page==UiPage::Pause?1:0;MenuTransition tr;posted.clear();
  for(int frame=0;frame<10;++frame){const auto before=state;const bool accepted=!tr.active();const auto action=tr.input(state,a);if(accepted)sound.input(before,state,a,action);tr.advance(state,1);}
  assert(uiCommands()==std::vector<unsigned>{page==UiPage::Confirm?id::SE_UI_RACE_OK:id::SE_RC_PAUSE_EXIT_GAME});++checked;
 }
 MenuSound sound;MenuState before;for(auto& flags:before.licenseUnlocks)flags.fill(~0u);before.enter(UiPage::EngineClass);auto after=before;after.input(y);sound.input(before,after,y,UiAction::None);before=after;after.input(y);posted.clear();sound.input(before,after,y,UiAction::None);assert(uiCommands()==std::vector<unsigned>{id::SE_UI_SHEET_LEFT});++checked;
 before.enter(UiPage::MainMenu);before.cursor=1;after=before;after.input(a);posted.clear();sound.input(before,after,a,UiAction::None);assert(uiCommands()==std::vector<unsigned>{id::SE_UI_CTRL_WIN_POPUP});before=after;after.input(down);posted.clear();sound.input(before,after,down,UiAction::None);assert((uiCommands()==std::vector<unsigned>{id::SE_UI_CTRL_WIN_POPDOWN,id::SE_UI_BTN_IN}));checked+=2;
 event(UiPage::Unlock,0,a,{id::SE_UI_BTN_OK});event(UiPage::Unlock,0,b,{});
 MenuSound present;MenuState reward;reward.enter(UiPage::Unlock);reward.unlockCondition=0;reward.unlockBit=14;
 assert(MenuSound::sectionOf(reward.page)==MenuSound::Section::Present);
 posted.clear();present.frame(reward,false);
 for(const auto& c:posted)assert(c.type!=SoundCommand::Start);
 present.frame(reward,true);
 auto fanCount=[](){unsigned n=0;for(const auto& c:posted)n+=c.type==SoundCommand::Start&&c.id==id::STRM_O_LEVEL_OPEN_FAN;return n;};
 assert((loadedGroups==std::vector<std::uint32_t>{id::group::GRP_CMN,id::group::GRP_PRESENT}));
 assert(fanCount()==1);for(unsigned i=0;i<60;++i)present.frame(reward,true);assert(fanCount()==1);
 reward.unlockCondition=4;reward.unlockBit=18;present.frame(reward,true);assert(fanCount()==2);
 present.frame(reward,true);assert(fanCount()==2);
 reward.enter(UiPage::MainMenu);present.frame(reward,true);
 bool title=false;for(const auto& c:posted)title|=c.type==SoundCommand::Start&&c.id==id::W_BGM_TITLE;assert(title);
 printf("PASS %u production menu audio input scenarios, same-page confirmation/restart and blocked repeat gate; native audio commands recorded\n",checked);
}
