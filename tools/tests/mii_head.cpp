#include <mkw/mii_head.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <cstring>
int main(int argc,char** argv){
 assert(argc==3);mkw::MiiPartBank bank;
 if(!std::strcmp(argv[1],"--reject")){assert(!bank.open(argv[2]));return 0;}
 assert(bank.open(argv[1]));
 constexpr unsigned groups[]={13,5,3,8,16,0,14,9,6},counts[]={12,72,8,72,72,4,12,8,1};
 unsigned shapes=0,triangles=0;
 for(unsigned g=0;g<9;++g)for(unsigned n=0;n<counts[g];++n){mkw::MiiPartShape p;assert(bank.shape(groups[g],n,p));assert(p.type==g);++shapes;triangles+=p.indices.size()/3;}
 constexpr unsigned tg[]={1,2,11,12,10,4,17,15,7},tc[]={50,24,25,4,2,12,72,12,10};unsigned textures=0;
 for(unsigned g=0;g<9;++g)for(unsigned n=0;n<tc[g];++n){mkw::MiiPartTexture t;assert(bank.texture(tg[g],n,t));++textures;}
 mkw::RflHeadSpec spec;auto& a=spec.appearance;a.face.eyeType=2;a.face.eyeScale=4;a.face.eyeRotation=4;a.face.eyeX=2;a.face.eyeY=12;
 a.face.browType=6;a.face.browScale=4;a.face.browRotation=6;a.face.browX=2;a.face.browY=8;
 a.face.mouthScale=4;a.face.mouthY=13;a.noseType=1;a.noseScale=4;a.noseY=8;a.glassesScale=4;a.glassesY=11;a.hairType=1;
 mkw::MiiHead head;unsigned builds=0;
 for(unsigned face=0;face<8;++face)for(unsigned flip=0;flip<2;++flip){a.faceType=face;a.hairFlip=flip;assert(mkw::buildMiiHead(bank,spec,128,head));assert(head.parts[7].texture.rgba.size()==65536);assert(head.parts[3].mirrored==bool(flip));++builds;}
 a.faceType=0;a.hairFlip=false;assert(mkw::buildMiiHead(bank,spec,128,head));
 std::ofstream obj(argv[2]);unsigned offset=1;
 for(unsigned i=0;i<9;++i){const auto& p=head.parts[i];obj<<"o part_"<<i<<'\n';for(auto& v:p.shape.vertices)obj<<"v "<<v.position[0]/256.f<<' '<<v.position[1]/256.f<<' '<<v.position[2]/256.f<<'\n';for(unsigned j=0;j<p.shape.indices.size();j+=3)obj<<"f "<<offset+p.shape.indices[j]<<' '<<offset+p.shape.indices[j+1]<<' '<<offset+p.shape.indices[j+2]<<'\n';offset+=p.shape.vertices.size();}
 assert(obj);a.hairType=72;assert(!mkw::buildMiiHead(bank,spec,128,head));assert(head.parts[7].texture.rgba.size()==65536);
 mkw::MiiPartShape p;p.type=88;assert(!bank.shape(1,0,p)&&p.type==88);assert(!bank.open("/nonexistent/mii-bank"));assert(bank.shape(3,0,p));
 std::printf("%u shapes, %u triangles, %u textures, %u complete CPU head builds passed; GPU/UI pending\n",shapes,triangles,textures,builds+1);
}
