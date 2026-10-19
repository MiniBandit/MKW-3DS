#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mkw {
// DriverIKParam.bin, USA 807d3d44..807d4ed8. Entries are vehicle-major,
// then character; byte zero enables the entry and bytes 1..32 interpolate
// the shared endpoint arrays. An absent/disabled entry requires IKParam.ikp.
struct DriverLookParameters {
    float pitchNegative=45, pitchPositive=10, yawBody=130, yawHead=120;
    float headYawScale=.5f, headPitchScale=.85f, spineYawScale=.03f, rootYawScale=0;
    // IKParam offsets 18,1c,20,24,28,2c,30,34,38,3c,40,44,50,54.
    std::array<float,14> vehicleResponse{};
    bool vehicleResponseEnabled=true; // Driver+144, supplied by display-parameter cooker.
    bool decodeFallback(const void* bytes,size_t size) {
        if(!bytes||size<0x94)return false;
        const auto* data=static_cast<const uint8_t*>(bytes);
        const unsigned offsets[]={0x64,0x68,0x6c,0x90,0x70,0x74,0x78,0x7c};
        std::array<float,8> values{};
        for(unsigned j=0;j<values.size();++j){
            const auto* b=data+offsets[j];
            const uint32_t bits=uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
            std::memcpy(&values[j],&bits,4);
            if(!std::isfinite(values[j]))return false;
        }
        assign(values);
        const unsigned responseOffsets[]={0x18,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34,0x38,0x3c,0x40,0x44,0x50,0x54};
        for(unsigned j=0;j<vehicleResponse.size();++j){
            const auto* b=data+responseOffsets[j];uint32_t bits=uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
            std::memcpy(&vehicleResponse[j],&bits,4);if(!std::isfinite(vehicleResponse[j]))return false;
        }
        return true;
    }
    bool decode(const void* bytes,size_t size,unsigned vehicle,unsigned character) {
        if(!bytes||size<0x10c)return false;
        const auto* data=static_cast<const uint8_t*>(bytes);
        auto word=[&](size_t at){return uint32_t(data[at])<<24|uint32_t(data[at+1])<<16|
            uint32_t(data[at+2])<<8|data[at+3];};
        auto real=[&](size_t at){uint32_t bits=word(at);float value;std::memcpy(&value,&bits,4);return value;};
        const unsigned stride=word(0),vehicles=word(4),characters=word(8);
        if(stride!=33||vehicles!=36||characters!=42||vehicle>=vehicles||character>=characters||
            size!=0x10c+size_t(stride)*vehicles*characters)return false;
        const auto* entry=data+0x10c+(size_t(vehicle)*characters+character)*stride;
        if(!entry[0])return false;
        std::array<float,8> values{};
        // Parameter indices 13..20 map to object offsets 64,68,6c,90,70,74,78,7c.
        for(unsigned j=0;j<values.size();++j){
            const unsigned index=12+j;
            const float high=real(0xc+index*4),low=real(0x8c+index*4);
            if(!std::isfinite(high)||!std::isfinite(low))return false;
            const float ratio=float(entry[index+1])/255.f;
            const float interpolated=high==low?high:low+ratio*(high-low);
            // Retail fctiwz truncates toward zero after +0.5, including negatives.
            values[j]=std::trunc(.5f+100.f*interpolated)/100.f;
        }
        assign(values);
        constexpr unsigned responseIndices[]={6,7,8,3,4,5,0,1,2,9};
        for(unsigned j=0;j<10;++j){
            const unsigned i=responseIndices[j];const float high=real(0xc+i*4),low=real(0x8c+i*4);
            if(!std::isfinite(high)||!std::isfinite(low))return false;
            const float ratio=float(entry[i+1])/255.f;
            vehicleResponse[j]=std::trunc(.5f+100.f*(high==low?high:low+ratio*(high-low)))/100.f;
        }
        // Constructor values survive the common-table override (807d3c44..58).
        vehicleResponse[10]=.2f;vehicleResponse[11]=.02f;vehicleResponse[12]=.8f;vehicleResponse[13]=.9f;
        return true;
    }
private:
    void assign(const std::array<float,8>& values){
        pitchNegative=values[0];pitchPositive=values[1];yawBody=values[2];yawHead=values[3];
        headYawScale=values[4];headPitchScale=values[5];spineYawScale=values[6];rootYawScale=values[7];
    }
};

