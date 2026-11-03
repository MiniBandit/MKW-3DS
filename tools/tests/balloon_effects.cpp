// New balloon burst resources through the real loader, particle lifetime,
// child creation and draw-list compiler. No 3DS graphics/audio are emulated.
#include <mkw/race_effects.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <memory>
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
using namespace mkw;
int main(int argc,char** argv){
    assert(argc==11);auto effects=std::make_unique<RaceEffects>();unsigned draws=0,peak=0;
    for(int bank=1;bank<argc;++bank){assert(effects->load(argv[bank]));
        std::vector<ef::Material> materials(effects->system().resources.size());
        for(unsigned i=0;i<materials.size();++i){const auto& r=effects->system().resources[i];
            if(!std::strncmp(r.name,"rk_baloon",9))assert(ef::translateMaterial(r.desc.draw,materials[i]));}
        std::vector<ef::TextureSize> sizes;
        for(const auto& t:effects->textures)sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
        for(const char* name:{"rk_baloonR","rk_baloonB"}){
            effects->reset();const int spec=effects->find(name);assert(spec>=0);
            const auto& resource=effects->system().resources[spec];assert(resource.desc.emitLife==1);
            EffectFrame frame;frame.origin={120,180,-500};frame.scale=4;
            effects->burst(spec,frame);bool children[3]{};unsigned lastLive=0;
            for(unsigned tick=0;tick<240;++tick){effects->step();peak=std::max(peak,effects->liveParticles());
                ef::DrawList list;ef::buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),list);
                for(const auto& b:list.batches){const char* child=b.res->name;
                    if(!std::strncmp(child,name,std::strlen(name))&&b.count){++draws;
                        const char suffix=child[std::strlen(name)];if(suffix>='0'&&suffix<='2')children[suffix-'0']=true;}}
                if(effects->liveParticles())lastLive=tick;
            }
            assert(children[0]&&children[1]&&children[2]&&lastLive>0&&lastLive<239);
            assert(!effects->liveParticles()&&!effects->system().effects.used()&&!effects->system().emitters.used()&&!effects->system().managers.used());
        }
        effects->unload();assert(!effects->ready&&effects->textures.empty()&&effects->system().resources.empty());
    }
    std::printf("PASS ten mode banks, both original one-tick roots and three recursive children drawn, finite particle/emitter/manager teardown and unload; child draws=%u peak particles=%u; target rendering/audio pending\n",draws,peak);
}
