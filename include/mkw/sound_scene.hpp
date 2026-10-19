#pragma once
#include <mkw/sound_system.hpp>
#include <algorithm>
namespace mkw { using snd::u8; using snd::u32; using snd::Archive; using snd::SoundEntry; using snd::WaveReach; using snd::InvalidId; }
namespace mkw {
// Files of the scene and, per file with waves, the resident mask (empty:
// every wave). Main thread; the archive is read only after start().
struct ScenePlan { std::vector<u32> files; std::vector<std::vector<u8>> masks; };
inline ScenePlan planSoundScene(const Archive& a, const SoundSystem::Scene& sc,u32 carriedSound=InvalidId) {
 ScenePlan plan;
 auto contains=[](const u32* ids,int count,u32 id){return count>0&&std::find(ids,ids+count,id)!=ids+count;};
 std::vector<u32> items;
 std::vector<u8> inScene(a.fileCount()), resident(a.fileCount());
 for (int i = 0; i < sc.groupCount; ++i) {
  a.groupFiles(sc.groups[i], items);
  const bool res = sc.residentCount < 0 || contains(sc.residentGroups,sc.residentCount,sc.groups[i]);
  for (u32 f : items) {
   if (f >= a.fileCount() || contains(sc.skipFiles,sc.skipCount,f)) continue;
   if (!inScene[f]) { inScene[f] = 1; plan.files.push_back(f); }
   resident[f] |= res;
  }
 }
 // Keep just the departing BGM's sequence/stream and reached wave bank,
 // never the preceding race's actors, course groups or engine families.
 if(const auto* sound=a.sound(carriedSound)){
  const auto reach=a.reach(carriedSound);
  for(u32 f:{sound->fileId,reach.file})if(f<a.fileCount()){
   if(!inScene[f]){inScene[f]=1;plan.files.push_back(f);}
   resident[f]=1;
  }
 }
 plan.masks.resize(plan.files.size());
 if (sc.residentCount < 0 && !sc.onDemandCount && !sc.residentSoundCount) return plan;   // every wave resident
 std::vector<std::vector<u8>> byFile(a.fileCount());
 for (u32 f : plan.files) byFile[f].assign(a.file(f)->waveCount, 0);
 // 1: on demand, 2: resident regardless of its group.
 std::vector<u8> pick(a.soundCount());
 for (int i = 0; i < sc.onDemandCount; ++i) if (sc.onDemandSounds[i] < pick.size()) pick[sc.onDemandSounds[i]] = 1;
 for (int i = 0; i < sc.residentSoundCount; ++i) if (sc.residentSounds[i] < pick.size()) pick[sc.residentSounds[i]] = 2;
 if(carriedSound<pick.size())pick[carriedSound]=2;
 for (u32 id = 0; id < a.soundCount(); ++id) {
  const SoundEntry* e = a.sound(id);
  if (pick[id] == 1 || !e || e->fileId >= inScene.size() || !inScene[e->fileId]) continue;
  const WaveReach r = a.reach(id);
  if (r.file == InvalidId || !inScene[r.file] || (!resident[r.file] && pick[id] != 2)) continue;
  for (u32 k = 0; k < r.count; ++k) if (r.waves[k] < byFile[r.file].size()) byFile[r.file][r.waves[k]] = 1;
 }
 for (size_t i = 0; i < plan.files.size(); ++i) plan.masks[i] = std::move(byFile[plan.files[i]]);
 return plan;
}
}
