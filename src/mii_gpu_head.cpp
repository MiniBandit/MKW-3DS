#include <mkw/mii_gpu_head.hpp>
#include <mkw/mii_icon_camera.hpp>
#include <algorithm>
#include <cstring>
namespace mkw {namespace {
constexpr std::uint32_t skin[]={0xf0d8c4ff,0xffbc80ff,0xd88850ff,0xffb090ff,0x985030ff,0x522e1cff};
constexpr std::uint32_t hair[]={0x1e1a18ff,0x382015ff,0x552617ff,0x704024ff,0x727278ff,0x49361aff,0x7a5928ff,0xc19f64ff};
constexpr std::uint32_t glasses[]={0x101010ff,0x603810ff,0x981810ff,0x203060ff,0x905800ff,0x605850ff};
constexpr std::uint32_t cap[]={0xb84030ff,0xf07828ff,0xf8d820ff,0x80c828ff,0x007428ff,0x204898ff,0x40a0d8ff,0xe86078ff,0x702ca8ff,0x483818ff,0xe0e0e0ff,0x181814ff};
std::uint32_t abgr(std::uint32_t x){return (x>>24)|((x>>8)&0xff00)|((x<<8)&0xff0000)|((x&255)<<24);}
struct Context {const Model* model;const unsigned* kinds;};
}
bool MiiGpuHead::load(const MiiHead& head,std::uint32_t lighting,std::uint32_t ambient){
 if(head.skinColor>=6||head.hairColor>=8||head.beardColor>=8||head.glassesColor>=6||head.favoriteColor>=12)return false;
 auto model=std::make_unique<Model>();std::size_t nv=0,ni=0;
 for(const auto& p:head.parts){if(p.shape.indices.size()%3)return false;nv+=p.shape.vertices.size();ni+=p.shape.indices.size();}
 if(!nv||nv>65535||!ni||ni>65535)return false;
 model->vertices=static_cast<Vertex*>(linearAlloc(nv*sizeof(Vertex)));model->indices=static_cast<std::uint16_t*>(linearAlloc(ni*2));
 if(!model->vertices||!model->indices)return false;
 model->vertexCount=nv;model->indexCount=ni;unsigned vb=0,ib=0;std::array<unsigned,9> kinds{};
 // RFL opaque submission then transparent submission, not resource-file order.
 constexpr unsigned order[]={5,0,1,3,4,2,7,6,8};
 for(unsigned kind:order){const auto& p=head.parts[kind];if(p.shape.indices.empty())continue;
  Submesh sub{};sub.first=ib;sub.count=p.shape.indices.size();sub.texture=sub.secondary=-1;
  sub.flags=(1u<<31)|(1u<<23);if(kind<6)sub.flags|=1u<<24;else sub.flags|=2;
  if(kind==8)sub.flags|=4;
  sub.lighting=lighting;sub.ambient=ambient;
  std::uint32_t color=kind==3?hair[head.hairColor]:kind==5?hair[head.beardColor]:kind==4?cap[head.favoriteColor]:kind==8?glasses[head.glassesColor]:kind==6?0x000000ff:skin[head.skinColor];
  sub.constant=abgr(color);
  if(!p.texture.rgba.empty()){
   auto tex=std::make_shared<Texture>();std::vector<std::uint8_t> rgba=p.texture.rgba;
   if(kind==2)for(std::size_t j=0;j<rgba.size();j+=4)std::swap(rgba[j+1],rgba[j+3]); // RFL RGBA -> RABG swap table.
   if(!tex->initRgba8(p.texture.width,p.texture.height,rgba.data(),rgba.size()))return false;
   sub.texture=model->textures.size();model->textures.push_back(std::move(tex));
   sub.flags|=(p.texture.wrapS<<8)|(p.texture.wrapT<<10);
  }
  for(unsigned j=0;j<p.shape.vertices.size();++j){const auto& src=p.shape.vertices[j];auto& v=model->vertices[vb+j];v={};
   v.position={src.position[0]/256.f,src.position[1]/256.f,src.position[2]/256.f};v.normal={src.normal[0]/16384.f,src.normal[1]/16384.f,src.normal[2]/16384.f};
   v.uv[0]=v.uv1[0]=src.uv[0]/8192.f;v.uv[1]=v.uv1[1]=1.f-src.uv[1]/8192.f;std::memset(v.color,255,4);
   if(!vb&&!j)model->minimum=model->maximum=v.position;
   model->minimum={std::min(model->minimum.x,v.position.x),std::min(model->minimum.y,v.position.y),std::min(model->minimum.z,v.position.z)};
   model->maximum={std::max(model->maximum.x,v.position.x),std::max(model->maximum.y,v.position.y),std::max(model->maximum.z,v.position.z)};
  }
  for(unsigned j=0;j<p.shape.indices.size();j+=3){for(unsigned k=0;k<3;++k){unsigned at=j+(p.mirrored&&k?3-k:k);if(p.shape.indices[at]>=p.shape.vertices.size())return false;model->indices[ib++]=vb+p.shape.indices[at];}}
  kinds[model->submeshes.size()]=kind;model->submeshes.push_back(sub);vb+=p.shape.vertices.size();
 }
 model->vertexCount=vb;model->indexCount=ib;GSPGPU_FlushDataCache(model->vertices,vb*sizeof(Vertex));GSPGPU_FlushDataCache(model->indices,ib*2);
 model->bakeStaticLighting();
 model_=std::move(model);kinds_=kinds;return true;
}
std::uint32_t MiiGpuHead::skinConstant() const{
 if(model_)for(unsigned i=0;i<model_->submeshes.size();++i)if(kinds_[i]==2)return model_->submeshes[i].constant;
 return 0;
}
bool MiiGpuHead::loadIcon(const MiiHead& head){
 if(!load(head,0,0))return false;
 for(unsigned i=0;i<model_->vertexCount;++i){auto& v=model_->vertices[i];
  const auto light=miiIconVertexLight({v.position.x,v.position.y,v.position.z},{v.normal.x,v.normal.y,v.normal.z});
  v.color[0]=v.color[1]=v.color[2]=light;v.color[3]=255;
 }
 for(auto& sub:model_->submeshes)sub.flags|=8; // Preserve baked vertex light.
 GSPGPU_FlushDataCache(model_->vertices,model_->vertexCount*sizeof(Vertex));return true;
}
void MiiGpuHead::bind(void* data,std::size_t index){
 const auto& context=*static_cast<Context*>(data);const unsigned kind=context.kinds[index];const auto& sub=context.model->submeshes[index];
 C3D_TexEnvBufUpdate(C3D_Both,0);for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
 auto* e=C3D_GetTexEnv(0);C3D_TexEnvColor(e,sub.constant);
 if(kind==2){
  C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_TEXTURE0);C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
 }else if(kind==4){
  // Every retail cap palette component is even: C/2 is exact in RGBA8.
  C3D_TexEnvColor(e,((sub.constant&0xfefefe)>>1)|0xff000000u);
  C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);
 }else if(kind==7){C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
 }else if(kind==8){C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
 }else{C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);}
 C3D_TexEnvSrc(e,C3D_Alpha,kind>=6?GPU_TEXTURE0:GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
 auto* light=C3D_GetTexEnv(1);C3D_TexEnvSrc(light,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(light,C3D_RGB,GPU_MODULATE);
}
void MiiGpuHead::draw(Renderer& renderer,const C3D_Mtx& transform) const{
 if(model_){Context context{model_.get(),kinds_.data()};renderer.draw(*model_,transform,false,bind,context);}
}
void MiiGpuHead::drawAtScreen(Renderer& renderer,const C3D_Mtx& transform,float x,float y,float scaleX,float scaleY) const{
 if(model_){Context context{model_.get(),kinds_.data()};renderer.drawAtScreen(*model_,transform,x,y,1.f,scaleX,scaleY,bind,context);}
}

}
