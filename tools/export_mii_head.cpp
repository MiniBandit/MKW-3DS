// Host-only bridge: reuse the production RFL assembly for offline portraits.
#include <mkw/mii_head.hpp>
#include <mkw/mii_icon_camera.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
int main(int argc,char** argv){
 if(argc!=4){std::cerr<<"parts.mpr appearance.txt output-directory\n";return 2;}
 mkw::RflHeadSpec spec;auto& a=spec.appearance;
 std::map<std::string,unsigned*> fields;
#define FIELD(n) fields[#n]=&a.n
#define FACE(n) fields[#n]=&a.face.n
 FIELD(favoriteColor);FIELD(faceType);FIELD(skinColor);FIELD(hairType);FIELD(hairColor);
 FIELD(eyeColor);FIELD(browColor);FIELD(noseType);FIELD(noseScale);FIELD(noseY);
 FIELD(mouthType);FIELD(mouthColor);FIELD(mustacheType);FIELD(beardType);FIELD(beardColor);
 FIELD(glassesType);FIELD(glassesColor);FIELD(glassesScale);FIELD(glassesY);
 FACE(eyeType);FACE(eyeScale);FACE(eyeRotation);FACE(eyeX);FACE(eyeY);
 FACE(browType);FACE(browScale);FACE(browRotation);FACE(browX);FACE(browY);
 FACE(mouthScale);FACE(mouthY);FACE(mustacheScale);FACE(mustacheY);
 FACE(moleScale);FACE(moleX);FACE(moleY);
#undef FIELD
#undef FACE
 fields["faceFeature"]=&spec.faceFeature;
 std::ifstream input(argv[2]);std::string key;unsigned value;
 if(!input)return 2;
 while(input>>key){
  if(!(input>>value)||value>255)return 2;
  if(key=="hairFlip")a.hairFlip=value!=0;
  else if(key=="moleEnabled")a.moleEnabled=value!=0;
  else if(fields.count(key))*fields[key]=value;
  else {std::cerr<<"Unsupported RFL field: "<<key<<'\n';return 2;}
 }
 if(!input.eof())return 2;
 mkw::MiiPartBank bank;mkw::MiiHead head;
 if(!bank.open(argv[1])||!mkw::buildMiiHead(bank,spec,128,head))return 1;
 const std::filesystem::path root(argv[3]);std::filesystem::create_directories(root);
 std::ofstream out(root/"head.json");out<<std::setprecision(9);
 mkw::MiiIconCamera camera;mkw::miiIconCamera(128,128,camera);
 out<<"{\"fov\":"<<camera.fovDegrees<<",\"colors\":["<<head.skinColor<<','<<head.hairColor<<','<<head.beardColor<<','<<head.glassesColor<<','<<head.favoriteColor<<"],\"parts\":[";
 for(unsigned k=0;k<9;++k){const auto& part=head.parts[k];if(k)out<<',';
  out<<"{\"kind\":"<<k<<",\"mirror\":"<<part.mirrored<<",\"size\":["<<part.texture.width<<','<<part.texture.height<<"],\"wrap\":["<<part.texture.wrapS<<','<<part.texture.wrapT<<"],\"vertices\":[";
  for(unsigned i=0;i<part.shape.vertices.size();++i){const auto& v=part.shape.vertices[i];if(i)out<<',';
   const std::array<float,3> p{v.position[0]/256.f,v.position[1]/256.f,v.position[2]/256.f};
   const std::array<float,3> n{v.normal[0]/16384.f,v.normal[1]/16384.f,v.normal[2]/16384.f};
   out<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<','<<v.uv[0]/8192.f<<','<<v.uv[1]/8192.f<<','<<unsigned(mkw::miiIconVertexLight(p,n))<<','<<n[0]<<','<<n[1]<<','<<n[2]<<']';
  }
  out<<"],\"indices\":[";for(unsigned i=0;i<part.shape.indices.size();++i){if(i)out<<',';out<<part.shape.indices[i];}out<<"]}";
  if(!part.texture.rgba.empty()){std::ofstream tex(root/("part-"+std::to_string(k)+".rgba"),std::ios::binary);tex.write(reinterpret_cast<const char*>(part.texture.rgba.data()),part.texture.rgba.size());if(!tex)return 1;}
 }
 out<<"]}\n";return out?0:1;
}
