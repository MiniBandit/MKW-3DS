#pragma once
#include <mkw/math.hpp>
#include <array>
#include <algorithm>
#include <cstdint>
namespace mkw {
// RaceGfx selects the coin renderer (805a4938/805a9984), whose virtual
// draw 805a9b64 calls the group2 pass with orthographic mode0. The other
// renderer's 24-degree perspective camera is not this mode's projection.
struct CoinHudCamera {
    static constexpr float Width=832.f,Height=480.f,EyeZ=1020.f,Near=1000.f,Far=-1000.f;
};
// Obj held-coin HUD 808841f0/808843d0/80884740/80884d3c. These are
// coordinates of the UI camera, not attachment offsets on a racer.
// One local screen only; the original2/3/4-screen profiles are out of scope.
class CoinHudStack {
public:
    struct Column {uint8_t coins=0;Vec3 position{},scale{};float pitch=-.3f;unsigned srtFrame()const{return coins?coins-1:0;}};
    void update(unsigned held,bool widescreen=true){
        shown_=std::min(held,100u);columns_=shown_?(shown_+9)/10:0;
        Vec3 size=widescreen?Vec3{.36f,.42f,.36f}:Vec3{.34f,.42f,.34f};
        const float maximum=widescreen?105.f:95.f;
        float spacing=size.x*140.f;
        if(spacing*columns_>maximum){const float ratio=maximum/(spacing*columns_);size=size*ratio;spacing*=ratio;}
        const float c=std::cos(-.3f),s=std::sin(-.3f);
        for(unsigned i=0;i<data_.size();++i){auto& column=data_[i];column={};
            if(i>=columns_)continue;
            column.coins=uint8_t(std::min(shown_-i*10,10u));
            const float missing=.5f*(1.f-float(column.coins)/10.f),lift=10.f*1.9f*size.y;
            column.scale={size.x,size.y*column.coins,size.z};
            column.position={-290.f+spacing*(.5f*float(columns_-1)-float(i)),-120.f+lift*(1.f-c*missing),1000.f+lift*(-s)*missing};
        }
    }
    unsigned shown()const{return shown_;}
    unsigned size()const{return columns_;}
    const Column& column(unsigned i)const{return data_[i];}
private:
    unsigned shown_=0,columns_=0;std::array<Column,10> data_{};
};
}
