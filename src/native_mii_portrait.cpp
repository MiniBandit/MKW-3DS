#include <mkw/native_mii_portrait.hpp>
#include <mkw/native_mii_icon_body.hpp>
#include <cfl_mii.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace mkw {
namespace {
struct Session {
 bool initialized=false;
 CFLCharModel head{};C3D_Tex image{};C3D_RenderTarget* target=nullptr;
 NativeMiiIconVertex* body=nullptr;unsigned char* indices=nullptr;unsigned char* readback=nullptr;
 ~Session(){
  C3D_FrameSync();
  if(target)C3D_RenderTargetDelete(target);
  if(image.data)C3D_TexDelete(&image);
  CFL_DeleteModel(&head);
  if(initialized)CFL_Finalize();
  linearFree(body);linearFree(indices);linearFree(readback);
 }
};
}
static bool composeNativeMiiImage(const NativeMii& identity,unsigned size,std::vector<std::uint8_t>& output,bool withBody,CFLExpression expression){
 if(identity[0]!=3||(size!=64&&size!=128&&size!=256))return false;
 auto storage=std::make_unique<Session>();auto& s=*storage;
 if(!(s.initialized=CFL_Initialize()))return false;
 MiiData mii{};std::memcpy(&mii,identity.data(),sizeof mii);
 if(!CFL_InitCharModel(&s.head,&mii,CFL_RESOLUTION_128,CFL_EXPRESSION_FLAG(expression))||
    !CFL_IsAvailableExpression(&s.head,expression))return false;
 s.body=static_cast<NativeMiiIconVertex*>(linearAlloc(sizeof NativeMiiIconBody));
 s.indices=static_cast<unsigned char*>(linearAlloc(sizeof NativeMiiIconBodyIndices));
 s.readback=static_cast<unsigned char*>(linearAlloc(size*size*4));
 if(!s.body||!s.indices||!s.readback||!C3D_TexInitVRAM(&s.image,size,size,GPU_RGBA8))return false;
 s.target=C3D_RenderTargetCreateFromTex(&s.image,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);
 if(!s.target)return false;
 // Native IconBody bind pose is aligned to its head joint. Apply the native
 // body-proportion formula below the neck; the head itself never scales.
 const float height=mii.height/128.f,build=mii.width/128.f;
 const float sy=height*.77f+.5f,sx=build*(height*.47f+.4f)+height*.23f+.4f;
 for(unsigned i=0;i<std::size(NativeMiiIconBody);++i){
  s.body[i]=NativeMiiIconBody[i];auto& v=s.body[i];
  v.position[0]*=sx;v.position[1]=(v.position[1]-NativeMiiIconNeckY)*sy;v.position[2]*=sx;
  v.normal[0]/=sx;v.normal[1]/=sy;v.normal[2]/=sx;
  const float length=std::sqrt(v.normal[0]*v.normal[0]+v.normal[1]*v.normal[1]+v.normal[2]*v.normal[2]);
  if(length>0)for(auto& n:v.normal)n/=length;
 }
 std::memcpy(s.indices,NativeMiiIconBodyIndices,sizeof NativeMiiIconBodyIndices);
 GSPGPU_FlushDataCache(s.body,sizeof NativeMiiIconBody);GSPGPU_FlushDataCache(s.indices,sizeof NativeMiiIconBodyIndices);
 C3D_Mtx projection,view;
 Mtx_Persp(&projection,C3D_AngleFromDegrees(9.8762f),1,500,1000,false);
 // Readback is uploaded through Texture::initRgba8 (top-down RGBA).
 // Do not retain CFL's direct-render-target sampling Y inversion here.
 // CFL's icon depth projection increases with eye distance, matching the
 // native part renderer's LEQUAL test and white depth clear.
 projection.r[2].z=3.5f;projection.r[2].w=1750.f;
 // Retail MiiGroup preset 0 selects camera type 2: eye X/Z = 75.5/107.9
 // (USA 8076af8c / 8089f4e8+3c,+38). Keep that angle in native head units.
 constexpr float radius=600;const float ratio=withBody?75.5f/107.9f:0.f;
 const float z=radius/std::sqrt(1+ratio*ratio);
 const auto eye=FVec3_New(z*ratio,34.5f,z),target=FVec3_New(0,34.5f,0),up=FVec3_New(0,1,0);
 Mtx_LookAt(&view,eye,target,up,false);
 CFL_RebindShader();CFL_BindDefaultShader();
 if(!C3D_FrameBegin(C3D_FRAME_SYNCDRAW))return false;
 C3D_RenderTargetClear(s.target,C3D_CLEAR_ALL,0,0xFFFFFF00);C3D_FrameDrawOn(s.target);C3D_SetViewport(0,0,size,size);
 C3D_DepthMap(true,-1,0);C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
 C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
 for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
 const auto loc=CFL_GetShaderLocations();
 C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,loc.projection,&projection);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,loc.modelView,&view);
 auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,s.body,sizeof(NativeMiiIconVertex),3,0x210);
 CFL_SetDefaultMaterial(CFL_GetFavoriteColor(mii.mii_details.shirt_color),false);
 auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_FRAGMENT_PRIMARY_COLOR,GPU_PREVIOUS,GPU_PREVIOUS);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
 if(withBody){C3D_DepthTest(true,GPU_LEQUAL,GPU_WRITE_ALL);C3D_DrawElements(GPU_TRIANGLES,sizeof NativeMiiIconBodyIndices,C3D_UNSIGNED_BYTE,s.indices);}
 CFL_DrawIconHead(&s.head,expression,nullptr,&projection,&view);
 C3D_FrameEnd(0);C3D_FrameSync();
 C3D_SyncDisplayTransfer(static_cast<u32*>(s.image.data),GX_BUFFER_DIM(size,size),reinterpret_cast<u32*>(s.readback),GX_BUFFER_DIM(size,size),
  GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
 GSPGPU_InvalidateDataCache(s.readback,size*size*4);
 std::vector<std::uint8_t> pixels(size*size*4);
 for(unsigned i=0;i<size*size;++i){
  const auto* in=s.readback+i*4;auto* out=pixels.data()+i*4;const unsigned alpha=in[0];out[3]=alpha;
  // Source-over into transparent target stores premultiplied RGB; menu
  // dynamic textures expect straight RGBA, just like ordinary PNG assets.
  for(unsigned c=0;c<3;++c)out[c]=alpha?std::min(255u,(unsigned(in[3-c])*255+alpha/2)/alpha):0;
 }
 output=std::move(pixels);return true;
}
bool composeNativeMiiPortrait(const NativeMii& identity,unsigned size,std::vector<std::uint8_t>& output){
 return composeNativeMiiImage(identity,size,output,true,CFL_EXPRESSION_NORMAL);
}
bool composeNativeMiiPhotoHead(const NativeMii& identity,unsigned size,bool blink,std::vector<std::uint8_t>& output){
 return composeNativeMiiImage(identity,size,output,false,blink?CFL_EXPRESSION_BLINK:CFL_EXPRESSION_SMILE);
}

}
