#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <vector>
namespace mkw {
// Intro-only visible geometry supplement for scenery without camera KCL.
// Temporary BVH is discarded before play; no per-frame terrain queries.
class IntroMeshClearance {
 struct Triangle {Vec3 a,b,c,lo,hi;};
 struct Node {Vec3 lo,hi;unsigned first,count,left=0,right=0;};
 std::vector<Triangle> triangles_;std::vector<Node> nodes_;
 static Vec3 minimum(Vec3 a,Vec3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
 static Vec3 maximum(Vec3 a,Vec3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
 unsigned split(unsigned first,unsigned count){
  Node n{triangles_[first].lo,triangles_[first].hi,first,count};
  for(unsigned i=first+1;i<first+count;++i){n.lo=minimum(n.lo,triangles_[i].lo);n.hi=maximum(n.hi,triangles_[i].hi);}
  const unsigned id=unsigned(nodes_.size());nodes_.push_back(n);
  if(count>16){const auto size=n.hi-n.lo;const unsigned axis=size.x>size.y?(size.x>size.z?0:2):(size.y>size.z?1:2);
   auto coordinate=[&](const Triangle&t){const auto c=t.lo+t.hi;return axis==0?c.x:axis==1?c.y:c.z;};
   const unsigned half=count/2;std::nth_element(triangles_.begin()+first,triangles_.begin()+first+half,triangles_.begin()+first+count,[&](const auto&a,const auto&b){return coordinate(a)<coordinate(b);});
   const auto left=split(first,half),right=split(first+half,count-half);nodes_[id].left=left;nodes_[id].right=right;nodes_[id].count=0;
  }return id;
 }
 static Vec3 closest(Vec3 p,const Triangle&t){
  const auto ab=t.b-t.a,ac=t.c-t.a,ap=p-t.a;const float d1=dot(ab,ap),d2=dot(ac,ap);
  if(d1<=0&&d2<=0)return t.a;
  const auto bp=p-t.b;const float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return t.b;
  const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return t.a+ab*(d1/(d1-d3));
  const auto cp=p-t.c;const float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return t.c;
  const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return t.a+ac*(d2/(d2-d6));
  const float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return t.b+(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6)));
  return t.a+(ab*vb+ac*vc)/(va+vb+vc);
 }
 void query(unsigned id,float radius,Vec3 p,Vec3& push,float& deepest)const{
  const auto&n=nodes_[id];if(p.x+radius<n.lo.x||p.y+radius<n.lo.y||p.z+radius<n.lo.z||p.x-radius>n.hi.x||p.y-radius>n.hi.y||p.z-radius>n.hi.z)return;
  if(!n.count){query(n.left,radius,p,push,deepest);query(n.right,radius,p,push,deepest);return;}
  for(unsigned i=n.first;i<n.first+n.count;++i){const auto&t=triangles_[i];const auto delta=p-closest(p,t);const float distance=length(delta),depth=radius-distance;
   if(depth>deepest){deepest=depth;push=normalized(delta,normalized(cross(t.b-t.a,t.c-t.a),{0,1,0}))*depth;}}
 }
public:
 void add(Vec3 a,Vec3 b,Vec3 c){if(dot(cross(b-a,c-a),cross(b-a,c-a))<1e-8f)return;triangles_.push_back({a,b,c,minimum(a,minimum(b,c)),maximum(a,maximum(b,c))});}
 void build(){nodes_.clear();if(!triangles_.empty()){nodes_.reserve(triangles_.size()/4);split(0,unsigned(triangles_.size()));}}
 bool correction(float radius,Vec3 p,Vec3& push)const{push={};float deepest=0;if(!nodes_.empty())query(0,radius,p,push,deepest);return deepest>0;}
};
}
