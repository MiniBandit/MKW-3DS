#include <mkw/staff_scene.hpp>
#include <mkw/staff_presentation.hpp>
#include <mkw/staff_camera_data.hpp>
#include <mkw/award_environment_data.hpp>
#include <mkw/award_environment_draw.hpp>
#include <mkw/award_physics.hpp>
#include <mkw/award_lighting.hpp>
#include <mkw/ceremony_vehicle.hpp>
#include <mkw/native_mii_portrait.hpp>
#include <mkw/mii_appearance.hpp>
#include <mkw/menu_movie.hpp>
#include <mkw/menu_movie_audio.hpp>
#include <mkw/flow_text.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/loading_screen.hpp>
#include <mkw/presentation_cadence.hpp>
#include <mkw/sound_system.hpp>
#include <mkw/sound_ids.hpp>
#include <mkw/color_fader.hpp>
#include <game/kart/KartObject.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartParam.hh>
#include <game/system/KPadController.hh>
#include <cstdio>
#include <cstring>
#include <string>
namespace mkw {
namespace {
constexpr float Unit=240.f/456;
const char* message(unsigned id){for(const auto& m:StaffMessages)if(m.id==id)return m.text;return "";}
struct Photo {
 Texture image;std::unique_ptr<Model> body,head;MiiAppearance identity{};bool full=false;
 bool load(const NativeMii& native,bool variant){
  if(!image.load(StaffPhotoPaths[variant]))return false;
  if(!decodeMiiAppearance(native,identity))return false;
  const auto& source=StaffPhotoBodies[identity.female?1:0];
  body=std::make_unique<Model>();if(!body->load(source.model))return false;
  head=std::make_unique<Model>();
  // The photo has no moving 3D camera. Use the already-correct native icon
  // pipeline for hair, glasses, lighting and depth, then keep two expressions.
  // Construction happens during loading; no CFL work runs in a photo frame.
  for(bool blink:{false,true}){
   std::vector<std::uint8_t> pixels;
   if(!composeNativeMiiPhotoHead(native,128,blink,pixels))return false;
   auto texture=std::make_shared<Texture>();
   if(!texture->initRgba8(128,128,pixels.data(),pixels.size()))return false;
   head->textures.push_back(std::move(texture));
  }
  constexpr float half=51.8400827f; // 600*tan(native icon FOV 9.8762deg / 2)
  head->vertices=static_cast<Vertex*>(linearAlloc(4*sizeof(Vertex)));
  head->indices=static_cast<std::uint16_t*>(linearAlloc(6*sizeof(std::uint16_t)));
  if(!head->vertices||!head->indices)return false;
  const float positions[4][2]={{-half,34.5f-half},{half,34.5f-half},{-half,34.5f+half},{half,34.5f+half}};
  for(unsigned i=0;i<4;++i){auto& v=head->vertices[i];v={};v.position={positions[i][0],positions[i][1],0};v.normal={0,0,1};
   v.uv[0]=float(i&1);v.uv[1]=float(i>>1);std::memset(v.color,255,4);}
  constexpr std::uint16_t indices[]={0,1,2,2,1,3};std::copy(std::begin(indices),std::end(indices),head->indices);
  head->vertexCount=4;head->indexCount=6;head->minimum={-half,34.5f-half,0};head->maximum={half,34.5f+half,0};
  Submesh face{};face.first=0;face.count=6;face.texture=0;face.secondary=-1;face.flags=2|4|8;face.constant=0xffffffff;
  head->submeshes.push_back(face);
  GSPGPU_FlushDataCache(head->vertices,4*sizeof(Vertex));GSPGPU_FlushDataCache(head->indices,6*sizeof(std::uint16_t));
  // Photo bodies use twelve original colour textures, never tint skin/gloves.
  bool found=false;for(auto& sub:body->submeshes)if(sub.count==6&&sub.texture>=0&&unsigned(sub.texture)<body->textures.size()&&sub.first==6){
   sub.texture=source.colours[std::min(identity.favoriteColor,11u)];found=true;
  }
  body->submeshes.erase(std::remove_if(body->submeshes.begin(),body->submeshes.end(),[](const auto& sub){return sub.first==0;}),body->submeshes.end());
  full=found;return full;
 }
 void draw(Renderer& renderer,bool variant,unsigned tick){
  renderer.drawScreenSprite(image,0,0,400,240,StaffPhotoU,StaffPhotoV);
  if(!full)return;
  // The authored photo is a billboard with a separate animated face_1.
  // Keep its pose/colour, attach the native head through the same local
  // RFL-axis bridge used by the menu body; no Wii head data is loaded.
  const auto& def=StaffPhotoBodies[identity.female?1:0];
  C3D_Mtx root,joint,local,attached,world;Mtx_Identity(&root);
  Mtx_Translate(&root,0,-body->minimum.y,-15,true); // put the feet at the explicit photo anchor
  Mtx_Identity(&joint);for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)joint.r[r].c[3-c]=def.head[r*4+c];
  // Lift the head into the collar; retain the native-to-photo axis bridge.
  Mtx_Identity(&local);Mtx_Translate(&local,7,3,0,true);
  Mtx_RotateZ(&local,3.141592741f,true);
  Mtx_RotateY(&local,-1.570796371f,true);
  Mtx_Scale(&local,.85f,.85f,.85f);
  Mtx_Multiply(&attached,&joint,&local);Mtx_Multiply(&world,&root,&attached);
  const auto placement=staffPhotoPlacement(variant);
  const float x=placement.x,y=placement.y,scale=placement.scale;
  // Both authored photo parts are billboards. The baked native head has
  // internal depth, but no per-pixel depth relative to this separate body.
  // Draw the body last so its collar/shoulder/hand silhouette occludes hair.
  // The alpha-blended head never writes depth; transparent body pixels
  // retain the head behind them, preserving its native material and light.
  head->submeshes[0].texture=tick%180>=2&&tick%180<6?1:0;
  renderer.drawAtScreen(*head,world,x,y+2.f,1.f,scale,scale); // LCD-pixel neck alignment; body/occlusion stay fixed.
  renderer.drawAtScreen(*body,root,x,y,1,scale,scale);
 }
};
struct Cast final:AssetJob {
 struct Actor {CeremonyVehicle model;VehicleAnimation vehicle;DriverController controller;DriverPose pose;VehicleAnimation::Snapshot snapshot;bool visible=false;};
 AwardResults results;std::unique_ptr<AwardEnvironmentJob> environment;std::vector<std::unique_ptr<Actor>> actors;
 bool loaded=false;std::string failure;
 explicit Cast(const AwardResults& r):results(r){label="staff driving scene";}
 const char* detail()const override{return failure.c_str();}
 void run()override{
  Model::setLightingCourse(awardLighting("ending_demo"));
  environment=std::make_unique<AwardEnvironmentJob>(AwardEnvironments[3]);environment->run();
  if(!environment->loaded){failure=environment->detail();return;}
  for(unsigned i=0;i<results.count;++i){if(cancelled())return;auto actor=std::make_unique<Actor>();const auto& p=results.players[i];
   if(!actor->model.load(p.character,p.vehicle,RaceTeam::None,&failure))return;
   if(!p.human)actor->model.freezeLighting();
   for(const char* name:DriverController::names)actor->controller.setDuration(name,actor->model.clipDuration(name));
   actors.push_back(std::move(actor));
  }loaded=true;
 }
 void step(AwardPhysicsScene& physics,unsigned tick){
  using namespace Kinoko;
  for(unsigned i=0;i<actors.size();++i){auto& a=*actors[i];auto* kart=physics.kart(i);a.visible=physics.visible(i)&&kart;if(!a.visible)continue;
   a.vehicle.step(*kart);a.snapshot=a.vehicle.snapshot();const auto& state=kart->status();DriverSignals s;
   s.bike=kart->isBike();s.vehicleId=results.players[i].vehicle;s.speed=kart->move()->speed();s.steering=kart->inputs()->currentState().stick.x;
   s.drift=state.onBit(Kart::eStatus::DriftManual,Kart::eStatus::DriftAuto);s.driftDirection=kart->move()->hopStickX();
   s.airborne=state.offBit(Kart::eStatus::TouchingGround);s.hopping=state.onBit(Kart::eStatus::Hop);s.blink=(tick+i*13)%180==0;
   s.ceremonyVictory=physics.celebrating(i);s.finishType=0;a.pose=a.controller.step(s);
  }
 }
 void draw(Renderer& renderer,unsigned tick){
  std::array<Renderer::ShadowVolume,12> shadows{};unsigned count=0;
  for(auto& actor:actors)if(actor->visible){actor->model.pose(actor->snapshot,actor->pose,tick);shadows[count++]=actor->model.shadow();}
  renderer.drawShadowVolumes(shadows.data(),count,.5f);
  for(auto& actor:actors)if(actor->visible)actor->model.draw(renderer);
 }
};
struct Session {
 Renderer& renderer;Texture font;Photo photo;MenuMovie movie;MenuMovieAudio audio;
 explicit Session(Renderer& r):renderer(r){}
 std::shared_ptr<Cast> cast;std::unique_ptr<AwardPhysicsScene> physics;
 ~Session(){
  renderer.setDualScreenIntro(false);if(cast)cast->cancel();audio.close();movie.close();
  SoundSystem::instance().stop();AssetLoader::instance().stop();renderer.waitForGpu();
  renderer.setRetailLighting(nullptr);cast.reset();physics.reset();renderer.clearUnusedTextureCache();
 }
};
void black(Renderer& renderer){renderer.setDualScreenIntro(false);renderer.begin({0,0,1000},{0,0,0},{0,1,0});renderer.drawScreenFade(1,false);renderer.end();}
struct CreditDraw {
 Renderer* renderer=nullptr;const Texture* font=nullptr;FlowDrawer drawer;
 static void submit(const FlowBatch& batch,void* payload){
  auto& self=*static_cast<CreditDraw*>(payload);
  static_assert(sizeof(FlowQuad)==sizeof(Renderer::MaterialQuad));
  self.renderer->drawScreenMaterialQuads(batch.texture>=0?self.font:nullptr,
   reinterpret_cast<const Renderer::MaterialQuad*>(batch.quads),batch.count,batch.color0,batch.color1,0,0);
 }
 void congratulations(Renderer& render,const Texture& texture){
  renderer=&render;font=&texture;
  const FlowAwardBinding binding{"congratulations","CONGRATULATIONS!",-2,&StaffFont};
  drawer.drawAward(FlowStaffPhotoCaptionLayout,{0,0,1,1,0},nullptr,0,&binding,1,submit,this);
 }
 void credits(Renderer& render,const Texture& texture,bool movie,unsigned tick){
  renderer=&render;font=&texture;
  const auto* table=movie?StaffMovieCredits:StaffDemoCredits;const unsigned count=movie?std::size(StaffMovieCredits):std::size(StaffDemoCredits);
  for(unsigned i=0;i<count;++i){const auto& row=table[i];if(tick<row.start||tick>=unsigned(row.end)+60)continue;
   for(unsigned region=0;region<3;++region){const unsigned title=row.ids[region*2],name=row.ids[region*2+1];if(!title&&!name)continue;
    const std::string heading=message(title),names=message(name);
    // Original Staff BMG 8900/8901 use two text layers with the same line
    // positions and opposite slide directions. Spaces retain blank lines.
    const auto blank=[](std::string value){for(auto& c:value)if(c!='\n')c=' ';return value;};
    for(unsigned layer=0;layer<2;++layer){if((layer==0&&!title)||(layer==1&&!name))continue;
     const unsigned slot=region*2+layer;const auto& def=*StaffCreditLayouts[slot];
     const bool exiting=tick>=row.end;const bool reverse=layer==1;
     const auto play=flowPlay(def,"Fade",exiting?(reverse?"Fadein":"Fadeout"):(tick-row.start<60?(reverse?"Fadeout":"Fadein"):"Show"));
     auto sampled=play;sampled.frame=exiting?float(tick-row.end):std::min(float(tick-row.start),60.f);
     const std::string content=title?(layer==0?heading+"\n"+blank(names):blank(heading)+"\n"+names):names;
     const FlowAwardBinding binding{"TextBox_00",content.c_str(),-2,&StaffFont};
     drawer.drawAward(def,StaffCreditControls[slot][movie?1:0],&sampled,1,&binding,1,submit,this);
    }
   }
  }
 }
};
}
AwardSceneResult runStaff(Renderer& renderer,const AwardResults& source,const NativeMii& identity,bool variant,
 void(*report)(const char*),Kinoko::EGG::SceneManager& scenes){
 renderer.setDeferred(false);renderer.frameSync=true;renderer.waitForGpu();renderer.setRetailLighting(nullptr);
 if(!renderer.enableBottom())return AwardSceneResult::Failed;
 auto storage=std::make_unique<Session>(renderer);auto& session=*storage;
 auto& loader=AssetLoader::instance();auto& sound=SoundSystem::instance();sound.stop();
 renderer.reserveBloom();
 AwardResults results=source;
 if(!results.valid||!results.count){results={};results.valid=true;results.count=12;
  constexpr unsigned characters[]={0,7,16,10,6,1,8,14,11,2,9,3};
  for(unsigned i=0;i<12;++i){const unsigned c=characters[i],weight=i<4?1:i<8?0:2;results.players[i]={uint8_t(i),uint8_t(c),uint8_t(weight),i==0,RaceTeam::None,0};}
 }
 results.teams=false;
 {LoadingScope loading;black(renderer);
  if(!session.font.load("textures/staff_font.t3x")||!session.photo.load(identity,variant)){report("FAIL staff photo/native Mii resources");return AwardSceneResult::Failed;}
  session.cast=std::make_shared<Cast>(results);
  if(loader.start())loader.submit(session.cast,AssetPriority::Urgent);else session.cast->run();
  if(sound.start()){const uint32_t groups[]={snd::id::group::GRP_CMN,snd::id::group::GRP_ENDING_A};sound.loadScene(groups,2);}
  while(loader.running()&&!session.cast->done()){if(!aptMainLoop())return AwardSceneResult::Exit;black(renderer);}
  if(!session.cast->loaded){report(session.cast->detail());return AwardSceneResult::Failed;}
  for(auto& actor:session.cast->actors)if(!actor->model.prepareMii(results.raceMii)){report("FAIL staff Mii head");return AwardSceneResult::Failed;}
  session.physics=std::make_unique<AwardPhysicsScene>(scenes);
  if(!session.physics->createEnding(results)){report("FAIL staff physical scene");return AwardSceneResult::Failed;}
  session.cast->environment->prepareVram(renderer);
  const auto audioWait=svcGetSystemTick();
  while(sound.available()&&!sound.sceneReady()){
   if(!aptMainLoop())return AwardSceneResult::Exit;
   if(svcGetSystemTick()-audioWait>120ULL*SYSCLOCK_ARM11){report("FAIL staff scene audio load");return AwardSceneResult::Failed;}
   black(renderer);
  }
 }
 sound.play(snd::id::STRM_O_ENDING_PART_A,1);
 RaceCameraCourse cameraCourse=staffCameraCourse();RaceCameraDirector camera;camera.setCourse(&cameraCourse);
 unsigned shot=16,shotFrame=0,phase=0,tick=0;bool shotEntered=false;std::uint64_t start=svcGetSystemTick();
 PresentationCadence cadence;cadence.setTicksPerSlot(2);unsigned lastTick=0;auto creditDraw=std::make_unique<CreditDraw>();
 while(aptMainLoop()){
  hidScanInput();const auto down=hidKeysDown();const unsigned wanted=unsigned(double(svcGetSystemTick()-start)*60/SYSCLOCK_ARM11);
  // Keep simulation deterministic when a display frame takes longer. During
  // an actual blocking transition reset the wall clock below.
  while(lastTick<wanted&&lastTick<tick+6){++lastTick;
   if(phase==0){
    if(!session.physics->step()){report("FAIL staff driving update");return AwardSceneResult::Failed;}
    session.cast->step(*session.physics,lastTick);
    unsigned target=shot>=2&&shot<=8?std::min(shot-2,results.count-1):0;
    if(!session.physics->visible(target))for(unsigned i=0;i<results.count;++i)if(session.physics->visible(i)){target=i;break;}
    auto* kart=session.physics->kart(target);RaceCameraInput in;
    in.kartPos={kart->pos().x,kart->pos().y,kart->pos().z};in.kartForward={kart->move()->vel1Dir().x,kart->move()->vel1Dir().y,kart->move()->vel1Dir().z};in.cameraDistY=kart->cameraDistY();
    if(!shotEntered){camera.selectDemoShot(shot,in);shotEntered=true;}else camera.stepDemoShot(in);
    if(++shotFrame>=cameraCourse.cameras[shot].time){const auto next=cameraCourse.cameras[shot].next;if(next<cameraCourse.cameras.size())shot=next;shotFrame=0;shotEntered=false;}
   }
  }
  tick=lastTick;
  if(phase==0&&(tick>=6300||((down&(KEY_A|KEY_START))&&tick>=70))){
   LoadingScope loading;renderer.waitForGpu();session.cast.reset();session.physics.reset();renderer.setRetailLighting(nullptr);sound.stop();
   const auto video=variant?"movies/staff_true.gmv":"movies/staff_normal.gmv";
   const auto audio=variant?"movies/staff_true.wav":"movies/staff_normal.wav";
   if(!session.movie.open(video,false)){report("FAIL staff movie open");return AwardSceneResult::Failed;}
   const bool audioEnabled=session.audio.open(audio,false);
   session.movie.request(0);const auto movieWait=svcGetSystemTick();
   while(!session.movie.ready()||(audioEnabled&&!session.audio.ready())){
    if(!aptMainLoop())return AwardSceneResult::Exit;
    if(session.movie.failed()||svcGetSystemTick()-movieWait>30ULL*SYSCLOCK_ARM11){report("FAIL staff movie/audio load");return AwardSceneResult::Failed;}
    session.audio.update();black(renderer);session.movie.upload();
   }
   phase=1;start=svcGetSystemTick();lastTick=tick=0;cadence.reset();
  }else if(phase==1&&(tick>=9060||((down&(KEY_A|KEY_START))&&tick>=70))){
   session.audio.close();session.movie.close();renderer.waitForGpu();phase=2;start=svcGetSystemTick();lastTick=tick=0;cadence.reset();
  }else if(phase==2&&tick>=70&&(down&(KEY_A|KEY_B|KEY_START)))return AwardSceneResult::Complete;
  if(phase==1){session.movie.request(tick);session.audio.update();if(session.movie.failed())return AwardSceneResult::Failed;}
  if(!cadence.due(tick))continue;
  renderer.animationFrame=tick; // Ambient course SRT0/UV tracks, same scene tick as the crowd.
  renderer.setDualScreenIntro(phase==0);
  if(phase==0){renderer.setRetailLighting(awardLighting("ending_demo"),true);renderer.requestBloom(AwardBlooms[3]);
   const auto view=camera.view();renderer.setFov(view.fov,10,300000);renderer.begin(view.eye,view.target,{0,1,0});
   renderer.setCourseLighting(true);session.cast->environment->updateDecorations(tick,tick);session.cast->environment->drawBackground(renderer);session.cast->environment->drawDecorations(renderer);session.cast->draw(renderer,tick);renderer.markIntroWorldEnd();
   creditDraw->credits(renderer,session.font,false,tick);
  }else{
   renderer.setCourseLighting(false);renderer.setFov(45,10,100000);renderer.begin({0,0,1000},{0,0,0},{0,1,0});renderer.drawScreenFade(1,false);
   if(phase==1){session.movie.upload();const auto* image=session.movie.texture();
    // Original movie windows crossfade at authored section boundaries;
    // they do not alternate with every credit record.
    if(image)for(unsigned slot=0;slot<2;++slot){
     const auto placement=staffMoviePlacement(slot,tick);if(placement.alpha<=0)continue;
     const float x=200+(slot?148:-148)*Unit-128*Unit,y=120-(slot?-98:98)*Unit-96*Unit;
     renderer.drawScreenSprite(*image,x,y,256*Unit,192*Unit,1,.75f,placement.alpha);
    }
    creditDraw->credits(renderer,session.font,true,tick);
   }else{session.photo.draw(renderer,variant,tick);creditDraw->congratulations(renderer,session.font);}
  }
  if(tick<21)renderer.drawScreenFade(colorFaderOpacity(tick,true),false);
  if(phase==0&&tick>6278)renderer.drawScreenFade(colorFaderOpacity(tick-6278,false),false);
  if(phase!=0){renderer.beginBottom();renderer.drawScreenFade(1,false);}
  renderer.end();
 }
 return AwardSceneResult::Exit;
}
}
