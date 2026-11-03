"""Production DSKM6 reader + two-chain solver, with host-only renderer boundaries."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT.parent/'changes/toadette-physics-20261012'
# Use the existing production-loader fixture, without running its full bank suite.
fixture=ROOT/'tools/tests/driver_effect_roots.py'
ns={'__file__':str(fixture)}
exec(fixture.read_text().split("cpp=OUT/")[0],ns)
test=r'''
using namespace mkw;
int main(int argc,char**argv){
 assert(argc>=2);
 ToadetteBraids chain;ToadetteBraids::Frames rest;
 for(unsigned s=0;s<2;++s)for(unsigned j=0;j<3;++j)
  rest[s*3+j]={{s?14.f:-14.f,-float(j*13),0},{0,-1,0},{1,0,0},{0,0,1}};
 auto first=chain.step(rest,{0,20,0},0,true);
 for(auto& f:rest)f.p.x+=12;
 auto moved=chain.step(rest,{12,20,0},1,true);
 assert(length(moved[0].p-rest[0].p)<.001f);
 assert(length(moved[3].p-rest[3].p)<.001f);
 assert(length(moved[0].x-rest[0].x)>.01f); // translation induces inertia
 assert(length(moved[1].p-moved[0].p)>12.999f);
 assert(length(moved[1].p-moved[0].p)<13.001f);
 auto duplicate=chain.step(rest,{12,20,0},1,true);
 assert(length(duplicate[2].x-moved[2].x)==0); // stereo must not step twice
 for(unsigned t=2;t<600;++t){
  for(unsigned s=0;s<2;++s)for(unsigned j=0;j<3;++j)rest[s*3+j].p.x=(s?14.f:-14.f)+std::sin(t*.1f)*20;
  const auto& out=chain.step(rest,{std::sin(t*.1f)*20,20,0},t,true);
  for(unsigned i=0;i<6;++i){
   assert(std::isfinite(length(out[i].p)));assert(std::fabs(length(out[i].x)-1)<.001f);
   const float limit[3]={40,45,60};
   assert(dot(out[i].x,rest[i].x)>=std::cos(limit[i%3]*.01745329252f)-.001f);
  }
 }
 chain.reset();const auto& fresh=chain.step(rest,{0,20,0},10000,false);
 assert(length(fresh[0].p-rest[0].p)<.001f);
 for(int file=1;file<argc;++file){
  std::ifstream f(argv[file],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
  assert(bytes.size()>32);uint32_t vertices;std::memcpy(&vertices,bytes.data()+8,4);
  Model model;model.vertexCount=vertices;auto storage=std::make_unique<Vertex[]>(vertices);model.vertices=storage.get();
  auto bank=std::make_unique<DriverAnimation>();assert(bank->load(argv[file],model));assert(bank->hasBraids_);assert(bank->matrixCount()<=19);
  C3D_Mtx world;world.r[0].x=world.r[1].y=world.r[2].z=1;
  for(unsigned t=0;t<180;++t){
   world.r[0].w=std::sin(t*.05f)*70;bank->setBraidWorld(world);
   const float* pose=bank->pose(0,float(t),t);assert(pose);
   for(unsigned i=0;i<bank->matrixCount()*21;++i)assert(std::isfinite(pose[i]));
   Vec3 root;assert(bank->effectRoot(root));
  }
  bank->reset();assert(!bank->hasBraids_);
 }
 puts("PASS braid inertia, roots, lengths, limits, stereo tick, reset and production DSKM6 race/menu palettes");
}
'''
cpp=OUT/'braids-production.cpp'
cpp.write_text(ns['boundary']+ns['header']+'\nnamespace mkw {\n'+''.join(ns[k] for k in ('reset','find','load','evaluate','pose'))+'\n}\n'+test)
exe=OUT/'braids-asan'
# This probe requires a separately compiled executable.
if not Path(str(exe)).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(exe)))
subprocess.run([str(exe),str(OUT/'toadette.dskm'),str(OUT/'menu-toadette.dskm')],check=True)
