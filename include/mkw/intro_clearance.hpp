#pragma once
#include <mkw/math.hpp>
#include <string>
#include <vector>
namespace mkw {
struct IntroPathFrame {Vec3 eye,target;float fov=60;int shot=-1;};
// Presentation adaptation for the extended two-LCD preview frustum. Plan
// the whole authored shot before reveal; never wait for a collision to snap.
class IntroClearancePath {
public:
 using Query=bool(*)(void*,float,Vec3,Vec3&);
 // smooth: the old least-squares cubic refit of every shot (DIAG A/B only;
 // it overshot "move, then hold" final zooms and pulled the target off centre).
 void build(const std::vector<IntroPathFrame>& frames,Query query,void* context,bool smooth=false);
 void clear(){offsets_.clear();targetOffsets_.clear();rawPush_.clear();residual_=0;notes_.clear();}
 Vec3 offset(unsigned frame)const{return frame<offsets_.size()?offsets_[frame]:Vec3{};}
 Vec3 targetOffset(unsigned frame)const{return frame<targetOffsets_.size()?targetOffsets_[frame]:Vec3{};}
 unsigned frames()const{return unsigned(offsets_.size());}
 unsigned residual()const{return residual_;}
 // DIAG: the push each frame needs on the authored rail (no offset).
 float rawPush(unsigned frame)const{return frame<rawPush_.size()?rawPush_[frame]:0.f;}
 // DIAG: one line per shot that needed a push (how it was cleared).
 const std::string& notes()const{return notes_;}
 static Vec3 correction(const IntroPathFrame&,Vec3 offset,Query,void*,float margin=0.f);
private:
 std::vector<Vec3> offsets_,targetOffsets_;std::vector<float> rawPush_;unsigned residual_=0;std::string notes_;
};
}
