#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <vector>
namespace mkw {
struct Bounds {
 Vec3 minimum,maximum;
 void include(Vec3 p){minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};}
};
// Conservative affine AABB transform. Eight corners also avoid cancellation
// from center/extent reconstruction on large course coordinates.
inline Bounds transformBounds(const Bounds& b,const float* m){
 Bounds out{};bool first=true;
 for(unsigned c=0;c<8;++c){
  Vec3 p{c&1?b.maximum.x:b.minimum.x,c&2?b.maximum.y:b.minimum.y,c&4?b.maximum.z:b.minimum.z};
  Vec3 q{m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]};
  if(first){out={q,q};first=false;}else out.include(q);
 }
 return out;
}
// Center/extent form of the same transform for small skeleton-local boxes:
// about a sixth of the arithmetic. Padding of 1e-5 of the term magnitudes
// keeps it a superset of the eight-corner result despite rounding.
inline Bounds transformBoundsPadded(const Bounds& b,const float* m){
 const float c[3]={(b.minimum.x+b.maximum.x)*.5f,(b.minimum.y+b.maximum.y)*.5f,(b.minimum.z+b.maximum.z)*.5f};
 const float e[3]={(b.maximum.x-b.minimum.x)*.5f,(b.maximum.y-b.minimum.y)*.5f,(b.maximum.z-b.minimum.z)*.5f};
 float lo[3],hi[3];
 for(unsigned r=0;r<3;++r){
  const float* row=m+r*4;const float a0=std::fabs(row[0]),a1=std::fabs(row[1]),a2=std::fabs(row[2]);
  const float center=row[0]*c[0]+row[1]*c[1]+row[2]*c[2]+row[3];
  const float extent=a0*e[0]+a1*e[1]+a2*e[2];
  const float pad=1e-5f*(a0*(std::fabs(c[0])+e[0])+a1*(std::fabs(c[1])+e[1])+a2*(std::fabs(c[2])+e[2])+std::fabs(row[3]));
  lo[r]=center-extent-pad;hi[r]=center+extent+pad;
 }
 return {{lo[0],lo[1],lo[2]},{hi[0],hi[1],hi[2]}};
}
// Row-major object-to-clip matrix, including the PICA display tilt. PICA clips
// x/y to [-w,w] and z to [-w,0]. No far-distance or occlusion approximation.
class ClipFrustum {
 std::array<std::array<float,4>,6> planes_{};
public:
 ClipFrustum()=default;
 explicit ClipFrustum(const float* m){for(unsigned c=0;c<4;++c){
  planes_[0][c]=m[12+c]+m[c];planes_[1][c]=m[12+c]-m[c];
  planes_[2][c]=m[12+c]+m[4+c];planes_[3][c]=m[12+c]-m[4+c];
  planes_[4][c]=m[12+c]+m[8+c];planes_[5][c]=-m[8+c];
 }}
 bool intersects(const Bounds& b)const{
  for(const auto& p:planes_){
   const float x=p[0]*(p[0]>=0?b.maximum.x:b.minimum.x);
   const float y=p[1]*(p[1]>=0?b.maximum.y:b.minimum.y);
   const float z=p[2]*(p[2]>=0?b.maximum.z:b.minimum.z);
   // Outward numerical slack, also at far-plane cancellation. Nonfinite
   // comparisons retain geometry rather than hiding uncertain bounds.
   const float tolerance=1e-5f*(std::fabs(x)+std::fabs(y)+std::fabs(z)+std::fabs(p[3])+1.f);
   if(x+y+z+p[3]<-tolerance)return false;
  }
  return true;
 }
 // Every point of b is inside every plane, by the same ordered float sums as
 // intersects() at b's innermost corner. Rounding is monotonic, so intersects()
 // then holds for any box inside b (NaN never qualifies).
 bool contains(const Bounds& b)const{
  for(const auto& p:planes_){
   const float x=p[0]*(p[0]>=0?b.minimum.x:b.maximum.x);
   const float y=p[1]*(p[1]>=0?b.minimum.y:b.maximum.y);
   const float z=p[2]*(p[2]>=0?b.minimum.z:b.maximum.z);
   if(!(x+y+z+p[3]>=0.f))return false;
  }
  return true;
 }
};
struct IndexRange {uint32_t first=0,count=0;};
struct VisibilityBlock {Bounds bounds;IndexRange indices;};
struct SubmeshVisibility {Bounds bounds;std::vector<VisibilityBlock> blocks;};
// Static index order is preserved, including transparent triangles and decals.
// Only contiguous invisible runs may be omitted; no asset reduction/reordering.
template<class Vertex> SubmeshVisibility buildVisibility(const Vertex* vertices,const uint16_t* indices,uint32_t first,uint32_t count,unsigned blockTriangles=128){
 SubmeshVisibility out{};if(!count)return out;
 out.bounds.minimum=out.bounds.maximum=vertices[indices[first]].position;
 const unsigned stride=std::max(1u,blockTriangles)*3;
 out.blocks.reserve((count+stride-1)/stride);
 for(uint32_t at=first;at<first+count;at+=stride){
  VisibilityBlock b{};b.indices={at,std::min<uint32_t>(stride,first+count-at)};
  b.bounds.minimum=b.bounds.maximum=vertices[indices[at]].position;
  for(unsigned i=at;i<at+b.indices.count;++i)b.bounds.include(vertices[indices[i]].position);
  out.bounds.include(b.bounds.minimum);out.bounds.include(b.bounds.maximum);out.blocks.push_back(b);
 }
 return out;
}
struct VisibleRanges {
 // Bound extra draw overhead. Merging a gap only submits extra invisible
 // triangles; all visible triangles keep their exact original order.
 static constexpr unsigned Capacity=4;
 std::array<IndexRange,Capacity+1> ranges{};unsigned count=0;
 void append(IndexRange r){
  if(count&&ranges[count-1].first+ranges[count-1].count==r.first){ranges[count-1].count+=r.count;return;}
  ranges[count++]=r;if(count<=Capacity)return;
  unsigned best=0;uint32_t gap=~0u;
  for(unsigned i=0;i+1<count;++i){auto d=ranges[i+1].first-ranges[i].first-ranges[i].count;if(d<gap){gap=d;best=i;}}
  ranges[best].count=ranges[best+1].first+ranges[best+1].count-ranges[best].first;
  for(unsigned i=best+1;i+1<count;++i)ranges[i]=ranges[i+1];
  --count;
 }
};
// GPU-skinned geometry retains a small bone-local box per index block.
// This includes mixed-bone triangles without approximating their animation.
struct SkinBounds {unsigned matrix;Bounds bounds;};
struct SkinVisibilityBlock {IndexRange indices;std::vector<SkinBounds> bones;};
inline Bounds posedSkinBounds(const SkinVisibilityBlock& block,const float* palette,unsigned stride){
 Bounds out{};bool first=true;
 for(const auto& bone:block.bones){const auto b=transformBoundsPadded(bone.bounds,palette+bone.matrix*stride);
  if(first){out=b;first=false;}else{out.include(b.minimum);out.include(b.maximum);}}
 return out;
}
inline VisibleRanges selectSkinVisible(const std::vector<SkinVisibilityBlock>& blocks,const float* palette,unsigned stride,const ClipFrustum& frustum){
 VisibleRanges out;
 for(const auto& block:blocks)if(frustum.intersects(posedSkinBounds(block,palette,stride)))out.append(block.indices);
 return out;
}
// Bounds whose radius/distance from eye (same space) is below minRatio.
inline bool tinyFrom(const Bounds& b,Vec3 eye,float minRatio){
 const Vec3 h=(b.maximum-b.minimum)*.5f,d=(b.maximum+b.minimum)*.5f-eye;
 return dot(h,h)<minRatio*minRatio*dot(d,d);
}
inline VisibleRanges selectVisible(const SubmeshVisibility& mesh,const ClipFrustum& frustum){
 VisibleRanges out;if(!frustum.intersects(mesh.bounds))return out;
 // Blocks lie inside the submesh bounds: a contained submesh keeps them all.
 if(frustum.contains(mesh.bounds)){for(const auto& b:mesh.blocks)out.append(b.indices);return out;}
 for(const auto& b:mesh.blocks)if(frustum.intersects(b.bounds))out.append(b.indices);
 return out;
}
}
