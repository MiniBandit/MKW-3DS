#include <mkw/mii_voice.hpp>
#include <mkw/notification_led.hpp>
#include <game/kart/KartAction.hh>
#include <mkw/circle_pad.hpp>
#include <mkw/gyro_trick.hpp>
#include <mkw/native_mii_portrait.hpp>
#include <mkw/native_mii_head.hpp>
#include <mkw/mii_body.hpp>
#include <mkw/mii_garage.hpp>
#include <mkw/course_effect_roots.hpp>
#include <game/field/CollisionDirector.hh>
#include <mkw/driver_item_signals.hpp>
#include <mkw/driver_look_data.hpp>
#include <mkw/driver_look_gate.hpp>
#include <mkw/sound_ids.hpp>
#include <mkw/license_times.hpp>
#include <mkw/license_records.hpp>
#include <mkw/license_save.hpp>
#include <mkw/mii_portrait.hpp>
#include <3ds.h>
#include <mkw/renderer.hpp>
#include <mkw/course_objects.hpp>
#include <mkw/course_catalog.hpp>
#include <mkw/qa.hpp>
#include <mkw/animation.hpp>
#include <mkw/driver_controller.hpp>
#include <mkw/driver_effect_anchor.hpp>
#include <mkw/roster.hpp>
#include <mkw/garage.hpp>
#include <mkw/vehicle_animation.hpp>
#include <mkw/luma_visual.hpp>
#include <mkw/race_grid.hpp>
#include <mkw/balloon_battle.hpp>
#include <mkw/battle_round_result.hpp>
#include <mkw/balloon_battle_runtime.hpp>
#include <mkw/race_intro.hpp>
#include <mkw/intro_mesh_clearance.hpp>
#include <mkw/race_camera.hpp>
#include <mkw/race_chase_camera.hpp>
#include <mkw/race_events.hpp>
#include <mkw/race_flow_ui.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/kart_start_effects.hpp>
#include <mkw/kart_smoke_effects.hpp>
#include <mkw/kart_surface_effects.hpp>
#include <mkw/creature_effects.hpp>
#include <mkw/blooper_screen_effects.hpp>
#include <mkw/race_results.hpp>
#include <mkw/award_results.hpp>
#include <mkw/award_scene.hpp>
#include <mkw/staff_scene.hpp>
#include <mkw/staff_entry.hpp>
#include <mkw/gp_telemetry.hpp>
#include <mkw/trick_presentation.hpp>
#include <mkw/race_items.hpp>
#include <mkw/star_power.hpp>
#include <mkw/job_worker.hpp>
#include <mkw/animation_budget.hpp>
#include <mkw/render_quality.hpp>
#include <mkw/racer_visuals.hpp>
#include <mkw/race_hud.hpp>
#include <mkw/race_bottom_screen.hpp>
#include <mkw/lakitu.hpp>
#include <mkw/course_bloom.hpp>
#include <mkw/course_lighting.hpp>
#include <game/kart/KartCollide.hh>
#include <game/kart/BattleEvents.hh>
#include <mkw/coin_battle_runtime.hpp>
#include <mkw/battle_session.hpp>
#include <mkw/race_effects.hpp>
#include <mkw/resource_load.hpp>
#include <mkw/loading_screen.hpp>
#include <mkw/kart_item_effects.hpp>
#include <mkw/kart_effect_lod.hpp>
#include <mkw/kart_wall_contact.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/player_shadow.hpp>
#include <mkw/castle_regions.hpp>
#include <mkw/camera_shake.hpp>
#include <mkw/lightning_flash.hpp>
#include <mkw/vehicle_exhaust.hpp>
#include <mkw/opponent_effects.hpp>
#include <mkw/bike_drift_effects.hpp>
#include <mkw/bike_scrape_effects.hpp>
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_vehicle_icons.hpp>
#include <mkw/menu_renderer.hpp>
#include <mkw/menu_models.hpp>
#include <mkw/menu_movie_audio.hpp>
#include <mkw/menu_audio_assets.hpp>
#include <mkw/menu_sound.hpp>
#include <mkw/race_sound.hpp>
#include <mkw/sound_system.hpp>
#include <mkw/runtime_profile.hpp>
#include <mkw/presentation_cadence.hpp>
#include <mkw/menu_autopilot.hpp>
#include <mkw/menu_profile.hpp>
#include <mkw/asset_loader.hpp>
#include <mkw/asset_pack.hpp>
#include <game/kart/Status.hh>
#include <abstract/File.hh>
#include <egg/core/ExpHeap.hh>
#include <egg/core/SceneManager.hh>
#include <host/SceneCreatorDynamic.hh>
#include <host/KTestSystem.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/ResourceManager.hh>
#include <game/system/KPadDirector.hh>
#include <game/render/KartCamera.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartSuspensionPhysics.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartPhysics.hh>
#include <game/kart/KartJump.hh>
#include <game/field/ObjectDirector.hh>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>

// Heap split sized for the smallest launch mode, Old 3DS (64 MB application
// region): LINEAR 32 MiB leaves about 29 MiB of ordinary heap there. Kinoko's
// 16 MiB arena lives in that heap, so the racers' animation banks (about
// 11 MiB) are stored in LINEAR (animation.cpp); a race then peaks near 21 MiB
// of heap and 25 MiB of LINEAR. Menu texture residency adapts to the free
// LINEAR space. A New 3DS's larger region goes to the heap. The emulator gives
// homebrew 96 MB, so it cannot show an Old 3DS shortage.
extern "C" u32 __ctru_linear_heap_size=32u<<20;
// libctru carves the main thread's stack from the start of that heap (initSystem), 32 KiB
// by default. main's frame alone is about 26 KiB, so the menu loop's callbacks overran it
// into unmapped memory below 0x08000000; 64 KiB costs 32 KiB of ordinary heap.
extern "C" u32 __stacksize__=64u<<10;

namespace K = Kinoko;
static constexpr const char* DATA = "sdmc:/3ds/mkw-3ds";
namespace mkw {extern std::atomic<unsigned> fileOpens;extern std::atomic<unsigned long long> fileOpenTicks;}
static bool exists(const char* p) { FILE* f=fopen(p,"rb"); if(!f)return false; fclose(f);return true; }
static K::u8* replayData=nullptr;
using mkw::Courses;
using mkw::CourseCount;
static unsigned courseChoice=0,driverChoice=0,vehicleChoice=0;
static bool fullRace=false,automaticDrift=false;
static unsigned selectedEngineClass=2,selectedRaceMode=1;
static bool selectedMirror=false,selectedTeams=false;
static mkw::RaceTeam selectedHumanTeam=mkw::RaceTeam::Red;
static unsigned selectedCpu=1,selectedCpuVehicles=0,selectedItems=0;
// Time Trials: one racer, no item boxes, a Triple Mushroom at the start, and
// the best finish per course kept on the SD card (milliseconds, 0 = none).
static bool timeTrial=false;
static bool battleMode=false,coinRunnersMode=false;
static unsigned battleTicks=0;
static mkw::BalloonBattle balloonBattle;
static mkw::BalloonBattleRuntime balloonRuntime;
static mkw::CoinBattleRuntime coinBattle;
static unsigned battleScore(unsigned racer){return coinRunnersMode?coinBattle.score(racer):balloonBattle.score(racer);}
static unsigned battleRank(unsigned racer){return coinRunnersMode?coinBattle.rank(racer):balloonBattle.rank(racer);}
static mkw::CourseBestTimes bestTimes{};
static unsigned activeLicense=0;
static mkw::LicenseRecords activeRecords{};
static std::array<mkw::LicenseSave,4> licenseSaves{};
static std::array<bool,4> damagedLicenses{};
static constexpr const char* LicenseDirectory="sdmc:/3ds/mkw-3ds";
struct LicenseSelectionSaveJob final:mkw::AssetJob {
    unsigned slot=0;mkw::LicenseSave value;bool ok=false;
    void run()override{ok=mkw::writeLicenseSave(LicenseDirectory,slot,value);}
};
static bool persistLicense(unsigned slot,mkw::LicenseSave& save){
    mkw::LoadingScope loading;
    return mkw::writeLicenseSave(LicenseDirectory,slot,save);
}
class GyroTrickDevice {
public:
    GyroTrickDevice(){
        float coefficient=0;
        if(R_SUCCEEDED(HIDUSER_EnableGyroscope())){
            ready_=true;
            if(R_SUCCEEDED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&coefficient))&&
               std::isfinite(coefficient)&&coefficient>0)
                scale_=coefficient>1.f?1.f/coefficient:coefficient;
            else{HIDUSER_DisableGyroscope();ready_=false;}
        }
    }
    ~GyroTrickDevice(){if(ready_)HIDUSER_DisableGyroscope();}
    void reset(){input_.reset();}
    void read(bool active){
        if(!ready_||!active){reset();return;}
        angularRate rate{};hidGyroRead(&rate);
        // libctru angularRate: y=Pitch, x=Roll, z=Yaw. Turning in place
        // (Yaw) is deliberately excluded from the steering-wheel gesture.
        input_.sample(rate.y*scale_,rate.x*scale_);
    }
    K::System::Trick step(){return K::System::Trick(input_.step());}
private:
    mkw::GyroTrick input_;float scale_=0;bool ready_=false;
};
static void loadBestTimes(){bestTimes=licenseSaves[activeLicense].times;}
static bool saveBestTimes(){licenseSaves[activeLicense].times=bestTimes;return persistLicense(activeLicense,licenseSaves[activeLicense]);}
static bool courseAvailable(unsigned i){return Courses[i].supported&&exists("Race/Common.szs")&&exists(Courses[i].archive)&&exists(Courses[i].models)&&(!*Courses[i].sky||exists(Courses[i].sky));}
static void showControls(bool ready){
    consoleClear();printf("MKW/ MARIO KART RECOMPILED 3DS\n%s / manual drift\n",Courses[courseChoice].name);
    printf("3DS CONTROLS\nCircle Pad: steer  A: accelerate\nB: brake  R: hop/drift  L: use item\nD-pad: trick / bike wheelie\nSTART: maps/pause  SELECT: restart\nX/Y: character  B+X/Y: vehicle\n");
    if(!ready)printf("Missing selected course data.\n");
}
static mkw::NativeMii selectedRaceMii{};
static uint8_t selectedMiiVoice=0;
static mkw::MiiBodySpec selectedMiiBody{};
static unsigned selectedGarageIndex(){return driverChoice<24?driverChoice*mkw::VehiclesPerDriver+vehicleChoice:unsigned(mkw::GarageChoices.size())+mkw::miiGarageOffset(selectedMiiBody.retailCharacter)+vehicleChoice;}
static const auto& selectedVehicle(){return mkw::raceGarageChoice(selectedGarageIndex());}
static mkw::CpuRoster selectedRoster;
static std::array<uint8_t,12> selectedStartPositions{};
static void configure(K::System::RaceConfig* config,void* context) {
    if(replayData){config->setGhost(replayData);config->raceScenario().players[0].type=K::System::RaceConfig::Player::Type::Ghost;return;}
    auto& s=config->raceScenario();s.course=Courses[courseChoice].id;
    s.engineClass=uint8_t(selectedEngineClass);s.mirror=selectedMirror;
    s.cpuDifficulty=uint8_t(selectedCpu);s.cpuVehicles=uint8_t(selectedCpuVehicles);s.itemMode=uint8_t(selectedItems);
    s.gameMode=static_cast<K::System::RaceConfig::Scenario::GameMode>(selectedRaceMode);
    s.teams=selectedTeams;s.humanTeam=uint8_t(selectedHumanTeam);s.battleType=coinRunnersMode?1:0;
    if(fullRace){static_cast<mkw::RaceGrid*>(context)->configure(*config,selectedGarageIndex(),timeTrial?1:12,selectedRoster.ready?&selectedRoster.choices:nullptr);for(unsigned i=0;i<s.playerCount;++i)s.players[i].startPosition=selectedStartPositions[i];s.players[0].driftIsAuto=automaticDrift;return;}
    auto& p=s.players[0];p.type=K::System::RaceConfig::Player::Type::Local;
    const auto& driver=selectedVehicle();
    p.character=static_cast<K::Character>(driver.character);p.vehicle=static_cast<K::Vehicle>(driver.vehicle);p.driftIsAuto=false;
}
static mkw::Vec3 vec(const K::EGG::Vector3f& v){return {v.x,v.y,v.z};}
static char resourceFailure[96]{};
static void recordResourceFailure(const char* text){
    if(std::strncmp(text,"FAIL ",5))return;
    std::snprintf(resourceFailure,sizeof resourceFailure,"%s",text+5);
    if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/race-load-errors.txt","a")){
        const auto heap=mallinfo();
        fprintf(f,"%s; linear=%lu vram=%lu heap=%lu/%lu unused=%lu\n",text,(unsigned long)linearSpaceFree(),(unsigned long)vramSpaceFree(),(unsigned long)heap.uordblks,(unsigned long)envGetHeapSize(),(unsigned long)heap.fordblks);fclose(f);
    }
}
// Release builds (CIA or SD-backed 3DSX): no test hooks or general runtime log;
// with kQa false the compiler drops those branches and their text.
#ifdef MKW_RELEASE
static constexpr bool kQa=false;
static inline void report(const char* text){recordResourceFailure(text);}
#define LOGF(...) do{}while(0)
#else
static constexpr bool kQa=true;
static void report(const char* text) {
    recordResourceFailure(text);
    if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/runtime.log","a")){fprintf(f,"%s\n",text);fclose(f);}
}
#define LOGF(...) do{char logLine_[256];snprintf(logLine_,sizeof logLine_,__VA_ARGS__);report(logLine_);}while(0)
#endif
// Narrow pause regression trace, written only at a START/menu transition.
// Keep the last transition even in release packages; no per-frame SD writes.
static void recordPauseStage(const char* stage,bool begin=false){
#ifndef MKW_RELEASE
    if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/pause-transition.txt",begin?"w":"a")){
        fprintf(f,"HW87 %s\n",stage);fclose(f);
    }
