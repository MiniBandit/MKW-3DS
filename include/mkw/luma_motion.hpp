#pragma once
#include <mkw/math.hpp>
#include <algorithm>

namespace mkw {
// TicoModel::Update, RMCE01 807caed8. Advance once per physics tick,
// including when culled; rendering must never advance this state.
class LumaMotion {
public:
    struct Input {
        Vec3 position{},previousPosition{},velocity{};
        Vec3 right{1,0,0},bodyUp{0,1,0},bodyForward{0,0,1};
        Vec3 poseUp{0,1,0},poseForward{0,0,1};
        Vec3 movementUp{0,1,0},internalVelocityDirection{0,0,1};
        float scaleY=1,hopHeight=0;
        bool resetPosition=false,overZipper=false,inCannon=false;
    };
    void reset(){*this=LumaMotion{};}
    void step(const Input& in){
        if(!initialized_||in.resetPosition){
            right_=in.right;up_=in.poseUp;forward_=in.poseForward;
            position_=in.position+Vec3{0,90.f*in.scaleY,0};
            offset_={0,1,0};initialized_=true;
            return;
        }
        up_=lerp(up_,in.bodyUp.y>in.movementUp.y?in.bodyUp:in.movementUp,.05f);
        zipper_=std::clamp(zipper_+(in.overZipper?.05f:-.05f),0.f,1.f);
        forward_=in.internalVelocityDirection;
        if(zipper_>0)forward_=normalized(lerp(forward_,in.bodyForward,zipper_),forward_);
        up_=normalized(up_);
        right_=normalized(cross(up_,forward_),right_);
        forward_=cross(right_,up_);
        auto bodyUp=in.bodyUp;bodyUp.y=std::fabs(bodyUp.y);
        offset_=lerp(offset_,bodyUp,.05f);
        Vec3 target=in.position-Vec3{0,in.hopHeight,0}+offset_*(90.f*in.scaleY)+in.velocity;
        const Vec3 extra=in.inCannon?in.position-in.previousPosition-in.velocity:Vec3{};
        if(dot(extra,extra)>dot(extraVelocity_,extraVelocity_))extraVelocity_=extra;
        else extraVelocity_=follow(extraVelocity_,extra);
        position_=follow(position_+extraVelocity_,target);
    }
    Vec3 position()const{return position_;}
    Vec3 right()const{return right_;}
    Vec3 up()const{return up_;}
    Vec3 forward()const{return forward_;}
private:
    static Vec3 follow(Vec3 from,Vec3 to){
        return {from.x+.4f*(to.x-from.x),from.y+.2f*(to.y-from.y),from.z+.4f*(to.z-from.z)};
    }
    Vec3 position_{},offset_{0,1,0},extraVelocity_{};
    Vec3 right_{1,0,0},up_{0,1,0},forward_{0,0,1};
    float zipper_=0;
    bool initialized_=false;
};
}
