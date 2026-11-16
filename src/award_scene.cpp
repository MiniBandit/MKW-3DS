#include <mkw/award_lighting.hpp>
#include <mkw/award_scene.hpp>
#include <mkw/loading_screen.hpp>
#include <mkw/award_scene_assets.hpp>
#include <mkw/award_camera_data.hpp>
#include <mkw/award_ui.hpp>
#include <mkw/color_fader.hpp>
#include <mkw/console_model.hpp>
#include <mkw/diag.hpp>
#include <mkw/presentation_cadence.hpp>
#include <mkw/renderer.hpp>
#include <mkw/sound_ids.hpp>
#include <mkw/sound_system.hpp>
#include <mkw/job_worker.hpp>
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace mkw {
namespace {
#ifndef MKW_RELEASE
// DIAG ceremony heartbeat. The ceremony loops (resource waits, drive, podium)
// log their state about once a second for the first 180 lines; a watchdog
// thread on core 0 logs where the main thread is once it has not come round a
// loop for 3, 10, 30, 60, 120 and 240 s (a call that never returns), then stays
// quiet, so a runner's "runtime.log unchanged" stall check still fires.
class CeremonyHeartbeat {
public:
 explicit CeremonyHeartbeat(void (*report)(const char*)):report_(report){
  start_=svcGetSystemTick();beat_.store(start_);LightEvent_Init(&quit_,RESET_STICKY);
  s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
  thread_=threadCreate(watch,this,16*1024,std::max<s32>(0x18,priority-1),0,false);
  line("start");
 }
 ~CeremonyHeartbeat(){
  LightEvent_Signal(&quit_);
  if(thread_){threadJoin(thread_,U64_MAX);threadFree(thread_);}
  where_.store("exit");line("end");
 }
 void at(const char* where){where_.store(where,std::memory_order_relaxed);beat_.store(svcGetSystemTick(),std::memory_order_relaxed);}
 void loop(const char* where,unsigned phase,unsigned frame,unsigned tick,double pending,const AwardDrive* drive,int soundReady,int loaderDone){
  phase_.store(phase,std::memory_order_relaxed);frame_.store(frame,std::memory_order_relaxed);tick_.store(tick,std::memory_order_relaxed);
  pending_.store(unsigned(std::max(0.,pending)*1000),std::memory_order_relaxed);loops_.fetch_add(1,std::memory_order_relaxed);
  if(drive){driveTick_.store(int(drive->tick()));driveShot_.store(int(drive->shotIndex()));driveShotFrame_.store(int(drive->shotFrame()));driveDone_.store(int(drive->finished()));}
  else driveTick_.store(-1);
  soundReady_.store(soundReady,std::memory_order_relaxed);loaderDone_.store(loaderDone,std::memory_order_relaxed);
  at(where);
  const auto now=svcGetSystemTick();
  if(logged_<180&&now-lastLog_>=SYSCLOCK_ARM11){lastLog_=now;++logged_;line("hb");}
 }
 void drawn(){frames_.fetch_add(1,std::memory_order_relaxed);}
private:
 static void watch(void* self){
  auto& h=*static_cast<CeremonyHeartbeat*>(self);
  static constexpr double marks[]={3,10,30,60,120,240};
  unsigned next=0;
  while(!LightEvent_WaitTimeout(&h.quit_,1000000000LL)){
   const double silent=double(svcGetSystemTick()-h.beat_.load(std::memory_order_relaxed))/SYSCLOCK_ARM11;
   if(silent<1.5){next=0;continue;}
   if(next<std::size(marks)&&silent>=marks[next]){++next;char tag[48];std::snprintf(tag,sizeof tag,"watchdog main silent %.1f s",silent);h.line(tag);}
  }
 }
 void line(const char* tag){
  char text[320];
  std::snprintf(text,sizeof text,"ceremony %s: t=%.1f where=%s phase=%u frame=%u tick=%u pending=%.3f loops=%u drawn=%u drive tick=%d shot=%d/%d done=%d sound_ready=%d loader_done=%d worker=%d",
   tag,double(svcGetSystemTick()-start_)/SYSCLOCK_ARM11,where_.load(),phase_.load(),frame_.load(),tick_.load(),pending_.load()/1000.0,loops_.load(),frames_.load(),
   driveTick_.load(),driveShot_.load(),driveShotFrame_.load(),driveDone_.load(),soundReady_.load(),loaderDone_.load(),int(jobWorker().running()));
  report_(text);
 }
 void (*report_)(const char*);
 std::uint64_t start_=0,lastLog_=0;unsigned logged_=0;
 std::atomic<std::uint64_t> beat_{0};
 std::atomic<const char*> where_{"entry"};
 std::atomic<unsigned> phase_{99},frame_{0},tick_{0},pending_{0},loops_{0},frames_{0};
 std::atomic<int> driveTick_{-1},driveShot_{-1},driveShotFrame_{-1},driveDone_{-1},soundReady_{-1},loaderDone_{-1};
 LightEvent quit_{};Thread thread_=nullptr;
};
#else
struct CeremonyHeartbeat {
 explicit CeremonyHeartbeat(void (*)(const char*)){}
 void at(const char*){}
 void loop(const char*,unsigned,unsigned,unsigned,double,const AwardDrive*,int,int){}
 void drawn(){}
};
#endif
// AwardsMgr's static initializer, USA 807d3b08..807d3b48. Stage
// vertices already contain this anchor; only actors need it added.
constexpr Vec3 StandAnchor{0,1224,27850};
void black(Renderer& renderer,std::uint64_t waitStarted=0){
 renderer.setDualScreenIntro(false);
 renderer.begin({0,0,1000},{0,0,0},{0,1,0});
 renderer.drawScreenFade(1,false);
 // Keep the last lower-LCD picture; the blocking/wait scope overlays its
 // software tyre without transferring a newly cleared bottom render target.
 renderer.end();
}
struct Session {
 Renderer& renderer;
 AwardUi ui;
 std::shared_ptr<AwardSceneAssets> assets;
 std::unique_ptr<AwardPhysicsScene> physics;
 ~Session(){
  renderer.setDualScreenIntro(false);
  if(assets)assets->cancel();
  SoundSystem::instance().stop();
  AssetLoader::instance().stop();
  renderer.waitForGpu();renderer.setRetailLighting(nullptr);
  assets.reset();physics.reset();ui.unload();renderer.clearUnusedTextureCache();
 }
};
// Skipping the podium ducks the sound effect players under the music cut.
// AwardFade::HandleClick (80621474) calls 80716e60(1): PlayersVolumeMgr moves
// players 2-6 and 8-10 (OBJ, ENGINE, RUN, SPARK, VEHICLE, VOICE, VOICE_NPC,
// VOICE_DELAY) to 0.35 over 30 frames; BGM, MENU_UI_SE and RACE_UI keep theirs.
// The duck holds until the next sound scene starts: its start (8070e11c ->
// 80716c10) puts every player but MENU_UI_SE back to its base volume (1.0).
struct SkipDuck {
 SoundSystem& sound;float value=1,step=0;int frames=0;bool started=false;
 static constexpr float Target=.35f;static constexpr int Frames=30;
 void post(unsigned player,float v){SoundCommand c;c.type=SoundCommand::PlayerVolume;c.id=player;c.value=v;sound.post(c);}
 void start(){started=true;frames=Frames;step=(Target-value)/float(Frames);}
 void calc(){
  if(!frames)return;
  value=--frames?value+step:Target;
  for(unsigned p:{2u,3u,4u,5u,6u,8u,9u,10u})post(p,value);
 }
 ~SkipDuck(){if(started)for(unsigned p:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u})post(p,1.f);}
};
}
AwardSceneResult runAwardPodium(Renderer& renderer,const AwardResults& results,void (*report)(const char*),std::uint64_t transitionStart,Kinoko::EGG::SceneManager* scenes){
 const auto entryStart=transitionStart?transitionStart:svcGetSystemTick();
 // Declared before Session: its watchdog outlives the session's teardown.
 CeremonyHeartbeat heartbeat(report);
 std::uint64_t podiumStart=0;bool entryRecorded=false,podiumRecorded=false,boardRecorded=false;
 // Write after the first non-black GPU frame, never as on-screen loading text.
 auto recordLoad=[&](const char* stage,std::uint64_t started){
#ifndef MKW_RELEASE
  renderer.waitForGpu();
  const double ms=double(svcGetSystemTick()-started)*1000.0/SYSCLOCK_ARM11;
  if(FILE* file=std::fopen("sdmc:/3ds/MKWiiDS/award-load-times.jsonl","a")){
   std::fprintf(file,"{\"stage\":\"%s\",\"milliseconds\":%.3f,\"within2000ms\":%s}\n",stage,ms,ms<=2000?"true":"false");std::fclose(file);
  }
#endif
 };
 const bool podium=hasAwardPodium(results);
 if(!hasAwardScene(results))return AwardSceneResult::Failed;
 renderer.setDeferred(false);renderer.frameSync=true;
 if(!renderer.enableBottom()){report("FAIL ceremony lower LCD target");return AwardSceneResult::Failed;}
 renderer.waitForGpu();renderer.setRetailLighting(nullptr);
 if(!SoundSystem::instance().carryingMusic())SoundSystem::instance().stop();
 Session session{renderer};
 // Reserve render targets before scene textures claim VRAM.
 renderer.reserveBloom();
 // GP medals use the human's final podium place. VS has no GP cup model.
 unsigned trophyRank=0;
 if(results.mode==0&&!results.teams)
  for(unsigned i=0;i<std::min(results.count,3u);++i)if(results.players[i].human){trophyRank=i+1;break;}
 const RaceTeam winner=results.teams?results.humanTeam:RaceTeam::None;
 session.assets=std::make_shared<AwardSceneAssets>(results,podium?0:1,winner,trophyRank);
 auto& loader=AssetLoader::instance();
 const auto resourceWait=svcGetSystemTick();
 auto waiting=std::make_unique<LoadingScope>();
 black(renderer,resourceWait);
 if(loader.start())loader.submit(session.assets,AssetPriority::Urgent);
 else session.assets->run();
 auto& sound=SoundSystem::instance();
 const bool soundReadyToLoad=sound.start();
 const auto soundLoadStart=svcGetSystemTick();
 if(soundReadyToLoad){
  constexpr uint32_t groups[]={snd::id::group::GRP_CMN,snd::id::group::GRP_AWARD_DEMO};
  sound.loadScene(groups,std::size(groups));
 }
 if(loader.running())while(!session.assets->done()){
  heartbeat.loop("assets wait",99,0,0,0,nullptr,soundReadyToLoad?int(sound.sceneReady()):-1,0);
  if(!aptMainLoop())return AwardSceneResult::Exit;
  black(renderer,resourceWait);
 }
 if(!session.assets->loaded){report(session.assets->detail());report("FAIL ceremony resources");return AwardSceneResult::Failed;}
 heartbeat.at("physics create");
 if(session.assets->drive){
  if(!scenes){report("FAIL ceremony scene manager");return AwardSceneResult::Failed;}
  session.physics=std::make_unique<AwardPhysicsScene>(*scenes);
  LoadingScope loading;
  if(!session.physics->create(results)||!session.assets->drive->bindPhysics(*session.physics)){
   report("FAIL ceremony physical driving");return AwardSceneResult::Failed;
  }
 }
 if(session.assets->drive&&!session.assets->drive->prepareMii(results.raceMii)){report("FAIL Mii driving head");return AwardSceneResult::Failed;}
 if(session.assets->parade&&!session.assets->parade->prepareMii(results.raceMii)){report("FAIL Mii parade head");return AwardSceneResult::Failed;}
 heartbeat.at("ui load");
 bool uiLoaded=false;{LoadingScope loading;uiLoaded=session.ui.load();}
 if(!uiLoaded){report("FAIL ceremony result layout");return AwardSceneResult::Failed;}
 session.ui.configure(results,podium,awardOutcome(results)==AwardOutcome::Draw);
 renderer.setRetailLighting(&AwardLightings[podium?0:1],true);
 session.assets->environment->prepareVram(renderer);
 if(auto* cast=session.assets->cast.get()){
  cast->stand->prepareVram(renderer);
  for(auto& actor:cast->podium){if(!actor->prepareMii(results.raceMii)){report("FAIL Mii ceremony head");return AwardSceneResult::Failed;}actor->prepareVram(renderer);}
  for(auto& actor:cast->audience){if(!actor->prepareMii(results.raceMii)){report("FAIL Mii ceremony head");return AwardSceneResult::Failed;}actor->prepareVram(renderer);}
 }
 if(auto* trophy=session.assets->trophy.get())trophy->model->prepareVram(renderer);
 if(soundReadyToLoad){
  while(!sound.sceneReady()){
   heartbeat.loop("sound wait",98,0,0,0,nullptr,0,1);
   if(!aptMainLoop())return AwardSceneResult::Exit;
   if(svcGetSystemTick()-soundLoadStart>120ULL*SYSCLOCK_ARM11){report("FAIL ceremony audio load");return AwardSceneResult::Failed;}
   black(renderer,soundLoadStart);
  }
  sound.finishCarriedMusic(20);
  sound.play(podium?(results.mode==0?snd::id::STRM_O_GP_AWARD:snd::id::STRM_O_VS_AWARD):snd::id::STRM_O_AWARD_LOSE,1);
  if(!podium)sound.play(snd::id::SE_DEMO_LOSE_ENV_BIRDS,2);
 }
 enum class Phase {Drive,DriveOut,Intro,SkipOut,SkipIn,Board,Leaving,FadeOut};
 waiting.reset();
 Phase phase=session.assets->drive?Phase::Drive:Phase::Intro;
 bool jugemuWasVisible=false;
 unsigned frame=0,tick=0,skipFrame=0;
 auto enterBoard=[&](){
  phase=Phase::Board;frame=0;session.ui.enterBoard();
  // DIAG: a ceremony without a podium (the human placed 4th or lower) waits on
  // its result board for A and never records "podium": runners stop on either.
  if(!boardRecorded){recordLoad("board",entryStart);boardRecorded=true;}
  // AwardResults::BeforeEntranceAnimations dispatches AwardCupModel::Activate.
  if(auto* trophy=session.assets->trophy.get())sound.play(trophy->show());
 };
 auto previous=svcGetSystemTick();double pending=0;
 SkipDuck skipDuck{sound};
 // The race's cadence (presentation_cadence.hpp): 60 FPS on New 3DS, as the Wii.
 auto ceremonyTicks=[&]{return racePresentationTicks(isNew3ds(),renderer.stereoWanted(),true,diag::stereoSixty,diag::introSixty);};
 PresentationCadence presentation;presentation.setTicksPerSlot(ceremonyTicks());
 while(aptMainLoop()){
  if(const unsigned ticks=ceremonyTicks();ticks!=presentation.ticksPerSlot())presentation.setTicksPerSlot(ticks);
  heartbeat.loop("input",unsigned(phase),frame,tick,pending,session.assets->drive.get(),soundReadyToLoad?int(sound.sceneReady()):-1,1);
  // AwardFade and AwardResults register only the Front input (80621178, 80621f70): A, not B or START.
  hidScanInput();bool advance=(hidKeysDown()&KEY_A)!=0;
  const auto now=svcGetSystemTick();pending+=std::min(0.1,double(now-previous)/SYSCLOCK_ARM11)*60.;previous=now;
  // Handle edges once per display refresh; simulation may owe zero or several ticks.
  // AwardFade::HandleClick (80621408..24) accepts a skip after tick70,
  // only while the first camera is before frame400 (805b4888..48a8).
  if(advance&&phase==Phase::Drive&&frame>=70){phase=Phase::DriveOut;frame=0;}
  else if(advance&&podium&&phase==Phase::Intro&&frame>=70&&frame<400){
   skipFrame=frame;phase=Phase::SkipOut;frame=0;sound.stopSlot(1,30);skipDuck.start();
  }
  else if(advance&&phase==Phase::Board&&frame>=180&&session.ui.boardTransitionFinished()){
   session.ui.showCongratulations(false);session.ui.leaveBoard();
   if(auto* trophy=session.assets->trophy.get())trophy->leave();
   phase=Phase::Leaving;frame=0;
  }
  while(pending>=1){
   pending-=1;++tick;++frame;skipDuck.calc();
   if(const auto id=session.ui.step())sound.play(id);
   heartbeat.at("step");
   if(phase==Phase::Drive){
    heartbeat.at("drive step");
    session.assets->drive->step();
    if(session.assets->drive->finished()){phase=Phase::DriveOut;frame=0;}
   }else if(phase==Phase::DriveOut&&frame>=ColorFadeOutLastFrame){
    podiumStart=svcGetSystemTick();
    heartbeat.at("podium load");
    renderer.waitForGpu();black(renderer);renderer.waitForGpu();
    if(session.physics)session.physics->release();
    {LoadingScope loading;
    if(!session.assets->loadPodium()){report(session.assets->detail());return AwardSceneResult::Failed;}}
    if(auto* cast=session.assets->cast.get()){
     cast->stand->prepareVram(renderer);
     for(auto& actor:cast->podium){if(!actor->prepareMii(results.raceMii)){report("FAIL Mii ceremony head");return AwardSceneResult::Failed;}actor->prepareVram(renderer);}
     for(auto& actor:cast->audience){if(!actor->prepareMii(results.raceMii)){report("FAIL Mii ceremony head");return AwardSceneResult::Failed;}actor->prepareVram(renderer);}
    }
    renderer.clearUnusedTextureCache();
    phase=Phase::Intro;frame=0;tick=0;pending=0;previous=svcGetSystemTick();
   }else if(phase==Phase::Intro&&frame>=(podium?AwardCameraClips[0].duration:float(ColorFadeInLastFrame))){
    enterBoard();
   }else if(phase==Phase::SkipOut&&frame>=ColorFadeOutLastFrame){
    // AwardFade 80621380..c0 switches to demowin02 behind black and
    // starts STRM_O_AWARD_CUT. That cue belongs to skipping, not leaving.
    phase=Phase::SkipIn;frame=0;sound.play(snd::id::STRM_O_AWARD_CUT,1);
   }else if(phase==Phase::SkipIn&&frame>=ColorFadeInLastFrame){
    enterBoard();
   }else if(phase==Phase::Leaving&&session.ui.boardTransitionFinished()){
    // AwardResults::HandleClick changes section (8062254c..bc).
    // It never replays demowin03 as a made-up departure flight.
    phase=Phase::FadeOut;frame=0;sound.stopSlot(1,int(ColorFadeFrames));
   }else if(phase==Phase::FadeOut&&frame>=ColorFadeOutLastFrame){
    black(renderer);return AwardSceneResult::Complete;
   }
   if((phase==Phase::Board||phase==Phase::Leaving)&&frame>0)
    if(auto* trophy=session.assets->trophy.get())trophy->step(tick);
   if(session.assets->environment->stepIntroEffects(float(frame+(phase==Phase::SkipOut?skipFrame:0)),
     podium&&(phase==Phase::Intro||phase==Phase::SkipOut),tick))
    sound.hold(snd::id::SE_DEMO_HANABI,3);
  }
  // Same 30 Hz presentation budget as course intros; physics, camera and
  // original effect lifetimes continue on their authored 60 Hz ticks.
  heartbeat.at("sleep");
  if(!presentation.due(tick)){svcSleepThread(1000000);continue;}
  heartbeat.at("draw");
  const bool introCamera=podium&&(phase==Phase::Intro||phase==Phase::SkipOut);
  const float cameraFrame=float(frame+(phase==Phase::SkipOut?skipFrame:0));
  renderer.requestBloom(AwardBlooms[podium?0:1]);
  const bool driving=phase==Phase::Drive||phase==Phase::DriveOut;
  const auto activeCamera=driving?session.assets->drive->camera():
   sampleAwardCamera(AwardCameraClips[podium?(introCamera?0:1):3],introCamera?cameraFrame:0);
  const auto effectCamera=awardCameraMatrices(activeCamera);
  renderer.setDualScreenIntro(true);
  renderer.beginSourceCamera(effectCamera.view.data(),effectCamera.projection.data());
  // FrameBegin retires the preceding immediate GPU frame before mutable
  // animation vertices/palettes are touched.
  auto& assets=*session.assets;
  assets.environment->updateDecorations(float(tick),tick);
  if(assets.cast)assets.cast->updateActors(float(tick),tick);
  if(assets.parade)assets.parade->update(tick);
  if(driving)assets.drive->pose();
  // Loading the EGG light set does not enable it for world-model draws.
  renderer.setCourseLighting(true);
  assets.environment->drawBackground(renderer);
  if(assets.cast)assets.cast->drawStand(renderer,placeAwardStand(results.teams,{},StandAnchor));
  assets.environment->drawDecorations(renderer);
  const bool jugemu=introCamera&&assets.environment->drawIntroPerformers(renderer,cameraFrame,tick);
  // Original DemoJugemu activation (807758b0..bc / 80775998..a4)
  // starts this sequence once, rather than holding/restarting it each frame.
  if(jugemu&&!jugemuWasVisible)sound.play(snd::id::SE_DEMO_JUGEM,2);
  else if(!jugemu&&jugemuWasVisible)sound.stopSlot(2,0);
  jugemuWasVisible=jugemu;
  if(assets.cast)assets.cast->drawActors(renderer,results.teams,StandAnchor);
  if(assets.parade)assets.parade->draw(renderer);
  if(driving)assets.drive->draw(renderer);
  renderer.setCourseLighting(false);
  assets.environment->drawEffects(renderer,effectCamera.view.data());
  if(driving)assets.drive->drawEffects(renderer,effectCamera.view.data());
  renderer.applyBloom();
  renderer.markIntroWorldEnd();
  if((phase==Phase::Board||phase==Phase::Leaving)&&assets.trophy){
   auto& trophy=*assets.trophy;
   trophy.model->update(trophy.motion.frame(),tick);
   trophy.draw(renderer);
  }
  session.ui.draw(renderer);
  float fade=phase==Phase::Drive||phase==Phase::Intro||phase==Phase::SkipIn?colorFaderOpacity(float(frame),true):0.f;
  if(phase==Phase::DriveOut||phase==Phase::SkipOut||phase==Phase::FadeOut)fade=colorFaderOpacity(float(frame),false);
  if(fade>0)renderer.drawScreenFade(fade,false);
 heartbeat.at("end");
 renderer.end();
 heartbeat.drawn();heartbeat.at("record");
 if(fade<1.f){
  if(!entryRecorded){recordLoad("ceremony",entryStart);entryRecorded=true;}
  if(podiumStart&&!podiumRecorded){recordLoad("podium",podiumStart);podiumRecorded=true;}
 }
 }
 return AwardSceneResult::Exit;
}
}
