// Generated against packaged HW81 meshes and original DrawOpa materials.
#pragma once
#include <cstring>
namespace mkw::coursePhase {
struct Range {const char* name;unsigned vertices,indices,first,count;};
inline constexpr Range Ranges[]={
{"course_00_000.gmdl",24208,42741,22785,444},
{"course_01_000.gmdl",25364,54636,37602,2517},
{"course_01_000.gmdl",25364,54636,47259,5856},
{"course_02_000.gmdl",17478,35856,21780,171},
{"course_02_000.gmdl",17478,35856,29991,1653},
{"course_03_000.gmdl",22148,41022,11724,30},
{"course_03_000.gmdl",22148,41022,11754,36},
{"course_03_000.gmdl",22148,41022,24168,3138},
{"course_03_000.gmdl",22148,41022,27306,1746},
{"course_03_000.gmdl",22148,41022,34068,438},
{"course_03_000.gmdl",22148,41022,34506,126},
{"course_03_000.gmdl",22148,41022,34632,2598},
{"course_03_000.gmdl",22148,41022,37230,1194},
{"course_03_000.gmdl",22148,41022,38424,597},
{"course_04_000.gmdl",29854,56136,54747,111},
{"course_04_000.gmdl",29854,56136,54858,1002},
{"course_05_000.gmdl",35270,58551,55596,252},
{"course_12_000.gmdl",38077,71049,70401,648},
{"course_17_000.gmdl",16358,38742,30228,255},
{"course_17_000.gmdl",16358,38742,35469,48},
{"course_17_000.gmdl",16358,38742,35517,12},
{"course_17_000.gmdl",16358,38742,35529,264},
{"course_17_000.gmdl",16358,38742,38586,156},
{"course_20_000.gmdl",13742,32361,31125,234},
{"course_22_000.gmdl",20084,40728,33207,5091},
{"course_23_000.gmdl",25295,47652,46983,270},
{"course_29_000.gmdl",16877,35667,25299,822},
{"course_29_000.gmdl",16877,35667,26121,327},
{"course_29_000.gmdl",16877,35667,26448,1551},
{"course_29_000.gmdl",16877,35667,35379,240},
{"course_31_000.gmdl",8039,13392,3489,2943},
{"course_31_000.gmdl",8039,13392,6432,912},
{"course_31_000.gmdl",8039,13392,7344,816},
{"course_31_000.gmdl",8039,13392,10488,192},
{"course_33_000.gmdl",4594,8190,6486,1344},
};
template<class Model> void apply(Model& model,const char* path){
const char* name=std::strrchr(path,'/');name=name?name+1:path;
for(const auto& r:Ranges)if(model.vertexCount==r.vertices&&model.indexCount==r.indices&&!std::strcmp(name,r.name))
for(auto& s:model.submeshes)if(s.first==r.first&&s.count==r.count)s.lighting|=1u<<26;
}
}
