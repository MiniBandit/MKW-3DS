#include <mkw/render_quality.hpp>
#include <mkw/race_hud.hpp>
#include <mkw/render_metrics.hpp>
#include <mkw/race_items.hpp>
#include <mkw/item_box.hpp>
#include <mkw/item_box_uv.hpp>
#include <mkw/bomb_blast.hpp>
#include <mkw/race_balloon.hpp>
#include <mkw/race_chase_layout.hpp>
#include <mkw/race_flow_pose.hpp>
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
#include <game/system/RaceManager.hh>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
namespace mkw {
namespace {
// Retail itembox / itemBoxNise materials (layout generated from the BRRES). Every pass is
// one draw of one submesh, in GX order: mat_boxin (itemBoxNise: and its rainbow layer),
// mat_question, then mat_boxout's environment shell and its sparkle per dot group.
enum class BoxPass : uint8_t { Inner, Rainbow, Question, Shell, Sparkle };
struct BoxMaterial { Renderer* renderer; const Model* model; const ItemBoxLayout* layout;
    const std::array<std::array<float,6>,300>* inner; C3D_Mtx transform; unsigned frame; BoxPass pass; uint8_t group,rainbow; };
constexpr float IdentitySRT[6]={1,0,0,0,1,0};
uint32_t abgr(const unsigned char (&c)[3]){return 0xff000000u|c[0]|uint32_t(c[1])<<8|uint32_t(c[2])<<16;}
void bindBox(void* context,std::size_t){
    auto& m=*static_cast<BoxMaterial*>(context);const auto& L=*m.layout;
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
    auto texture=[&](int index,GPU_TEXTURE_WRAP_PARAM wrap){auto* t=&m.model->textures[index]->gpu;C3D_TexSetWrap(t,wrap,wrap);return t;};
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_TexEnvColor(e0,0xffffffff);C3D_TexEnvSrc(e0,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);
    // No item box material updates framebuffer alpha (GX alphaUpdate off): the blends keep it.
    if(m.pass==BoxPass::Inner||m.pass==BoxPass::Rainbow){
        // mat_boxin: weight*TEXC, environment mapped; GX cull front shows the far inner walls.
        // itemBoxNise lerps it toward its rainbow layer by A0 (m.rainbow): (1-A0)*TEXC here, then
        // the Rainbow pass adds A0*layer over the same depth.
        const bool layer=m.pass==BoxPass::Rainbow;
        float srt[6];const auto& f=(*m.inner)[m.frame%m.inner->size()];for(unsigned i=0;i<6;++i)srt[i]=layer?L.rainbowSRT[i]:f[i];
        m.renderer->setEnvironmentUv(m.transform,srt);
        C3D_TexBind(1,layer?texture(L.rainbowTexture,GPU_CLAMP_TO_EDGE):texture(L.innerTexture,GPU_REPEAT));
        const uint32_t weight=layer?m.rainbow:L.innerWeight*(255u-m.rainbow)/255u;
        C3D_TexEnvColor(e0,0xff000000u|weight*0x010101u);
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
        C3D_CullFace(m.renderer->faceCull(GPU_CULL_BACK_CCW));
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,layer?GPU_ONE:GPU_ZERO,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,layer?GPU_WRITE_COLOR:GPU_WRITE_ALL);
    }else if(m.pass==BoxPass::Question){
        // mat_question: lerp(low, high, TEXC), alpha TEXA tested >= reference.
        C3D_TexBind(0,texture(L.questionTexture,GPU_CLAMP_TO_EDGE));
        C3D_TexEnvColor(e0,abgr(L.questionLow));
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
        C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0);
        C3D_TexEnvColor(e1,abgr(L.questionHigh));
        C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
        C3D_AlphaTest(true,GPU_GEQUAL,L.questionAlpha);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
        if(m.layout==&ItemBoxMaterials){const auto& uv=ItemBoxQuestionUV[m.frame%ItemBoxQuestionUV.size()];m.renderer->setUvOffset(uv[2],uv[5]);}
    }else{
        // mat_boxout: additive, no depth write, GX cull back. GX stage 2 adds the environment
        // shell to the sparkle of stages 0-1; each is its own additive pass here.
        C3D_CullFace(m.renderer->faceCull(GPU_CULL_FRONT_CCW));
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ONE,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
        if(m.pass==BoxPass::Shell){
            m.renderer->setEnvironmentUv(m.transform,IdentitySRT);
            C3D_TexBind(1,texture(L.shell,GPU_CLAMP_TO_EDGE));
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
        }else{
            // GX: clamp(2*(3/4*noise - (1-mask) - 1/2)), the noise read through the indirect
            // texture. Its offset is constant per dot: the dots of group m.group use their
            // offset, their mask is that channel of the mask texture (cook_race_items.py).
            float srt[6];for(unsigned i=0;i<6;++i)srt[i]=L.noiseSRT[i];
            srt[2]+=L.sparkleOffset[m.group][0];srt[5]+=L.sparkleOffset[m.group][1];
            m.renderer->setEnvironmentUv(m.transform,srt);
            C3D_TexBind(0,texture(L.mask,GPU_REPEAT));C3D_TexBind(1,texture(L.noise,GPU_REPEAT));
            C3D_TexEnvColor(e0,0xffbfbfbf);
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
            constexpr GPU_TEVOP_RGB inverseMask[]={GPU_TEVOP_RGB_ONE_MINUS_SRC_R,GPU_TEVOP_RGB_ONE_MINUS_SRC_G,GPU_TEVOP_RGB_ONE_MINUS_SRC_B};
            C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0);
            C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,inverseMask[m.group]);
            C3D_TexEnvFunc(e1,C3D_RGB,GPU_SUBTRACT);
            // The GX -1/2 bias, then its x2 scale: prev + 0 - 1/2 (ADD_SIGNED), doubled.
            auto* e2=C3D_GetTexEnv(2);
            C3D_TexEnvColor(e2,0xff000000);
            C3D_TexEnvSrc(e2,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e2,C3D_RGB,GPU_ADD_SIGNED);
            C3D_TexEnvScale(e2,C3D_RGB,GPU_TEVSCALE_2);
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
bool boxPassShape(void* context,std::size_t submesh){
    const auto& m=*static_cast<BoxMaterial*>(context);const auto& L=*m.layout;
    return submesh==(m.pass==BoxPass::Inner||m.pass==BoxPass::Rainbow?L.inner:m.pass==BoxPass::Question?L.question:L.outer);
}
void drawBox(Renderer& renderer,const Model& model,const ItemBoxLayout& layout,const std::array<std::array<float,6>,300>& inner,const C3D_Mtx& t,unsigned frame,bool sparkle=true){
    BoxMaterial m{&renderer,&model,&layout,&inner,t,frame,BoxPass::Inner,0,0};
    const Vec3 centre{t.r[0].w,t.r[1].w,t.r[2].w};
    if(layout.rainbowTexture>=0){
        // ItemObjFib (PAL 0x807A7D90): A0 = 255 * (camera distance - 3000) / (6000 - 3000),
        // clamped and truncated; far fake boxes take the real boxes' rainbow.
        const Vec3 d=centre-renderer.cameraPosition();
        m.rainbow=uint8_t(std::clamp(255.f*(std::sqrt(dot(d,d))-3000.f)/3000.f,0.f,255.f));
    }
    renderer.draw(model,t,false,bindBox,m,boxPassShape);
    if(m.rainbow){m.pass=BoxPass::Rainbow;renderer.draw(model,t,false,bindBox,m,boxPassShape);}
    // hatenaPlane is an NW4R BILLBOARD_ROT node with no local rotation: at the box centre,
    // facing the screen upright, scaled with the box; it does not tumble with the box.
    const float scale=std::sqrt(t.r[0].x*t.r[0].x+t.r[1].x*t.r[1].x+t.r[2].x*t.r[2].x);
    m.pass=BoxPass::Question;renderer.draw(model,renderer.cameraFacing(centre,scale),false,bindBox,m,boxPassShape);
    m.pass=BoxPass::Shell;renderer.draw(model,t,false,bindBox,m,boxPassShape);
    // Reduced detail drops the additive sparkle passes of distant boxes.
    if(sparkle)for(m.group=0;m.group<layout.sparkleGroups;++m.group){m.pass=BoxPass::Sparkle;renderer.draw(model,t,false,bindBox,m,boxPassShape);}
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
// ItemObj property +0x6c by object (0x809C2F48 + type*0x74, from the templates copied by
// initItemObjProperties 0x80790AFC): the drop shadow's x and z scale; 0 for no shadow model.
float itemShadowWidth(ItemKind kind){
    switch(kind){
    case ItemKind::GreenShell:case ItemKind::RedShell:case ItemKind::Banana:case ItemKind::Bobomb:return 1;
    case ItemKind::Star:return .8f;
    case ItemKind::BlueShell:return .9f;
    case ItemKind::FakeBox:return 1.2f;
    case ItemKind::ThunderCloud:return 1.3f;
    default:return 0;
    }
}
C3D_Mtx axesTransform(Vec3 x,Vec3 y,Vec3 z,Vec3 p){
    C3D_Mtx t;Mtx_Identity(&t);
    t.r[0].x=x.x;t.r[0].y=y.x;t.r[0].z=z.x;t.r[0].w=p.x;
    t.r[1].x=x.y;t.r[1].y=y.y;t.r[1].z=z.y;t.r[1].w=p.y;
    t.r[2].x=x.z;t.r[2].y=y.z;t.r[2].z=z.z;t.r[2].w=p.z;
    return t;
}
void fakeBoxTransform(C3D_Mtx& t,Vec3 p,Vec3 scale,unsigned counter){
    const float a=float(counter%180)*2.f*float(M_PI)/180.f;
    Mtx_Identity(&t);Mtx_Translate(&t,p.x,p.y,p.z,true);Mtx_RotateZ(&t,a,true);Mtx_RotateX(&t,a,true);
    Mtx_Scale(&t,.4f*scale.x,.4f*scale.y,.4f*scale.z);
}
// CtrlRaceItemBalloon (game_image chase_icon; PAL load 0x807F1F50, calcSelf 0x807F22BC):
// player 0's incoming-item icon and the BalloonManager fields it reads.
struct ChaseIcon {
    int fade=0;float fadeFrame=0;               // eFade clip (ChaseFade) and frame; init 0x807F2118: alpha_0
    float roopFrame=0,zoomFrame=0;              // roop (hilight turn) and zoom (picture pulse) groups
    float roopRate=1,zoomRate=1;                // their group speeds (GroupThing_construct 0x8063C844: 1)
    bool hidden=true;                           // +0x80
    float x=0,paneX=0;                          // +0x184 eased x; chase_icon_null x (layout units)
    int item=-1,racerPicture=-1,itemPicture=-1; // +0x18c, +0x17c, +0x180
    uint8_t picture=ChasePictureRed;            // the item pane: chase_icon.brlyt's tt_item_kame_red
    bool alarm=false;float alarmX=0,alarmRate=0;
    int opened=-1;std::array<uint16_t,21> shown{}; // QA
};
// The on-screen test |x| < screen width / 2 (*0x809C4758 +0x3F0), on the 400 px top screen.
constexpr float ChaseHalfWidth=200.f*456.f/240.f;
// fn_1_12C6E4 (0x8063CA98) starts the opposite eFade clip at the mirrored frame, so the
// alpha carries on. calcSelf's show: alpha_0 -> fade_in, fade_out -> fade_in.
void chaseOpen(ChaseIcon& c){
    c.hidden=false;
    if(c.fade==0){c.fade=1;c.fadeFrame=0;}
    else if(c.fade==3){c.fadeFrame=ChaseFade[1].frames*(1-c.fadeFrame/ChaseFade[3].frames);c.fade=1;}
}
// Its close: x 0; alpha_255 -> fade_out, fade_in -> fade_out; at alpha_0 hidden.
void chaseClose(ChaseIcon& c){
    c.x=0;c.hidden=false;
    if(c.fade==2){c.fade=3;c.fadeFrame=0;}
    else if(c.fade==1){c.fadeFrame=ChaseFade[3].frames*(1-c.fadeFrame/ChaseFade[1].frames);c.fade=3;}
    else if(c.fade==0){c.hidden=true;c.fadeFrame=0;}
}
// The view the icon is placed from (fn_1_2E28D0 0x807F2C84, fn_1_2E32D8 0x807F368C).
// Without look-behind: a LookAtCamera at the eye mirrored through the target in x/z
// (2 target - eye, the eye's height kept), looking at the target with up negated, so
// a point behind the kart keeps its side of the screen. With look-behind: the camera.
Vec3 chaseView(const HudCamera& camera,Vec3 p){
    Vec3 eye=camera.eye,up=camera.up;
    if(!camera.lookBack){eye={2*camera.target.x-eye.x,eye.y,2*camera.target.z-eye.z};up=-up;}
    const Vec3 z=normalized(eye-camera.target,{0,0,1}),x=normalized(cross(up,z),{1,0,0}),y=cross(z,x),d=p-eye;
    return {dot(d,x),dot(d,y),dot(d,z)};
}
// fn_80227E18 on the race frustum, canvas centred (fn_1_55C6C 0x80566020): layout
// x = vx * cot(fovy/2) * 228 / -vz on the 456-unit-high HUD canvas.
float chaseX(Vec3 v,float fov){
    const float x=v.x*228.f/(std::tan(fov*float(M_PI)/360.f)*-v.z);
    return std::isfinite(x)?x:0.f;
}
// The tracking tail: chase_icon_null at (x, 0); roop at rate r = clamp(-2000/vz, 0.01, 2)
// and zoom at 1.5r (0x808A9EEC, 0x808A9EF0, 0x808A9ED8, 0x808A9EF4); the alarm while
// |x| is on screen.
void chasePlace(ChaseIcon& c,float x,float vz,bool lookBack,bool alarm){
    c.x=lookBack?0.f:x;c.paneX=x;
    const float r=-(2000.f/vz),rate=r>=.01f?std::min(r,2.f):.01f;
    c.roopRate=rate;c.zoomRate=1.5f*rate;
    if(alarm&&std::fabs(x)<ChaseHalfWidth){c.alarm=true;c.alarmX=c.x;c.alarmRate=rate;}
}
// fn_1_2E28D0: a Red Shell homing on the racer, or a near Blue Shell, sits where the
// mirrored view sees it (closed while behind that camera), easing 0.96/0.04
// (0x808A9EDC/0x808A9EE0). A Blue Shell locked on the racer stays at view depth -500
// (0x808A9EE4) and eases 0.94 (0x808A9EE8) toward the centre. No alarm for a Blue in
// its blast (state 6).
void chaseItem(ChaseIcon& c,const ItemActor& a,bool lockedBlue,const HudCamera& camera,bool mirror){
    float x,vz;
    if(!lockedBlue){
        const Vec3 v=chaseView(camera,a.visualPosition());
        if(v.z>0){chaseClose(c);return;}
        x=chaseX(v,camera.fov);if(mirror)x=-x;
        if(!camera.lookBack&&c.x!=0)x=.96f*c.x+.04f*x;
        vz=v.z;
    }else{
        // Retail leaves x unset when +0x184 is 0 (stack 0x44(r1), stored only at 0x807F32F8): 0 here.
        x=!camera.lookBack&&c.x!=0?.94f*c.x:0.f;vz=-500.f;
        chaseOpen(c);
    }
    chasePlace(c,x,vz,camera.lookBack,!(a.kind==ItemKind::BlueShell&&a.explosion));
}
// fn_1_2E32D8: a racer behind sits where the mirrored view sees its kart while within
// 7500 of that camera (0x808D3E18) and behind the race camera; easing 0.7/0.3
// (0x808A9DF0/0x808A9EF8).
void chaseRacer(ChaseIcon& c,Vec3 position,float raceZ,const HudCamera& camera,bool mirror){
    const Vec3 v=chaseView(camera,position);
    if(!camera.lookBack&&!(v.z>=-7500.f&&raceZ>=0)){chaseClose(c);return;}
    float x=chaseX(v,camera.fov);if(mirror)x=-x;
    if(!camera.lookBack&&c.x!=0)x=.7f*c.x+.3f*x;
    chasePlace(c,x,v.z,camera.lookBack,true);
}
// fn_1_2E27C0 (0x807F2B74): while fading in or shown; a new item restarts the easing.
void chaseItemPicture(ChaseIcon& c,int slot,ItemKind kind){
    if(c.fade!=1&&c.fade!=2)return;
    if(slot!=c.item)c.x=0;
    c.item=slot;c.racerPicture=-1;
    if(kind==ItemKind::BlueShell){if(c.itemPicture!=5){c.itemPicture=5;c.picture=ChasePictureBlue;}}
    else if(c.itemPicture!=1){c.itemPicture=1;c.picture=ChasePictureRed;}
}
// fn_1_2E3138 (0x807F34EC): Star, then Bullet Bill, Mega Mushroom, Thunder Cloud; the
// picture only moves to a status the racer has.
void chaseRacerPicture(ChaseIcon& c,bool star,bool bullet,bool mega,bool cloud){
    c.itemPicture=-1;
    if(c.fade!=1&&c.fade!=2)return;
    int& p=c.racerPicture;
    if(star){if(p!=0){p=0;c.picture=ChasePictureStar;}}
    else if(bullet){if(p!=1){p=1;c.picture=ChasePictureBullet;}}
    else if(mega){if(p!=2){p=2;c.picture=ChasePictureMega;}}
    else if(cloud&&p!=3){p=3;c.picture=ChasePictureCloud;}
}
// UIAnimator group calc (fn_1_12C830 0x8063CBE4): the frame advances by the group speed;
// a looping clip wraps, an ended one starts its BRCTR next clip at frame 0.
void chaseAdvance(const ChaseClip* clips,int& clip,float& frame,float speed){
    frame+=speed;
    const auto& c=clips[clip];
    if(frame<c.frames)return;
    if(c.loop){while(frame>=c.frames)frame-=c.frames;}
    else if(c.next>=0){clip=c.next;frame=0;}
    else frame=c.frames;
}
float chaseSample(const ChaseClip& c,float frame){return flowSample(ChaseKeys+c.first,c.count,c.reverse?c.frames-frame:frame,false);}
// Player 0's BalloonManager (0x807F132C; items register through fn_1_2E1A00 0x807F1DB4)
// and CtrlRaceItemBalloon::calcSelf, then its animator: once per 60 Hz step.
void stepChase(ChaseIcon& c,const RaceItems& items,const HudCamera& camera){
    namespace K=Kinoko;using St=K::Kart::eStatus;
    c.alarm=false;c.opened=-1;
    const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
    // The time-trial race page (control mask 0x116E) has no CtrlRaceItemBalloon (0x800).
    if(scenario.isTimeTrial())return;
    auto* race=K::System::RaceManager::Instance();auto* karts=K::Kart::KartObjectManager::Instance();
    const unsigned count=std::min<unsigned>(scenario.playerCount,12);
    const auto& me=karts->object(0)->status();
    // Items register as they update (Red fn_1_29A4C8 0x807AA87C, Blue fn_1_29CD64 0x807AD118,
    // both -> fn_1_34869C), by object type then slot. The first homing on the racer
    // (+0xbc: a Red's +0x280; a Blue's once locked, +0x2a8) ends it; Blue Shells also keep
    // the nearest (+0xb8, +0xb4) by render-group camera distance |pos - eye|^2 *
    // (min(fovy,55)/55)^2 (fn_1_2773C0 0x80787774, fn_1_276CC8 0x8078707C).
    const auto& actors=items.actors();
    const float fovScale=std::min(camera.fov,55.f)/55.f;
    int target=-1,nearBlue=-1;float nearest=3.40282347e38f; // 0x808A9E38
    for(ItemKind kind:{ItemKind::RedShell,ItemKind::BlueShell})
        for(unsigned i=0;i<actors.size()&&target<0;++i){
            const auto& a=actors[i];if(!a.active||a.kind!=kind)continue;
            const Vec3 d=a.visualPosition()-camera.eye;const float distance=dot(d,d)*fovScale*fovScale;
            if(a.target==0&&(kind==ItemKind::RedShell||(a.guidance.phase>=2&&a.guidance.targetLive))){target=int(i);nearest=distance;}
            else if(kind==ItemKind::BlueShell&&distance<nearest){nearest=distance;nearBlue=int(i);}
        }
    // BalloonManager_calc: from race frame 420 (0x808D3E10) until stage 4, the racer nearest
    // the camera with Star, Mega, Bullet Bill or a Thunder Cloud (KartState bits 63, 79, 91,
    // 93) whose kart + (0,120,0) is behind it (view z > 0); while looking back, one on screen
    // up to 7500 in front (battle 75000: CtrlRaceItemBalloon_load's 0x808D3E14).
    int racer=-1;float racerZ=0;
    const unsigned stage=unsigned(race->stage());
    if(stage>=2&&stage<4&&race->timer()>=420&&!me.onBit(St::BeforeRespawn)){
        const Vec3 back=normalized(camera.eye-camera.target,{0,0,1}),right=normalized(cross(camera.up,back),{1,0,0});
        const float range=scenario.gameMode==K::System::RaceConfig::Scenario::GameMode::Battle?75000.f:7500.f;
        float best=3.40282347e38f;
        for(unsigned i=1;i<count;++i){
            if(race->player(i).finished())continue;
            const auto* kart=karts->object(i);const auto& s=kart->status();
            if(!s.onBit(St::Star)&&!s.onBit(St::Mega)&&!s.onBit(St::Bullet)&&!s.onBit(St::ThunderCloud))continue;
            const auto& p=kart->pos();const Vec3 d=Vec3{p.x,p.y+120.f,p.z}-camera.eye;
            const float z=dot(d,back);
            if(camera.lookBack?!(z>=-range&&z<=0&&std::fabs(chaseX({dot(d,right),0,z},camera.fov))<=ChaseHalfWidth):!(z>0))continue;
            if(dot(d,d)<best){best=dot(d,d);racer=int(i);racerZ=z;}
        }
    }
    // calcSelf: closed once the racer finishes and in the respawn fall (bit 4). Its other
    // test, the race page's *0x809C4758 +0x38B, has no port state.
    const bool mirror=scenario.mirror; // RaceConfig +0xB90 bit 0
    const int was=c.fade;
    if(race->player(0).finished()||me.onBit(St::BeforeRespawn))chaseClose(c);
    else{
        const bool shown=c.fade!=0;c.hidden=!shown;
        if(target>=0){
            const auto& a=actors[unsigned(target)];
            chaseOpen(c);chaseItem(c,a,a.kind==ItemKind::BlueShell,camera,mirror);chaseItemPicture(c,target,a.kind);
        }else if(racer>=0){
            const auto* kart=karts->object(unsigned(racer));const auto& s=kart->status();const auto& p=kart->pos();
            chaseOpen(c);chaseRacer(c,{p.x,p.y,p.z},racerZ,camera,mirror);
            chaseRacerPicture(c,s.onBit(St::Star),s.onBit(St::Bullet),s.onBit(St::Mega),s.onBit(St::ThunderCloud));
        }else if(nearBlue>=0&&nearest<1e7f){ // 0x808A9ED0
            chaseOpen(c);chaseItem(c,actors[unsigned(nearBlue)],false,camera,mirror);chaseItemPicture(c,nearBlue,ItemKind::BlueShell);
        }else if(shown)chaseClose(c);
        // Look-behind (camera flag 0x20) hides it at once.
        if(camera.lookBack){c.hidden=true;c.fade=0;c.fadeFrame=0;}
    }
    if(was==0&&c.fade==1){c.opened=c.picture;++c.shown[c.picture];}
    // LayoutUIControl_calc (0x8063DB00) then runs the animator (fn_1_12C408 0x8063C7BC):
    // eFade at 0.75 (init 0x807F2118, 0x808A9ECC), roop and zoom at their rates.
    chaseAdvance(ChaseFade,c.fade,c.fadeFrame,.75f);
    int loop=0;chaseAdvance(ChaseRoop,loop,c.roopFrame,c.roopRate);chaseAdvance(ChaseZoom,loop,c.zoomFrame,c.zoomRate);
}
}
struct RaceHud::Impl {
    std::array<std::unique_ptr<Model>,21> models;
    std::array<Vec3,12> bulletUp{};
    std::array<bool,12> wasBullet{};
    std::array<ThunderCloudVisual,12> clouds{};
    std::array<uint16_t,12> previousCloud{};
    std::array<std::unique_ptr<DriverAnimation>,21> animations;
    // Per-object CHR0 frames (retail AnmObj frame, advanced by its rate each update): each
    // Blue Shell's "fly" by actor slot, each racer's Bullet "wait". GPU-skinned banks draw
    // every object at its own frame.
    struct Wings { float frame=0;uint16_t age=0;bool live=false; };
    std::array<Wings,RaceItems::MaxActors> blueWings{};
    std::array<float,12> bulletFrame{};
    int blueFly=0,bulletWait=0;
    std::unique_ptr<Model> box;
    std::unique_ptr<Model> bombCore;
    // Retail drop-shadow volumes (tools/cook_item_shadows.py); optional, as older SD cards
    // lack them. `shadows` is reused for the frame's single stencil batch.
    std::unique_ptr<Model> shadowKoura,shadowBanana,shadowKumo,boxShadow;
    std::vector<Renderer::ShadowVolume> shadows;
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
    ChaseIcon chase;
    std::array<Texture,ChaseTextures.size()> chaseTextures;
};
RaceHud::RaceHud():impl_(std::make_unique<Impl>()){}
RaceHud::~RaceHud()=default;
void RaceHud::reset(){impl_=std::make_unique<Impl>();}
bool RaceHud::load(){
    reset();bool valid=true;
    auto model=[](const char* path){auto m=std::make_unique<Model>();if(!path||!m->load(path))m.reset();return m;};
    impl_->box=model(ItemBoxModel);valid&=bool(impl_->box);
    impl_->shadowKoura=model("models/item_shadow_koura.gmdl");impl_->shadowBanana=model("models/item_shadow_banana.gmdl");
    impl_->shadowKumo=model("models/item_shadow_kumo.gmdl");impl_->boxShadow=model("models/item_box_shadow.gmdl");
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
            // Blue Shells and Bullets animate per object (several at once, each from its own
            // start): skinned on the GPU with one palette per draw. A rejected GPU setup keeps
            // the CPU skinning above (one shared pose).
            if((i==7||i==15)&&impl_->animations[i])impl_->animations[i]->prepareLitSkinning(*impl_->models[i]);
        }
    }
    if(impl_->animations[7])impl_->blueFly=std::max(0,impl_->animations[7]->find("fly"));
    if(impl_->animations[15])impl_->bulletWait=std::max(0,impl_->animations[15]->find("wait"));
    if(impl_->models[3])bakeMaskUv(*impl_->models[3],FakeBoxMaterials);
    for(unsigned i=1;i<=12;++i)valid&=impl_->ranks[i].load(HudRanks[i].path);
    for(unsigned i=0;i<10;++i)valid&=impl_->digits[i].load(HudDigits[i].path);
    for(unsigned i=0;i<HudExtraCount;++i)valid&=impl_->extras[i].load(HudExtras[i].path);
    for(unsigned i=0;i<BalloonNames.size();++i)valid&=impl_->names[i].load(BalloonNames[i].path);
    valid&=impl_->balloonLine.load(BalloonLinePath);
    for(unsigned i=0;i<ChaseTextures.size();++i)valid&=impl_->chaseTextures[i].load(ChaseTextures[i]);
    return valid;
}
bool RaceHud::chaseAlarm(float& x,float& rate) const{const auto& c=impl_->chase;x=c.alarmX;rate=c.alarmRate;return c.alarm;}
int RaceHud::chaseOpened() const{return impl_->chase.opened;}
const std::array<uint16_t,21>& RaceHud::chaseShown() const{return impl_->chase.shown;}
void RaceHud::stepWorld(const RaceItems& items,Vec3 cameraBackward,bool started,const HudCamera* camera){
    for(unsigned i=0;i<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++i){
        const auto& effect=items.hud(i);
        auto* kart=Kinoko::Kart::KartObjectManager::Instance()->object(i);
        if(effect.bullet){
            const auto& sourceUp=kart->move()->smoothedUp();const Vec3 up{sourceUp.x,sourceUp.y,sourceUp.z};
            impl_->bulletUp[i]=impl_->wasBullet[i]?normalized(lerp(impl_->bulletUp[i],up,BulletUpSmoothing)):up;
        }
        // The Bullet's start (fn_1_8B404 0x8059B7B8) plays kart_killer "wait" (its only clip,
        // a loop) at frame 0, rate 1 (0x80891FA8 / 0x80891FAC).
        impl_->bulletFrame[i]=effect.bullet&&impl_->wasBullet[i]?impl_->bulletFrame[i]+1:0;
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
    // Blue Shell wings: togezo_koura CHR0 "fly", anim 0 of ItemObjBlue's table (0x808D22F0:
    // fly, wait). Its release (vtable 0x808D23D8 +0x20, fn_1_29C280 0x807AC634) and launch
    // (+0x30, fn_1_29E338 0x807AE6EC) play it at frame 0, rate 1 (fn_1_47018 0x805573CC: AnmObj
    // SetFrame 0x808A5AF8, SetUpdateRate 0x808A5AFC). State 4's enter (fn_1_29D7A0 0x807ADB54)
    // sets the rate to 1.6 (0x808A5C5C), state 5's (the dive, fn_1_29DB60 0x807ADF14) to 0; no
    // other Blue state touches it. The guidance phase is that state index.
    for(unsigned k=0;k<RaceItems::MaxActors;++k){
        const auto& a=items.actors()[k];auto& wings=impl_->blueWings[k];
        if(!a.active||a.kind!=ItemKind::BlueShell||a.explosion){wings.live=false;continue;}
        if(!wings.live||a.age<wings.age){wings.frame=0;wings.live=true;}
        else wings.frame+=a.guidance.phase==5?0.f:a.guidance.phase==4?1.6f:1.f;
        wings.age=a.age;
    }
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
    if(camera)stepChase(impl_->chase,items,*camera);
}
void RaceHud::drawBombBlast(Renderer& renderer,const C3D_Mtx& t,unsigned age) const{
    auto& model=impl_->bombCore;if(!model)return;
    Vertex* originalVertices=model->vertices;
    if(impl_->bombFrames)model->vertices=impl_->bombFrames+std::min(age,impl_->bombFrameCount-1)*model->vertexCount;
    if(impl_->blastMaterials)drawBlast(renderer,*model,t,age,false);
    else renderer.draw(*model,t);
    model->vertices=originalVertices;
}
// Retail drop shadows are closed volumes in the shadow pass of the karts and course objects
// (ItemObj fn_1_28FC8C 0x807A0040 and ObjItemBox register with the same manager, 0x809C1B40),
// hence the same .5. RaceGfxManager_vf_10 (0x805B3B00) draws that manager's list +0x14
// (0x805B8E70) as GfxManager group 9: category 2 (0x808B6ED0), opaque priority 0x34 + 1 =
// 0x35 (fn_8023BB18 0x8023BC68, fn_8023C400). The course model (group 6, CourseModel ctor
// 0x8078E404) draws at 0x1B + 0xA = 0x25, the karts (group 10) at 0x4E and the item objects
// (group 11, fn_1_28FC8C) at 0x51: the volumes shade the course, never a kart or an item.
// One stencil batch: each volume encloses its caster.
void RaceHud::drawShadows(Renderer& renderer,const RaceItems& items,Vec3 viewer){
    if(renderQuality().shadowVolumes){
        auto& volumes=impl_->shadows;volumes.clear();
        auto add=[&](const std::unique_ptr<Model>& m,const C3D_Mtx& t){if(m&&renderer.visible(*m,t))volumes.push_back({m.get(),t});};
        // ObjItemBox (0x80828AE0-0x80828C14): itembox-shadow at the box's rest position, without
        // the appearance rise, with the box's tumble, its world Y row x4 (0x808AC700) and scale.
        if(impl_->boxShadow)for(const auto& box:items.boxes())if(box.active&&dot(box.position-viewer,box.position-viewer)<20000.f*20000.f){
            const auto pose=itemBoxPose(box);
            C3D_Mtx t;Mtx_Identity(&t);Mtx_Translate(&t,box.position.x,box.position.y,box.position.z,true);
            Mtx_Scale(&t,1,4,1);Mtx_RotateZ(&t,pose.angle,true);Mtx_RotateX(&t,pose.angle,true);
            Mtx_Scale(&t,pose.scale.x,pose.scale.y,pose.scale.z);
            add(impl_->boxShadow,t);
        }
        // ItemObj (fn_1_290404 0x807A07B8 -> fn_1_2906B4 0x807A0A68): koura_s or banana_s with
        // the model's matrix, x and z scaled by +0x6c; a Fake Item Box's stays upright (makeT,
        // fn_1_297C38 0x807A8108). Held, thrown and dropped objects alike.
        auto itemShadow=[&](ItemKind kind,const HeldItemVisual& v,Vec3 p){
            const float w=itemShadowWidth(kind);
            if(!w||v.scale.x<=0||v.scale.y<=0||v.scale.z<=0)return;
            const auto& m=kind==ItemKind::Banana?impl_->shadowBanana:impl_->shadowKoura;
            if(kind==ItemKind::FakeBox)add(m,axesTransform({v.scale.x*w,0,0},{0,v.scale.y,0},{0,0,v.scale.z*w},p));
            else add(m,axesTransform(v.right*(v.scale.x*w),v.up*v.scale.y,v.forward*(v.scale.z*w),p));
        };
        for(const auto& actor:items.actors())if(actor.active&&!actor.explosion&&unsigned(actor.kind)<21&&impl_->models[unsigned(actor.kind)]&&
            dot(actor.position-viewer,actor.position-viewer)<22000.f*22000.f){
            auto v=releasedItemVisual(actor);Vec3 p=actor.visualPosition();
            if(actor.kind==ItemKind::FakeBox)v.scale={actor.scale,actor.scale,actor.scale};
            if(actor.kind==ItemKind::BlueShell&&actor.guidance.phase<4){
                // In flight (+0x78 bits 0x4000 and 0x4000000, 0x807AC518) the volume stands at
                // the object's height, not the drawn one, and keeps only the yaw (0x807A0C04).
                p=actor.position;
                if(v.up.z<.99f){
                    const float xz=v.right.z,zz=v.forward.z,n=xz*xz+zz*zz,r=n>0?1/std::sqrt(n):0;
                    v.right=n>0?Vec3{zz*r,0,xz*r}:Vec3{1,0,0};
                    v.forward=n>0?Vec3{-xz*r,0,zz*r}:Vec3{0,0,1};v.up={0,1,0};
                }
            }
            itemShadow(actor.kind,v,p);
        }
        for(unsigned i=0;i<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++i){
            const auto& effect=items.hud(i);
            if(effect.deployed){
                const ItemKind kind=effect.heldKind==ItemKind::TripleGreenShell?ItemKind::GreenShell:effect.heldKind==ItemKind::TripleRedShell?ItemKind::RedShell:
                    effect.heldKind==ItemKind::TripleBanana?ItemKind::Banana:effect.heldKind;
                if(impl_->models[unsigned(kind)])for(unsigned n=0;n<3;++n)if(effect.heldMask&(1u<<n))itemShadow(kind,items.heldItemVisual(i,n),items.heldItemPosition(i,n));
            }
            if(effect.cloud&&impl_->models[14]){
                // fn_1_29EFE8 (0x807AF4C8): kumo_s upright (makeT), 280 minus the cloud's height
                // +0x1EC (365 on one screen, 0x808A5F34) from the cloud.
                Vec3 p=impl_->clouds[i].modelPosition();p.y+=280.f-365.f;
                const float w=itemShadowWidth(ItemKind::ThunderCloud);
                add(impl_->shadowKumo,axesTransform({w,0,0},{0,1,0},{0,0,w},p));
            }
        }
        renderer.drawShadowVolumes(volumes.data(),unsigned(volumes.size()),.5f);
    }
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
        if(i==7||i==15){
            // GPU-skinned banks are posed per object where drawn. The CPU fallback shares
            // one pose: the first object's.
            auto& bank=*impl_->animations[i];if(bank.gpuSkinned())continue;
            float frame=0;
            if(i==7){for(const auto& wings:impl_->blueWings)if(wings.live){frame=wings.frame;break;}}
            else for(unsigned p=0;p<Kinoko::System::RaceConfig::Instance()->raceScenario().playerCount;++p)
                if(items.hud(p).bullet){frame=impl_->bulletFrame[p];break;}
            bank.apply(*impl_->models[i],i==7?impl_->blueFly:impl_->bulletWait,frame,tick);
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
        if(ratio<quality.minRatio())continue;
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
            else if(auto* wings=!blast&&actor.kind==ItemKind::BlueShell?impl_->animations[7].get():nullptr;wings&&wings->gpuSkinned()){
                // This shell's own wing frame (stepWorld); one clip, so pose() never blends.
                const auto& own=impl_->blueWings[&actor-items.actors().data()];
                renderer.setSkinPalette(wings->pose(impl_->blueFly,own.live?own.frame:0.f,tick),wings->matrixCount(),21);
                renderer.draw(*model,t);
                renderer.setSkinPalette(nullptr,0,12);
            }
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
            // This racer's own "wait" frame since its Bullet started (stepWorld).
            auto* arms=impl_->animations[15].get();const bool skinned=arms&&arms->gpuSkinned();
            if(skinned)renderer.setSkinPalette(arms->pose(impl_->bulletWait,impl_->bulletFrame[i],tick),arms->matrixCount(),21);
            renderer.draw(*impl_->models[15],t);
            if(skinned)renderer.setSkinPalette(nullptr,0,12);
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
            const auto visual=items.heldItemVisual(i,n);
            // Retail spins only the triple orbit (fn_1_2842E0 0x80794694, carried by
            // heldItemVisual); a single trailed shell keeps the trail's axes
            // (fn_1_2831B8 0x8079356C -> fn_1_280F2C 0x807912E0): it does not spin.
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
                if(scenario.players[i].type==Kinoko::System::RaceConfig::Player::Type::Ghost)continue;
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
    // CtrlRaceItemBalloon (chase_icon_1) draws in the name balloons' race-page pass, before
    // the HUD panes. chase_icon_null: the control origin + (x, 0), scale 0.8 with one screen
    // (init 0x807F2118, 0x808A9EC4), its eFade alpha passed to base, then the hilight turned
    // by roop (NW4R rotation is counter-clockwise), then the picture scaled by zoom.
    if(const auto& c=impl_->chase;!c.hidden){
        const float alpha=std::clamp(chaseSample(ChaseFade[c.fade],c.fadeFrame)/255.f,0.f,1.f);
        constexpr float Unit=240.f/456.f; // the race HUD's layout unit -> top-screen px
        const float sx=.8f*ChaseControl[2]*Unit,sy=.8f*ChaseControl[3]*Unit;
        const float cx=200.f+(ChaseControl[0]+c.paneX*ChaseControl[2])*Unit,cy=120.f-ChaseControl[1]*Unit;
        auto picture=[&](const Texture& texture,const ChasePicture& p,float u,float v,float scale,float angle){
            const float hw=p.w*scale*.5f,hh=p.h*scale*.5f,co=std::cos(angle),si=std::sin(angle);
            const uint32_t color=0xffffff00u|uint32_t(std::lround(alpha*255.f));
            Renderer::MaterialQuad q{};
            for(unsigned k=0;k<4;++k){ // TL, TR, BL, BR, screen y down
                const float lx=k&1?hw:-hw,ly=k&2?hh:-hh;
                q.x[k]=cx+(lx*co-ly*si)*sx;q.y[k]=cy+(lx*si+ly*co)*sy;q.u[k]=k&1?u:0.f;q.v[k]=k&2?v:0.f;q.color[k]=color;
            }
            renderer.drawScreenMaterialQuads(&texture,&q,1,p.color0,p.color1,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        };
        if(alpha>0){
            picture(impl_->chaseTextures[ChaseBase.texture],ChaseBase,1,1,1,0);
            picture(impl_->chaseTextures[ChaseHilight.texture],ChaseHilight,1,1,1,-chaseSample(ChaseRoop[0],c.roopFrame)*float(M_PI)/180.f);
            picture(impl_->icons[c.picture],ChaseItem,HudItems[c.picture].u,HudItems[c.picture].v,chaseSample(ChaseZoom[0],c.zoomFrame),0);
        }
    }
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
    // CtrlRaceRankNum_1_0 (y -150, scale 1.6) leaves downward. Drop shadow
    // position_sha (game_image_position.brlyt): drawn first, a race_null child
    // at (-6,-7), pane alpha 150, material C0/C1 black, same texture pattern;
    // it spins and scales with race_null and slides with it (rest offset).
    const float cx=HudRank.x+HudRank.w*.5f,restY=HudRank.y+HudRank.h*.5f,cy=restY+exitLift(1.6f);
    if(scale>0.01f){
        const float sdx=HudRankShadow.x+HudRankShadow.w*.5f-cx,sdy=HudRankShadow.y+HudRankShadow.h*.5f-restY;
        const float c=std::cos(angle),sn=std::sin(angle),sx=(sdx*c-sdy*sn)*scale,sy=(sdx*sn+sdy*c)*scale;
        const float w=HudRank.w*scale,h=HudRank.h*scale;
        renderer.drawScreenSpriteRegion(impl_->ranks[shown],cx+sx-w*.5f,cy+sy-h*.5f,w,h,0,0,art.u,art.v,150.f/255.f,0x000000ffu,nullptr,false,angle);
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
