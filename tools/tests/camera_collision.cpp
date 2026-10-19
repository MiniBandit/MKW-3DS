#include <mkw/race_camera.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace mkw;
static bool near(Vec3 a,Vec3 b){return length(a-b)<.001f;}
struct Query{int calls=0;bool hit=true,wall=true;Vec3 eye,previous,push{0,0,-400};};
static bool query(void* ctx,float radius,Vec3 eye,Vec3 previous,Vec3& push,bool& wall){
 auto& q=*static_cast<Query*>(ctx);assert(radius==65);++q.calls;q.eye=eye;q.previous=previous;push=q.push;wall=q.wall;return q.hit;
}
int main(){
 RaceCameraCourse course;course.goalCamera=0;course.cameras.resize(1);course.cameras[0].viewStart={0,0,600};
 RaceCameraInput in{};in.stage=2;in.finished=true;in.kartForward={0,0,1};in.chaseEye={10,100,700};
 Query q;in.collideEye=query;in.collisionContext=&q;
 RaceCameraDirector c;c.setCourse(&course);c.reset({}, {}, {},in);
 c.step(in);assert(q.calls==1);assert(near(q.previous,in.chaseEye));
 assert(near(c.view().eye,q.eye+q.push));auto corrected=c.view().eye;
 // Swept query must use the corrected previous eye, not the unconstrained one.
 q.hit=false;c.step(in);assert(near(q.previous,corrected));assert(near(c.view().eye,q.eye));
 // A close wall lifted pitch by 0.5 then decayed by 0.97; distance clamps to 300.
 assert(std::fabs(length(c.view().eye-c.view().target)-300)<.001f);
 assert(c.view().eye.y>c.view().target.y);
 // Unobstructed distance recovers to the 600-unit target.
 for(int i=0;i<160;++i)c.step(in);
 assert(length(c.view().eye-c.view().target)>599);
 // No collisions in live chase or preview; no new simulation-dependent API needed.
 in.finished=false;int calls=q.calls;c.step(in);assert(q.calls==calls);
 in.previewActive=true;in.finished=true;c.step(in);assert(q.calls==calls);
 // Non-wall collision pushes out, but does not lift pitch.
 RaceCameraDirector floor;floor.setCourse(&course);in.previewActive=false;q.hit=true;q.wall=false;
 floor.reset({}, {}, {},in);floor.step(in);q.hit=false;floor.step(in);
 assert(std::fabs(floor.view().eye.y-floor.view().target.y)<.001f);
 // Fixed AREA cameras retain the original exclusion; type 6 uses the query.
 course.goalCamera=-1;course.areaCamera={0};
 in.findArea=[](void*,int,Vec3){return 0;};
 for(int type:{1,6,9}){course.cameras[0].type=type;RaceCameraDirector area;area.setCourse(&course);area.reset({}, {}, {},in);calls=q.calls;area.step(in);assert(q.calls==calls+(type==6));}
 puts("PASS auto-camera push, corrected sweep history, wall-only pitch, recovery and mode gates");
}
