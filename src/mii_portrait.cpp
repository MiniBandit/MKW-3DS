#include <mkw/mii_portrait.hpp>
#include <mkw/mii_icon_camera.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace mkw {
namespace {
using Pixel=std::array<double,4>;
Pixel rgb(unsigned v){return {double((v>>16)&255)/255,double((v>>8)&255)/255,double(v&255)/255,1};}
int wrap(int x,int size,unsigned mode){
 if(mode){x%=mode==2?size*2:size;if(x<0)x+=mode==2?size*2:size;return mode==2&&x>=size?2*size-1-x:x;}
 return std::clamp(x,0,size-1);
}
Pixel sample(const MiiPartTexture& t,double u,double v){
 const double x=u*t.width-.5,y=v*t.height-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));
 const double fx=x-ix,fy=y-iy;Pixel out{};
 for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){
  const auto at=(std::size_t(wrap(iy+int(j),t.height,t.wrapT))*t.width+wrap(ix+int(i),t.width,t.wrapS))*4;
  const double w=(i?fx:1-fx)*(j?fy:1-fy);
  for(unsigned c=0;c<4;++c)out[c]+=w*t.rgba[at+c]/255.;
 }
 return out;
}
struct Vertex {double x,y,z,u,v,light;};
}
bool composeMiiPortrait(const MiiHead& head,unsigned size,std::vector<std::uint8_t>& output){
 if(size!=32&&size!=64&&size!=128&&size!=256)return false;
 constexpr unsigned skin[]={0xf0d8c4,0xffbc80,0xd88850,0xffb090,0x985030,0x522e1c};
 constexpr unsigned hair[]={0x1e1a18,0x382015,0x552617,0x704024,0x727278,0x49361a,0x7a5928,0xc19f64};
 constexpr unsigned glasses[]={0x101010,0x603810,0x981810,0x203060,0x905800,0x605850};
 constexpr unsigned cap[]={0xb84030,0xf07828,0xf8d820,0x80c828,0x007428,0x204898,0x40a0d8,0xe86078,0x702ca8,0x483818,0xe0e0e0,0x181814};
 if(head.skinColor>=6||head.hairColor>=8||head.beardColor>=8||head.glassesColor>=6||head.favoriteColor>=12)return false;
 const unsigned n=size*2;
 MiiIconCamera camera;if(!miiIconCamera(size,size,camera))return false;
 const double focal=n/(2*std::tan(double(camera.fovDegrees)*3.14159265358979323846/360));
 std::vector<Pixel> pixels(std::size_t(n)*n);
 std::vector<double> depth(pixels.size(),std::numeric_limits<double>::infinity());
 for(unsigned kind:{5u,0u,1u,3u,4u,2u,7u,6u,8u}){
  const auto& part=head.parts[kind];if(part.shape.indices.empty())continue;
  if(part.shape.indices.size()%3)return false;
  const auto& tex=part.texture;
  if((tex.width||tex.height||!tex.rgba.empty())&&(!tex.width||!tex.height||tex.width>1024||tex.height>1024||tex.rgba.size()!=std::size_t(tex.width)*tex.height*4))return false;
  if(kind==2||kind>=4){if(kind!=5&&(!tex.width||!tex.height||tex.width>1024||tex.height>1024||tex.rgba.size()!=std::size_t(tex.width)*tex.height*4))return false;}
  const Pixel constant=rgb(kind==3?hair[head.hairColor]:kind==5?hair[head.beardColor]:kind==4?cap[head.favoriteColor]:kind==8?glasses[head.glassesColor]:kind==6?0:skin[head.skinColor]);
  std::vector<Vertex> vertices;vertices.reserve(part.shape.vertices.size());
  for(const auto& s:part.shape.vertices){
   std::array<float,3> p{},normal{};for(unsigned c=0;c<3;++c){p[c]=s.position[c]/256.f;normal[c]=s.normal[c]/16384.f;}
   const double z=600.-p[2];if(z<camera.nearPlane||z>camera.farPlane)return false;
   vertices.push_back({n*.5+focal*p[0]/z,n*.5-focal*(p[1]-34.5)/z,z,s.uv[0]/8192.,s.uv[1]/8192.,miiIconVertexLight(p,normal)/255.});
  }
  for(unsigned i=0;i<part.shape.indices.size();i+=3){
   unsigned indices[3]={part.shape.indices[i],part.shape.indices[i+1],part.shape.indices[i+2]};
   if(part.mirrored)std::swap(indices[1],indices[2]);
   for(auto index:indices)if(index>=vertices.size())return false;
   const auto &a=vertices[indices[0]],&b=vertices[indices[1]],&c=vertices[indices[2]];
   const double area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
   if(std::abs(area)<1e-10||(kind!=8&&area<=0))continue;
   int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),x1=std::min(int(n)-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
   int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),y1=std::min(int(n)-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
   for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
    double w1=((x+.5-a.x)*(c.y-a.y)-(y+.5-a.y)*(c.x-a.x))/area;
    double w2=((b.x-a.x)*(y+.5-a.y)-(b.y-a.y)*(x+.5-a.x))/area,w0=1-w1-w2;
    if(std::min({w0,w1,w2})< -1e-9)continue;
    w0/=a.z;w1/=b.z;w2/=c.z;const double den=w0+w1+w2,z=1/den;
    const auto at=std::size_t(y)*n+x;if(z>depth[at]+1e-7)continue;
    w0/=den;w1/=den;w2/=den;
    const double light=w0*a.light+w1*b.light+w2*c.light;
    Pixel t{};if(!tex.rgba.empty())t=sample(tex,w0*a.u+w1*b.u+w2*c.u,w0*a.v+w1*b.v+w2*c.v);
    Pixel color=constant;
    for(unsigned ch=0;ch<3;++ch){
     if(kind==2)color[ch]=constant[ch]*(1-t[1])+t[ch==1?3:ch]*t[1];
     else if(kind==4)color[ch]=constant[ch]*(t[ch]+1)*.5;
     else if(kind==7)color[ch]=t[ch];
     else if(kind==8)color[ch]=constant[ch]*t[ch];
     color[ch]=std::clamp(color[ch]*light,0.,1.);
    }
    const double alpha=kind>=6?t[3]:1.;if(alpha<=0)continue;
    auto& dst=pixels[at];for(unsigned ch=0;ch<3;++ch)dst[ch]=color[ch]*alpha+dst[ch]*(1-alpha);
    dst[3]=alpha+dst[3]*(1-alpha);if(kind<6)depth[at]=z;
   }
  }
 }
 std::vector<std::uint8_t> result(std::size_t(size)*size*4);
 for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
  Pixel p{};for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i)for(unsigned c=0;c<4;++c)p[c]+=pixels[std::size_t(y*2+j)*n+x*2+i][c]*.25;
  if(p[3]>0)for(unsigned c=0;c<3;++c)p[c]/=p[3];
  for(unsigned c=0;c<4;++c)result[(std::size_t(y)*size+x)*4+c]=std::uint8_t(std::clamp(p[c]*255+.5,0.,255.));
 }
 output.swap(result);return true;
}
}
