"""Exercise production DSKM loader/evaluator with host allocation/model boundaries."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT.parent/'changes/driver-attachments-20261012'
header=(ROOT/'include/mkw/animation.hpp').read_text().replace('#include "mkw/renderer.hpp"','').replace('private:','public:')
source=(ROOT/'src/animation.cpp').read_text()
reset=source[source.index('void DriverAnimation::reset()'):source.index('bool DriverAnimation::gpuSkin(')]
load=source[source.index('bool DriverAnimation::load('):source.index('namespace {')]
evaluate=source[source.index('namespace {'):source.index('bool DriverAnimation::apply(')]
pose=source[source.index('const float* DriverAnimation::pose('):source.index('bool DriverAnimation::makeRigid(')]
find=source[source.index('int DriverAnimation::find('):source.index('bool DriverAnimation::load(')]
boundary=r'''
#include <mkw/math.hpp>
#include <mkw/pose_blend.hpp>
#include <mkw/driver_effect_anchor.hpp>
#include <mkw/kart_item_effects.hpp>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <iterator>
void* linearAlloc(size_t n){return std::malloc(n);}void linearFree(void*p){std::free(p);}
struct C3D_Mtx {struct Row {float x=0,y=0,z=0,w=0;} r[4];};
namespace mkw {
struct Vertex{Vec3 position{},normal{};float uv[2]{};};struct RigidPart{};
struct Model{unsigned vertexCount=0;Vertex*vertices=nullptr;struct Sub{int texture=0;};std::vector<Sub>submeshes;};
struct Renderer{static constexpr unsigned MaxSkinBones=24;};
struct {unsigned poseEvaluations=0;}renderMetrics;
namespace pack{long size(const char*){return -1;}bool readInto(const char*,void*,size_t){return false;}}
}
'''
test=r'''
using namespace mkw;
static void near(float a,float b){assert(std::fabs(a-b)<.0001f);}
int main(int argc,char**argv){
 assert(argc>1);unsigned cases=0;
 for(int file=1;file<argc;++file){
  std::ifstream f(argv[file],std::ios::binary);std::vector<char>data((std::istreambuf_iterator<char>(f)),{});assert(data.size()>32);
  uint32_t vertices;std::memcpy(&vertices,data.data()+8,4);Model model;model.vertexCount=vertices;
  auto storage=std::make_unique<Vertex[]>(vertices);model.vertices=storage.get();
  auto bank=std::make_unique<DriverAnimation>();assert(bank->load(argv[file],model));
  for(unsigned i=0;i<bank->count();++i)for(float frame:{0.f,.5f,float(bank->duration(i)-1),bank->duration(i)+.5f}){
   bank->clearPoseTransition();assert(bank->pose(i,frame,100));Vec3 root;assert(bank->effectRoot(root));
   const auto&c=bank->clips_[i];const float clamped=c.loop?std::fmod(frame,float(c.count)):std::clamp(frame,0.f,float(c.count-1));
   const unsigned a=unsigned(clamped),b=a+1<c.count?a+1:(c.loop?0:a);const float t=clamped-a;
   const unsigned stride=bank->matrices_*21+bank->shapes_;
   const auto*rows=reinterpret_cast<const float*>(c.data);float expected[3];
   for(unsigned axis=0;axis<3;++axis){
    const float*x;unsigned at;
    if(bank->effectRootSlot_>=0){x=rows;at=unsigned(bank->effectRootSlot_)*21+axis*4+3;expected[axis]=x[a*stride+at]+(x[b*stride+at]-x[a*stride+at])*t;}
    else{x=bank->effectRootTracks_[i];expected[axis]=x[a*3+axis]+(x[b*3+axis]-x[a*3+axis])*t;}
   }
   near(root.x,expected[0]);near(root.y,expected[1]);near(root.z,expected[2]);++cases;
  }
  // A root-only track must share the rendered palette's six-tick blend.
  bank->clearPoseTransition();bank->pose(0,0,100);Vec3 previous;bank->effectRoot(previous);
  bank->pose(1,0,100);Vec3 transitioned;bank->effectRoot(transitioned);
  bank->clearPoseTransition();bank->pose(1,0,200);Vec3 current;bank->effectRoot(current);
  near(transitioned.x,previous.x+(current.x-previous.x)/6);near(transitioned.y,previous.y+(current.y-previous.y)/6);near(transitioned.z,previous.z+(current.z-previous.z)/6);
  bank->reset();Vec3 out;assert(!bank->effectRoot(out));
 }
 DriverEffectAnchor anchor;Vec3 out;assert(!anchor.predict({},{},out));
 anchor.store({15,40,50},{10,20,30});assert(anchor.predict({100,200,300},{1,2,3},out));
 near(out.x,106);near(out.y,222);near(out.z,323);anchor.reset();assert(!anchor.predict({},{},out));
 puts("PASS production DSKM5 loader/pose/root/blend/reset and original cached-position delta + velocity anchor");
 printf("root samples=%u banks=%d\n",cases,argc-1);
}
'''
cpp=OUT/'driver-effect-roots-production.cpp'
cpp.write_text(boundary+header+'\nnamespace mkw {\n'+reset+find+load+evaluate+pose+'\n}\n'+test)
exe=OUT/'driver-effect-roots-asan'
# This probe requires a separately compiled executable.
if not Path(str(exe)).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(exe)))
files=sorted((ROOT/'build/driver-effects/models').glob('*.dskm'))
assert len(files)==288
subprocess.run([str(exe),*[str(p) for p in files]],check=True)
