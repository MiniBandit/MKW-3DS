#pragma once
#include <mkw/sound_system.hpp>
#include <mkw/sound_ids.hpp>
namespace mkw {
// Keep common UI waves and menu music immediate. Character/menu voices use
// the engine's existing on-demand cache instead of one large LINEAR bank.
// Group membership and the retail sounds themselves are unchanged.
inline SoundSystem::Scene menuSoundScene(const std::uint32_t* groups,int count,int keepMs) {
 static constexpr std::uint32_t resident[]={snd::id::group::GRP_CMN};
 static constexpr std::uint32_t music[]={snd::id::W_BGM_TITLE,snd::id::SEQ_O_SELECT,snd::id::STRM_O_OPTION,snd::id::STRM_O_LEVEL_OPEN_FAN};
 SoundSystem::Scene s;s.groups=groups;s.groupCount=count;s.keepMs=keepMs;
 bool menu=false;for(int i=0;i<count;++i)menu|=groups[i]==snd::id::group::GRP_MENU||groups[i]==snd::id::group::GRP_PRESENT;
 if(menu){s.residentGroups=resident;s.residentCount=1;s.residentSounds=music;s.residentSoundCount=4;s.cacheBytes=1024u*1024u;}
 return s;
}
}
