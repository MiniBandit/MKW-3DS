#include <mkw/intro_clearance.hpp>
#include <mkw/display_layout.hpp>
#include <algorithm>
#include <cmath>
namespace mkw {
Vec3 IntroClearancePath::correction(const IntroPathFrame& f,Vec3 offset,Query query,void* context,float margin){
 if(!query)return {};
 constexpr float near=100.f;
 const float tangent=std::tan(std::clamp(f.fov,1.f,170.f)*.00872664626f);
 const float halfHeight=near*tangent*display::IntroHeight/display::Height,halfWidth=near*tangent*display::Aspect;
 const auto forward=normalized(f.target-f.eye-offset,{0,0,-1}),right=normalized(cross(forward,{0,1,0}),{1,0,0}),up=cross(right,forward);
 const auto center=f.eye+offset+forward*near-up*(near*tangent*(display::IntroHeight-display::Height)/display::Height);
 const float w=halfWidth,h=halfHeight*2.f/3.f,radius=.5f*std::sqrt(w*w+h*h)+margin;
 Vec3 deepest{};float depth=0;
 for(unsigned row=0;row<3;++row)for(unsigned col=0;col<2;++col){
  const auto p=center+right*((float(col)+.5f)*w-halfWidth)+up*((float(row)+.5f)*h-halfHeight);Vec3 push{};
  if(query(context,radius,p,push)&&dot(push,push)>depth){deepest=push;depth=dot(push,push);}
 }
 return deepest;
}
namespace {
// Least-squares cubic Bezier for the whole authored shot. Endpoints are
// retained; all interior samples contribute to one curve, never local detours.
std::vector<Vec3> smoothShot(const std::vector<IntroPathFrame>& frames,unsigned begin,unsigned end,bool target){
 const unsigned count=end-begin;std::vector<Vec3> result(count);
 const auto get=[&](unsigned i){return target?frames[i].target:frames[i].eye;};
 const auto first=get(begin),last=get(end-1);Vec3 r1{},r2{};double a=0,b=0,c=0;
 for(unsigned i=1;i+1<count;++i){
  const double t=double(i)/double(count-1),u=1-t,b0=u*u*u,b1=3*u*u*t,b2=3*u*t*t,b3=t*t*t;
  const auto r=get(begin+i)-first*float(b0)-last*float(b3);
  a+=b1*b1;b+=b1*b2;c+=b2*b2;r1+=r*float(b1);r2+=r*float(b2);
 }
 const double det=a*c-b*b;Vec3 p1=first+(last-first)/3.f,p2=first+(last-first)*(2.f/3.f);
 if(det>1e-12){p1=(r1*float(c)-r2*float(b))/float(det);p2=(r2*float(a)-r1*float(b))/float(det);}
 for(unsigned i=0;i<count;++i){const float t=count>1?float(i)/float(count-1):0.f,u=1.f-t;
  result[i]=first*(u*u*u)+p1*(3*u*u*t)+p2*(3*u*t*t)+last*(t*t*t);}
 return result;
}
}
void IntroClearancePath::build(const std::vector<IntroPathFrame>& frames,Query query,void* context){
 offsets_.assign(frames.size(),{});targetOffsets_.assign(frames.size(),{});residual_=0;
 for(unsigned begin=0;begin<frames.size();){
  unsigned end=begin+1;while(end<frames.size()&&frames[end].shot==frames[begin].shot)++end;
  const auto eyes=smoothShot(frames,begin,end,false),targets=smoothShot(frames,begin,end,true);
  std::vector<IntroPathFrame> shot;shot.reserve(end-begin);
  for(unsigned i=begin;i<end;++i)shot.push_back({eyes[i-begin],targets[i-begin],frames[i].fov,frames[i].shot});
  const auto clear=[&](Vec3 translation){
   for(const auto& f:shot){const auto p=correction(f,translation,query,context,2.f);if(dot(p,p)>.0025f)return false;}
   return true;
  };
  Vec3 translation{};
  if(query&&!clear({})){
   // One rigid placement for the entire shot. Even offline repeated local
   // push-outs made visible bends/reversals; no frame now gets its own escape direction.
   Vec3 projected{};
   for(unsigned pass=0;pass<24;++pass){
    Vec3 deepest{};
    for(const auto& f:shot){const auto p=correction(f,projected,query,context,2.f);if(dot(p,p)>dot(deepest,deepest))deepest=p;}
    if(dot(deepest,deepest)<=.0025f)break;
    projected+=deepest;
   }
   bool found=clear(projected);float best=found?length(projected):1e30f;if(found)translation=projected;
   const auto forward=normalized(targets.front()-eyes.front(),{0,0,-1});
   const auto side=normalized(cross(forward,{0,1,0}),{1,0,0}),back=normalized(Vec3{-forward.x,0,-forward.z},{0,0,1});
   const Vec3 directions[]={{0,1,0},side,side*-1.f,back,back*-1.f,
     normalized(Vec3{0,1,0}+side),normalized(Vec3{0,1,0}-side),normalized(Vec3{0,1,0}+back)};
   for(float distance=16.f;distance<=8192.f&&distance<best;distance*=2.f){
    for(auto direction:directions){const auto candidate=direction*distance;
     if(clear(candidate)){translation=candidate;best=distance;found=true;break;}}
   }
   // If no whole-shot placement clears the geometry, retain the bounded
   // whole-shot projection and report residuals. Never add a live correction.
   if(!found)translation=length(projected)<=8192.f?projected:Vec3{};
  }
  for(unsigned i=begin;i<end;++i){
   offsets_[i]=eyes[i-begin]+translation-frames[i].eye;
   targetOffsets_[i]=targets[i-begin]-frames[i].target;
   const auto p=correction(shot[i-begin],translation,query,context);
   if(dot(p,p)>.01f)++residual_;
  }
  begin=end;
 }
}
}
