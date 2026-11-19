#include <mkw/intro_clearance.hpp>
#include <mkw/display_layout.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
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
void IntroClearancePath::build(const std::vector<IntroPathFrame>& frames,Query query,void* context,bool smooth){
 offsets_.assign(frames.size(),{});targetOffsets_.assign(frames.size(),{});rawPush_.assign(frames.size(),0.f);residual_=0;notes_.clear();
 for(unsigned begin=0;begin<frames.size();){
  unsigned end=begin+1;while(end<frames.size()&&frames[end].shot==frames[begin].shot)++end;
  // The authored rail as retail plays it. Only a shot that clips gets one
  // rigid offset below; nothing reshapes its motion.
  std::vector<Vec3> eyes,targets;
  if(smooth){eyes=smoothShot(frames,begin,end,false);targets=smoothShot(frames,begin,end,true);}
  else for(unsigned i=begin;i<end;++i){eyes.push_back(frames[i].eye);targets.push_back(frames[i].target);}
  std::vector<IntroPathFrame> shot;shot.reserve(end-begin);
  for(unsigned i=begin;i<end;++i)shot.push_back({eyes[i-begin],targets[i-begin],frames[i].fov,frames[i].shot});
  // Starts at the frame that last failed: candidates mostly fail on the same
  // frames, and the answer (every frame clear) doesn't depend on the order.
  unsigned lastFail=0;
  const auto clear=[&](Vec3 translation){
   const unsigned n=unsigned(shot.size());
   for(unsigned k=0;k<n;++k){const unsigned i=(lastFail+k)%n;const auto p=correction(shot[i],translation,query,context,2.f);if(dot(p,p)>.0025f){lastFail=i;return false;}}
   return true;
  };
  Vec3 translation{};
  // Few frames clip, by little: GCN DK Mountain's final zoom has 4 of 317
  // frames within 47.5 units, yet one rigid placement moved the whole shot
  // 1552 units (the eye only, so the view swung 9.5 degrees: off centre).
  // First a local bump: each clipping frame's push, eased in and out over
  // 30 frames either side (raised cosine), the larger bump winning where two
  // meet, so the camera eases round the contact and back onto the rail.
  constexpr int Ease=30;
  const int n=int(shot.size());
  std::vector<Vec3> local(n,Vec3{});
  bool localClear=!query;
  for(unsigned pass=0;query&&pass<8;++pass){
   std::vector<Vec3> add(n,Vec3{});bool any=false;
   for(int i=0;i<n;++i){
    const auto p=correction(shot[i],local[i],query,context,2.f);if(dot(p,p)<=.0025f)continue;any=true;
    for(int k=std::max(0,i-Ease);k<=std::min(n-1,i+Ease);++k){
     const float w=.5f+.5f*std::cos(3.14159265f*float(k-i)/float(Ease+1));
     if(dot(p,p)*w*w>dot(add[k],add[k]))add[k]=p*w;
    }
   }
#ifndef MKW_RELEASE
   {unsigned clipping=0;float worst=0,largest=0;
    for(int i=0;i<n;++i){if(dot(add[i],add[i])>.0025f)++clipping;worst=std::max(worst,length(add[i]));largest=std::max(largest,length(local[i]));}
    char line[128];std::snprintf(line,sizeof line,"intro shot %d pass %u: %u frames pushed, push %.1f, bump %.1f\n",shot.front().shot,pass,clipping,worst,largest);notes_+=line;}
#endif
   if(!any){localClear=true;break;}
   for(int i=0;i<n;++i)local[i]+=add[i];
  }
  if(query&&!localClear&&!clear({})){
   // Fallback, one rigid placement for the entire shot. Even offline repeated local
   // push-outs made visible bends/reversals; no frame now gets its own escape direction.
   // Only a placement within ~2 degrees of the nearest view is kept (below),
   // so search no farther: the nearest eye-target distance x tan 2 degrees.
   float nearest=1e30f;for(const auto& f:shot)nearest=std::min(nearest,length(f.target-f.eye));
   const float reach=std::min(8192.f,nearest*.0349f);
   Vec3 projected{};
   for(unsigned pass=0;pass<24&&length(projected)<=reach;++pass){
    Vec3 deepest{};
    for(const auto& f:shot){const auto p=correction(f,projected,query,context,2.f);if(dot(p,p)>dot(deepest,deepest))deepest=p;}
    if(dot(deepest,deepest)<=.0025f)break;
    projected+=deepest;
   }
   // The target stays put, so an offset swings the view: the largest angle
   // between the authored and the offset view over the shot. A narrow final
   // zoom shows a sideways push as an off-centre picture, so among clearing
   // placements take the one that swings least, at the shortest distance.
   const auto swing=[&](Vec3 c){
    float worst=1.f;
    for(const auto& f:shot){const auto a=normalized(f.target-f.eye,{0,0,-1}),b=normalized(f.target-f.eye-c,a);worst=std::min(worst,dot(a,b));}
    return worst; // cosine: larger is better
   };
   bool found=clear(projected);float bestCos=found?swing(projected):-2.f;if(found)translation=projected;
   const auto forward=normalized(targets.front()-eyes.front(),{0,0,-1});
   const auto side=normalized(cross(forward,{0,1,0}),{1,0,0}),back=normalized(Vec3{-forward.x,0,-forward.z},{0,0,1});
   // Along the view line first: it keeps the authored direction.
   const Vec3 directions[]={forward,forward*-1.f,{0,1,0},side,side*-1.f,back,back*-1.f,
     normalized(Vec3{0,1,0}+side),normalized(Vec3{0,1,0}-side),normalized(Vec3{0,1,0}+back)};
   Vec3 chosen{};float chosenDistance=0;
   for(float distance=16.f;distance<=2.f*reach;distance*=2.f){
    bool any=false;
    for(auto direction:directions){const auto candidate=direction*distance;
     if(!clear(candidate))continue;
     any=true;const float c=swing(candidate);if(c>bestCos){bestCos=c;translation=candidate;chosen=direction;chosenDistance=distance;found=true;}}
    if(any)break;
   }
   // The doubling overshoots up to 2x: bisect the chosen direction down.
   if(chosenDistance>0){
    float lo=chosenDistance*.5f,hi=chosenDistance;
    for(unsigned step=0;step<6;++step){const float mid=(lo+hi)*.5f;if(clear(chosen*mid))hi=mid;else lo=mid;}
    translation=chosen*hi;
   }
   // If no whole-shot placement clears the geometry, retain the bounded
   // whole-shot projection and report residuals. Never add a live correction.
   if(!found)translation=length(projected)<=8192.f?projected:Vec3{};
#ifndef MKW_RELEASE
   {char line[96];std::snprintf(line,sizeof line,"intro shot %d rigid %.1f (%s)\n",shot.front().shot,length(translation),found?"clear":"residual");notes_+=line;}
#endif
   // A rigid placement that leaves frames clipping, or swings the view more
   // than 2 degrees (the off-centre shot players reported), loses to the
   // eased bump: the few grazing frames stand (retail's camera has no clearance).
   float worst=1.f;
   for(const auto& f:shot){const auto a=normalized(f.target-f.eye,{0,0,-1}),b=normalized(f.target-f.eye-translation,a);worst=std::min(worst,dot(a,b));}
   if(!found||worst<std::cos(2.f*.0174532925f)){
#ifndef MKW_RELEASE
    {char line[96];std::snprintf(line,sizeof line,"intro shot %d rigid swings %.1f deg: kept the bump\n",shot.front().shot,std::acos(std::max(-1.f,worst))*57.29578f);notes_+=line;}
#endif
    translation={};
   }else for(auto& v:local)v={};
  }
  for(unsigned i=begin;i<end;++i){
   offsets_[i]=eyes[i-begin]+local[i-begin]+translation-frames[i].eye;
   targetOffsets_[i]=targets[i-begin]-frames[i].target;
#ifndef MKW_RELEASE
   if(query)rawPush_[i]=length(correction(frames[i],{},query,context,2.f));
#endif
   const auto p=correction(shot[i-begin],local[i-begin]+translation,query,context);
   if(dot(p,p)>.01f)++residual_;
  }
  begin=end;
 }
}
}
