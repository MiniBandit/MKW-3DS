"""Production DSKM7/8 loader/evaluator against cooked original head subtrees."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT.parent / 'changes/manual-feedback-20261013'
fixture = ROOT / 'tools/tests/driver_effect_roots.py'
ns = {'__file__': str(fixture)}
exec(fixture.read_text().split("cpp=OUT/")[0], ns)
test = r'''
using namespace mkw;
int main(int argc,char**argv){
 assert(argc>1);
 for(int file=1;file<argc;++file){
  std::ifstream f(argv[file],std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(f)),{});
  assert(data.size()>32);
  uint32_t vertices;std::memcpy(&vertices,data.data()+8,4);
  Model model;model.vertexCount=vertices;
  auto storage=std::make_unique<Vertex[]>(vertices);model.vertices=storage.get();
  auto bank=std::make_unique<DriverAnimation>();assert(bank->load(argv[file],model));
  if(!bank->lookCount_){
   assert(bank->pose(0,0,100));
   std::printf("PASS legacy bank %s\n",argv[file]);continue;
  }
  const int clip=bank->find("drive");assert(clip>=0);
  bank->pose(clip,15,100);
  const auto neutral=bank->palette_;
  bank->braids_.reset();bank->setLookAngles(45,20);bank->pose(clip,15,100);
  const auto looked=bank->palette_;
  unsigned moved=0,unchanged=0;
  for(unsigned slot=0;slot<bank->matrices_;++slot){
   bool descendant=false;
   for(unsigned j=0;j<bank->lookCount_;++j)descendant|=bank->lookSlots_[j]==slot;
   float error=0;
   for(unsigned j=0;j<21;++j)error+=std::fabs(looked[slot*21+j]-neutral[slot*21+j]);
   if(!descendant){assert(error<.001f);++unchanged;}
   else if(error>.001f)++moved;
  }
  assert(moved&&unchanged);
  if(bank->hasBraids_){
   assert(bank->lookCount_>=7&&moved>=7);
   auto expected=neutral;
   applyDriverLookPalette(expected.data(),bank->lookHead_,bank->lookSlots_.data(),bank->lookCount_,45,20);
   for(unsigned chain:{0u,3u})for(unsigned at:{3u,7u,11u}){
    const unsigned i=bank->braidSlots_[chain]*21+at;
    assert(std::fabs(looked[i]-expected[i])<.001f);
   }
  }
  for(unsigned at:{3u,7u,11u})assert(std::fabs(looked[bank->lookHead_*21+at]-neutral[bank->lookHead_*21+at])<.001f);
  bank->braids_.reset();bank->setLookAngles(0,0);bank->pose(clip,15,100);
  for(unsigned j=0;j<bank->matrices_*21;++j)assert(std::fabs(bank->palette_[j]-neutral[j])<.001f);
  // A clip transition must blend the base animation, never yesterday's look
  // delta followed by today's delta (which would double the rotation).
  auto control=std::make_unique<DriverAnimation>();assert(control->load(argv[file],model));
  control->pose(clip,15,100);
  bank->braids_.reset();bank->setLookAngles(45,20);bank->pose(clip,15,100);
  const int other=bank->find("damage");assert(other>=0);
  bank->pose(other,0,101);control->pose(other,0,101);
  auto expected=control->palette_;
  applyDriverLookPalette(expected.data(),control->lookHead_,control->lookSlots_.data(),control->lookCount_,45,20);
  if(!bank->hasBraids_)for(unsigned j=0;j<bank->matrices_*21;++j)assert(std::fabs(bank->palette_[j]-expected[j])<.001f);
  if(bank->bodyLook_[0].count){
   for(unsigned bodyIndex=0;bodyIndex<2;++bodyIndex){
    auto body=std::make_unique<DriverAnimation>();assert(body->load(argv[file],model));
    body->pose(clip,15,100);const auto base=body->palette_;
    DriverLookParameters p;p.headYawScale=0;p.rootYawScale=bodyIndex==0?1:0;p.spineYawScale=bodyIndex==1?1:0;
    body->lookMotion_.step(true,true,40,0,0,0,0,p);body->braids_.reset();body->pose(clip,15,100);
    const auto& subtree=body->bodyLook_[bodyIndex];unsigned changed=0,untouched=0;
    const unsigned joint=subtree.joint*21;
    for(unsigned at:{3u,7u,11u})assert(std::fabs(body->palette_[joint+at]-base[joint+at])<.001f);
    for(unsigned slot=0;slot<body->matrices_;++slot){
     bool belongs=false;for(unsigned n=0;n<subtree.count;++n)belongs|=subtree.slots[n]==slot;
     float error=0;for(unsigned j=0;j<21;++j)error+=std::fabs(body->palette_[slot*21+j]-base[slot*21+j]);
     if(!belongs){assert(error<.001f);++untouched;}else if(error>.001f)++changed;
     // A rigid callback preserves distance to its pivot, including attachments.
     float before=0,after=0;for(unsigned at:{3u,7u,11u}){
      const float x=base[slot*21+at]-base[joint+at],y=body->palette_[slot*21+at]-base[joint+at];before+=x*x;after+=y*y;
     }
     bool dynamicBraid=false;
     if(body->hasBraids_)for(unsigned n:{1u,2u,4u,5u})dynamicBraid|=slot==body->braidSlots_[n];
     // Braid links are subsequently simulated, not rigid descendants. Their
     // two anchor joints must still preserve the body's pivot distance.
     if(!dynamicBraid)assert(std::fabs(before-after)<.1f);
    }
    assert(changed);if(bodyIndex==1)assert(untouched);
    std::printf("BODY %s joint=%u changed=%u untouched=%u\n",argv[file],bodyIndex,changed,untouched);
   }
  }
  if(bank->bodyLook_[0].count){
   auto moving=std::make_unique<DriverAnimation>();assert(moving->load(argv[file],model));
   moving->pose(clip,15,200);const auto base=moving->palette_;
   DriverLookParameters response;response.vehicleResponse={.3f,.2f,.1f,.4f,.3f,.2f,.5f,.4f,.3f,.25f,.2f,.02f,.8f,.9f};
   for(unsigned t=0;t<100;++t)moving->vehicleResponse_.step(0,0,false,1,false,.2f,-.3f,response);
   moving->braids_.reset();moving->pose(clip,15,200);
   float error=0;for(unsigned j=0;j<moving->matrices_*21;++j)error+=std::fabs(moving->palette_[j]-base[j]);
   assert(error>1.f);
   // Re-evaluation must start from CHR0, never accumulate vehicle deltas.
   const auto once=moving->palette_;moving->braids_.reset();moving->pose(clip,15,200);
   for(unsigned j=0;j<moving->matrices_*21;++j)assert(std::fabs(moving->palette_[j]-once[j])<.001f);
   moving->resetLook();moving->braids_.reset();moving->pose(clip,15,200);
   for(unsigned j=0;j<moving->matrices_*21;++j)assert(std::fabs(moving->palette_[j]-base[j])<.001f);
   std::printf("VEHICLE %s: response applied, repeat stable, reset neutral\n",argv[file]);
  }
  if(bank->bodyLook_[0].count){
   auto live=std::make_unique<DriverAnimation>();assert(live->load(argv[file],model));
   DriverController controller;DriverSignals sig;sig.accelerating=true;sig.speed=50;
   C3D_Mtx seated;seated.r[0].x=seated.r[1].y=seated.r[2].z=1;
   DriverLookParameters p;p.vehicleResponse={.3f,.2f,.1f,.4f,.3f,.2f,.5f,.4f,.3f,.25f,.2f,.02f,.8f,.9f};
   for(unsigned t=0;t<40;++t){
    if(t==1){sig.drift=true;sig.driftDirection=1;}
    if(t==2)sig.drift=false;
    const auto motion=controller.step(sig);const int c=live->find(motion.clipName);assert(c>=0);
    live->stepLook(c,motion.frame,t,false,{},seated,true,p,motion,false,.2f,-.3f);
    const auto before=live->vehicleResponse_.angles();
    assert(live->motionBlendValid_&&live->motionBlendWeight_==motion.blendWeight);
    // Stereo/extra queries of the same physics tick cannot advance response.
    live->stepLook(c,motion.frame,t,false,{},seated,true,p,motion,false,.9f,.9f);
    for(unsigned n=0;n<3;++n){assert(live->vehicleResponse_.angles().pitch[n]==before.pitch[n]);assert(live->vehicleResponse_.angles().roll[n]==before.roll[n]);}
    assert(live->pose(c,motion.frame,t));
    if(t>=1&&t<=20)assert(motion.state==2);
   }
   assert(std::fabs(live->vehicleResponse_.angles().pitchSum())>1.f);
   live->resetLook();assert(!live->motionBlendValid_&&live->vehicleResponse_.angles().pitchSum()==0);
   std::printf("LIVE %s: controller/shared blend/physical response/same-tick/reset PASS\n",argv[file]);
  }
  if(bank->hasBraids_){
   auto live=std::make_unique<DriverAnimation>();assert(live->load(argv[file],model));
   DriverPose motion;motion.blendWeight=1;
   DriverLookParameters p;
   C3D_Mtx seated;seated.r[0].x=seated.r[1].y=seated.r[2].z=1;
   for(unsigned t=0;t<12;++t){
    seated.r[0].w=float(t)*120;
    live->stepLook(clip,15,t,false,{},seated,true,p,motion,false,0,0);
    live->setBraidWorld(seated);live->pose(clip,15,t);
    for(unsigned root:{0u,3u})for(unsigned at:{3u,7u,11u}){
     const auto offset=live->braidSlots_[root]*21+at;
     assert(std::fabs(live->palette_[offset]-live->beforeLookPalette_[offset])<.002f);
    }
   }
   std::printf("BRAID moving stepLook then render: anchors attached PASS\n");
  }
  std::printf("PASS %s: %u head matrices, %u unchanged body matrices\n",argv[file],moved,unchanged);
 }
}
'''
cpp = OUT / 'driver-look-palette-production.cpp'
cpp.write_text(ns['boundary'] + '\nsize_t linearSpaceFree(){return 0;}\n' + ns['header'] +
               '\n#include <mkw/driver_look_target.hpp>\nnamespace mkw {\n' + ns['reset'] + ns['find'] + ns['load'] +
               ns['evaluate'] + ns['source'][ns['source'].index('void DriverAnimation::stepLook('):ns['source'].index('const float* DriverAnimation::pose(')] + ns['pose'] + '\n}\n' + test)
exe = OUT / 'driver-look-palette-test'
# This probe requires a separately compiled executable.
if not Path(str(exe)).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(exe)))
subprocess.run([str(exe), *[str(OUT/(name+'-look.dskm')) for name in
                          ('mario', 'peach', 'toadette')],
                str(ROOT/'build/driver-effects/models/peach_mc_kart_driver.dskm')], check=True)
