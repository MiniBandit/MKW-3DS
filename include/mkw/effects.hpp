#pragma once
#include "mkw/math.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace mkw {
struct EmitterShape {
    float volume[5],direction[3],rotation[3],speed,diffusion,radiation,randomSpeed,velocityRandom;
    uint32_t flags;
};
struct SparkSpec {
    float sizeX,sizeY,inherit,rate;
    uint32_t life,texture,layer;
    uint8_t primary[4],secondary[4];
    float offset[2][3],speed[2],scale[32][2],alpha[32];
    EmitterShape shapes[2];
    float sizeDelta[2],colourRange[2][3],pivot[2],rotate[2];
    float drag,lifeRandom,rateRandom,alphaRandom;
    uint32_t drawFlags;
    uint8_t shapeType,shapeAssist,shapeDirection,shapePivot;
};
static_assert(sizeof(SparkSpec)==660,"DEFX record");
struct Spark {
    Vec3 position,velocity,direction,origin,up;
    unsigned age=32,spec=0,wheel=0,life=0;
    float size[2]{},rotation=0,alphaVariation=1;
    uint8_t primary[4]{},secondary[4]{};
};
class DriftEffects {
public:
    static constexpr unsigned Capacity=128;
    std::array<Spark,Capacity> sparks{};
    std::array<SparkSpec,7> specs{};
    unsigned emitted=0,blueFrames=0,orangeFrames=0;
    bool ready=false;
    bool load(const char* path){
        ready=false;
        FILE* f=std::fopen(path,"rb");if(!f)return false;uint32_t h[3]{};
        bool ok=std::fread(h,1,sizeof h,f)==sizeof h&&!std::memcmp(h,"DEFX",4)&&h[1]==3&&h[2]==specs.size();
        ok=ok&&std::fread(specs.data(),sizeof(SparkSpec),specs.size(),f)==specs.size();
        ok=ok&&std::fgetc(f)==EOF;std::fclose(f);
        for(const auto& s:specs){
            ok&=s.life>0&&s.life<=32&&s.texture<3&&s.layer<4;
            ok&=std::isfinite(s.sizeX)&&std::isfinite(s.sizeY)&&s.sizeX>0&&s.sizeY>0;
            ok&=std::isfinite(s.rate)&&s.rate>=0&&s.rate<=8&&s.inherit>=0&&s.inherit<=1;
            ok&=std::isfinite(s.drag)&&s.drag>=0&&s.drag<=1;
        }
        reset();return ready=ok;
    }
    void reset(){
        for(auto& p:sparks)p.age=32;
        cursor_=0;random_=0x584d4b57;haveOrigin_=false;state_=0;rates_={};previousScale_=1;
    }
    // Origins are REAR wheel centres, once for a bike and twice for a kart.
    // The optional kart up vector keeps emission attached on banked surfaces.
    void step(unsigned state,Vec3 left,Vec3 right,Vec3 forward,unsigned rearWheelCount=2,Vec3 up={0,1,0},float kartScale=1){
        if(!ready)return;
        rearWheelCount=std::min(rearWheelCount,2u);
        up=normalized(up);forward=normalized(tangent(forward,up),{0,0,1});
        const Vec3 side=normalized(cross(up,forward),{1,0,0});
        const Vec3 origins[]={left,right};
        const float scaleRatio=previousScale_>0?kartScale/previousScale_:1.f;
        for(auto& p:sparks)if(p.age<p.life){
            const auto& s=specs[p.spec];++p.age;
            if(haveOrigin_){
                const Vec3 delta=origins[p.wheel]-previous_[p.wheel];
                p.position+=delta*s.inherit;p.origin+=delta;
                p.position=p.origin+(p.position-p.origin)*scaleRatio;
                p.velocity=p.velocity*scaleRatio;p.size[0]*=scaleRatio;p.size[1]*=scaleRatio;
            }
            p.velocity=p.velocity*s.drag;p.position+=p.velocity;
            if(s.shapeDirection==1)p.direction=normalized(p.position-p.origin,p.direction);
        }
        previous_[0]=left;previous_[1]=right;previousScale_=kartScale;haveOrigin_=true;
        // New effects have fresh accumulators and emit immediately, including
        // a fractional-rate parent. Residual charge must not survive releasing R.
        const bool first=state!=state_;
        if(first)rates_={};
        state_=state;
        if(state==0)return;
        if(state==2)++blueFrames;else if(state>=3)++orangeFrames;
        auto world=[&](Vec3 v){return side*v.x+up*v.y+forward*v.z;};
        for(unsigned wheel=0;wheel<rearWheelCount;++wheel){
            for(unsigned group=0;group<(state>=2?2u:1u);++group){
                const unsigned id=state<2?6:(state>=3?3:0)+group;
                const auto& s=specs[id];auto& rate=rates_[wheel][group];
                rate+=s.rate*(1+s.rateRandom*(random()*2-1));
                if(first&&s.rate>0&&rate<1)rate=1;
                while(rate>=1){
                    --rate;
                    Vec3 local,velocity;
                    emission(s.shapes[wheel],local,velocity);
                    const Vec3 offset{s.offset[wheel][0],s.offset[wheel][1],s.offset[wheel][2]};
                    const Vec3 origin=origins[wheel]+world(offset)*kartScale;
                    const Vec3 position=origin+world(local)*kartScale;
                    const Vec3 motion=world(velocity)*kartScale;
                    // Both flash children occur on the SAME parent birth, with
                    // its shared velocity. Core and smoke each have one child.
                    const unsigned children=state>=2&&group==1?2:1;
                    for(unsigned child=0;child<children;++child){
                        auto& p=sparks[cursor_++%Capacity];const auto& def=specs[id+child];
                        p.spec=id+child;p.wheel=wheel;p.age=0;
                        p.life=std::max(1u,unsigned(def.life)-unsigned(def.life*random()*def.lifeRandom));
                        p.position=position;p.origin=origin;p.velocity=motion;p.up=up;
                        p.direction=normalized(def.shapeDirection==1?world(local):motion,forward*-1.f);
                        const float sizeRandom=random(); // source SyncRand links X/Y
                        p.size[0]=(def.sizeX+def.sizeDelta[0]*sizeRandom)*kartScale;
                        p.size[1]=(def.sizeY+def.sizeDelta[1]*sizeRandom)*kartScale;
                        p.rotation=def.rotate[0]+def.rotate[1]*random();
                        p.alphaVariation=1+def.alphaRandom*(random()*2-1);
                        for(unsigned channel=0;channel<3;++channel){
                            p.primary[channel]=colour(def.primary[channel],def.colourRange[0][channel]);
                            p.secondary[channel]=colour(def.secondary[channel],def.colourRange[1][channel]);
                        }
                        p.primary[3]=def.primary[3];p.secondary[3]=def.secondary[3];
                        ++emitted;
                    }
                }
            }
        }
    }
private:
    static Vec3 rotate(Vec3 p,Vec3 a){
        float sx=std::sin(a.x),cx=std::cos(a.x),sy=std::sin(a.y),cy=std::cos(a.y),sz=std::sin(a.z),cz=std::cos(a.z);
        Vec3 x{p.x,cx*p.y-sx*p.z,sx*p.y+cx*p.z};
        Vec3 y{cy*x.x+sy*x.z,x.y,-sy*x.x+cy*x.z};
        return {cz*y.x-sz*y.y,sz*y.x+cz*y.y,y.z};
    }
    void emission(const EmitterShape& e,Vec3& local,Vec3& velocity){
        constexpr float Tau=6.283185307f;
        local={};Vec3 radial{};
        const auto form=e.flags&255;
        const float start=(e.flags&(1u<<18))?e.volume[2]:e.volume[2]+random()*Tau;
        if(form==0){ // NW4R disc
            float dist=random(),inner=e.volume[1]*.01f;
            dist=(e.flags&(1u<<24))?std::sqrt(dist+inner*inner*(1-dist)):dist+inner*(1-dist);
            const float angle=start+(e.volume[3]-e.volume[2])*random();
            radial={std::sin(angle),0,-std::cos(angle)};
            const float sizeZ=(e.flags&(1u<<25))?e.volume[0]:e.volume[4];
            local={radial.x*dist*e.volume[0],0,radial.z*dist*sizeZ};
        }else if(form==8){ // Source drift smoke uses the linked-size sphere form.
            float dist=random(),inner=e.volume[1]*.01f;
            if(e.flags&(1u<<24))dist=1-dist*dist*dist;
            dist+=inner*(1-dist);
            const float angle=start+(e.volume[3]-e.volume[2])*random();
            const float latitude=random()*(Tau*.5f)+Tau*.25f;
            radial={-std::cos(latitude)*std::sin(angle),-std::sin(latitude),std::cos(latitude)*std::cos(angle)};
            local=radial*(dist*e.volume[0]);
        }
        velocity=radial*e.radiation;
        if(e.speed){
            Vec3 dir{0,1,0};
            if(e.diffusion)dir=rotate(dir,{random()*e.diffusion,random()*Tau,0});
            velocity+=rotate(dir,{e.direction[0],e.direction[1],e.direction[2]})*e.speed;
        }
        if(e.randomSpeed)velocity+=rotate({0,0,1},{random()*Tau,random()*Tau,random()*Tau})*e.randomSpeed;
        if(e.velocityRandom)velocity=velocity*(1-e.velocityRandom*(random()*2-1));
        const Vec3 rotation{e.rotation[0],e.rotation[1],e.rotation[2]};
        local=rotate(local,rotation);velocity=rotate(velocity,rotation);
    }
    unsigned cursor_=0,random_=0x584d4b57,state_=0;float previousScale_=1;
    Vec3 previous_[2]{};bool haveOrigin_=false;
    std::array<std::array<float,2>,2> rates_{};
    // NW4R's generator, private to visuals so race/replay RNG is untouched.
    float random(){random_=random_*0x343fdu+0x269ec3u;return (random_>>16)*(1.f/65536.f);}
    uint8_t colour(uint8_t base,float range){return uint8_t(std::clamp(base*(1+range*(2*random()-1)),0.f,255.f));}
};
}
