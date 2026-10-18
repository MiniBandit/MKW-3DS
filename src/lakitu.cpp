#include <mkw/lakitu.hpp>
#include <mkw/lakitu_assets.hpp>
#include <mkw/renderer.hpp>
#include <mkw/animation.hpp>
#include <game/field/jugem/JugemMove.hh>
#include <game/kart/KartObject.hh>
#include <game/kart/KartMove.hh>
#include <game/system/RaceManager.hh>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
namespace mkw {
namespace {
using Kinoko::EGG::Vector3f;
namespace KK=Kinoko::Kart;
constexpr float Pi=3.14159265f;
Vec3 vec(const Vector3f& v){return {v.x,v.y,v.z};}
Vector3f egg(Vec3 v){return Vector3f(v.x,v.y,v.z);}
void store(C3D_Mtx& out,const float* m){Mtx_Identity(&out);for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)out.r[r].c[3-c]=m[r*4+c];}
// Model-space attachment joints per clip frame (tools/export_joints.ts, DJNT v1).
struct Joints {
    struct Clip{char name[32];unsigned duration=0,loop=0;size_t offset=0;};
    std::vector<float> data;std::vector<Clip> clips;unsigned joints=0;
    bool load(const char* path){
        data.clear();clips.clear();
        FILE* f=std::fopen(path,"rb");if(!f)return false;
        uint32_t h[4];bool ok=std::fread(h,4,4,f)==4&&!std::memcmp(h,"DJNT",4)&&h[1]==1&&h[2]&&h[2]<=8&&h[3]&&h[3]<=16;
        if(ok){joints=h[2];ok=!std::fseek(f,long(32*joints),SEEK_CUR);clips.resize(h[3]);}
        for(auto& c:clips){
            uint32_t d[2];ok=ok&&std::fread(c.name,1,32,f)==32&&std::fread(d,4,2,f)==2&&d[0]&&d[0]<=1000;if(!ok)break;
            c.name[31]=0;c.duration=d[0];c.loop=d[1];c.offset=data.size();
            const size_t n=size_t(d[0])*joints*12;data.resize(data.size()+n);ok=std::fread(data.data()+c.offset,4,n,f)==n;
        }
        std::fclose(f);if(!ok){data.clear();clips.clear();}return ok;
    }
    int find(const char* name) const {for(unsigned i=0;i<clips.size();++i)if(!std::strcmp(clips[i].name,name))return int(i);return 0;}
    const float* at(int clip,unsigned joint,unsigned frame) const {
        const auto& c=clips[clip];frame=c.loop?frame%c.duration:std::min(frame,c.duration-1);
        return data.data()+c.offset+(size_t(frame)*joints+joint)*12;
    }
};
// JugemInterp (USA 0x8074C1E0): one sine-eased keyframe segment.
struct Interp {
    float time=0,duration=1,rate=0;
    void init(float d){time=0;duration=d;rate=0;}
    bool calc(){const float t=std::clamp(time/duration,0.f,1.f);rate=std::clamp(.5f*(1.f+std::sin(-.5f*Pi+t*Pi)),0.f,1.f);time+=1.f;const bool done=time>duration;if(done)time=duration;return done;}
};
enum class Mode{Away,Start,Lap,Reverse,Rescue,Goal};
enum class Phase{Drop,Arc,Stay,Ascend};
enum Attachment{None,Signal,LapBoard,LapFinal,Reverse,Flag};
// Copied with the draw: the lamp colours by value.
struct Binding{const Model* model;std::array<uint32_t,6> lamps;bool signal;int digit;};
// jugemu_signal: light_N out=(C2+TEXC*RAS-0.5)*2; lightbN lerp(C2,TEXC,C2), alpha A2*TEXA.
// jugemu_lap 'number': PAT0 digit texture. Everything else: TEXC*RAS.
void bindLakitu(void* context,std::size_t submesh){
    const auto& b=*static_cast<const Binding*>(context);
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e0,C3D_Both,GPU_MODULATE);
    if(b.signal)for(unsigned i=0;i<LakituLampSubmesh.size();++i)if(submesh==LakituLampSubmesh[i]){
        const uint32_t c=b.lamps[i],pica=(c>>24)|((c>>8)&0xff00u)|((c<<8)&0xff0000u)|((c&0xffu)<<24);
        if(i<3){
            C3D_TexEnvColor(e1,pica|0xff000000u);
            C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_RGB,GPU_ADD_SIGNED);C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
        }else{
            C3D_TexEnvColor(e0,pica);
            C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
            C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_MODULATE);
        }
    }
    if(b.digit>=0&&submesh==LakituLapNumberSubmesh){
        const int texture=LakituLapDigit[std::clamp(b.digit,0,int(LakituLapDigit.size())-1)];
        if(texture<int(b.model->textures.size())){auto* t=&b.model->textures[texture]->gpu;C3D_TexSetWrap(t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);C3D_TexBind(0,t);}
    }
}
}
struct Lakitu::Impl {
    struct Part {std::unique_ptr<Model> model;std::unique_ptr<DriverAnimation> bank;bool rigid=false;};
    Part body,rod,flag,signal,lap,lapFinal,reverse;
    Joints bodyJoints,rodJoints;
    bool ready=false;
    const KK::KartObject* kart=nullptr;
    std::unique_ptr<Kinoko::Field::JugemMove> move;
    Mode mode=Mode::Away;Phase phase=Phase::Drop;Interp interp;
    unsigned phaseTimer=0,ascendTimer=0,introFrames=0,lastLap=1,tick=0;
    bool started=false,wasRespawning=false,wasFinished=false,released=false;
    bool visible=true; // LakituInput::visible of the last step
    float reverseActivation=0;bool reverseOn=false;
    // Direct placement (pickup, goal): position, facing, oscillation phases.
    Vec3 pos{},goalFrom{},goalTo{},forward{0,0,1};float phaseX=0,phaseY=0;
    Vector3f anchor; // JugemMove +0x1c as last set here (the rescue ascent adds to it)
    C3D_Mtx world{};
    Attachment attachment=None;int digit=-1;std::array<uint32_t,6> lamps{};
    const char* bodyClip="wait";const char* rodClip="wait";unsigned clipStart=0;int clipFrame=-1;
    Vec3 cameraFrame(Vec3 forwardH,Vec3 local) const {
        // USA 0x8071ECA4: yaw-only basis from the camera, at the kart.
        const Vec3 up{0,1,0},side=normalized(cross(up,forwardH),{1,0,0});
        return vec(kart->pos())+side*local.x+up*local.y+forwardH*local.z;
    }
    // Rescue target (PAL 0x807242F4 / 0x80724430): the kart's full 3-D forward (mainRot * ez)
    // times 180, plus 100 up, from the kart.
    Vector3f rescueTarget() const {
        Vector3f local=kart->mainRot().rotateVector(Vector3f::ez);
        local.x=local.x*180.f;local.z=local.z*180.f;local.y=local.y*180.f+100.f;
        return kart->pos()+local;
    }
    Vec3 upright(Vec3 local) const {
        // JugemUnit::transformLocalToWorldUpright (USA 0x807230D4).
        Vector3f dir=kart->move()->vel1Dir()+kart->mainRot().rotateVector(Vector3f::ez);dir.normalise2();
        Vector3f right=Vector3f::ey.cross(dir);right.normalise2();
        if(right.squaredLength()<=1e-7f)right=Vector3f::ex;
        Vector3f forwardV=right.cross(Vector3f::ey);forwardV.normalise2();
        return vec(kart->pos()+right*local.x+Vector3f::ey*local.y+forwardV*local.z);
    }
    void clips(const char* b,const char* r,int frame=-1){if(std::strcmp(b,bodyClip)||std::strcmp(r,rodClip)){bodyClip=b;rodClip=r;clipStart=tick;}clipFrame=frame;}
    void begin(Mode m,Phase p,float duration,Vec3 at){
        mode=m;phase=p;interp.init(duration);phaseTimer=ascendTimer=0;
        move->init();move->setPos(egg(at),true);move->setForwardFromKartObjMainRot(true);move->setDescending(false);
    }
    void ascend(float duration){phase=Phase::Ascend;interp.init(duration);move->setDescending(false);ascendTimer=0;}
    bool rise(){
        const bool done=interp.calc();
        move->setRiseVel(++ascendTimer<10?Vector3f(0,30,0):Vector3f(0,80,0));move->setRising(true);
        return done;
    }
    void hold(Vec3 anchor){
        move->setAwayOrDescending(false);move->setAnchorPos(egg(anchor));move->setForwardFromKartObjPosDelta(false);
    }
    void glide(Vec3 anchor){
        move->setAwayOrDescending(true);move->setAnchorPos(egg(anchor));move->setForwardFromKartObjMainRot(true);
    }
    void place(Vec3 at,Vec3 face,bool oscillate){
        // JugemMove's yaw basis and (0,40,0) body offset, without the kart follow.
        forward=normalized(lerp(forward,normalized(face,forward),.08f),forward);
        const Vec3 up{0,1,0},right=normalized(cross(up,forward),{1,0,0}),f=cross(right,up);
        Vec3 p=at;
        if(oscillate){phaseX=std::fmod(phaseX+.04f,2*Pi);phaseY=std::fmod(phaseY+.08f,2*Pi);p=p+right*(80.f*std::cos(phaseX))+up*(30.f*std::sin(phaseY));}
        p=p+up*40.f;
        const float m[12]={right.x,up.x,f.x,p.x,right.y,up.y,f.y,p.y,right.z,up.z,f.z,p.z};store(world,m);
    }
};
Lakitu::Lakitu():impl_(std::make_unique<Impl>()){}
Lakitu::~Lakitu()=default;
bool Lakitu::load(){
    auto& m=*impl_;reset();if(m.ready)return true;
    auto part=[](Impl::Part& p,const char* model,const char* bank){
        p.model=std::make_unique<Model>();if(!p.model->load(model)){p.model.reset();return false;}
        if(!bank)return true;
        p.bank=std::make_unique<DriverAnimation>();if(!p.bank->load(bank,*p.model)){p.bank.reset();p.model.reset();return false;}
        p.rigid=p.bank->makeRigid(*p.model);return true;
    };
    m.ready=part(m.body,LakituBody,LakituBodyBank)&&part(m.rod,LakituRod,LakituRodBank)&&part(m.flag,LakituFlag,LakituFlagBank)&&
            part(m.signal,LakituSignal,nullptr)&&part(m.lap,LakituLap,nullptr)&&part(m.lapFinal,LakituLapFinal,nullptr)&&
            part(m.reverse,LakituReverse,nullptr)&&m.bodyJoints.load(LakituBodyJoints)&&m.rodJoints.load(LakituRodJoints)&&
            m.bodyJoints.joints>=2&&m.rodJoints.joints>=1;
    if(!m.ready)unload();
    reset();return m.ready;
}
void Lakitu::unload(){
    auto& m=*impl_;
    for(auto* p:{&m.body,&m.rod,&m.flag,&m.signal,&m.lap,&m.lapFinal,&m.reverse}){p->bank.reset();p->model.reset();p->rigid=false;}
    m.bodyJoints.data.clear();m.rodJoints.data.clear();m.ready=false;m.move.reset();m.kart=nullptr;
}
void Lakitu::reset(){
    auto& m=*impl_;m.mode=Mode::Away;m.move.reset();m.kart=nullptr;m.introFrames=0;m.lastLap=1;m.started=false;
    m.wasRespawning=m.wasFinished=false;m.reverseActivation=0;m.reverseOn=false;m.attachment=None;m.tick=0;m.visible=true;
}
void Lakitu::step(const LakituInput& in){
    auto& m=*impl_;m.visible=in.visible;if(!m.ready||!in.kart)return;
    if(in.kart!=m.kart){m.kart=in.kart;m.move=std::make_unique<Kinoko::Field::JugemMove>(m.kart);m.move->init();m.mode=Mode::Away;}
    ++m.tick;
    const auto& status=m.kart->status();
    const Vec3 kartPos=vec(m.kart->pos());
    const Vec3 forwardH=normalized(Vec3{in.cameraForward.x,0,in.cameraForward.z},{0,0,1});
    // JugemSwitchReverse::calc.
    if(in.stage>=2&&!in.finished&&Kinoko::System::RaceManager::Instance()->player().drivingWrongWay()){
        if(status.offBit(KK::eStatus::InAction))m.reverseActivation+=1.f/60.f;
        if(m.reverseActivation>1.f&&status.offBit(KK::eStatus::BeforeRespawn)){m.reverseActivation=1.f;m.reverseOn=true;}
    }else{m.reverseActivation-=1.f/60.f;if(m.reverseActivation<0.f){m.reverseActivation=0.f;m.reverseOn=false;}}
    const bool respawning=status.onBit(KK::eStatus::InRespawn);
    // Triggers, highest priority first.
    if(in.stage==0&&!in.introHeld&&!m.started&&++m.introFrames>=172-120){
        // Fly in over the last 120 intro ticks; jg_cd starts with the countdown.
        m.started=true;m.begin(Mode::Start,Phase::Drop,120,m.cameraFrame(forwardH,{0,2000,10}));
        m.attachment=Signal;m.digit=-1;m.clips("jg_ww","jg_ww");
    }else if(respawning&&!m.wasRespawning){
        // JugemUnit rescue enter (PAL 0x807242F4): JugemMove starts at the rescue target,
        // gliding (+0x140), not descending (+0x148), facing from the kart's main rotation.
        m.mode=Mode::Rescue;m.phase=Phase::Stay;m.phaseTimer=0;m.attachment=None;m.clips("wait","pickup");
        m.move->init();m.move->setPos(m.rescueTarget(),true);m.move->setAwayOrDescending(true);
        m.move->setForwardFromKartObjMainRot(true);m.move->setDescending(false);m.anchor=Vector3f::zero;
    }else if(in.finished&&!m.wasFinished){
        m.mode=Mode::Goal;m.phase=Phase::Drop;m.interp.init(60);m.attachment=Flag;m.forward=forwardH*-1.f;m.clips("jg_wy","wait");
        m.goalFrom=m.cameraFrame(forwardH,{0,2000,700});m.goalTo=m.cameraFrame(forwardH,{0,120,700});m.pos=m.goalFrom;
    }else if(in.lap!=m.lastLap&&in.lap>=2&&in.lap<=in.lapCount&&!in.finished&&m.mode!=Mode::Start&&m.mode!=Mode::Rescue){
        m.begin(Mode::Lap,Phase::Drop,10,m.cameraFrame(forwardH,{-10,2000,0}));
        m.attachment=in.lap==in.lapCount?LapFinal:LapBoard;
        constexpr int frames[6]={0,0,0,1,2,3};m.digit=frames[std::min(in.lap,5u)];m.clips("wait","wait");
    }else if(m.mode==Mode::Away&&m.reverseOn){
        // JugemUnit::enterReverse.
        m.begin(Mode::Reverse,Phase::Drop,40,m.upright({0,2000,0}));m.move->setForwardFromKartObjPosDelta(true);
        m.attachment=Reverse;m.digit=-1;m.clips("jg_no","jg_no");
    }
    m.wasRespawning=respawning;m.wasFinished=in.finished;m.lastLap=in.lap;
    auto& move=*m.move;
    switch(m.mode){
    case Mode::Away:break;
    case Mode::Start:{
        constexpr Vec3 spawn{0,2000,10},stay{0,250,320},exit{0,2000,650};
        // jg_cd from countdown timer 11; a played-once clip then holds its last frame.
        const int countdown=lakituCountdownFrame(in.stage,in.timer);
        if(countdown>=0&&std::strcmp(m.bodyClip,"jg_cd"))m.clips("jg_cd","jg_cd");
        if(!std::strcmp(m.bodyClip,"jg_cd"))m.clipFrame=std::max(countdown,0);
        if(m.phase==Phase::Drop){
            const bool done=m.interp.calc();m.glide(m.cameraFrame(forwardH,lerp(spawn,stay,m.interp.rate)));
            if(done){m.phase=Phase::Stay;move.setDescending(true);}
        }else if(m.phase==Phase::Stay){
            m.hold(m.cameraFrame(forwardH,stay));
            if(in.stage>=2&&in.timer>=250){m.phase=Phase::Ascend;m.interp.init(60);move.setDescending(false);}
        }else{
            const bool done=m.interp.calc();m.glide(m.cameraFrame(forwardH,lerp(stay,exit,m.interp.rate)));
            if(done)m.mode=Mode::Away;
        }
        move.calc();break;
    }
    case Mode::Lap:{
        constexpr Vec3 spawn{-10,2000,0},mid{-10,1000,200},stay{-350,240,400};
        if(m.phase==Phase::Drop){
            const bool done=m.interp.calc();m.glide(m.cameraFrame(forwardH,lerp(spawn,mid,m.interp.rate)));
            if(done){m.phase=Phase::Arc;m.interp.init(20);}
        }else if(m.phase==Phase::Arc){
            const bool done=m.interp.calc();m.glide(m.cameraFrame(forwardH,lerp(mid,stay,m.interp.rate)));
            if(done){m.phase=Phase::Stay;m.phaseTimer=0;move.setDescending(true);}
        }else if(m.phase==Phase::Stay){
            m.hold(m.cameraFrame(forwardH,stay));
            if(++m.phaseTimer>=90)m.ascend(60);
        }else if(m.rise())m.mode=Mode::Away;
        move.calc();break;
    }
    case Mode::Reverse:{
        // JugemUnit::calcReverse (USA 0x80724880); enterReverse begins in Descending.
        constexpr Vec3 descend{0,500,0},stay{0,250,350};
        const float distance=length(kartPos-vec(move.transPos()));
        if(m.phase==Phase::Drop){
            const bool done=m.interp.calc();m.glide(m.upright(lerp(descend,stay,m.interp.rate)));
            if(done)m.phase=Phase::Stay;
        }else if(m.phase==Phase::Stay){
            m.hold(m.upright(stay));
            if(!m.reverseOn||distance>2000.f)m.ascend(70);
        }else if(m.rise())m.mode=Mode::Away;
        move.calc();break;
    }
    case Mode::Rescue:{
        // JugemUnit rescue calc (PAL 0x80724430): JugemMove follows the rescue target for
        // the kart's InRespawn time (fn_1_78C7C: 110 frames; 140 with state bit 135, which
        // this port does not produce), then the anchor climbs (0,5,0) + t * (0,2,0) a frame
        // (0x809C2A34 / 0x809C2A4C) and Lakitu leaves after t > 60 (0x808C959C).
        constexpr unsigned CarryFrames=110,RiseFrames=60;
        ++m.phaseTimer;
        if(m.phase==Phase::Stay){
            m.anchor=m.rescueTarget();move.setAnchorPos(m.anchor);
            if(m.phaseTimer>CarryFrames){m.phaseTimer=0;m.phase=Phase::Ascend;m.clips("wait","wait");}
        }else{
            m.anchor=m.anchor+Vector3f(0.f,5.f,0.f);
            m.anchor=m.anchor+Vector3f(0.f,2.f,0.f)*float(m.phaseTimer);
            move.setAnchorPos(m.anchor);
            if(m.phaseTimer>RiseFrames)m.mode=Mode::Away;
        }
        move.calc();break;
    }
    case Mode::Goal:{
        if(m.phase==Phase::Drop){
            const bool done=m.interp.calc();m.pos=lerp(m.goalFrom,m.goalTo,m.interp.rate);
            if(done)m.phase=Phase::Stay;
        }else if(m.phase==Phase::Stay){
            m.pos=m.goalTo;
            if(length(kartPos-m.goalTo)>2000.f){m.phase=Phase::Ascend;m.phaseTimer=0;}
        }else{
            m.pos=m.pos+(m.phaseTimer<10?Vec3{0,30,0}:Vec3{0,80,0});
            if(++m.phaseTimer>=70)m.mode=Mode::Away;
        }
        m.place(m.pos,kartPos-m.pos,true);break;
    }
    }
    if(m.mode==Mode::Start||m.mode==Mode::Lap||m.mode==Mode::Reverse||m.mode==Mode::Rescue){
        const auto& t=move.transform();float rows[12];
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=t[r,c];
        store(m.world,rows);
    }
    // Signal lamps (hg_signal_light1..3 at 3/2/1, hg_signal_start at GO).
    const std::array<uint32_t,6>* row=&LakituSignal0[0];
    if(in.stage>=2)row=&LakituSignal3[std::min(in.timer>=240?in.timer-240:0u,60u)];
    else if(in.stage==1&&in.timer>=60){
        const unsigned k=std::min((in.timer-60)/60,2u),f=std::min((in.timer-60)%60,30u);
        row=k==0?&LakituSignal0[f]:k==1?&LakituSignal1[f]:&LakituSignal2[f];
    }
    m.lamps=*row;
}
uint16_t Lakitu::soundLoop()const{
    const auto& m=*impl_;if(!m.ready||!m.kart)return 0;
    return m.mode==Mode::Rescue?0x1a0:m.mode==Mode::Reverse?0x19f:0;
}
Vec3 Lakitu::position()const{const auto& w=impl_->world;return {w.r[0].c[0],w.r[1].c[0],w.r[2].c[0]};}
void Lakitu::draw(Renderer& renderer){
    auto& m=*impl_;if(!m.ready||!m.kart||m.mode==Mode::Away||!m.visible)return;
    const unsigned elapsed=m.tick-m.clipStart;
    auto frameOf=[&](DriverAnimation& bank,const char* name){
        const int clip=bank.find(name);if(clip<0)return std::pair<int,unsigned>{-1,0};
        const unsigned duration=std::max(1u,unsigned(bank.duration(clip)));
        const unsigned f=m.clipFrame>=0?unsigned(m.clipFrame):elapsed;
        // The rescue grab is flagged looping but runs once: the carry (110
        // ticks) outlasts its 98 frames, and a loop re-grabbed the kart.
        const bool loop=bank.loop(clip)&&std::strcmp(name,"pickup");
        return std::pair<int,unsigned>{clip,loop?f%duration:std::min(f,duration-1)};
    };
    auto drawPart=[&](Impl::Part& p,const char* name,const C3D_Mtx& transform){
        auto [clip,frame]=frameOf(*p.bank,name);if(clip<0)return;
        if(p.rigid){
            RigidPose pose;pose.parts=&p.bank->rigidParts();pose.palette=p.bank->pose(clip,float(frame),m.tick);pose.matrices=p.bank->matrixCount();
            if(!pose.palette)return;
            renderer.setRigidPose(&pose);renderer.draw(*p.model,transform);renderer.setRigidPose(nullptr);
        }else{p.bank->apply(*p.model,clip,float(frame),m.tick);renderer.draw(*p.model,transform);}
    };
    drawPart(m.body,m.bodyClip,m.world);
    auto [bodyClip,bodyFrame]=frameOf(*m.body.bank,m.bodyClip);
    C3D_Mtx joint,rodWorld;store(joint,m.bodyJoints.at(m.bodyJoints.find(m.bodyClip),0,bodyFrame));
    Mtx_Multiply(&rodWorld,&m.world,&joint);
    if(m.attachment==Flag){drawPart(m.flag,"jg_wy",rodWorld);return;}
    drawPart(m.rod,m.rodClip,rodWorld);
    if(m.attachment==None)return;
    auto [rodClip,rodFrame]=frameOf(*m.rod.bank,m.rodClip);
    C3D_Mtx hook,hookWorld;store(hook,m.rodJoints.at(m.rodJoints.find(m.rodClip),0,rodFrame));
    Mtx_Multiply(&hookWorld,&rodWorld,&hook);
    (void)bodyClip;(void)rodClip;
    Model* board=m.attachment==Signal?m.signal.model.get():m.attachment==LapBoard?m.lap.model.get():
                 m.attachment==LapFinal?m.lapFinal.model.get():m.reverse.model.get();
    const Binding binding{board,m.lamps,m.attachment==Signal,m.attachment==LapBoard?m.digit:-1};
    renderer.draw(*board,hookWorld,false,bindLakitu,binding);
}
}
