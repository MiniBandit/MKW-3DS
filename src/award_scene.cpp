#include <mkw/award_lighting.hpp>
#include <mkw/award_scene.hpp>
#include <mkw/loading_screen.hpp>
#include <mkw/award_scene_assets.hpp>
#include <mkw/award_camera_data.hpp>
#include <mkw/award_ui.hpp>
#include <mkw/color_fader.hpp>
#include <mkw/presentation_cadence.hpp>
#include <mkw/renderer.hpp>
#include <mkw/sound_ids.hpp>
#include <mkw/sound_system.hpp>
#include <algorithm>
#include <cstdio>

namespace mkw {
namespace {
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
}
AwardSceneResult runAwardPodium(Renderer& renderer,const AwardResults& results,void (*report)(const char*),std::uint64_t transitionStart,Kinoko::EGG::SceneManager* scenes){
 const auto entryStart=transitionStart?transitionStart:svcGetSystemTick();
 std::uint64_t podiumStart=0;bool entryRecorded=false,podiumRecorded=false;
 // Write after the first non-black GPU frame, never as on-screen loading text.
 auto recordLoad=[&](const char* stage,std::uint64_t started){
#ifndef MKW_RELEASE
  renderer.waitForGpu();
  const double ms=double(svcGetSystemTick()-started)*1000.0/SYSCLOCK_ARM11;
  if(FILE* file=std::fopen("sdmc:/3ds/mkw-3ds/award-load-times.jsonl","a")){
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
  if(!aptMainLoop())return AwardSceneResult::Exit;
  black(renderer,resourceWait);
 }
 if(!session.assets->loaded){report(session.assets->detail());report("FAIL ceremony resources");return AwardSceneResult::Failed;}
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
  // AwardResults::BeforeEntranceAnimations dispatches AwardCupModel::Activate.
  if(auto* trophy=session.assets->trophy.get())sound.play(trophy->show());
 };
 auto previous=svcGetSystemTick();double pending=0;
 PresentationCadence presentation;presentation.setTicksPerSlot(2);
 while(aptMainLoop()){
  hidScanInput();bool advance=(hidKeysDown()&(KEY_A|KEY_B|KEY_START))!=0;
  const auto now=svcGetSystemTick();pending+=std::min(0.1,double(now-previous)/SYSCLOCK_ARM11)*60.;previous=now;
  // Handle edges once per display refresh; simulation may owe zero or several ticks.
  // AwardFade::HandleClick (80621408..24) accepts a skip after tick70,
  // only while the first camera is before frame400 (805b4888..48a8).
  if(advance&&phase==Phase::Drive&&frame>=70){phase=Phase::DriveOut;frame=0;}
  else if(advance&&podium&&phase==Phase::Intro&&frame>=70&&frame<400){
   skipFrame=frame;phase=Phase::SkipOut;frame=0;sound.stopSlot(1,30);
  }
  else if(advance&&phase==Phase::Board&&frame>=180&&session.ui.boardTransitionFinished()){
   session.ui.showCongratulations(false);session.ui.leaveBoard();
   if(auto* trophy=session.assets->trophy.get())trophy->leave();
   phase=Phase::Leaving;frame=0;
  }
  while(pending>=1){
   pending-=1;++tick;++frame;
   if(const auto id=session.ui.step())sound.play(id);
   if(phase==Phase::Drive){
    session.assets->drive->step();
    if(session.assets->drive->finished()){phase=Phase::DriveOut;frame=0;}
   }else if(phase==Phase::DriveOut&&frame>=ColorFadeOutLastFrame){
    podiumStart=svcGetSystemTick();
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
  if(!presentation.due(tick)){svcSleepThread(1000000);continue;}
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
 renderer.end();
 if(fade<1.f){
  if(!entryRecorded){recordLoad("ceremony",entryStart);entryRecorded=true;}
  if(podiumStart&&!podiumRecorded){recordLoad("podium",podiumStart);podiumRecorded=true;}
 }
 }
 return AwardSceneResult::Exit;
}
}
