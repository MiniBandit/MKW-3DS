#include <mkw/mii_face_mask.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
int main(int argc,char** argv){
 assert(argc==3);std::ifstream file(argv[1],std::ios::binary);assert(file);
 std::array<std::vector<std::uint8_t>,8> images;
 mkw::MiiMaskInput in;
 auto read32=[&](){std::uint8_t b[4];file.read(reinterpret_cast<char*>(b),4);assert(file);return unsigned(b[0])|(unsigned(b[1])<<8)|(unsigned(b[2])<<16)|(unsigned(b[3])<<24);};
 for(unsigned p=0;p<8;++p){auto w=read32(),h=read32();assert(w<=1024&&h<=1024);images[p].resize(w*h*4);file.read(reinterpret_cast<char*>(images[p].data()),images[p].size());assert(file);if(w&&h)in.textures[p]={w,h,images[p].data()};}
 in.face.eyeType=2;in.face.eyeScale=4;in.face.eyeRotation=4;in.face.eyeX=2;in.face.eyeY=12;
 in.face.browType=6;in.face.browScale=4;in.face.browRotation=6;in.face.browX=2;in.face.browY=8;
 in.face.mouthScale=4;in.face.mouthY=13;in.face.mustacheScale=4;in.face.mustacheY=10;
 in.face.moleScale=4;in.face.moleX=10;in.face.moleY=18;in.eyeColor=2;in.browColor=1;in.beardColor=1;
 for(unsigned sample=0;sample<3;++sample){
  if(sample==1){in.face.eyeRotation=2;in.face.browRotation=8;in.face.mouthScale=6;in.eyeColor=4;}
  if(sample==2){in.face.eyeRotation=6;in.face.browRotation=4;in.face.eyeX=5;in.eyeColor=5;}
  std::vector<std::uint8_t> out;assert(mkw::composeMiiFaceMask(in,128,out));assert(out.size()==128*128*4);
  const std::string path=std::string(argv[2])+std::to_string(sample)+".rgba";
  std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<char*>(out.data()),out.size());assert(stream);
 }
 std::vector<std::uint8_t> kept={1,2,3,4};in.eyeColor=6;assert(!mkw::composeMiiFaceMask(in,128,kept)&&kept==std::vector<std::uint8_t>({1,2,3,4}));
 in.eyeColor=2;assert(!mkw::composeMiiFaceMask(in,0,kept));
 in.textures={};assert(mkw::composeMiiFaceMask(in,64,kept));for(auto c:kept)assert(c==0);
 in.textures[0]={1,1,nullptr};assert(!mkw::composeMiiFaceMask(in,64,kept));
 std::puts("3 production masks, empty mask, invalid input and output preservation passed");
}
