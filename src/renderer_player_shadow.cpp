#include <mkw/renderer.hpp>
#include <mkw/player_shadow.hpp>
#include <mkw/rainbow_shadow.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mkw {
namespace {
// Resolve the actual static receiving geometry under the racer before drawing.
// Placement hint only: never use a center ray to hide/switch the whole shadow.
bool hasPlayerReceiver(const Model* const* models,unsigned count,Vec3 origin,Vec3& ground,Vec3& normal,float startHeight=120.f,float* visibleTop=nullptr,float topLimit=512.f,float* roadOpacity=nullptr){
 const Vec3 start=origin+Vec3{0,startHeight,0};
 const float ceiling=visibleTop?std::max(start.y,origin.y+topLimit):start.y;
 float nearest=1920.f,opacityDistance=128.f;bool eligible=false,opacityIsRoad=false;
 auto covers=[&](Vec3 lo,Vec3 hi){return start.x>=lo.x&&start.x<=hi.x&&start.z>=lo.z&&start.z<=hi.z&&hi.y>=start.y-nearest&&lo.y<=ceiling;};
 for(unsigned i=0;i<count;++i){
  const auto& model=*models[i];if(!covers(model.minimum,model.maximum))continue;
  for(unsigned j=0;j<model.visibility.size();++j){
   const auto& sub=model.submeshes[j];
   // Blended light sheets are not the solid surface below the racer.
   const bool roadFade=(sub.lighting&rainbowShadow::Flag)!=0;
   if((sub.flags&2)&&!roadFade&&!playerShadow::receives(sub.flags))continue;
   // Authored road overlays do not replace their underlying solid receiver.
   // In particular a no-depth-write decal must not flip the whole racer to
   // the fallback just because it is tied with / above the road triangle.
   if(!playerShadow::receives(sub.flags)&&
      (sub.lighting&(retail::CoplanarLayer|retail::RaisedRoadLayer)))continue;
   for(const auto& block:model.visibility[j].blocks){
    if(!covers(block.bounds.minimum,block.bounds.maximum))continue;
    for(unsigned n=block.indices.first;n+2<block.indices.first+block.indices.count;n+=3){
     const Vec3 a=model.vertices[model.indices[n]].position;
     const Vec3 b=model.vertices[model.indices[n+1]].position;
     const Vec3 c=model.vertices[model.indices[n+2]].position;
     const Vec3 ab=b-a,ac=c-a;
     const float det=ab.x*ac.z-ab.z*ac.x;if(std::fabs(det)<1e-5f)continue;
     const float x=start.x-a.x,z=start.z-a.z;
     const float u=(x*ac.z-z*ac.x)/det,v=(ab.x*z-ab.z*x)/det;
     if(u<0||v<0||u+v>1)continue;
     const float y=a.y+u*ab.y+v*ac.y;
     if(visibleTop&&y<=ceiling&&playerShadow::receives(sub.flags))*visibleTop=std::max(*visibleTop,y);
     // Alpha lookup tolerates the same small mesh/KCL height offsets as
     // receiver placement, including a surface just above the floor ray.
     const float opacityError=std::fabs(y-(start.y-4.f));
     if(roadOpacity&&(opacityError<opacityDistance-.1f||
        (roadFade&&!opacityIsRoad&&opacityError<=opacityDistance+.1f))){
      opacityDistance=opacityError;opacityIsRoad=roadFade;
      const float a0=model.vertices[model.indices[n]].color[3];
      const float a1=model.vertices[model.indices[n+1]].color[3];
      const float a2=model.vertices[model.indices[n+2]].color[3];
      *roadOpacity=roadFade?(a0+u*(a1-a0)+v*(a2-a0))/255.f:1.f;
     }
     const float distance=start.y-y;
     if(distance>=0&&(distance<nearest||(roadFade&&distance<=nearest+.1f))){nearest=distance;eligible=roadFade||playerShadow::receives(sub.flags);
      ground={start.x,start.y-distance,start.z};normal=normalized(cross(ab,ac));
      if(normal.y<0)normal=-normal;}
    }
   }
  }
 }
 return eligible;
}
}

