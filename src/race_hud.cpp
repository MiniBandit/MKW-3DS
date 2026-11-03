#include <mkw/race_chase_warning.hpp>
#include <mkw/render_quality.hpp>
#include <mkw/hud_feedback.hpp>
#include <mkw/race_hud.hpp>
#include <mkw/battle_hud_assets.hpp>
#include <mkw/battle_hud_motion.hpp>
#include <mkw/coin_hud_camera.hpp>
#include <mkw/object_bank.hpp>
#include <mkw/render_metrics.hpp>
#include <mkw/race_items.hpp>
#include <mkw/item_box.hpp>
#include <mkw/item_box_uv.hpp>
#include <mkw/bomb_blast.hpp>
#include <mkw/blast_renderer.hpp>
#include <mkw/race_balloon.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/blooper.hpp>
#include <mkw/thunder_cloud.hpp>
#include <mkw/item_rules.hpp>
#include <mkw/item_visual.hpp>
#include <game/kart/KartMove.hh>
#include <game/kart/KartState.hh>
#include <mkw/race_assets.hpp>
#include <mkw/renderer.hpp>
#include <mkw/animation.hpp>
#include <mkw/pose_compaction.hpp>
#include <game/kart/KartObjectManager.hh>
#include <game/system/RaceConfig.hh>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace mkw {
namespace {
// Retail itembox / itemBoxNise materials (layout generated from the BRRES).
struct BoxMaterial { Renderer* renderer; const Model* model; const ItemBoxLayout* layout;
    const std::array<std::array<float,6>,300>* inner; C3D_Mtx transform; unsigned frame; bool sparkle; float environmentBlend; };
constexpr float ShellWarningDistance=6000.f;
float boxEnvironmentBlend(Vec3 position,Vec3 viewer){
    const Vec3 delta=position-viewer;
    // Full retail EnvCamera inside the warning radius, fixed baked UVs far
    // away. Smoothstep keeps both endpoints continuous without extra passes.
    const float t=std::clamp((7800.f*7800.f-dot(delta,delta))/(7800.f*7800.f-ShellWarningDistance*ShellWarningDistance),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
bool fullBoxDetail(Vec3 position,Vec3 viewer){return boxEnvironmentBlend(position,viewer)>0.f;}
constexpr float IdentitySRT[6]={1,0,0,0,1,0};
uint32_t abgr(const unsigned char (&c)[3]){return 0xff000000u|c[0]|uint32_t(c[1])<<8|uint32_t(c[2])<<16;}
void bindBox(void* context,std::size_t submesh){
    auto& m=*static_cast<BoxMaterial*>(context);const auto& L=*m.layout;
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
    auto environment=[&](const float (&srt)[6]){
        if(m.model->staticLightingBaked&&m.environmentBlend<=0.f){m.renderer->clearEnvironmentUv();m.renderer->setUv1Transform(srt);}
        else m.renderer->setEnvironmentUv(m.transform,srt,false,m.environmentBlend);
    };
    auto texture=[&](int index,GPU_TEXTURE_WRAP_PARAM wrap){auto* t=&m.model->textures[index]->gpu;C3D_TexSetWrap(t,wrap,wrap);return t;};
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_TexEnvColor(e0,0xffffffff);C3D_TexEnvSrc(e0,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);
    if(submesh==L.inner){
        // mat_boxin: weight*TEXC, environment mapped; GX cull front shows the far inner walls.
        float srt[6];const auto& f=(*m.inner)[m.frame%m.inner->size()];for(unsigned i=0;i<6;++i)srt[i]=f[i];
        environment(srt);
        C3D_TexBind(1,texture(L.innerTexture,GPU_REPEAT));
        C3D_TexEnvColor(e0,0xff000000u|L.innerWeight*0x010101u);
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
        m.renderer->setMaterialCullNow(GPU_CULL_BACK_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    }else if(submesh==L.question){
        // mat_question: lerp(low, high, TEXC), alpha TEXA tested >= reference.
        C3D_TexBind(0,texture(L.questionTexture,GPU_CLAMP_TO_EDGE));
        C3D_TexEnvColor(e0,abgr(L.questionLow));
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
        C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0);
        C3D_TexEnvColor(e1,abgr(L.questionHigh));
        C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
        C3D_AlphaTest(true,GPU_GEQUAL,L.questionAlpha);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
        if(m.layout==&ItemBoxMaterials){const auto& uv=ItemBoxQuestionUV[m.frame%ItemBoxQuestionUV.size()];m.renderer->setUvOffset(uv[2],uv[5]);}
    }else if(submesh==L.outer){
        // mat_boxout: additive, no depth write, GX cull back.
        m.renderer->setMaterialCullNow(GPU_CULL_FRONT_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
        if(!m.sparkle){
            environment(IdentitySRT);
            C3D_TexBind(1,texture(L.shell,GPU_CLAMP_TO_EDGE));
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
        }else{
            // GX stage 1 has SUBHALF before SCALE_2. Omitting that bias
            // lights up most of the mask instead of only its narrow peaks.
            // Clamping the first subtraction is safe: negative values cannot
            // survive the following subtraction of 1/2.
            environment(L.noiseSRT);
            C3D_TexBind(0,texture(L.mask,GPU_REPEAT));C3D_TexBind(1,texture(L.noise,GPU_REPEAT));
            C3D_TexEnvColor(e0,0xffbfbfbf);
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
            C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0);
            C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR);
            C3D_TexEnvFunc(e1,C3D_RGB,GPU_SUBTRACT);
            auto* e2=C3D_GetTexEnv(2);
            C3D_TexEnvColor(e2,0xff808080);
            C3D_TexEnvSrc(e2,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);
            C3D_TexEnvFunc(e2,C3D_RGB,GPU_SUBTRACT);C3D_TexEnvScale(e2,C3D_RGB,GPU_TEVSCALE_2);
        }
    }
}
bool outerOnly(void* context,std::size_t submesh){return submesh==static_cast<BoxMaterial*>(context)->layout->outer;}
bool innerOnly(void* context,std::size_t submesh){return submesh==static_cast<BoxMaterial*>(context)->layout->inner;}
bool questionOnly(void* context,std::size_t submesh){return submesh==static_cast<BoxMaterial*>(context)->layout->question;}
C3D_Mtx questionBillboard(const Renderer& renderer,const C3D_Mtx& model){
    // Both original BRRES files mark hatenaPlane as NW4R ROT, not a rigid
    // child of the spinning cube. Keep its roll, scale and center, but face
    // the camera plane so its reverse side never mirrors the question mark.
    C3D_Mtx result=renderer.cameraFacing({model.r[0].w,model.r[1].w,model.r[2].w},1.f);
    const Vec3 right{result.r[0].x,result.r[1].x,result.r[2].x};
    const Vec3 up{result.r[0].y,result.r[1].y,result.r[2].y};
    const Vec3 back{result.r[0].z,result.r[1].z,result.r[2].z};
    const Vec3 modelUp{model.r[0].y,model.r[1].y,model.r[2].y};
    float rollX=dot(modelUp,right),rollY=dot(modelUp,up);
    const float length=std::hypot(rollX,rollY);
    if(length>1e-6f){rollX/=length;rollY/=length;}else{rollX=0;rollY=1;}
    const float sx=std::sqrt(model.r[0].x*model.r[0].x+model.r[1].x*model.r[1].x+model.r[2].x*model.r[2].x);
    const float sy=std::sqrt(dot(modelUp,modelUp));
    const float sz=std::sqrt(model.r[0].z*model.r[0].z+model.r[1].z*model.r[1].z+model.r[2].z*model.r[2].z);
    const Vec3 x=(right*rollY-up*rollX)*sx,y=(right*rollX+up*rollY)*sy,z=back*sz;
    result.r[0].x=x.x;result.r[1].x=x.y;result.r[2].x=x.z;
    result.r[0].y=y.x;result.r[1].y=y.y;result.r[2].y=y.z;
    result.r[0].z=z.x;result.r[1].z=z.y;result.r[2].z=z.z;
    return result;
}
void bakeBoxEnvironment(Model& model,const ItemBoxLayout& layout){
    if(!model.staticLightingBaked)return;
    for(unsigned shape:{unsigned(layout.inner),unsigned(layout.outer)}){
        const auto& sub=model.submeshes[shape];
        for(unsigned j=sub.first;j<sub.first+sub.count;++j){auto& v=model.vertices[model.indices[j]];const auto n=normalized(v.normal);
            v.uv1[0]=.5f-.5f*n.x;v.uv1[1]=.5f+.5f*(.948683298f*n.y+.316227766f*n.z);
        }
    }
    flushGpuData(model.vertices,model.vertexCount*sizeof(Vertex));
}
void drawBox(Renderer& renderer,const Model& model,const ItemBoxLayout& layout,const std::array<std::array<float,6>,300>& inner,const C3D_Mtx& t,unsigned frame,bool sparkle=true,float environmentBlend=1.f){
    BoxMaterial m{&renderer,&model,&layout,&inner,t,frame,false,environmentBlend};
    renderer.draw(model,t,false,bindBox,m,innerOnly);
    renderer.clearEnvironmentUv();
    const C3D_Mtx question=questionBillboard(renderer,t);
    renderer.draw(model,question,false,bindBox,m,questionOnly);
    renderer.setUvOffset(0,0);
    renderer.draw(model,t,false,bindBox,m,outerOnly);
    // Reduced detail drops the additive sparkle pass of distant boxes.
    if(sparkle){m.sparkle=true;renderer.draw(model,t,false,bindBox,m,outerOnly);}
    renderer.clearEnvironmentUv();renderer.setUvOffset(0,0);
}
// The fake box mask samples TEX0 through a static SRT; bake it into UV0.
void bakeMaskUv(Model& model,const ItemBoxLayout& L){
    if(L.outer>=model.submeshes.size())return;
    const float* r=L.maskSRT;const auto& s=model.submeshes[L.outer];std::vector<bool> done(model.vertexCount);
    for(unsigned n=s.first;n<s.first+s.count;++n){const auto i=model.indices[n];if(done[i])continue;done[i]=true;
        auto& v=model.vertices[i];const float u=v.uv[0],w=v.uv[1];
        v.uv[0]=r[0]*u-r[1]*w+r[1]+r[2];v.uv[1]=-r[3]*u+r[4]*w+1-r[4]-r[5];}
    flushGpuData(model.vertices,model.vertexCount*sizeof(Vertex));
}
// Retail ItemObjFakeBox draw (PAL 0x807A7FEC): 0.4x object scale, 2 deg/frame about X and Z.
void fakeBoxTransform(C3D_Mtx& t,Vec3 p,Vec3 scale,unsigned counter){
    const float a=float(counter%180)*2.f*float(M_PI)/180.f;
    Mtx_Identity(&t);Mtx_Translate(&t,p.x,p.y,p.z,true);Mtx_RotateZ(&t,a,true);Mtx_RotateX(&t,a,true);
    Mtx_Scale(&t,.4f*scale.x,.4f*scale.y,.4f*scale.z);
}
}
struct RaceHud::Impl {
    std::array<std::unique_ptr<Model>,21> models,staticModels;
    std::array<std::unique_ptr<Model>,2> itemShadows;
    std::array<Vec3,12> bulletUp{};
    std::array<bool,12> wasBullet{};
    std::array<ThunderCloudVisual,12> clouds{};
    ObjectBank cloudMaterials;int cloudColor=-1;std::vector<SubmeshOverride> cloudOverrides;
    std::array<uint16_t,12> previousCloud{};
    std::array<std::unique_ptr<DriverAnimation>,21> animations;
    std::unique_ptr<Model> box;
    std::unique_ptr<Model> bombCore;
    bool blastMaterials=false; // raw-UV models/bomb_blast.gmdl with the retail binder
    Vertex* bombFrames=nullptr;
    unsigned bombFrameCount=0;
    ~Impl(){if(bombFrames)linearFree(bombFrames);}
    struct BoxUV { uint16_t index;float u,v; };
    std::vector<BoxUV> boxUV;
    unsigned boxUVTick=~0u;
    Vec3 blooperEndOffset{};unsigned blooperEndAge=~0u;
    std::array<Texture,21> icons;
    std::array<Texture,13> ranks;
    std::array<Texture,10> digits;
    std::array<Texture,HudExtraCount> extras;
    BattleHudView battle;BattleHudMotion battleMotion;
    std::array<std::array<Texture,2>,2> battleSigns;
    std::array<std::array<Texture,10>,2> battleDigits,battlePersonal;
    Texture battleDash;
    std::unique_ptr<Model> coinStack;
    ObjectBank coinStackBank;
    std::array<std::vector<SubmeshOverride>,10> coinStackMaterials;
    // Player 0's CtrlRaceItemWindow (USA 0x807e4c54) inside CtrlRaceBase's
    // show/hide slide (0x807e2b34): state 1 hidden, 2 showing, 3 shown, 0 hiding.
    struct ItemWindow {
        int state=1;float slide=-1,phase=0;
        unsigned pop=9,popFrame=9,picture=~0u;
        bool roulette=false;
        ItemKind curr=ItemKind::None,next=ItemKind::None;
        ItemKind kind=ItemKind::None;uint8_t charges=0; // last picture, kept while sliding out
        uint16_t mushroomUses=0;
    } window;
    // Lap and position counters (CtrlRaceLap / CtrlRaceRankNum): the same
    // slide, shown once HasStarted (USA 0x807E2B18: the Raceinfo frame counter
    // runs, i.e. from the countdown after the start pan).
    struct BaseSlide {int state=1;float slide=-1;} base;
    // CtrlRaceRankNum (USA 0x807ea3e0): eRankTransition eClose -> eOpen -> eWait;
    // the digit changes on eOpen's first frame.
    struct RankSpin { int anim=2;unsigned frame=0,shown=0,lastTick=0; } rankSpin;
    // CtrlRaceNameBalloon x3 (USA 0x807e60c0) and its candidate list (0x807e6d04).
    struct Balloon { int racer=-1,shown=-1;float alpha=0; };
    std::array<Balloon,3> balloons{};
    unsigned balloonTick=0;
    std::array<Texture,25> names;
    Texture balloonLine;
    int warning=-1;unsigned warningAge=0;float warningAlpha=0;
    Vec3 warningPosition{};ChaseWarningMotion warningMotion;
    bool warningAudible=false;float warningPan=0,warningRate=0;
};
RaceHud::RaceHud():impl_(std::make_unique<Impl>()){}
RaceHud::~RaceHud()=default;
void RaceHud::reset(){speedometer=false;impl_=std::make_unique<Impl>();}
void RaceHud::resetBattleView(){impl_->battleMotion.reset();impl_->battle.points=impl_->battle.red=impl_->battle.blue=0;}
void RaceHud::setBattleView(BattleHudView view,unsigned tick){impl_->battle=view;
    if(view.enabled)impl_->battleMotion.step(view.points,view.red,view.blue,tick);}
uint8_t RaceHud::takeBattleCues(){return impl_->battleMotion.takeCues();}
bool RaceHud::load(){
    reset();bool valid=true;
    const auto& scenario=Kinoko::System::RaceConfig::Instance()->raceScenario();
    const bool battle=scenario.gameMode==Kinoko::System::RaceConfig::Scenario::GameMode::Battle;
    auto model=[](const char* path,bool dynamic=false){auto m=std::make_unique<Model>();if(!path||!m->load(path,dynamic))m.reset();return m;};
    impl_->box=model(ItemBoxModel);
    valid&=bool(impl_->box);
    if(impl_->box)bakeBoxEnvironment(*impl_->box,ItemBoxMaterials);
    impl_->itemShadows[0]=model("models/item_shadow_shell.gmdl");
    impl_->itemShadows[1]=model("models/item_shadow_banana.gmdl");
    for(const auto& shadow:impl_->itemShadows)valid&=bool(shadow);
    impl_->bombCore=model("models/bomb_blast.gmdl");impl_->blastMaterials=bool(impl_->bombCore);
    valid&=bool(impl_->bombCore);
    if(impl_->bombCore){
        auto animation=std::make_unique<DriverAnimation>();
        if(animation->load("models/bomb_core.dskm",*impl_->bombCore)){
            // GPU commands consume vertex pointers after submission. Immutable
            // poses let concurrent explosions select different ages safely.
            impl_->bombFrameCount=std::min(86u,unsigned(animation->duration(0))+1);
            const size_t bytes=impl_->bombCore->vertexCount*sizeof(Vertex);
            impl_->bombFrames=static_cast<Vertex*>(linearAlloc(bytes*impl_->bombFrameCount));
            if(impl_->bombFrames){
                for(unsigned frame=0;frame<impl_->bombFrameCount;++frame){
                    animation->apply(*impl_->bombCore,0,float(frame),frame);
                    std::memcpy(impl_->bombFrames+frame*impl_->bombCore->vertexCount,impl_->bombCore->vertices,bytes);
                }
                // Losslessly weld vertices only when all pre-sampled ages
                // agree byte-for-byte. Independent explosions still use their
                // own immutable pose; no animation age or triangle is removed.
                auto& model=*impl_->bombCore;
                const auto remap=compactPoses(impl_->bombFrames,model.vertexCount,impl_->bombFrameCount);
                const unsigned unique=*std::max_element(remap.begin(),remap.end())+1;
                const size_t compactBytes=size_t(unique)*sizeof(Vertex)*impl_->bombFrameCount;
                if(auto* compact=static_cast<Vertex*>(linearAlloc(compactBytes))){
                    std::memcpy(compact,impl_->bombFrames,compactBytes);
                    linearFree(impl_->bombFrames);impl_->bombFrames=compact;
                } // Allocation failure retains the already compacted larger buffer.
                for(unsigned i=0;i<model.indexCount;++i)model.indices[i]=remap[model.indices[i]];
                if(model.staticEnvironment2){
                    std::vector<float> remapped(size_t(unique)*2);
                    for(unsigned i=0;i<model.vertexCount;++i){remapped[2*remap[i]]=model.staticEnvironment2[2*i];remapped[2*remap[i]+1]=model.staticEnvironment2[2*i+1];}
                    std::memcpy(model.staticEnvironment2,remapped.data(),remapped.size()*sizeof(float));
                    flushGpuData(model.staticEnvironment2,remapped.size()*sizeof(float));
                }
                model.vertexCount=unique;
                flushGpuData(model.indices,model.indexCount*sizeof(uint16_t));
                flushGpuData(impl_->bombFrames,compactBytes);
            }else valid=false;
        }else valid=false;
    }
    for(unsigned i=0;i<21;++i){
        const char* itemPath=i==12?"models/item_12.gmdl":ItemModels[i];
        if(itemPath){impl_->models[i]=model(itemPath,true);impl_->staticModels[i]=model(itemPath);valid&=bool(impl_->models[i])&&bool(impl_->staticModels[i]);}
        if(HudItems[i].path)valid&=impl_->icons[i].load(HudItems[i].path);
        if((i==6||i==7||i==12||i==13||i==14||i==15)&&impl_->models[i]){
            char path[96];std::snprintf(path,sizeof path,"models/item_%u.dskm",i);
            // Unlit and never environment-mapped, these meshes do not read
            // normals: skin positions only (the same position arithmetic).
            auto animation=std::make_unique<DriverAnimation>();
            if(animation->load(path,*impl_->models[i])){animation->skinNormals(true);impl_->animations[i]=std::move(animation);}
            else valid=false;
        }
    }
    if(impl_->models[14]&&impl_->animations[14]){
        valid&=impl_->cloudMaterials.load("models/item_14.dobj");
        impl_->cloudColor=impl_->cloudMaterials.color("kumo");valid&=impl_->cloudColor>=0;
        impl_->cloudOverrides.resize(impl_->models[14]->submeshes.size());
        auto& bank=*impl_->animations[14];auto& cloud=*impl_->models[14];
        valid&=bank.prepareLitSkinning(cloud);
        // The local carrier keeps live lighting; other carriers use the immutable copy.
        // Both copies must use the same GPU palette/vertex coordinate convention.
        auto& fixed=*impl_->staticModels[14];
        auto fixedBank=std::make_unique<DriverAnimation>();
        valid&=fixedBank->load("models/item_14.dskm",fixed)&&fixedBank->prepareLitSkinning(fixed);
        // Unlit GPU indices are kept in normal.x; the shared CHR0 pose has identical slots.
        if(bank.gpuSkinned())bank.shareDrawVertices(cloud);
    }
    if(impl_->models[3])bakeMaskUv(*impl_->models[3],FakeBoxMaterials);
    if(impl_->staticModels[3]){bakeMaskUv(*impl_->staticModels[3],FakeBoxMaterials);bakeBoxEnvironment(*impl_->staticModels[3],FakeBoxMaterials);}
    if(!battle)for(unsigned i=1;i<=12;++i)valid&=impl_->ranks[i].load(HudRanks[i].path);
    for(unsigned i=0;i<10;++i)valid&=impl_->digits[i].load(HudDigits[i].path);
    for(unsigned i=0;i<HudExtraCount;++i)valid&=impl_->extras[i].load(HudExtras[i].path);
    for(unsigned i=0;i<BalloonNames.size();++i)valid&=impl_->names[i].load(BalloonNames[i].path);
    valid&=impl_->balloonLine.load(BalloonLinePath);
    if(battle){
        impl_->battle.enabled=true;
        for(unsigned team=0;team<2;++team)for(unsigned d=0;d<10;++d){
            valid&=impl_->battleDigits[team][d].load(BattleHudDigits[team][d].path);
            valid&=impl_->battlePersonal[team][d].load(BattleHudPersonalDigits[team][d].path);
        }
        for(unsigned team=0;team<2;++team)for(unsigned sign=0;sign<2;++sign)valid&=impl_->battleSigns[team][sign].load(BattleSignArt[team][sign].path);
        valid&=impl_->battleDash.load(BattleHudDash.path);
        if(scenario.battleType==1){
            impl_->coinStack=model("models/battle_hud_coin_stack.gmdl");
            valid&=bool(impl_->coinStack)&&impl_->coinStackBank.load("models/battle_hud_coin_stack.dobj");
            const int srt=impl_->coinStackBank.scroll("pile_coin");
            valid&=srt>=0;
            if(impl_->coinStack&&srt>=0){
                for(unsigned count=0;count<10;++count){auto& overrides=impl_->coinStackMaterials[count];
                    overrides.resize(impl_->coinStack->submeshes.size());
                    valid&=impl_->coinStackBank.material(-1,0,srt,float(count),-1,0,overrides.data(),unsigned(overrides.size()));
                }
            }
        }
    }
    return valid;
}
void RaceHud::stepWorld(const RaceItems& items,Vec3 cameraBackward,bool started){
    {
        auto& m=*impl_;int kind=-1;float nearest=ShellWarningDistance;Vec3 position{};
        const auto& p=Kinoko::Kart::KartObjectManager::Instance()->object(0)->pos();
        if(started)for(const auto& a:items.actors()){
            if(!a.active||a.target!=0||a.explosion||a.scattered||
               (a.kind!=ItemKind::RedShell&&a.kind!=ItemKind::BlueShell))continue;
            const float distance=length(a.visualPosition()-Vec3{p.x,p.y,p.z});
            if(distance<=nearest){nearest=distance;kind=int(a.kind);position=a.visualPosition();}
        }
        // CtrlRaceChase 807e7260..7324 and807e8f18..9048 also warn of
        // powered racers behind us: Star, Bullet, Mega, then Thunder Cloud.
        // Keep targeted shells above this fallback, and omit team-mates.
        if(started&&kind<0){
            const auto& scenario=Kinoko::System::RaceConfig::Instance()->raceScenario();
            const auto& direction=Kinoko::Kart::KartObjectManager::Instance()->object(0)->move()->dir();
            float distanceLimit=10000000.f; // squared, original808a45a8
            for(unsigned i=1;i<scenario.playerCount;++i){
                if(scenario.teams&&scenario.players[i].team==scenario.players[0].team)continue;
                const auto& h=items.hud(i);
                const int threat=h.star?9:h.bullet?15:h.mega?11:h.cloud?14:-1;
                if(threat<0)continue;
                auto* kart=Kinoko::Kart::KartObjectManager::Instance()->object(i);
                using St=Kinoko::Kart::eStatus;
                if(kart->state()->status().onBit( St::BeforeRespawn)||kart->state()->status().onBit(St::InCannon))continue;
                const auto& q=kart->pos();const Vec3 delta{q.x-p.x,q.y-p.y,q.z-p.z};
                if(dot(delta,{direction.x,direction.y,direction.z})>=0)continue;
                const float d=dot(delta,delta);
                if(d<distanceLimit){distanceLimit=d;kind=threat;position={q.x,q.y,q.z};}
            }
        }
        if(kind<0||kind!=m.warning)m.warningAudible=false;
        if(kind>=0){
            if(m.warning<0){m.warningAge=0;m.warningMotion={};}
            m.warningPosition=position;
            m.warning=kind;m.warningAlpha=ChaseAlpha[std::min(m.warningAge,5u)]/255.f;++m.warningAge;
        }else{m.warningAlpha=0;m.warning=-1;m.warningAge=0;}
    }
    for(unsigned i=0;i<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++i){
        const auto& effect=items.hud(i);
        auto* kart=Kinoko::Kart::KartObjectManager::Instance()->object(i);
        if(effect.bullet){
            const auto& sourceUp=kart->move()->smoothedUp();const Vec3 up{sourceUp.x,sourceUp.y,sourceUp.z};
            impl_->bulletUp[i]=impl_->wasBullet[i]?normalized(lerp(impl_->bulletUp[i],up,BulletUpSmoothing)):up;
        }
        impl_->wasBullet[i]=bool(effect.bullet);
        if(effect.cloud){
            auto& cloud=impl_->clouds[i];const auto& p=kart->pos();const auto& d=kart->move()->dir();
            const Vec3 position{p.x,p.y,p.z};const Vec3 direction=i==0?cameraBackward:Vec3{-d.x,-d.y,-d.z};
            if(!impl_->previousCloud[i]){
                // A passed cloud keeps flying from where it was (ItemObjKumo 0x807d8b5c).
                if(effect.cloudFrom<impl_->clouds.size()){cloud=impl_->clouds[effect.cloudFrom];cloud.transfer();}
                else cloud.spawn(position,direction);
            }
            cloud.step(position,direction,CloudDuration-std::min<unsigned>(CloudDuration,effect.cloud),
                kart->move()->hopPosY(),kart->state()->status().onBit(Kinoko::Kart::eStatus::BeforeRespawn));
        }
    }
    for(unsigned i=0;i<impl_->previousCloud.size();++i)impl_->previousCloud[i]=items.hud(i).cloud;
    auto& w=impl_->window;const auto& held=items.hud(0);
    // CtrlRaceItemWindow_calcSelf (0x807e4c54): the inventory item (+0x8c) with its
    // count as the picture variant, else the slot lock (+0x78) with variant 1. The
    // picture (fn_1_35073C 0x8083bea4) varies by count only for Triple Mushrooms
    // (kinoko_3 / kinoko_2 / kinoko). Deploying a held group (single or triple
    // shells and bananas) empties the inventory (ItemHolderPlayerb4_use 0x80788904),
    // so the window shows nothing for it and slides out.
    const ItemWindowPicture shown=itemWindowPicture(held);const ItemKind displayed=shown.kind;
    // The pop counter is consumed before a picture change can restart it.
    w.popFrame=w.pop;if(w.pop<9)++w.pop;
    // Every Golden Mushroom use restarts the pop (+0x238, set by activateMushroom).
    const uint16_t uses=items.mushroomUses(0);
    if(held.item==ItemKind::GoldenMushroom&&uses!=w.mushroomUses)w.pop=0;
    w.mushroomUses=uses;
    if(held.roulette){
        // Faces: next = the newest picture, curr = the one it replaced.
        if(!w.roulette){w.curr=ItemKind::None;w.next=held.rouletteIcon;}
        else if(held.rouletteIcon!=w.next){w.curr=w.next;w.next=held.rouletteIcon;}
        w.phase=items.roulette(0).phase;w.picture=~0u;
    }else if(displayed!=ItemKind::None){
        const uint8_t charges=shown.variant;
        const unsigned picture=unsigned(displayed)|(unsigned(charges)<<9);
        if(picture!=w.picture){w.picture=picture;w.pop=0;}
        w.kind=displayed;w.charges=charges;
    }else w.picture=~0u;
    w.roulette=held.roulette;
    // shouldHide (0x807e4afc): no roulette, no inventory item and no slot lock.
    if(!held.roulette&&displayed==ItemKind::None){if(w.state==3)w.state=0;}
    else if(w.state!=3)w.state=2;
    auto advance=[](int& state,float& slide){
        switch(state){
        case 1:slide=-1;break;
        case 3:slide=0;break;
        case 0:slide=std::fabs(slide)+.05f;if(slide>=1){slide=-1;state=1;}break;
        default:slide=-std::fabs(slide)+.05f;if(slide>=0){slide=0;state=3;}break;
        }
    };
    advance(w.state,w.slide);
    auto& b=impl_->base;
    if(started&&b.state!=3)b.state=2;
    advance(b.state,b.slide);
}
void RaceHud::drawWorld(Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick){
    std::array<bool,21> needed{};
    for(const auto& actor:items.actors())if(actor.active&&unsigned(actor.kind)<needed.size()&&
        !(actor.kind==ItemKind::Bobomb&&actor.explosion)&&dot(actor.position-viewer,actor.position-viewer)<22000.f*22000.f)
        needed[unsigned(actor.kind)]=true;
    for(unsigned player=0;player<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++player){
        const auto& effect=items.hud(player);needed[15]|=bool(effect.bullet);needed[14]|=bool(effect.cloud);
        if(effect.deployed&&unsigned(effect.heldKind)<needed.size())needed[unsigned(effect.heldKind)]=true;
    }
    auto* localKart=Kinoko::Kart::KartObjectManager::Instance()->object(0);
    const auto blooper=items.blooperVisual(0,unsigned(localKart->param()->stats().weightClass));
    needed[12]=blooper.clip!=nullptr;
    needed[13]=items.hud(0).pow&&items.hud(0).pow<=158;
    for(unsigned i=0;i<21;++i)if(i!=14&&needed[i]&&impl_->animations[i]){
        if(i==12){
            auto& animation=*impl_->animations[i];const int clip=animation.find(blooper.clip);
            if(clip>=0){
                animation.clearPoseTransition();
                if(!std::strcmp(blooper.clip,"attack_end")&&(impl_->blooperEndAge==~0u||blooper.frame<impl_->blooperEndAge)){
                    const int attack=animation.find("attack");
                    if(attack>=0){
                        const auto* old=animation.pose(attack,float(items.blooperEndAttackFrame(0)),tick);
                        const Vec3 oldRoot{old[3],old[7],old[11]};
                        const auto* next=animation.pose(clip,0,tick);
                        impl_->blooperEndOffset=oldRoot-Vec3{next[3],next[7],next[11]};
                    }
                }
                impl_->blooperEndAge=!std::strcmp(blooper.clip,"attack_end")?blooper.frame:~0u;
                animation.apply(*impl_->models[i],clip,float(blooper.frame),tick);
                animation.apply(*impl_->staticModels[i],clip,float(blooper.frame),tick);
            }
            continue;
        }
        if(i==13){
            const unsigned left=items.hud(0).pow;if(!left||left>158)continue;
            const char* name=left>110?"attack1":left>60?"attack2":"attack3";
            const unsigned start=left>110?158:left>60?110:60;
            const int clip=impl_->animations[i]->find(name);
            if(clip>=0){impl_->animations[i]->apply(*impl_->models[i],clip,float(start-left),tick);impl_->animations[i]->apply(*impl_->staticModels[i],clip,float(start-left),tick);}
            continue;
        }
        int clip=impl_->animations[i]->find(i==6?"wait_a":"wait");
        // Reduced quality: one pose per animationStep ticks (apply reuses it).
        const unsigned step=renderQuality().animationStep,poseTick=step?tick/step*step:tick;
        impl_->animations[i]->apply(*impl_->models[i],clip<0?0:clip,float(poseTick),poseTick);
        impl_->animations[i]->apply(*impl_->staticModels[i],clip<0?0:clip,float(poseTick),poseTick);
    }
    const auto& quality=renderQuality();const Vec3 camera=renderer.cameraPosition();
    // Both meshes are the original closed shadow volumes. Gather their
    // transforms once and resolve overlapping items in one stencil pass,
    // before drawing the item models, so they cannot shade themselves.
    std::array<Renderer::ShadowVolume,RaceItems::MaxActors+RaceItems::MaxRacers*3> shadows{};
    unsigned shadowCount=0;
    auto shadow=[&](ItemKind kind,Vec3 position,const HeldItemVisual& pose){
        const float factor=itemShadowScale(kind);if(!factor)return;
        const auto& model=impl_->itemShadows[kind==ItemKind::Banana||kind==ItemKind::TripleBanana?1:0];
        if(!model)return;
        const Vec3 x=pose.right*(pose.scale.x*factor),y=pose.up*pose.scale.y,z=pose.forward*(pose.scale.z*factor);
        C3D_Mtx t;Mtx_Identity(&t);
        t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=position.x;
        t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=position.y;
        t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=position.z;
        if(renderer.visible(*model,t))shadows[shadowCount++]={model.get(),t};
    };
    for(const auto& actor:items.actors())if(actor.active&&!actor.explosion&&
        dot(actor.position-viewer,actor.position-viewer)<22000.f*22000.f)
        shadow(actor.kind,actor.visualPosition(),releasedItemVisual(actor));
    for(unsigned player=0;player<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++player){
        const auto& effect=items.hud(player);if(!effect.deployed)continue;
        for(unsigned slot=0;slot<3;++slot)if(effect.heldMask&(1u<<slot)){
            auto pose=items.heldItemVisual(player,slot);
            if(spinningShell(effect.heldKind)){
                const float spin=shellVisualSpin(items.frame());
                pose.forward=pose.forward*std::cos(spin)+pose.right*std::sin(spin);
                pose.right=cross(pose.up,pose.forward);
            }
            shadow(effect.heldKind,items.heldItemPosition(player,slot),pose);
        }
    }
    renderer.drawShadowVolumes(shadows.data(),shadowCount,.5f);
    bool boxesInRange=false;
    if(impl_->box)for(const auto& box:items.boxes())if(box.active&&dot(box.position-viewer,box.position-viewer)<20000.f*20000.f){
        const auto pose=itemBoxPose(box);
        C3D_Mtx t;Mtx_Identity(&t);Mtx_Translate(&t,pose.position.x,pose.position.y,pose.position.z,true);
        // EGG makeRT({angle,0,angle}) = Rz * Rx; model origin stays at its
        // authored hover center, including on ramps and airborne box rows.
        Mtx_RotateZ(&t,pose.angle,true);Mtx_RotateX(&t,pose.angle,true);
        Mtx_Scale(&t,pose.scale.x,pose.scale.y,pose.scale.z);
        boxesInRange=true;
        // The box mesh is never deformed: a box wholly outside the view
        // frustum produces no fragments, so skip its material passes.
        if(!renderer.visible(*impl_->box,t))continue;
        const bool full=fullBoxDetail(pose.position,viewer);
        const auto& model=*impl_->box;
        const float ratio=quality.reduced&&!full?RenderQuality::screenRatio({model.minimum,model.maximum},t,camera):1.f;
        if(ratio<quality.minScreenRatio)continue;
        drawBox(renderer,model,ItemBoxMaterials,ItemBoxInnerSRT,t,tick,full||ratio>=quality.boxSparkleRatio,boxEnvironmentBlend(pose.position,viewer));
    }
    // Leave the uniforms as the last drawBox would, whether or not any was drawn.
    if(boxesInRange){renderer.clearEnvironmentUv();renderer.setUvOffset(0,0);}
    for(const auto& actor:items.actors())if(actor.active&&unsigned(actor.kind)<21&&dot(actor.position-viewer,actor.position-viewer)<22000.f*22000.f){
        const bool blast=(actor.kind==ItemKind::Bobomb||actor.kind==ItemKind::BlueShell)&&actor.explosion;
        const bool fullFake=actor.kind==ItemKind::FakeBox&&fullBoxDetail(actor.visualPosition(),viewer);
        auto& model=blast?impl_->bombCore:impl_->staticModels[unsigned(actor.kind)];if(!model)continue;
        Vertex* originalVertices=model->vertices;
        if(blast&&impl_->bombFrames)model->vertices=impl_->bombFrames+std::min(unsigned(actor.age),impl_->bombFrameCount-1)*model->vertexCount;
        const Vec3 position=actor.visualPosition();
        C3D_Mtx t;
        if(actor.kind==ItemKind::FakeBox){
            fakeBoxTransform(t,position,{actor.scale,actor.scale,actor.scale},actor.age);
            if(fullFake||!quality.tooSmall({model->minimum,model->maximum},t,camera))drawBox(renderer,*model,FakeBoxMaterials,FakeBoxInnerSRT,t,tick,true,boxEnvironmentBlend(position,viewer));
            model->vertices=originalVertices;continue;
        }
        const auto visual=releasedItemVisual(actor);
        const auto x=visual.right*visual.scale.x,y=visual.up*visual.scale.y,z=visual.forward*visual.scale.z;
        Mtx_Identity(&t);
        t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=position.x;
        t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=position.y;
        t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=position.z;
        if(!quality.tooSmall({model->minimum,model->maximum},t,camera)){
            if(blast&&impl_->blastMaterials)blast::drawBlast(renderer,*model,t,actor.age,actor.kind==ItemKind::BlueShell);
            else renderer.draw(*model,t);
        }
        model->vertices=originalVertices;
    }
    if(blooper.clip&&impl_->models[12]&&impl_->animations[12]){
        // GessoScreen::UpdateModel anchors the model to the racer position:
        // start (0,150,-100), attack (0,180,-weightDistance), weight scale.
        // Its original CHR0 carries the entry, tentacles and squirt movement.
        // Retail attack_end blends out the difference from the interrupted
        // attack root over ten frames (807d5a9c: blend step .1).
        const Vec3 delta=!std::strcmp(blooper.clip,"attack_end")?
            impl_->blooperEndOffset*(blooper.scale*std::max(0.f,1.f-blooper.frame*.1f)):Vec3{};
        const auto& view=renderer.viewFrustum().view;
        auto* anchorKart=Kinoko::Kart::KartObjectManager::Instance()->object(blooper.player);
        const auto& direction=anchorKart->move()->dir();
        // HasCamera uses that camera's direction; CPUs use -Movement::dir.
        // The original orthonormalization retains world-up, not camera pitch.
        const Vec3 back=blooper.player==0?Vec3{view.r[2].x,view.r[2].y,view.r[2].z}:
                                                    Vec3{-direction.x,-direction.y,-direction.z};
        const Vec3 up{0,1,0},right=normalized(cross(up,back),{1,0,0}),z=cross(right,up);
        const auto& anchor=anchorKart->pos();
        const Vec3 position=Vec3{anchor.x,anchor.y,anchor.z}+up*blooper.up-z*blooper.forward+
                            right*delta.x+up*delta.y+z*delta.z;
        C3D_Mtx transform;Mtx_Identity(&transform);
        const Vec3 x=right*blooper.scale,y=up*blooper.scale,scaledZ=z*blooper.scale;
        transform.r[0].x=x.x;transform.r[0].y=y.x;transform.r[0].z=scaledZ.x;transform.r[0].w=position.x;
        transform.r[1].x=x.y;transform.r[1].y=y.y;transform.r[1].z=scaledZ.y;transform.r[1].w=position.y;
        transform.r[2].x=x.z;transform.r[2].y=y.z;transform.r[2].z=scaledZ.z;transform.r[2].w=position.z;
        renderer.draw(*impl_->staticModels[12],transform);
    }
    if(items.hud(0).pow&&items.hud(0).pow<=158&&impl_->models[13]){
        const auto& pose=Kinoko::Kart::KartObjectManager::Instance()->object(0)->pose();
        // Retail PowObj uses inverse-view up250, back-30 (=look forward30),
        // uniform1.3 scale. Its authored CHR0 performs the three block stomps.
        renderer.drawCameraFacing(*impl_->staticModels[13],{pose[0,3],pose[1,3],pose[2,3]},250.f,30.f,1.3f);
    }
    for(unsigned i=0;i<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++i){
        const auto& effect=items.hud(i);
        const bool orbit=effect.deployed;
        if(!effect.bullet&&!effect.cloud&&!orbit)continue;
        auto* kart=Kinoko::Kart::KartObjectManager::Instance()->object(i);const auto& source=kart->pose();
        C3D_Mtx body;Mtx_Identity(&body);for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)body.r[r].c[3-c]=source[r,c];
        if(effect.bullet&&impl_->models[15]){
            const auto& p=kart->pos();
            const Vec3 position=bulletVisualPosition({p.x,p.y,p.z},impl_->bulletUp[i],kart->bsp().initialYPos);
            auto t=body;t.r[0].w=position.x;t.r[1].w=position.y;t.r[2].w=position.z;
            renderer.draw(*(i==0?impl_->models[15]:impl_->staticModels[15]),t);
        }
        if(effect.cloud&&impl_->models[14]&&impl_->animations[14]){
            const auto& cloud=impl_->clouds[i];
            auto& animation=*impl_->animations[14];
            const int clip=animation.find("kumo");
            animation.apply(*impl_->models[14],clip<0?0:clip,float(cloud.animationFrame),tick);
            const auto z=cloud.facing,x=normalized(cross({0,1,0},z),{1,0,0}),y=cross(z,x),pCloud=cloud.modelPosition();
            C3D_Mtx t;Mtx_Identity(&t);
            t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=pCloud.x;
            t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=pCloud.y;
            t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=pCloud.z;
            Mtx_Scale(&t,ThunderCloudVisual::ModelScale,ThunderCloudVisual::ModelScale,ThunderCloudVisual::ModelScale);
            impl_->cloudMaterials.material(-1,0,-1,0,impl_->cloudColor,cloud.flashFrame,
                impl_->cloudOverrides.data(),unsigned(impl_->cloudOverrides.size()));
            renderer.setOverrides(impl_->cloudOverrides.data(),unsigned(impl_->cloudOverrides.size()));
            // The original eyes and bolt have separate local bones. Bind this
            // cloud's CHR0 palette, then clear it before the next world item.
            if(animation.gpuSkinned())renderer.setSkinPalette(animation.palette(),animation.matrixCount(),21);
            renderer.draw(*(i==0?impl_->models[14]:impl_->staticModels[14]),t);
            renderer.setSkinPalette(nullptr,0,12);renderer.setOverrides(nullptr,0);
        }
        unsigned orbitModel=effect.heldKind==ItemKind::TripleGreenShell?0:effect.heldKind==ItemKind::TripleRedShell?1:effect.heldKind==ItemKind::TripleBanana?2:unsigned(effect.heldKind);
        auto& heldModel=i==0?impl_->models[orbitModel]:impl_->staticModels[orbitModel];
        if(orbit&&heldModel)for(unsigned n=0;n<3;++n)if(effect.heldMask&(1u<<n)){
            const Vec3 position=items.heldItemPosition(i,n);
            auto visual=items.heldItemVisual(i,n);
            if(spinningShell(effect.heldKind)){
                // Triples already carry their own source spin in heldItemVisual.
                const float spin=shellVisualSpin(items.frame());
                visual.forward=visual.forward*std::cos(spin)+visual.right*std::sin(spin);
                visual.right=cross(visual.up,visual.forward);
            }
            if(orbitModel==unsigned(ItemKind::FakeBox)){
                C3D_Mtx t;fakeBoxTransform(t,position,visual.scale,tick);
                const auto& boxModel=i==0?impl_->models[3]:impl_->staticModels[3];
                drawBox(renderer,*boxModel,FakeBoxMaterials,FakeBoxInnerSRT,t,tick,true,i==0?1.f:boxEnvironmentBlend(position,viewer));continue;
            }
            const auto x=visual.right*visual.scale.x,y=visual.up*visual.scale.y,z=visual.forward*visual.scale.z;
            C3D_Mtx t;Mtx_Identity(&t);
            t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=position.x;
            t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=position.y;
            t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=position.z;
            renderer.draw(*heldModel,t);
        }
    }
}
bool RaceHud::notificationWarning(bool& lit)const{
    lit=impl_->warningMotion.lightFrame<10.f;
    return impl_->warning>=0&&impl_->warningAudible;
}
bool RaceHud::notificationCloudLit()const{return impl_->previousCloud[0]&&impl_->clouds[0].flashFrame>0.f;}
bool RaceHud::chaseWarningAudio(float& pan,float& rate)const{
    pan=impl_->warningPan;rate=impl_->warningRate;
    return impl_->warning>=0&&impl_->warningAudible;
}
void RaceHud::drawOverlay(Renderer& renderer,const RaceItems& items,unsigned stage,unsigned timer,unsigned rank,unsigned lap,bool finished,unsigned tick,int finishFrame,float exitSlide,bool lookBack){
    // The group2 coin models follow the world, ahead of the 2D controls.
    // Their position belongs to the fixed HUD camera, never the race camera.
    if(impl_->coinStack&&stage>=2&&!finished){
        CoinHudStack stack;stack.update(impl_->battle.points);
        renderer.setScreenDepth(renderer.stereoLayer(stereo::HudLayer));
        renderer.setSkinPalette(nullptr,0,12);renderer.setRigidPose(nullptr);
        for(unsigned i=0;i<stack.size();++i){const auto& c=stack.column(i);
            const auto transform=coinHudColumnTransform(c);
            const auto& materials=impl_->coinStackMaterials[c.srtFrame()];
            renderer.setOverrides(materials.data(),unsigned(materials.size()));
            renderer.drawCoinHudModel(*impl_->coinStack,transform,i==0);
        }
        renderer.setOverrides(nullptr,0);
    }
    impl_->warningAudible=false;
    if(stage>=2&&!finished&&impl_->warning>=0&&impl_->warningAlpha>0){
        float px=0,py=0,z=0;
        const bool projected=renderer.projectChaseTop(impl_->warningPosition,px,py,z,lookBack);
        auto& motion=impl_->warningMotion;
        px=chaseDrivingScreenX(px,400.f,lookBack);
        if(lookBack){motion.x=0;} // native rear-view flag centres the warning
        if(projected&&motion.advance(px-200.f,z,impl_->warningAge)){
        // CtrlRaceChase calls the non-targeted warning only within the
        // screen width; map its native +/-300 pan range to this LCD.
        impl_->warningAudible=std::fabs(px-200.f)<200.f;
        impl_->warningPan=std::clamp(motion.x*(608.f/400.f)*(.8f/300.f),-.8f,.8f);
        impl_->warningRate=std::clamp(-2000.f/z,.01f,2.f);
        // Native projection is adapted to this LCD; vertical chase anchor
        // remains at the original bottom HUD offset, while X follows item.
        const float scale=240.f/456.f,cx=200.f+motion.x,cy=120.f+180.f*scale;
        const float size=128.f*.8f*scale,icon=64.f*scale*motion.zoom();
        const auto& base=HudExtras[HudChaseBase];const auto& light=HudExtras[HudChaseLight];
        renderer.drawScreenSpriteRegion(impl_->extras[HudChaseBase],cx-size/2,cy-size/2,size,size,0,0,base.u,base.v,impl_->warningAlpha,0xffffb7ffu);
        // NW4R hilight C0=(255,0,0,0), C1=(255,0,0,255):
        // full red RGB with texture alpha, rather than RGB*tinted texture.
        const float angle=motion.lightAngle(),c=std::cos(angle),sn=std::sin(angle);
        float corners[4][2];
        for(unsigned i=0;i<4;++i){const float dx=(i&1?1:-1)*size*.5f,dy=(i&2?1:-1)*size*.5f;
            corners[i][0]=cx+dx*c-dy*sn;corners[i][1]=cy+dx*sn+dy*c;}
        const float uv[4][2]={{0,0},{light.u,0},{0,light.v},{light.u,light.v}};
        const uint32_t white[4]={0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};
        renderer.drawScreenMaterialQuad(&impl_->extras[HudChaseLight],corners,uv,white,
            0xff000000u,0xff0000ffu,impl_->warningAlpha);
        const auto& art=HudItems[unsigned(impl_->warning)];
        renderer.drawScreenSpriteRegion(impl_->icons[unsigned(impl_->warning)],cx-icon/2,cy-icon/2,icon,icon,0,0,art.u,art.v,impl_->warningAlpha);
        }
    }
    auto sprite=[&](const Texture& texture,const RaceSprite& art,HudRect rect,float alpha=1.f,uint32_t tint=0xffffffffu){
        renderer.drawScreenSprite(texture,rect.x,rect.y,rect.w,rect.h,art.u,art.v,alpha,tint);
    };
    // Stereo (Mario Kart 7's layers): the ink and the name balloons sit at
    // the depth they are anchored to; the HUD panes 4 px behind the screen;
    // the countdown, GO, FINISH and the position come out of it.
    const float hudDepth=renderer.stereoLayer(stereo::HudLayer);
    // Original single-player BRCTR corner anchors and BRLYT pane sizes. 3DS is
    // 5:3: retain uniform glyph scaling and each corner's source safe margin.
    // Name balloons. Retail anchors kart pos + (0,120,0) (static init 0x807ea268),
    // keeps up to three racers in front within 7500 units and on screen, replaces
    // the farthest, and fades each pane 30 frames. Before GO (timer > 180) the
    // player's own balloon shows; during the race others from timer 420.
    {
        auto& bs=impl_->balloons;
        const auto& scenario=Kinoko::System::RaceConfig::Instance()->raceScenario();
        using Mode=Kinoko::System::RaceConfig::Scenario::GameMode;
        const bool opponentNames=scenario.gameMode==Mode::Versus||scenario.gameMode==Mode::Battle;
        const unsigned count=std::min<unsigned>(scenario.playerCount,12);
        auto* karts=Kinoko::Kart::KartObjectManager::Instance();
        auto anchor=[&](unsigned i){const auto& p=karts->object(i)->pos();return Vec3{p.x,p.y+120.f,p.z};};
        struct Seen{bool ok=false;float x=0,y=0,z=0;};
        std::array<Seen,12> seen{};
        for(unsigned i=0;i<count;++i){
            auto& s=seen[i];
            s.ok=renderer.projectTop(anchor(i),s.x,s.y,s.z)&&s.z<=0&&s.z>=-7500.f&&s.x>=0&&s.x<=400;
        }
        if(stage<2){
            for(auto& b:bs)b.racer=-1;
            if(timer>180&&seen[0].ok)bs[0].racer=0;
        }else if(timer<420||finished||!opponentNames){
            for(auto& b:bs)b.racer=-1;
        }else{
            for(auto& b:bs)if(b.racer==0||(b.racer>0&&(unsigned(b.racer)>=count||!seen[b.racer].ok)))b.racer=-1;
            for(unsigned i=1;i<count;++i){
                if(!seen[i].ok||std::any_of(bs.begin(),bs.end(),[&](const auto& b){return b.racer==int(i);}))continue;
                auto free=std::find_if(bs.begin(),bs.end(),[](const auto& b){return b.racer<0;});
                if(free!=bs.end()){free->racer=int(i);continue;}
                auto far=std::min_element(bs.begin(),bs.end(),[&](const auto& a,const auto& b){return seen[a.racer].z<seen[b.racer].z;});
                if(seen[far->racer].z<seen[i].z)far->racer=int(i);
            }
        }
        // eFade: alpha_0 / fade_in (30 frames) / alpha_255 / fade_out at 60 Hz.
        for(unsigned elapsed=std::min(tick-impl_->balloonTick,30u);elapsed;--elapsed)for(auto& b:bs){
            if(b.shown!=b.racer){if(b.shown>=0&&b.alpha>0)b.alpha=std::max(0.f,b.alpha-1.f/30);else{b.shown=b.racer;b.alpha=0;}}
            else if(b.shown>=0)b.alpha=std::min(1.f,b.alpha+1.f/30);
        }
        impl_->balloonTick=tick;
        constexpr float Unit=240.f/456.f; // balloon_1_0 scale 1 on the 456-unit-high race canvas
        static constexpr uint32_t SlotGold[4]={0xe8d400ffu,0xe8d400ffu,0xe8d400ffu,0xe8d400ffu};
        for(const auto& b:bs){
            if(b.shown<0||b.alpha<=0||unsigned(b.shown)>=count)continue;
            float ax,ay,az;if(!renderer.projectTop(anchor(unsigned(b.shown)),ax,ay,az)||az>0)continue;
            renderer.setScreenDepth(renderer.stereoParallax(-az));
            const unsigned character=std::min(24u,unsigned(scenario.players[b.shown].character));
            const float lw=BalloonLine[2]*Unit,lh=BalloonLine[3]*Unit;
            renderer.drawScreenSpriteRegion(impl_->balloonLine,ax+BalloonLine[0]*Unit-lw*.5f,ay-BalloonLine[1]*Unit-lh*.5f,lw,lh,0,0,
                                            BalloonLineUV[0],BalloonLineUV[1],b.alpha,0xffffffffu,b.shown==0?SlotGold:nullptr);
            const auto& n=BalloonNames[character];
            const float tw=n.width*BalloonFontScale[0]*Unit,th=n.height*BalloonFontScale[1]*Unit;
            const float right=ax+(BalloonText[0]+BalloonText[2]*.5f)*Unit,cy=ay-BalloonText[1]*Unit;
            // Original chara_name_only_text_color_{red,blue} C1 values.
            const unsigned team=scenario.teams?scenario.players[b.shown].team:2;
            const uint32_t color=team==0?0xff4646ffu:team==1?0x463cffffu:0xffffffffu;
            renderer.drawScreenSpriteRegion(impl_->names[character],right-tw,cy-th*.5f,tw,th,0,0,n.u,n.v,b.alpha,color);
        }
    }
    renderer.setScreenDepth(hudDepth);
    // Single-player HUD slot colour (USA 0x807e2674, table 0x8088fd18): the
    // mean of (255,255,0) and (210,170,0) on top, +(40,130,40) on the bottom.
    static constexpr uint32_t Gold[4]={0xe8d400ffu,0xe8d400ffu,0xffff28ffu,0xffff28ffu};
    auto gold=[&](const Texture& texture,const RaceSprite& art,HudRect rect){
        renderer.drawScreenSpriteRegion(texture,rect.x,rect.y,rect.w,rect.h,0,0,art.u,art.v,1.f,0xffffffffu,Gold);
    };
    // RaceHUD page end (AfterControlUpdate 0x80835A28): every CtrlRaceBase
    // control hides with the same slide, 400 units toward the nearer vertical
    // edge (CtrlRaceBase::InitSelf 0x807E2E3C) in its own control scale.
    if(exitSlide>=1.f)return;
    auto exitLift=[&](float controlScale){
        const float slide=exitSlide>=0?exitSlide:impl_->base.slide;
        const float s=slide*slide;
        return 400.f*1.0005f*(s>=.1f?s-.1f:-s)*controlScale*240.f/456.f;
    };
    const auto& w=impl_->window;
    if(w.state!=1){
        constexpr float Unit=2.1f*240.f/456.f; // CtrlRaceItemWindow_1_0 scale, layout unit -> px
        // CtrlRaceBase slide (0x807e2960): t^2 dips 40 units, then flies 360
        // units toward the nearer edge (up) over 20 frames.
        const float slide=exitSlide>=0?std::max(exitSlide,w.state==0?std::fabs(w.slide):0.f):w.slide;
        const float s=slide*slide;
        const float lift=400.f*1.0005f*(s>=.1f?s-.1f:-s)*Unit;
        const float cx=HudItemFrame.x+HudItemFrame.w*.5f,cy=HudItemFrame.y+HudItemFrame.h*.5f-lift;
        auto frame=[&](float dy,float squash,uint32_t rgba){
            const float h=HudItemFrame.h*squash;if(h<.05f)return;
            renderer.drawScreenSpriteRegion(impl_->extras[HudFrame],HudItemFrame.x,cy+dy-h*.5f,HudItemFrame.w,h,0,0,
                                            HudExtras[HudFrame].u,HudExtras[HudFrame].v,1.f,rgba,Gold);
        };
        // Material colour 1 darkens (modulate); colour 0 brightens toward white.
        auto picture=[&](const Texture& texture,const RaceSprite& art,float dy,float squash,float sx,float sy,uint32_t rgba,uint32_t color0){
            const float pw=HudItemIcon.w*sx,ph=HudItemIcon.h*sy*squash;if(ph<.05f)return;
            const float x=cx-pw*.5f,y=cy+dy-ph*.5f;
            if(color0)renderer.drawScreenFontSprite(texture,x,y,pw,ph,art.u,art.v,1.f,rgba,rgba,color0,0xffffffffu);
            else renderer.drawScreenSpriteRegion(texture,x,y,pw,ph,0,0,art.u,art.v,1.f,rgba);
        };
        auto icon=[&](ItemKind kind)->const Texture&{return impl_->icons[std::min(unsigned(kind),20u)];};
        auto iconArt=[&](ItemKind kind)->const RaceSprite&{return HudItems[std::min(unsigned(kind),20u)];};
        if(w.roulette){
            // Cube roll: item_next_null y=+20cos(90t), rotation X 90(1-t);
            // item_curr_null y=-20sin(90t), rotation X 90t (ortho: height*cos).
            const float t=std::clamp(w.phase,0.f,1.f),angle=t*float(M_PI)*.5f;
            const unsigned a=unsigned(255.f*(1.f-t))&255u;
            const uint32_t dark=(a<<24)|(a<<16)|(a<<8)|0xffu;
            const float nextDy=-20.f*Unit*std::cos(angle),nextSquash=std::sin(angle);
            const float currDy=20.f*Unit*std::sin(angle),currSquash=std::cos(angle);
            frame(nextDy,nextSquash,0xffffffffu);
            if(w.next!=ItemKind::None)picture(icon(w.next),iconArt(w.next),nextDy,nextSquash,1,1,0xffffffffu,dark&0xffffff00u|a);
            if(w.curr!=ItemKind::None){
                frame(currDy,currSquash,dark);
                picture(icon(w.curr),iconArt(w.curr),currDy,currSquash,1,1,dark,0);
            }
        }else{
            frame(0,1,0xffffffffu);
            // Pop (0x807e5220): f=1-2n(n-8)/96 over 9 frames, x scale f+0.5(f-1),
            // with a white flash 510(f-1) in material colour 0.
            const float n=float(std::min(w.popFrame,9u)),f=w.popFrame<9?1.f-2.f*n*(n-8.f)/96.f:1.f;
            const unsigned flash=unsigned(510.f*(f-1.f))&255u;
            const uint32_t color0=flash?(flash<<24)|(flash<<16)|(flash<<8):0;
            if(w.kind!=ItemKind::None){
                // Only a Triple Mushroom changes picture with its count (3/2/1,
                // fn_1_35073C); triple shells and bananas leave the inventory
                // when they deploy, so the window never shows their count.
                if(w.kind==ItemKind::TripleMushroom&&w.charges==2)
                    picture(impl_->extras[HudMushroomTwo],HudExtras[HudMushroomTwo],0,1,f+.5f*(f-1.f),f,0xffffffffu,color0);
                else{
                    const ItemKind kind=w.kind==ItemKind::TripleMushroom&&w.charges==1?ItemKind::Mushroom:w.kind;
                    picture(icon(kind),iconArt(kind),0,1,f+.5f*(f-1.f),f,0xffffffffu,color0);
                }
            }
        }
    }
    if(!impl_->battle.enabled){
    // CtrlRaceLap init (0x807e53d8) tints all four lap panes with the slot colour.
    // CtrlRaceLap_1_0 (y 133, scale 0.8) leaves upward.
    const float lapLift=exitLift(.8f);
    auto lifted=[&](HudRect r,float dy){
        // Preserve the authored one-row layout; scale the whole group 1.1x.
        const float cx=400.f-(HudRank.x+HudRank.w*.5f),cy=240.f-(HudRank.y+HudRank.h*.5f);
        // Retail TPL alpha bounds: LAP (150,0)-(235,39) / 240x40;
        // number 3 (11,3)-(53,59) / 64x64. Centre visible ink, not the
        // label's large transparent left margin, keeping the total on screen.
        constexpr float left=HudLapLabel.x+HudLapLabel.w*(150.f/240.f);
        constexpr float right=HudLapTotal.x+HudLapTotal.w*(53.f/64.f);
        constexpr float top=HudLapCurrent.y+HudLapCurrent.h*(3.f/64.f);
        constexpr float bottom=HudLapTotal.y+HudLapTotal.h*(59.f/64.f);
        r.x=cx+(r.x-(left+right)*.5f)*1.1f;
        r.y=cy+(r.y-(top+bottom)*.5f)*1.1f;
        r.w*=1.1f;r.h*=1.1f;
        r.y+=dy-15.f;return r;
    };
    gold(impl_->extras[HudLap],HudExtras[HudLap],lifted(HudLapLabel,-lapLift));gold(impl_->extras[HudSlash],HudExtras[HudSlash],lifted(HudLapSlash,-lapLift));
    lap=std::clamp(lap,1u,3u);
    gold(impl_->digits[lap],HudDigits[lap],lifted(HudLapCurrent,-lapLift));
    gold(impl_->digits[3],HudDigits[3],lifted(HudLapTotal,-lapLift));
    }
    // Speedometer (not in the original): the player's speed in the game's own
    // units, which Wii speedometer mods label km/h. Bottom right, the one free
    // corner, in the lap counter's gold digits; leaves downward with the HUD.
    if(speedometer)if(const auto* kart=Kinoko::Kart::KartObjectManager::Instance()->object(0)){
        const unsigned speed=std::min(999u,unsigned(std::lround(std::abs(kart->move()->speed()))));
        char text[4];const int n=std::snprintf(text,sizeof text,"%u",speed);
        constexpr float Size=22,Advance=15,Right=392,Top=194;
        const float lift=exitLift(1.f);
        for(int i=0;i<n;++i){const unsigned d=unsigned(text[i]-'0');gold(impl_->digits[d],HudDigits[d],{Right-Size-float(n-1-i)*Advance,Top+lift,Size,Size});}
        renderer.drawScreenText("KM/H",Right-26,Top+Size+3+lift,1.f,0xffffffffu);
    }
    if(!impl_->battle.enabled){
    rank=std::clamp(rank,1u,12u);
    auto& rs=impl_->rankSpin;
    if(!rs.shown){rs.shown=rank;rs.lastTick=tick;}
    for(unsigned elapsed=std::min(tick-rs.lastTick,30u);elapsed;--elapsed){
        if(rank!=rs.shown){if(rs.anim==2){rs.anim=0;rs.frame=0;}else if(rs.anim==1)rs.shown=rank;}
        ++rs.frame;
        if(rs.anim==0&&rs.frame>=16){rs.anim=1;rs.frame=0;if(rank!=rs.shown)rs.shown=rank;}
        else if(rs.anim==1&&rs.frame>=11){rs.anim=2;rs.frame=0;}
    }
    rs.lastTick=tick;
    // game_image_position_close/open.brlan on race_null (scale, rotate Z) and
    // the white position_l_00 echo (scale, alpha), piecewise linear keys.
    float scale=1,turns=0,echoScale=0,echoAlpha=0;
    const float f=float(rs.frame);
    if(rs.anim==0){scale=std::max(0.f,1-f/15.f);turns=std::min(f/15.f,1.f)*5.f;}
    else if(rs.anim==1){
        scale=f<3?1.222f+(0.847f-1.222f)*f/3:f<5?0.847f+(1.046f-0.847f)*(f-3)/2:f<6?1.046f+(1.f-1.046f)*(f-5):1.f;
        echoScale=1.2f+(3.073f-1.2f)*std::min(f/10.f,1.f);echoAlpha=std::max(0.f,1-f/10.f);
    }
    const unsigned shown=std::clamp(rs.shown,1u,12u);
    const auto& art=HudRanks[shown];
    const float angle=-turns*2.f*float(M_PI);
    // The position sits over the near road, which comes up to the screen
    // plane at the bottom edge; Mario Kart 7 shows it on the bottom screen.
    // It takes MK7's countdown layer, MK7's one top-screen 2D layer over the
    // near road, so it is never behind the road it covers.
    renderer.setScreenDepth(renderer.stereoLayer(stereo::CountdownLayer));
    // CtrlRaceRankNum_1_0 (y -150, scale 1.6) leaves downward. Both panes
    // follow race_null's scale/rotation. position_sha maps the same texture
    // to black at its authored alpha 150/255, behind the coloured rank.
    const float cx=HudRank.x+HudRank.w*.5f,cy=HudRank.y+HudRank.h*.5f+exitLift(1.6f);
    if(scale>0.01f){
        const float w=HudRank.w*scale,h=HudRank.h*scale;
        const float dx=(HudRankShadow.x+HudRankShadow.w*.5f-(HudRank.x+HudRank.w*.5f))*scale;
        const float dy=(HudRankShadow.y+HudRankShadow.h*.5f-(HudRank.y+HudRank.h*.5f))*scale;
        const float shadowX=cx+dx*std::cos(angle)-dy*std::sin(angle);
        const float shadowY=cy+dx*std::sin(angle)+dy*std::cos(angle);
        renderer.drawScreenSpriteRegion(impl_->ranks[shown],shadowX-w*.5f,shadowY-h*.5f,w,h,0,0,art.u,art.v,150.f/255.f,0x000000ffu,nullptr,false,angle);
        renderer.drawScreenSpriteRegion(impl_->ranks[shown],cx-w*.5f,cy-h*.5f,w,h,0,0,art.u,art.v,1.f,0xffffffffu,nullptr,false,angle);
    }
    if(echoAlpha>0){
        const float w=HudRank.w*echoScale*scale,h=HudRank.h*echoScale*scale;
        // defolt_color sets only black RGB on position_l_00: its alpha stays 0.
        renderer.drawScreenFontSprite(impl_->ranks[shown],cx-w*.5f,cy-h*.5f,w,h,art.u,art.v,echoAlpha,0xffffffffu,0xffffffffu,0xffffff00u,0xffffffffu);
    }
    }else{
        // CtrlRaceBattleTotalPoint / CtrlRaceBattlePoint: the round's team
        // sums and the local racer's points. Set wins belong to results only.
        const auto& b=impl_->battle;
        auto battleSprite=[&](const Texture& t,const BattleHudArt& a,BattleHudRect r){
            renderer.drawScreenSpriteRegion(t,r.x,r.y,r.w,r.h,0,0,a.u,a.v);
        };
        const float lift=exitLift(1.f);
        const BattleHudRect slots[2][3]={{BattleHud_red_a,BattleHud_red_b,BattleHud_red_c},{BattleHud_blue_a,BattleHud_blue_b,BattleHud_blue_c}};
        for(unsigned team=0;team<2;++team){
            char text[4];const unsigned n=unsigned(std::snprintf(text,sizeof text,"%u",impl_->battleMotion.team(team).score));
            for(unsigned j=0;j<n;++j){const unsigned digit=unsigned(text[j]-'0'),slot=team?j:3-n+j;auto r=slots[team][slot];r.y-=lift;
                const auto& pose=impl_->battleMotion.teamFrame(team,slot);const auto& art=BattleHudDigits[team][digit];
                const float cx=r.x+r.w*.5f,cy=r.y+r.h*.5f,c=std::cos(pose.angle),sn=std::sin(pose.angle);
                float corners[4][2];for(unsigned k=0;k<4;++k){const float x=(k&1?.5f:-.5f)*r.w*pose.sx,y=(k&2?.5f:-.5f)*r.h*pose.sy;
                    corners[k][0]=cx+x*c-y*sn;corners[k][1]=cy+x*sn+y*c;}
                const float uv[4][2]={{0,0},{art.u,0},{0,art.v},{art.u,art.v}};const uint32_t colors[4]={~0u,~0u,~0u,~0u};
                // Restore the authored BRLAN black/white material registers.
                renderer.drawScreenMaterialQuad(&impl_->battleDigits[team][digit],corners,uv,colors,pose.black,pose.white,1.f);
            }
        }
        auto dash=BattleHud_hifun;dash.y-=lift;battleSprite(impl_->battleDash,BattleHudDash,dash);
        const unsigned team=std::min(b.team,1u);
        auto text=[&](const char* str,float cx,float cy,float sx,float sy,float fontHeight,float alpha){
            float width=0;for(const char* ch=str;*ch;++ch){const unsigned index=*ch=='+'?10:*ch=='-'?11:unsigned(*ch-'0');width+=BattleGlyphMetrics[team][index].advance*sx;}
            float x=cx-width*.5f;const float y=cy-fontHeight*sy*.5f;
            for(const char* ch=str;*ch;++ch){const unsigned index=*ch=='+'?10:*ch=='-'?11:unsigned(*ch-'0');
                const auto& art=index<10?BattleHudPersonalDigits[team][index]:BattleSignArt[team][index-10];
                const auto& texture=index<10?impl_->battlePersonal[team][index]:impl_->battleSigns[team][index-10];
                renderer.drawScreenSpriteRegion(texture,x+BattleGlyphMetrics[team][index].origin*sx,y,art.w*sx,art.h*sy,0,0,art.u,art.v,alpha);
                x+=BattleGlyphMetrics[team][index].advance*sx;}
        };
        char points[8];std::snprintf(points,sizeof points,"%u",std::min(b.points,65535u));
        text(points,BattleHud_personal.x+BattleHud_personal.w*.5f,BattleHud_personal.y+BattleHud_personal.h*.5f+exitLift(2.f),BattleHudPersonalScale,BattleHudPersonalScale,64.f,1.f);
        if(impl_->battleMotion.popupVisible()){
            char delta[8];std::snprintf(delta,sizeof delta,"%+d",impl_->battleMotion.popupDelta());const auto& pose=impl_->battleMotion.popupPose();
            text(delta,BattlePopupOriginX,BattlePopupOriginY-pose.y*BattlePopupScale,
                pose.sx*BattlePopupScale,pose.sy*BattlePopupScale*BattlePopupFontHeight/64.f,64.f,pose.alpha);
        }
    }
    renderer.setScreenDepth(hudDepth);
    auto count=[&](HudExtra which,unsigned frame){
        const auto& art=HudExtras[which];
        renderer.setScreenDepth(renderer.stereoLayer(stereo::CountdownLayer));
        for(const auto& layer:HudCountFrames[std::min(frame,unsigned(HudCountFrames.size()-1))]){
            if(layer.alpha<=0||layer.scale<=0)continue;
            const float width=art.w*layer.scale,height=art.h*layer.scale;
            renderer.drawScreenFontSprite(impl_->extras[which],200+layer.x-width/2,120+layer.y-height/2,
                                          width,height,art.u,art.v,layer.alpha,layer.top,layer.bottom,layer.color0,layer.color1);
        }
        renderer.setScreenDepth(hudDepth);
    };
    const auto cue=countdownCue(stage,timer);
    if(cue.digit)count(HudExtra(unsigned(HudCount3)+3-unsigned(cue.digit)),cue.frame);
    else if(cue.go)count(HudGo,cue.frame);
    // RaceHUD::BeforeControlUpdate 0x8083525C: FINISH (Race.bmg 0x4B5) plays
    // CtrlRaceCount::FadeIn once; go_fade_in ends transparent at frame 70.
    if(finished&&finishFrame>=0&&unsigned(finishFrame)<HudCountFrames.size())count(HudFinish,unsigned(finishFrame));
}
}
