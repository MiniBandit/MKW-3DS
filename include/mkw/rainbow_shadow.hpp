#pragma once
#include <cstring>
namespace mkw::rainbowShadow {
// Original z2_road: DrawOpa, SRC_ALPHA, depth write; vertex alpha is the road fade.
inline constexpr unsigned Flag=1u<<27;
template<class Model> void apply(Model& m,const char* path){
const char* n=std::strrchr(path,'/');n=n?n+1:path;
if(!std::strcmp(n,"course_13_000.gmdl")&&m.vertexCount==26228&&m.indexCount==58944)for(auto& s:m.submeshes)if(s.first==44988&&s.count==4044)s.lighting|=Flag;
}
}