bool Renderer::configurePlayerShadow(bool enabled){
 if(enabled&&playerShadowTarget_)return true;
 waitForGpu();
 if(!enabled){
  if(actorShadowTarget_)C3D_RenderTargetDelete(actorShadowTarget_);
  actorShadowTarget_=nullptr;
  if(actorShadowTex_.data)C3D_TexDelete(&actorShadowTex_);
  actorShadowTex_={};
  if(playerShadowTarget_)C3D_RenderTargetDelete(playerShadowTarget_);
  playerShadowTarget_=nullptr;playerShadowRoadOpacity_=1.f;
  playerShadowReceiver_=playerShadowBuried_=false;
  playerShadowBuriedEnter_=playerShadowBuriedExit_=0;
  playerShadowGround_=playerShadowLastOrigin_={};playerShadowGroundNormal_={0,1,0};
  if(playerShadowTex_.data)C3D_TexDelete(&playerShadowTex_);
  playerShadowTex_={};
  if(playerShadowClipTex_.data)C3D_TexDelete(&playerShadowClipTex_);
  playerShadowClipTex_={};playerContactTex_.reset();playerShadowDepthTex_.reset();playerShadowFadeTex_.reset();return true;
 }
 if(!C3D_TexInitVRAM(&playerShadowTex_,playerShadow::Size,playerShadow::Size,GPU_RGBA8))return false;
 playerShadowTarget_=C3D_RenderTargetCreateFromTex(&playerShadowTex_,GPU_TEXFACE_2D,0,GPU_RB_DEPTH16);
 if(!playerShadowTarget_){C3D_TexDelete(&playerShadowTex_);playerShadowTex_={};return false;}
 C3D_TexSetFilter(&playerShadowTex_,GPU_LINEAR,GPU_LINEAR);
 C3D_TexSetWrap(&playerShadowTex_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
 playerShadowTex_.border=0;
 if(!C3D_TexInit(&playerShadowClipTex_,8,8,GPU_RGBA8)){
  C3D_RenderTargetDelete(playerShadowTarget_);playerShadowTarget_=nullptr;
  C3D_TexDelete(&playerShadowTex_);playerShadowTex_={};return false;
 }
 // Range coverage also supplies the accepted 112/255 shadow opacity.
 std::memset(playerShadowClipTex_.data,112,8*8*4);
 GSPGPU_FlushDataCache(playerShadowClipTex_.data,8*8*4);
 C3D_TexSetFilter(&playerShadowClipTex_,GPU_NEAREST,GPU_NEAREST);
 C3D_TexSetWrap(&playerShadowClipTex_,GPU_CLAMP_TO_BORDER,GPU_CLAMP_TO_BORDER);
 playerShadowClipTex_.border=0;
 // Receiver depth must be sampled after interpolation: PICA clamps primary
 // vertex colors before interpolation, corrupting large road triangles.
 std::vector<uint8_t> depthPixels(256*8*4);
 for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x){
  const auto at=(y*256+x)*4;depthPixels[at]=uint8_t(x);depthPixels[at+3]=255;
 }
 if(!playerShadowDepthTex_.initRgba8(256,8,depthPixels.data(),depthPixels.size())){
  configurePlayerShadow(false);return false;
 }
 C3D_TexSetFilter(&playerShadowDepthTex_.gpu,GPU_LINEAR,GPU_LINEAR);
 C3D_TexSetWrap(&playerShadowDepthTex_.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
 // Same bounded clip as normal shadows, with an immutable alpha ramp.
 // Sampling the original road vertex alpha gives a spatial fade at exactly
 // the authored transparent sections, shared by the silhouette and wheel AO.
 auto& fadePixels=depthPixels;
 std::fill(fadePixels.begin(),fadePixels.end(),0);
 for(unsigned y=0;y<256;++y)for(unsigned x=0;x<8;++x)
  fadePixels[(y*8+x)*4+3]=uint8_t(std::round(112.f*float(255-y)/255.f));
 if(!playerShadowFadeTex_.initRgba8(8,256,fadePixels.data(),fadePixels.size())){
  configurePlayerShadow(false);return false;
 }
 C3D_TexSetFilter(&playerShadowFadeTex_.gpu,GPU_LINEAR,GPU_LINEAR);
 C3D_TexSetWrap(&playerShadowFadeTex_.gpu,GPU_CLAMP_TO_BORDER,GPU_CLAMP_TO_EDGE);
 playerShadowFadeTex_.gpu.border=0;
 // One shared soft contact decal for every wheel/vehicle, generated once.
 auto& pixels=depthPixels;pixels.assign(32*32*4,0);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
  const float dx=(float(x)-15.5f)/14.5f,dy=(float(y)-15.5f)/14.5f;
  // Coverage, not final opacity: contact and silhouette share the final 112/255 shade.
  // Keep the tire footprint solid; feather only the outer rim.
  // A centre-only peak left the visible contact beside the tire too pale.
  const float t=std::clamp((1.f-std::sqrt(dx*dx+dy*dy))/.35f,0.f,1.f);
  pixels[(y*32+x)*4+3]=uint8_t(std::round(255.f*t*t*(3.f-2.f*t)));
 }
 // If the optional decal allocation fails, the accepted dynamic shadow stays.
 if(!playerContactTex_.ready)playerContactTex_.initRgba8(32,32,pixels.data(),pixels.size());
 // A separate capture preserves the player's texture for the second eye.
 // Optional allocation failure keeps the original Lakitu volume available.
 if(C3D_TexInitVRAM(&actorShadowTex_,playerShadow::Size,playerShadow::Size,GPU_RGBA8)){
  actorShadowTarget_=C3D_RenderTargetCreateFromTex(&actorShadowTex_,GPU_TEXFACE_2D,0,GPU_RB_DEPTH16);
  if(!actorShadowTarget_){C3D_TexDelete(&actorShadowTex_);actorShadowTex_={};}
  else {C3D_TexSetFilter(&actorShadowTex_,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&actorShadowTex_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);actorShadowTex_.border=0;}
 }
 return true;
}
void Renderer::playerShadowNow(const ShadowCaster* casters,unsigned count,const Model* const* receivers,unsigned receiverCount,Vec3 origin,Vec3 lightDirection,float scale,const ShadowVolume* fallback,const ShadowReceiver* objects,unsigned objectCount,bool actor){
 // The judge has an independent capture, and must never overwrite the
 // player's support/snow/fade history. Restore it even on an early return.
 if(actor&&!actorShadowTarget_){if(fallback)shadowVolumesNow(fallback,1,.5f,actor&&!shadowUnion_?1:0);return;}
 const auto savedGround=playerShadowGround_,savedNormal=playerShadowGroundNormal_,savedOrigin=playerShadowLastOrigin_;
 const bool savedReceiver=playerShadowReceiver_,savedBuried=playerShadowBuried_;
 const auto savedEnter=playerShadowBuriedEnter_,savedExit=playerShadowBuriedExit_;
 const float savedOpacity=playerShadowRoadOpacity_;
 auto restore=[&](void*){if(actor){
  std::swap(playerShadowTex_,actorShadowTex_);std::swap(playerShadowTarget_,actorShadowTarget_);
  playerShadowGround_=savedGround;playerShadowGroundNormal_=savedNormal;playerShadowLastOrigin_=savedOrigin;
  playerShadowReceiver_=savedReceiver;playerShadowBuried_=savedBuried;
  playerShadowBuriedEnter_=savedEnter;playerShadowBuriedExit_=savedExit;playerShadowRoadOpacity_=savedOpacity;
 }};
 std::unique_ptr<void,decltype(restore)> guard(this,restore);
 if(actor){
  std::swap(playerShadowTex_,actorShadowTex_);std::swap(playerShadowTarget_,actorShadowTarget_);
  playerShadowGround_={origin.x,fallback?fallback->transform.r[1].w-100.f:origin.y-300.f,origin.z};
  playerShadowGroundNormal_={0,1,0};playerShadowReceiver_=true;playerShadowBuried_=false;playerShadowRoadOpacity_=1.f;
 }
 // Like the authored volume shadow, visibility follows the racer and the
 // already rendered scene depth. The center ray only places contacts and
 // rejects an entered snow shell; missing one triangle never hides the pass.
 const bool movingSupport=count&&casters[0].movingSupport;
#ifndef MKW_RELEASE
 const auto supportStart=svcGetSystemTick();
#endif
 if(!actor&&!eye_&&!bottomWorldPass_){
  if(length(origin-playerShadowLastOrigin_)>512.f){
   playerShadowReceiver_=false;playerShadowBuried_=false;
   playerShadowBuriedEnter_=playerShadowBuriedExit_=0;
  }
  playerShadowLastOrigin_=origin;
  Vec3 supportNormal{};float supportY=0,maxRadius=0,wheelCenterY=0;unsigned supports=0;
  for(unsigned i=0;i<count;++i){const auto& c=casters[i];
   maxRadius=std::max(maxRadius,c.contactRadius);
   if(!c.wheelContact||c.contactRadius<=0||c.contactNormal.y<=.2f)continue;
   const Vec3 n=c.contactNormal,p=c.contactCenter-n*c.contactRadius;
   supportY+=p.y-(n.x*(origin.x-p.x)+n.z*(origin.z-p.z))/n.y;
   supportNormal+=n;wheelCenterY+=c.contactCenter.y;++supports;
  }
  if(supports){supportY/=float(supports);wheelCenterY/=float(supports);supportNormal=normalized(supportNormal);}
  Vec3 candidate{},candidateNormal{};float topY=-1.e30f,roadOpacity=1.f;
  const float startHeight=supports?supportY+4.f-origin.y:120.f;
  const bool found=!movingSupport&&hasPlayerReceiver(receivers,receiverCount,origin,candidate,candidateNormal,
      startHeight,&topY,std::max(32.f,maxRadius*2.5f),&roadOpacity);
  // A material boundary may change alpha abruptly even though its triangle
  // interpolation is smooth. Ease the actual support opacity once per frame.
  playerShadowRoadOpacity_+=(std::clamp(roadOpacity,0.f,1.f)-playerShadowRoadOpacity_)*.15f;
  if(supports){
   // The actual collision support wins over a lower floor hit when a road
   // seam or suspension offset puts the center ray just below the surface.
   playerShadowGround_={origin.x,supportY,origin.z};playerShadowGroundNormal_=supportNormal;
   if(found&&std::fabs(candidate.y-supportY)<=playerShadow::contactHeightTolerance()){
    playerShadowGround_=candidate;playerShadowGroundNormal_=candidateNormal;
   }else if(!movingSupport&&topY>=supportY-4.f&&topY<=supportY+8.f){
    playerShadowGround_.y=topY;
   }
   playerShadowReceiver_=true;
   // An enclosing shell must sit above the wheel centres AND have a
   // distinct lower rendered support. A road/KCL offset alone is not snow.
   // Retain the existing temporal hysteresis, without using it as visibility.
   if(movingSupport){playerShadowBuried_=false;playerShadowBuriedEnter_=playerShadowBuriedExit_=0;}
   else if(found&&candidate.y<topY-8.f&&topY>wheelCenterY){
    playerShadowBuriedExit_=0;
    if(++playerShadowBuriedEnter_>=3)playerShadowBuried_=true;
   }else{
    playerShadowBuriedEnter_=0;
    if(++playerShadowBuriedExit_>=3)playerShadowBuried_=false;
   }
  }else if(found&&!playerShadowBuried_){
   playerShadowGround_=candidate;playerShadowGroundNormal_=candidateNormal;playerShadowReceiver_=true;
  }else if(!playerShadowReceiver_){
   playerShadowGround_=origin-Vec3{0,std::max(40.f,maxRadius),0};playerShadowGroundNormal_={0,1,0};
   playerShadowReceiver_=true;
  }
 }
#ifndef MKW_RELEASE
 renderMetrics.shadowSupportMs+=float((svcGetSystemTick()-supportStart)*1000.0/SYSCLOCK_ARM11);
#endif
 const bool softTerrain=playerShadowBuried_;
 if(!playerShadowTarget_||!count){if(fallback&&!softTerrain)shadowVolumesNow(fallback,1,.5f,actor&&!shadowUnion_?1:0);return;}
 flushSprites();retailEndSubmesh();resetMaterialUniforms();
 // Isolated projection passes must not inherit scrolling road/object UVs.
 // Otherwise the contact pass samples a shifted silhouette, marks its stencil,
 // then rejects the correctly projected receiver beneath it.
 C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentPrimaryLoc_,false);
 const float extent=std::max(320.f,320.f*scale);
 const Vec3 direction=playerShadow::directionOnSupport(lightDirection,playerShadowGroundNormal_);
 const Vec3 eye=origin-direction*1600.f;
 // R carries caster light-depth; A remains the exact existing silhouette.
 // Match the span to racer scale so small/large states retain precision.
 const float depthSpan=std::max(1024.f,1024.f*scale);
 C3D_Mtx lightView,lightProjection;
 const Vec3 lightUp=std::fabs(direction.y)>.98f?Vec3{0,0,1}:Vec3{0,1,0};
 Mtx_LookAt(&lightView,FVec3_New(eye.x,eye.y,eye.z),FVec3_New(origin.x,origin.y,origin.z),FVec3_New(lightUp.x,lightUp.y,lightUp.z),false);
 Mtx_Ortho(&lightProjection,-extent,extent,-extent,extent,1.f,4000.f,false);
 // One capture per presented frame, shared by stereo and the intro's lower
 // view. Never write a texture after the other eye has begun sampling it.
 if(!eye_&&!bottomWorldPass_){
  C3D_FrameSplit(GX_CMDLIST_FLUSH);
  // Premultiplied light depth: empty pixels have zero depth AND coverage.
  // Compare receiverDepth*coverage against storedDepth, never interpolate
  // a fictitious far-plane depth into a thin tire's silhouette.
  C3D_RenderTargetClear(playerShadowTarget_,C3D_CLEAR_ALL,0,0);
  C3D_FrameDrawOn(playerShadowTarget_);C3D_SetViewport(0,0,playerShadow::Size,playerShadow::Size);
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,shadowCaptureLoc_,true);
  C3D_FVUnifSet(GPU_VERTEX_SHADER,shadowDepthLoc_,0,0,-1.f/depthSpan,.5f-1600.f/depthSpan);
  // Light-depth at the intersection of this vertex's projection ray and
  // the supporting plane. Clamping only the depth (not UV/geometry) keeps
  // wheel silhouettes complete when suspension lets the mesh cross ground.
  const Vec3 floorNormal=playerShadowGroundNormal_;
  const float nd=dot(floorNormal,direction);
  C3D_Mtx inverseLight=lightView;
  float floorRow[4]={0,0,0,1};
  if(nd<-.05f&&Mtx_Inverse(&inverseLight)){
   const Vec3 coefficient=(direction-floorNormal/nd)/depthSpan;
   const float plane[4]={coefficient.x,coefficient.y,coefficient.z,
    .5f-dot(direction,origin)/depthSpan+dot(floorNormal,playerShadowGround_)/(nd*depthSpan)};
   std::fill(std::begin(floorRow),std::end(floorRow),0.f);
   for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)floorRow[c]+=plane[r]*inverseLight.r[r].c[3-c];
  }
  C3D_FVUnifSet(GPU_VERTEX_SHADER,shadowFloorLoc_,floorRow[0],floorRow[1],floorRow[2],floorRow[3]);
  // Leave a transparent four-texel guard around the capture. Rasterization or
  // bilinear sampling at a frustum edge must never expose a black border.
  C3D_SetScissor(GPU_SCISSOR_NORMAL,4,4,playerShadow::Size-4,playerShadow::Size-4);
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,playerShadowProjectLoc_,false);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&lightProjection);
  C3D_DepthMap(true,-1,0);C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
  C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);
  C3D_CullFace(GPU_CULL_NONE);
  C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,false);
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,retailBoolLoc_,false);
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,diffuseLightingLoc_,false);
  C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
  // AO is projected separately along each wheel's support normal. It must
  // not enter the sunlight capture, where curved receivers stretch it.
  for(unsigned i=0;i<count;++i){
   const auto& c=casters[i];if(!c.model)continue;const auto& model=*c.model;
   C3D_Mtx mv;Mtx_Multiply(&mv,&lightView,&c.transform);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&mv);
   auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
   AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
   auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,model.vertices,sizeof(Vertex),4,0x3210);
   const bool skin=c.palette&&c.bones<=MaxSkinBones;
   const bool lit=skin&&model.skinBoneIndices&&!model.fixedLighting&&!model.staticLightingBaked;
   if(lit){AttrInfo_AddLoader(attr,4,GPU_FLOAT,1);BufInfo_Add(buf,model.skinBoneIndices,sizeof(float),1,4);}
   C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,skin);C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,lit);
   if(skin)for(unsigned b=0;b<c.bones;++b)for(unsigned r=0;r<3;++r){const auto* m=c.palette+b*c.stride+r*4;C3D_FVUnifSet(GPU_VERTEX_SHADER,bonesLoc_+int(b*3+r),m[0],m[1],m[2],m[3]);}
   for(const auto& s:model.submeshes){
    const unsigned first=c.count?std::max<unsigned>(s.first,c.first):s.first;
    const unsigned end=c.count?std::min<unsigned>(s.first+s.count,c.first+c.count):s.first+s.count;
    if(end<=first)continue;
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* e=C3D_GetTexEnv(0);C3D_TexEnvColor(e,0xff000000u);
    C3D_TexEnvSrc(e,C3D_RGB,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
    const bool cutout=(s.flags&1)&&s.texture>=0;
    C3D_TexEnvSrc(e,C3D_Alpha,cutout?GPU_TEXTURE0:GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
    C3D_AlphaTest(cutout,GPU_GREATER,127);
    if(cutout){
     C3D_TexBind(0,model.textures[s.texture]->sampledGpu());
     C3D_TexEnvSrc(e,C3D_RGB,GPU_PRIMARY_COLOR,GPU_TEXTURE0);
     C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
     C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
    }
    C3D_DrawElements(GPU_TRIANGLES,end-first,C3D_UNSIGNED_SHORT,model.indices+first);
    stats_.triangles+=(end-first)/3;++stats_.drawCalls;
   }
  }
 }
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,shadowCaptureLoc_,false);
 // The original scene's depth/stencil stays intact across the auxiliary pass.
 auto* target=bottomWorldPass_?bottomTarget_:(bloomFrame_||stadiumFrame_)?sceneTarget_:topTarget();
 C3D_FrameDrawOn(target);
 C3D_SetViewport(0,0,240,bottomWorldPass_?320:400);
 C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,240,bottomWorldPass_?320:400);
 C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
 C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&view_);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,false);C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,false);
 auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
 AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
 C3D_Mtx inverse=view_,worldToLight,viewToLight;
 if(!Mtx_Inverse(&inverse)){if(fallback)shadowVolumesNow(fallback,1,.5f,actor&&!shadowUnion_?1:0);return;}
 Mtx_Multiply(&worldToLight,&lightProjection,&lightView);Mtx_Multiply(&viewToLight,&worldToLight,&inverse);
 for(unsigned r=0;r<2;++r)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[r],
  .5f*(viewToLight.r[r].x+viewToLight.r[3].x),.5f*(viewToLight.r[r].y+viewToLight.r[3].y),
  .5f*(viewToLight.r[r].z+viewToLight.r[3].z),.5f*(viewToLight.r[r].w+viewToLight.r[3].w));
 C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[2],viewToLight.r[3].x,viewToLight.r[3].y,viewToLight.r[3].z,viewToLight.r[3].w);
 C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,0,0,0,0);
 // Compare each receiver to the captured caster at the same light UV.
 // Four 8-bit steps cover quantization plus the finite TEV comparison ramp,
 // preserving full opacity at contact without the previous
 // broad front-side allowance (which accepted the opposing bowl wall).
 const float depthPlane[4]={direction.x/depthSpan,direction.y/depthSpan,direction.z/depthSpan,
     .5f-dot(direction,origin)/depthSpan+4.f/255.f};
 float depthRow[4]{};
 for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)depthRow[c]+=depthPlane[r]*inverse.r[r].c[3-c];
 C3D_FVUnifSet(GPU_VERTEX_SHADER,shadowDepthLoc_,depthRow[0],depthRow[1],depthRow[2],depthRow[3]);
 // A bounded light-space volume, not an infinite projected image. Unit 1
 // includes the whole caster footprint (including front wheel contacts),
 // while excluding surfaces beyond that footprint toward the light.
 const float reach=actor?2400.f:1800.f;
 const float plane[4]={direction.x/reach,direction.y/reach,direction.z/reach,
                      (extent*.5f-dot(direction,origin))/reach};
 float clipRow[4]{};
 for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)clipRow[c]+=plane[r]*inverse.r[r].c[3-c];
 C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],clipRow[0],clipRow[1],clipRow[2],clipRow[3]);
 // While inside a soft shell, clip its upper surface against the actual
 // lower support plane. Scene depth still occludes the shadow naturally;
 // never pull it through the opaque snow or toggle the whole caster off.
 float supportRow[4]={0,0,0,.5f};
 if(softTerrain){
  const Vec3 n=playerShadowGroundNormal_;
  const float supportPlane[4]={n.x/reach,n.y/reach,n.z/reach,
      1.f-(dot(n,playerShadowGround_)+4.f)/reach};
  std::fill(std::begin(supportRow),std::end(supportRow),0.f);
  for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)supportRow[c]+=supportPlane[r]*inverse.r[r].c[3-c];
 }
 C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],supportRow[0],supportRow[1],supportRow[2],supportRow[3]);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,true);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,playerShadowProjectLoc_,true);
 C3D_TexBind(1,&playerShadowClipTex_);
 // Reject whole receiver blocks outside the light capture, before issuing
 // GPU draws. Previously every nearby block was sent even with all UVs outside.
 float clipRows[16];for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
  clipRows[r*4+c]=worldToLight.r[r].c[3-c];
 ClipFrustum shadowFrustum(clipRows);
 C3D_Tex projected=playerShadowTex_;
 projected.param=(projected.param&~GPU_TEXTURE_MODE(7))|GPU_TEXTURE_MODE(GPU_TEX_PROJECTION);
 C3D_Mtx cameraClip;Mtx_Multiply(&cameraClip,&projection_,&view_);
 float cameraRows[16];for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)cameraRows[r*4+c]=cameraClip.r[r].c[3-c];
 const ClipFrustum receiverView(cameraRows);
 // Render-thread scratch: reuse capacity, rebuild for each projection/eye.
 // Never reuse model pointers or visibility decisions across frames.
 struct ReceiverRun {const Model* model;unsigned sub;IndexRange range;bool fade;};
 static std::vector<ReceiverRun> receiverRuns;
 bool receiversPrepared=false,contactPass=false;
 const float viewDeterminant=view_.r[0].x*(view_.r[1].y*view_.r[2].z-view_.r[1].z*view_.r[2].y)
     -view_.r[0].y*(view_.r[1].x*view_.r[2].z-view_.r[1].z*view_.r[2].x)
     +view_.r[0].z*(view_.r[1].x*view_.r[2].y-view_.r[1].y*view_.r[2].x);
 auto receiversPass=[&](){
  if(!receiversPrepared){
   receiverRuns.clear();const float radius=actor?2400.f:extent*4.f;
   for(unsigned i=0;i<receiverCount;++i){
    const auto& model=*receivers[i];
    if(!playerShadow::intersects(model.minimum,model.maximum,origin,radius))continue;
    const bool fade=std::any_of(model.submeshes.begin(),model.submeshes.end(),[](const auto& s){return s.lighting&rainbowShadow::Flag;});
    for(unsigned j=0;j<model.visibility.size();++j){
     const auto& sub=model.submeshes[j];
     if(!(sub.lighting&rainbowShadow::Flag)&&!playerShadow::receives(sub.flags))continue;
     const auto& vis=contactPass&&model.contactIndices?model.contactCoarseVisibility[j]:model.visibility[j];
     const unsigned lightMask=shadowFrustum.remainingPlanes(vis.bounds),viewMask=receiverView.remainingPlanes(vis.bounds);
     if(lightMask==ClipFrustum::Outside||viewMask==ClipFrustum::Outside)continue;
     IndexRange run{};
     auto submit=[&](){if(run.count){receiverRuns.push_back({&model,j,run,fade});run={};}};
     for(const auto& block:vis.blocks){
      if(!playerShadow::intersects(block.bounds.minimum,block.bounds.maximum,origin,radius)||
         !shadowFrustum.intersects(block.bounds,lightMask)||!receiverView.intersects(block.bounds,viewMask)){submit();continue;}
      auto append=[&](IndexRange indices){
       if(run.count&&run.first+run.count!=indices.first)submit();
       if(!run.count)run.first=indices.first;
       run.count+=indices.count;
      };
      // Keep coarse rejection, then inspect only its eight immutable children.
      // A wheel must not transform 128 road triangles for a tiny footprint.
      if(contactPass&&model.contactVisibility.size()==model.visibility.size()){
       const auto& fine=model.contactVisibility[j].blocks;
       const unsigned first=(block.indices.first-sub.first)/48;
       const unsigned end=std::min<unsigned>(fine.size(),first+(block.indices.count+47)/48);
       const unsigned childLight=shadowFrustum.remainingPlanes(block.bounds);
       const unsigned childView=receiverView.remainingPlanes(block.bounds);
       for(unsigned k=first;k<end;++k){
        if(!shadowFrustum.intersects(fine[k].bounds,childLight)||!receiverView.intersects(fine[k].bounds,childView)){submit();continue;}
        append(fine[k].indices);
       }
      }else append(block.indices);
     }
     submit();
    }
   }
   receiversPrepared=true;
  }
  const Model* bound=nullptr;unsigned boundSub=~0u;
  for(const auto& run:receiverRuns){
   const auto& model=*run.model;const auto& sub=model.submeshes[run.sub];
   if(bound!=&model){
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,model.vertices,sizeof(Vertex),4,0x3210);
    bound=&model;boundSub=~0u;
   }
   if(boundSub!=run.sub){
    C3D_CullFace(run.fade&&!(sub.flags&4)?(viewDeterminant<0?GPU_CULL_BACK_CCW:GPU_CULL_FRONT_CCW):GPU_CULL_NONE);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,shadowReceiverFadeLoc_,run.fade?0.f:1.f,0,run.fade?playerShadowRoadOpacity_:0.f,0);
    C3D_TexBind(1,run.fade?&playerShadowFadeTex_.gpu:&playerShadowClipTex_);
    C3D_DepthMap(true,-1,playerShadow::receiverBias(sub.lighting)+playerShadow::ReceiverReplayBias);
    boundSub=run.sub;
   }
   C3D_DrawElements(GPU_TRIANGLES,run.range.count,C3D_UNSIGNED_SHORT,(contactPass&&model.contactIndices?model.contactIndices:model.indices)+run.range.first);
   stats_.triangles+=run.range.count/3;++stats_.drawCalls;
  }
 };
 // Moving/static drivable objects use the exact mesh/pose that wrote
 // scene depth this frame. The same stencil deduplicates track/object layers.
 auto objectPass=[&](){
 for(unsigned i=0;i<objectCount;++i){
  const auto& r=objects[i];if(!r.model||!r.count||!shadowFrustum.intersects(r.bounds)||!receiverView.intersects(r.bounds))continue;
  const auto& model=*r.model;
  C3D_Mtx mv;Mtx_Multiply(&mv,&view_,&r.transform);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&mv);
  const bool skin=r.palette&&r.bones<=MaxSkinBones;
  const bool indexed=skin&&model.skinBoneIndices&&!model.fixedLighting&&!model.staticLightingBaked;
  auto* a=C3D_GetAttrInfo();AttrInfo_Init(a);
  AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,3);
  AttrInfo_AddLoader(a,2,GPU_FLOAT,4);AttrInfo_AddLoader(a,3,GPU_UNSIGNED_BYTE,4);
  auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,model.vertices,sizeof(Vertex),4,0x3210);
  if(indexed){AttrInfo_AddLoader(a,4,GPU_FLOAT,1);BufInfo_Add(buf,model.skinBoneIndices,sizeof(float),1,4);}
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,skin);C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,indexed);
  if(skin)for(unsigned b=0;b<r.bones;++b)for(unsigned row=0;row<3;++row){
   const auto* m=r.palette+b*12+row*4;C3D_FVUnifSet(GPU_VERTEX_SHADER,bonesLoc_+int(b*3+row),m[0],m[1],m[2],m[3]);
  }
  C3D_DepthMap(true,-1,playerShadow::receiverBias(r.lighting)+playerShadow::ReceiverReplayBias);
  C3D_DrawElements(GPU_TRIANGLES,r.count,C3D_UNSIGNED_SHORT,model.indices+r.first);
  stats_.triangles+=r.count/3;++stats_.drawCalls;
 }
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,false);C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,false);
 };
 // Both projections shade actual surfaces with one shared stencil.
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,retailBoolLoc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,diffuseLightingLoc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,true);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,playerShadowProjectLoc_,true);
 C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
 C3D_TexEnvBufUpdate(C3D_Both,0);
 C3D_AlphaTest(true,GPU_GREATER,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,shadowUnion_?GPU_BLEND_MAX:GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,shadowUnion_?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA);
 C3D_DepthTest(true,GPU_GEQUAL,shadowUnion_?GPU_WRITE_ALPHA:GPU_WRITE_COLOR);
 C3D_StencilTest(true,GPU_EQUAL,0,0xff,0xff);
 C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_INCR);

 // Resolve max(body coverage, contact coverage) locally BEFORE marking
 // stencil. A faint body fringe must not reject a darker contact footprint.
 // No collision-plane polygon is drawn into the scene, including platforms.
