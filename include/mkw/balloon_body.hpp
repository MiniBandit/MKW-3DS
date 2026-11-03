#pragma once
#include <mkw/balloon_presentation.hpp>

namespace mkw {
// BalloonUnit8087ba84/8087bc64. Field names correspond to the native
// anchor(+bc), heading(+c8), fan(+d4), knot delta(+e0), knot(+ec), centre
// delta(+f8), previous centre delta(+104), centre(+110), previous centre(+11c)
// and carried contact impulse(+158). Publish only after ALL units step.
class BalloonBody {
public:
    static constexpr float Epsilon=1.1920928955078125e-7f;
    Vec3 anchor{},heading{0,0,1},fan{0,1,0},knotDelta{},knot{},centreDelta{},previousCentreDelta{},centre{},previousCentre{},contact{};
    Vec3 right{1,0,0},up{0,1,0},front{0,0,1};float previousScale=.1f;
    bool initialized=false,resetLatch=false;
    void init(const BalloonAttachment& p,unsigned slot,unsigned count,float scale){
        anchor=balloonAnchor(p);heading=p.front;fan=balloonFan(p,slot,count);
        knot=anchor+fan*160.f;centre=knot+fan*(50.f*(scale/4.f));
        knotDelta=centreDelta=previousCentreDelta=contact={};previousCentre=centre;previousScale=scale;
        right={1,0,0};up={0,1,0};front={0,0,1};initialized=true;
    }
    void publish(float scale){previousScale=scale;previousCentre=centre;previousCentreDelta=centreDelta;}
    static Vec3 unit(Vec3 v,Vec3 fallback={0,1,0}){return dot(v,v)<=Epsilon?fallback:v/length(v);}
    static Vec3 reject(Vec3 v,Vec3 axis){return v-axis*dot(v,axis);}
    static Vec3 rotate(Vec3 v,Vec3 axis,float angle){
        const float c=std::cos(angle),s=std::sin(angle);return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));
    }
    static Vec3 stretch(Vec3 v,float rest){const float l=length(v);return l>rest&&dot(v,v)>Epsilon?v*((l-rest)/l):Vec3{};}
    static Vec3 compress(Vec3 v,float rest){const float l=length(v);return rest>l&&dot(v,v)>Epsilon?v*((rest-l)/l):Vec3{};}
    // Native static BSS+30/34/38 are zero (no writes/relocations beyond
    // the two vector static initializers). Keep them explicit in the input
    // so a traced nonzero value can be represented without a solver rewrite.
    struct Feedback {float contactThreshold,anchorRate,anchorCarry;};
    template<class Random> void step(const BalloonAttachment* p,unsigned slot,unsigned count,float scale,Vec3 velocity,
            bool clampSpeed,const BalloonBody* const* peers,unsigned peerCount,Random random,Feedback feedback={0,0,0}){
        if(!initialized)return;
        constexpr float dt=1.f/3.f;const bool attached=p!=nullptr;
        const Vec3 oldAnchor=anchor,newAnchor=attached?balloonAnchor(*p):knot;
        Vec3 desiredFan=attached?balloonFan(*p,slot,count):Vec3{0,1,0};
        Vec3 newHeading{0,0,1};
        if(attached){
            Vec3 axis=cross(Vec3{0,1,0},p->front);
            if(dot(axis,axis)>Epsilon){
                axis=unit(axis);const Vec3 parallel=axis*dot(velocity,axis);
                const Vec3 tilt=unit(Vec3{0,1,0}+parallel*-.01f+(velocity-parallel)*-.003f);
                Vec3 tiltAxis=cross(Vec3{0,1,0},tilt);
                if(dot(tiltAxis,tiltAxis)>Epsilon){tiltAxis=unit(tiltAxis);
                    desiredFan=rotate(desiredFan,tiltAxis,std::atan2(std::sqrt(tilt.x*tilt.x+tilt.z*tilt.z),tilt.y));}
            }
            Vec3 horizontal{p->front.x,0,p->front.z};
            if(dot(horizontal,horizontal)>Epsilon){
                horizontal=unit(horizontal);const float yaw=std::atan2(dot(cross(Vec3{0,1,0},heading),horizontal),dot(heading,horizontal));
                newHeading=rotate(heading,{0,1,0},.05f*yaw);
                const float lag=std::atan2(dot(cross(Vec3{0,1,0},horizontal),newHeading),dot(horizontal,newHeading));
                desiredFan=rotate(desiredFan,{0,1,0},lag);
            }
        }
        desiredFan=unit(lerp(fan,desiredFan,.12f));
        const float oldSize=previousScale/4.f;
        //8087c310..c33c: size times UP, then two dt multiplies, just
        //like gravity. A third dt makes full balloons heavier than air.
        const Vec3 gravity=Vec3{0,-.35f,0}*(dt*dt),buoyancy=Vec3{0,1,0}*(oldSize*dt*dt);
        Vec3 growthNoise{};
        const float growth=(scale-previousScale)*(1.f+(scale/4.f)*(.1f-1.f));
        if(growth>0){
            // Retail draws Z,Y,X from its Random instance, then rejects the
            // component parallel to the desired fan direction.
            const float z=random()-.5f,y=random()-.5f,x=random()-.5f;
            const Vec3 wobble=desiredFan+reject({x,y,z},desiredFan);
            if(dot(wobble,wobble)>Epsilon)growthNoise=unit(wobble)*(growth*40.f*dt);
        }
        Vec3 peerForce{},peerImpulse{};
        if(attached){
            for(unsigned i=0;i<peerCount;++i){const auto& other=*peers[i];if(&other==this)continue;
                const Vec3 delta=previousCentre-other.previousCentre;
                const Vec3 overlap=compress(delta,38.f*(oldSize+other.previousScale/4.f));
                peerForce+=overlap*.5f;
                if(length(overlap)>0&&dot(delta,delta)>Epsilon){
                    const Vec3 normal=unit(delta);const float approaching=dot(normal,other.previousCentreDelta-previousCentreDelta);
                    if(approaching>feedback.contactThreshold)peerImpulse+=normal*(approaching*1.28f);
                }
            }
            peerForce=peerForce*dt;peerImpulse=peerImpulse*dt+contact;
        }
        Vec3 velocityNoise{};
        if(dot(previousCentreDelta,previousCentreDelta)>Epsilon){
            const Vec3 direction=unit(previousCentreDelta);
            const float z=random()-.5f,y=random()-.5f,x=random()-.5f;
            const Vec3 noise=unit(reject({x,y,z},direction),{0,-1,0});
            //8087c8e8 loads0.1 from808ade88; the rate is squared next.
            //Using10 here amplifies this force10000x before the38 cap.
            const float rate=.1f*length(previousCentreDelta);
            velocityNoise=noise*(std::min(rate*rate*.12f,38.f)*dt);
        }
        const Vec3 previousRight=right,previousUp=up,previousFront=front;
        for(unsigned sub=0;sub<3;++sub){
            Vec3 knotForce=peerForce,anchorForce{},centreSpring{},centreImpulse{};
            if(!attached){const Vec3 longString=stretch(anchor-knot,160.f);anchorForce+=longString;centreSpring+=longString*2.f;}
            const Vec3 shapeDelta=knot-centre;
            if(dot(shapeDelta,shapeDelta)>Epsilon){
                const Vec3 spring=stretch(shapeDelta,50.f*oldSize)-compress(shapeDelta,50.f*oldSize);
                knotForce+=spring;centreImpulse+=spring;
                const Vec3 opposite=spring*-.5f;
                centreSpring+=opposite;anchorForce+=opposite;knotForce+=opposite;centreImpulse+=opposite;
            }
            if(attached){const Vec3 error=anchor+fan*160.f-knot;const Vec3 follow=error*feedback.anchorRate;
                anchorForce+=follow;}
            if(attached&&knot.y-centre.y>0)knotForce.y+=(knot.y-centre.y)*.05f;
            const Vec3 anchorNow=lerp(oldAnchor,newAnchor,float(sub+1)*dt);
            Vec3 newKnot=anchorNow+desiredFan*160.f+anchorForce;
            Vec3 delta=newKnot-knot-anchorForce;
            if(!attached){
                knotForce=buoyancy+centreSpring;
                delta=knotDelta*.99f+knotForce;
                newKnot=knot+delta+anchorForce;
            }
            if(attached&&clampSpeed&&dot(delta,delta)>Epsilon){
                const float speed=length(delta);
                const float cap=std::clamp(speed*.1f,40.f*dt,100.f*dt);
                //8087d050 compares zero against the velocity projection:
                // negative projections become zero, positive ones add to
                // the cap. min() can make allowed negative and reverse the
                // tether, or leave it behind a fast kart indefinitely.
                const float allowed=cap+std::max(0.f,dot(delta/speed,velocity));
                if(allowed<speed){delta=delta*(allowed/speed);newKnot=knot+delta+anchorForce;}
            }
            const Vec3 acceleration=gravity+buoyancy+growthNoise+velocityNoise+centreImpulse+peerImpulse*.5f;
            const Vec3 newCentreDelta=centreDelta*.967f+acceleration;
            const Vec3 newCentre=centre+newCentreDelta+knotForce;
            anchor=anchorNow;heading=newHeading;fan=desiredFan;knotDelta=delta;knot=newKnot;
            contact=peerImpulse*.5f;centreDelta=newCentreDelta;centre=newCentre;
            const Vec3 stem=centre-knot;
            //80532d34 aligns columnY using the PREVIOUS frame's columnX.
            // A degenerate target/cross product retains the previous matrix.
            const Vec3 newUp=unit(stem);Vec3 newFront=cross(previousRight,newUp);
            if(dot(stem,stem)>Epsilon&&dot(newFront,newFront)>Epsilon){
                newFront=unit(newFront);up=newUp;front=newFront;right=cross(newUp,newFront);
            }else{right=previousRight;up=previousUp;front=previousFront;}
        }
    }
};
}
