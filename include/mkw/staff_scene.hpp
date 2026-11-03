#pragma once
#include <mkw/award_scene.hpp>
#include <mkw/license_identity.hpp>
namespace mkw {
// Original 6300-tick driving credits, complete retail movie + PCM, final photo.
// Must enter after retiring the race/award child and all of its GPU jobs.
AwardSceneResult runStaff(Renderer&,const AwardResults&,const NativeMii&,bool trueEnding,
 void(*report)(const char*),Kinoko::EGG::SceneManager&);
}
