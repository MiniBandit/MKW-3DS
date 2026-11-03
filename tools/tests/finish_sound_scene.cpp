#include <mkw/sound_scene.hpp>
#include <mkw/sound_ids.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace mkw;
int main(int argc,char**argv){
 assert(argc==2);std::ifstream input(argv[1],std::ios::binary);assert(input);
 std::vector<u8> bytes{std::istreambuf_iterator<char>(input),{}};Archive archive;assert(archive.load(bytes));
 const u32 menuGroups[]={snd::id::group::GRP_CMN,snd::id::group::GRP_MENU};
 SoundSystem::Scene menu;menu.groups=menuGroups;menu.groupCount=2;
 const auto before=planSoundScene(archive,menu);
 const u32 groups[]={snd::id::group::GRP_CMN,snd::id::group::GRP_RACE};
 const u32 sounds[]={snd::id::SE_UI_BTN_IN,snd::id::SE_RC_PAUSE_EXIT_GAME,snd::id::SE_RC_GOAL};
 SoundSystem::Scene race;race.groups=groups;race.groupCount=2;race.residentCount=0;
 race.residentSounds=sounds;race.residentSoundCount=3;
 const auto after=planSoundScene(archive,race);
 auto mask=[&](const ScenePlan& plan,u32 file)->const std::vector<u8>*{
  auto at=std::find(plan.files.begin(),plan.files.end(),file);
  return at==plan.files.end()?nullptr:&plan.masks[size_t(at-plan.files.begin())];
 };
 for(u32 id:sounds){
  const auto* sound=archive.sound(id);assert(sound);
  const auto reach=archive.reach(id);assert(reach.count);
  const auto* planned=mask(after,reach.file);assert(planned);
  const auto* reused=mask(before,reach.file);
  for(u32 i=0;i<reach.count;++i){
   assert(planned->empty()||(*planned)[reach.waves[i]]);
   // SoundSystem keeps an already-loaded bank's old mask. The menu's
   // shared bank must therefore already cover these race UI/finish waves.
   assert(!reused||reused->empty()||(*reused)[reach.waves[i]]);
  }
  std::printf("id=%u player=%u file=%u waves=%u reused=%u resident=ok\n",id,sound->playerId,reach.file,reach.count,unsigned(reused!=nullptr));
 }
}
