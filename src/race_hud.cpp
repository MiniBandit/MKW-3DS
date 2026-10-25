#include <mkw/render_quality.hpp>
#include <mkw/race_hud.hpp>
#include <mkw/render_metrics.hpp>
#include <mkw/race_items.hpp>
#include <mkw/item_box.hpp>
#include <mkw/item_box_uv.hpp>
#include <mkw/bomb_blast.hpp>
#include <mkw/race_balloon.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/blooper.hpp>
#include <mkw/thunder_cloud.hpp>
#include <mkw/item_rules.hpp>
#include <mkw/item_visual.hpp>
#include <game/kart/KartMove.hh>
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
    const std::array<std::array<float,6>,300>* inner; C3D_Mtx transform; unsigned frame; bool sparkle; };
constexpr float IdentitySRT[6]={1,0,0,0,1,0};
uint32_t abgr(const unsigned char (&c)[3]){return 0xff000000u|c[0]|uint32_t(c[1])<<8|uint32_t(c[2])<<16;}
void bindBox(void* context,std::size_t submesh){
    auto& m=*static_cast<BoxMaterial*>(context);const auto& L=*m.layout;
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
    auto texture=[&](int index,GPU_TEXTURE_WRAP_PARAM wrap){auto* t=&m.model->textures[index]->gpu;C3D_TexSetWrap(t,wrap,wrap);return t;};
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_TexEnvColor(e0,0xffffffff);C3D_TexEnvSrc(e0,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);
    if(submesh==L.inner){
        // mat_boxin: weight*TEXC, environment mapped; GX cull front shows the far inner walls.
        float srt[6];const auto& f=(*m.inner)[m.frame%m.inner->size()];for(unsigned i=0;i<6;++i)srt[i]=f[i];
        m.renderer->setEnvironmentUv(m.transform,srt);
        C3D_TexBind(1,texture(L.innerTexture,GPU_REPEAT));
        C3D_TexEnvColor(e0,0xff000000u|L.innerWeight*0x010101u);
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
        C3D_CullFace(GPU_CULL_BACK_CCW);
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
        C3D_CullFace(GPU_CULL_FRONT_CCW);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
        if(!m.sparkle){
            m.renderer->setEnvironmentUv(m.transform,IdentitySRT);
            C3D_TexBind(1,texture(L.shell,GPU_CLAMP_TO_EDGE));
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
        }else{
            // clamp(2*(3/4*noise - (1-mask))).
            m.renderer->setEnvironmentUv(m.transform,L.noiseSRT);
            C3D_TexBind(0,texture(L.mask,GPU_REPEAT));C3D_TexBind(1,texture(L.noise,GPU_REPEAT));
            C3D_TexEnvColor(e0,0xffbfbfbf);
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
            C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0);
            C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR);
            C3D_TexEnvFunc(e1,C3D_RGB,GPU_SUBTRACT);C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
        }
    }
}
// Retail bombCore blast materials (tables generated from the BRRES by tools/export_blast.ts):
// bombCoreRed for the Bob-omb, bombCoreBlue for the Blue Shell. The env-mapped glow stage of
// the core (+C0*glowWhite3s) has no third texture coordinate here and is omitted.
struct BlastMaterial { const Model* model; const uint8_t* colours; };
uint32_t rgba8(const uint8_t* c){return uint32_t(c[0])|uint32_t(c[1])<<8|uint32_t(c[2])<<16|uint32_t(c[3])<<24;}
void bindBlast(void* context,std::size_t submesh){
    auto& m=*static_cast<BlastMaterial*>(context);
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto texture=[&](int index,GPU_TEXTURE_WRAP_PARAM s,GPU_TEXTURE_WRAP_PARAM t){auto* x=&m.model->textures[index]->gpu;C3D_TexSetWrap(x,s,t);return x;};
    C3D_CullFace(GPU_CULL_NONE);
    auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
    if(submesh==BombCoreSubmesh){
        const uint8_t *c0=m.colours,*c1=m.colours+4;
        C3D_TexBind(0,texture(BombWaveTexture,GPU_REPEAT,GPU_REPEAT));C3D_TexBind(1,texture(BombNetTexture,GPU_REPEAT,GPU_REPEAT));
        // GX 0: lerp(C1, C0, ef_ami), alpha ef_ami (C1 rides in the combiner buffer colour).
        C3D_TexEnvBufColor(rgba8(c1));
        C3D_TexEnvColor(e0,rgba8(c0));
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS_BUFFER,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);
        // GX 1: lerp(prev, C0, ef_waveIA) + C1, alpha prev * ef_waveIA + A0.
        C3D_TexEnvColor(e1,rgba8(c0));
        C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(e1,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_Alpha,GPU_MODULATE);
        auto* e2=C3D_GetTexEnv(2);
        C3D_TexEnvColor(e2,(rgba8(c1)&0xffffffu)|uint32_t(c0[3])<<24);
        C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e2,C3D_Both,GPU_ADD);
        // GX 2 alpha: prev - A1, the dissolve; alpha test >= 1/255.
        auto* e3=C3D_GetTexEnv(3);
        C3D_TexEnvColor(e3,uint32_t(c1[3])<<24);
        C3D_TexEnvSrc(e3,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e3,C3D_Alpha,GPU_SUBTRACT);
        C3D_AlphaTest(true,GPU_GEQUAL,1);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    }else{
        const uint8_t *c0=m.colours+8,*c1=m.colours+12;
        C3D_TexBind(0,texture(BombShockTexture,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE));
        // bombShk: lerp(C0, C1, wave08_3), alpha 2 * texture alpha * A0, blended, no depth write.
        C3D_TexEnvBufColor(rgba8(c0));
        C3D_TexEnvColor(e0,rgba8(c1));
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS_BUFFER,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);C3D_TexEnvScale(e0,C3D_Alpha,GPU_TEVSCALE_2);
        C3D_TexEnvColor(e1,uint32_t(c0[3])<<24);
        C3D_TexEnvSrc(e1,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_Alpha,GPU_MODULATE);
        C3D_AlphaTest(false,GPU_ALWAYS,0);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
    }
}
void drawBlast(Renderer& renderer,const Model& model,const C3D_Mtx& t,unsigned age,bool blue){
    const unsigned frame=std::min(age,BombBlastFrames-1);
    BlastMaterial m{&model,(blue?BombBlastBlue:BombBlastRed)[frame].data()};
    std::array<SubmeshOverride,2> uv{};
    for(auto& o:uv){o.flags=SubmeshOverride::Uv;o.uv[0]=o.uv[4]=o.uv[6]=o.uv[10]=1;} // identity rows
    for(unsigned k=0;k<12;++k)uv[BombCoreSubmesh].uv[k]=BombCoreUV[frame][k];
    for(unsigned k=0;k<6;++k)uv[BombShockSubmesh].uv[k]=BombShockUV[frame][k];
    renderer.setOverrides(uv.data(),unsigned(uv.size()));
    // The context is copied by value: a deferred frame draws after this returns.
    renderer.draw(model,t,false,bindBlast,m);
    renderer.setOverrides(nullptr,0);
}
bool outerOnly(void* context,std::size_t submesh){return submesh==static_cast<BoxMaterial*>(context)->layout->outer;}
void drawBox(Renderer& renderer,const Model& model,const ItemBoxLayout& layout,const std::array<std::array<float,6>,300>& inner,const C3D_Mtx& t,unsigned frame,bool sparkle=true){
    BoxMaterial m{&renderer,&model,&layout,&inner,t,frame,false};
    renderer.draw(model,t,false,bindBox,m);
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
    std::array<std::unique_ptr<Model>,21> models;
    std::array<Vec3,12> bulletUp{};
    std::array<bool,12> wasBullet{};
    std::array<ThunderCloudVisual,12> clouds{};
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
    std::array<Texture,21> icons;
    std::array<Texture,13> ranks;
    std::array<Texture,10> digits;
    std::array<Texture,HudExtraCount> extras;
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
    std::array<Texture,24> names;
    Texture balloonLine;
};
RaceHud::RaceHud():impl_(std::make_unique<Impl>()){}
RaceHud::~RaceHud()=default;
void RaceHud::reset(){impl_=std::make_unique<Impl>();}
bool RaceHud::load(){
    reset();bool valid=true;
    auto model=[](const char* path){auto m=std::make_unique<Model>();if(!path||!m->load(path))m.reset();return m;};
    impl_->box=model(ItemBoxModel);valid&=bool(impl_->box);
    impl_->bombCore=model("models/bomb_blast.gmdl");impl_->blastMaterials=bool(impl_->bombCore);
    if(!impl_->bombCore)impl_->bombCore=model("models/bomb_core.gmdl");
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
                model.vertexCount=unique;
                flushGpuData(model.indices,model.indexCount*sizeof(uint16_t));
                flushGpuData(impl_->bombFrames,compactBytes);
            }else valid=false;
        }else valid=false;
    }
    for(unsigned i=0;i<21;++i){
        if(ItemModels[i]){impl_->models[i]=model(ItemModels[i]);valid&=bool(impl_->models[i]);}
        if(HudItems[i].path)valid&=impl_->icons[i].load(HudItems[i].path);
        if((i==6||i==7||i==13||i==14||i==15)&&impl_->models[i]){
            char path[96];std::snprintf(path,sizeof path,"models/item_%u.dskm",i);
            // Unlit and never environment-mapped, these meshes do not read
            // normals: skin positions only (the same position arithmetic).
            auto animation=std::make_unique<DriverAnimation>();
            if(animation->load(path,*impl_->models[i])){animation->skinNormals(true);impl_->animations[i]=std::move(animation);}
        }
    }
    if(impl_->models[3])bakeMaskUv(*impl_->models[3],FakeBoxMaterials);
    for(unsigned i=1;i<=12;++i)valid&=impl_->ranks[i].load(HudRanks[i].path);
    for(unsigned i=0;i<10;++i)valid&=impl_->digits[i].load(HudDigits[i].path);
    for(unsigned i=0;i<HudExtraCount;++i)valid&=impl_->extras[i].load(HudExtras[i].path);
    for(unsigned i=0;i<BalloonNames.size();++i)valid&=impl_->names[i].load(BalloonNames[i].path);
    valid&=impl_->balloonLine.load(BalloonLinePath);
    return valid;
}
void RaceHud::stepWorld(const RaceItems& items,Vec3 cameraBackward,bool started){
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
            cloud.step(position,direction,CloudDuration-std::min<unsigned>(CloudDuration,effect.cloud));
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
    needed[13]=items.hud(0).pow&&items.hud(0).pow<=158;
    for(unsigned i=0;i<21;++i)if(needed[i]&&impl_->animations[i]){
        if(i==13){
            const unsigned left=items.hud(0).pow;if(!left||left>158)continue;
            const char* name=left>110?"attack1":left>60?"attack2":"attack3";
            const unsigned start=left>110?158:left>60?110:60;
            const int clip=impl_->animations[i]->find(name);
            if(clip>=0)impl_->animations[i]->apply(*impl_->models[i],clip,float(start-left),tick);
            continue;
        }
        int clip=impl_->animations[i]->find(i==6?"wait_a":"wait");
        // Reduced quality: one pose per animationStep ticks (apply reuses it).
        const unsigned step=renderQuality().animationStep,poseTick=step?tick/step*step:tick;
        impl_->animations[i]->apply(*impl_->models[i],clip<0?0:clip,float(poseTick),poseTick);
    }
    const auto& quality=renderQuality();const Vec3 camera=renderer.cameraPosition();
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
        const float ratio=quality.reduced?RenderQuality::screenRatio({impl_->box->minimum,impl_->box->maximum},t,camera):1.f;
        if(ratio<quality.minScreenRatio)continue;
        drawBox(renderer,*impl_->box,ItemBoxMaterials,ItemBoxInnerSRT,t,tick,ratio>=quality.boxSparkleRatio);
    }
    // Leave the uniforms as the last drawBox would, whether or not any was drawn.
    if(boxesInRange){renderer.clearEnvironmentUv();renderer.setUvOffset(0,0);}
    for(const auto& actor:items.actors())if(actor.active&&unsigned(actor.kind)<21&&dot(actor.position-viewer,actor.position-viewer)<22000.f*22000.f){
        const bool blast=(actor.kind==ItemKind::Bobomb||actor.kind==ItemKind::BlueShell)&&actor.explosion;
        auto& model=blast?impl_->bombCore:impl_->models[unsigned(actor.kind)];if(!model)continue;
        Vertex* originalVertices=model->vertices;
        if(blast&&impl_->bombFrames)model->vertices=impl_->bombFrames+std::min(unsigned(actor.age),impl_->bombFrameCount-1)*model->vertexCount;
        const Vec3 position=actor.visualPosition();
        C3D_Mtx t;
        if(actor.kind==ItemKind::FakeBox){
            fakeBoxTransform(t,position,{actor.scale,actor.scale,actor.scale},actor.age);
            if(!quality.tooSmall({model->minimum,model->maximum},t,camera))drawBox(renderer,*model,FakeBoxMaterials,FakeBoxInnerSRT,t,tick);
            model->vertices=originalVertices;continue;
        }
        const auto visual=releasedItemVisual(actor);
        const auto x=visual.right*visual.scale.x,y=visual.up*visual.scale.y,z=visual.forward*visual.scale.z;
        Mtx_Identity(&t);
        t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=position.x;
        t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=position.y;
        t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=position.z;
        if(!quality.tooSmall({model->minimum,model->maximum},t,camera)){
            if(blast&&impl_->blastMaterials)drawBlast(renderer,*model,t,actor.age,actor.kind==ItemKind::BlueShell);
            else renderer.draw(*model,t);
        }
        model->vertices=originalVertices;
    }
    if(items.hud(0).pow&&items.hud(0).pow<=158&&impl_->models[13]){
        const auto& pose=Kinoko::Kart::KartObjectManager::Instance()->object(0)->pose();
        // Retail PowObj uses inverse-view up250, back-30 (=look forward30),
        // uniform1.3 scale. Its authored CHR0 performs the three block stomps.
        renderer.drawCameraFacing(*impl_->models[13],{pose[0,3],pose[1,3],pose[2,3]},250.f,30.f,1.3f);
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
            renderer.draw(*impl_->models[15],t);
        }
        if(effect.cloud&&impl_->models[14]){
            const auto& cloud=impl_->clouds[i];
            const auto z=cloud.facing,x=normalized(cross({0,1,0},z),{1,0,0}),y=cross(z,x),pCloud=cloud.modelPosition();
            C3D_Mtx t;Mtx_Identity(&t);
            t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=pCloud.x;
            t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=pCloud.y;
            t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=pCloud.z;
            renderer.draw(*impl_->models[14],t);
        }
        unsigned orbitModel=effect.heldKind==ItemKind::TripleGreenShell?0:effect.heldKind==ItemKind::TripleRedShell?1:effect.heldKind==ItemKind::TripleBanana?2:unsigned(effect.heldKind);
        if(orbit&&impl_->models[orbitModel])for(unsigned n=0;n<3;++n)if(effect.heldMask&(1u<<n)){
            const Vec3 position=items.heldItemPosition(i,n);
            auto visual=items.heldItemVisual(i,n);
            if(spinningShell(effect.heldKind)){
                // Triples already carry their own source spin in heldItemVisual.
                const float spin=shellVisualSpin(tick);
                visual.forward=visual.forward*std::cos(spin)+visual.right*std::sin(spin);
                visual.right=cross(visual.up,visual.forward);
            }
            if(orbitModel==unsigned(ItemKind::FakeBox)){
                C3D_Mtx t;fakeBoxTransform(t,position,visual.scale,tick);
                drawBox(renderer,*impl_->models[orbitModel],FakeBoxMaterials,FakeBoxInnerSRT,t,tick);continue;
            }
            const auto x=visual.right*visual.scale.x,y=visual.up*visual.scale.y,z=visual.forward*visual.scale.z;
            C3D_Mtx t;Mtx_Identity(&t);
            t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=position.x;
            t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=position.y;
            t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=position.z;
            renderer.draw(*impl_->models[orbitModel],t);
        }
    }
}
void RaceHud::drawOverlay(Renderer& renderer,const RaceItems& items,unsigned stage,unsigned timer,unsigned rank,unsigned lap,bool finished,unsigned tick,int finishFrame,float exitSlide){
    const auto& held=items.hud(0);
    auto sprite=[&](const Texture& texture,const RaceSprite& art,HudRect rect,float alpha=1.f,uint32_t tint=0xffffffffu){
        renderer.drawScreenSprite(texture,rect.x,rect.y,rect.w,rect.h,art.u,art.v,alpha,tint);
    };
    // Stereo (Mario Kart 7's layers): the ink and the name balloons sit at
    // the depth they are anchored to; the HUD panes 4 px behind the screen;
    // the countdown, GO, FINISH and the position come out of it.
    const float hudDepth=renderer.stereoLayer(stereo::HudLayer);
    if(held.ink){
        const unsigned age=BlooperInkDuration-std::min<unsigned>(held.ink,BlooperInkDuration);
        if(age<HudInkFrames.size()){
            const auto& ink=HudInkFrames[age];
            renderer.setScreenDepth(renderer.stereoParallax(HudInkDepth));
            // Original camera-space emitter(0,0,-350) and150-unit half size,
            // projected with the active native camera. Source growth/fade450f.
            const float fullSize=240.f*HudInkHalfExtent/(HudInkDepth*std::tan(renderer.fov()*3.14159265358979323846f/360.f));
            const float w=fullSize*ink.scaleX,h=fullSize*ink.scaleY;
            sprite(impl_->extras[HudInk],HudExtras[HudInk],{200-w/2,120-h/2,w,h},ink.alpha,0x000000ffu);
        }
    }
    // Original single-player BRCTR corner anchors and BRLYT pane sizes. 3DS is
    // 5:3: retain uniform glyph scaling and each corner's source safe margin.
    // Name balloons. Retail anchors kart pos + (0,120,0) (static init 0x807ea268),
    // keeps up to three racers in front within 7500 units and on screen, replaces
    // the farthest, and fades each pane 30 frames. Before GO (timer > 180) the
    // player's own balloon shows; during the race others from timer 420.
    {
        auto& bs=impl_->balloons;
        const auto& scenario=Kinoko::System::RaceConfig::Instance()->raceScenario();
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
        }else if(timer<420||finished){
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
            const unsigned character=std::min(23u,unsigned(scenario.players[b.shown].character));
            const float lw=BalloonLine[2]*Unit,lh=BalloonLine[3]*Unit;
            renderer.drawScreenSpriteRegion(impl_->balloonLine,ax+BalloonLine[0]*Unit-lw*.5f,ay-BalloonLine[1]*Unit-lh*.5f,lw,lh,0,0,
                                            BalloonLineUV[0],BalloonLineUV[1],b.alpha,0xffffffffu,b.shown==0?SlotGold:nullptr);
            const auto& n=BalloonNames[character];
            const float tw=n.width*BalloonFontScale[0]*Unit,th=n.height*BalloonFontScale[1]*Unit;
            const float right=ax+(BalloonText[0]+BalloonText[2]*.5f)*Unit,cy=ay-BalloonText[1]*Unit;
            renderer.drawScreenSpriteRegion(impl_->names[character],right-tw,cy-th*.5f,tw,th,0,0,n.u,n.v,b.alpha);
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
    // CtrlRaceLap init (0x807e53d8) tints all four lap panes with the slot colour.
    // CtrlRaceLap_1_0 (y 133, scale 0.8) leaves upward.
    const float lapLift=exitLift(.8f);
    auto lifted=[&](HudRect r,float dy){r.y+=dy;return r;};
    gold(impl_->extras[HudLap],HudExtras[HudLap],lifted(HudLapLabel,-lapLift));gold(impl_->extras[HudSlash],HudExtras[HudSlash],lifted(HudLapSlash,-lapLift));
    lap=std::clamp(lap,1u,3u);
    gold(impl_->digits[lap],HudDigits[lap],lifted(HudLapCurrent,-lapLift));
    gold(impl_->digits[3],HudDigits[3],lifted(HudLapTotal,-lapLift));
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
    // Incoming threats: a small item frame each, top centre, toward their side.
    if(!finished&&exitSlide<0)for(unsigned i=0;i<std::min(warningCount,unsigned(warnings.size()));++i){
        const auto& warning=warnings[i];
        if(warning.kind>=20)continue;
        constexpr float Size=30;
        const float x=200+std::clamp(warning.side,-1.f,1.f)*90-Size*.5f+float(i)*4,y=6+float(i)*3;
        const bool close=warning.distance<1500;
        const uint32_t tint=close&&(tick/6)%2?0xff5050ffu:0xffffffffu;
        const float alpha=std::clamp(1.3f-warning.distance/6000.f,.55f,1.f);
        renderer.drawScreenSpriteRegion(impl_->extras[HudFrame],x-3,y-3,Size+6,Size+6,0,0,HudExtras[HudFrame].u,HudExtras[HudFrame].v,alpha,tint);
        const unsigned k=warning.kind;
        renderer.drawScreenSpriteRegion(impl_->icons[k],x,y,Size,Size,0,0,HudItems[k].u,HudItems[k].v,alpha,tint);
    }
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
    // CtrlRaceRankNum_1_0 (y -150, scale 1.6) leaves downward. The retail
    // drop shadow (HudRankShadow) is left out: it read as a stray grey copy.
    const float cx=HudRank.x+HudRank.w*.5f,cy=HudRank.y+HudRank.h*.5f+exitLift(1.6f);
    if(scale>0.01f){
        const float w=HudRank.w*scale,h=HudRank.h*scale;
        renderer.drawScreenSpriteRegion(impl_->ranks[shown],cx-w*.5f,cy-h*.5f,w,h,0,0,art.u,art.v,1.f,0xffffffffu,nullptr,false,angle);
    }
    if(echoAlpha>0){
        const float w=HudRank.w*echoScale*scale,h=HudRank.h*echoScale*scale;
        // defolt_color sets only black RGB on position_l_00: its alpha stays 0.
        renderer.drawScreenFontSprite(impl_->ranks[shown],cx-w*.5f,cy-h*.5f,w,h,art.u,art.v,echoAlpha,0xffffffffu,0xffffffffu,0xffffff00u,0xffffffffu);
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
