#include "mkw/settings_preview.hpp"
#include "mkw/menu_transition.hpp"
#include "mkw/menu_lighting.hpp"
#include "mkw/renderer.hpp"
#include "mkw/driver_controller.hpp"
#include "mkw/race_teams.hpp"
#include "mkw/vehicle_animation.hpp"
#include "mkw/vehicle_lod_layout.hpp"
#include "mkw/vehicle_visual_assets.hpp"
#include <mkw/course_bloom.hpp>
#include <mkw/course_lighting.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mkw {
namespace {
// Luigi Circuit's start straight (1024x128, ETC1, 5 mips): one of the course
// textures of 256x256 or more that Texture Quality: Low loads from mip 1.
constexpr const char* RoadTexture="textures/course_08_023.t3x";
// Both versions of the road, loaded the way the race loads them.
struct RoadJob final:AssetJob {
 std::shared_ptr<Texture> high,standard;
 RoadJob(){label="settings road";}
 void run()override{
  auto full=std::make_shared<Texture>(),half=std::make_shared<Texture>();
  if(cancelled()||!full->load(RoadTexture,0))return;
  if(cancelled()||!half->load(RoadTexture,1))return;
  high=std::move(full);standard=std::move(half);
 }
};
// One racer as the race loads an opponent: the full models and banks
// (VehicleVisualAssets) plus the split-screen bodies Opponent Detail:
// Reduced switches to at mid distance (RacerVisuals::load).
struct Racer {
 const GarageChoice* choice=nullptr;
 VehicleVisualAssets assets;
 std::unique_ptr<Model> driverLod,bodyLod,handleLod;
};
struct RacerJob final:AssetJob {
 unsigned garage;std::unique_ptr<Racer> racer;
 explicit RacerJob(unsigned g):garage(g){label="settings racer";}
 void run()override{
  Model::setLightingCourse(&MenuLightings[0]);
  const auto& choice=GarageChoices[garage];
  auto r=std::make_unique<Racer>();r->choice=&choice;
  if(cancelled()||!r->assets.load(choice.character,choice.vehicle,RaceTeam::None))return;
  auto low=[&](const char* part,const char* suffix){
   char name[160];std::snprintf(name,sizeof name,"models/%s%s%s.gmdl",choice.prefix,suffix,part);
   auto model=std::make_unique<Model>();if(cancelled()||!model->load(name))return std::unique_ptr<Model>{};
   model->freezeLighting();model->enableStaticCulling();return model;
  };
  const char* suffix=teamVehicleSuffix(choice.vehicle,RaceTeam::None);
  r->driverLod=low("driver_lod","");r->bodyLod=low("body_lod",suffix);
  if(VehicleLodLayout[choice.vehicle].handle)r->handleLod=low("handle_lod",suffix);
  auto& a=r->assets;
  auto freeze=[](auto& model,auto& bank){if(!model)return;
   model->freezeLighting(bank&&bank->gpuSkinned()?bank->palette():nullptr);
   if(bank&&bank->gpuSkinned()){bank->shareDrawVertices(*model);model->enableSkinCulling(bank->matrixCount());}
   else if(!bank)model->enableStaticCulling();
  };
  freeze(a.driver,a.animation);freeze(a.body,a.bodyAnimation);
  for(unsigned i=0;i<a.extras.size();++i)freeze(a.extras[i],a.extraAnimation[i]);
  for(auto& wheel:a.wheels)if(wheel){wheel->freezeLighting();wheel->enableStaticCulling();}
  if(!cancelled())racer=std::move(r);
 }
};
// The license's driver and vehicle; Miis and unset choices show Mario on
// the Standard Kart M.
unsigned garageFor(const MenuState& state){
 const auto& s=state.selection;
 return s.driver<24&&s.vehicle<VehiclesPerDriver?s.driver*VehiclesPerDriver+s.vehicle:0u;
}
// The sample's row and values. Custom's rows show their own option; the
// preset rows show the racer (the Bloom row's sample) with the highlighted
// preset's values, Custom's row with the current ones.
struct Sample {unsigned row;UiSettings o;bool preset;};
Sample sampleFor(const MenuState& state){
 // Custom's rows: Texture, Bloom, Opponents, 800px, Minimap, CRT.
 static constexpr unsigned Rows[]={0,2,3,4,5,6};
 if(state.settingsCustom)return {Rows[std::min(state.cursor,5u)],state.settings,false};
 return {2,state.cursor<SettingsPresetCount?settingsPreset(state.cursor):state.settings,true};
}
using Quad=Renderer::MaterialQuad;
constexpr float X=SettingsPreview::X,Y=SettingsPreview::Y,W=SettingsPreview::Width,H=SettingsPreview::Height;
Quad rect(float x,float y,float w,float h,std::uint32_t top,std::uint32_t bottom){
 Quad q{};
 const float xs[4]={x,x+w,x,x+w},ys[4]={y,y,y+h,y+h};
 for(unsigned k=0;k<4;++k){q.x[k]=xs[k];q.y[k]=ys[k];q.color[k]=k<2?top:bottom;}
 return q;
}
// The window: a soft shadow, a silver bevel and a dark inner edge.
void drawFrame(Renderer& renderer){
 const Quad quads[]={
  rect(X-3,Y-1,W+8,H+8,0x00000018u,0x00000030u),
  rect(X-4,Y-4,W+8,H+8,0xf4f4fcffu,0xa4a4c4ffu),
  rect(X-1,Y-1,W+2,H+2,0x40405cffu,0x40405cffu),
  rect(X,Y,W,H,0x202028ffu,0x202028ffu)};
 renderer.drawScreenMaterialQuads(nullptr,quads,std::size(quads),0,0xffffffffu);
}
// The road seen from a kart's height: a plane in perspective, drawn as
// strips with the depth at each strip edge (1/z is linear in screen y). At
// the bottom edge High shows a texel as about 2.3 pixels, Standard as 4.6;
// the start line's chequers run up the right of the window.
void drawRoad(Renderer& renderer,const Texture& road,unsigned age,float zoom=1.f){
 constexpr unsigned Strips=16;
 constexpr float Far=2.5f,Centre=.2f,Length=1.4f;
 const float halfWidth=.06f/zoom;
 // Rolling forward slowly: half a texel of the full texture per frame.
 const float scroll=std::fmod(float(age)*(.5f/128.f),1.f);
 Quad quads[Strips];
 auto depth=[](float s){return 1.f/(1.f-s*(1.f-1.f/Far));};
 for(unsigned i=0;i<Strips;++i){
  const float s0=float(i)/Strips,s1=float(i+1)/Strips,z0=depth(s0),z1=depth(s1);
  const float yBottom=Y+H-s0*H,yTop=Y+H-s1*H;
  auto& q=quads[i];
  // TL, TR, BL, BR.
  const float xs[4]={X,X+W,X,X+W},ys[4]={yTop,yTop,yBottom,yBottom},zs[4]={z1,z1,z0,z0};
  for(unsigned k=0;k<4;++k){
   q.x[k]=xs[k];q.y[k]=ys[k];
   q.u[k]=Centre+(k%2?1.f:-1.f)*halfWidth*zs[k];
   q.v[k]=-(Length/zoom*(zs[k]-1.f)+scroll);
   // Distance haze toward the top, as on the course.
   const float fog=std::clamp((zs[k]-1.f)/(Far-1.f),0.f,1.f)*.25f;
   const auto c=std::uint32_t(std::lround(255.f*(1.f-fog)));
   q.color[k]=c<<24|c<<16|c<<8|0xffu;
  }
 }
 renderer.drawScreenMaterialQuads(&road,quads,Strips,0,0xffffffffu,GPU_REPEAT,GPU_REPEAT);
}
// The racer on the menu camera (MenuModels::begin: eye Z1000 looking at the
// origin, 45 degree field of view), close enough to fill most of the window,
// turning slowly and pitched toward the camera.
constexpr float EyeDistance=1000.f,FovDegrees=45.f;
constexpr float RacerZ=520.f,RacerY=-31.f,RacerPitch=.38f;
// The 3D samples' world: the road is the plane y=0 and the chassis stands at
// (0,-lift,0), lift being its tires' lowest point (initialSnapshot), so the
// shadow volume sees the racer on level ground as in a race. The camera is the
// menu camera moved with the racer, so the framing is the 2D road's.
struct View {Vec3 eye,target,up;C3D_Mtx toWorld;};
View viewFor(float lift){
 C3D_Mtx m;Mtx_Identity(&m);
 Mtx_Translate(&m,0,RacerY,RacerZ,true);Mtx_RotateX(&m,RacerPitch,true);Mtx_Translate(&m,0,lift,0,true);
 Mtx_Inverse(&m);
 auto at=[&](float x,float y,float z,float w){const auto v=Mtx_MultiplyFVec4(&m,FVec4_New(x,y,z,w));return Vec3{v.x,v.y,v.z};};
 return {at(0,0,EyeDistance,1),at(0,0,0,1),normalized(at(0,1,0,0)),m};
}
float liftFor(const Racer& racer){
 C3D_Mtx identity;Mtx_Identity(&identity);
 VehicleAnimation::Snapshot pose;
 if(!VehicleAnimation::initialSnapshot(racer.choice->vehicle,identity,pose)||!pose.count)return 0;
 float low=0;
 for(unsigned w=0;w<pose.count;++w)low+=pose.tirePositions[w].y-pose.radius[w]*pose.scale.y;
 return low/float(pose.count);
}
// The road under the racer: the part of y=0 the window shows, as a grid whose
// corners are where the rays through the window's pixels meet the plane. Its
// image is exactly the window (a plane maps to the screen by a homography), so
// no scissor is needed; it writes depth, so the shadow volume lands on it.
// Texels are square, at the 2D road's scale; haze toward the top as there.
std::unique_ptr<Model> groundFor(std::shared_ptr<Texture> texture,const View& view){
 constexpr unsigned N=8;
 const float t=std::tan(FovDegrees*float(M_PI)/360.f);
 std::array<std::array<Vec3,N+1>,N+1> points;
 float nearest=1e30f;
 for(unsigned j=0;j<=N;++j)for(unsigned i=0;i<=N;++i){
  const float sx=X+W*float(i)/N,sy=Y+H*float(j)/N;
  const auto d=Mtx_MultiplyFVec4(&view.toWorld,FVec4_New((sx-200.f)/120.f*t,(120.f-sy)/120.f*t,-1,0));
  if(d.y>-1e-4f)return nullptr; // the window would show the horizon
  const float s=-view.eye.y/d.y;
  points[j][i]=view.eye+Vec3{d.x,d.y,d.z}*s;
  nearest=std::min(nearest,length(points[j][i]-view.eye));
 }
 constexpr float Centre=.2f;
 const float acrossU=length(points[N][N]-points[N][0])/.24f,alongV=acrossU/8.f;
 auto model=std::make_unique<Model>();
 model->vertexCount=(N+1)*(N+1);model->indexCount=N*N*6;
 model->vertices=static_cast<Vertex*>(linearAlloc(model->vertexCount*sizeof(Vertex)));
 model->indices=static_cast<uint16_t*>(linearAlloc(model->indexCount*sizeof(uint16_t)));
 if(!model->vertices||!model->indices)return nullptr;
 model->minimum={1e30f,0,1e30f};model->maximum={-1e30f,0,-1e30f};
 for(unsigned j=0;j<=N;++j)for(unsigned i=0;i<=N;++i){
  const Vec3 p=points[j][i];auto& v=model->vertices[j*(N+1)+i];
  const float fog=std::clamp((length(p-view.eye)/nearest-1.f)/1.5f,0.f,1.f)*.25f;
  const auto c=uint8_t(std::lround(255.f*(1.f-fog)));
  v={p,{0,1,0},{Centre+p.x/acrossU,p.z/alongV},{0,0},{c,c,c,255}};
  model->minimum={std::min(model->minimum.x,p.x),0,std::min(model->minimum.z,p.z)};
  model->maximum={std::max(model->maximum.x,p.x),0,std::max(model->maximum.z,p.z)};
 }
 unsigned n=0;
 for(unsigned j=0;j<N;++j)for(unsigned i=0;i<N;++i){
  const auto a=uint16_t(j*(N+1)+i),b=uint16_t(a+1),c=uint16_t(a+N+1),d=uint16_t(c+1);
  for(uint16_t k:{a,c,b,b,c,d})model->indices[n++]=k;
 }
 // One opaque, two-sided, unshaded submesh with repeating UVs: a shadow receiver.
 Submesh sub{};sub.first=0;sub.count=model->indexCount;sub.texture=0;sub.secondary=-1;
 sub.flags=4u|8u|1u<<8|1u<<10; // 8: unlit, the vertex colour as is
 model->submeshes.push_back(sub);model->textures.push_back(std::move(texture));
 model->freezeLighting();model->enableStaticCulling();
 flushGpuData(model->indices,model->indexCount*sizeof(uint16_t));
 return model;
}
C3D_Mtx chassisFor(float lift,unsigned age){
 C3D_Mtx chassis;Mtx_Identity(&chassis);
 Mtx_Translate(&chassis,0,-lift,0,true);
 // A three-quarter front view at first, then turning.
 Mtx_RotateY(&chassis,-.8f+float(age)*.006f,true);
 return chassis;
}
// Places the vehicle at `chassis` (its source initial suspension state).
bool placeRacer(const Racer& racer,VehicleAnimation& vehicle,const C3D_Mtx& chassis){
 VehicleAnimation::Snapshot pose;
 return VehicleAnimation::initialSnapshot(racer.choice->vehicle,chassis,pose)&&vehicle.setSnapshot(pose);
}
// Animates and draws the placed racer; low draws the Opponent Detail:
// Reduced models.
void drawRacer(Renderer& renderer,Racer& racer,VehicleAnimation& vehicle,bool low,unsigned age){
 auto& a=racer.assets;const auto& choice=*racer.choice;
 C3D_Mtx seated=vehicle.body();Mtx_Translate(&seated,0,choice.seatY,choice.seatZ,true);
 auto draw=[&](const Model& model,const C3D_Mtx& transform,const DriverAnimation* bank=nullptr){
  if(bank&&bank->gpuSkinned())renderer.setSkinPalette(bank->palette(),bank->matrixCount(),21);
  else renderer.setSkinPalette(nullptr,0,12);
  renderer.draw(model,transform);
 };
 if(low){
  // RacerVisuals::drawPrepared at LOD stage 2.
  if(racer.bodyLod)draw(*racer.bodyLod,vehicle.body());
  if(racer.handleLod)draw(*racer.handleLod,vehicle.accessory("handle",false));
  if(racer.driverLod)draw(*racer.driverLod,seated);
  if(VehicleLodLayout[choice.vehicle].wheels)
   for(unsigned i=0;i<vehicle.wheelCount();++i)if(auto& wheel=a.wheels[vehicle.modelIndex(i)])draw(*wheel,vehicle.wheel(i));
  renderer.setSkinPalette(nullptr,0,12);
  return;
 }
 // Seated still: the race's "drive" clip held on its first frame, so the
 // driver does not loop its steering motion while the racer turns.
 const int clip=a.animation->find(driverVehicleClipName("drive",choice.bike));
 if(clip>=0){
  a.animation->setBraidWorld(seated);
  a.animation->apply(*a.driver,clip,0,age,a.animation->face("drive"),0);
 }
 auto animate=[&](DriverAnimation* bank,Model* model,bool handle){
  if(!bank||!model)return;
  const int c=handle&&bank->handleDriveClip()>=0?bank->handleDriveClip():bank->shakeClip();
  if(c>=0)bank->apply(*model,c,handle?vehicle.driveFrame()*(bank->duration(unsigned(c))-1)/30.f:float(age),age);
 };
 animate(a.bodyAnimation.get(),a.body.get(),false);
 for(unsigned i=0;i<a.extras.size();++i)animate(a.extraAnimation[i].get(),a.extras[i].get(),i==0);
 draw(*a.body,vehicle.body(),a.bodyAnimation.get());
 draw(*a.driver,seated,a.animation.get());
 for(unsigned i=0;i<vehicle.wheelCount();++i)if(auto& wheel=a.wheels[vehicle.modelIndex(i)])draw(*wheel,vehicle.wheel(i));
 for(unsigned i=0;i<a.extras.size();++i)if(a.extras[i])
  for(unsigned n=0;n<vehicle.accessoryCount(GarageExtraNames[i]);++n)
   draw(*a.extras[i],vehicle.accessory(GarageExtraNames[i],a.extraAnimation[i]&&a.extraAnimation[i]->handleDriveClip()>=0,n),a.extraAnimation[i].get());
 renderer.setSkinPalette(nullptr,0,12);
}
const CourseBloom& previewBloom(){
 static const CourseBloom* bloom=courseBloom("Race/Course/beginner_course.szs");
 return bloom?*bloom:CourseBlooms[0];
}
}
struct SettingsPreview::Impl {
 std::shared_ptr<RoadJob> roadJob;
 std::shared_ptr<Texture> high,standard;
 std::shared_ptr<RacerJob> racerJob;
 std::unique_ptr<Racer> racer;
 VehicleAnimation vehicle;
 // The road plane for the current racer (its lift), at both qualities.
 std::unique_ptr<Model> groundHigh,groundStandard;
 const Racer* groundRacer=nullptr;float lift=0;
 bool roadFailed=false;unsigned racerFailed=~0u;
 // Targets this page created: bloom's (requested
 // this frame by begin(), composited by draw()).
 bool bloomUsed=false,bloomFrame=false;
 bool roadResident()const{return high&&standard;}
 bool racerResident(unsigned garage)const{return racer&&racer->choice==&GarageChoices[garage];}
 bool groundReady()const{return groundHigh&&groundStandard&&racer&&groundRacer==racer.get();}
 void publish(Renderer& renderer){
  if(roadJob&&roadJob->done()){
   if(roadJob->high&&roadJob->standard){high=std::move(roadJob->high);standard=std::move(roadJob->standard);}
   else if(!roadJob->cancelled())roadFailed=true;
   roadJob.reset();
  }
  if(racerJob&&racerJob->done()){
   if(racerJob->racer){retireRacer(renderer);racer=std::move(racerJob->racer);}
   else if(!racerJob->cancelled())racerFailed=racerJob->garage;
   racerJob.reset();
  }
  buildGround(renderer);
 }
 void buildGround(Renderer& renderer){
  if(!racer||!roadResident()||groundRacer==racer.get())return;
  retireGround(renderer);
  lift=liftFor(*racer);
  const auto view=viewFor(lift);
  groundHigh=groundFor(high,view);groundStandard=groundFor(standard,view);
  groundRacer=racer.get();
 }
 void retireGround(Renderer& renderer){
  renderer.retire(std::move(groundHigh));renderer.retire(std::move(groundStandard));groundRacer=nullptr;
 }
 void retireRacer(Renderer& renderer){
  if(!racer)return;
  retireGround(renderer);
  auto& a=racer->assets;
  renderer.retire(std::move(a.body));renderer.retire(std::move(a.driver));renderer.retire(std::move(a.shadow));
  for(auto& wheel:a.wheels)renderer.retire(std::move(wheel));
  for(auto& extra:a.extras)renderer.retire(std::move(extra));
  // The banks' palettes may still be read by the frame in flight.
  renderer.retire(std::move(a.animation));renderer.retire(std::move(a.bodyAnimation));
  for(auto& bank:a.extraAnimation)renderer.retire(std::move(bank));
  renderer.retire(std::move(racer->driverLod));renderer.retire(std::move(racer->bodyLod));renderer.retire(std::move(racer->handleLod));
  racer.reset();
 }
 void release(Renderer& renderer){
  if(roadJob){roadJob->cancel();roadJob.reset();}
  if(racerJob){racerJob->cancel();racerJob.reset();}
  retireRacer(renderer);renderer.retire(std::move(high));renderer.retire(std::move(standard));
  roadFailed=false;racerFailed=~0u;
  // Their VRAM goes back to the menus and the next race (which allocates
  // its own as its settings say).
  if(bloomUsed)renderer.releaseBloom();
  bloomUsed=bloomFrame=false;
 }
};
SettingsPreview::SettingsPreview():impl_(std::make_unique<Impl>()){}
SettingsPreview::~SettingsPreview()=default;
void SettingsPreview::request(const MenuState& state,AssetPriority priority){
 auto& m=*impl_;
 if(state.page!=UiPage::Settings)return;
 if(!m.roadResident()&&!m.roadFailed){
  if(!m.roadJob)m.roadJob=std::make_shared<RoadJob>();
  if(!m.roadJob->done())AssetLoader::instance().submit(m.roadJob,priority);
 }
 const unsigned garage=garageFor(state);
 if(!m.racerResident(garage)&&m.racerFailed!=garage){
  if(m.racerJob&&m.racerJob->garage!=garage){m.racerJob->cancel();m.racerJob.reset();}
  if(!m.racerJob)m.racerJob=std::make_shared<RacerJob>(garage);
  if(!m.racerJob->done())AssetLoader::instance().submit(m.racerJob,priority);
 }
}
void SettingsPreview::update(Renderer& renderer,const MenuState& state){
 auto& m=*impl_;
 if(state.page!=UiPage::Settings){if(m.roadJob||m.racerJob||m.roadResident()||m.racer||m.roadFailed||m.racerFailed!=~0u||m.bloomUsed)m.release(renderer);return;}
 m.publish(renderer);
}
void SettingsPreview::prepare(Renderer& renderer,const MenuState& state){
 auto& m=*impl_;
 if(state.page!=UiPage::Settings){update(renderer,state);return;}
 // QA path: run the loads on the calling thread.
 if(!m.roadResident()){
  if(m.roadJob)m.roadJob->cancel();
  m.roadJob=std::make_shared<RoadJob>();m.roadJob->run();
  if(!m.roadJob->high)m.roadFailed=true;else{m.high=std::move(m.roadJob->high);m.standard=std::move(m.roadJob->standard);}
  m.roadJob.reset();
 }
 const unsigned garage=garageFor(state);
 if(!m.racerResident(garage)){
  if(m.racerJob)m.racerJob->cancel();
  m.racerJob=std::make_shared<RacerJob>(garage);m.racerJob->run();
  if(m.racerJob->racer){m.retireRacer(renderer);m.racer=std::move(m.racerJob->racer);}else m.racerFailed=garage;
  m.racerJob.reset();
 }
 update(renderer,state);
}
void SettingsPreview::begin(Renderer& renderer,const MenuState& state){
 auto& m=*impl_;m.bloomFrame=false;
 if(state.page!=UiPage::Settings||!m.groundReady()){renderer.begin({0,0,EyeDistance},{0,0,0},{0,1,0});return;}
 // The Bloom row: the race's bloom (Luigi Circuit's settings) over the
 // window only. The setting is what the next race uses anyway.
 const auto sample=sampleFor(state);
 renderer.bloomEnabled=sample.o.bloom;
 if(std::min(sample.row,3u)==2&&sample.o.bloom){
  static constexpr float Window[4]={X,Y,W,H};
  renderer.requestBloom(previewBloom(),Window);m.bloomUsed=m.bloomFrame=true;
 }
 const auto view=viewFor(m.lift);
 // 3D: the racer (view depth EyeDistance-RacerZ) on the screen plane, as
 // races put the kart near it, and the road behind it. Without this the
 // reference plane fell at the race camera's ratio of the target distance and
 // the racer stood well in front of the screen.
 renderer.setStereoSubject(stereo::Race,(EyeDistance-RacerZ)/stereo::Race.levelRatio);
 renderer.begin(view.eye,view.target,view.up);
}
void SettingsPreview::draw(Renderer& renderer,const MenuState& state,const UiTransitionView&){
 if(state.page!=UiPage::Settings)return;
 auto& m=*impl_;const auto sample=sampleFor(state);const auto& o=sample.o;
 drawFrame(renderer);
 // 800px Mode (row 4), Minimap (row 5, a race's lower screen) and CRT (row 6)
 // show the plain racer: the whole top screen is the sample.
 const unsigned row=std::min(sample.row,4u);
 if(!m.roadResident())return;
 if(row==0){drawRoad(renderer,o.textureQuality?*m.standard:*m.high,state.age);return;}
 // The other samples: the racer on the road at the chosen quality.
 if(!m.groundReady()){drawRoad(renderer,o.textureQuality?*m.standard:*m.high,state.age,.5f);return;}
 C3D_Mtx identity;Mtx_Identity(&identity);
 const Model& ground=o.textureQuality?*m.groundStandard:*m.groundHigh;
 // 3D: the scene stays inside the window in both eyes (its road is exactly
 // the window only in the centre view).
 renderer.setScreenClip(true,X,Y,W,H);
 struct EndClip {Renderer& r;~EndClip(){r.setScreenClip(false);}} endClip{renderer};
 renderer.setSkinPalette(nullptr,0,12);renderer.draw(ground,identity);
 auto& a=m.racer->assets;
 const C3D_Mtx chassis=chassisFor(m.lift,state.age);
 // The native shadow volume under the racer, drawn before it as in a race.
 if(!placeRacer(*m.racer,m.vehicle,chassis))return;
 if(a.shadow){const Renderer::ShadowVolume volume{a.shadow.get(),m.vehicle.body()};renderer.drawShadowVolumes(&volume,1,.5f);}
 drawRacer(renderer,*m.racer,m.vehicle,(row==3||sample.preset)&&o.opponentDetail,state.age);
 if(m.bloomFrame)renderer.applyBloom();
}
void SettingsPreview::reset(Renderer& renderer){impl_->release(renderer);}
}
