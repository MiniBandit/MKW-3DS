#pragma once
#include <mkw/math.hpp>
#include <vector>
namespace mkw {
struct IntroPathFrame {Vec3 eye,target;float fov=60;int shot=-1;};
// Presentation adaptation for the extended two-LCD preview frustum. Plan
// the whole authored shot before reveal; never wait for a collision to snap.
class IntroClearancePath {
public:
 using Query=bool(*)(void*,float,Vec3,Vec3&);
 void build(const std::vector<IntroPathFrame>& frames,Query query,void* context);
 void clear(){offsets_.clear();targetOffsets_.clear();residual_=0;}
 Vec3 offset(unsigned frame)const{return frame<offsets_.size()?offsets_[frame]:Vec3{};}
 Vec3 targetOffset(unsigned frame)const{return frame<targetOffsets_.size()?targetOffsets_[frame]:Vec3{};}
 unsigned frames()const{return unsigned(offsets_.size());}
 unsigned residual()const{return residual_;}
 static Vec3 correction(const IntroPathFrame&,Vec3 offset,Query,void*,float margin=0.f);
private:
 std::vector<Vec3> offsets_,targetOffsets_;unsigned residual_=0;
};
}
