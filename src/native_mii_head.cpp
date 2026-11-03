#include <mkw/native_mii_head.hpp>
#include <cfl_mii.h>
#include <mkw/retail_lighting.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mkw {
namespace {
constexpr CFLExpression Expressions[]={CFL_EXPRESSION_NORMAL,CFL_EXPRESSION_BLINK,
 CFL_EXPRESSION_SMILE,CFL_EXPRESSION_ANGER,CFL_EXPRESSION_SORROW,CFL_EXPRESSION_SURPRISE};
struct BuildSession {
 bool initialized=false;CFLCharModel head{};
 ~BuildSession(){C3D_FrameSync();CFL_DeleteModel(&head);if(initialized)CFL_Finalize();}
};
std::uint32_t color(const float* rgb){
 auto c=[](float x){return unsigned(std::lround(std::clamp(x,0.f,1.f)*255));};
 return c(rgb[0])|(c(rgb[1])<<8)|(c(rgb[2])<<16)|0xff000000u;
}
std::shared_ptr<Texture> takeTexture(C3D_Tex& original){
 if(!original.data)return {};
 auto result=std::make_shared<Texture>();result->gpu=original;result->ready=true;
 original={};return result;
}
}
struct NativeMiiHead::Impl {
 Model model;
 struct Part {bool texture=false,alphaOnly=false,cap=false,mask=false,noSpecular=false;};
 std::array<Part,CFL_MAX_PARTS> parts{};
 std::array<std::shared_ptr<Texture>,Count> masks;
 std::uint32_t skin=0;
 struct Context {const Impl* head;unsigned expression;};
};
NativeMiiHead::NativeMiiHead()=default;
NativeMiiHead::~NativeMiiHead()=default;
bool NativeMiiHead::ready()const{return bool(impl_);}
std::uint32_t NativeMiiHead::skinConstant()const{return impl_?impl_->skin:0;}
bool NativeMiiHead::load(const NativeMii& identity,unsigned resolution){
 if(identity[0]!=3||(resolution!=64&&resolution!=128))return false;
 BuildSession session;
 if(!(session.initialized=CFL_Initialize()))return false;
 MiiData mii{};std::memcpy(&mii,identity.data(),sizeof mii);
 CFLExpressionFlag flags=0;for(auto expression:Expressions)flags|=CFL_EXPRESSION_FLAG(expression);
 auto& source=session.head;
 if(!CFL_InitCharModel(&source,&mii,resolution,flags)||source.maskPartIndex<0)return false;
 for(auto expression:Expressions)if(!CFL_IsAvailableExpression(&source,expression))return false;
 auto next=std::make_unique<Impl>();auto& model=next->model;model.nativeMiiLighting=true;
 constexpr float skin[6][3]={{1,.827f,.678f},{1,.714f,.420f},{.870f,.475f,.259f},{1,.667f,.549f},{.678f,.318f,.161f},{.388f,.173f,.094f}};
 next->skin=color(skin[std::min(unsigned(mii.face_style.skinColor),5u)]);
 std::size_t nv=0,ni=0;
 for(int i=0;i<source.partCount;++i){const auto& p=source.parts[i];
  const auto count=p.useIndices?p.indexCount:p.vertexCount;
  if(!p.vbo||count%3||(p.useIndices&&!p.ibo))return false;
  nv+=p.vertexCount;ni+=count;
 }
 if(!nv||nv>65535||!ni)return false;
 model.vertices=static_cast<Vertex*>(linearAlloc(nv*sizeof(Vertex)));
 model.indices=static_cast<std::uint16_t*>(linearAlloc(ni*sizeof(std::uint16_t)));
 if(!model.vertices||!model.indices)return false;
 for(unsigned e=0;e<Count;++e){auto native=Expressions[e];
  next->masks[e]=takeTexture(source.maskTexForExpr[native]);source.maskTexBaked[native]=false;
 }
 unsigned vb=0,ib=0;
 // Preserve CFL's opaque/untextured then textured pass order and original
 // per-part depth writes. Texture ownership is transferred exactly once;
 // the mask part aliases its normal-expression texture in CFL.
 for(unsigned pass=0;pass<2;++pass)for(int i=0;i<source.partCount;++i){auto& p=source.parts[i];
  if(p.hasTexture!=(pass==1))continue;
  const unsigned n=model.submeshes.size();auto& part=next->parts[n];
  part={p.hasTexture,p.isAlphaOnly,p.capBlend,i==source.maskPartIndex,p.noSpecular};
  Submesh sub{};sub.first=ib;sub.count=p.useIndices?p.indexCount:p.vertexCount;
  sub.secondary=-1;sub.texture=-1;sub.constant=color(p.color);
  sub.flags=(1u<<31)|(1u<<23)|4u|2u|(p.depthWrite?(1u<<24):0u);
  // CFL always alpha-blends, independently of per-part depth writes.
  // Use the scene GX diffuse channel, not the legacy fixed brightness grade.
  sub.lighting=retail::Retail|retail::Menu|(1u<<retail::ChannelShift)|(2u<<retail::DiffuseShift);
  sub.ambient=0xffffffffu;
  // Keep the scene renderer's normal lighting and depth convention. Native
  // head construction, colours, mask UVs and cap recipe remain CFL-owned.
  if(p.hasTexture){
   // Reject zero-coverage texels before depth/bloom and expose the same
   // texture mask to the existing shadow silhouette capture.
   sub.flags|=1u|(4u<<26);
   auto texture=part.mask?next->masks[Normal]:takeTexture(p.tex);
   if(!texture)return false;
   auto wrap=[](unsigned gpu){return gpu==GPU_REPEAT?1u:gpu==GPU_MIRRORED_REPEAT?2u:0u;};
   sub.flags|=(wrap((texture->gpu.param>>12)&3)<<8)|(wrap((texture->gpu.param>>8)&3)<<10);
   sub.texture=model.textures.size();model.textures.push_back(std::move(texture));
   if(!part.mask)p.hasTexture=false;
  }
  const auto* vertices=static_cast<const CFLVertex*>(p.vbo);
  for(unsigned j=0;j<p.vertexCount;++j){const auto& in=vertices[j];auto& out=model.vertices[vb+j];out={};
   out.position={in.position[0],in.position[1],in.position[2]};
   out.normal={in.normal[0],in.normal[1],in.normal[2]};
   out.uv[0]=out.uv1[0]=in.texcoord[0];out.uv[1]=out.uv1[1]=in.texcoord[1];
   std::memset(out.color,255,sizeof out.color);
   if(vb+j==0)model.minimum=model.maximum=out.position;
   model.minimum={std::min(model.minimum.x,out.position.x),std::min(model.minimum.y,out.position.y),std::min(model.minimum.z,out.position.z)};
   model.maximum={std::max(model.maximum.x,out.position.x),std::max(model.maximum.y,out.position.y),std::max(model.maximum.z,out.position.z)};
  }
  for(unsigned j=0;j<sub.count;++j){const unsigned index=p.useIndices?static_cast<const unsigned char*>(p.ibo)[j]:j;
   if(index>=p.vertexCount)return false;
   model.indices[ib++]=vb+index;
  }
  vb+=p.vertexCount;model.submeshes.push_back(sub);
 }
 model.vertexCount=vb;model.indexCount=ib;
 GSPGPU_FlushDataCache(model.vertices,vb*sizeof(Vertex));GSPGPU_FlushDataCache(model.indices,ib*2);
 // Only the resident mesh and six masks survive. The resource archive,
 // CFL construction vertices and bake shader are released by session.
 impl_=std::move(next);return true;
}
NativeMiiHead::Expression NativeMiiHead::expression(const DriverPose& pose){
 if(!pose.faceName)return Normal;
 const auto* name=pose.faceName;
 if(!std::strcmp(name,"wink"))return pose.faceFrame>=2&&pose.faceFrame<6?Blink:Normal;
 if(!std::strcmp(name,"damage")||!std::strcmp(name,"bad"))return Sorrow;
 if(!std::strcmp(name,"1st")||!std::strcmp(name,"good")||!std::strcmp(name,"gut"))return Smile;
 if(!std::strcmp(name,"dash")||!std::strncmp(name,"drift_",6))return Anger;
 if(!std::strncmp(name,"sjump",5)||!std::strcmp(name,"jump"))return Surprise;
 return Normal;
}
void NativeMiiHead::bind(void* payload,std::size_t index){
 const auto& context=*static_cast<Impl::Context*>(payload);const auto& head=*context.head;
 const auto& part=head.parts[index];const auto& sub=head.model.submeshes[index];
 for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
 C3D_TexEnvBufUpdate(C3D_Both,0);
 if(part.mask)C3D_TexBind(0,&head.masks[context.expression]->gpu);
 // Match CFL_DrawIconHead: original material, Phong LUT and specular
 // secondary colour, including noSpecular and the cap-add recipe.
 CFL_BindDefaultShader();
 const float rgb[]={float(sub.constant&255)/255.f,float((sub.constant>>8)&255)/255.f,float((sub.constant>>16)&255)/255.f};
 CFL_SetDefaultMaterial(rgb,part.noSpecular);
 auto* env=C3D_GetTexEnv(0);
 if(part.texture&&!part.alphaOnly){
  C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_FRAGMENT_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
 }else{C3D_TexEnvSrc(env,C3D_RGB,GPU_FRAGMENT_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_RGB,GPU_REPLACE);}
 C3D_TexEnvSrc(env,C3D_Alpha,part.texture?GPU_TEXTURE0:GPU_FRAGMENT_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Alpha,GPU_REPLACE);
 auto* shine=C3D_GetTexEnv(1);
 C3D_TexEnvSrc(shine,C3D_RGB,GPU_PREVIOUS,GPU_FRAGMENT_SECONDARY_COLOR);C3D_TexEnvFunc(shine,C3D_RGB,part.noSpecular?GPU_REPLACE:GPU_ADD);
 C3D_TexEnvSrc(shine,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(shine,C3D_Alpha,GPU_REPLACE);
 if(part.cap){auto* cap=C3D_GetTexEnv(2);
  C3D_TexEnvSrc(cap,C3D_RGB,GPU_PREVIOUS,GPU_FRAGMENT_PRIMARY_COLOR);C3D_TexEnvFunc(cap,C3D_RGB,GPU_ADD);
  C3D_TexEnvSrc(cap,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(cap,C3D_Alpha,GPU_REPLACE);
 }
}
const Model* NativeMiiHead::shadowModel()const{return ready()?&impl_->model:nullptr;}
void NativeMiiHead::draw(Renderer& renderer,const C3D_Mtx& transform,Expression expression)const{
 if(!impl_)return;
 Impl::Context context{impl_.get(),unsigned(expression)<Count?unsigned(expression):Normal};
 renderer.draw(impl_->model,transform,false,bind,context);
}
void NativeMiiHead::drawAtScreen(Renderer& renderer,const C3D_Mtx& transform,float x,float y,float sx,float sy,Expression expression)const{
 if(!impl_)return;
 Impl::Context context{impl_.get(),unsigned(expression)<Count?unsigned(expression):Normal};
 renderer.drawAtScreen(impl_->model,transform,x,y,1.f,sx,sy,bind,context);
}
}
