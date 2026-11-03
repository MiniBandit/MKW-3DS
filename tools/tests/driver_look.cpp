#include "mkw/driver_look.hpp"
#include "mkw/driver_look_data.hpp"
#include "mkw/driver_look_palette.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<char> read(const char* path){
    std::ifstream file(path,std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file),{}};
}
static bool near(float a,float b){return std::fabs(a-b)<.0001f;}
int main(){
    using namespace mkw;
    auto common=read("assets/common/DriverIKParam.bin");
    const auto display=read("assets/common/kartDriverDispParam.bin");
    auto mario=read("assets/roster/mario/IKParam.ikp");
    DriverLookParameters p;
    assert(!p.decode(common.data(),common.size(),0,0));
    assert(p.decodeFallback(mario.data(),mario.size()));
    assert(near(p.yawBody,130)&&near(p.yawHead,120)&&near(p.headPitchScale,.85f));
    assert(p.decode(common.data(),common.size(),0,6));
    assert(near(p.pitchNegative,40)&&near(p.yawBody,114.94f)&&near(p.yawHead,129.96f));
    assert(p.decode(common.data(),common.size(),18,6));
    assert(near(p.pitchNegative,35)&&near(p.yawHead,119.8f));
    unsigned valid=0;
    for(unsigned vehicle=0;vehicle<36;++vehicle)for(unsigned character=0;character<42;++character){
        const bool enabled=common[0x10c+(vehicle*42+character)*33]!=0;
        assert(p.decode(common.data(),common.size(),vehicle,character)==enabled);
        valid+=enabled;
    }
    assert(!p.decode(common.data(),common.size()-1,0,6));
    assert(!p.decode(common.data(),common.size(),36,6));
    unsigned cooked=0;
    for(unsigned character=0;character<24;++character)for(unsigned vehicle=0;vehicle<36;++vehicle){
        DriverLookParameters cached,decoded;
        if(!driverLookParameters(character,vehicle,cached))continue;
        ++cooked;
        float square=0;
        for(unsigned i=0;i<3;++i){
            const auto* q=reinterpret_cast<const unsigned char*>(display.data())+8+(vehicle*48+character)*56+0x14+i*4;
            uint32_t bits=uint32_t(q[0])<<24|uint32_t(q[1])<<16|uint32_t(q[2])<<8|q[3];float v;std::memcpy(&v,&bits,4);square+=v*v;
        }
        assert(cached.vehicleResponseEnabled==(square>0x1p-23f));
        if(decoded.decode(common.data(),common.size(),vehicle,character)){
            for(unsigned i=0;i<14;++i)assert(near(cached.vehicleResponse[i],decoded.vehicleResponse[i]));
            assert(near(cached.pitchNegative,decoded.pitchNegative));
            assert(near(cached.pitchPositive,decoded.pitchPositive));
            assert(near(cached.yawBody,decoded.yawBody)&&near(cached.yawHead,decoded.yawHead));
            assert(near(cached.headYawScale,decoded.headYawScale)&&near(cached.headPitchScale,decoded.headPitchScale));
            assert(near(cached.spineYawScale,decoded.spineYawScale)&&near(cached.rootYawScale,decoded.rootYawScale));
        }
    }
    assert(cooked==288);
    // This head's local X points down model -Z. Pitch must still rotate around
    // model X, as the original callback uses the driver's X, not the head's X.
    float palette[]={0,0,1,2, 0,1,0,3, -1,0,0,4, 0,0,1, 0,1,0, -1,0,0};
    uint32_t slots[]={0};
    applyDriverLookPalette(palette,0,slots,1,0,90);
    assert(near(palette[0],0)&&near(palette[4],1)&&near(palette[8],0));
    assert(near(palette[12],0)&&near(palette[15],1)&&near(palette[18],0));
    assert(near(palette[3],2)&&near(palette[7],3)&&near(palette[11],4));
    assert(p.decodeFallback(mario.data(),mario.size()));
    DriverLookMotion motion;
    auto first=motion.step(true,true,45,0,45,0,0,p);
    // Original .7 response: .015 + .165*.7 = .1305; Mario head yaw scale .5.
    assert(near(first.yaw,2.93625f)&&near(first.spine,.176175f)&&near(first.pitch,0));
    for(unsigned i=0;i<120;++i)motion.step(true,true,45,-40,45,0,0,p);
    const auto looking=motion.angles();
    assert(looking.yaw>0&&looking.pitch<0);
    const auto returning=motion.step(false,true,0,0,0,0,0,p);
    assert(std::fabs(returning.yaw)>1); // no snapping when the request expires
    for(unsigned i=0;i<300;++i)motion.step(false,true,0,0,0,0,0,p);
    const auto neutral=motion.angles();
    assert(std::fabs(neutral.yaw)<.001f&&std::fabs(neutral.pitch)<.001f);
    motion.reset();
    const auto above=motion.step(true,false,0,-90,0,0,0,p);
    assert(near(above.pitch,-70.f*.85f*.1305f));
    assert(near(above.root,0)&&near(above.spine,0));
    std::printf("PASS: %u enabled entries, %u cooked combinations; fallback, angular response, return and model-axis pitch\n",valid,cooked);
}
