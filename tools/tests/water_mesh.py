import json,struct,subprocess
from pathlib import Path
root=Path.cwd();out=root.parent/'changes/manual-feedback-20261013'
x=json.loads((out/'factory-water-candidate/model.json').read_text());s=next(s for m in x['models'] for s in m['shapes'] if s['material']=='ef_sea')
(out/'water-geometry.bin').write_bytes(b''.join(struct.pack('<10f4B',*v) for v in s['vertices']))
source=(root/'src/renderer.cpp').read_text();a=source.index('            auto project=[&](const Vertex& v)');b=source.index('            const uint32_t count=mesh.size()',a);body=source[a:b]
code='''#include "mkw/math.hpp"
#include "mkw/water_indirect.hpp"
#include "mkw/water_clip.hpp"
#include "mkw/water_mesh_index.hpp"
#include <cstring>
#include <vector>
#include <cstdio>
#include <cassert>
#include <chrono>
using namespace mkw;
struct Vertex {Vec3 position,normal;float uv[2],uv1[2];uint8_t color[4];};
struct Matrix {struct Row{float x,y,z,w;}r[4];};
struct Range {unsigned first,count;};
struct Ranges {unsigned count=1;Range ranges[1];};
struct Model {std::vector<uint16_t> indices;};
std::vector<Vertex> run(const Vertex* vertices,const Model& model,const WaterIndirectField& field,Matrix clip,unsigned animationFrame){
float horizontalSign_=-1;Ranges ranges{1,{{0,unsigned(model.indices.size())}}};
'''+body+'''return mesh;}
int main(int argc,char**argv){
assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);const size_t n=ftell(f)/sizeof(Vertex);rewind(f);std::vector<Vertex> vertices(n);assert(fread(vertices.data(),sizeof(Vertex),n,f)==n);fclose(f);
Model model;for(unsigned i=0;i<n;++i)model.indices.push_back(i);
struct Reader{FILE*f;bool read(void*p,size_t n){return fread(p,1,n,f)==n;}} reader{fopen(argv[2],"rb")};assert(reader.f);uint32_t h[2];assert(reader.read(h,8));WaterIndirectField field;assert(field.read(reader));fclose(reader.f);
unsigned largest=0;size_t largestIndexed=0;double worst=0;
for(unsigned i=0;i<24;++i){
const auto centre=(vertices[(i*9)%n].position+vertices[(i*9+1)%n].position+vertices[(i*9+2)%n].position)/3.f;
const float angle=float(i)*.7f;const Vec3 forward=normalized(Vec3{sin(angle),-.35f,cos(angle)}),right=normalized(cross(forward,{0,1,0})),up=cross(right,forward),eye=centre-forward*900.f+Vec3{0,200,0};
Matrix clip{};auto row=[&](unsigned i,Vec3 v,float constant){clip.r[i]={v.x,v.y,v.z,constant-dot(v,eye)};};
const float fy=1.f/tan(.5f*55.f*3.14159265f/180.f),fx=fy/(400.f/240.f);row(0,up*fy,0);row(1,right*(-fx),0);row(3,forward,0);row(2,forward*(-400000.f/399990.f),10.f*400000.f/399990.f);
auto start=std::chrono::steady_clock::now();auto mesh=run(vertices.data(),model,field,clip,i*17);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();largest=std::max(largest,unsigned(mesh.size()));worst=std::max(worst,ms);
const auto original=mesh;auto indices=indexWaterMesh(mesh);assert(indices.size()==original.size());
for(size_t k=0;k<indices.size();++k)assert(std::memcmp(&mesh[indices[k]],&original[k],sizeof(Vertex))==0);
const size_t bytes=mesh.size()*sizeof(Vertex)+indices.size()*sizeof(uint16_t);largestIndexed=std::max(largestIndexed,bytes);
printf("indexed=%zu indices=%zu bytes=%zu ",mesh.size(),indices.size(),bytes);
for(auto v:mesh)assert(std::isfinite(v.uv1[0])&&std::isfinite(v.uv1[1]));printf("view=%u vertices=%zu host_ms=%.3f\\n",i,mesh.size(),ms);
}printf("MAX indexed_GPU_bytes=%zu\\n",largestIndexed);printf("MAX vertices=%u GPU_bytes=%zu host_ms=%.3f (not ARM timing)\\n",largest,size_t(largest)*sizeof(Vertex),worst);}
'''
p=out/'water-mesh-probe.cpp';p.write_text(code)
# This probe requires a separately compiled executable.
if not Path(str(out / 'water-mesh-probe')).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(out / 'water-mesh-probe')))
result=subprocess.run([str(out/'water-mesh-probe'),str(out/'water-geometry.bin'),str(out/'factory-water-cooked/models/course_factory_water_000.wind-test')],check=True,capture_output=True,text=True)
(out/'water-mesh-probe.log').write_text(result.stdout);print(result.stdout)
