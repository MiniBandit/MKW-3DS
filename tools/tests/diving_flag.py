"""Exercise the production sea-contact bridge using the original status bit.
No scene/game launch; the existing buoyancy/render/clipping paths are separate.
"""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT.parent/'changes/original-parity-20261012'
s=(ROOT/'vendor/kinoko/source/game/kart/KartMove.cc').read_text();start=s.index('void KartMove::calcRisingWater()');end=s.index('\n/// @addr',start);function=s[start:end]
pre=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cmath>
using f32=float;using u16=uint16_t;
namespace Kinoko {
namespace Field {
struct ObjectDirector {bool hasSea=false;float surface=100;static ObjectDirector object;
 static ObjectDirector* Instance(){return &object;}void* psea(){return hasSea?this:nullptr;}
 float risingWaterKillPlaneHeight(){return surface-260;}float distAboveRisingWater(float y){return y-surface;}
};ObjectDirector ObjectDirector::object;
}
namespace Kart {
enum class eStatus {Diving=53,BoostOffroadInvincibility=39,MovingWaterDecaySpeed=85};
struct Flags {bool dive=true,immune=false,other=true;
 void resetBit(eStatus b){assert(b==eStatus::Diving);dive=false;}void setBit(eStatus b){assert(b==eStatus::Diving);dive=true;}
 bool offBit(eStatus b){assert(b==eStatus::BoostOffroadInvincibility);return !immune;}
};
struct Pos {float y;};struct Collision {unsigned drown=0;void activateOob(bool water,void* p,bool b,bool c){assert(water&&!p&&!b&&!c);++drown;}};
struct Params {struct Stats {float kclSpeed[4]{1,1,1,.6f};}data;const Stats& stats(){return data;}};
struct KartMove {Flags state;Collision collision;Params params;float m_kclSpeedFactor=1;Pos edge[2]{{110},{90}};
 Flags& status(){return state;}Pos wheelPos(u16){return {1000};}Pos wheelEdgePos(u16 i){assert(i<2);return edge[i];}u16 tireCount(){return 2;}
 Collision* collide(){return &collision;}Params* param(){return &params;}void calcRisingWater();
};
'''
body=r'''
}
}
int main(){using namespace Kinoko;Kart::KartMove k;auto& sea=Field::ObjectDirector::object;
 k.calcRisingWater();assert(!k.state.dive&&k.state.other&&k.m_kclSpeedFactor==1); // no sea clears 53 only
 sea.hasSea=true;k.calcRisingWater();assert(k.state.dive&&k.state.other&&std::fabs(k.m_kclSpeedFactor-.96f)<.000001f);
 k.state.immune=true;k.m_kclSpeedFactor=1;k.calcRisingWater();assert(k.state.dive&&k.m_kclSpeedFactor==1); // VFX survives boost immunity
 k.edge[1].y=100;k.calcRisingWater();assert(!k.state.dive); // equality is not below
 k.edge[0].y=100;k.edge[1].y=110;k.calcRisingWater();assert(!k.state.dive);
 k.edge[0].y=-170;k.state.immune=false;k.calcRisingWater();assert(k.state.dive&&k.collision.drown==1&&k.m_kclSpeedFactor==.6f);
 sea.hasSea=false;k.calcRisingWater();assert(!k.state.dive&&k.collision.drown==1);
 puts("PASS production sea-contact status 53, lowest wheel edge, clear/surface boundary, boost immunity, preserved unrelated flags and deep-water OOB");
}
'''
cpp=OUT/'diving-flag-production.cpp';cpp.write_text(pre+function+body)
# This probe requires a separately compiled executable.
if not Path(str(OUT / 'diving-flag-test')).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(OUT / 'diving-flag-test')))
r=subprocess.run([str(OUT/'diving-flag-test')],check=True,capture_output=True,text=True);print(r.stdout.strip());(OUT/'diving-flag-test.log').write_text(r.stdout)