#ifndef MKW_RELEASE
 const auto contactStart=svcGetSystemTick();
 const unsigned beforeContactDraws=stats_.drawCalls,beforeContactTriangles=stats_.triangles;
#endif
 for(unsigned wheel=0;playerContactTex_.ready&&wheel<count;++wheel){
  const auto& c=casters[wheel];if(c.hideContact||c.contactRadius<=0.f)continue;
  const float opacity=playerShadow::contactOpacity(c.contactRadius,dot(c.contactCenter-playerShadowGround_,playerShadowGroundNormal_));
  if(opacity<=0.f)continue;
  const Vec3 n=normalized(c.contactNormal,playerShadowGroundNormal_);
  Vec3 point=c.contactCenter-n*c.contactRadius;
  if(!c.wheelContact&&playerShadowGroundNormal_.y>.2f)
   point.y-=dot(point-playerShadowGround_,playerShadowGroundNormal_)/playerShadowGroundNormal_.y;
  const Vec3 axle{c.transform.r[0].x,c.transform.r[1].x,c.transform.r[2].x};
  const Vec3 tangent=normalized(axle-n*dot(axle,n),{1,0,0});
  const Vec3 forward=normalized(cross(n,tangent));
  const float width=std::max(c.contactHalfWidth,1.f),height=std::max(c.contactRadius*c.contactSpread,1.f);
  C3D_Mtx aoView,aoProjection,aoWorld,aoCamera;
  const Vec3 aoEye=point+n*100.f;
  Mtx_LookAt(&aoView,FVec3_New(aoEye.x,aoEye.y,aoEye.z),FVec3_New(point.x,point.y,point.z),FVec3_New(forward.x,forward.y,forward.z),false);
  Mtx_Ortho(&aoProjection,-width,width,-height,height,68.f,132.f,false);
  Mtx_Multiply(&aoWorld,&aoProjection,&aoView);Mtx_Multiply(&aoCamera,&aoWorld,&inverse);
  float rows[16];for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)rows[r*4+col]=aoWorld.r[r].c[3-col];
  shadowFrustum=ClipFrustum(rows);receiversPrepared=false;contactPass=true;
  // Conservative projection of the whole contact slab, with filter/rounding
  // margin. Tilted PICA viewport is 240 by screenWidth. Near-plane crossings
  // fall back to an unrestricted scissor rather than clipping visible pixels.
  float x0=240.f,y0=bottomWorldPass_?320.f:400.f,x1=0,y1=0;
  const float viewportY=bottomWorldPass_?320.f:400.f;bool safe=true;
  for(unsigned corner=0;corner<8;++corner){
   const Vec3 v=point+tangent*((corner&1?1.f:-1.f)*width*1.125f)
       +forward*((corner&2?1.f:-1.f)*height*1.125f)+n*(corner&4?40.f:-40.f);
   auto project=[&](unsigned r){return cameraClip.r[r].x*v.x+cameraClip.r[r].y*v.y+cameraClip.r[r].z*v.z+cameraClip.r[r].w;};
   const float cw=project(3);if(cw<=1e-3f){safe=false;break;}
   const float x=(project(0)/cw+1.f)*120.f,y=(project(1)/cw+1.f)*viewportY*.5f;
   x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
  }
  if(safe){
   const unsigned left=unsigned(std::clamp(std::floor(x0-2.f),0.f,240.f)),right=unsigned(std::clamp(std::ceil(x1+2.f),0.f,240.f));
   const unsigned top=unsigned(std::clamp(std::floor(y0-2.f),0.f,viewportY)),bottom=unsigned(std::clamp(std::ceil(y1+2.f),0.f,viewportY));
   if(left>=right||top>=bottom)continue;
   C3D_SetScissor(GPU_SCISSOR_NORMAL,left,top,right,bottom);
  }else C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,240,unsigned(viewportY));
  for(unsigned r=0;r<2;++r)C3D_FVUnifSet(GPU_VERTEX_SHADER,normalRowLoc_[r],
   .5f*(aoCamera.r[r].x+aoCamera.r[3].x),.5f*(aoCamera.r[r].y+aoCamera.r[3].y),
   .5f*(aoCamera.r[r].z+aoCamera.r[3].z),.5f*(aoCamera.r[r].w+aoCamera.r[3].w));
  C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,true);
  // Clip receiver depth to a 64-unit slab around the actual tire support,
  // so a distant bowl wall cannot inherit another copy of this footprint.
  const float plane[4]={n.x/64.f,n.y/64.f,n.z/64.f,.5f-dot(n,point)/64.f};
  float row[4]{};for(unsigned col=0;col<4;++col)for(unsigned r=0;r<4;++r)row[col]+=plane[r]*inverse.r[r].c[3-col];
  C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],row[0],row[1],row[2],row[3]);
  C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],supportRow[0],supportRow[1],supportRow[2],supportRow[3]);
  // Binary footprint mask, independent of the final union opacity. Bit 0
  // means already shaded; bit 1 is this wheel's temporary footprint.
  auto drawContactReceivers=[&](){
   C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&view_);
   auto* attributes=C3D_GetAttrInfo();AttrInfo_Init(attributes);
   AttrInfo_AddLoader(attributes,0,GPU_FLOAT,3);AttrInfo_AddLoader(attributes,1,GPU_FLOAT,3);AttrInfo_AddLoader(attributes,2,GPU_FLOAT,4);AttrInfo_AddLoader(attributes,3,GPU_UNSIGNED_BYTE,4);
   receiversPass();C3D_CullFace(GPU_CULL_NONE);
   C3D_FVUnifSet(GPU_VERTEX_SHADER,shadowReceiverFadeLoc_,1,0,0,0);C3D_TexBind(1,&playerShadowClipTex_);
   objectPass();
  };
  C3D_TexBind(0,&projected);C3D_TexBind(2,&playerContactTex_.gpu);
  for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
  auto* mask=C3D_GetTexEnv(0);C3D_TexEnvColor(mask,uint32_t(std::round(255.f*opacity))<<24);
  C3D_TexEnvSrc(mask,C3D_Alpha,GPU_TEXTURE2,GPU_CONSTANT);C3D_TexEnvFunc(mask,C3D_Alpha,GPU_MODULATE);
  auto* maskClip=C3D_GetTexEnv(4);C3D_TexEnvSrc(maskClip,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE1);C3D_TexEnvFunc(maskClip,C3D_Alpha,GPU_MODULATE);
  C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITEMASK(0));
  C3D_StencilTest(true,GPU_EQUAL,2,1,2); // ref & mask == 0, write bit 1
  C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
  drawContactReceivers();
  C3D_DepthTest(true,GPU_GEQUAL,shadowUnion_?GPU_WRITE_ALPHA:GPU_WRITE_COLOR);
  C3D_StencilTest(true,GPU_EQUAL,2,3,3);
  C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_INVERT); // 2 -> 1
  for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
  auto* ao=C3D_GetTexEnv(0);C3D_TexEnvColor(ao,uint32_t(std::round(255.f*opacity))<<24);
  C3D_TexEnvSrc(ao,C3D_RGB,GPU_TEXTURE2);C3D_TexEnvOpRgb(ao,GPU_TEVOP_RGB_SRC_ALPHA);C3D_TexEnvFunc(ao,C3D_RGB,GPU_REPLACE);C3D_TexEnvScale(ao,C3D_RGB,GPU_TEVSCALE_4);
  C3D_TexEnvSrc(ao,C3D_Alpha,GPU_TEXTURE2,GPU_CONSTANT);C3D_TexEnvFunc(ao,C3D_Alpha,GPU_MODULATE);
  auto* difference=C3D_GetTexEnv(1);
  C3D_TexEnvSrc(difference,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(difference,C3D_Alpha,GPU_SUBTRACT);
  C3D_TexEnvScale(difference,C3D_RGB,GPU_TEVSCALE_4);
  auto* united=C3D_GetTexEnv(2);
  C3D_TexEnvSrc(united,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(united,C3D_Alpha,GPU_ADD);
  C3D_TexEnvScale(united,C3D_RGB,GPU_TEVSCALE_4);
  // The stencil is the gate; never multiply the union by a soft mask.
  auto* footprint=C3D_GetTexEnv(3);C3D_TexEnvColor(footprint,0);
  C3D_TexEnvSrc(footprint,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(footprint,C3D_RGB,GPU_REPLACE);
  auto* clip=C3D_GetTexEnv(4);C3D_TexEnvSrc(clip,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE1);C3D_TexEnvFunc(clip,C3D_Alpha,GPU_MODULATE);
  drawContactReceivers();
 }
#ifndef MKW_RELEASE
 renderMetrics.contactMs+=float((svcGetSystemTick()-contactStart)*1000.0/SYSCLOCK_ARM11);
 renderMetrics.contactDraws+=stats_.drawCalls-beforeContactDraws;
 renderMetrics.contactTriangles+=stats_.triangles-beforeContactTriangles;
#endif
 C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,240,bottomWorldPass_?320:400);
 receiversPrepared=false;contactPass=false;
 // Shade the remaining body projection after the contact union.
 shadowFrustum=ClipFrustum(clipRows);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,false);
 C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&view_);
 auto* bodyAttr=C3D_GetAttrInfo();AttrInfo_Init(bodyAttr);
 AttrInfo_AddLoader(bodyAttr,0,GPU_FLOAT,3);AttrInfo_AddLoader(bodyAttr,1,GPU_FLOAT,3);AttrInfo_AddLoader(bodyAttr,2,GPU_FLOAT,4);AttrInfo_AddLoader(bodyAttr,3,GPU_UNSIGNED_BYTE,4);
 // Shade the body first on actual receiving geometry. Local AO below uses
 // the same stencil to avoid double darkness, never a floating contact plane.
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,retailBoolLoc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,diffuseLightingLoc_,false);
 C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
 C3D_TexEnvBufUpdate(C3D_Both,0);
 C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],clipRow[0],clipRow[1],clipRow[2],clipRow[3]);
 C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],supportRow[0],supportRow[1],supportRow[2],supportRow[3]);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,true);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,playerShadowProjectLoc_,true);
 C3D_TexBind(1,&playerShadowClipTex_);
 for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
 // R = filtered(depth * coverage), A = filtered(coverage). Compare
 // receiverDepth*A-R: equivalent to comparing conditional caster depth,
 // without division or contamination from the transparent background.
 auto* weightedReceiver=C3D_GetTexEnv(0);
 C3D_TexEnvColor(weightedReceiver,0x70000000u);
 C3D_TexEnvSrc(weightedReceiver,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(weightedReceiver,C3D_RGB,GPU_REPLACE);
 C3D_TexEnvSrc(weightedReceiver,C3D_Alpha,GPU_TEXTURE2,GPU_TEXTURE0);
 C3D_TexEnvOpAlpha(weightedReceiver,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_ALPHA);
 C3D_TexEnvFunc(weightedReceiver,C3D_Alpha,GPU_MODULATE);
 auto* depthCompare=C3D_GetTexEnv(1);
 C3D_TexEnvSrc(depthCompare,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);
 C3D_TexEnvOpAlpha(depthCompare,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_R);
 C3D_TexEnvFunc(depthCompare,C3D_Alpha,GPU_SUBTRACT);C3D_TexEnvScale(depthCompare,C3D_Alpha,GPU_TEVSCALE_4);
 for(unsigned stage=2;stage<=3;++stage)C3D_TexEnvScale(C3D_GetTexEnv(stage),C3D_Alpha,GPU_TEVSCALE_4);
 auto* silhouette=C3D_GetTexEnv(4);
 C3D_TexEnvSrc(silhouette,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(silhouette,C3D_Alpha,GPU_MODULATE);
 auto* range=C3D_GetTexEnv(5);
 C3D_TexEnvSrc(range,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE1);C3D_TexEnvFunc(range,C3D_Alpha,GPU_MODULATE);
 C3D_AlphaTest(true,GPU_GREATER,0);C3D_CullFace(GPU_CULL_NONE);
 C3D_AlphaBlend(GPU_BLEND_ADD,shadowUnion_?GPU_BLEND_MAX:GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,shadowUnion_?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA);
 C3D_TexBind(0,&projected);C3D_TexBind(2,&playerShadowDepthTex_.gpu);
 C3D_DepthTest(true,GPU_GEQUAL,shadowUnion_?GPU_WRITE_ALPHA:GPU_WRITE_COLOR);
 C3D_StencilTest(true,GPU_EQUAL,0,0xff,0xff);
 C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_INCR);
 receiversPass();
 C3D_CullFace(GPU_CULL_NONE);
 C3D_FVUnifSet(GPU_VERTEX_SHADER,shadowReceiverFadeLoc_,1,0,0,0);
 C3D_TexBind(1,&playerShadowClipTex_);
 objectPass();
 C3D_TexBind(0,nullptr);C3D_TexBind(1,nullptr);C3D_TexBind(2,nullptr);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,false);
 C3D_BoolUnifSet(GPU_VERTEX_SHADER,playerShadowProjectLoc_,false);
 // Reset per-caster coverage. Scene alpha retains the maximum across
 // casters, so faint edges cannot block a darker overlapping silhouette.
 {
 C3D_StencilTest(true,GPU_ALWAYS,0,0xff,0xff);
 C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_ZERO);
 // The stencil-clear quad uses packed Vertex, not the optional fifth bone
 // attribute of the last animated object receiver.
 auto* restoredAttr=C3D_GetAttrInfo();AttrInfo_Init(restoredAttr);
 AttrInfo_AddLoader(restoredAttr,0,GPU_FLOAT,3);AttrInfo_AddLoader(restoredAttr,1,GPU_FLOAT,3);
 AttrInfo_AddLoader(restoredAttr,2,GPU_FLOAT,4);AttrInfo_AddLoader(restoredAttr,3,GPU_UNSIGNED_BYTE,4);
 screenFadeNow(0,false,false,shadowUnion_?3:0);
 }
 C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
 C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
 C3D_DepthMap(true,-1,0);C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
 resetMaterialUniforms();clearEnvironmentUvNow();spriteStateReady_=false;
}
}