#endif
}
// QA captures: a stereo frame also writes its right eye beside the left
// (name.ppm -> name-right.ppm).
static void saveRightEye(mkw::Renderer& renderer,const char* path){
    if(!renderer.stereoShown())return;
    std::vector<uint8_t> pixels(400*240*3);std::string right(path);
    const auto dot=right.rfind(".ppm");if(dot==std::string::npos)return;
    right.insert(dot,"-right");
    // Each eye's image must be what its LCD framebuffer received.
    bool presented=renderer.captureTop(pixels.data(),0)&&renderer.presentedMatches(pixels.data(),0);
    if(renderer.captureTop(pixels.data(),1)){
        presented&=renderer.presentedMatches(pixels.data(),1);
        if(FILE* f=fopen(right.c_str(),"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
    }else presented=false;
    if(!presented){std::string line="FAIL stereo presentation ";line+=path;report(line.c_str());}
}
// Stereo menus (docs/STEREO_3D.md), Mario Kart 7's garage: the backdrop pane
// (quad 0) at the garage's back wall behind the selection models, enlarged
// about the screen centre by its shift so neither eye bares an edge; the
// models in front of it; the other panes on the screen plane.
static void drawMenuBackdrop(mkw::Renderer& renderer,mkw::MenuRenderer& menuRenderer,const std::vector<mkw::UiQuad>& quads){
    const float parallax=mkw::MenuModels::backdropParallax(renderer);
    if(parallax==0||quads.empty()){menuRenderer.draw(renderer,quads,false,0,1);return;}
    static std::vector<mkw::UiQuad> backdrop(1);
    backdrop[0]=quads[0];auto& q=backdrop[0];
    if(q.x<=0&&q.x+q.w>=400){
        const float grow=(400.f+std::fabs(parallax))/400.f;
        q.x=200.f+(q.x-200.f)*grow;q.y=120.f+(q.y-120.f)*grow;q.w*=grow;q.h*=grow;
    }
    renderer.setScreenDepth(parallax);
    menuRenderer.draw(renderer,backdrop,false,0,1);
    renderer.setScreenDepth(renderer.stereoLayer(mkw::stereo::PaneLayer));
}
// Menu content is composed once, then its semantic groups are independently
// laid out for the preview LCD and the lower selection LCD.
static bool menuOnBottom(mkw::UiPage page){
    // Opaque pause pages keep the upper LCD until the renderer can retain
    // the paused race image independently of the released scenery meshes.
    return page!=mkw::UiPage::Strap&&page!=mkw::UiPage::Intro&&
           page!=mkw::UiPage::Pause&&page!=mkw::UiPage::QuitConfirm;
}
// Runs a frame-local lambda as a JobWorker job.
template<class F> void runJob(void* f){(*static_cast<F*>(f))();}
static int startupFailure(const char* message){
    consoleInit(GFX_BOTTOM,nullptr);
    printf("MKW startup failed\n%s\n\n",message);
    printf("3DSX needs SD resources in:\n/3ds/mkw-3ds/\n\nAzahar: set the virtual SD root\nto the packaged build/playable\ndirectory.\n\nPress B or START to exit.\n");
    while(aptMainLoop()){
        hidScanInput();
        if(hidKeysDown()&(KEY_B|KEY_START))break;
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    gfxExit();return 1;
}
// Source DrawXlu surfaces do not write depth. Submit them after opaque
// actors: Koopa Cape's tunnel must cover morays, and Coconut Mall's glass
// floor must composite over the submerged palm trunks like the scenery.
// Context is copied by Renderer.
static void drawCourseLayer(mkw::Renderer& renderer,const mkw::Model& model,
                            const C3D_Mtx& transform,int course,bool transparent,bool roadOverlay=false){
    (void)course; // Every course uses the same authored DrawOpa/DrawXlu split.
    struct Context {const mkw::Model* model;bool transparent,roadOverlay;};
    const Context context{&model,transparent,roadOverlay};
    renderer.draw(model,transform,false,nullptr,context,[](void* data,std::size_t i){
        const auto& c=*static_cast<const Context*>(data);
        const auto& sub=c.model->submeshes[i];
        const bool authoredEarly=sub.lighting&(mkw::retail::AuthoredOpaqueBlend|(1u<<27));
        if(authoredEarly)return c.transparent&&c.roadOverlay;
        const bool overlay=sub.lighting&(mkw::retail::CoplanarLayer|mkw::retail::RaisedRoadLayer);
        return bool(sub.flags&2)==c.transparent&&(!c.transparent||overlay==c.roadOverlay);
    });
}

int main() {
    osSetSpeedupEnable(true); // Supported New 3DS CPU/L2 mode; simulation remains fixed at 60 Hz.
    gfxInitDefault();
    mkw::NotificationLed notificationLed;
    // Saves are writable SD data for both CIA and external-resource 3DSX.
    mkdir("sdmc:/3ds",0777);mkdir(LicenseDirectory,0777);
    if(kQa)mkdir(DATA,0777);
    if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/runtime.log","w")){fputs("MKW ARM11 startup\n",f);fclose(f);}
    // Embedded assets are read in place; writable diagnostics stay on SD.
    struct AssetMount {
        bool mounted=R_SUCCEEDED(romfsInit());
        ~AssetMount(){mkw::pack::close();if(mounted)romfsExit();}
    } assets;
    // Packaging, not QA/release mode, determines where the assets live.
    // CIA has embedded RomFS; the ordinary 3DSX uses its matching SD package.
    const char* assetRoot=assets.mounted?"romfs:/":DATA;
    report(assets.mounted?"assets: embedded RomFS":"assets: external SD package");
    if(chdir(assetRoot)!=0||!exists("Race/Common.szs")){
        report("FAIL asset directory or Race/Common.szs");
        return startupFailure("Game resources could not be found.");
    }
    // ARM11 VFP: round-to-nearest, flush denormals. Keep Kinoko's explicit Wii
    // arithmetic and disable compiler FP contraction; ghost trace is the oracle.
    unsigned fpscr;asm volatile("vmrs %0, fpscr":"=r"(fpscr));
    fpscr=(fpscr&~(3u<<22))|(1u<<24);asm volatile("vmsr fpscr, %0"::"r"(fpscr));
    void* memory=memalign(32,K::MEMORY_SPACE_SIZE);
    if(!memory){report("FAIL simulation heap reservation");return startupFailure("Simulation memory allocation failed.");}
    auto* heap=K::EGG::ExpHeap::create(memory,K::MEMORY_SPACE_SIZE,K::DEFAULT_OPT);
    if(!heap){report("FAIL simulation heap create");return startupFailure("Simulation heap creation failed.");}
    heap->becomeCurrentHeap();K::EGG::SceneManager::SetRootHeap(heap);
    char line[256];LOGF("heap_reserved=%lu heap_free=%lu fpscr=%08x",(unsigned long)K::MEMORY_SPACE_SIZE,(unsigned long)heap->getAllocatableSize(),fpscr);
    K::EGG::Vector3f test(3,4,0);
    LOGF("math_length=%.9f bits=%08lx",test.length(),(unsigned long)K::f2u(test.length()));
    if(kQa)setenv("KINOKO_FILESYSTEM_ROOT",assetRoot,1);
    bool qaMode=false;if(kQa)for(const char* test:{"trace","smoke","controls","replay","effects","animation","roster","garage","garage_controls","menu","objects"}){char marker[64];snprintf(marker,sizeof marker,"sdmc:/3ds/mkw-3ds/%s-test.txt",test);qaMode|=exists(marker);}
    fullRace=!qaMode;
    GyroTrickDevice gyroTrick;
    // consoleInit changes the bottom LCD to RGB565 and disables double buffering.
    // The normal dual-screen renderer transfers RGB8 frames, so only QA modes
    // that actually use the text console may take ownership of that screen.
    if(qaMode&&!exists("sdmc:/3ds/mkw-3ds/menu-test.txt"))consoleInit(GFX_BOTTOM,nullptr);
    auto profiler=std::make_unique<mkw::RuntimeProfile>();
    using Profile=mkw::RuntimeProfile;
    auto autopilot=std::make_unique<mkw::MenuAutopilot>();
    const bool autoplay=kQa&&!qaMode&&autopilot->open("sdmc:/3ds/mkw-3ds/menu-autoplay.txt");
    // race-capture.txt holds the sampling interval in simulation steps (default 20);
    // a second nonzero number also captures every presented frame from the
    // end-of-race menu's choice until the race is left (its fade to black).
    unsigned raceCaptureEvery=0,raceCaptured=~0u;bool raceCaptureLeave=false;
    if(autoplay)if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/race-capture.txt","r")){unsigned n=0,leave=0;const int got=fscanf(f,"%u %u",&n,&leave);raceCaptureEvery=got>=1&&n>1?n:20;raceCaptureLeave=got==2&&leave;fclose(f);}
    const bool raceCapture=raceCaptureEvery>0;
    auto menuProfile=std::make_unique<mkw::MenuProfile>();
    if(autoplay||(kQa&&exists("sdmc:/3ds/mkw-3ds/menu-performance.txt")))menuProfile->enable();
    using MenuStage=mkw::MenuProfile::Stage;
    uint64_t raceLoadStart=0;
    // QA: race load stage timing with file-open counts.
    uint64_t stageTick=0;unsigned stageOpens=0;unsigned long long stageOpenTicks=0;
    auto stage=[&](const char* name){
        const auto now=svcGetSystemTick();const unsigned opens=mkw::fileOpens.load();const auto openTicks=mkw::fileOpenTicks.load();
        if(stageTick&&menuProfile->enabled()){char text[160];snprintf(text,sizeof text,"load %s %.1f ms, %u opens (%.1f ms opening)",name,double(now-stageTick)*1000.0/SYSCLOCK_ARM11,opens-stageOpens,double(openTicks-stageOpenTicks)*1000.0/SYSCLOCK_ARM11);menuProfile->event(text);report(text);}
        stageTick=now;stageOpens=opens;stageOpenTicks=openTicks;
    };
    auto finishAutoplay=[&](){
        if(!menuProfile->enabled())return;
        menuProfile->write("sdmc:/3ds/mkw-3ds/menu-performance.csv");
        if(autoplay)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/menu-autoplay-result.json","w")){fprintf(f,"{\"pass\":%s,\"races\":%u,\"failure\":\"%s\"}\n",autopilot->passed()?"true":"false",autopilot->racesStarted(),autopilot->failure());fclose(f);}
    };
    // Persistent renderer state must not consume the ARM11 main stack.
    auto rendererStorage=std::make_unique<mkw::Renderer>();
    auto& renderer=*rendererStorage;
    if(!renderer.init()){report("FAIL Citro3D initialization");return startupFailure("Citro3D initialization failed.");}
    // The startup save read also needs an RGB8 lower LCD (the debug console
    // otherwise leaves RGB565 active until the first menu).
    if(!renderer.enableBottom()){renderer.shutdown();return startupFailure("Lower LCD initialization failed.");}
    mkw::initializeLoadingScreen(renderer);
    // Stereo 3D (docs/STEREO_3D.md): New 3DS with a 3D screen only. QA can pin
    // the slider with stereo-slider.txt (0..1) for lockstep captures.
    if(renderer.stereoCapable()){
        float slider=-1;
        if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/stereo-slider.txt","r")){if(fscanf(f,"%f",&slider)!=1)slider=-1;fclose(f);}
        renderer.setSliderOverride(slider);
        LOGF("stereo capable slider_override=%.2f",slider);
    }else report("stereo unavailable (2D model)");
    // Second-core presentation jobs (racer posing); none run while the race steps.
    if(!mkw::jobWorker().start())report("job worker unavailable: presentation jobs run on core 0");
    // Race frames draw on the second core one frame behind their recording
    // (src/render_thread.cpp); menus draw immediately.
    if(!renderer.startRenderThread())report("render thread unavailable: race frames draw on core 0");
    mkw::MenuState menu;
    menu.titleRandom=std::uint32_t(svcGetSystemTick())|1u;
    menu.cpuRandom.state=std::uint32_t(svcGetSystemTick());
    mkw::LicenseLoad loaded;
    {mkw::LoadingScope loading;loaded=mkw::loadLicenseSaves(LicenseDirectory,licenseSaves);
    for(unsigned slot=0;slot<4;++slot){
        auto& save=licenseSaves[slot];
        damagedLicenses[slot]=loaded==mkw::LicenseLoad::Damaged;
        if(loaded==mkw::LicenseLoad::Missing){
            const bool times=mkw::loadLicenseTimes(LicenseDirectory,slot,save.times);
            const bool records=mkw::loadLicenseRecords(LicenseDirectory,slot,save.records);
            save.occupied=times||records;
        }
        menu.licenseBestTimes[slot]=save.times;menu.licenseOccupied[slot]=save.occupied;menu.licenseCups[slot]=save.cups;menu.licenseNames[slot]=save.name;menu.licenseUnlocks[slot]=save.unlockFlags;menu.saveFailed|=damagedLicenses[slot];
    }
    }
    {unsigned last=0;FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/last-license.txt","r");
     if(f){if(std::fscanf(f,"%u",&last)!=1)last=0;std::fclose(f);}
     if(last>=4||!menu.licenseOccupied[last]){last=0;for(unsigned i=0;i<4;++i)if(menu.licenseOccupied[i]){last=i;break;}}
     // Load the selected profile before any strap/title input can reach
     // the shared menu autosave path. Rules must never be replaced by UiSelection defaults.
     if(licenseSaves[last].occupied)menu.selection=licenseSaves[last].settings;
     else menu.selection.license=last;}
    mkw::MenuRenderer menuRenderer;
    struct LicensePortraitCache {mkw::NativeMii identity{};bool checked=false;std::vector<std::uint8_t> rgba;};
    std::array<LicensePortraitCache,4> licensePortraits;
    auto publishLicensePortraits=[&](const std::vector<mkw::UiQuad>& visible){
        std::array<bool,4> needed{};
        for(const auto& q:visible)if(q.licensePortrait<4)needed[q.licensePortrait]=true;
        for(unsigned slot=0;slot<4;++slot){
            auto& cached=licensePortraits[slot];const auto& save=licenseSaves[slot];
            if(!save.occupied||cached.identity!=save.nativeMii){
                menuRenderer.clearLicensePortrait(renderer,slot);cached.rgba.clear();cached.checked=false;cached.identity=save.nativeMii;
            }
            if(!needed[slot]||!save.occupied)continue;
            if(!cached.checked){
                renderer.waitForGpu();cached.checked=true;
                mkw::composeNativeMiiPortrait(save.nativeMii,128,cached.rgba);
                if(cached.rgba.empty())report("Native Mii portrait unavailable: CFL resource or model failure");
            }
            if(!cached.rgba.empty()&&!menuRenderer.hasLicensePortrait(slot)){
                renderer.waitForGpu();menuRenderer.setLicensePortrait(renderer,slot,128,128,cached.rgba.data(),cached.rgba.size());
            }
        }
    };
    // CFL portrait baking is synchronous GPU work. Finish all saved-license
    // portraits before the opening/title is shown, not during its cross-LCD exit.
    {
        mkw::LoadingScope loading;
        std::vector<mkw::UiQuad> portraits(licenseSaves.size());
        for(unsigned slot=0;slot<portraits.size();++slot)portraits[slot].licensePortrait=slot;
        publishLicensePortraits(portraits);
    }
    // Original animation banks keep palette workspaces; they must not occupy
    // the 3DS main thread's small stack alongside the race renderer.
    auto menuModelStorage=std::make_unique<mkw::MenuModels>();
    auto& menuModels=*menuModelStorage;
    for(unsigned slot=0;slot<licenseSaves.size();++slot)menuModels.setIdentity(renderer,slot,slot==menu.selection.license&&menu.selection.miiSelected?selectedRaceMii:licenseSaves[slot].nativeMii);
    mkw::MenuMovieAudio menuAudio;
    mkw::MenuVehicleIcons menuIcons;
    mkw::UiRaceView uiRace;
    // Wii presentation is confined to the upper LCD. Keep both lower buffers
    // black rather than allocating a second GPU render target.
    if(fullRace){
        for(unsigned i=0;i<2;++i){
            u16 w=0,h=0;auto* framebuffer=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
            if(framebuffer)std::memset(framebuffer,0,size_t(w)*h*3);
            gfxSwapBuffers();
        }
        gfxFlushBuffers();
    }
    hidSetRepeatParameters(24,6); // Source menus repeat directions; A/B are edge-triggered.
    if(fullRace){
        mkw::MenuMovieAudio::initialize();
        // Retail sound engine (docs/AUDIO_SOURCE.md); silent when the DSP is unavailable.
        const bool sound=mkw::SoundSystem::instance().start();
        LOGF("sound: %s linear_free=%lu",sound?"engine running":mkw::MenuMovieAudio::dspReady()?"archive unavailable, silent":"DSP unavailable, silent",(unsigned long)linearSpaceFree());
    }
    // Preview fixture used by the explicit one-shot file launcher.
    // The fixture is ephemeral: no race results or license progress are saved.
    K::EGG::SceneManager* menuAwardScenes=nullptr;
    auto previewAward=[&](unsigned rank,K::EGG::SceneManager* existingScenes=nullptr){
        mkw::AwardResults preview;preview.valid=true;preview.count=12;preview.mode=0;
        preview.cup=0;preview.engineClass=2;preview.grade=rank==1?6:0;
        for(unsigned i=0;i<preview.count;++i){
            const auto& choice=mkw::GarageChoices[i*mkw::VehiclesPerDriver];
            preview.players[i]={uint8_t(i),uint8_t(choice.character),uint8_t(choice.vehicle),i+1==rank,mkw::RaceTeam::None,60-i*4};
        }
        mkw::pack::open();
        // Preview has not created the normal root yet. Bootstrap its services
        // with the ceremony archive, then leave the root for AwardPhysicsScene.
        const auto previewStart=svcGetSystemTick();
        K::Host::SceneCreatorDynamic previewCreator;
        K::EGG::SceneManager localPreviewScenes(&previewCreator);
        auto& previewScenes=existingScenes?*existingScenes:localPreviewScenes;
        if(!existingScenes&&mkw::hasAwardPodium(preview)){
            K::System::RaceConfig::RegisterInitCallback([](K::System::RaceConfig* cfg,void*){
                auto& s=cfg->raceScenario();s.course=K::Course::Win_Demo;s.playerCount=1;
                s.gameMode=K::System::RaceConfig::Scenario::GameMode::Awards;
                s.players[0].type=K::System::RaceConfig::Player::Type::CPU;
            },nullptr);
            {mkw::LoadingScope loading;previewScenes.changeScene(0);}
            previewScenes.currentScene()->heap()->enableAllocation();
            previewScenes.destroyCurrentSceneNoIncoming(false);
            K::System::RaceConfig::RegisterInitCallback(nullptr,nullptr);
        }
        if(existingScenes&&previewScenes.currentSceneId()==2){
            previewScenes.currentScene()->heap()->enableAllocation();
            previewScenes.destroyCurrentSceneNoIncoming(false);
        }
        const auto outcome=mkw::runAwardPodium(renderer,preview,report,previewStart,&previewScenes);
        // A title preview owns its temporary root; restore all singleton services
        // before the normal first race creates its own root. AwardPhysicsScene
        // has already retired its child on return from runAwardPodium.
        if(!existingScenes)while(previewScenes.currentScene()){
            previewScenes.currentScene()->heap()->enableAllocation();
            previewScenes.destroyCurrentSceneNoIncoming(true);
        }
        return outcome;
    };
    // One-shot manual ceremony test. Consumed before entry.
    if(fullRace)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/award-preview.txt","r")){
        unsigned rank=1;const int parsed=fscanf(f,"%u",&rank);fclose(f);
        if(parsed!=1)rank=1;rank=std::clamp(rank,1u,12u);
        std::remove("sdmc:/3ds/mkw-3ds/award-preview.txt");
        const auto outcome=previewAward(rank);
        mkw::jobWorker().stop();mkw::pack::close();
        mkw::SoundSystem::instance().stop();mkw::MenuMovieAudio::shutdown();
        mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();
        return outcome==mkw::AwardSceneResult::Failed?1:0;
    }
    static mkw::MenuSound menuSound;
    static mkw::RaceSound raceSound;
    static mkw::RaceAudioFrame actors;
    mkw::menuRigidModels=!(kQa&&exists("sdmc:/3ds/mkw-3ds/menu-cpu-skin.txt"));
    // Everything a page needs to appear: textures, movie streams and models.
    // The key changes only when the composed resident set can change.
    struct ResidencyKey {
        mkw::UiPage page=mkw::UiPage::Strap;unsigned cursor=0;mkw::UiSelection selection{};bool unavailable=false;
        unsigned unlockCondition=~0u,unlockBit=~0u,titleArtwork=0;
        bool modal=false;mkw::UiPage underlayPage=mkw::UiPage::Strap;unsigned underlayCursor=0;
        bool operator==(const ResidencyKey&)const=default;
    };
    auto residencyKey=[](const mkw::MenuState& state,const mkw::MenuState* underlay){
        ResidencyKey key;key.page=state.page;key.cursor=state.cursor;key.selection=state.selection;key.unavailable=state.unavailable;key.titleArtwork=state.titleArtwork;
        key.unlockCondition=state.unlockCondition;key.unlockBit=state.unlockBit;
        if(underlay){key.modal=true;key.underlayPage=underlay->page;key.underlayCursor=underlay->cursor;}
        return key;
    };
    auto residentQuads=[&](const mkw::MenuState& state,const mkw::UiTransitionView* motion,std::vector<mkw::UiQuad>& out){
        // Load the complete authored page before its first visible frame.
        // Clipping during a slide must not load and retire textures row by row.
        mkw::UiTransitionView view=motion?*motion:mkw::UiTransitionView{};
        view.active=false;view.entering=false;view.back=false;view.preload=true;view.holdFrame=0;view.selectedControl=mkw::uiSelectedMenuControl(state);
        mkw::composeMenu(state,uiRace,out,view);
    };
    std::array<std::uint32_t,4> pauseReserves{};
    auto pauseTextureReserve=[&](){
        auto& reserve=pauseReserves[unsigned(menu.selection.mode)];
        if(reserve)return reserve;
        auto state=menu;state.enter(mkw::UiPage::Pause);
        std::vector<mkw::UiQuad> paused,confirmation;
        residentQuads(state,nullptr,paused);
        state.enter(mkw::UiPage::QuitConfirm);residentQuads(state,nullptr,confirmation);
        paused.insert(paused.end(),confirmation.begin(),confirmation.end());
        // Plan the space without reading textures during driving. Use the
        // fitted allocation sizes and leave headroom for allocation overhead.
        reserve=std::max<std::uint32_t>(2u<<20,mkw::uiResidentTextureBytes(paused)+(128u<<10));
        return reserve;
    };
    // Set once a race's assets are resident; releaseRace (assigned with them
    // below) frees them. The menus never draw race assets.
    bool raceResident=false,raceGraphicsSuspended=false;
    std::function<void()> drawPausedBottom;
    std::function<void()> releaseRace,suspendRaceGraphics;
    std::function<bool()> restoreRaceGraphics;
    // Retail course fog object (StaticR 0x805A30C8..0x805A36CC): KMP AREA
    // type 2 selects the BFG state; DK Summit fades on the race timer.
    mkw::RetailFogState retailFog;unsigned retailFogSteps=0;int retailFogArea=-1;
    auto stepRetailFog=[&](mkw::Vec3 eye,unsigned steps){
        const auto* lighting=mkw::courseLighting(Courses[courseChoice].archive);
        const auto* map=K::System::CourseMap::Instance();
        if(!lighting||!map)return;
        const auto* race=K::System::RaceManager::Instance();
        const K::EGG::Vector3f position(eye.x,eye.y,eye.z);
        retailFogArea=map->getCurrentAreaID(s16(retailFogArea),position,K::System::MapdataAreaBase::Type(2));
        const auto* area=retailFogArea>=0?map->getArea(u16(retailFogArea)):nullptr;
        for(unsigned i=0;i<steps;++i)retailFog.step(*lighting,area?area->param(0):0,Courses[courseChoice].id==K::Course::DK_Summit,race?race->timer():0);
        renderer.setRetailFog(retailFog.value);
    };
    // Set when the race faded out for a section change (the end-of-race menu):
    // the next menu page starts with the scene fader's fade in.
    bool sectionFadeIn=false;
    auto nextUnlock=[&](){
        const unsigned slot=std::min(menu.selection.license,3u);
        auto& save=licenseSaves[slot];
        menu.unlockNextRequested=false;
        if(damagedLicenses[slot]||!save.occupied)return false;
        const auto available=save.availableUnlocks();
        if(available.empty()){
            if(menu.page==mkw::UiPage::Unlock)menu.enter(mkw::UiPage::MainMenu);
            menu.unlockCondition=menu.unlockBit=~0u;return false;
        }
        auto notice=available.front();
        const auto result=mkw::activateNextLicenseUnlock(LicenseDirectory,slot,save,notice);
        menu.saveFailed=result==mkw::LicenseUnlockActivation::SaveFailed;
        menu.unlockCondition=notice.condition;menu.unlockBit=notice.completionBit;
        menu.licenseUnlocks[slot]=save.unlockFlags;
        menu.enter(mkw::UiPage::Unlock);
        return true;
    };
    auto runMenu=[&](){
        mkw::animationStride.store(1);
        notificationLed.clear();
        gyroTrick.reset();
        for(unsigned slot=0;slot<4;++slot)menu.licenseUnlocks[slot]=licenseSaves[slot].unlockFlags;
        if(menu.page==mkw::UiPage::MainMenu)nextUnlock();
        if(menu.page==mkw::UiPage::Pause){mkw::animationStride.store(1);recordPauseStage("menu-enter",true);profiler->finish();}
        renderer.setDualScreenIntro(false);
        renderer.frameSync=true;
        renderer.setDeferred(false);
        renderer.waitForGpu();
        if(menu.page==mkw::UiPage::Pause)recordPauseStage("renderer-drained");
        if(!mkw::SoundSystem::instance().available()){
            if(mkw::SoundSystem::instance().start())menuSound.resetAfterEngineRestart();
        }
        if(menu.page==mkw::UiPage::Pause){
            if(suspendRaceGraphics)suspendRaceGraphics();
            recordPauseStage("race-graphics-released");
            std::vector<mkw::UiQuad> paused,confirmation;
            residentQuads(menu,nullptr,paused);
            auto confirm=menu;confirm.enter(mkw::UiPage::QuitConfirm);
            residentQuads(confirm,nullptr,confirmation);
            paused.insert(paused.end(),confirmation.begin(),confirmation.end());
            bool ready=menuRenderer.prepare(paused);
            recordPauseStage(ready?"pause-menu-ready":"pause-menu-allocation-failed");
            if(!ready){
                menuRenderer.release(renderer);renderer.waitForGpu();
                // Track meshes and their shadow indices must remain resident.
                // Retry only the menu allocation; never evict the whole course.
                ready=menuRenderer.prepare(paused);
            }
            while(!ready&&aptMainLoop()){
                hidScanInput();const unsigned down=hidKeysDown();
                if(down&(KEY_B|KEY_START)){menu.enter(mkw::UiPage::Race);return mkw::UiAction::ResumeRace;}
                if(down&KEY_A){menuRenderer.release(renderer);renderer.waitForGpu();ready=menuRenderer.prepare(paused);}
                renderer.begin({0,0,1000},{0,0,0},{0,1,0});renderer.drawScreenFade(1.f,false);
                renderer.drawScreenText("Pause menu could not load",24,65,1.5f,0xffffffffu);
                renderer.drawScreenText("A: retry   B: resume",24,105,1.5f,0xffffffffu);renderer.end();
            }
            if(!ready)return mkw::UiAction::Exit;
        }
        auto& loader=mkw::AssetLoader::instance();
        if(!loader.start())report("FAIL menu asset thread; loading on demand");
        mkw::pack::open();
        auto last=svcGetSystemTick();double pending=0;
        int activeAudio=-1;
        mkw::MenuTransition transition;
        if(sectionFadeIn){sectionFadeIn=false;menu.age=menu.focusAge=0;transition.enterSection(menu);}
        else if(menu.page==mkw::UiPage::Pause){menu.age=menu.focusAge=0;transition.enterPage(menu);}
        std::vector<mkw::UiQuad> quads,topMenuQuads,bottomMenuQuads,visualResident,incomingResident;
        std::vector<std::vector<mkw::UiQuad>> prefetchResident;std::vector<mkw::MenuState> prefetchStates;
        ResidencyKey visualKey,incomingKey;bool visualKeyed=false,incomingKeyed=false,prefetched=false;
        // The first page appears only once resident; until then the previous
        // frame (race or black) stays on screen instead of a partial page.
        bool revealed=false,pendingRaceMiiConfirm=false;unsigned stable=0,heldFrames=0;
        std::unique_ptr<mkw::LoadingScope> revealLoading;
        u64 loadWaitStart=0;bool loadRecovery=false;
        std::shared_ptr<LicenseSelectionSaveJob> selectionSave;
        u64 saveStarted=0;
        auto leave=[&](mkw::UiAction action){
            loader.stop();renderer.waitForGpu();menuAudio.close();
            menuModels.reset(renderer);menuIcons.reset(renderer);
            menuRenderer.release(renderer);
            renderer.setRetailLighting(raceResident?mkw::courseLighting(Courses[courseChoice].archive):nullptr,false);
            renderer.waitForGpu();return action;
        };
        auto chooseIdentity=[&](mkw::LicenseIdentity& identity,bool keepName=false,bool raceMii=false){
            renderer.waitForGpu();menuAudio.close();
            mkw::SoundSystem::instance().stop();loader.stop();
            menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release(renderer);renderer.waitForGpu();
            const auto result=raceMii?mkw::selectRaceMii(identity.nativeMii):keepName?mkw::selectLicenseMii(identity.nativeMii):mkw::selectLicenseIdentity(identity);
            loader.start();
            if(mkw::SoundSystem::instance().start())menuSound.resetAfterEngineRestart();
            activeAudio=-1;visualKeyed=incomingKeyed=prefetched=false;revealed=false;
            stable=heldFrames=0;loadWaitStart=0;loadRecovery=false;
            last=svcGetSystemTick();pending=0;hidScanInput();
            return result;
        };
        while(aptMainLoop()){
            if(selectionSave&&selectionSave->done()){
                menu.saveFailed=!selectionSave->ok;
                if(selectionSave->ok)licenseSaves[selectionSave->slot]=selectionSave->value;
                selectionSave.reset();saveStarted=0;
            }
            for(unsigned slot=0;slot<4;++slot)menu.licenseUnlocks[slot]=licenseSaves[slot].unlockFlags;
            // The logical page becomes Race while the pause exit still animates.
            // Keep the resident course for ResumeRace; only release when leaving
            // the race section for an unrelated menu or a different race.
            if(raceResident&&!menu.retainsRaceAssets()&&!transition.visual(menu).retainsRaceAssets())releaseRace();
            if(!renderer.bottomEnabled()&&!renderer.enableBottom()){report("FAIL lower menu target");return leave(mkw::UiAction::Exit);}
            menuProfile->begin(unsigned(menu.page),transition.active());
            hidScanInput();unsigned down=hidKeysDown();
            unsigned navigation=down|hidKeysDownRepeat();
            mkw::UiInput input{bool(navigation&(KEY_DUP|KEY_CPAD_UP)),bool(navigation&(KEY_DDOWN|KEY_CPAD_DOWN)),bool(navigation&(KEY_DLEFT|KEY_CPAD_LEFT)),bool(navigation&(KEY_DRIGHT|KEY_CPAD_RIGHT)),bool(down&KEY_A),bool(down&KEY_B),bool(down&KEY_START),bool(down&KEY_Y)};
            if(!revealed){
                if(menu.page==mkw::UiPage::Error&&(input.back||input.accept))return leave(mkw::UiAction::Exit);
                // A failed SD read must not expose a partial pause page or
                // trap the player in a frozen race while retries continue.
                if(raceResident&&(menu.page==mkw::UiPage::Pause||menu.page==mkw::UiPage::QuitConfirm)&&(input.back||input.start)){
                    menu.enter(mkw::UiPage::Race);return leave(mkw::UiAction::ResumeRace);
                }
                input={};
            }
            if(autoplay){input=autopilot->menu(menu,revealed&&!transition.active());if(autopilot->finished())return leave(mkw::UiAction::Exit);}
            const auto beforeInput=menu;
            if(selectionSave||pendingRaceMiiConfirm)input={};
            const bool acceptedInput=!transition.active()&&!selectionSave&&!pendingRaceMiiConfirm;
            auto action=transition.input(menu,input);
            if(acceptedInput&&menu.selectRaceMiiRequest>=24){
                mkw::LicenseIdentity identity;mkw::MiiAppearance appearance;mkw::MiiBodySpec body;
                const unsigned outfit=unsigned(menu.selectRaceMiiRequest-24);
                const auto result=chooseIdentity(identity,true,true);
                if(result==mkw::IdentitySelection::Accepted&&mkw::decodeMiiAppearance(identity.nativeMii,appearance)&&mkw::miiBodySpec(appearance,outfit,body)){
                    selectedRaceMii=identity.nativeMii;selectedMiiVoice=mkw::miiVoiceIndex(appearance);menu.selectedMiiVoice=selectedMiiVoice;selectedMiiBody=body;
                    menu.selection.driver=unsigned(menu.selectRaceMiiRequest);menu.selection.miiWeight=body.weightClass;
                    menu.selection.miiCharacter=body.retailCharacter;menu.selection.miiSelected=true;
                    // Restore and present Character first. The system applet
                    // destroyed its models; elapsed applet/loading time is not animation time.
                    pendingRaceMiiConfirm=true;
                }else{menu.selectRaceMiiRequest=-1;menu.saveFailed=result==mkw::IdentitySelection::Failed;}
            }
            if(acceptedInput){
                const unsigned slot=std::min(menu.selection.license,3u);
                auto& save=licenseSaves[slot];menu.licenseRecords=save.records;
                if(beforeInput.page==mkw::UiPage::License&&menu.page==mkw::UiPage::MainMenu){
                    if(damagedLicenses[slot]){transition.reset();menu.enter(mkw::UiPage::License);menu.saveFailed=true;}
                    else{
                        if(!save.occupied){
                            mkw::LicenseIdentity identity;
                            const auto chosen=chooseIdentity(identity);
                            if(chosen==mkw::IdentitySelection::Accepted){
                                mkw::LicenseSave created;created.occupied=true;created.settings.license=slot;
                                created.name=identity.name;created.nativeMii=identity.nativeMii;
                                created.miiId=mkw::licenseMiiId(identity.nativeMii);
                                menu.saveFailed=!persistLicense(slot,created);
                                if(!menu.saveFailed)save=created;
                            }else menu.saveFailed=chosen==mkw::IdentitySelection::Failed;
                            if(!save.occupied){const bool failed=menu.saveFailed;transition.reset();menu=beforeInput;menu.saveFailed=failed;}

                        }else if(save.nativeMii[0]!=3){
                            // Preserve all imported progress and the license name.
                            mkw::LicenseIdentity identity;identity.name=save.name;
                            const auto chosen=chooseIdentity(identity,true);
                            menu.saveFailed=chosen==mkw::IdentitySelection::Failed;
                            if(chosen==mkw::IdentitySelection::Accepted){
                                auto updated=save;updated.nativeMii=identity.nativeMii;
                                updated.miiId=mkw::licenseMiiId(identity.nativeMii);
                                menu.saveFailed=!persistLicense(slot,updated);
                                if(!menu.saveFailed)save=updated;
                            }
                            if(save.nativeMii[0]!=3){const bool failed=menu.saveFailed;transition.reset();menu=beforeInput;menu.saveFailed=failed;}
                        }else menu.saveFailed=false;
                        if(save.occupied&&menu.page==mkw::UiPage::MainMenu)menu.selection=save.settings;
                        menu.licenseBestTimes[slot]=save.times;menu.licenseOccupied[slot]=save.occupied;
                        menu.licenseNames[slot]=save.name;menu.licenseUnlocks[slot]=save.unlockFlags;
                        if(save.occupied&&menu.page==mkw::UiPage::MainMenu&&!menu.saveFailed){
                         FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/last-license.txt.tmp","w");
                         if(f){const bool wrote=std::fprintf(f,"%u\n",slot)>0;const bool closed=std::fclose(f)==0;
                          if(wrote&&closed)std::rename("sdmc:/3ds/mkw-3ds/last-license.txt.tmp","sdmc:/3ds/mkw-3ds/last-license.txt");}
                        }
                    }
                }
                if(menu.editLicenseRequest>=0){
                    const unsigned edited=unsigned(menu.editLicenseRequest);menu.editLicenseRequest=-1;
                    if(edited<licenseSaves.size()&&licenseSaves[edited].occupied){
                        mkw::LicenseIdentity identity;
                        const auto chosen=chooseIdentity(identity);
                        menu.saveFailed=chosen==mkw::IdentitySelection::Failed;
                        if(chosen==mkw::IdentitySelection::Accepted){
                            auto updated=licenseSaves[edited];
                            updated.name=identity.name;updated.nativeMii=identity.nativeMii;
                            updated.miiId=mkw::licenseMiiId(identity.nativeMii);
                            menu.saveFailed=!persistLicense(edited,updated);
                            if(!menu.saveFailed){licenseSaves[edited]=updated;menu.licenseNames[edited]=updated.name;}
                        }
                    }
                }
                if(menu.eraseLicenseRequest>=0){
                    const unsigned erased=unsigned(menu.eraseLicenseRequest);menu.eraseLicenseRequest=-1;
                    mkw::LicenseSave empty;empty.settings.license=erased;
                    // Persist the empty slot as a tombstone: never reimport the
                    // older separate time/stat files after an explicit erase.
                    if(persistLicense(erased,empty)){
                        licenseSaves[erased]=empty;menu.licenseBestTimes[erased]={};damagedLicenses[erased]=false;menu.licenseOccupied[erased]=false;menu.licenseCups[erased]={};
                        menu.selection=empty.settings;menu.saveFailed=false;
                    }else menu.saveFailed=true;
                }else if(mkw::licenseSelectionNeedsSave(menu,save,damagedLicenses[slot])){
                    auto updated=save;updated.settings=menu.selection;
                    if(loader.running()&&action==mkw::UiAction::None){
                        // Publish the cached settings only after the atomic save
                        // succeeds. Render/audio continue while SD I/O is slow;
                        // navigation waits so another write cannot race it.
                        selectionSave=std::make_shared<LicenseSelectionSaveJob>();
                        selectionSave->slot=slot;selectionSave->value=updated;
                        selectionSave->label="license save";saveStarted=svcGetSystemTick();
                        loader.submit(selectionSave,mkw::AssetPriority::Urgent);
                    }else{
                        menu.saveFailed=!persistLicense(slot,updated);
                        if(!menu.saveFailed)save=updated;
                    }
                }
            }
            if(acceptedInput&&(menu.unlockNextRequested||
                (beforeInput.page==mkw::UiPage::License&&menu.page==mkw::UiPage::MainMenu&&!menu.saveFailed))){
                const auto previous=menu;
                nextUnlock();
                // Keep License -> MainMenu's transition when there is no
                // notification. Replacing it with MainMenu -> itself erases it.
                if(menu.page!=previous.page||(menu.page==mkw::UiPage::Unlock&&
                    (menu.unlockCondition!=previous.unlockCondition||menu.unlockBit!=previous.unlockBit))){
                    transition.reset();transition.begin(beforeInput,menu);
                }
            }
            // StartRace/RestartRace remain on the same logical page while
            // their OK animation runs. Notify audio once at accepted input,
            // rather than mistaking repeated presses during that hold for
            // additional clicks.
            if(acceptedInput&&!pendingRaceMiiConfirm){
                menuSound.input(beforeInput,menu,input,action);
                if(beforeInput.page==mkw::UiPage::Character&&menu.page!=beforeInput.page&&!input.back)
                    notificationLed.characterSelected(menu.selection.driver,menu.selection.ledMode,menu.selection.ledBrightness);
            }
            if(transition.active())loader.cancelPrefetch();
            loader.setForeground(!revealed||transition.active());
            if(action!=mkw::UiAction::None){if(action==mkw::UiAction::StartRace||action==mkw::UiAction::RestartRace)menuSound.leave();return leave(action);}
            auto now=svcGetSystemTick();pending+=std::min(double(now-last)/SYSCLOCK_ARM11,.1)*60;last=now;
            unsigned ticks=unsigned(pending);pending-=ticks;
            for(unsigned slot=0;slot<licenseSaves.size();++slot)menuModels.setIdentity(renderer,slot,slot==menu.selection.license&&menu.selection.miiSelected?selectedRaceMii:licenseSaves[slot].nativeMii);
            // While the outgoing page is visible, the logical state is the page
            // that appears next: start its loads now and gate its entrance.
            if(transition.exiting()){
                const auto key=residencyKey(menu,nullptr);
                if(!incomingKeyed||!(key==incomingKey)){residentQuads(menu,nullptr,incomingResident);incomingKey=key;incomingKeyed=true;}
                if(menu.page==mkw::UiPage::License)publishLicensePortraits(incomingResident);
                menuRenderer.request(incomingResident,mkw::AssetPriority::Urgent);
                menuModels.request(menu,mkw::AssetPriority::Urgent);menuIcons.request(mkw::MenuVehicleIcons::driverFor(menu),mkw::AssetPriority::Urgent);
            }else incomingKeyed=false;
            const bool texturesReady=menuRenderer.ready(incomingResident),modelsReady=menuModels.ready(menu),iconsReady=menuIcons.ready(mkw::MenuVehicleIcons::driverFor(menu));
            const bool incomingReady=!transition.exiting()||(texturesReady&&modelsReady&&iconsReady);
            if(transition.holding()&&menuProfile->enabled()&&++heldFrames%30==0){
                snprintf(line,sizeof line,"hold page=%u frames=%u textures=%d(pending %u) models=%d icons=%d queue=%u",unsigned(menu.page),heldFrames,int(texturesReady),menuRenderer.loadsPending(),int(modelsReady),int(iconsReady),loader.pending());
                menuProfile->event(line);
            }else if(!transition.holding())heldFrames=0;
            // Page readiness is a real resource gate, not an unbounded wait.
            // Slow cards get five seconds of async loading, then one clean
            // retry without speculative caches competing for LINEAR memory.
            if(!incomingReady||!revealed){
                if(!loadWaitStart)loadWaitStart=now;
                if(now-loadWaitStart>u64(SYSCLOCK_ARM11)*5){
                    mkdir("sdmc:/3ds",0777);mkdir(DATA,0777);
                    std::vector<mkw::UiQuad> retry;
                    auto retryMotion=transition.view();
                    residentQuads(menu,&retryMotion,retry);
                    menuRenderer.logPending(retry,"sdmc:/3ds/mkw-3ds/menu-load-errors.txt");
                    loader.stop();renderer.waitForGpu();
                    menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release(renderer);renderer.waitForGpu();
                    bool recovered=false;
                    if(!loadRecovery){
                        recovered=menuRenderer.prepare(retry);
                        menuModels.prepare(renderer,menu);recovered=menuIcons.prepare(renderer,menu)&&recovered;
                    }
                    loadRecovery=true;
                    if(!recovered){transition.reset();menu.enter(mkw::UiPage::Error);}
                    loader.start();revealed=false;visualKeyed=incomingKeyed=false;prefetched=false;stable=0;
                    loadWaitStart=0;last=svcGetSystemTick();pending=0;
                    continue;
                }
            }else {loadWaitStart=0;loadRecovery=false;}
            if(!revealed)ticks=0;
            mkw::uiFocusMovieChapter(menu,menuRenderer.cupMovieChapter());
            transition.advance(menu,ticks,incomingReady);
            if(transition.holding())menuProfile->hold();
            menuModels.advance(ticks);
            action=transition.takeAction();
            if(action!=mkw::UiAction::None){if(action==mkw::UiAction::StartRace||action==mkw::UiAction::RestartRace)menuSound.leave();return leave(action);}
            menuProfile->mark(MenuStage::Input);
            const auto& visual=transition.visual(menu);const auto motion=transition.view();
            mkw::composeMenu(visual,uiRace,quads,motion);
            menuProfile->mark(MenuStage::Compose);
            const auto key=residencyKey(visual,motion.underlay);
            if(!visualKeyed||!(key==visualKey)){
                residentQuads(visual,&motion,visualResident);visualKey=key;visualKeyed=true;
                // The user moved on: queued prefetches for the old state are stale.
                loader.cancelPrefetch();prefetched=false;stable=0;
            }
            menuRenderer.request(visualResident,mkw::AssetPriority::Urgent);
            menuModels.request(visual,mkw::AssetPriority::Urgent);menuIcons.request(mkw::MenuVehicleIcons::driverFor(visual),mkw::AssetPriority::Urgent);
            // Once a page is settled, warm the pages and selections one input away.
            if(menu.page!=mkw::UiPage::Intro&&!transition.active()&&revealed&&++stable>=2){
                if(!prefetched){
                    prefetched=true;prefetchStates.clear();prefetchResident.clear();
                    const mkw::UiInput moves[]={{false,false,false,false,true},{false,false,false,false,false,true},{true},{false,true},{false,false,true},{false,false,false,true}};
                    for(const auto& move:moves){
                        auto next=menu;const auto result=next.input(move);
                        if(result!=mkw::UiAction::None||next.unavailable)continue;
                        if(next.page==menu.page&&next.cursor==menu.cursor)continue;
                        prefetchStates.push_back(next);
                    }
                }
                // One neighbour per frame keeps composition out of any single frame.
                if(prefetchResident.size()<prefetchStates.size()){prefetchResident.emplace_back();residentQuads(prefetchStates[prefetchResident.size()-1],nullptr,prefetchResident.back());}
                for(size_t i=0;i<prefetchResident.size();++i){
                    menuRenderer.request(prefetchResident[i],mkw::AssetPriority::Prefetch);
                    menuModels.request(prefetchStates[i],mkw::AssetPriority::Prefetch);
                }
                // The highlighted character's twelve machines, after a short dwell.
                if(stable>=30&&menu.page==mkw::UiPage::Character&&menu.characterDriver(menu.cursor)<24)menuIcons.request(int(menu.characterDriver(menu.cursor)),mkw::AssetPriority::Prefetch);
            }
            menuRenderer.update(renderer);menuModels.update(renderer,visual,motion);menuIcons.update(renderer,visual);
            if(menuProfile->enabled())loader.drainSlow([](const char* label,const char* detail,float run,float wait,void* profile){char text[256];snprintf(text,sizeof text,"slow job %.1f ms (queued %.1f ms): %s %s",run,wait,label,detail);static_cast<mkw::MenuProfile*>(profile)->event(text);},menuProfile.get());
            menuProfile->mark(MenuStage::Resident);
            if(!revealed)revealed=menuRenderer.ready(visualResident)&&menuModels.ready(visual)&&menuIcons.ready(mkw::MenuVehicleIcons::driverFor(visual));
            menuProfile->mark(MenuStage::Textures);
            // The engine plays the title wave (W_BGM_TITLE) and every other menu sound;
            // the THP opening keeps its own stream. The cooked title WAV remains the
            // fallback when the sound archive is missing.
            const bool engineAudio=mkw::SoundSystem::instance().available();
            menuSound.frame(visual,revealed);
            // QA: sound engine health as profiler events (in memory; written at the end).
            if(menuProfile->enabled()&&engineAudio){static unsigned soundLog=0;if(++soundLog%300==0){const auto st=mkw::SoundSystem::instance().stats();
                snprintf(line,sizeof line,"sound page=%u voices=%lu sounds=%lu dropped=%lu underruns=%lu missing=%lu failed=%lu audio_ms=%.3f peak_ms=%.3f waves=%lu linear_free=%lu",unsigned(visual.page),(unsigned long)st.voices,(unsigned long)st.sounds,(unsigned long)st.dropped,(unsigned long)st.underruns,(unsigned long)st.missing,(unsigned long)st.failed,st.audioMs,st.audioPeakMs,(unsigned long)mkw::SoundSystem::instance().residentWaveBytes(),(unsigned long)linearSpaceFree());menuProfile->event(line);}}
            const int wantedAudio=!revealed?-1:visual.page==mkw::UiPage::Intro?0:visual.page==mkw::UiPage::Title&&!engineAudio?1:-1;
            if(wantedAudio!=activeAudio){
                menuAudio.close();activeAudio=wantedAudio;
                if(activeAudio==0)menuAudio.open("movies/title.wav");
                if(activeAudio==1){menuAudio.open(mkw::UiTitleMusicPath,true,mkw::UiTitleMusicLoopSample);menuAudio.setVolume(mkw::UiTitleMusicVolume);}
                last=svcGetSystemTick();pending=0;
            }
            menuProfile->mark(MenuStage::Audio);
            menuRenderer.streamMovies(quads,motion.underlay?motion.underlay->age:visual.age,
                                      revealed);
            if(!transition.active()&&menu.page==mkw::UiPage::Intro&&menuRenderer.moviesFinished()){const auto before=menu;menu.enter(mkw::UiPage::Title);transition.begin(before,menu);continue;}
            menuProfile->mark(MenuStage::Movies);
            if(activeAudio>=0)menuAudio.update();
            menuProfile->mark(MenuStage::Audio);
            // A first page with a movie (the course choice after Next Race) is
            // ready only once a frame is uploaded; nothing is drawn before the
            // reveal, so the GPU is idle here.
            if(!revealed){
                // There is no drawing frame to release evicted resources yet.
                // Reclaim them while the GPU is idle so pending loads can use
                // the actual freed memory, not just update()'s retirement count.
                renderer.waitForGpu();menuRenderer.uploadMovies();
                if(!revealLoading)revealLoading=std::make_unique<mkw::LoadingScope>();
                renderer.begin({0,0,1000},{0,0,0},{0,1,0});renderer.drawScreenFade(1,false);
                renderer.end();
                menuProfile->mark(MenuStage::Begin);menuProfile->end(renderer,0);continue;
            }
            revealLoading.reset();
            publishLicensePortraits(quads);
            menuModels.begin(renderer);
            menuProfile->mark(MenuStage::Begin);
            // GPU idle: publish streamed movie frames for this frame.
            menuRenderer.uploadMovies();
            menuProfile->mark(MenuStage::Movies);
            renderer.drawScreenFade(1.f,false);
            drawMenuBackdrop(renderer,menuRenderer,quads);
            menuProfile->mark(MenuStage::Background);
            menuModels.draw(renderer,visual,motion);
            menuProfile->mark(MenuStage::Scene);
            const bool lowerMenu=menuOnBottom(visual.page);
            if(lowerMenu){
                mkw::layoutMenuScreens(visual,quads,topMenuQuads,bottomMenuQuads);
                menuRenderer.draw(renderer,topMenuQuads,false,0,~0u,&menuIcons);
                if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
                renderer.beginBottom();renderer.drawScreenFade(1.f,false);
                menuRenderer.draw(renderer,bottomMenuQuads,true,0,~0u,&menuIcons);
                if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
            }else menuRenderer.draw(renderer,quads,false,1,~0u,&menuIcons);
            if(menu.page==mkw::UiPage::Error&&resourceFailure[0]){
                renderer.drawScreenFade(1.f,false);
                renderer.drawScreenText("LOAD FAILED",16,55,1.7f,0xffcc88ffu);
                const char* cursor=resourceFailure;
                for(unsigned row=0;*cursor&&row<3;++row){
                    const auto length=std::min<size_t>(34,std::strlen(cursor));
                    char text[41];std::memcpy(text,cursor,length);text[length]=0;
                    renderer.drawScreenText(text,16,95+row*22,1.3f,0xffffffffu);cursor+=length;
                }
                renderer.drawScreenText("A/B: return to menu",16,195,1.3f,0xffffffffu);
            }
            if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
            if(!lowerMenu){
                renderer.beginBottom();
                if((visual.page==mkw::UiPage::Pause||visual.page==mkw::UiPage::QuitConfirm)&&drawPausedBottom)drawPausedBottom();
                else renderer.drawScreenFade(1.f,false);
                if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
            }
            if(!incomingReady)mkw::drawLoadingWheel(renderer,loadWaitStart);
            if(selectionSave)mkw::drawLoadingWheel(renderer,saveStarted);
            menuProfile->mark(MenuStage::Foreground);
            renderer.end();
            menuProfile->mark(MenuStage::Submit);menuProfile->end(renderer,unsigned(quads.size()));
            if(pendingRaceMiiConfirm){
                renderer.waitForGpu(); // actual restored picture, not just resident files
                const auto before=menu;
                menu.confirmRaceMii(selectedMiiBody.weightClass,selectedMiiBody.retailCharacter);
                transition.begin(before,menu);transition.holdConfirmation(menuModels.confirmationFrames());
                mkw::UiInput confirm{};confirm.accept=true;
                menuSound.input(before,menu,confirm,mkw::UiAction::None);
                notificationLed.characterSelected(menu.selection.driver,menu.selection.ledMode,menu.selection.ledBrightness);
                pendingRaceMiiConfirm=false;last=svcGetSystemTick();pending=0;
            }
            // QA (autoplay + menu-capture.txt): sample menu frames for review.
            static const unsigned menuCaptureEvery=[]{unsigned n=0;if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/menu-capture.txt","r")){if(fscanf(f,"%u",&n)!=1)n=0;fclose(f);}return n;}();
            static unsigned menuFrames=0;
            if(autoplay&&menuCaptureEvery&&++menuFrames%menuCaptureEvery==0){
                std::vector<uint8_t> pixels(400*240*3);char path[64];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/menu-auto-%02u-%05u.ppm",unsigned(visual.page),menuFrames);
                if(renderer.captureTop(pixels.data()))if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
                saveRightEye(renderer,path);
                std::vector<uint8_t> lower(320*240*3);std::string lowerPath(path);lowerPath.insert(lowerPath.size()-4,"-bottom");
                if(renderer.captureBottom(lower.data()))if(FILE* f=fopen(lowerPath.c_str(),"wb")){fprintf(f,"P6\n320 240\n255\n");fwrite(lower.data(),1,lower.size(),f);fclose(f);}
            }
        }
        return leave(mkw::UiAction::Exit);
    };
    if((kQa&&exists("sdmc:/3ds/mkw-3ds/menu-test.txt"))){
        struct Snapshot {const char* name;mkw::UiPage page;unsigned cursor;mkw::UiEngineClass engine=mkw::UiEngineClass::CC150;};
        const Snapshot pages[]={{"strap",mkw::UiPage::Strap,0},{"license",mkw::UiPage::License,0},{"class",mkw::UiPage::EngineClass,1},{"title",mkw::UiPage::Title,0},{"main",mkw::UiPage::MainMenu,0},{"single-player",mkw::UiPage::Mode,0},{"character",mkw::UiPage::Character,8},{"vehicle",mkw::UiPage::Vehicle,0},{"drift",mkw::UiPage::Drift,1},{"cup",mkw::UiPage::Cup,0},{"course",mkw::UiPage::Course,0},{"confirm",mkw::UiPage::Confirm,0},{"pause",mkw::UiPage::Pause,0},
            {"vs-mode",mkw::UiPage::VSMode,0},{"vs-rules",mkw::UiPage::VSRules,0},{"battle-mode",mkw::UiPage::BattleMode,0},{"battle-cup",mkw::UiPage::BattleCup,0},{"battle-stage",mkw::UiPage::BattleStage,0},{"team-confirm",mkw::UiPage::TeamConfirm,0},{"drift-help",mkw::UiPage::DriftHelp,0},{"settings",mkw::UiPage::Settings,0},{"license-settings",mkw::UiPage::LicenseSettings,0},{"erase-license",mkw::UiPage::EraseLicense,1},{"quit-confirm",mkw::UiPage::QuitConfirm,1},
            {"vehicle-karts",mkw::UiPage::Vehicle,0,mkw::UiEngineClass::CC50},{"vehicle-bikes",mkw::UiPage::Vehicle,0,mkw::UiEngineClass::CC100}};
        bool snapshotsOnly=false;
        if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/menu-test.txt","r")){char mode[24]{};if(fscanf(f,"%23s",mode)==1)snapshotsOnly=!strcmp(mode,"snapshots");fclose(f);}
        bool ok=renderer.enableBottom();renderer.setDualScreenIntro(false);
        unsigned captured=0,expected=std::size(pages),motionFrames=0;std::vector<uint8_t> pixels(400*240*3);mkw::UiRaceView fixture{};
        auto capture=[&](const char* name,const mkw::MenuState& visual,const mkw::UiTransitionView& motion,bool save=true,bool models=true){
            std::vector<mkw::UiQuad> quads,resident,upper,lower;mkw::composeMenu(visual,fixture,quads,motion);
            auto preload=motion;preload.active=false;preload.entering=false;preload.back=false;preload.preload=true;preload.holdFrame=0;preload.selectedControl=mkw::uiSelectedMenuControl(visual);
            mkw::composeMenu(visual,fixture,resident,preload);
            for(const auto& quad:quads)ok&=!quad.bottom;
            renderer.waitForGpu();ok&=menuRenderer.prepare(resident);ok&=menuRenderer.updateMovies(motion.underlay?motion.underlay->age:visual.age);
            menuModels.prepare(renderer,visual);ok&=menuIcons.prepare(renderer,visual);menuModels.begin(renderer);renderer.drawScreenFade(1.f,false);
            drawMenuBackdrop(renderer,menuRenderer,quads);if(models)menuModels.draw(renderer,visual);
            if(menuOnBottom(visual.page)){
                mkw::layoutMenuScreens(visual,quads,upper,lower);menuRenderer.draw(renderer,upper,false,0,~0u,&menuIcons);
                if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
                renderer.beginBottom();renderer.drawScreenFade(1.f,false);menuRenderer.draw(renderer,lower,true,0,~0u,&menuIcons);
            }else{
                menuRenderer.draw(renderer,quads,false,1,~0u,&menuIcons);
                if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
                renderer.beginBottom();renderer.drawScreenFade(1.f,false);
            }
            if(motion.blackAlpha>0)renderer.drawScreenFade(motion.blackAlpha,false);
            renderer.end();
            if(!save)return;
            char path[96];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/menu-%s.ppm",name);
            if(renderer.captureTop(pixels.data())){if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);++captured;}else ok=false;}else ok=false;
            saveRightEye(renderer,path);
            std::string lowerPath(path);lowerPath.insert(lowerPath.size()-4,"-bottom");
            if(renderer.captureBottom(pixels.data())){if(FILE* f=fopen(lowerPath.c_str(),"wb")){fprintf(f,"P6\n320 240\n255\n");fwrite(pixels.data(),1,320*240*3,f);fclose(f);}else ok=false;}else ok=false;
        };
        for(const auto& page:pages){
            menu.selection={};menu.selection.mode=mkw::UiMode::GrandPrix;
            menu.selection.engineClass=page.engine;
            if(page.page==mkw::UiPage::Vehicle||page.page==mkw::UiPage::Drift||page.page==mkw::UiPage::Cup||page.page==mkw::UiPage::Confirm){menu.selection.driver=10;menu.selection.vehicle=page.page==mkw::UiPage::Vehicle?0:4;}
            menu.enter(page.page);menu.cursor=page.cursor;menu.advance(54);
            for(unsigned tick=0;tick<6;++tick){menu.advance(1);capture(page.name,menu,{},false);}
            capture(page.name,menu,{});
            if(page.page==mkw::UiPage::Mode){++expected;capture("single-player-flat",menu,{},true,false);}
        }
        struct MotionCase {const char* name;mkw::UiPage page;unsigned cursor;bool back;mkw::UiMode mode=mkw::UiMode::GrandPrix;};
        const MotionCase motions[]={{"forward",mkw::UiPage::Mode,0,false},{"back",mkw::UiPage::EngineClass,2,true},{"section",mkw::UiPage::MainMenu,0,false},{"modal",mkw::UiPage::Cup,0,false},
            {"cup-course",mkw::UiPage::Cup,1,false,mkw::UiMode::VS},{"course-cup",mkw::UiPage::Course,2,true,mkw::UiMode::VS}};
        for(const auto& test:motions){
            if(snapshotsOnly)break;
            menu.selection={};menu.selection.mode=test.mode;menu.selection.driver=10;menu.selection.vehicle=4;
            menu.enter(test.page);menu.cursor=test.cursor;menu.advance(54);
            for(unsigned tick=0;tick<6;++tick){menu.advance(1);capture(test.name,menu,{},false);}
            mkw::MenuTransition transition;mkw::UiInput key;key.accept=!test.back;key.back=test.back;transition.input(menu,key);
            for(unsigned frame=0;frame<120;++frame){
                char name[80];snprintf(name,sizeof name,"motion-%s-%03u",test.name,frame);++expected;++motionFrames;
                capture(name,transition.visual(menu),transition.view());
                if(!transition.active())break;
                transition.advance(menu,1);
                if(frame==119)ok=false;
            }
        }
        if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/menu-result.json","w")){fprintf(f,"{\"pass\":%s,\"pages\":%u,\"transition_frames\":%u,\"top_only\":false}\n",ok&&captured==expected?"true":"false",unsigned(std::size(pages)),motionFrames);fclose(f);}else ok=false;
        report(ok?"MENU SCREENSHOT PASS":"MENU SCREENSHOT FAIL");renderer.waitForGpu();menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release();mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();return ok?0:4;
    }
    auto applySelection=[&](){selectedRoster=menu.cpuRoster;battleMode=menu.selection.mode==mkw::UiMode::Battle;coinRunnersMode=battleMode&&menu.selection.coinRunners;timeTrial=menu.selection.mode==mkw::UiMode::TimeTrials;activeLicense=menu.selection.license;activeRecords=licenseSaves[activeLicense].records;if(timeTrial)loadBestTimes();courseChoice=battleMode?32+menu.selection.battleStage:menu.selection.course;driverChoice=menu.selection.driver;vehicleChoice=menu.selection.vehicle;automaticDrift=menu.selection.automatic;
        selectedRaceMode=timeTrial?2:menu.selection.mode==mkw::UiMode::GrandPrix?0:menu.selection.mode==mkw::UiMode::Battle?3:1;
        const bool vs=menu.selection.mode==mkw::UiMode::VS;
        selectedTeams=battleMode||(vs&&menu.selection.teams);selectedHumanTeam=menu.selection.humanTeam;
        const unsigned cc=vs?menu.selection.vsClass:menu.selection.mode==mkw::UiMode::GrandPrix?unsigned(menu.selection.engineClass):2;
        selectedMirror=cc==3;selectedEngineClass=std::min(cc,2u);
        selectedCpu=battleMode?menu.selection.battleCpu:vs?menu.selection.vsCpu:(selectedRaceMode==0?std::min(selectedEngineClass,2u):1u);selectedCpuVehicles=battleMode?menu.selection.battleVehicles:vs?menu.selection.vsVehicles:0;selectedItems=battleMode?menu.selection.battleItems:vs?menu.selection.vsItems:0;
    };
    if(fullRace){if(runMenu()!=mkw::UiAction::StartRace){finishAutoplay();renderer.waitForGpu();menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release();mkw::SoundSystem::instance().stop();mkw::MenuMovieAudio::shutdown();mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();return 0;}applySelection();menu.raceStarted();raceLoadStart=svcGetSystemTick();}
    bool dataReady=exists("Race/Common.szs")&&courseAvailable(courseChoice);
    if(dataReady&&(kQa&&exists("sdmc:/3ds/mkw-3ds/trace-test.txt"))) {
        // Invoke upstream's complete per-frame KRKG comparison, not just final position.
        freopen("trace-console.log","w",stdout);setvbuf(stdout,nullptr,_IONBF,0);
        auto* sys=K::KTestSystem::CreateInstance();
        char a0[]="-g",a1[]="lc.rkg",a2[]="-k",a3[]="lc.krkg";
        char* args[]={a0,a1,a2,a3};sys->parseOptions(4,args);
        auto start=svcGetSystemTick();sys->init();bool ok=sys->run();
        double ms=(svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11;
        if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/trace-result.json","w")){fprintf(f,"{\"pass\":%s,\"elapsed_ms\":%.3f,\"hardware_verified\":false}\n",ok?"true":"false",ms);fclose(f);}
        report(ok?"TRACE PASS":"TRACE FAIL");gfxExit();return ok?0:2;
    }
    const bool effectsTest=(kQa&&exists("sdmc:/3ds/mkw-3ds/effects-test.txt"));
    // QA (autoplay + lakitu-test.txt): drop the player out of bounds at this step.
    // QA: "step [request]" forces an out-of-bounds activation; the optional camera
    // request is the KartCollide one (0x408 solid-style hold, 0x418 fall rise).
    unsigned lakituRespawnStep=0,lakituRequest=0x408;if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/lakitu-test.txt","r")){if(fscanf(f,"%u %x",&lakituRespawnStep,&lakituRequest)<1)lakituRespawnStep=0;fclose(f);}
    // QA (autoplay + give-test.txt): "step item holdFrom holdTo [player]".
    // Player defaults to 0; hold its button over [holdFrom,holdTo).
    // The grant retries until the slot is free; the hold window follows the grant.
    // QA (autoplay + cpu-log.txt): per-racer AI state every N steps in runtime.log.
    unsigned cpuLogEvery=0;if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/cpu-log.txt","r")){if(fscanf(f,"%u",&cpuLogEvery)!=1)cpuLogEvery=0;fclose(f);}
    struct QaGive{unsigned step=0,item=0,from=0,to=0,granted=0,player=0;};std::vector<QaGive> qaGives;
    if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/give-test.txt","r")){
        char row[192];while(fgets(row,sizeof row,f)){QaGive g{};
            const int fields=sscanf(row,"%u %u %u %u %u",&g.step,&g.item,&g.from,&g.to,&g.player);
            if(fields>=4&&g.player<12&&g.item<21&&g.from>=g.step&&g.to>=g.from)qaGives.push_back(g);
        }fclose(f);
    }
    const bool garageTest=(kQa&&exists("sdmc:/3ds/mkw-3ds/garage-test.txt"));
    const bool animationTest=(kQa&&exists("sdmc:/3ds/mkw-3ds/animation-test.txt"))||garageTest;
    const unsigned fixtureFrames=garageTest?64:300,fixtureChoices=garageTest?72:12;
    bool animationPass=true;unsigned checkedDrivers=0,checkedClips=0,checkedFrames=0,rollingChoices=0;
    bool fixtureRolling=false;
    const bool replay=dataReady&&(kQa&&exists("sdmc:/3ds/mkw-3ds/replay-test.txt"));
    if(replay){size_t size;replayData=K::Abstract::File::LoadHost("lc.rkg",size);}
    report("Citro3D hardware-r2 ready (queue callback fence, whole-triangle effects, UV4)");
    K::Host::SceneCreatorDynamic creator;
    K::EGG::SceneManager scenes(&creator);
    menuAwardScenes=&scenes;
    mkw::RaceGrid raceGrid;
    float boostFov=0,maxFov=0;unsigned trickCount=0;bool wasTricking=false,wasBoosting=false;uint16_t lastMushroomUses=0;
    mkw::TrickPresentation trickModel; // the local driver model's trick gate (sparkle + SE_VCL_JUMP_ACTION)
    // Course preview before every race. While it plays Kinoko keeps simulating
    // but holds its intro stage; its own 172 frames then lead into the countdown.
    mkw::RaceIntro intro;mkw::IntroClearancePath introClearance;unsigned introTick=0,introCaptured=~0u;
    // Race camera director (src/race_camera.cpp): start pan, out-of-bounds
    // holds and wipe, respawn re-seat, goal and KMP TV cameras. It reads the
    // simulation after each step and never writes it.
    auto cameraCourse=std::make_unique<mkw::RaceCameraCourse>();
    auto cameraDirector=std::make_unique<mkw::RaceCameraDirector>();
    // Heap-held like the director: main's frame already fills most of the
    // main-thread stack that runMenu and its callees share.
    mkw::RaceChaseCamera chaseCamera;auto startPanStorage=std::make_unique<mkw::StartPanParam>();auto& startPan=*startPanStorage;
    mkw::RaceCameraInput cameraInput;
    auto& raceEvents=mkw::RaceEvents::instance();
    raceEvents.subscribe([](const mkw::RaceEventInfo& e,void* context){if(e.event==mkw::RaceEvent::PreviewEnd)static_cast<mkw::RaceSound*>(context)->previewEnded();},&raceSound);
    if(menuProfile->enabled())raceSound.setTrace([](const char* text,void* context){static_cast<mkw::MenuProfile*>(context)->event(text);},menuProfile.get());
    mkw::RaceFlowUi flowUi;
    // Retail post-race flow (src/race_results.cpp): FINISH, HUD exit, result
    // boards and the end-of-race menu, over the still-running race. A VS set
    // runs four races (RaceMenu::IsFinalRace) and scores them all.
    auto resultsStorage=std::make_unique<mkw::RaceResultsFlow>();auto& results=*resultsStorage;mkw::RaceFlowInput resultsInput;bool resultsReported=false;
    mkw::AwardResults awardResults;
    bool awardPending=false;
    bool raceSavePending=false;
    auto flushRaceSave=[&](){
        if(!raceSavePending)return;
        menu.saveFailed=!persistLicense(activeLicense,licenseSaves[activeLicense]);
        if(!menu.saveFailed)raceSavePending=false;
    };

    mkw::GpTelemetry gpTelemetry;
    auto raceCount=[&](){return menu.selection.mode==mkw::UiMode::VS?menu.selection.vsRaces:4u;};
    struct VsSet {unsigned race=0,driver=~0u;std::array<uint8_t,12> finishPositions{};std::array<uint16_t,12> points{},roundStartPoints{};std::array<mkw::GpRacePerformance,4> performance{};std::array<bool,4> performanceCaptured{};std::array<unsigned,4> performanceCourses{};bool next=false;};
    auto vsSetStorage=std::make_unique<VsSet>();auto& vsSet=*vsSetStorage;bool nextRaceQueued=false;
    mkw::BattleSession battleSession;battleSession.reset(menu.selection.battleWins);bool sessionCoin=coinRunnersMode;
    auto startVsRace=[&](bool restart){
        if(battleMode){
            if(restart){battleSession.restartRound();vsSet.next=false;nextRaceQueued=false;return;}
            if(vsSet.next&&vsSet.driver==driverChoice&&sessionCoin==coinRunnersMode)battleSession.nextRound();
            else{battleSession.reset(menu.selection.battleWins);sessionCoin=coinRunnersMode;vsSet.driver=driverChoice;}
            vsSet.race=battleSession.round();vsSet.next=false;return;
        }
        if(restart){vsSet.points=vsSet.roundStartPoints;vsSet.next=false;nextRaceQueued=false;return;}
        if(vsSet.next&&vsSet.race<raceCount()&&vsSet.driver==driverChoice){++vsSet.race;selectedStartPositions=vsSet.finishPositions;}
        else{vsSet=VsSet{};vsSet.race=1;vsSet.driver=driverChoice;selectedStartPositions.fill(0);}
        vsSet.next=false;vsSet.roundStartPoints=vsSet.points;
    };
    vsSet.race=1;vsSet.driver=driverChoice; // the first race was chosen above
    struct FlowState {unsigned stage=0;bool preview=false,fall=false,respawn=false,finished=false;} flow;
    auto fillCameraInput=[&](mkw::RaceCameraInput& in){
        auto* kart=K::Kart::KartObjectManager::Instance()->object(0);auto* race=K::System::RaceManager::Instance();
        const auto& params=kart->param()->camera();const auto& status=kart->status();
        in.chaseEye=chaseCamera.eye();in.chasePivot=chaseCamera.pivot();
        in.collideEye=[](void*,float radius,mkw::Vec3 eye,mkw::Vec3 previous,mkw::Vec3& push,bool& wall){
            K::EGG::Vector3f offset;
            const bool hit=K::Field::CollisionDirector::Instance()->checkCameraSphere(radius,
                K::EGG::Vector3f(eye.x,eye.y,eye.z),K::EGG::Vector3f(previous.x,previous.y,previous.z),offset,wall);
            push=vec(offset);return hit;
        };
        if(const auto* camera=K::Render::KartCamera::Instance()){
            in.lookBack=camera->lookingBack();in.rearEye=vec(camera->rearPosition());in.rearPivot=vec(camera->rearTarget());
        }
        in.kartPos=vec(kart->pos());in.kartForward=vec(kart->move()->smoothedForward());
        in.cameraDistY=kart->cameraDistY();in.targetPosY=params.targetPosY;in.cameraFov=params.fov;in.boostFov=boostFov;
        in.bullet=status.onBit(K::Kart::eStatus::Bullet);in.inCannon=status.onBit(K::Kart::eStatus::InCannon);
        in.cameraPosY=params.posY;in.cameraDistance=params.dist;
        in.stage=unsigned(race->stage());in.previewActive=intro.active();
        in.beforeRespawn=status.onBit(K::Kart::eStatus::BeforeRespawn);in.inRespawn=status.onBit(K::Kart::eStatus::InRespawn);
        in.respawnTimer=kart->collide()->respawnTimer();in.battleWipeSerial=kart->move()->battleWipeSerial();
        in.finished=raceGrid.finished();in.oobRequest=kart->collide()->oobCameraRequest();
        in.findArea=[](void*,int current,mkw::Vec3 p){return int(K::System::CourseMap::Instance()->getCurrentAreaID(int16_t(current),K::EGG::Vector3f(p.x,p.y,p.z),K::System::MapdataAreaBase::Type(0)));};
    };
    auto startCamera=[&](){
        if(!fullRace)return;
        auto* resources=K::System::ResourceManager::Instance();
        size_t size=0;const void* kmp=resources->getFile("course.kmp",&size,K::System::ArchiveId::Course);
        cameraDirector->setCourse(cameraCourse->load(kmp,size)?cameraCourse.get():nullptr);
        // RaceCameraMgr::ctor (PAL 0x805A8468): race_camera.bcp, else start_camera.bcp.
        const void* bcp=resources->getFile("race_camera.bcp",&size,K::System::ArchiveId::Core);
        if(!bcp||!startPan.load(bcp,size)){bcp=resources->getFile("start_camera.bcp",&size,K::System::ArchiveId::Core);if(!bcp||!startPan.load(bcp,size))startPan.defaults();}
        auto* kart=K::Kart::KartObjectManager::Instance()->object(0);
        K::EGG::Vector3f startPos,startAngles;K::System::RaceManager::Instance()->findKartStartPoint(startPos,startAngles,0);
        chaseCamera.init(*kart);boostFov=0;fillCameraInput(cameraInput);
        cameraDirector->reset(startPan,vec(startPos),vec(startAngles),cameraInput);
        flow={};flow.preview=intro.active();
        results.reset();resultsInput={};resultsReported=false;flowUi.unloadResults();
        {const auto* move=K::Kart::KartObjectManager::Instance()->object(0)->move();
         gpTelemetry.reset(K::System::RaceConfig::Instance()->raceScenario().players[0].driftIsAuto,
            move->miniTurboSerial(),move->offroadFrameSerial(),move->objectDamageSerial());}
        if(selectedRaceMode==0&&vsSet.race>=1&&vsSet.race<=4)vsSet.performanceCaptured[vsSet.race-1]=false;
        LOGF("race camera: start pan %u points, goal camera %d, camera areas %u",startPan.count,cameraCourse->goalCamera,unsigned(cameraCourse->areaCamera.size()));
    };
    // GPVSLeaderboardUpdate::FillRows (USA 0x8085A1C8): rows in finishing
    // order with the VS points before this race and the points it adds.
    auto fillResults=[&](){
        const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
        const unsigned n=std::min(raceGrid.count(),12u);
        std::array<unsigned,12> order{};for(unsigned i=0;i<n;++i)order[i]=i;
        std::stable_sort(order.begin(),order.begin()+n,[&](unsigned a,unsigned b){return battleMode?battleRank(a)<battleRank(b):raceGrid.rank(a)<raceGrid.rank(b);});
        if(battleMode){
            std::array<uint16_t,12> scores{};std::array<mkw::RaceTeam,12> teams;
            teams.fill(mkw::RaceTeam::None);
            for(unsigned i=0;i<n;++i){scores[i]=uint16_t(battleScore(i));teams[i]=mkw::RaceTeam(scenario.players[i].team);}
            battleSession.seal(scores,teams,n);
        }
        std::array<mkw::RaceResultEntry,12> rows{};
        int length=kQa?snprintf(line,sizeof line,"results race %u/%u:",vsSet.race,raceCount()):0;
        for(unsigned p=0;p<n;++p){
            const unsigned i=order[p];auto& r=rows[p];
            if(!battleMode)vsSet.finishPositions[i]=uint8_t(p+1);
            r.racer=uint8_t(i);r.character=uint8_t(scenario.players[i].character);r.human=i==0;
            r.team=scenario.teams?mkw::RaceTeam(scenario.players[i].team):mkw::RaceTeam::None;
            r.previous=battleMode?uint16_t(battleSession.previousScore(i)):vsSet.points[i];r.gain=battleMode?uint16_t(battleSession.roundScore(i)):timeTrial?0:uint16_t(mkw::RaceResultsFlow::points(n,p+1));
            if(!battleMode)vsSet.points[i]=uint16_t(std::min(99999u,unsigned(r.previous)+r.gain));
            if(kQa&&length>0&&size_t(length)<sizeof line)length+=snprintf(line+length,sizeof line-size_t(length)," %u:%u+%u",i,unsigned(r.previous),unsigned(r.gain));
        }
        report(line);
        // Time Trials: Next returns to the course choice; no set to finish.
        results.fill(rows.data(),n,battleMode?battleSession.complete():(!timeTrial&&vsSet.race>=raceCount()));
        if(battleMode)results.setBattle(battleSession.wins(mkw::RaceTeam::Red),battleSession.wins(mkw::RaceTeam::Blue),battleSession.target());
    };
    auto introInfo=[&](){return mkw::RaceIntroInfo{courseChoice,selectedMirror?3u:selectedEngineClass,courseChoice/4,vsSet.race?vsSet.race-1:0,raceCount(),selectedRaceMode==3&&menu.selection.coinRunners?4:selectedRaceMode,menu.selection.teams};};
    std::vector<std::unique_ptr<mkw::Model>> track,sky;
    unsigned raceWarmupFrames=0;
    auto startIntro=[&](){
        intro.stop();introClearance.clear();
        if(!fullRace)return;
        size_t size=0;const void* kmp=K::System::ResourceManager::Instance()->getFile("course.kmp",&size,K::System::ArchiveId::Course);
        if(intro.load(kmp,size)){
            mkw::IntroMeshClearance visibleScenery;
            if(Courses[courseChoice].id==K::Course::Dry_Dry_Ruins){
                for(const auto& model:track)for(const auto& sub:model->submeshes){
                    if(!mkw::playerShadow::receives(sub.flags))continue;
                    for(unsigned i=sub.first;i+2<sub.first+sub.count;i+=3)
                        visibleScenery.add(model->vertices[model->indices[i]].position,
                            model->vertices[model->indices[i+1]].position,model->vertices[model->indices[i+2]].position);
                }
                visibleScenery.build();
            }
            auto query=[](void* context,float radius,mkw::Vec3 center,mkw::Vec3& out){
                out={};bool hit=false;
                if(auto* collision=K::Field::CollisionDirector::Instance()){
                    K::EGG::Vector3f push;bool wall=false;
                    hit=collision->checkCameraSphere(radius,{center.x,center.y,center.z},{center.x,center.y,center.z},push,wall);
                    if(hit)out=vec(push);
                }
                mkw::Vec3 visiblePush;
                if(static_cast<mkw::IntroMeshClearance*>(context)->correction(radius,center,visiblePush)){
                    hit=true;if(mkw::dot(visiblePush,visiblePush)>mkw::dot(out,out))out=visiblePush;
                }
                return hit;
            };
            introClearance.build(intro.previewFrames(),query,&visibleScenery);
            LOGF("intro clearance preplanned=%u unresolved=%u",introClearance.frames(),introClearance.residual());
            intro.start();
        }
        LOGF("course preview %s: %u frames",intro.active()?"playing":"absent",intro.frames());
        introTick=0;introCaptured=~0u;
        if(intro.active()&&!flowUi.loadIntro(introInfo()))LOGF("course preview labels failed to load");
        K::System::RaceManager::Instance()->setIntroHeld(intro.active());
        startCamera();
    };
    auto endIntro=[&](){intro.stop();introClearance.clear();renderer.waitForGpu();flowUi.unloadIntro();K::System::RaceManager::Instance()->setIntroHeld(false);};
    // Race audio (docs/AUDIO_SOURCE.md "Race"): after the race's graphics, so its
    // residency fits the LINEAR they left.
    auto beginRaceSound=[&](){
        if(!fullRace)return true;
        const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
        mkw::RaceSound::Setup setup;setup.course=unsigned(Courses[courseChoice].id);setup.engineClass=scenario.engineClass;setup.mirror=scenario.mirror;
        setup.graphicsReserve=pauseTextureReserve();
        setup.coinBattle=coinRunnersMode;setup.mode=unsigned(scenario.gameMode);setup.preview=intro.active();
        setup.racers=std::min<unsigned>(scenario.playerCount,12);
        for(unsigned i=0;i<setup.racers;++i){const auto& p=scenario.players[i];setup.racer[i]={uint8_t(p.character),uint8_t(p.vehicle),p.type==K::System::RaceConfig::Player::Type::Local};}
        if(driverChoice>=24)setup.racer[0].miiVoice=selectedMiiVoice;
        std::destroy_at(&actors);std::construct_at(&actors);mkw::resetRaceAudioObjects();mkw::SoundSystem::instance().finishCarriedMusic();raceSound.begin(setup);
        return raceSound.prepareScene();
    };
    auto raceItems=std::make_unique<mkw::RaceItems>();
    mkw::RacerVisuals opponents;
    mkw::RaceHud raceHud;
    // The lower LCD during races (docs/BOTTOM_SCREEN.md); menus keep it black.
    mkw::RaceBottomScreen bottomScreen;
    static mkw::BottomView bottomView;
    drawPausedBottom=[&](){
        // Keep the last race snapshot frozen. No physics/roulette ticks move
        // while the upper LCD owns the pause menu.
        if(bottomScreen.active())bottomScreen.draw(renderer,bottomView);
        else renderer.drawScreenFade(1.f,false);
    };
    mkw::Lakitu lakitu;
    auto driverAnimationStorage=std::make_unique<mkw::DriverAnimation>();
    auto& driverAnimation=*driverAnimationStorage;
    mkw::DriverEffectAnchor driverEffectAnchor;
    mkw::DriverController driverController;
    mkw::DriverPose driverPose;
    mkw::VehicleAnimation vehicleAnimation;
    std::unique_ptr<mkw::LumaVisual> playerLuma;
    mkw::CameraShake cameraShake;uint32_t shakeActionStarts=~0u;mkw::LightningFlash lightningFlash;
    auto driftEffectsStorage=std::make_unique<mkw::DriftEffects>();
    auto& driftEffects=*driftEffectsStorage;
    mkw::Texture sparkTexture[3];
    auto raceEffectsStorage=std::make_unique<mkw::RaceEffects>();
    auto& raceEffects=*raceEffectsStorage;
    auto effectTextures=std::make_unique<std::vector<mkw::Texture>>();
    struct EffectIds{int flame,flameBurst,wheelie,itemBox,trick,speedLines,chip[6],shellTail[3],shellBreak[2],bombBlast,blueBlast,bananaBreak,blueBlastSmoke;} fx{};
    // Every racer's KartEffect power-up emitters (docs/POWERUPS_SOURCE.md); player 0 is local.
    mkw::KartFxSlots kartFxSlots;
    // Heap, like the other presentation state above: the slipstream timers grew it and
    // main's frame shares the 64 KiB main-thread stack.
    auto kartFxStorage=std::make_unique<std::array<mkw::KartItemEffects,mkw::KartFxSlots::MaxRacers>>();
    auto& kartFx=*kartFxStorage;
    std::array<K::Kart::KartMove::SoundCues,mkw::KartFxSlots::MaxRacers> kartContacts{};
    // EffectManager's kart effect LOD (docs/VFX_RESEARCH.md): the ranking, and each racer's
    // clip on the presented camera, taken after a step for the next one (race start: visible,
    // distance FLT_MAX).
    struct KartFxLod{mkw::KartEffectLod lod;std::array<mkw::EffectClip,mkw::KartFxSlots::MaxRacers> clip{};bool ready=false;};
    auto kartFxLodStorage=std::make_unique<KartFxLod>();
    auto& kartFxLod=*kartFxLodStorage;
    mkw::KartSmokeState playerSmoke;
    std::array<mkw::KartSurfaceEffects,12> surfaceEffects;
    auto remoteSmokeStorage=std::make_unique<mkw::RemoteKartSmoke>();
    auto& remoteSmoke=*remoteSmokeStorage;
    mkw::KartLandingEffects landingEffects;
    mkw::KartBrakeEffects brakeEffects;
    mkw::KartWheelSpinEffects wheelSpinEffects;
    mkw::OpponentDrivingEffects opponentDrivingEffects;
    mkw::BikeDriftEffects bikeDriftEffects;
    mkw::BikeScrapeEffects bikeScrapeEffects;
    auto creatureEffectsStorage=std::make_unique<mkw::CreatureEffects>();
    auto& creatureEffects=*creatureEffectsStorage;
    mkw::BlooperScreenEffects blooperScreenEffects;
    std::array<bool,12> lavaBeforeRespawn{};
    auto resetKartFx=[&](){lavaBeforeRespawn={};for(unsigned i=0;i<kartFx.size();++i)kartFx[i].reset(i,i==0);kartFxLod=KartFxLod{};playerSmoke={};for(auto& surface:surfaceEffects)surface.reset();remoteSmoke.reset();opponentDrivingEffects.reset();bikeDriftEffects.reset();bikeScrapeEffects.reset();landingEffects.reset();brakeEffects.reset();creatureEffects.reset();blooperScreenEffects.reset();driverEffectAnchor.reset();};
    resetKartFx();
    bool raceVfxReady=false;
    auto loadRaceVfx=[&](){
        if(raceVfxReady)return true;
        const char* sparks[]={"textures/drift_sparkTex2.t3x","textures/drift_flash2.t3x","textures/drift_rk_smoke2.t3x"};
        for(unsigned i=0;i<3;++i)if(!sparkTexture[i].ready&&!sparkTexture[i].load(sparks[i])){report("FAIL required drift texture");return false;}
        if(!driftEffects.load(replay?"textures/drift-effects-bike.bin":"textures/drift-effects.bin")){report("FAIL required drift definitions");return false;}
        char effectBank[64];snprintf(effectBank,sizeof effectBank,"textures/race-effects-%02u%s.bin",unsigned(Courses[courseChoice].id),battleMode?(coinRunnersMode?"-coin":"-balloon"):"");
        if(!raceEffects.ready&&!raceEffects.load(effectBank)){char failure[96];snprintf(failure,sizeof failure,"FAIL race-effects.bin: %s errno=%d",raceEffects.loadError(),raceEffects.loadErrno());report(failure);return false;}
        if(!mkw::courseEffectRootsPresent(raceEffects,unsigned(Courses[courseChoice].id))){report("FAIL required course effect roots");return false;}
        effectTextures->resize(raceEffects.textures.size());
        for(unsigned i=0;i<raceEffects.textures.size();++i){
            auto& texture=(*effectTextures)[i];
            if(texture.ready)continue;
            const char* name=raceEffects.textures[i].name;
            char path[64];snprintf(path,sizeof path,"textures/%s.t3x",name);
            bool vram=false;
            for(const char* slip:{"fx_rk_wave0","fx_rk_wave5_5","fx_rk_wave2"})vram|=!std::strcmp(name,slip);
            if(!(vram?texture.loadVram(path):texture.load(path))){char failure[96];snprintf(failure,sizeof failure,"FAIL effect texture %s",path);report(failure);return false;}
        }
        fx=EffectIds{
        raceEffects.find("rk_miniTurbo"),raceEffects.find("rk_miniTurbo1T"),raceEffects.find("rk_wheelie"),raceEffects.find("rk_itemBox"),
        raceEffects.find("rk_jumpSeikou"),raceEffects.find("rk_koukasen"),
        {raceEffects.find("rk_driftSpark1L_Chip00"),raceEffects.find("rk_driftSpark1R_Chip00"),raceEffects.find("rk_driftSpark2L_Chip00"),
         raceEffects.find("rk_driftSpark2R_Chip00"),raceEffects.find("rk_driftSpark1LB_Chip00"),raceEffects.find("rk_driftSpark1RB_Chip00")},
        {raceEffects.find("rk_kouraTailG"),raceEffects.find("rk_kouraTailR"),raceEffects.find("rk_kouraTailB")},
        {raceEffects.find("rk_kouraBrkG"),raceEffects.find("rk_kouraBrkR")},raceEffects.find("rk_bombHei"),raceEffects.find("rk_haneKoura"),raceEffects.find("rk_bananaSmk"),raceEffects.find("rk_bombBlue")};
        for(int spec:{fx.flame,fx.flameBurst,fx.wheelie,fx.itemBox,fx.trick,fx.speedLines,
                      fx.chip[0],fx.chip[1],fx.chip[2],fx.chip[3],fx.chip[4],fx.chip[5],
                      fx.shellTail[0],fx.shellTail[1],fx.shellTail[2],fx.shellBreak[0],fx.shellBreak[1],
                      fx.bombBlast,fx.blueBlast,fx.bananaBreak,fx.blueBlastSmoke})
            if(spec<0){report("FAIL required kart/item effect root");return false;}
        if(battleMode&&!coinRunnersMode&&(raceEffects.find("rk_baloonR")<0||raceEffects.find("rk_baloonB")<0)){report("FAIL original balloon burst roots");return false;}
        if(coinRunnersMode)for(const char* root:mkw::CoinPresentation::Roots)if(raceEffects.find(root)<0){report("FAIL original coin roots");return false;}
        if(!bikeDriftEffects.bind(raceEffects)){report("FAIL local bike chassis sparks");return false;}
        if(!bikeScrapeEffects.bind(raceEffects)){report("FAIL bike body scrape");return false;}
        if(!kartFxSlots.bind(raceEffects)){report("FAIL required power-up effect roots");return false;}
        if(!opponentDrivingEffects.bind(raceEffects)){report("FAIL required opponent driving effect roots");return false;}
        if(!remoteSmoke.bind(raceEffects)){report("FAIL required remote smoke roots");return false;}
        if(!brakeEffects.bind(raceEffects)){report("FAIL required bike brake smoke root");return false;}
        if(!landingEffects.bind(raceEffects)){report("FAIL required landing smoke roots");return false;}
        if(!creatureEffects.bind(raceEffects)){report("FAIL required creature effect roots");return false;}
        if(!blooperScreenEffects.bind(raceEffects)){report("FAIL required ink effect roots");return false;}
        raceVfxReady=true;
        return true;
    };
    auto unloadRaceVfx=[&](){
        raceEffects.unload();driftEffects.reset();driftEffects.ready=false;
        for(auto& t:sparkTexture)t.reset();for(auto& t:*effectTextures)t.reset();
        raceVfxReady=false;
    };
    std::array<int,16> tailActor;tailActor.fill(-1);
    std::unique_ptr<mkw::Model> vehicle,driver,vehicleShadow;
    std::unique_ptr<mkw::NativeMiiHead> playerMiiHead;
    std::array<std::unique_ptr<mkw::Model>,4> wheels;
    std::array<std::unique_ptr<mkw::Model>,9> accessories;
    std::unique_ptr<mkw::DriverAnimation> bodyAnimation;
    std::array<std::unique_ptr<mkw::DriverAnimation>,9> accessoryAnimations;
    struct PartClock {int clip=-1;unsigned start=0;};
    PartClock bodyClock;std::array<PartClock,9> accessoryClocks;
    auto partClip=[](mkw::DriverAnimation* bank,const K::Kart::KartObject& kart,bool handle){
        if(!bank)return -1;
        bool airborne=kart.status().offBit(K::Kart::eStatus::TouchingGround),tricking=kart.status().onBit(K::Kart::eStatus::InATrick);
        const char* name=handle&&bank->find("handle_drive")>=0?"handle_drive":tricking&&bank->find("sjump")>=0?"sjump":tricking&&bank->find("jump2")>=0?"jump2":airborne&&bank->find("jump")>=0?"jump":kart.status().onBit(K::Kart::eStatus::Boost)&&bank->find("dash")>=0?"dash":"shake";
        return bank->find(name);
    };
    auto prepareGarageFixture=[&](){
        if(!garageTest)return;
        auto* pad=K::System::KPadDirector::Instance()->hostController();
        pad->setInputsRawStick(0,7,7,K::System::Trick::None);
        for(unsigned i=0;i<412;++i)scenes.calc();
        vehicleAnimation.reset();vehicleAnimation.step(*K::Kart::KartObjectManager::Instance()->object(0));
        fixtureRolling=false;
    };
    auto loadDriver=[&](){
        mkw::Model::setLightingCourse(mkw::courseLighting(Courses[courseChoice].archive));
        bodyClock={};accessoryClocks={};
        driverEffectAnchor.reset();driverAnimation.reset();bodyAnimation.reset();vehicle.reset();driver.reset();playerMiiHead.reset();playerLuma.reset();vehicleShadow.reset();for(auto& w:wheels)w.reset();for(auto& a:accessoryAnimations)a.reset();for(auto& a:accessories)a.reset();
        if(!replay&&driverChoice>=24){
            playerMiiHead=std::make_unique<mkw::NativeMiiHead>();
            if(!playerMiiHead->load(selectedRaceMii)){report("FAIL selected Mii head");return false;}
        }
        const char* prefix=replay?"replay_":selectedVehicle().prefix;
        auto teamSuffix=[&](const char* name){return replay||!std::strcmp(name,"driver")||!std::strcmp(name,"shadow")?"":mkw::teamVehicleSuffix(selectedVehicle().vehicle,selectedTeams?selectedHumanTeam:mkw::RaceTeam::None);};
        auto part=[&](const char* name){auto model=std::make_unique<mkw::Model>();char path[128];snprintf(path,sizeof path,"models/%s%s%s.gmdl",prefix,teamSuffix(name),name);if(!model->load(path,true))model.reset();return model;};
        vehicle=part("body");driver=part("driver");if(!replay)vehicleShadow=part("shadow");
        static const char* wheelNames[]={"tire_fl","tire_fr","tire_rl","tire_rr"};
        bool bike=replay||selectedVehicle().bike;
        for(unsigned i=0;i<(bike?2u:4u);++i)wheels[i]=part(wheelNames[i]);
        auto animation=[&](const char* name,mkw::Model& model){auto bank=std::make_unique<mkw::DriverAnimation>();char path[128];snprintf(path,sizeof path,"models/%s%s%s.dskm",prefix,teamSuffix(name),name);if(!bank->load(path,model))bank.reset();else bank->prepareLitSkinning(model);return bank;};
        if(!replay){const auto& entry=selectedVehicle();
            if(vehicle&&entry.bodyAnimation)bodyAnimation=animation("body",*vehicle);
            for(unsigned i=0;i<accessories.size();++i)if(entry.extrasMask&(1u<<i)){
                accessories[i]=part(mkw::GarageExtraNames[i]);
                if(accessories[i]&&(entry.extraAnimationMask&(1u<<i)))accessoryAnimations[i]=animation(mkw::GarageExtraNames[i],*accessories[i]);
            }
        }
        driftEffects.load(bike?"textures/drift-effects-bike.bin":"textures/drift-effects.bin");
        vehicleAnimation.reset();vehicleAnimation.step(*K::Kart::KartObjectManager::Instance()->object(0));
        char path[128];snprintf(path,sizeof path,"models/%sdriver.dskm",prefix);
        bool ok=driver&&driverAnimation.load(path,*driver);driverAnimation.fullRate=true;driverController.reset();driverAnimation.resetLook();driverPose={};
        if(driver)driverAnimation.prepareLitSkinning(*driver);else driverAnimation.skinNormals(false);
        for(unsigned i=0;i<driverAnimation.count();++i)driverController.setDuration(driverAnimation.name(i),driverAnimation.duration(i));
        if(bike){static const char* kartNames[]={"sjump_a","sjump_b","sjump_c"};static const char* bikeNames[]={"sjump_ma","sjump_mb","sjump_mc"};
            for(unsigned i=0;i<3;++i){int clip=driverAnimation.find(bikeNames[i]);if(clip>=0)driverController.setDuration(kartNames[i],driverAnimation.duration(clip));}
        }
        LOGF("driver_loaded %s clips=%u animation=%d linear_free=%lu",prefix,driverAnimation.count(),ok,(unsigned long)linearSpaceFree());
        bool complete=ok&&vehicle&&(replay||vehicleShadow);
        if(!replay&&selectedVehicle().character==23){
            playerLuma=std::make_unique<mkw::LumaVisual>();
            complete=playerLuma->load()&&complete;
        }
        for(unsigned i=0;i<(bike?2u:4u);++i)complete&=bool(wheels[i]);
        if(!replay){const auto& entry=selectedVehicle();
            complete&=!entry.bodyAnimation||bool(bodyAnimation);
            for(unsigned i=0;i<accessories.size();++i){
                if(entry.extrasMask&(1u<<i))complete&=bool(accessories[i])&&vehicleAnimation.accessorySupported(mkw::GarageExtraNames[i]);
                if(entry.extraAnimationMask&(1u<<i))complete&=bool(accessoryAnimations[i]);
            }
        }
        if(complete&&playerMiiHead){
            const auto rgba=selectedMiiBody.clothingRGBA;
            const auto clothing=(rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba&255)<<24);
            char name[128];std::snprintf(name,sizeof name,"%sdriver",prefix);
            for(const auto& role:mkw::MiiRaceMaterials)if(!std::strcmp(role.model,name)){
                auto sub=std::find_if(driver->submeshes.begin(),driver->submeshes.end(),[&](const auto& s){return s.first==role.first&&s.count==role.count;});
                if(sub==driver->submeshes.end()){report("FAIL Mii material region");return false;}
                sub->constant=role.skin?playerMiiHead->skinConstant():clothing;
            }
        }
        if(complete&&!replay){
            driver->dynamicLightTextures=true;vehicle->dynamicLightTextures=true;
            for(auto& model:accessories)if(model)model->dynamicLightTextures=true;
            for(auto& model:wheels)if(model)model->dynamicLightTextures=true;
        }
        if(!complete)report("FAIL selected vehicle asset or animation load");
        return complete;
    };
    auto miiHeadTransform=[&](const C3D_Mtx& seated,C3D_Mtx& world){
        C3D_Mtx joint,local,attached;
        if(!playerMiiHead||!driverAnimation.headTransform(joint))return false;
        Mtx_Identity(&local);Mtx_Translate(&local,selectedMiiBody.headX,selectedMiiBody.headY,0,true);
        Mtx_RotateZ(&local,3.141592741f,true);Mtx_RotateY(&local,-1.570796371f,true);
        Mtx_Scale(&local,selectedMiiBody.headScale,selectedMiiBody.headScale,selectedMiiBody.headScale);
        Mtx_Multiply(&attached,&joint,&local);Mtx_Multiply(&world,&seated,&attached);return true;
    };
    mkw::CourseObjects courseObjects;
    courseObjects.setAllocationReserve(pauseTextureReserve());
    bool trackLoadFailed=false;
    int trackCourse=-1; // Course whose scenery is completely resident, or -1.
    auto resetRaceSystems=[&](bool reload){
        if(!fullRace)return;
        battleTicks=0;raceHud.speedometer=false;raceHud.resetBattleView();
        if(battleMode){
            size_t ruleSize=0;
            const void* ruleData=K::System::ResourceManager::Instance()->getFile("minigame.kmg",&ruleSize,K::System::ArchiveId::Core);
            mkw::BattleRules rules;
            if(!rules.load(ruleData,ruleSize,unsigned(Courses[courseChoice].id),raceGrid.count(),coinRunnersMode?mkw::BattleMode::Coin:mkw::BattleMode::Balloon)){
                trackLoadFailed=true;report("FAIL original battle rules load");
            }else{
                const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
                std::array<mkw::RaceTeam,12> teams;teams.fill(mkw::RaceTeam::None);
                if(scenario.teams)for(unsigned i=0;i<raceGrid.count();++i)teams[i]=mkw::RaceTeam(scenario.players[i].team);
                balloonBattle.configure(rules,teams);
                if(coinRunnersMode&&!coinBattle.reset(rules,raceGrid.count(),teams,unsigned(Courses[courseChoice].id))){
                    trackLoadFailed=true;report("FAIL original coin battle resources");
                }
            }
        }
        balloonBattle.reset(raceGrid.count());
        K::Kart::BattleEvents::SetHook([](void*,const K::Kart::BattleEvents::Event& event){
            auto* race=K::System::RaceManager::Instance();
            if(!battleMode||!race||!race->isStageReached(K::System::RaceManager::Stage::Race)||
                race->isStageReached(K::System::RaceManager::Stage::FinishGlobal))return false;
            const unsigned victim=unsigned(event.victim),attacker=unsigned(event.attacker);
            if(coinRunnersMode)return event.kind==K::Kart::BattleEvents::Kind::Steal?
                coinBattle.steal(attacker,victim):coinBattle.hit(victim,event.kind==K::Kart::BattleEvents::Kind::Fall);
            const bool changed=event.kind==K::Kart::BattleEvents::Kind::Steal?
                balloonBattle.stealBalloon(attacker,victim):balloonBattle.loseBalloon(victim,attacker);
            if(changed){
                if(event.kind==K::Kart::BattleEvents::Kind::Steal)balloonRuntime.steal(attacker,victim);
                else balloonRuntime.lose(victim,event.kind!=K::Kart::BattleEvents::Kind::Fall);
            }
            if(changed&&!balloonBattle.balloons(victim))
                K::Kart::KartObjectManager::Instance()->object(victim)->move()->loseAllBalloons();
            return changed;
        },nullptr);
        K::Kart::KartMove::SetBattleRefillHook([](void*,size_t racer){
            auto* race=K::System::RaceManager::Instance();
            if(battleMode&&!coinRunnersMode&&race&&race->isStageReached(K::System::RaceManager::Stage::Race)&&
                !race->isStageReached(K::System::RaceManager::Stage::FinishGlobal))
                if(balloonBattle.replenish(unsigned(racer)))balloonRuntime.replenish(unsigned(racer));
        },nullptr);
        if(!raceGrid.reset()){trackLoadFailed=true;report("FAIL course CPU route: race suspended; choose another map");}
        raceItems->resetFromCourse(raceGrid.count());
        if(battleMode&&!raceGrid.ai().setBattleSources(*raceItems,coinRunnersMode?&coinBattle:nullptr)){
            trackLoadFailed=true;report("FAIL original battle search graph");
        }
        if(!raceItems->oddsReady()){trackLoadFailed=true;report("FAIL original item odds load");}
        if(battleMode&&!coinRunnersMode){
            const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
            std::array<mkw::RaceTeam,12> teams;teams.fill(mkw::RaceTeam::None);
            for(unsigned i=0;i<raceGrid.count();++i)teams[i]=mkw::RaceTeam(scenario.players[i].team);
            if(!balloonRuntime.reset(raceGrid.count(),teams)){trackLoadFailed=true;report("FAIL original balloon models");}
        }
        if(timeTrial){raceItems->reset({},raceGrid.count());raceItems->give(0,mkw::ItemKind::TripleMushroom);}
        if(reload){if(!opponents.load()){trackLoadFailed=true;report("FAIL opponent graphics load");}}
        else opponents.resetAnimation();
        LOGF("race_grid count=%u opponents=%u route=%d item_boxes=%u",raceGrid.count(),opponents.count(),raceGrid.routeReady(),unsigned(raceItems->boxes().size()));
    };
    // The race bottom screen (docs/BOTTOM_SCREEN.md). Bloom's VRAM targets come
    // first, so the course's first bloom request (below) still finds its room.
    auto loadBottomScreen=[&](){
        const auto* bloom=mkw::courseBloom(Courses[courseChoice].archive);
        if(!effectsTest&&bloom&&bloom->enabled()&&!renderer.reserveBloom()){report("FAIL required bloom targets");return false;}
        if(!effectsTest&&!renderer.configurePlayerShadow(menu.selection.dynamicShadows))report("WARN player shadow target unavailable; native volume retained");
        if(!renderer.configureStadiumScreen(!effectsTest&&(int(Courses[courseChoice].id)==18||int(Courses[courseChoice].id)==6||int(Courses[courseChoice].id)==35))){report("FAIL stadium screen targets");return false;}
        if(!bottomScreen.load(renderer,courseChoice)){report("FAIL required bottom screen");return false;}
        if(driverChoice>=24&&!bottomScreen.setMiiPortrait(renderer,selectedRaceMii)){report("FAIL Mii map portrait");return false;}
        if(kQa){bottomScreen.describe(line,sizeof line);report(line);}
        return true;
    };
    // Reduced quality: the course's textures move to VRAM once bloom and the
    // bottom screen have theirs, which frees their LINEAR (render_quality.hpp).
    auto moveCourseTexturesToVram=[&](){
        if(!mkw::renderQuality().vramCourseTextures)return;
        renderer.waitForGpu();
        const auto reserve=mkw::RenderQuality::vramReserve;unsigned moved=0;
        for(auto* parts:{&track,&sky})for(auto& model:*parts)for(auto& t:model->textures)if(t)moved+=renderer.moveToVram(*t,reserve);
        moved+=courseObjects.moveTexturesToVram(renderer,reserve);
        LOGF("course textures in VRAM moved=%u linear_free=%lu vram_free=%lu",moved,(unsigned long)linearSpaceFree(),(unsigned long)vramSpaceFree());
    };
    // Course meshes and their spatial indices are loaded for a race and kept
    // across pause. Only race teardown/course changes release this scenery.
    auto loadSceneryMeshes=[&](){
        mkw::Model::setLightingCourse(mkw::courseLighting(Courses[courseChoice].archive));
        bool failed=false;
        track.clear();sky.clear();
        auto loadList=[&](const char* list,auto& parts){
            if(!*list)return;
            FILE* f=fopen(list,"r");
            if(!f){failed=true;report("FAIL course model list");return;}
            char name[64];while(fscanf(f,"%63s",name)==1){
                if(strchr(name,'/')||strstr(name,"..")){failed=true;break;}
                auto model=std::make_unique<mkw::Model>();char path[128];snprintf(path,sizeof path,"models/%s.gmdl",name);
                if(model->load(path))parts.push_back(std::move(model));else {failed=true;report(path);report("FAIL course part load");}
            }fclose(f);
            if(parts.empty())failed=true;
        };
        loadList(Courses[courseChoice].models,track);
        loadList(Courses[courseChoice].sky,sky);
        // map_model.brres is the minimap projection, not race scenery. Several
        // courses contain a full-size untextured map mesh that otherwise draws
        // a white sheet over the actual road.
        for(auto& model:track){
            model->enableStaticCulling(true);
            if(int(Courses[courseChoice].id)==12)model->enableCastleRegions();
        }
        return !failed&&!track.empty();
    };
    mkw::Vec3 playerShadowLight{};
    auto loadTrack=[&](){
        courseObjects.reset();
        trackLoadFailed=!loadSceneryMeshes();stage("course meshes");
        playerShadowLight=mkw::playerShadow::direction(mkw::courseLighting(Courses[courseChoice].archive));
        // One world-space sun direction for this loaded course: moving past
        // the decorative sun can never flip the light or enter a fallback.
        // Decorative sea/backdrops can extend far beyond the drivable map.
        // Use the already-loaded route bounds, once per load, so they cannot
        // rotate the sun vector (Coconut Mall's sea flipped its Z direction).
        mkw::Vec3 lightCenter{};
        const bool haveLightCenter=raceGrid.routeCenter(lightCenter);
        if(const auto* map=K::System::CourseMap::Instance())for(unsigned i=0;i<map->getGeoObjCount();++i){
            const auto* object=map->getGeoObj(i);
            if(haveLightCenter&&(object->id()==0x6f||object->id()==0x178)&&(object->presenceFlag()&1)){
                playerShadowLight=lightCenter-vec(object->pos());break;
            }
        }
        // HW105: Dad requested 180 degrees from HW104 and the existing reach cap.
        // One load-time direction; preserve the global and downhill safeguards.
        if(Courses[courseChoice].id==K::Course::Moonview_Highway){
            playerShadowLight={.119735f,-1.f,.557371f};
            playerShadowLight.y=-std::hypot(playerShadowLight.x,playerShadowLight.z)/mkw::playerShadow::MaxLengthPerHeight;
        }
        playerShadowLight=mkw::playerShadow::limitedDirection(playerShadowLight);
        if(Courses[courseChoice].id==K::Course::Coconut_Mall){
            if(!courseObjects.loadCoconut()){trackLoadFailed=true;report("FAIL Coconut object assets");}
            LOGF("Coconut palms=%u; escalators use live collision transforms",courseObjects.palmCount());
        }
        if(!courseObjects.loadCourse(unsigned(Courses[courseChoice].id))){trackLoadFailed=true;report("FAIL course object models");}
        stage("course objects");
        if(const auto* lighting=mkw::courseLighting(Courses[courseChoice].archive)){retailFog.reset(*lighting);retailFogArea=-1;}
        renderer.setRetailLighting(mkw::courseLighting(Courses[courseChoice].archive),false);
        if(!renderer.retailLightingReady(mkw::courseLighting(Courses[courseChoice].archive))){trackLoadFailed=true;report("FAIL required course lighting textures");}
        report(Courses[courseChoice].name);
        trackCourse=trackLoadFailed?-1:int(courseChoice);
    };
    // Drops every asset of the current race attempt; the GPU must be idle.
    // After a failed load the menus and the next attempt start from the same
    // memory as a first launch, with nothing half-built left behind.
    releaseRace=[&](){
        raceWarmupFrames=0;renderer.loadingFrame=false;
        notificationLed.clear();
        flushRaceSave();
        renderer.waitForGpu();balloonRuntime.clear();raceGrid.ai().clearBattleSearch();coinBattle.clear();
        renderer.configureStadiumScreen(false);renderer.configurePlayerShadow(false);
        raceGraphicsSuspended=false;
        opponents.reset();raceHud.reset();lakitu.unload();flowUi.unload();
        driverAnimation.reset();bodyAnimation.reset();for(auto& a:accessoryAnimations)a.reset();
        for(auto& a:accessories)a.reset();for(auto& w:wheels)w.reset();vehicle.reset();driver.reset();playerMiiHead.reset();playerLuma.reset();vehicleShadow.reset();
        sky.clear();track.clear();courseObjects.reset();trackCourse=-1;raceResident=false;intro.stop();
        renderer.setRetailLighting(nullptr);raceSound.quit();bottomScreen.unload(renderer);
        unloadRaceVfx();
    };
    decltype(driverController) pausedDriverController;
    decltype(driverPose) pausedDriverPose;
    decltype(bodyClock) pausedBodyClock;
    decltype(accessoryClocks) pausedAccessoryClocks;
    suspendRaceGraphics=[&](){
        if(!raceResident||raceGraphicsSuspended)return;
        mkw::AssetLoader::instance().stop();mkw::jobWorker().finishAll();renderer.waitForGpu();
        pausedDriverController=driverController;pausedDriverPose=driverPose;
        pausedBodyClock=bodyClock;pausedAccessoryClocks=accessoryClocks;
        menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release(renderer);renderer.waitForGpu();
        opponents.reset();driverAnimation.reset();bodyAnimation.reset();
        for(auto& a:accessoryAnimations)a.reset();for(auto& a:accessories)a.reset();for(auto& w:wheels)w.reset();
        vehicle.reset();driver.reset();playerMiiHead.reset();playerLuma.reset();vehicleShadow.reset();
        courseObjects.suspendGraphics();
        // Preserve track/sky meshes, shadow spatial indices, course masks and
        // render targets. Pause only releases racers and object presentation.
        raceGraphicsSuspended=true;
    };
    restoreRaceGraphics=[&](){
        if(!raceGraphicsSuspended)return true;
        mkw::LoadingScope loading;
        renderer.waitForGpu();menuRenderer.release(renderer);menuModels.reset(renderer);menuIcons.reset(renderer);renderer.waitForGpu();
        auto attempt=[&](){
            if(!loadDriver()||!opponents.load()||!courseObjects.resumeGraphics())return false;
            driverController=pausedDriverController;driverPose=pausedDriverPose;
            bodyClock=pausedBodyClock;accessoryClocks=pausedAccessoryClocks;
            moveCourseTexturesToVram();
            raceGraphicsSuspended=false;trackCourse=int(courseChoice);return true;
        };
        if(attempt())return true;
        // Retry after dropping every partially restored presentation, with the
        // race still frozen. Sound caches, items, native objects and EF stay live.
        opponents.reset();driverAnimation.reset();bodyAnimation.reset();
        for(auto& a:accessoryAnimations)a.reset();for(auto& a:accessories)a.reset();for(auto& w:wheels)w.reset();
        vehicle.reset();driver.reset();playerMiiHead.reset();playerLuma.reset();vehicleShadow.reset();
        courseObjects.suspendGraphics();
        return attempt();
    };
    auto loadRequiredRace=[&](){
        mkw::Model::setLightingCourse(mkw::courseLighting(Courses[courseChoice].archive));
        mkw::LoadingScope loading;
        // Keep the retail confirmation/rev playing while graphics load.
        // The empty-engine race residency reset still happens before planning
        // race banks, but must not truncate the departing menu cue.
        bool finishingMenuCue=mkw::SoundSystem::instance().slotAttached(mkw::MenuSound::RaceConfirmSlot);
        resourceFailure[0]=0;
        auto cleanup=[&](){
            mkw::jobWorker().finishAll();renderer.waitForGpu();
            releaseRace();menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release(renderer);
            menuAudio.close();
            if(!finishingMenuCue){
                auto& sound=mkw::SoundSystem::instance();
                if(sound.carryingMusic()){
                    constexpr uint32_t groups[]={mkw::snd::id::group::GRP_CMN};
                    sound.loadScene(groups,1);
                }else sound.stop();
            }
            renderer.clearUnusedTextureCache();mkw::pack::close();mkw::pack::open();
        };
        auto attempt=[&](){
            trackLoadFailed=false;
            // Begin from an empty audio scene: menu residency/masks cannot
            // suppress race waves or consume the race's allocation budget.
            if(fullRace&&!finishingMenuCue&&!mkw::SoundSystem::instance().carryingMusic()){raceSound.quit();mkw::SoundSystem::instance().stop();
                if(!mkw::SoundSystem::instance().start()){report("FAIL required sound engine");return false;}}
            stage("sound engine");
            if(!loadRaceVfx())return false;stage("race effects");
            if(fullRace&&!lakitu.load()){report("FAIL required Lakitu");return false;}
            stage("Lakitu");
            loadTrack();if(trackLoadFailed)return false;stage("course lighting");
            if(!loadDriver()){report("FAIL required player graphics");return false;}
            stage("player graphics");
            if(fullRace&&!raceHud.load()){report("FAIL required item/HUD graphics or animation");return false;}
            stage("items and HUD");
            if(fullRace&&!flowUi.load()){report("FAIL required race transitions");return false;}
            stage("race transitions");
            if(fullRace&&!loadBottomScreen())return false;stage("bottom screen");
            moveCourseTexturesToVram();stage("texture VRAM transfer");
            resetRaceSystems(true);if(trackLoadFailed)return false;stage("grid and opponents");
            if(fullRace){
                startIntro();
                if(intro.active()&&!flowUi.loadIntro(introInfo())){report("FAIL required preview labels");return false;}
                stage("intro labels");
                if(finishingMenuCue){
                    const auto cueWait=svcGetSystemTick();
                    auto& sound=mkw::SoundSystem::instance();
                    while(sound.slotAttached(mkw::MenuSound::RaceConfirmSlot)&&
                          svcGetSystemTick()-cueWait<7ULL*SYSCLOCK_ARM11){
                        if(!aptMainLoop())return false;
                        svcSleepThread(16666667);
                    }
                    sound.stop();finishingMenuCue=false;
                    if(!sound.start()){report("FAIL required sound engine");return false;}
                }
                if(!beginRaceSound()){report("FAIL required race sound cache budget");return false;}
                stage("sound cache plan");
                const auto started=svcGetSystemTick();
                while(!raceSound.resourcesReady()){
                    // Slow SD cards may need a long first read. Only a
                    // prolonged failure aborts this complete-load attempt.
                    if(!aptMainLoop()||(svcGetSystemTick()-started)>120ULL*SYSCLOCK_ARM11){report("FAIL required race sound banks/cache loading");return false;}
                    svcSleepThread(20*1000000LL);
                }
                stage("sound cache ready");
                courseObjects.setAllocationReserve(pauseTextureReserve());
            }
            // Execute and retire the first complete render, then its reuse
            // frame while still black. Resource allocation alone does not
            // prime pose uploads, shadow captures or their GPU dependencies.
            raceWarmupFrames=fullRace?2u:0u;
            if(fullRace)K::System::RaceManager::Instance()->setIntroHeld(true);
            raceResident=fullRace;return true;
        };
        // Release the preceding race before the first attempt too. A second
        // race must not need room for both generations of graphics/audio.
        cleanup();stage("race cleanup");
        const bool ok=mkw::loadRequiredResources(attempt,cleanup);
        trackLoadFailed=!ok;
        return ok;
    };
    // Old 3DS draws reduced detail; full-quality.txt keeps every draw for QA.
    // Set before the first race loads: some reductions apply at load.
    bool new3ds=false;APT_CheckNew3DS(&new3ds);
    mkw::renderQuality().setReduced(!new3ds&&!(kQa&&exists("sdmc:/3ds/mkw-3ds/full-quality.txt")));
    mkw::renderQuality().mipmaps=!exists("sdmc:/3ds/mkw-3ds/diagnose-no-mipmaps.txt");
    report(mkw::renderQuality().mipmaps?"HW13 authored material mip filters/LOD bias":"LOD0 fallback: full chain retained");
    report(mkw::renderQuality().reduced?"render quality reduced":"render quality full");
    if(dataReady){
        K::System::RaceConfig::RegisterInitCallback(configure,&raceGrid);
        stage("start");report(Courses[courseChoice].name);{mkw::LoadingScope loading;scenes.changeScene(0);}report("Kinoko race initialized");stage("kinoko scene");
        if((kQa&&exists("sdmc:/3ds/mkw-3ds/roster-test.txt"))||(kQa&&exists("sdmc:/3ds/mkw-3ds/garage_controls-test.txt"))){
            bool garageControls=(kQa&&exists("sdmc:/3ds/mkw-3ds/garage_controls-test.txt"));
            unsigned count=garageControls?72:12;
            bool pass=true;
            for(unsigned choice=0;choice<count;++choice){
                driverChoice=garageControls?choice/mkw::VehiclesPerDriver:choice;vehicleChoice=garageControls?choice%mkw::VehiclesPerDriver:0;
                if(choice){scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);}
                pass&=mkw::runControlChecks(scenes);
                char path[96];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/controls-choice-%u.json",garageControls?driverChoice*mkw::VehiclesPerDriver+vehicleChoice:driverChoice);rename("sdmc:/3ds/mkw-3ds/controls-result.json",path);
            }
            if(FILE* f=fopen(garageControls?"garage_controls-result.json":"roster-result.json","w")){fprintf(f,"{\"pass\":%s,\"characters\":%u}\n",pass?"true":"false",count);fclose(f);}
            for(auto& texture:sparkTexture)texture.reset();for(auto& texture:*effectTextures)texture.reset();mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();return pass?0:3;
        }
        if((kQa&&exists("sdmc:/3ds/mkw-3ds/controls-test.txt"))){bool ok=mkw::runControlChecks(scenes);report(ok?"CONTROLS PASS":"CONTROLS FAIL");for(auto& texture:sparkTexture)texture.reset();for(auto& texture:*effectTextures)texture.reset();mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();return ok?0:3;}
        LOGF("race_heap_free=%lu linear_free=%lu",(unsigned long)scenes.currentScene()->heap()->getAllocatableSize(),(unsigned long)linearSpaceFree());
        loadRequiredRace();
        prepareGarageFixture();
        LOGF("graphics_loaded track_parts=%u vehicle=%d driver=%d linear_free=%lu vram_free=%lu",unsigned(track.size()),!!vehicle,!!driver,(unsigned long)linearSpaceFree(),(unsigned long)vramSpaceFree());
    }else report("WAIT required game data absent");
    if(qaMode)showControls(dataReady);
    if(dataReady&&(kQa&&exists("sdmc:/3ds/mkw-3ds/objects-test.txt"))){
        // QA: each course object type viewed in place, two captures 20 ticks apart.
        std::vector<unsigned> choices;
        if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/objects-test.txt","r")){int id;while(fscanf(f,"%d",&id)==1)for(unsigned c=0;c<CourseCount;++c)if(int(Courses[c].id)==id&&courseAvailable(c))choices.push_back(c);fclose(f);}
        if(choices.empty())for(unsigned c=0;c<CourseCount;++c)if(int(Courses[c].id)<32&&courseAvailable(c))choices.push_back(c);
        bool ok=true;unsigned shots=0,targetsTotal=0,tick=0;std::vector<uint8_t> pixels(400*240*3);
        // Optional framing per target: "course name eyeX eyeY eyeZ lookX lookY lookZ".
        struct Framing{int course;char name[32];mkw::Vec3 eye,look;};std::vector<Framing> framings;
        if(kQa)if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/objects-camera.txt","r")){Framing c{};while(fscanf(f,"%d %31s %f %f %f %f %f %f",&c.course,c.name,&c.eye.x,&c.eye.y,&c.eye.z,&c.look.x,&c.look.y,&c.look.z)==8)framings.push_back(c);fclose(f);}
        auto* pad=K::System::KPadDirector::Instance()->hostController();
        auto simulate=[&](unsigned frames){for(unsigned i=0;i<frames;++i){pad->setInputsRawStick(0,7,7,K::System::Trick::None);scenes.calc();++tick;}};
        // Review frames take the race's deferred path; each capture waits for its frame.
        renderer.setDeferred(true);
        for(unsigned choice:choices){
            courseChoice=choice;
            renderer.waitForGpu();
            scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);courseObjects.invalidate();
            loadTrack();
            if(trackLoadFailed){ok=false;LOGF("objects course %u load failed",unsigned(Courses[choice].id));continue;}
            // Past the 172-tick intro and 240-tick countdown: race-only motion runs.
            simulate(480);
            const auto targets=courseObjects.reviewTargets();targetsTotal+=unsigned(targets.size());
            const auto start=vec(K::Kart::KartObjectManager::Instance()->object(0)->pos());
            for(const auto& target:targets){
                LOGF("objects target %u %s %.0f %.0f %.0f %.0f",unsigned(Courses[choice].id),target.name,target.center.x,target.center.y,target.center.z,target.radius);
                for(char shot='a';shot<='b';++shot){
                    mkw::Vec3 center;float radius;
                    if(!courseObjects.reviewBounds(target.visual,center,radius))break;
                    // View from the nearest respawn point (on the road, as a racer
                    // sees it), backed off to frame the object.
                    const float distance=std::clamp(radius*2.6f,450.f,14000.f);
                    mkw::Vec3 road=start;float best=1e30f;
                    const auto* map=K::System::CourseMap::Instance();
                    for(unsigned j=0;j<map->getJugemPointCount();++j){const auto p=vec(map->getJugemPoint(j)->pos());const float dd=mkw::length(p-center);if(dd<best){best=dd;road=p;}}
                    mkw::Vec3 toward=road-center;toward.y=0;toward=mkw::normalized(toward,{0,0,1});
                    const float rise=std::clamp(road.y+250.f-center.y,-distance*.5f,distance*.6f);
                    // Large drivable objects (roads, nets, bridges) are framed from above.
                    const float lift=radius>2500.f?distance*.5f:distance*.15f;
                    mkw::Vec3 eye=center+toward*distance+mkw::Vec3{0,std::max(rise,lift),0};
                    for(const auto& f:framings)if(f.course==int(Courses[choice].id)&&!strcmp(f.name,target.name)){eye=f.eye;center=f.look;}
                    renderer.animationFrame=tick;renderer.begin(eye,center,{0,1,0});
                    C3D_Mtx identity;Mtx_Identity(&identity);
                    renderer.animationFrame=tick;renderer.setCourseLighting(true);
                    for(auto& model:sky)renderer.draw(*model,identity,true);
                    courseObjects.drawSky(renderer,tick,eye);
                    for(auto& model:track)drawCourseLayer(renderer,*model,identity,int(Courses[choice].id),false);
                    courseObjects.draw(renderer,tick,eye);
                    for(auto& model:track)drawCourseLayer(renderer,*model,identity,int(Courses[choice].id),true,true);
                    for(auto& model:track)drawCourseLayer(renderer,*model,identity,int(Courses[choice].id),true);
                    courseObjects.drawWater(renderer);
                    renderer.setCourseLighting(false);renderer.end();
                    char path[112];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/objects-%02u-%s-%c.ppm",unsigned(Courses[choice].id),target.name,shot);
                    if(renderer.captureTop(pixels.data())){if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);++shots;}else ok=false;}else ok=false;
                    if(shot=='a')simulate(20);
                }
            }
            LOGF("objects course %u: %u types, %u models, linear_free=%lu",unsigned(Courses[choice].id),unsigned(targets.size()),courseObjects.modelCount(),(unsigned long)linearSpaceFree());
        }
        if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/objects-result.json","w")){fprintf(f,"{\"pass\":%s,\"courses\":%u,\"targets\":%u,\"shots\":%u}\n",ok?"true":"false",unsigned(choices.size()),targetsTotal,shots);fclose(f);}
        renderer.waitForGpu();courseObjects.reset();track.clear();sky.clear();for(auto& texture:sparkTexture)texture.reset();mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();return ok?0:5;
    }
    std::vector<mkw::UiQuad> raceQuads;
    mkw::PresentationCadence presentation;
    // Simulation stays at 60 Hz on every model. Old 3DS presents races at
    // 30 FPS. New 3DS and New 2DS XL present at 60, or at 30 while the 3D
    // slider is up (stereo draws every frame twice). Dual-screen Intro also
    // presents every two ticks; its camera and simulation still step at 60 Hz.
    auto raceTicksPerFrame=[&]{return mkw::racePresentationTicks(new3ds,renderer.stereoWanted(),fullRace&&intro.active());};
    presentation.setTicksPerSlot(raceTicksPerFrame());
    LOGF("race presentation every %u ticks",presentation.ticksPerSlot());
    // accumulator: simulation ticks owed (1 = 1/60 s). Races owe one per top-screen
    // refresh (59.83 Hz), as retail 3DS games step: a 60.00 Hz clock would gain a
    // tick on the refresh every 6 s and present that frame with an extra tick.
    auto previous=svcGetSystemTick();double accumulator=0,physicsMs=0,maxPhysics=0;unsigned frames=0,restarts=0;
    u32 lastRefresh=C3D_FrameCounter(0);
    unsigned physicsSteps=0;
    mkw::AnimationBudget animationBudget;
    // The presented camera of the current step: drawn each frame and handed to the NW4R
    // effects as their process camera after each step.
    auto presentationCamera=[&](mkw::Vec3& eye,mkw::Vec3& target){
        if(effectsTest&&frames<60){eye={-15800,2700,49500};target={-15900,1200,51000};return;}
        if(intro.active()){
            const auto offset=introClearance.offset(intro.frame());
            eye=intro.eye()+offset;target=intro.target()+introClearance.targetOffset(intro.frame());return;
        }
        if(fullRace){const auto v=cameraDirector->view();eye=v.eye+cameraShake.offset(v.eye,v.target);target=v.target;return;}
        auto* camera=K::Render::KartCamera::Instance();auto* cameraKart=K::Kart::KartObjectManager::Instance()->object(0);
        eye=vec(camera->position());target=vec(camera->target());
        target.y+=cameraKart->param()->camera().targetPosY-cameraKart->cameraDistY();
    };
    mkw::Job playerJob;bool playerJobQueued=false;
    const bool smoke=(kQa&&exists("sdmc:/3ds/mkw-3ds/smoke-test.txt"));
    bool capturedBlue=false;
    bool replayPass=true;unsigned finishes=0;double stepTotal=0,stepMax=0;unsigned replayHeap[2]{};
    uint64_t lastRaceFrame=0;
    // The animation fixtures read posed vertices back while racing: immediate.
    const bool deferredRace=!animationTest;
    renderer.setDeferred(deferredRace);
    while(aptMainLoop()){
        // Poll controls and advance source state once per top-screen refresh.
        // Submit once per presentation slot. FrameBegin below retires
        // the prior GPU queue without inserting a second VBlank wait.
        // No presentation job may run while anything below can step the race.
        mkw::jobWorker().finishAll();
        if(awardPending){
            const auto awardTransitionStart=svcGetSystemTick();
            awardPending=false;
            // The result snapshot owns its data. Drain every user of the old
            // race before freeing its child heap; the root keeps configuration
            // and archive/input services alive for the next menu selection.
            mkw::AssetLoader::instance().stop();renderer.waitForGpu();
            releaseRace();menuAudio.close();menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release(renderer);
            if(!mkw::SoundSystem::instance().carryingMusic())mkw::SoundSystem::instance().stop();renderer.clearUnusedTextureCache();
            results.reset();resultsInput={};nextRaceQueued=false;
            if(scenes.currentSceneId()==2){
                scenes.currentScene()->heap()->enableAllocation();
                scenes.destroyCurrentSceneNoIncoming(false);
            }
            auto outcome=mkw::runAwardPodium(renderer,awardResults,report,awardTransitionStart,&scenes);
            const unsigned slot=std::min(menu.selection.license,3u);
            const auto ending=mkw::staffEnding(licenseSaves[slot],awardResults);
            if(outcome==mkw::AwardSceneResult::Complete&&ending!=mkw::StaffEnding::None){
                outcome=mkw::runStaff(renderer,awardResults,licenseSaves[slot].nativeMii,
                    ending==mkw::StaffEnding::True,report,scenes);
                if(outcome==mkw::AwardSceneResult::Complete){
                    auto updated=licenseSaves[slot];const unsigned bit=ending==mkw::StaffEnding::True?56:57;
                    updated.unlockFlags[bit/32]|=1u<<(bit%32);
                    mkw::LoadingScope loading;
                    if(mkw::writeLicenseSave(DATA,slot,updated))licenseSaves[slot]=std::move(updated);
                    else menu.saveFailed=true;
                }
            }
            awardResults={};
            if(outcome==mkw::AwardSceneResult::Exit)break;
            menuSound.resetAfterEngineRestart();
            menu.enter(outcome==mkw::AwardSceneResult::Failed?mkw::UiPage::Error:mkw::UiPage::MainMenu);
            sectionFadeIn=true;renderer.setDeferred(deferredRace);
            accumulator=0;previous=svcGetSystemTick();lastRefresh=C3D_FrameCounter(0);presentation.reset();
            continue;
        }
        // Races wait for the next refresh unless one has passed since the last
        // count. citro3d counts it before libctru sets the (sticky) VBlank0 event,
        // so a stale event costs one recheck and no refresh is ever missed.
        if(fullRace)for(unsigned waits=0;C3D_FrameCounter(0)==lastRefresh&&waits<3;++waits)gspWaitForEvent(GSPGPU_EVENT_VBlank0,false);
        hidScanInput();auto held=hidKeysHeld(),down=hidKeysDown();
        const bool endMenu=menu.page==mkw::UiPage::Results&&results.phase()==mkw::RaceFlowPhase::Menu;
        gyroTrick.read(fullRace&&!qaMode&&!autoplay&&!replay&&dataReady&&
                       menu.page==mkw::UiPage::Race&&!intro.active()&&!results.active());
        if(autoplay){if(intro.active())held=down=0;else autopilot->race(held,down,physicsSteps,results.active(),endMenu,menu.cursor);if(autopilot->finished())break;}
#ifdef MKW_FRAME_CAPTURE
        // Diagnostic build only: L+R+Down starts a bounded RAM capture.
        // Consume the chord so it does not use an item or hop while arming.
        if(fullRace&&!intro.active()&&menu.page==mkw::UiPage::Race&&
           (held&(KEY_L|KEY_R))==(KEY_L|KEY_R)&&(down&KEY_DDOWN))profiler->start();
        if((held&(KEY_L|KEY_R))==(KEY_L|KEY_R)&&(held&KEY_DDOWN)){
            held&=~u32(KEY_L|KEY_R|KEY_DDOWN);down&=~u32(KEY_L|KEY_R|KEY_DDOWN);
        }
#endif
        if(raceWarmupFrames)held=down=0;
        if(fullRace){
            // A or START skips the course preview; that press does nothing else.
            if(intro.active()&&menu.page==mkw::UiPage::Race&&(down&(KEY_A|KEY_START))){endIntro();down&=~u32(KEY_A|KEY_START);}
            if(results.active()){
                // Result boards take A/B/START (Leaderboard, USA 0x8083A5E0);
                // the end-of-race menu moves with the D-pad and confirms with A.
                // Once the RaceHUD page has ended START no longer pauses.
                if(down&(KEY_A|KEY_B|KEY_START))resultsInput.advance=true;
                if(endMenu){
                    const u32 nav=down|(autoplay?0u:hidKeysDownRepeat());
                    const unsigned oldCursor=menu.cursor;
                    if(nav&(KEY_DUP|KEY_CPAD_UP))menu.input({true});
                    else if(nav&(KEY_DDOWN|KEY_CPAD_DOWN))menu.input({false,true});
                    results.setCursor(menu.cursor);
                    if(menu.cursor!=oldCursor)mkw::SoundSystem::instance().play(mkw::snd::id::SE_UI_BTN_IN,0);
                    if(down&KEY_A)resultsInput.accept=true;
                }
                if(results.hudGone())down&=~u32(KEY_START);
            }
            if(down&KEY_START)menu.input({false,false,false,false,false,false,true});
            // Mario Kart 7: a tap on the lower screen switches its map view.
            if((down&KEY_TOUCH)&&menu.page==mkw::UiPage::Race&&!intro.active())bottomScreen.toggleView();
            if(menu.page!=mkw::UiPage::Race&&menu.page!=mkw::UiPage::Results){
                if(menu.page==mkw::UiPage::Pause)raceSound.pause(true);
                // A Grand Prix's next race starts without visiting the menus.
                auto action=nextRaceQueued?mkw::UiAction::StartRace:runMenu();nextRaceQueued=false;renderer.setDeferred(deferredRace);if(action==mkw::UiAction::Exit)break;
                if(action==mkw::UiAction::ResumeRace){
                    if(restoreRaceGraphics&&restoreRaceGraphics())raceSound.pause(false);
                    else {report("FAIL resume graphics load");releaseRace();menu.enter(mkw::UiPage::Error);}
                }
                if(action==mkw::UiAction::StartRace||action==mkw::UiAction::RestartRace){
                    mkw::LoadingScope loading;
                    raceGraphicsSuspended=false;
                    raceLoadStart=svcGetSystemTick();
                    renderer.waitForGpu();menuRenderer.release();
                    applySelection();startVsRace(action==mkw::UiAction::RestartRace);
                    // A different or previously failed course reloads completely;
                    // restarting the same race keeps its scenery.
                    const bool reloadTrack=trackCourse!=int(courseChoice);
                    if(reloadTrack){courseObjects.reset();track.clear();sky.clear();}
                    stage("start");
                    // Kinoko cannot recover from a missing course archive, so check first.
                    trackLoadFailed=!courseAvailable(courseChoice);
                    if(trackLoadFailed)report("FAIL selected course data absent");
                    else{
                        intro.stop(); // Its rail belongs to the scene being replaced.
                        scenes.currentScene()->heap()->enableAllocation();
                        if(scenes.currentSceneId()==0)scenes.createChildScene(2,scenes.currentScene());
                        else scenes.changeSiblingScene(2);
                        courseObjects.invalidate();stage("kinoko scene");
                        driftEffects.reset();raceEffects.reset();resetKartFx();cameraShake.reset();shakeActionStarts=~0u;lightningFlash={};boostFov=0;driverPose={};wasTricking=false;wasBoosting=false;trickModel.reset();++restarts;
                        loadRequiredRace();
                    }
                    raceResident=!trackLoadFailed;if(!trackLoadFailed)menu.raceStarted();else menu.enter(mkw::UiPage::Error);uiRace={};
                }
                accumulator=0;previous=svcGetSystemTick();lastRefresh=C3D_FrameCounter(0);presentation.reset();continue;
            }
        }else if(down&KEY_START)break;
        auto now=svcGetSystemTick();double delta=double(now-previous)/SYSCLOCK_ARM11;previous=now;
        const u32 refresh=C3D_FrameCounter(0),refreshes=refresh-lastRefresh;lastRefresh=refresh;
        // Races: whole refreshes, at most six (the clock's 0.1 s cap). Elsewhere, or
        // should the refresh count ever stall, the clock.
        accumulator+=fullRace&&refreshes?double(std::min<u32>(refreshes,6)):std::min(delta,0.1)*60.0;
        if(raceWarmupFrames)accumulator=1; // settle initial support, never catch up loading wall time
        profiler->begin(unsigned(Courses[courseChoice].id),frames,physicsSteps,float(delta*1000));
        if(dataReady){
            if(trackLoadFailed){
                accumulator=0;
                // Unwind the partial race before the error page's menu runs.
                if(fullRace){renderer.waitForGpu();releaseRace();menu.enter(mkw::UiPage::Error);}
                else if(kQa)printf("\x1b[18;1HCourse load/route failed");
                gspWaitForVBlank();continue;
            }
            if(animationTest&&frames&&frames%fixtureFrames==0&&frames/fixtureFrames<fixtureChoices){
                renderer.waitForGpu();unsigned index=frames/fixtureFrames;
                driverChoice=garageTest?index/mkw::VehiclesPerDriver:index;vehicleChoice=garageTest?index%mkw::VehiclesPerDriver:0;
                scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);courseObjects.invalidate();
                animationPass&=loadDriver();prepareGarageFixture();driftEffects.reset();raceEffects.reset();resetKartFx();accumulator=0;
            }
            if(fullRace&&(down&KEY_SELECT))raceHud.speedometer=!raceHud.speedometer;
            if(qaMode&&((down&KEY_SELECT)||(!replay&&(down&(KEY_X|KEY_Y))))){
                renderer.waitForGpu();
                if(!replay&&(down&(KEY_X|KEY_Y))){
                    if(held&KEY_B)vehicleChoice=(vehicleChoice+mkw::VehiclesPerDriver+(down&KEY_Y?1:-1))%mkw::VehiclesPerDriver;
                    else driverChoice=(driverChoice+mkw::StarterDrivers.size()+(down&KEY_Y?1:-1))%mkw::StarterDrivers.size();
                }
                scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);courseObjects.invalidate();
                if(down&(KEY_X|KEY_Y))loadDriver();
                accumulator=0;driftEffects.reset();raceEffects.reset();resetKartFx();cameraShake.reset();shakeActionStarts=~0u;lightningFlash={};boostFov=0;driverController.reset();driverAnimation.resetLook();driverPose={};vehicleAnimation.reset();vehicleAnimation.step(*K::Kart::KartObjectManager::Instance()->object(0));bodyClock={};accessoryClocks={};wasTricking=false;wasBoosting=false;trickModel.reset();restarts++;report("race restarted");
                resetRaceSystems(bool(down&(KEY_X|KEY_Y)));
            }
            circlePosition pad;hidCircleRead(&pad);
            // QA autoplay owns every input: a stray host controller (Azahar's UDP
            // input server) must not steer the scripted race.
            if(autoplay||raceWarmupFrames)pad={0,0};
            unsigned buttons=(held&KEY_A?1:0)|(held&KEY_B?2:0)|(held&KEY_L?4:0)|(held&KEY_R?(held&KEY_A?10:2):0);
            if(fullRace)buttons&=~4u; // Inventory owns L in races; retain stock mushrooms in trace/QA.
            auto trick=K::System::Trick::None;
            if(held&(KEY_DUP|KEY_ZR))trick=K::System::Trick::Up;else if(held&KEY_DDOWN)trick=K::System::Trick::Down;
            else if(held&KEY_DLEFT)trick=K::System::Trick::Left;else if(held&KEY_DRIGHT)trick=K::System::Trick::Right;
            // The D-pad is out of reach while the thumb steers on the Circle Pad.
            // The pad picks the direction like a Wii Remote shake with the stick
            // held; neutral is Up (a wheelie on bikes).
            auto padTrick=[&]{
                constexpr int Tilt=60;
                if(pad.dy<=-Tilt&&std::abs(pad.dy)>=std::abs(pad.dx))return K::System::Trick::Down;
                if(pad.dx<=-Tilt&&std::abs(pad.dx)>std::abs(pad.dy))return K::System::Trick::Left;
                if(pad.dx>=Tilt&&std::abs(pad.dx)>std::abs(pad.dy))return K::System::Trick::Right;
                return K::System::Trick::Up;
            };
            // R off a ramp (Mario Kart 7): a press shortly before or on a trick
            // ramp's lip (Kinoko's Trickable window) tricks instead of hopping.
            // Y tricks/wheelies anywhere; hold X or ZL for rear view. ZR equals D-pad Up. QA owns X/Y.
            static unsigned rampTrickFrames=0;
            if(down&KEY_R)rampTrickFrames=8;else if(rampTrickFrames)--rampTrickFrames;
            const bool rampTrick=fullRace&&rampTrickFrames&&(held&KEY_R)&&
                K::Kart::KartObjectManager::Instance()->object(0)->status().onBit(K::Kart::eStatus::Trickable);
            if(trick==K::System::Trick::None){
                if(rampTrick){trick=padTrick();buttons=(buttons&~10u)|(held&KEY_B?2u:0u);}
                else if(!qaMode&&(held&KEY_Y))trick=padTrick();
            }
            if(auto* camera=K::Render::KartCamera::Instance())
                camera->requestLookBack(fullRace&&!qaMode&&!replay&&!raceGrid.finished()&&bool(held&(KEY_X|KEY_ZL)));
            // Preserve the Wii input lattice used by ghost records.
            int stick=mkw::circlePadSteering(pad.dx);
            if(garageTest){buttons=1;stick=frames%fixtureFrames<32?-4:4;}
            auto* controller=K::System::KPadDirector::Instance()->hostController();
            auto start=svcGetSystemTick();
            if(replay||effectsTest||animationTest)accumulator=1;
            // QA race captures: lockstep, two source ticks per presented frame, so
            // pacing-dependent presentation (name balloons) matches across builds.
            else if(raceCapture)accumulator=2;
            while(accumulator>=1){auto tick=svcGetSystemTick();bool jumpAction=false;
                // Restore current human input every simulation tick before any
                // temporary autopilot override, including catch-up frame exits.
                auto raceTrick=trick;
                const auto motionTrick=gyroTrick.step();
                if(raceTrick==K::System::Trick::None)raceTrick=motionTrick;
                if(selectedMirror){
                    if(raceTrick==K::System::Trick::Left)raceTrick=K::System::Trick::Right;
                    else if(raceTrick==K::System::Trick::Right)raceTrick=K::System::Trick::Left;
                }
                controller->setInputsRawStick(buttons,(selectedMirror?-stick:stick)+7,7,raceTrick);
                if(fullRace){
                    // After the goal retail hands the player to its AI
                    // (AIEngine PostRaceDriving, USA 0x80728C28): the CPU
                    // route driver steers and the item AI stays idle.
                    const bool afterGoal=battleMode?results.active():raceGrid.finished();
                    if(battleMode){std::array<uint16_t,12> scores{};
                        for(unsigned i=0;i<raceGrid.count();++i)scores[i]=uint16_t(battleScore(i));
                        raceItems->setBattleScores(scores);
                    }
                    raceGrid.driveLocal=afterGoal||(autoplay&&autopilot->routeDrive());
                    std::array<mkw::ItemInput,12> inputs{};inputs[0]={bool(held&KEY_L),mkw::circlePadThrowBack(pad.dy),pad.dy > 40};
                    if(afterGoal)inputs[0]={};
                    else if(raceGrid.driveLocal){
                        inputs[0]=raceItems->autoItemInput(0);
                        // The CPU route threads between boxes: QA rolls the item roulette instead.
                        if(physicsSteps%600==300&&qaGives.empty())raceItems->startRoulette(0);
                    }
                    // CPU items: retail AIItemRace commands from the previous AI tick.
                    for(unsigned i=1;i<raceGrid.count();++i){
                        if(!raceGrid.ai().ready()){inputs[i]=raceItems->autoItemInput(i);continue;}
                        const auto& c=raceGrid.ai().itemCommand(i);inputs[i]={c.use,c.backwards,c.forwards};
                    }
                        if(kQa&&autoplay&&!afterGoal&&raceGrid.driveLocal)for(auto& g:qaGives){
                            if(g.player>=raceGrid.count())continue;
                            // Opponent POW must actually target player zero; do not alter ranks.
                            if(!g.granted&&g.item==13&&(raceItems->hud(0).pow||(g.player&&raceGrid.rank(g.player)<=raceGrid.rank(0))))continue;
                            if(!g.granted&&physicsSteps>=g.step&&raceItems->give(g.player,mkw::ItemKind(g.item))){g.granted=physicsSteps;LOGF("QA give player=%u item=%u step=%u local_rank=%u caster_rank=%u",g.player,g.item,physicsSteps,raceGrid.rank(0),raceGrid.rank(g.player));}
                            if(g.granted&&g.to>g.from&&physicsSteps<g.granted+(g.to-g.step))inputs[g.player].use=physicsSteps>=g.granted+(g.from-g.step);
                            if(g.granted&&physicsSteps<=g.granted+(g.to-g.step)+60&&physicsSteps%10==0){
                                const auto& h=raceItems->hud(g.player);unsigned bombs=0,blasts=0;
                                for(const auto& a:raceItems->actors())if(a.active&&a.kind==mkw::ItemKind::Bobomb){++bombs;blasts+=a.explosion;}
                                LOGF("QA held player=%u step=%u item=%d pow=%u local_pow=%u bombs=%u blasts=%u",g.player,physicsSteps,int(h.item),unsigned(h.pow),unsigned(raceItems->hud(0).pow),bombs,blasts);
                            }
                        }
                    raceItems->beforeStepNative(inputs.data(),raceGrid.count(),K::System::RaceManager::Instance()->isStageReached(K::System::RaceManager::Stage::Race),raceGrid.bulletNoEndPlayers());
                    for(unsigned i=0;i<raceGrid.count();++i){
                        const auto& h=raceItems->hud(i);const bool held=h.deployed&&h.heldCharges>0;
                        raceGrid.ai().setItemView(i,{uint8_t(h.item),uint8_t(held?h.heldCharges:0),h.roulette!=0,held,
                            held&&(h.heldKind==mkw::ItemKind::RedShell||h.heldKind==mkw::ItemKind::TripleRedShell),h.ink&&!h.inkDelay,uint8_t(h.heldKind)});
                    }
                    {
                        // Landed traps for the CPU obstacle avoider.
                        std::array<mkw::Vec3,mkw::CpuAi::MaxHazards> hazards{};std::array<float,mkw::CpuAi::MaxHazards> radii{};unsigned n=0;
                        for(const auto& a:raceItems->actors()){
                            if(n>=hazards.size())break;
                            if(!a.active||a.explosion||!a.settled)continue;
                            if(a.kind!=mkw::ItemKind::Banana&&a.kind!=mkw::ItemKind::FakeBox&&a.kind!=mkw::ItemKind::Bobomb)continue;
                            hazards[n]=a.position;radii[n]=a.radius;++n;
                        }
                        raceGrid.ai().setHazards(hazards.data(),radii.data(),n);
                    }
                    // Resolve item activation/expiry before choosing this tick's
                    // controller, including CPU Bullet Bills and the handoff back.
                    raceGrid.beforeStep(raceItems->bulletPlayers());
                }
                if(autoplay&&cpuLogEvery&&fullRace&&physicsSteps%cpuLogEvery==0){
                    auto* manager=K::System::RaceManager::Instance();
                    for(unsigned i=0;i<raceGrid.count();++i){auto* kart=K::Kart::KartObjectManager::Instance()->object(i);auto p=vec(kart->pos());
                        using St=K::Kart::eStatus;const auto& st=kart->status();const auto* move=kart->move();
                        char flags[12]{};unsigned n=0;
                        for(auto [bit,c]:{std::pair{St::Shocked,'S'},{St::Crushed,'C'},{St::Mega,'M'},{St::Star,'*'},{St::ThunderCloud,'T'},
                                          {St::CollidingOffroad,'O'},{St::InAction,'A'},{St::MushroomBoost,'K'},{St::TouchingGround,'G'}})if(st.onBit(bit)&&n<10)flags[n++]=c;
                        const auto& hud=raceItems->hud(i);
                        snprintf(line,sizeof line,"QA cpu step=%u racer=%u rank=%u lap=%u completion=%.3f point=%u target=%d base=%.1f speed=%.1f pos=%.0f,%.0f,%.0f soft=%.1f kcl=%.2f ss=%d item=%d held=%d st=%s ai=%d%d%d",
                                 physicsSteps,i,raceGrid.rank(i),raceGrid.lap(i),manager->player(i).raceCompletion(),raceGrid.ai().routePoint(i),raceGrid.ai().targetRank(i),
                                 raceGrid.ai().baseSpeed(i),move->speed(),p.x,p.y,p.z,move->softSpeedLimit(),move->kclSpeedFactor(),
                                 move->slipstreamBoost()?2:move->slipstreamCharging()?1:0,int(hud.item),hud.deployed?int(hud.heldKind):-1,flags,
                                 raceGrid.ai().stuckState(i),raceGrid.ai().avoidState(i),raceGrid.ai().itemState(i));report(line);}
                }
                // QA: a fall boundary (variant 0) activation, so the retail SetCamera hold applies.
                if(autoplay&&lakituRespawnStep&&physicsSteps==lakituRespawnStep){auto* qaCollide=K::Kart::KartObjectManager::Instance()->object(0)->collide();qaCollide->setOobCameraPending(uint16_t(lakituRequest));qaCollide->activateOob(false,nullptr,false,false);}
                scenes.calc();
                {
                    // The player's driver model after the kart step: its trick animation,
                    // sparkle and sound start together (trick_presentation.hpp).
                    if(auto* local=K::Kart::KartObjectManager::Instance()->object(0)){using St=K::Kart::eStatus;const auto& st=local->status();
                        jumpAction=trickModel.step(st.onBit(St::TouchingGround),st.onBit(St::InATrick),st.onBit(St::OverZipper),st.onBit(St::ZipperTrick),
                                                   unsigned(local->move()->jump()->variant()),local->isBike());}
                }
                if(intro.active()&&!raceWarmupFrames){intro.step();++introTick;if(!intro.active())endIntro();}
                if(fullRace){
                    raceGrid.afterStep();raceItems->afterStepNative(raceGrid.count());
                    if(coinRunnersMode){
                        const auto* race=K::System::RaceManager::Instance();
                        coinBattle.step(race->isStageReached(K::System::RaceManager::Stage::Race)&&
                            !results.active()&&battleTicks<balloonBattle.durationTicks(),physicsSteps,cameraDirector->view().eye,raceEffects);
                    }
                    if(battleMode&&!coinRunnersMode)balloonRuntime.step(raceEffects);
                    if(battleMode){
                        if(!coinRunnersMode)balloonBattle.updateRanks();
                        std::array<unsigned,12> ranks{};
                        for(unsigned i=0;i<raceGrid.count();++i)ranks[i]=battleRank(i);
                        raceGrid.setBattleRanks(ranks);
                    }
                    // Snapshot before fillRaceAudioFrame consumes the sound cues.
                    // Particle triggers must also work when audio is unavailable.
                    kartContacts={};
                    for(unsigned i=0;i<std::min<unsigned>(raceGrid.count(),kartContacts.size());++i)
                        kartContacts[i]=K::Kart::KartObjectManager::Instance()->object(i)->move()->peekSoundCues();
                    auto* kart=K::Kart::KartObjectManager::Instance()->object(0);auto* race=K::System::RaceManager::Instance();
                    // Item blasts shake the camera of a nearby racer (OnKill -> RMCU 0x8059e034).
                    for(const auto& e:raceItems->events()){
                        if(e.effect==mkw::ItemEffect::Explosion&&e.action<0)cameraShake.blast(e.direction,vec(kart->pos()));
                        // POW stomps shake the user's and the targets' cameras (0x807d9b10 -> 0x8058a778).
                        if(e.effect==mkw::ItemEffect::PowStomp&&e.player==0&&e.frames>=1&&e.frames<=3)cameraShake.trigger(mkw::CameraShakePow1+e.frames-1,1.f);
                        if(e.effect==mkw::ItemEffect::LightningFlash)lightningFlash.start();
                    }
                    lightningFlash.step();
                    // The POW hit itself (action 11 start, 0x80563ce4) shakes the struck racer's camera.
                    {const auto* action=kart->action();
                     if(shakeActionStarts!=~0u&&action->startCount()!=shakeActionStarts&&action->lastStarted()==K::Kart::Action::UNK_11)cameraShake.trigger(mkw::CameraShakePow1+2,1.f);
                     shakeActionStarts=action->startCount();}
                    cameraShake.step();
                    // Kinoko's camera runs last in the scene step; the presentation
                    // copy follows it. On the respawn frame retail re-seats the camera
                    // (RaceCamera::OnRespawn) before that frame's update.
                    const bool respawnStart=kart->status().onBit(K::Kart::eStatus::InRespawn)&&!flow.respawn;
                    if(respawnStart){
                        K::Render::KartCamera::Instance()->onRespawn();
                        chaseCamera.init(*kart);
                    }
                    chaseCamera.calc(*kart);
                    fillCameraInput(cameraInput);
                    if(battleMode)cameraInput.finished=battleTicks>=balloonBattle.durationTicks();
                    cameraDirector->step(cameraInput);
                    // Race-flow events for presentation subscribers (audio).
                    const unsigned stage=unsigned(race->stage());
                    if(battleMode&&stage>=2&&!results.active()&&battleTicks<balloonBattle.durationTicks())++battleTicks;
                    const bool roundFinished=battleMode?battleTicks>=balloonBattle.durationTicks():cameraInput.finished;
                    if(selectedRaceMode==0){
                        const auto& body=kart->collisionData();
                        const bool wall=(body.bWall||body.bWall3)&&!body.bInvisibleWallOnly;
                        gpTelemetry.wallStep(stage>=2,kart->status().onBit(K::Kart::eStatus::BeforeRespawn),
                            mkw::countedWallContact(wall,uint8_t(body.closestWallFlags),uint8_t(body.closestWallSettings)),
                            mkw::length(vec(kart->dynamics()->velocity())),kart->move()->gpWallPenaltyAllowed());
                        gpTelemetry.step(stage>=2,cameraInput.finished,raceGrid.rank()==1,respawnStart,
                            kart->move()->startEffectDuration()>0,raceItems->successfulHits(0),kart->move()->miniTurboSerial(),
                            kart->move()->offroadFrameSerial(),kart->move()->objectDamageSerial(),raceGrid.ai().gpRankBonus(0));
                        if(cameraInput.finished&&!flow.finished&&vsSet.race>=1&&vsSet.race<=4){
                            vsSet.performance[vsSet.race-1]=gpTelemetry.data();
                            vsSet.performanceCaptured[vsSet.race-1]=true;
                            vsSet.performanceCourses[vsSet.race-1]=unsigned(Courses[courseChoice].id);
                        }
                    }
                    if(flow.preview&&!intro.active())raceEvents.emit(mkw::RaceEvent::PreviewEnd,physicsSteps);
                    if(flow.stage==0&&stage>=1)raceEvents.emit(mkw::RaceEvent::Countdown,physicsSteps);
                    if(flow.stage<=1&&stage>=2)raceEvents.emit(mkw::RaceEvent::Start,physicsSteps);
                    if(cameraInput.beforeRespawn&&!flow.fall)raceEvents.emit(mkw::RaceEvent::Fall,physicsSteps,cameraInput.oobRequest);
                    if(respawnStart)raceEvents.emit(mkw::RaceEvent::Respawn,physicsSteps);
                    if(roundFinished&&!flow.finished){
                        if(battleMode){
                            std::array<K::u8,12> order{};for(unsigned i=0;i<raceGrid.count();++i)order[i]=K::u8(i);
                            std::stable_sort(order.begin(),order.begin()+raceGrid.count(),[](unsigned a,unsigned b){return battleRank(a)<battleRank(b);});
                            race->endBattle(order);
                        }
                        raceEvents.emit(mkw::RaceEvent::Finish,physicsSteps,int(battleMode?battleRank(0):raceGrid.rank()));results.finish(timeTrial);
                        if(!qaMode&&!replay&&selectedRaceMode<=2){
                            // Catalog choice -> original vehicle ID (type * 3 + weight).
                            constexpr unsigned vehicleTypes[]={0,1,2,6,7,8,3,4,5,9,10,11};
                            const unsigned vehicle=vehicleChoice<12?selectedVehicle().vehicle:36;
                            if(activeRecords.finish(selectedRaceMode,driverChoice,vehicle,courseChoice,raceGrid.rank())){
                                licenseSaves[activeLicense].records=activeRecords;
                                raceSavePending=true;
                            }
                        }
                        if(timeTrial&&courseChoice<bestTimes.size()){
                            const auto& t=race->player(0).raceTimer();const uint32_t ms=uint32_t(t.min)*60000u+uint32_t(t.sec)*1000u+uint32_t(t.mil);
                            if(ms&&(!bestTimes[courseChoice]||ms<bestTimes[courseChoice])){bestTimes[courseChoice]=ms;licenseSaves[activeLicense].times=bestTimes;menu.licenseBestTimes[activeLicense]=bestTimes;raceSavePending=true;}
                        }
                    }
                    {
                        using Flow=mkw::RaceResultsFlow;
                        const uint32_t events=results.step(resultsInput);resultsInput={};
                        if(events&Flow::EventFill)fillResults();
                        if(events&Flow::EventResultsShown)raceEvents.emit(mkw::RaceEvent::ResultsShown,physicsSteps,int(vsSet.race));
                        if(events&Flow::EventTotalsShown){
                            raceEvents.emit(mkw::RaceEvent::TotalsShown,physicsSteps,int(vsSet.race));
                            // Commit the completed cup when final standings become
                            // available, without waiting for entry into the ceremony.
                            if(results.finalRace()&&selectedRaceMode==0&&!qaMode&&!replay){
                                for(unsigned place=0;place<results.rowCount();++place){
                                    const auto& row=results.row(place);if(!row.human)continue;
                                    const auto grade=mkw::gpCupGrade(vsSet.performance,vsSet.performanceCaptured,vsSet.performanceCourses,selectedEngineClass,unsigned(row.previous)+row.gain);
                                    auto& save=licenseSaves[activeLicense];
                                    save.cupResult(selectedMirror?3u:selectedEngineClass,courseChoice/4,place+1,grade?int(*grade):-1);
                                    menu.licenseCups[activeLicense]=save.cups;
                                    raceSavePending=true;break;
                                }
                            }
                        }
                        if(events&Flow::EventMenuShown){menu.finish(results.finalRace());raceEvents.emit(mkw::RaceEvent::MenuShown,physicsSteps,int(results.buttonCount()));}
                        if(events&Flow::EventOptionChosen){
                            mkw::SoundSystem::instance().play(mkw::snd::id::SE_RC_PAUSE_EXIT_GAME,0);
                            raceEvents.emit(mkw::RaceEvent::OptionChosen,physicsSteps,results.chosen());
                            LOGF("race end option %s (race %u/%u)",results.chosen()==0?(results.finalRace()?"Next":"Next Race"):"Quit Game",vsSet.race,raceCount());
                        }
                        if(events&Flow::EventLeave){
                            // The race has faded to black (RaceResultsFlow::Leaving).
                            // Keep full-file CRC/read/backup writes off the finish
                            // and score-count frames; commit before leaving the race.
                            raceSound.carryResultMusic();
                            flushRaceSave();
                            if(results.finalRace()&&results.chosen()==0){
                                std::array<uint8_t,12> vehicles{};
                                const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
                                for(unsigned i=0;i<vehicles.size();++i)vehicles[i]=uint8_t(scenario.players[i].vehicle);
                                awardResults.capture(results,selectedRaceMode,courseChoice/4,selectedEngineClass,selectedMirror,vehicles);
                                if(driverChoice>=24)awardResults.raceMii=selectedRaceMii;
                                awardResults.coinRunners=coinRunnersMode;
                                awardResults.performance=vsSet.performance;awardResults.performanceCaptured=vsSet.performanceCaptured;
                                awardResults.calculateGrade(vsSet.performanceCourses);
                                awardPending=mkw::hasAwardScene(awardResults);
                            }
                            sectionFadeIn=true;
                            vsSet.next=results.chosen()==0&&!results.finalRace();
                            if(menu.page==mkw::UiPage::Results){menu.cursor=unsigned(std::max(0,results.chosen()));nextRaceQueued=menu.input({false,false,false,false,true})==mkw::UiAction::StartRace;}
                        }
                        flowUi.step(results);
                    }
                    flow.stage=stage;flow.preview=intro.active();flow.fall=cameraInput.beforeRespawn;flow.respawn=cameraInput.inRespawn;flow.finished=roundFinished;
                    const auto view=cameraDirector->view();
                    mkw::BattleRoundResult battleOutcome;
                    if(battleMode){
                        const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
                        std::array<uint16_t,12> scores{};std::array<mkw::RaceTeam,12> teams{};
                        for(unsigned i=0;i<raceGrid.count();++i){scores[i]=uint16_t(battleScore(i));teams[i]=mkw::RaceTeam(scenario.players[i].team);}
                        battleOutcome=mkw::battleRoundResult(scores,teams,raceGrid.count());
                        raceHud.setBattleView({true,unsigned(teams[0]),scores[0],battleOutcome.points[0],battleOutcome.points[1]},physicsSteps);
                    }
                    raceHud.stepWorld(*raceItems,mkw::normalized(view.eye-view.target,{0,0,1}),stage>=1);
                    {
                        // Read-only presentation: reuse existing item events, native timers,
                        // and the HUD's own cloud / chase animation. No extra world queries.
                        using St=K::Kart::eStatus;using BT=K::Kart::KartBoost::Type;
                        const auto& st=kart->status();const auto& h=raceItems->hud(0);
                        mkw::led::Input led;led.level=menu.selection.ledMode;led.brightness=menu.selection.ledBrightness;
                        led.active=stage>=1&&!roundFinished&&!intro.active();
                        led.startSignal=stage==1&&race->timer()>=60?std::min<unsigned>((race->timer()-60)/60+1,3u):stage>=2&&race->timer()<300?4u:0u;
                        led.burnout=st.onBit(St::Burnout);
                        led.star=st.onBit(St::Star);led.mega=st.onBit(St::Mega);led.bullet=bool(h.bullet);
                        led.golden=bool(h.golden);led.mushroom=st.onBit(St::MushroomBoost);
                        // NotificationLed waits for the matching committed boost sound.
                        led.boostVisible=true;
                        led.boost=unsigned(std::max(0,int(kart->move()->boostRemaining(BT::MushroomAndBoostPanel))));
                        led.ramp=st.onBit(St::RampBoost)?kart->move()->rampBoostRemaining():0;
                        led.miniTurboSerial=kart->move()->miniTurboSerial();led.drift=unsigned(kart->move()->driftState());
                        led.cloud=bool(h.cloud);led.cloudLit=raceHud.notificationCloudLit();
                        led.warning=raceHud.notificationWarning(led.warningLit);
                        led.warningLit=mkw::SoundSystem::instance().warningLedPulse();
                        led.warning=led.warning||led.warningLit;
                        led.rainbow=!battleMode&&Courses[courseChoice].id==K::Course::Rainbow_Road&&raceGrid.lap()>=3;
                        const auto* action=kart->action();
                        notificationLed.step(led,*raceItems,action->startCount(),int(action->lastStarted()),action->lastAttacker());
                    }
                    // QA heartbeat: a freeze shows as the last line written.
                    if(kQa&&physicsSteps%300==0){const auto& hi=raceItems->hud(0);LOGF("hb step=%u stage=%u lap=%u rank=%u finished=%d item=%d",physicsSteps,stage,raceGrid.lap(),raceGrid.rank(),int(raceGrid.finished()),int(hi.item));}
                    mkw::LakituInput jugem;
                    jugem.kart=kart;jugem.stage=stage;jugem.timer=race->timer();jugem.lap=raceGrid.lap();jugem.lapCount=3;
                    jugem.introHeld=intro.active();jugem.finished=raceGrid.finished();jugem.cameraForward=view.target-view.eye;
                    lakitu.step(jugem);
                    {
                        // Race audio state (RaceRSARPlayer::Calc order: after the race step).
                        const auto& me=race->player(0);using St=K::Kart::eStatus;const auto& st=kart->status();
                        mkw::RaceSound::Frame audio;audio.stage=stage;audio.timer=race->timer();audio.currentLap=me.currentLap();
                        audio.battlePointCues=raceHud.takeBattleCues();
                        if(battleMode)audio.battleResultKind=uint8_t(battleOutcome.kind(mkw::RaceTeam(K::System::RaceConfig::Instance()->raceScenario().players[0].team),battleScore(0)));
                        audio.resultPointsChanged=results.pointsChanged();
                        audio.resultSlideSound=results.slideSound();
                        audio.finished=roundFinished;audio.rank=battleMode?battleRank(0):me.finishOrder()?me.finishOrder():raceGrid.rank();
                        audio.star=st.onBit(St::Star);audio.mega=st.onBit(St::Mega);audio.shocked=st.onBit(St::Shocked);
                        audio.introShot=intro.active()?intro.shot():-1;audio.hudRank=fullRace&&!battleMode&&!intro.active()?raceGrid.rank():0;
                        const uint16_t coinCues=coinBattle.takeCues(),coinArrivalCues=coinBattle.takeArrivalCues();
                        if(raceSound.active()){
                            const mkw::Vec3 eye=intro.active()?intro.eye():view.eye,look=intro.active()?intro.target():view.target;
                            actors.traceObjects=menuProfile->enabled();
                            mkw::fillRaceAudioFrame(actors,{eye.x,eye.y,eye.z},{look.x,look.y,look.z});
                            actors.chaseWarning=raceHud.chaseWarningAudio(actors.chasePan,actors.chaseRate);
                            if(driverChoice>=24)actors.kart[0].miiVoice=selectedMiiVoice;
                            mkw::fillRaceAudioItems(actors,*raceItems);mkw::fillRaceAudioObjects(actors);balloonRuntime.fillAudio(actors);
                            if(jumpAction)actors.kart[0].cues|=mkw::cue::JumpAction;
                            for(unsigned i=0;i<raceGrid.count();++i)if(coinCues&(1u<<i))actors.kart[i].cues|=mkw::cue::CoinGet;
                            for(unsigned i=0;i<raceGrid.count();++i)actors.kart[i].coinArrival|=bool(coinArrivalCues&(1u<<i));
                            actors.lakituSound=lakitu.soundLoop();const mkw::Vec3 jp=lakitu.position();actors.lakituPosition={jp.x,jp.y,jp.z};
                        }else{
                            for(unsigned i=0;i<std::min<unsigned>(raceGrid.count(),kartContacts.size());++i)
                                K::Kart::KartObjectManager::Instance()->object(i)->move()->takeSoundCues();
                        }
                        raceSound.step(audio,&actors);
                        // The audio thread only writes a bounded SPSC queue. Disk logging stays here.
                        mkw::RaceAudioLayer::ObjectTrace ot;
                        while(raceSound.layer().takeObjectTrace(ot)) {
                            if(!menuProfile->enabled()) continue;
                            char objectLine[512];
                            snprintf(objectLine,sizeof objectLine,"object sound tick=%lu course=%u object=%x index=%u mode=%u sound=%lu initial=%u started=%u muted=%u param=%lx pos=%.1f,%.1f,%.1f listener=%.1f,%.1f,%.1f distance=%.1f ambient=%.6f pan=%.3f dropped=%lu",
                                (unsigned long)ot.tick,ot.course,ot.object,ot.index,ot.mode,(unsigned long)ot.sound,
                                ot.initial,ot.started,ot.muted,(unsigned long)ot.userParam,
                                ot.position.x,ot.position.y,ot.position.z,ot.listener.x,ot.listener.y,ot.listener.z,
                                ot.distance,ot.volume,ot.pan,(unsigned long)raceSound.layer().objectTraceDropped.load());
                            menuProfile->event(objectLine);
                        }

                        // QA: race audio counters every 600 ticks (in memory; written with the profile).
                        if(menuProfile->enabled()&&raceSound.active()&&physicsSteps%600==0){
                            const auto st=mkw::SoundSystem::instance().stats();
                            snprintf(line,sizeof line,"race sound step=%u voices=%lu sounds=%lu dropped=%lu underruns=%lu missing=%lu failed=%lu audio_ms=%.3f peak_ms=%.3f cache=%lu/%lu loads=%lu evictions=%lu waves=%lu linear_free=%lu",
                                     physicsSteps,(unsigned long)st.voices,(unsigned long)st.sounds,(unsigned long)st.dropped,(unsigned long)st.underruns,(unsigned long)st.missing,(unsigned long)st.failed,st.audioMs,st.audioPeakMs,
                                     (unsigned long)st.cacheUsed,(unsigned long)st.cacheBytes,(unsigned long)st.cacheLoads,(unsigned long)st.cacheEvictions,(unsigned long)mkw::SoundSystem::instance().residentWaveBytes(),(unsigned long)linearSpaceFree());
                            menuProfile->event(line);
                            snprintf(line,sizeof line,"race sound actors frames=%lu playing=%lu last_missing=%ld/%ld/%ld",(unsigned long)raceSound.layer().framesUsed.load(),(unsigned long)raceSound.layer().actorSounds.load(),long(st.lastMissingSound),long(st.lastMissingFile),long(st.lastMissingWave));menuProfile->event(line);
                            {const auto& n=raceSound.layer().started;auto g=[&](int f){return (unsigned long)n[f].load();};using L=mkw::RaceAudioLayer;
                             snprintf(line,sizeof line,"race sound karts voices=%lu result=%lu items=%lu drift_frames=%lu road=%lu slip=%lu spark=%lu brake=%lu landing=%lu wall=%lu car=%lu hop=%lu dash=%lu dashj=%lu jumppad=%lu horn=%lu cannon=%lu effect=%lu trick=%lu other=%lu",
                                      (unsigned long)raceSound.layer().voiceStarts.load(),(unsigned long)raceSound.layer().resultVoice.load(),(unsigned long)raceSound.layer().itemSounds.load(),(unsigned long)raceSound.layer().driftFrames.load(),g(L::Road),g(L::Slip),g(L::Spark),g(L::Brake),g(L::Landing),g(L::Wall),g(L::Car),g(L::Hop),g(L::Dash),g(L::DashJ),g(L::JumpPad),g(L::Horn),g(L::Cannon),g(L::Effect),g(L::Trick),g(L::Other));menuProfile->event(line);
                             const auto& m=raceSound.layer().itemStarted;auto h=[&](int k){return (unsigned long)m[k].load();};
                             snprintf(line,sizeof line,"race sound items shot=%lu loop=%lu decide=%lu global=%lu screen=%lu cloud=%lu ambience=%lu object=%lu",h(L::QaShot),h(L::QaLoop),h(L::QaDecide),h(L::QaGlobal),h(L::QaScreen),h(L::QaCloud),h(L::QaAmbience),h(L::QaObject));menuProfile->event(line);}
                        }
                    }

                }
                double us=(svcGetSystemTick()-tick)*1000000.0/SYSCLOCK_ARM11;stepTotal+=us;stepMax=std::max(stepMax,us);physicsSteps++;accumulator-=1;
                if(autoplay&&cpuLogEvery&&fullRace){
                    // QA: full simulation tick cost (items, AI, physics) per log window.
                    static double windowTotal=0,windowMax=0;static unsigned windowSteps=0;
                    // Skip the tick that wrote the CPU log lines to the SD card.
                    if((physicsSteps-1)%cpuLogEvery!=0){windowTotal+=us;windowMax=std::max(windowMax,us);++windowSteps;}
                    if(physicsSteps%cpuLogEvery==0&&windowSteps){LOGF("QA perf step=%u mean_us=%.0f max_us=%.0f",physicsSteps,windowTotal/windowSteps,windowMax);windowTotal=windowMax=0;windowSteps=0;}
                }
                auto* kart=K::Kart::KartObjectManager::Instance()->object(0);const auto& pose=kart->pose();
                unsigned tires=kart->param()->tireCount();bool bike=kart->param()->isBike();
                // Kinoko creates front pair first, then rear pair; bikes are front/rear.
                unsigned rearLeft=bike?1:tires-2,rearRight=tires-1;
                auto left=vec(kart->tirePhysics(rearLeft)->pos()),right=vec(kart->tirePhysics(rearRight)->pos());
                // Source emitters attach to wheel centres; their authored offsets
                // position the sparks. Subtracting the tyre radius clips flares into the road.
                // Presentation observes the original boost types without altering physics.
                using BT=K::Kart::KartBoost::Type;
                float boost=kart->move()->boostActive(BT::MushroomAndBoostPanel)?1.f:kart->move()->boostActive(BT::TrickAndZipper)?.85f:(kart->move()->boostActive(BT::AllMt)||kart->status().onBit(K::Kart::eStatus::RampBoost))?.5f:0.f;
                if(effectsTest)boost=(frames>=30&&frames<60)||(frames>=280&&frames<340)?1.f:0.f;
                boostFov+=(boost*10.f-boostFov)*(boost*10.f>boostFov?.25f:.1f);maxFov=std::max(maxFov,boostFov);
                bool inTrick=kart->status().onBit(K::Kart::eStatus::InATrick,K::Kart::eStatus::ZipperTrick);
                const bool trickStarted=inTrick&&!wasTricking;
                if(trickStarted)++trickCount;
                // Driver presentation reads simulation state at exactly 60 Hz.
                mkw::DriverSignals signals;
                vehicleAnimation.step(*kart);
                if(garageTest){
                    animationPass&=vehicleAnimation.finite();
                    for(unsigned i=0;i<vehicleAnimation.wheelCount();++i)fixtureRolling|=std::fabs(vehicleAnimation.roll(i))>0.02f;
                }
                signals.bike=bike;signals.handleRelativeBike=kart->param()->stats().body==K::Kart::KartParam::Stats::Body::Handle_Relative_Bike;signals.wheelie=kart->status().onBit(K::Kart::eStatus::Wheelie);
                signals.airborne=kart->status().offBit(K::Kart::eStatus::TouchingGround);
                signals.hopping=kart->status().onBit(K::Kart::eStatus::Hop);
                signals.steering=kart->inputs()->currentState().stick.x;
                signals.burnout=kart->status().onBit(K::Kart::eStatus::Burnout);
                signals.burnoutPitch=kart->move()->burnout().pitch();
                signals.inCannon=kart->status().onBit(K::Kart::eStatus::InCannon);
                signals.vehicleId=unsigned(K::System::RaceConfig::Instance()->raceScenario().players[0].vehicle);
                signals.frozenInIce=K::System::RaceConfig::Instance()->raceScenario().course==K::Course::N64_Sherbet_Land&&kart->status().onBit(K::Kart::eStatus::InRespawn,K::Kart::eStatus::AfterRespawn);
                signals.speed=kart->move()->speed();
                signals.drift=kart->status().onBit(K::Kart::eStatus::DriftManual,K::Kart::eStatus::DriftAuto);
                signals.driftDirection=kart->move()->hopStickX();signals.boost=boost>0;signals.trick=inTrick;
                signals.hit=kart->status().onBit(K::Kart::eStatus::InAction,K::Kart::eStatus::Burnout);
                signals.finished=fullRace?raceGrid.finished():K::System::RaceManager::Instance()->stage()==K::System::RaceManager::Stage::FinishGlobal;
                if(fullRace)signals.finishType=mkw::driverFinishType(raceGrid.rank(),K::System::RaceConfig::Instance()->raceScenario().playerCount);
                signals.accelerating=kart->status().onBit(K::Kart::eStatus::Accelerate);signals.braking=kart->status().onBit(K::Kart::eStatus::Brake);
                signals.countdown=unsigned(K::System::RaceManager::Instance()->stage())<=1;
                if(fullRace)mkw::driverItemSignals(signals,*raceItems,0);
                signals.blink=physicsSteps%180==0;
                driverPose=driverController.step(signals);
                if(fullRace)opponents.step(physicsSteps,*raceItems);
                if(driver){
                    mkw::DriverLookParameters parameters;const auto& choice=selectedVehicle();
                    const bool available=mkw::driverLookParameters(choice.character,choice.vehicle,parameters);
                    C3D_Mtx seated=vehicleAnimation.body();Mtx_Translate(&seated,0,choice.seatY,choice.seatZ,true);
                    driverAnimation.stepLook(driverAnimation.find(mkw::driverVehicleClipName(driverPose.clipName,bike)),driverPose.frame,physicsSteps,
                        available&&signals.hasLookTarget&&mkw::driverLookAllowed(driverPose.clipName,
                            kart->status().onBit(K::Kart::eStatus::InATrick),kart->status().onBit(K::Kart::eStatus::OverZipper)),
                        signals.lookTarget,seated,true,parameters,driverPose,signals.handleRelativeBike,kart->physics()->pose()[1,0],kart->physics()->pose()[1,2]);
                }
                // Clip clocks observe every source tick, including the iterations
                // where no graphics are submitted.
                auto clockPart=[&](mkw::DriverAnimation* bank,PartClock& clock,bool handle){
                    int clip=partClip(bank,*kart,handle);
                    if(clip>=0&&clock.clip!=clip){clock.clip=clip;clock.start=physicsSteps;}
                };
                clockPart(bodyAnimation.get(),bodyClock,false);
                for(unsigned i=0;i<accessoryAnimations.size();++i)clockPart(accessoryAnimations[i].get(),accessoryClocks[i],i==0);
                wasTricking=inTrick;
                const unsigned driftState=effectsTest?(frames<60?0:frames<120?2:3):unsigned(kart->move()->driftState());
                driftEffects.step(driftState,left,right,{pose[0,2],pose[1,2],pose[2,2]},bike?1:2,{pose[0,1],pose[1,1],pose[2,1]},kart->scale().y);
                if(raceEffects.ready){
                    // Player 0's KartEffect emitters, attached in kart space.
                    const mkw::Vec3 kx{pose[0,0],pose[1,0],pose[2,0]},ky{pose[0,1],pose[1,1],pose[2,1]},kz{pose[0,2],pose[1,2],pose[2,2]},kp{pose[0,3],pose[1,3],pose[2,3]};
                    auto kartFrame=[&](mkw::Vec3 origin){mkw::EffectFrame f;f.origin=origin;f.x=kx;f.y=ky;f.z=kz;f.scale=kart->scale().y;return f;};
                    // Exhaust flames (rk_miniTurbo) while any boost runs, with the
                    // rk_miniTurbo1T burst on onset; pipes from kart/bikePartsDispParam.
                    const bool boosting=boost>0&&!intro.active();
                    // Each Mushroom press, including every Golden Mushroom use
                    // during an ongoing boost, replays the onset burst.
                    const uint16_t mushroomUses=fullRace?raceItems->mushroomUses(0):0;
                    const bool burst=boosting&&(!wasBoosting||mushroomUses!=lastMushroomUses);lastMushroomUses=mushroomUses;
                    const unsigned vehicleId=std::min(35u,unsigned(K::System::RaceConfig::Instance()->raceScenario().players[0].vehicle));
                    const auto& exhaust=mkw::VehicleExhausts[vehicleId];
                    for(unsigned i=0;i<4;++i){
                        const bool pipe=i<exhaust.count;
                        mkw::EffectFrame f=kartFrame(kp);
                        if(pipe){
                            const float* e=exhaust.pipes[i];
                            const float pitch=e[3]*float(M_PI)/180.f,yaw=e[4]*float(M_PI)/180.f;
                            const float sp=std::sin(pitch),cp=std::cos(pitch),sy=std::sin(yaw),cy=std::cos(yaw);
                            // Pipe axes: Ry(yaw)*Rx(pitch) in kart space.
                            auto axis=[&](float x,float y,float z){const float y1=y*cp-z*sp,z1=y*sp+z*cp;return kx*(x*cy+z1*sy)+ky*y1+kz*(-x*sy+z1*cy);};
                            f.origin=kartFrame(kp).toWorld({e[0],e[1],e[2]});f.x=axis(1,0,0);f.y=axis(0,1,0);f.z=axis(0,0,1);f.scale=kart->scale().y*exhaust.flameScale;
                            // The onset burst rides its pipe like the continuous flame.
                            raceEffects.move(8+i,f);
                            if(burst)raceEffects.restart(8+i,fx.flameBurst,f);
                        }
                        raceEffects.attach(i,fx.flame,f,pipe&&boosting);
                    }
                    wasBoosting=boosting;
                    // Bike wheelie wind trail from the rear wheel.
                    raceEffects.attach(4,fx.wheelie,kartFrame(left),(bike&&signals.wheelie)||(effectsTest&&frames>=120&&frames<180));
                    // Smoke and dust (KartEffect's rk_start*, rk_driftSmoke, rk_dirtSmokeA and
                    // rk_jumpSmoke* emitters), player 0 only. Startup follows the
                    // retail callback/input path; drift/off-road/landing thresholds
                    // below still need their separate original comparisons.
                    {
                        const int startSpec[3]={raceEffects.find("rk_start0"),raceEffects.find("rk_start1"),raceEffects.find("rk_start2")};
                        const int startMiss=raceEffects.find("rk_startMiss"),driftSmoke=raceEffects.find("rk_driftSmoke");
                        const auto stageNow=K::System::RaceManager::Instance()->stage();
                        const bool grounded=kart->status().onBit(K::Kart::eStatus::TouchingGround);
                        // Original simultaneous rear-wheel layers and input/duration
                        // callback. A residual charge after releasing A is not input.
                        const int startWheels=playerSmoke.start.step(K::System::RaceManager::Instance()->timer(),intro.active(),
                            stageNow==K::System::RaceManager::Stage::Countdown,kart->inputs()->currentState().accelerate(),
                            kart->collisionData(rearLeft).bFloor,kart->collisionData(rearRight).bFloor,bike,
                            kart->move()->startEffectSerial(),kart->move()->startEffectDuration());
                        static_assert(mkw::KartFxSlots::FirstFreeSlot+8<=mkw::RaceEffects::SlotCount);
                        if(startWheels!=mkw::KartStartEffects::Hold)for(unsigned wheel=0;wheel<2;++wheel)for(unsigned layer=0;layer<3;++layer)
                            raceEffects.attach(mkw::KartFxSlots::FirstFreeSlot+2+wheel*3+layer,startSpec[layer],kartFrame(vec(kart->wheelEdgePos(wheel?rearRight:rearLeft))),(startWheels&(1<<wheel))!=0);
                        // Drift tyre smoke once the drift is held on the ground.
                        const bool drifting=stageNow>=K::System::RaceManager::Stage::Race&&signals.drift&&grounded&&!bike;
                        raceEffects.attach(14,driftSmoke,kartFrame(left),drifting);
                        raceEffects.attach(15,driftSmoke,kartFrame(right),drifting);
                        // Original per-wheel floor variants select the surface category.
                        // Sand shares a previous-rear-midpoint emitter; grass/mud use
                        // independent current wheel-edge emitters, not tire centers.
                        mkw::KartSurfaceEffects::Wheel surfaceWheels[2];
                        for(unsigned w=0;w<2;++w){
                            const unsigned tire=w?rearRight:rearLeft;const auto& col=kart->collisionData(tire);
                            surfaceWheels[w]={vec(kart->wheelEdgePos(tire)),uint32_t(col.closestFloorFlags),col.closestFloorSettings,col.bFloor};
                        }
                        const unsigned surfaceCourse=unsigned(Courses[courseChoice].id);
                        const bool sandArea=surfaceCourse==14&&K::System::CourseMap::Instance()->getCurrentAreaID(-1,kart->pos(),K::System::MapdataAreaBase::Type(1))!=-1;
                        surfaceEffects[0].step(raceEffects,kartFrame(kp),surfaceWheels,surfaceCourse,bike,
                            !intro.active()&&!kart->status().onBit(K::Kart::eStatus::BeforeRespawn,K::Kart::eStatus::Bullet)&&
                            stageNow>=K::System::RaceManager::Stage::Race,driftState>=2,
                            mkw::length(vec(kart->velocity())),kart->move()->softSpeedLimit(),
                            kart->status().onBit(K::Kart::eStatus::ChargingSSMT),startWheels>0,sandArea);
                        const bool burnout=kart->status().onBit(K::Kart::eStatus::Burnout);
                        if(burnout&&!playerSmoke.burnout&&startMiss>=0)raceEffects.burst(startMiss,kartFrame(kp));
                        playerSmoke.burnout=burnout;
                        // Landing smoke uses the original floor/velocity
                        // signals in the shared racer presentation below.
                    }
                    // Retail bike +0x818: rear-wheel centre with floor-normal up
                    // and projected chassis forward, not the banked/leaning body.
                    const auto& bikeContact=kart->collisionData(rearRight);
                    auto bikeChassis=kartFrame(vec(kart->wheelPos(rearRight)));
                    bikeChassis.y=mkw::normalized(bikeContact.bFloor?vec(bikeContact.floorNrm):mkw::Vec3{0,1,0},{0,1,0});
                    bikeChassis.z=mkw::normalized(mkw::tangent(kz,bikeChassis.y),{0,0,1});
                    bikeChassis.x=mkw::normalized(mkw::cross(bikeChassis.y,bikeChassis.z),{1,0,0});
                    bikeDriftEffects.step(raceEffects,bikeChassis,driftState,kart->move()->hopStickX(),
                        kart->param()->stats().driftType==K::Kart::KartParam::Stats::DriftType::Inside_Drift_Bike,
                        kart->collisionData(rearRight).bFloor,bike&&!intro.active()&&!signals.hit);
                    auto scrapeBody=bikeChassis;scrapeBody.origin={kp.x,bikeChassis.origin.y,kp.z};
                    bikeScrapeEffects.step(raceEffects,scrapeBody,bikeChassis,driftState,kart->move()->hopStickX(),
                        kart->status().onBit(K::Kart::eStatus::DriftAuto),bikeContact.bFloor,
                        bike&&!intro.active()&&!signals.hit&&kart->param()->stats().driftType==K::Kart::KartParam::Stats::DriftType::Inside_Drift_Bike);
                    // Small light sparks: rk_driftSpark*_Chip00 parents at each rear wheel.
                    const unsigned drift=driftState;
                    if(bike){
                        raceEffects.attach(5,fx.chip[4],kartFrame(left),drift>=2);
                        raceEffects.attach(6,fx.chip[5],kartFrame(left),drift>=2);
                    }else{
                        raceEffects.attach(5,fx.chip[drift>=3?2:0],kartFrame(left),drift>=2);
                        raceEffects.attach(6,fx.chip[drift>=3?3:1],kartFrame(right),drift>=2);
                    }
                    // Trick sparkle when the driver model starts its trick animation
                    // (fn_1_18BE08 0x80697D34, with SE_VCL_JUMP_ACTION; trick_presentation.hpp).
                    // KartEffect kills and restarts rk_jumpSeikou on each trick, then sets
                    // only its translation to the kart every frame (world-aligned).
                    {
                        mkw::EffectFrame trickFrame;trickFrame.origin=vec(kart->pos());trickFrame.scale=kart->scale().y; // KartObjectProxy::getPos (0x8068fa0c)
                        raceEffects.move(12,trickFrame);
                        if((jumpAction&&!signals.hit)||(effectsTest&&frames==180))raceEffects.restart(12,fx.trick,trickFrame);
                    }
                    if(effectsTest&&frames==240){mkw::EffectFrame f;f.origin=kp+kz*300.f+ky*120.f;raceEffects.burst(fx.itemBox,f);}
                    // On-screen speed lines: ring emitter facing the camera, at the kart.
                    {
                        auto* cam=K::Render::KartCamera::Instance();
                        const mkw::Vec3 eye=fullRace?cameraDirector->view().eye:vec(cam->position()),target=fullRace?cameraDirector->view().target:vec(cam->target());
                        mkw::EffectFrame f;f.origin=target;f.z=mkw::normalized(target-eye,kz);
                        f.x=mkw::normalized(mkw::cross({0,1,0},f.z),kx);f.y=mkw::cross(f.z,f.x);
                        // KartEffect +0x5c copies InCannon (USA80690688/8c);
                        // the native camera streak path 806a11d8 also runs in flight.
                        raceEffects.attach(7,fx.speedLines,f,boosting||signals.inCannon);
                    }
                    // Shell tails (rk_kouraTailG/R/B): a world-space ribbon from each
                    // thrown shell near the camera, one point per frame.
                    if(fullRace){
                        const auto& actors=raceItems->actors();
                        for(unsigned a=0;a<16;++a){
                            int tail=-1,owner=-1;mkw::EffectFrame f;
                            for(unsigned n=a;n<actors.size();n+=16){
                                const auto& actor=actors[n];
                                const int kind=actor.kind==mkw::ItemKind::GreenShell?0:actor.kind==mkw::ItemKind::RedShell?1:actor.kind==mkw::ItemKind::BlueShell?2:-1;
                                if(!actor.active||actor.explosion||kind<0||mkw::length(actor.position-kp)>9000.f)continue;
                                tail=fx.shellTail[kind];owner=int(n);f.origin=actor.visualPosition();break;
                            }
                            // A slot that switches shells starts a new ribbon instead of joining them.
                            if(owner!=tailActor[a]){raceEffects.attach(16+a,-1,f,false);tailActor[a]=owner;}
                            raceEffects.attach(16+a,tail,f,tail>=0);
                        }
                    }
                    // Item object effects: shell breaks at the shell's scale (RMCU 0x80689070 /
                    // 0x806890e8), the Bob-omb blast at scale 1.2 (0x80689180) and the Blue's
                    // wing burst (0x806892c0), each where the object died.
                    if(fullRace)for(const auto& e:raceItems->events()){
                        if(e.effect!=mkw::ItemEffect::Break&&!(e.effect==mkw::ItemEffect::Explosion&&e.action<0))continue;
                        if(mkw::length(e.direction-kp)>9000.f)continue;
                        const auto kind=mkw::ItemKind(e.frames);int spec=-1;float scale=e.scale;
                        if(e.effect==mkw::ItemEffect::Explosion){spec=kind==mkw::ItemKind::BlueShell?fx.blueBlast:fx.bombBlast;scale=kind==mkw::ItemKind::BlueShell?1.f:1.2f;}
                        else if(kind==mkw::ItemKind::GreenShell||kind==mkw::ItemKind::TripleGreenShell)spec=fx.shellBreak[0];
                        else if(kind==mkw::ItemKind::RedShell||kind==mkw::ItemKind::TripleRedShell)spec=fx.shellBreak[1];
                        else if((kind==mkw::ItemKind::Banana||kind==mkw::ItemKind::TripleBanana)&&scale>=.1f)spec=fx.bananaBreak;
                        if(spec>=0){mkw::EffectFrame f;f.origin=e.direction;f.scale=scale;raceEffects.burst(spec,f);}
                        // Blue OnKill (807a376c) creates its wing burst, then
                        // a blue blast object (type 23). That object's OnKill
                        // selects rk_bombBlue at the original 1.2 scale
                        // (8079d078..b4). The model core alone lacks its smoke.
                        if(e.effect==mkw::ItemEffect::Explosion&&kind==mkw::ItemKind::BlueShell){
                            mkw::EffectFrame f;f.origin=e.direction;f.scale=1.2f;raceEffects.burstBlueSmoke(fx.blueBlastSmoke,f);
                        }
                    }
                    // KartEffect power-ups for every racer: Star, Bullet, Mega, Lightning and Thunder
                    // Cloud bolts, the shocked crackle and the POW (KartItemEffects). Presentation
                    // reads of this step's kart state and item events only.
                    if(fullRace){
                        std::array<mkw::KartFxInput,mkw::KartFxSlots::MaxRacers> fxIn{};
                        const unsigned racers=std::min<unsigned>(raceGrid.count(),fxIn.size());
                        for(const auto& e:raceItems->events())if(e.player<racers){
                            if(e.effect==mkw::ItemEffect::Bolt)(e.frames?fxIn[e.player].cloudBolt:fxIn[e.player].lightningBolt)=true;
                            if(e.effect==mkw::ItemEffect::PowStomp&&e.action==1&&e.frames<=2)fxIn[e.player].powWave=true;
                        }
                        for(const auto& e:raceItems->sounds())if(e.player<racers){
                            if(e.cue==mkw::ItemSoundCue::FakeBox)(e.flag?fxIn[e.player].fakeBoxVanish:fxIn[e.player].fakeBoxHit)=true;
                            if(e.cue==mkw::ItemSoundCue::BoxBreak)fxIn[e.player].itemBoxHit=true;
                        }
                        using St=K::Kart::eStatus;
                        // The LOD runs first (0x806789bc), on the clip of the step before.
                        // Player 0 is the only local racer and the camera's kart.
                        std::array<bool,mkw::KartEffectLod::MaxKarts> local{};local[0]=true;
                        if(!kartFxLod.ready){kartFxLod.lod.reset(racers,local,mkw::KartEffectLod::VsMode,mkw::renderQuality().kartEffectScreens);kartFxLod.ready=true;}
                        kartFxLod.lod.update(0,0,local,kartFxLod.clip);
                        uint32_t calcGroups=~0u,drawGroups=~0u;
                        for(unsigned i=0;i<racers;++i){
                            const auto* racer=K::Kart::KartObjectManager::Instance()->object(i);
                            const auto& m=racer->pose();const auto& status=racer->status();const auto* action=racer->action();
                            auto& in=fxIn[i];
                            in.position=vec(racer->pos());in.right={m[0,0],m[1,0],m[2,0]};in.up={m[0,1],m[1,1],m[2,1]};in.forward={m[0,2],m[1,2],m[2,2]};
                            in.scale=racer->scale().y;in.initialY=racer->bsp().initialYPos;in.bulletOffset=mkw::BulletModelHeight-in.initialY;
                            in.star=status.onBit(St::Star);in.mega=status.onBit(St::Mega);in.bullet=status.onBit(St::Bullet);
                            in.shocked=status.onBit(St::Shocked);in.beforeRespawn=status.onBit(St::BeforeRespawn);
                            in.hasRainbowEntry=raceEffects.find("rk_entry")>=0;
                            in.action=int(action->currentAction());in.actionFrame=action->frame();in.powDodged=action->powDodged();in.powPending=action->powPending();
                            in.spinSerial=action->spinStartCount();in.spinRotations=action->spinRotations();in.spinLeft=action->spinLeft();
                            in.crashSerial=action->crashStartCount();
                            // Slipstream: the draft and boost bits, the effect matrix and scale
                            // inputs, and this step's wall and bush contacts (Effects::Player).
                            const auto* move=racer->move();
                            in.drafting=move->slipstreamCharging();in.slipBoost=move->slipstreamBoost();in.inAction=status.onBit(St::InAction);
                            in.velocity=vec(racer->velocity());in.kartScale=vec(racer->scale());
                            in.baseSpeed=racer->param()->stats().speed;
                            // Original CPU drivers have no node callback; keep
                            // their pose fallback. The local callback's cached
                            // world root follows current physics + prediction.
                            if(i==0)in.hasFakeBoxDriverOrigin=driverEffectAnchor.predict(in.position,in.velocity,in.fakeBoxDriverOrigin);
                            const auto* objectDirector=K::Field::ObjectDirector::Instance();
                            in.hasSea=objectDirector&&objectDirector->psea();in.diving=status.onBit(St::Diving);
                            // Static water uses the original pocha GOBJ height table,
                            // not Psea's moving surface (e.g. Toad's Factory).
                            const int pochaSlot=racer->collide()->oobWaterHeightSlot();
                            if(in.beforeRespawn&&pochaSlot>=0){
                                const auto* map=K::System::CourseMap::Instance();
                                for(uint16_t j=0;j<map->getGeoObjCount();++j){
                                    const auto* object=map->getGeoObj(j);
                                    if(object->id()!=7||!(object->presenceFlag()&1))continue;
                                    in.hasPocha=true;
                                    if(object->setting(0)==pochaSlot)in.pochaHeight=object->pos().y;
                                }
                            }
                            const int lavaSlot=racer->collide()->oobLavaHeightSlot();
                            if(in.beforeRespawn&&!lavaBeforeRespawn[i]&&lavaSlot>=0){
                                const auto* map=K::System::CourseMap::Instance();
                                bool hasLava=false;float height=-60000.f;
                                for(uint16_t j=0;j<map->getGeoObjCount();++j){
                                    const auto* object=map->getGeoObj(j);
                                    if(!(object->presenceFlag()&1))continue;
                                    if(object->id()==17)hasLava=true;
                                    // Retail shares the pocha height table across water/lava/forest.
                                    if((object->id()==7||object->id()==17||object->id()==19)&&object->setting(0)==lavaSlot)height=object->pos().y;
                                }
                                if(hasLava){
                                    mkw::EffectFrame f;f.origin=in.position+in.velocity;f.origin.y=height;f.scale=in.scale;
                                    raceEffects.burst(raceEffects.find(i==0?"rk_pochaYogan":"rk_pochaYoganS"),f);
                                    courseObjects.triggerLavaSplash(i,f.origin,f.scale,physicsSteps);
                                }
                            }
                            lavaBeforeRespawn[i]=in.beforeRespawn;
                            in.bike=racer->isBike();in.groundStart=status.onBit(St::GroundStart);in.hardLanding=status.onBit(St::HardLanding);in.jumpPad=status.onBit(St::JumpPad);
                            in.chargingSSMT=status.onBit(St::ChargingSSMT);
                            in.countdownAccelerate=K::System::RaceManager::Instance()->stage()==K::System::RaceManager::Stage::Countdown&&racer->inputs()->currentState().accelerate();
                            const unsigned wheelCount=racer->tireCount(),rearL=in.bike?1:wheelCount-2,rearR=wheelCount-1;
                            in.waterRear[0]=vec(racer->wheelEdgePos(rearL));in.waterRear[1]=vec(racer->wheelEdgePos(rearR));
                            in.waterBaseSpeed=racer->param()->stats().speed;
                            for(unsigned w=0;w<2;++w){
                                const auto& c=racer->collisionData(w?rearR:rearL);
                                in.waterWheelKcl[w]=c.bFloor&&(uint32_t(c.closestFloorFlags)&(1u<<11));
                                in.waterKcl|=in.waterWheelKcl[w];
                            }
                            if(in.hasSea){
                                in.seaHeight=-objectDirector->distAboveRisingWater(0);
                                // 8058ab84: position.y + pose(1,1) * 100 * stats+0x168.
                                // Stats+0x168 is kclRot[30], not the BSP ride height.
                                in.waterBodyHeight=in.position.y+in.up.y*100.f*racer->param()->stats().kclRot[30];
                                in.waterRearHeight=std::min(in.waterRear[0].y,in.waterRear[1].y);
                                // UpdateValues +0x2f: bottom probe is initialY
                                // below the kart, plus the original 50-unit offset.
                                in.waterDeep=in.position.y-in.initialY+50.f<in.seaHeight;
                            }
                            in.waterDeep|=in.waterKcl;
                            in.pressed=in.action==14||status.onBit(St::Crushed);
                            const auto& body=racer->collisionData();
                            const bool wall=(body.bWall||body.bWall3)&&!body.bInvisibleWallOnly;
                            in.wallHit=mkw::countedWallContact(wall,uint8_t(body.closestWallFlags),uint8_t(body.closestWallSettings));
                            in.bushHit=mkw::bushContact(wall,uint8_t(body.closestWallFlags),uint8_t(body.closestWallSettings));
                            in.speedRatio=move->speedRatioCapped();
                            in.kartContact=kartContacts[i].bumpedBy!=0;in.contact=vec(kartContacts[i].contact);
                            in.objectHit=kartContacts[i].objectHit;in.objectContact=vec(kartContacts[i].objectContact);
                            in.poihanaThrow=kartContacts[i].poihanaThrow;
                            for(unsigned j=0;j<racers;++j)if(kartContacts[i].bumpedBy&(1u<<j))
                                in.otherContactFast|=K::Kart::KartObjectManager::Instance()->object(j)->velocity().length()>30.f;
                            kartFxSlots.select(i,i==0);kartFx[i].step(in,kartFxSlots,kartFxLod.lod.lod(i));
                            mkw::EffectFrame landingFrame;landingFrame.origin=in.position;landingFrame.x=in.right;landingFrame.y=in.up;landingFrame.z=in.forward;landingFrame.scale=in.scale;
                            landingEffects.step(raceEffects,i,i==0,landingFrame,(in.waterRear[0]+in.waterRear[1])*.5f,intro.active(),
                                in.beforeRespawn||in.bullet,kartFxLod.lod.lod(i),in.bike,in.groundStart,in.hardLanding,
                                status.onBit(St::DriftManual),in.jumpPad,in.waterKcl||in.diving,status.onBit(St::Wheelie),status.onBit(St::AllWheelsCollision));
                            wheelSpinEffects.step(raceEffects,i,landingFrame,vec(racer->wheelPos(rearL)),vec(racer->wheelPos(rearR)),
                                in.bike,status.onBit(St::ChargingSSMT),move->ssmtCharged(),status.onBit(St::StickLeft)?-1:status.onBit(St::StickRight)?1:0,
                                racer->collisionData(rearL).bFloor,racer->collisionData(rearR).bFloor,
                                intro.active(),in.beforeRespawn||in.bullet,
                                K::System::RaceConfig::Instance()->raceScenario().players[i].type==K::System::RaceConfig::Player::Type::Ghost,
                                kartFxLod.lod.lod(i));
                            brakeEffects.step(raceEffects,i,landingFrame,vec(racer->wheelPos(rearR)),
                                in.bike,i==0,K::System::RaceConfig::Instance()->raceScenario().players[i].type==K::System::RaceConfig::Player::Type::Ghost,
                                status.onBit(St::Brake),status.onBit(St::Accelerate),unsigned(move->driftState()),status.onBit(St::DriftAuto),
                                racer->speed(),racer->collisionData(rearR).bFloor,intro.active(),in.beforeRespawn||in.bullet,kartFxLod.lod.lod(i));
                            if(i){
                                const bool bike=racer->isBike();const unsigned wheels=racer->tireCount();
                                const unsigned l=bike?1:wheels-2,r=wheels-1;
                                mkw::EffectFrame frame;frame.origin=in.position;frame.x=in.right;frame.y=in.up;frame.z=in.forward;frame.scale=in.scale;
                                remoteSmoke.step(raceEffects,i,frame,vec(racer->wheelEdgePos(l)),vec(racer->wheelEdgePos(r)),
                                    K::System::RaceManager::Instance()->timer(),intro.active(),
                                    K::System::RaceManager::Instance()->stage()==K::System::RaceManager::Stage::Countdown,
                                    racer->inputs()->currentState().accelerate(),racer->collisionData(l).bFloor,racer->collisionData(r).bFloor,
                                    bike,status.onBit(St::Burnout),in.beforeRespawn||in.bullet,
                                    move->startEffectSerial(),move->startEffectDuration(),kartFxLod.lod.lod(i));
                                mkw::KartSurfaceEffects::Wheel surfaceWheels[2];
                                for(unsigned w=0;w<2;++w){const unsigned tire=w?r:l;const auto& c=racer->collisionData(tire);
                                    surfaceWheels[w]={vec(racer->wheelEdgePos(tire)),uint32_t(c.closestFloorFlags),c.closestFloorSettings,c.bFloor};}
                                const unsigned surfaceCourse=unsigned(Courses[courseChoice].id);
                                const bool sandArea=surfaceCourse==14&&K::System::CourseMap::Instance()->getCurrentAreaID(-1,racer->pos(),K::System::MapdataAreaBase::Type(1))!=-1;
                                surfaceEffects[i].step(raceEffects,frame,surfaceWheels,surfaceCourse,bike,
                                    !intro.active()&&!in.beforeRespawn&&!in.bullet&&!kartFxLod.lod.lod(i)&&
                                    K::System::RaceManager::Instance()->stage()>=K::System::RaceManager::Stage::Race,
                                    unsigned(move->driftState())>=2,mkw::length(vec(racer->velocity())),move->softSpeedLimit(),
                                    status.onBit(St::ChargingSSMT),remoteSmoke.startActive(i),sandArea,i);
                                mkw::OpponentDrivingInput driving;
                                driving.pose=frame;driving.rearLeft=vec(racer->wheelPos(l));driving.rearRight=vec(racer->wheelPos(r));
                                driving.vehicle=unsigned(K::System::RaceConfig::Instance()->raceScenario().players[i].vehicle);
                                driving.bike=bike;driving.driftState=unsigned(move->driftState());driving.driftDirection=move->hopStickX();
                                driving.insideDrift=racer->param()->stats().driftType==K::Kart::KartParam::Stats::DriftType::Inside_Drift_Bike;
                                driving.boost=status.onBit(St::Boost,St::RampBoost);
                                driving.wheelie=status.onBit(St::Wheelie);driving.canWheelie=move->canWheelie();
                                driving.leftFloor=racer->collisionData(l).bFloor;driving.rightFloor=racer->collisionData(r).bFloor;
                                driving.preview=intro.active();driving.hidden=in.beforeRespawn||in.bullet;
                                opponentDrivingEffects.step(raceEffects,i,driving,kartFxLod.lod.lod(i));
                            }
                            // A racer's group calculates (0x80678818) and draws (0x80678aec)
                            // without +7 or with +9.
                            const bool keep=kartFx[i].keepAlive();
                            if(!kartFxLod.lod.calcs(i,keep))calcGroups&=~mkw::KartFxSlots::groupBit(i);
                            if(!kartFxLod.lod.draws(i,0,keep))drawGroups&=~mkw::KartFxSlots::groupBit(i);
                        }
                        raceEffects.setGroups(calcGroups,drawGroups);
                    }
                    if(fullRace){creatureEffects.step(raceEffects);blooperScreenEffects.step(raceEffects,raceItems->hud(0).ink,raceGrid.rank(),raceGrid.count());}
                    raceEffects.step();
                    {mkw::Vec3 eye,target;presentationCamera(eye,target);raceEffects.setCamera(eye,target,{0,1,0});
                     // The clip manager (0x80783344) after the camera: each racer's sphere on
                     // the presented camera, full vertical fovy, the top screen's aspect.
                     if(fullRace){
                         const auto clipCamera=mkw::EffectClipCamera::lookAt(eye,target,intro.active()?intro.fov():cameraDirector->view().fov,mkw::display::Aspect);
                         const unsigned racers=std::min<unsigned>(raceGrid.count(),mkw::KartFxSlots::MaxRacers);
                         for(unsigned i=0;i<racers;++i)kartFxLod.clip[i]=clipCamera.clip(vec(K::Kart::KartObjectManager::Instance()->object(i)->pos()));
                         const bool forwards=!K::Render::KartCamera::Instance()->lookingBack();
                         creatureEffects.updateClip(&clipCamera,&forwards,1);
                     }}
                }
                // QA captures: present exactly on each capture tick (the remaining
                // accumulator runs next iteration), so builds compare frame for frame.
                if(autoplay&&intro.active()&&introTick%90==0)break;
                if(raceCapture&&menu.simulates()&&!intro.active()&&physicsSteps%raceCaptureEvery==0)break;
            }
            physicsMs=(svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11;maxPhysics=std::max(maxPhysics,physicsMs);
            profiler->mark(Profile::Simulation);
            if(fullRace){
                // The 3D slider can move mid-race: the rate follows it.
                if(const unsigned ticks=raceTicksPerFrame();ticks!=presentation.ticksPerSlot())presentation.setTicksPerSlot(ticks);
                // Never stall the 60 Hz input/physics loop for a busy render
                // list. Coalesce only the presentation; next frame snapshots
                // the newest pose, inputs and original animation clocks.
                if(!renderer.canRecordFrame()||!presentation.due(physicsSteps)){profiler->end(physicsSteps,renderer,false);continue;}
            }
            animationBudget.present(physicsSteps,fullRace&&!intro.active()&&!results.active()&&!raceGrid.finished()&&!raceWarmupFrames);
            renderer.loadingFrame=raceWarmupFrames!=0;
            renderer.frameSync=!fullRace;
            auto* camera=K::Render::KartCamera::Instance();auto* cameraKart=K::Kart::KartObjectManager::Instance()->object(0);
            renderer.animationFrame=physicsSteps;
            const auto raceView=cameraDirector->view();
            // Race geometry is authored in Wii world units. A 10-unit near
            // plane wasted depth precision on distant layered surfaces.
            // Garage/menu cameras retain their smaller clipping plane.
            renderer.setFov(intro.active()?intro.fov():fullRace?raceView.fov:cameraKart->param()->camera().fov+boostFov,100.f);
            if(fullRace){
                auto* race=K::System::RaceManager::Instance();auto& timer=race->timerManager().currentTimer();
                uiRace.count=raceGrid.count();uiRace.milliseconds=timer.min*60000+timer.sec*1000+timer.mil;
                for(unsigned i=0;i<uiRace.count;++i){auto p=K::Kart::KartObjectManager::Instance()->object(i)->pos();uiRace.racers[i]={raceGrid.garageChoice(i)/mkw::VehiclesPerDriver,raceGrid.rank(i),raceGrid.lap(i),raceGrid.finished(i),p.x,p.z};}
                // Only pages drawn over the race compose here; a page the race is
                // leaving for (e.g. the course choice after Next Race) is loaded
                // by runMenu once the race is released.
                if(menu.page==mkw::UiPage::Pause||menu.page==mkw::UiPage::QuitConfirm)mkw::composeMenu(menu,uiRace,raceQuads);
                else raceQuads.clear();
                if(menuRenderer.needsPrepare(raceQuads)){renderer.waitForGpu();
                    if(!menuRenderer.prepare(raceQuads))report("FAIL race map UI asset load");}
                // Board and menu textures arrive one per frame after the finish,
                // well before the board slides in.
                if(results.active()&&!resultsReported){
                    if(flowUi.loadResultsStep(battleMode)){
                        resultsReported=true;
                        LOGF("race flow results resident bytes=%u linear_free=%lu",flowUi.resultsBytes(),(unsigned long)linearSpaceFree());
                    }else if(flowUi.resultsFailed()){resultsReported=true;report("FAIL race flow results load");}
                }
            }
            profiler->mark(Profile::UiPrepare);
            if(fullRace&&!effectsTest&&(int(Courses[courseChoice].id)==18||int(Courses[courseChoice].id)==6||int(Courses[courseChoice].id)==35))courseObjects.requestStadiumScreen(renderer);
            if(fullRace&&!effectsTest)if(const auto* bloom=mkw::courseBloom(Courses[courseChoice].archive))renderer.requestBloom(*bloom);
            {
                // Fog follows the camera through KMP fog areas, one retail step per simulation step.
                const unsigned steps=physicsSteps>retailFogSteps?physicsSteps-retailFogSteps:0;retailFogSteps=physicsSteps;
                stepRetailFog(effectsTest&&frames<60?mkw::Vec3{-15800,2700,49500}:intro.active()?intro.eye():fullRace?raceView.eye:vec(camera->position()),steps);
            }
            {mkw::Vec3 eye,target;presentationCamera(eye,target);
             // Stereo (docs/STEREO_3D.md): Mario Kart 7's race camera on every race
             // shot, intro and replays included. Its reference plane keeps a fixed
             // view depth, a set fraction of the kart's depth along the chase
             // camera, so the kart sits 2 to 3.5 px behind the screen in the chase view.
             if(renderer.stereoCapable()&&fullRace){
                 const auto& params=cameraKart->param()->camera();
                 const mkw::Vec3 chase=chaseCamera.eye(),look=chaseCamera.pivot()+mkw::Vec3{0,params.targetPosY-cameraKart->cameraDistY(),0};
                 renderer.setStereoSubject(mkw::stereo::Race,dot(vec(cameraKart->pos())-chase,mkw::normalized(look-chase)));
             }else if(renderer.stereoCapable())renderer.setStereoSubject(mkw::stereo::Race,dot(vec(cameraKart->pos())-eye,mkw::normalized(target-eye)));
             renderer.setDualScreenIntro(fullRace&&intro.active());
             renderer.begin(eye,target,{0,1,0},selectedMirror);}
            profiler->mark(Profile::Wait);
            unsigned castleScene=mkw::castleRegion::Both;
            if(fullRace&&int(Courses[courseChoice].id)==12&&!intro.active()&&!results.active()&&
               !track.empty()&&track.front()->regionIndices[0]&&track.front()->regionIndices[1]&&
               cameraKart->status().offBit(K::Kart::eStatus::BeforeRespawn,K::Kart::eStatus::InRespawn,K::Kart::eStatus::InCannon)&&
               (!fullRace||!raceItems->hud(0).bullet)&&
               mkw::length(renderer.cameraPosition()-vec(cameraKart->pos()))<3000.f){
                const unsigned ck=K::System::RaceManager::Instance()->player(0).checkpointId();
                const unsigned playerRegion=mkw::castleRegion::checkpoint(ck);
                // Original sector search uses scratch searched flags only;
                // it does not advance RaceManager or any race progress.
                // Run before presentation jobs, after all physics steps.
                float ratio=0;const auto e=renderer.cameraPosition();
                const int cameraCk=ck<58?K::System::CourseMap::Instance()->findSector({e.x,e.y,e.z},ck,ratio):-1;
                const unsigned cameraRegion=cameraCk<0?mkw::castleRegion::Both:mkw::castleRegion::checkpoint(unsigned(cameraCk));
                if(playerRegion==cameraRegion)castleScene=playerRegion;
            }
            renderer.setSceneRegion(castleScene);
            // Opponent poses build on the job worker while the course draws.
            // Player pose: clips, the driver's face and skeleton, the body and
            // accessory clips. Their banks are independent of the draws below.
            const char* playerClip="";
            auto posePlayer=[&](){
                auto presentation=driverPose;
                const char* clipName=presentation.clipName;
                clipName=mkw::driverVehicleClipName(clipName,cameraKart->param()->isBike());
                float clipFrame=presentation.frame;
                if(!std::strcmp(clipName,"jump")&&driverAnimation.find(clipName)<0){clipName="jump_st";int clip=driverAnimation.find(clipName);clipFrame=clip>=0?driverAnimation.duration(clip)-1:0;}
                if(driverAnimation.face(clipName)>=0){presentation.faceName=clipName;presentation.faceFrame=clipFrame;}
                if(effectsTest&&frames>=180){static const char* fixture[]={"sjump_a","sjump_b","sjump_c"};clipName=fixture[(frames-180)/60];clipFrame=(frames-180)%60;}
                if(animationTest){
                    unsigned local=frames%fixtureFrames,interval=garageTest?2:12;
                    unsigned index=driverAnimation.count()?std::min(local/interval,driverAnimation.count()-1):0;
                    clipName=driverAnimation.name(index);clipFrame=float(local%interval)*(driverAnimation.duration(index)-1)/float(interval-1);
                    if(garageTest&&local>=60){clipName="drive";clipFrame=15;}
                    presentation.faceName=clipName;presentation.faceFrame=clipFrame;
                    if(local==0)++checkedDrivers;
                    if(local%interval==0&&local/interval<driverAnimation.count())++checkedClips;
                }
                playerClip=clipName;
                if(driver){
                    C3D_Mtx hairWorld=vehicleAnimation.body();
                    if(!replay)Mtx_Translate(&hairWorld,0,selectedVehicle().seatY,selectedVehicle().seatZ,true);
                    driverAnimation.setBraidWorld(hairWorld);
                    driverAnimation.apply(*driver,driverAnimation.find(clipName),clipFrame,animationTest?physicsSteps*6:physicsSteps,driverAnimation.face(presentation.faceName),presentation.faceFrame);
                    if(animationTest){for(unsigned i=0;i<driver->vertexCount;++i){auto v=driver->vertices[i].position;animationPass&=std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}++checkedFrames;}
                }
                auto animatePart=[&](mkw::DriverAnimation* bank,mkw::Model& model,bool handle,PartClock& clock){
                    if(!bank)return;
                    int clip=partClip(bank,*cameraKart,handle);if(clip<0)return;
                    if(clock.clip!=clip){clock.clip=clip;clock.start=physicsSteps;}
                    float frame=handle&&bank->handleDriveClip()==clip?vehicleAnimation.driveFrame()*(bank->duration(clip)-1)/30.f:float(physicsSteps-clock.start);
                    bank->apply(model,clip,frame,physicsSteps);
                };
                if(vehicle&&(!fullRace||!raceItems->hud(0).bullet)){
                    vehicleAnimation.body();animatePart(bodyAnimation.get(),*vehicle,false,bodyClock);
                    for(unsigned i=0;i<accessories.size();++i)if(accessories[i])animatePart(accessoryAnimations[i].get(),*accessories[i],i==0,accessoryClocks[i]);
                }
            };
            // Course objects record first: they are drawn first.
            courseObjects.prepare(renderer,physicsSteps,renderer.cameraPosition());
            if(fullRace)opponents.prepare(renderer,*raceItems,vec(cameraKart->pos()),physicsSteps,intro.active()||results.active()||raceGrid.finished());
            // Snapshot the original chassis transform before the player pose
            // worker reads VehicleAnimation's lazy matrices.
            const C3D_Mtx playerShadowTransform=vehicleAnimation.body();
            const bool playerBattleVisible=cameraKart->move()->battleVisible();
            // Native soft-wall contact distinguishes an entered snow shell
            // from the supporting ground underneath; ordinary offroad wheel
            // depth is not a soft wall. Keep casting on the lower support.
            bool softTerrainContact=cameraKart->collisionData().bSoftWall;
            for(unsigned w=0;w<vehicleAnimation.wheelCount();++w)
                softTerrainContact|=cameraKart->collisionData(w).bSoftWall;
            const bool insideSoftTerrain=softTerrainContact&&
                cameraKart->status().onBit(K::Kart::eStatus::SoftWallSuspension);
            const bool playerCastsShadow=vehicleShadow&&(!fullRace||!raceItems->hud(0).bullet)&&playerBattleVisible;
            // Retail collision already distinguishes drivable-object support.
            // Choose one whole shadow, never fill silhouette gaps with a blob.
            bool objectSupport=cameraKart->collisionData().bHasRoadVel;
            for(unsigned w=0;w<vehicleAnimation.wheelCount();++w)
                objectSupport|=cameraKart->collisionData(w).bHasRoadVel;
            const bool playerDynamicShadow=menu.selection.dynamicShadows&&renderer.playerShadowReady();
            // Test fixtures count clips while posing: they pose on this thread.
            playerJobQueued=fullRace&&!animationTest&&!effectsTest&&mkw::jobWorker().running();
            if(playerJobQueued){playerJob.reset(&runJob<decltype(posePlayer)>,&posePlayer);mkw::jobWorker().submit(playerJob);}
            C3D_Mtx transform;Mtx_Identity(&transform);
            renderer.setCourseLighting(true);
            if(castleScene!=mkw::castleRegion::Inside){
                for(auto& model:sky)renderer.draw(*model,transform,true);
                courseObjects.drawSky(renderer,physicsSteps,renderer.cameraPosition());
            }
            for(auto& model:track)drawCourseLayer(renderer,*model,transform,int(Courses[courseChoice].id),false);
            // Authored road decals belong below every racer shadow. Drawing
            // them in the late glass/light pass painted over both shadow types.
            // Coconut's coplanar glass is also the whitening layer above the
            // submerged PalmTree trunks. It must follow those opaque objects,
            // while still preceding racers and their native/static shadows.
            const bool glassOverObjects=Courses[courseChoice].id==K::Course::Coconut_Mall;
            if(!glassOverObjects)for(auto& model:track)drawCourseLayer(renderer,*model,transform,int(Courses[courseChoice].id),true,true);
            profiler->mark(Profile::Course);
            courseObjects.draw(renderer,physicsSteps,renderer.cameraPosition());
            if(glassOverObjects)for(auto& model:track)drawCourseLayer(renderer,*model,transform,int(Courses[courseChoice].id),true,true);
            profiler->mark(Profile::Scenery);
            if(coinRunnersMode)coinBattle.drawShadows(renderer,physicsSteps);
            renderer.setCourseLighting(false);
            if(playerJobQueued)mkw::jobWorker().finish(playerJob);else posePlayer();
            playerJobQueued=false;
            if(fullRace)opponents.draw(renderer,*raceItems,vec(cameraKart->pos()),physicsSteps,
                playerCastsShadow&&!playerDynamicShadow?vehicleShadow.get():nullptr,&playerShadowTransform);
            if(playerCastsShadow&&!fullRace&&!playerDynamicShadow){
                mkw::Renderer::ShadowVolume shadow{vehicleShadow.get(),playerShadowTransform};
                renderer.drawShadowVolumes(&shadow,1,.5f);
            }
            profiler->mark(Profile::Opponents);
            if(fullRace)raceHud.drawWorld(renderer,*raceItems,vec(cameraKart->pos()),physicsSteps);
            if(fullRace)lakitu.draw(renderer);
            profiler->mark(Profile::Items);
            auto drawSkinned=[&](const mkw::DriverAnimation* bank,const mkw::Model& model,const C3D_Mtx& matrix){
                if(bank&&bank->gpuSkinned())renderer.setSkinPalette(bank->palette(),bank->matrixCount(),21);
                renderer.draw(model,matrix);
                renderer.setSkinPalette(nullptr,0,12);
            };
            driverEffectAnchor.reset();
            if(vehicle&&(!fullRace||!raceItems->hud(0).bullet)&&playerBattleVisible){
                renderer.additiveColor=fullRace?(mkw::starMaterialColor(raceItems->hud(0).star)|
                    uint32_t(raceItems->hud(0).inkShade)):0;
                transform=vehicleAnimation.body();drawSkinned(bodyAnimation.get(),*vehicle,transform);
                if(playerLuma){playerLuma->prepare(renderer.viewFrustum(),vehicleAnimation,physicsSteps);playerLuma->draw(renderer);}
                if(driver){C3D_Mtx seated=transform;const auto& choice=selectedVehicle();
                    if(!replay)Mtx_Translate(&seated,0,choice.seatY,choice.seatZ,true);
                    drawSkinned(&driverAnimation,*driver,seated);
                    C3D_Mtx head;if(miiHeadTransform(seated,head))playerMiiHead->draw(renderer,head,mkw::NativeMiiHead::expression(driverPose));
                    mkw::Vec3 root;
                    if(driverAnimation.effectRoot(root)){
                        const mkw::Vec3 world{
                            seated.r[0].x*root.x+seated.r[0].y*root.y+seated.r[0].z*root.z+seated.r[0].w,
                            seated.r[1].x*root.x+seated.r[1].y*root.y+seated.r[1].z*root.z+seated.r[1].w,
                            seated.r[2].x*root.x+seated.r[2].y*root.y+seated.r[2].z*root.z+seated.r[2].w};
                        driverEffectAnchor.store(world,vec(cameraKart->pos()));
                    }
                }
                for(unsigned i=0;i<vehicleAnimation.wheelCount();++i){
                    auto& model=wheels[vehicleAnimation.modelIndex(i)];if(!model)continue;
                    renderer.draw(*model,vehicleAnimation.wheel(i));
                }
                for(unsigned i=0;i<accessories.size();++i)if(accessories[i]){
                    const char* name=mkw::GarageExtraNames[i];
                    for(unsigned instance=0;instance<vehicleAnimation.accessoryCount(name);++instance)
                        drawSkinned(accessoryAnimations[i].get(),*accessories[i],vehicleAnimation.accessory(name,accessoryAnimations[i]&&accessoryAnimations[i]->handleDriveClip()>=0,instance));
                }
            }
            if(coinRunnersMode){renderer.setCourseLighting(true);coinBattle.draw(renderer,physicsSteps);renderer.setCourseLighting(false);}
            if(battleMode&&!coinRunnersMode)balloonRuntime.draw(renderer);
            renderer.additiveColor=0;
            profiler->mark(Profile::Player);
            renderer.setCourseLighting(true);
            Mtx_Identity(&transform);
            for(auto& model:track)drawCourseLayer(renderer,*model,transform,int(Courses[courseChoice].id),true);
            courseObjects.drawWater(renderer);
            renderer.setCourseLighting(false);
            // Resolve local projected shadows after all scene layers. Keep scene
            // depth intact so opaque actors and entered snow still occlude them.
            const bool mergeJudgeShadow=playerDynamicShadow&&fullRace&&lakitu.shadowVisible();
            if(mergeJudgeShadow)renderer.setShadowUnion(true);
            if(playerDynamicShadow&&vehicle&&(!fullRace||!raceItems->hud(0).bullet)&&playerBattleVisible){
                std::array<mkw::Renderer::ShadowCaster,32> casters{};unsigned casterCount=0;
                auto caster=[&](const mkw::Model* model,const C3D_Mtx& matrix,const mkw::DriverAnimation* bank=nullptr){
                    if(!model||casterCount==casters.size())return;
                    auto& c=casters[casterCount++];c.model=model;c.transform=matrix;c.softTerrain=insideSoftTerrain;c.movingSupport=objectSupport;
                    if(bank&&bank->gpuSkinned()){c.palette=bank->palette();c.bones=bank->matrixCount();}
                };
                const C3D_Mtx chassis=vehicleAnimation.body();caster(vehicle.get(),chassis,bodyAnimation.get());
                if(driver){C3D_Mtx seated=chassis;const auto& choice=selectedVehicle();
                    if(!replay)Mtx_Translate(&seated,0,choice.seatY,choice.seatZ,true);
                    caster(driver.get(),seated,&driverAnimation);
                    C3D_Mtx head;if(miiHeadTransform(seated,head))caster(playerMiiHead->shadowModel(),head);}
                const auto contactPose=vehicleAnimation.snapshot();
                const bool rescueContact=cameraKart->status().onBit(K::Kart::eStatus::BeforeRespawn,K::Kart::eStatus::InRespawn);
                const bool tiltedContact=mkw::playerShadow::hideContactForRoll(
                    {chassis.r[0].x,chassis.r[1].x,chassis.r[2].x},
                    {chassis.r[0].y,chassis.r[1].y,chassis.r[2].y});
                for(unsigned w=0;w<vehicleAnimation.wheelCount();++w){
                    const auto before=casterCount;
                    caster(wheels[vehicleAnimation.modelIndex(w)].get(),vehicleAnimation.wheel(w));
                    if(casterCount>before){
                        auto& part=casters[casterCount-1];part.wheelContact=cameraKart->collisionData(w).bFloor;
                        part.contactCenter=contactPose.tirePositions[w];
                        part.contactNormal=mkw::normalized(vec(cameraKart->collisionData(w).floorNrm));
                        // Wheelie is intentionally exempt from the small-hop allowance.
                        // Bike wheel 0 is the front; keep only the rear decal.
                        const bool liftedFront=selectedVehicle().bike&&w==0&&cameraKart->status().onBit(K::Kart::eStatus::Wheelie);
                        part.contactRadius=liftedFront?0.f:contactPose.radius[w]*contactPose.scale.y;
                        part.hideContact=rescueContact||tiltedContact||courseObjects.suppressWheelContact(part.contactCenter,part.contactRadius);
                        part.contactSpread=1.53f*.8f*1.2f;
                        // Wheel roll is around local X, so its width/axle are
                        // stable through wheel spin. Retain authored rear scale.
                        const auto& wheelMesh=*part.model;
                        const mkw::Vec3 axle{part.transform.r[0].x,part.transform.r[1].x,part.transform.r[2].x};
                        const float halfWidth=.5f*(wheelMesh.maximum.x-wheelMesh.minimum.x)*mkw::length(axle);
                        part.contactHalfWidth=(halfWidth*1.15f+1.f)*1.1f*1.15f*1.2f;
                    }
                }
                for(unsigned j=0;j<accessories.size();++j)if(accessories[j])
                    for(unsigned n=0;n<vehicleAnimation.accessoryCount(mkw::GarageExtraNames[j]);++n)
                        caster(accessories[j].get(),vehicleAnimation.accessory(mkw::GarageExtraNames[j],accessoryAnimations[j]&&accessoryAnimations[j]->handleDriveClip()>=0,n),accessoryAnimations[j].get());
                std::array<const mkw::Model*,256> receivers{};unsigned receiverCount=0;
                for(const auto& model:track)if(receiverCount<receivers.size())receivers[receiverCount++]=model.get();
                const auto origin=vec(cameraKart->pos());
                const mkw::Renderer::ShadowVolume fallback{vehicleShadow.get(),playerShadowTransform};
                const auto& objectReceivers=courseObjects.shadowReceivers(origin,std::max(320.f,320.f*cameraKart->scale().y)*4.f);
                renderer.drawPlayerShadow(casters.data(),casterCount,receivers.data(),receiverCount,origin,playerShadowLight,cameraKart->scale().y,playerCastsShadow&&!insideSoftTerrain?&fallback:nullptr,objectReceivers.data(),unsigned(objectReceivers.size()));
            }
            if(fullRace&&lakitu.shadowVisible()){
                std::array<const mkw::Model*,256> receivers{};unsigned count=0;
                for(const auto& model:track)if(count<receivers.size())receivers[count++]=model.get();
                const auto& objects=courseObjects.shadowReceivers(lakitu.position(),2400.f);
                lakitu.drawShadow(renderer,receivers.data(),count,playerShadowLight,menu.selection.dynamicShadows,
                                  objects.data(),unsigned(objects.size()));
            }
            if(mergeJudgeShadow)renderer.setShadowUnion(false);
            renderer.drawSparks(driftEffects,sparkTexture);
            renderer.drawEffects(raceEffects,effectTextures->data());
            renderer.markIntroWorldEnd();
            mkw::jobWorker().finishAll();
            renderer.applyBloom();
            if(fullRace)renderer.drawScreenFade(lightningFlash.opacity(),lightningFlash.white);
            profiler->mark(Profile::Effects);
            // Stereo layers (Mario Kart 7's): the HUD and result boards 4 px
            // behind the screen; the full-screen wipe and the pause menu on the
            // screen plane.
            if(fullRace&&intro.active()){renderer.setScreenDepth(renderer.stereoLayer(mkw::stereo::HudLayer));flowUi.drawIntro(renderer,introInfo(),introTick);}
            if(fullRace&&!intro.active()){auto* race=K::System::RaceManager::Instance();

                raceHud.drawOverlay(renderer,*raceItems,unsigned(race->stage()),race->timer(),battleMode?battleRank(0):raceGrid.rank(),raceGrid.lap(),battleMode?battleTicks>=balloonBattle.durationTicks():raceGrid.finished(),physicsSteps,results.finishFrame(),results.hudSlide(),camera->lookingBack());
}
            renderer.setScreenDepth(renderer.stereoLayer(mkw::stereo::HudLayer));
            if(fullRace)flowUi.drawResults(renderer,results,bottomScreen.miiPortrait());
            renderer.setScreenDepth(0);
            if(fullRace)flowUi.drawWipe(renderer,cameraDirector->wipeProgress());
            profiler->mark(Profile::Hud);
            if(fullRace&&menu.page!=mkw::UiPage::Race)menuRenderer.draw(renderer,raceQuads,false);
            // Leaving after the end-of-race menu: the scene fader covers everything.
            if(fullRace)renderer.drawScreenFade(results.leaveFade(),false);
            if(fullRace&&bottomScreen.active()&&!intro.active()){
                // The lower LCD: the race read, never written (src/race_bottom_screen.cpp).
                mkw::fillBottomView(bottomView,raceGrid,*raceItems,intro.active(),physicsSteps,results.leaveFade());
                if(battleMode){
                    bottomView.battle=true;
                    bottomView.milliseconds=uint32_t(uint64_t(balloonBattle.durationTicks()-std::min(battleTicks,balloonBattle.durationTicks()))*1000/60);
                    for(unsigned i=0;i<bottomView.racers;++i){bottomView.racer[i].rank=uint8_t(battleRank(i));bottomView.racer[i].score=uint16_t(battleScore(i));}
                }
                renderer.beginBottom();
                bottomScreen.draw(renderer,bottomView);
            }
            profiler->mark(Profile::Bottom);
            if(qaMode&&frames%15==0){
                printf("\x1b[18;1H%-39s",trackLoadFailed?"ASSET LOAD FAILED: check build/package":"");
                auto* race=K::System::RaceManager::Instance();auto& timer=race->timerManager().currentTimer();
                printf("\x1b[9;1H%-18s %-18s\n%-18s\n%02u:%02u.%03u  stage %d       \nCP %u  progress %.2f    \nPhysics %.2f ms peak %.2f   \nTrack parts %u  resets %u\nHW-r2 draws %u tris %u        \n",replay?"Funky Kong (replay)":mkw::UiCharacters[std::min(driverChoice,25u)].name,replay?"Spear":selectedVehicle().name,playerClip,timer.min,timer.sec,timer.mil,int(race->stage()),race->player().checkpointId(),race->player().raceCompletion(),physicsMs,maxPhysics,unsigned(track.size()),restarts,renderer.drawCalls,renderer.triangles);
                if(fullRace)printf("\x1b[19;1HPosition %2u/12  Lap %u/3              \nItem: %-25s\nHold Circle Pad down + L: throw back\n",raceGrid.rank(),raceGrid.lap(),mkw::itemName(raceItems->hud(0).item));
            }
        }else renderer.begin({0,100,500},{0,0,0},{0,1,0});
        if(frames==0)report("graphics first frame submitting");
        renderer.end();profiler->mark(Profile::Submit);profiler->end(physicsSteps,renderer);frames++;
        if(raceWarmupFrames){
            renderer.waitForGpu();
            if(!--raceWarmupFrames)K::System::RaceManager::Instance()->setIntroHeld(intro.active());
            renderer.loadingFrame=false;
            accumulator=0;previous=svcGetSystemTick();lastRefresh=C3D_FrameCounter(0);presentation.reset();
        }
        if(raceLoadStart){
            const double ms=double(svcGetSystemTick()-raceLoadStart)*1000.0/SYSCLOCK_ARM11;raceLoadStart=0;lastRaceFrame=0;
            LOGF("race_load_ms=%.1f course=%s driver=%u vehicle=%u failed=%d",ms,Courses[courseChoice].name,driverChoice,vehicleChoice,int(trackLoadFailed));menuProfile->event(line);
        }
        if(menuProfile->enabled()){const auto now=svcGetSystemTick();menuProfile->raceFrame(lastRaceFrame?float(double(now-lastRaceFrame)*1000.0/SYSCLOCK_ARM11):0.f);lastRaceFrame=now;}
        if(frames==1)report("graphics first frame submitted");
        if(frames==2)report("graphics second frame submitted");
        if(garageTest&&frames%fixtureFrames==0){rollingChoices+=fixtureRolling;animationPass&=fixtureRolling;}
        // QA (menu autoplay): sample the course preview for review.
        if(autoplay&&intro.active()&&introTick/90!=introCaptured){introCaptured=introTick/90;
            std::vector<uint8_t> pixels(400*240*3);char path[64];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/intro-%02u-%04u.ppm",unsigned(Courses[courseChoice].id),introTick);
            if(renderer.captureTop(pixels.data()))if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
            saveRightEye(renderer,path);
            std::vector<uint8_t> lower(320*240*3);std::string lowerPath(path);lowerPath.insert(lowerPath.size()-4,"-bottom");
            if(renderer.captureBottom(lower.data()))if(FILE* f=fopen(lowerPath.c_str(),"wb")){fprintf(f,"P6\n320 240\n255\n");fwrite(lower.data(),1,lower.size(),f);fclose(f);}
        }
        // QA (autoplay + race-capture.txt): sample race frames for review.
        if(raceCapture&&menu.simulates()&&!intro.active()&&(physicsSteps/raceCaptureEvery!=raceCaptured||(raceCaptureLeave&&results.phase()>=mkw::RaceFlowPhase::Chosen))){raceCaptured=physicsSteps/raceCaptureEvery;
            std::vector<uint8_t> pixels(400*240*3);char path[64];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/race-%05u.ppm",physicsSteps);
            if(renderer.captureTop(pixels.data()))if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
            saveRightEye(renderer,path);
            // The lower LCD's frame beside it (name-bottom.ppm, 320x240).
            if(bottomScreen.active()&&renderer.captureBottom(pixels.data())){snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/race-%05u-bottom.ppm",physicsSteps);
                if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n320 240\n255\n");fwrite(pixels.data(),1,320*240*3,f);fclose(f);}}
        }
        if(replay&&!capturedBlue&&driftEffects.blueFrames>=25){
            std::vector<uint8_t> pixels(400*240*3);if(renderer.captureTop(pixels.data()))if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/capture-live-drift.ppm","wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}capturedBlue=true;
        }
        if(animationTest&&frames%fixtureFrames==fixtureFrames-1){
            std::vector<uint8_t> pixels(400*240*3);char path[96];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/capture-choice-%u.ppm",driverChoice*mkw::VehiclesPerDriver+vehicleChoice);
            if(renderer.captureTop(pixels.data()))if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
        }
        if(animationTest&&frames==fixtureFrames*fixtureChoices){
            if(FILE* f=fopen(garageTest?"garage-result.json":"animation-result.json","w")){fprintf(f,"{\"pass\":%s,\"visual_fixture\":true,\"choices\":%u,\"clips\":%u,\"finite_pose_frames\":%u,\"rolling_choices\":%u,\"linear_free\":%lu}\n",animationPass&&checkedDrivers==fixtureChoices?"true":"false",checkedDrivers,checkedClips,checkedFrames,rollingChoices,(unsigned long)linearSpaceFree());fclose(f);}break;
        }
        if(effectsTest&&frames==360){
            if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/effects-result.json","w")){fprintf(f,"{\"pass\":%s,\"visual_fixture\":true,\"source_sky_loaded\":%s,\"source_spark_loaded\":%s,\"blue_frames\":%u,\"orange_frames\":%u,\"particles_emitted\":%u}\n",!sky.empty()&&(sparkTexture[0].ready&&sparkTexture[1].ready&&driftEffects.ready)&&driftEffects.blueFrames&&driftEffects.orangeFrames?"true":"false",!sky.empty()?"true":"false",(sparkTexture[0].ready&&sparkTexture[1].ready&&driftEffects.ready)?"true":"false",driftEffects.blueFrames,driftEffects.orangeFrames,driftEffects.emitted);fclose(f);}break;
        }
        // QA (shots.txt): capture the top screen once the race reaches each
        // listed physics step, as shot-<step>.ppm. Works alongside autoplay.
        if(kQa){
            static std::vector<unsigned> shots;static bool shotsRead=false;
            if(!shotsRead){shotsRead=true;if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/shots.txt","r")){unsigned v;while(fscanf(f,"%u",&v)==1)shots.push_back(v);fclose(f);std::sort(shots.begin(),shots.end(),std::greater<unsigned>());}}
            if(!shots.empty()&&physicsSteps>=shots.back()){
                std::vector<uint8_t> pixels(400*240*3);
                if(renderer.captureTop(pixels.data())){char path[64];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/shot-%u.ppm",shots.back());if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}}
                shots.pop_back();
            }
        }
        if(dataReady&&((effectsTest&&(frames==5||frames==35||frames==95||frames==140||frames==155||frames==170||frames==186||frames==194||frames==210||frames==244||frames==250||frames==262||frames==270||frames==292||frames==310||frames==330))||(smoke&&frames==120)||(replay&&(frames==600||frames==1600)))){
            std::vector<uint8_t> pixels(400*240*3);if(renderer.captureTop(pixels.data())){
                char path[64];snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/capture-%u.ppm",frames);if(FILE* f=fopen(path,"wb")){fprintf(f,"P6\n400 240\n255\n");fwrite(pixels.data(),1,pixels.size(),f);fclose(f);}
            }
        }
        if(replay&&K::System::RaceManager::Instance()->stage()==K::System::RaceManager::Stage::FinishGlobal){
            K::System::RawGhostFile raw(replayData);K::System::GhostFile ghost(raw);
            const auto& player=K::System::RaceManager::Instance()->player();replayPass&=player.raceTimer()==ghost.raceTimer();
            for(int i=0;i<3;i++)replayPass&=player.getLapSplit(i+1)==ghost.lapTimer(i);
            replayHeap[finishes]=scenes.currentScene()->heap()->getAllocatableSize();finishes++;
            LOGF("rendered replay finished; drift blue_frames=%u orange_frames=%u particles=%u boost_fov_peak=%.3f trick_events=%u",driftEffects.blueFrames,driftEffects.orangeFrames,driftEffects.emitted,maxFov,trickCount);
            if(finishes==2){replayPass&=replayHeap[0]==replayHeap[1];if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/replay-result.json","w")){fprintf(f,"{\"pass\":%s,\"finishes\":%u,\"heap_free\":[%u,%u],\"physics_steps\":%u,\"mean_step_us\":%.3f,\"max_step_us\":%.3f,\"track_parts\":%u,\"vehicle_loaded\":%s}\n",replayPass?"true":"false",finishes,replayHeap[0],replayHeap[1],physicsSteps,stepTotal/physicsSteps,stepMax,unsigned(track.size()),vehicle?"true":"false");fclose(f);}break;}
            renderer.waitForGpu();scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);courseObjects.invalidate();accumulator=0;driftEffects.reset();raceEffects.reset();resetKartFx();cameraShake.reset();shakeActionStarts=~0u;lightningFlash={};boostFov=0;driverController.reset();driverAnimation.resetLook();driverPose={};vehicleAnimation.reset();wasTricking=false;wasBoosting=false;trickModel.reset();restarts++;
        }
        if(smoke&&frames==120){
            report("SMOKE COMPLETE 120 rendered frames");
            if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/smoke-result.json","w")){fprintf(f,"{\"frames\":%u,\"data_ready\":%s,\"linear_free\":%lu,\"physics_peak_ms\":%.3f}\n",frames,dataReady?"true":"false",(unsigned long)linearSpaceFree(),maxPhysics);fclose(f);}break;
        }
    }
    finishAutoplay();
    mkw::jobWorker().stop();
    flushRaceSave();
    renderer.waitForGpu();raceGrid.ai().clearBattleSearch();coinBattle.clear();balloonRuntime.clear();bottomScreen.unload(renderer);opponents.reset();raceHud.reset();lakitu.unload();flowUi.unload();driverAnimation.reset();sparkTexture[0].reset();sparkTexture[1].reset();sparkTexture[2].reset();for(auto& texture:*effectTextures)texture.reset();bodyAnimation.reset();for(auto& a:accessoryAnimations)a.reset();for(auto& a:accessories)a.reset();for(auto& w:wheels)w.reset();vehicle.reset();driver.reset();playerMiiHead.reset();playerLuma.reset();vehicleShadow.reset();sky.clear();track.clear();courseObjects.reset();menuModels.reset(renderer);menuIcons.reset(renderer);menuRenderer.release();mkw::SoundSystem::instance().stop();mkw::MenuMovieAudio::shutdown();mkw::shutdownLoadingScreen();renderer.shutdown();gfxExit();return 0;
}
