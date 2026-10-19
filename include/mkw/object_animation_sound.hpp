#pragma once
#include <mkw/object_sound_assets.hpp>
#include <cmath>
#include <cstring>

namespace mkw {
// Simulation-owned BRASD point-event cursor. No draw/culling/thread state
// determines whether an animal's animation makes a sound.
class ObjectAnimationSound {
public:
 static unsigned frames(const char* resource,const char* name){
  for(const auto& c:ObjectSoundClips)if(!std::strcmp(c.resource,resource)&&!std::strcmp(c.name,name))return c.frames;
  return 1;
 }
 template<class Object> void update(const Object& object,const char* resource,const char* name,float frame,float period=0){
  const ObjectSoundClip* clip=nullptr;
  for(const auto& c:ObjectSoundClips)if(!std::strcmp(c.resource,resource)&&!std::strcmp(c.name,name)){clip=&c;break;}
  if(!clip){clip_=nullptr;return;}
  const bool reset=clip_!=clip||frame<frame_;
  const float previous=reset?frame-.001f:frame_;
  if(frame>previous)for(unsigned i=0;i<clip->count;++i){
   const auto& event=ObjectSoundEvents[clip->first+i];
   const float point=period>0?event.frame+(std::floor((previous-event.frame)/period)+1)*period:event.frame;
   if(point>previous&&point<=frame)object.cueSound(event.sound,Object::SoundCall::Animation,event.volume,event.pitch);
  }
  clip_=clip;frame_=frame;
 }
private:
 const ObjectSoundClip* clip_=nullptr;
 float frame_=0;
};
}