// The four procedural angles (+4b4..+4c0) and their velocities (+4c4..+4d0).
// Caller supplies target yaw/pitch in degrees from the animated head frame,
// plus its authored body angles. The update is once per simulation tick.
class DriverLookMotion {
public:
    struct Angles {float root=0,spine=0,yaw=0,pitch=0;};
    void reset(){position_={};velocity_={};}
    Angles angles()const{return position_;}
    Angles step(bool active,bool body,float targetYaw,float targetPitch,float kartYaw,
            float authoredYaw,float authoredPitch,const DriverLookParameters& p,float response=.7f){
        float root=0,spine=0,yaw=0,pitch=0;
        if(active){
            authoredYaw+=position_.root+position_.spine+position_.yaw;
            authoredPitch+=position_.pitch;
            // USA807c0600..807c0670: select the same turn side for a target
            // behind the driver's current body pose, then cap the head request.
            const float limit=body?p.yawBody:p.yawHead;
            const float delta=std::fabs(kartYaw-authoredYaw);
            if(delta>150.f&&delta<360.f-(15.f+180.f-limit))targetYaw=kartYaw>0?360.f:-360.f;
            targetYaw=std::clamp(targetYaw,-70.f,70.f);
            targetPitch=std::clamp(targetPitch,-70.f,70.f)*(1.f-std::fabs(targetYaw)/85.f);
            // Avoid further rotating into an authored pose near its limit.
            auto attenuation=[](float angle,float requested,float limit){
                if(angle*requested<=0)return 1.f;
                const float span=.7f*limit-limit;
                const float t=span==0?0.f:(std::fabs(angle)-limit)/span;
                return std::clamp(t,0.f,1.f);
            };
            const float yawWeight=attenuation(authoredYaw,targetYaw,limit);
            const float pitchLimit=authoredPitch>0?p.pitchPositive:p.pitchNegative;
            const float pitchWeight=authoredPitch*targetPitch>0?
                std::min(yawWeight,attenuation(authoredPitch,targetPitch,pitchLimit)):1.f;
            yaw=targetYaw*yawWeight;pitch=targetPitch*pitchWeight;
            if(body){root=yaw*p.rootYawScale;spine=yaw*p.spineYawScale;yaw*=p.headYawScale;}
            pitch*=p.headPitchScale;
        }else{
            // USA807c0858: a bounded restoring request, not an instant reset.
            root=-std::clamp(position_.root,-20.f,20.f);
            spine=-std::clamp(position_.spine,-20.f,20.f);
            yaw=-std::clamp(position_.yaw,-20.f,20.f);
            pitch=-std::clamp(position_.pitch,-20.f,20.f);
        }
        // USA807c0918..807c0a2c. DriverManager's item target response is .7.
        response=std::clamp(response,0.f,1.f);
        const float drive=.015f+.165f*response;
        const float damping=.3f+.099999994f*response;
        const float spring=.003f+.012f*response;
        auto advance=[&](float& position,float& velocity,float target){
            velocity+=(target*drive-velocity*damping)-position*spring;
            position+=velocity;
        };
        if(body){advance(position_.root,velocity_.root,root);advance(position_.spine,velocity_.spine,spine);}
        advance(position_.yaw,velocity_.yaw,yaw);advance(position_.pitch,velocity_.pitch,pitch);
        return position_;
    }
private:
    Angles position_{},velocity_{};
};
}
