// CoinUnit presentation through real REFX resources, EF pools and draw compiler.
#include <mkw/coin_presentation.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
using namespace mkw;
int main(int argc,char** argv){
    assert(argc==11);auto effects=std::make_unique<RaceEffects>();unsigned draws=0,peak=0;
    auto drain=[&](){for(unsigned i=0;i<300;++i)effects->step();
        assert(!effects->liveParticles()&&!effects->system().effects.used()&&
            !effects->system().emitters.used()&&!effects->system().managers.used());};
    for(int bank=1;bank<argc;++bank){assert(effects->load(argv[bank]));
        std::vector<ef::Material> materials(effects->system().resources.size());
        for(unsigned i=0;i<materials.size();++i){const auto& r=effects->system().resources[i];
            const bool translated=ef::translateMaterial(r.desc.draw,materials[i]);
            if(!std::strncmp(r.name,"rk_coin",7))assert(translated);}
        std::vector<ef::TextureSize> sizes;
        for(const auto& t:effects->textures)sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
        for(const char* name:CoinPresentation::Roots){
            effects->reset();const int spec=effects->find(name);assert(spec>=0);
            const unsigned slot=effects->allocateObjectSlots(6);assert(slot==RaceEffects::SlotCount);
            EffectFrame frame;frame.origin={120,180,-500};effects->create(slot,spec,frame);
            bool drawn=false;
            for(unsigned t=0;t<30;++t){effects->step();peak=std::max(peak,effects->liveParticles());
                ef::DrawList list;ef::buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),list);
                for(const auto& b:list.batches)if(b.count&&!std::strncmp(b.res->name,name,std::strlen(name))){drawn=true;++draws;}}
            assert(drawn);effects->fade(slot);assert(!effects->alive(slot));drain();
        }
        effects->reset();CoinPresentation view;view.reset(3);
        std::array<CoinBattle::Coin,3> coins{};std::array<Vec3,12> owners{};owners[0]={0,0,-500};
        std::array<uint8_t,3> visible{1,1,1};
        coins[0].state=CoinBattle::State::Spawning;coins[0].appearanceTicks=150;
        coins[1].state=CoinBattle::State::Available;coins[1].dynamic=true;
        coins[2].state=CoinBattle::State::Dropped;coins[2].dynamic=true;
        for(auto& c:coins)c.body.position={100,150,-500};
        auto step=[&](){view.step(coins,33,owners,visible,*effects);effects->step();};
        step();assert(view.unit(0).scale==0&&view.unit(0).model);
        assert(view.unit(1).add&&!view.unit(2).add); // sky uses Add, damage state8 has none
        assert(effects->slotCount()==RaceEffects::SlotCount+18);
        for(unsigned t=1;t<150;++t){--coins[0].appearanceTicks;step();assert(std::isfinite(view.unit(0).pitch));}
        assert(view.unit(0).scale>.999f);
        coins[0].state=CoinBattle::State::Available;coins[2].state=CoinBattle::State::Available;
        step();assert(!view.unit(2).add); // state8->2 payload byte1 is0: Shine
        visible={0,0,0};step();drain();
        visible={1,1,1};step();assert(effects->system().effects.used()==3);
        // A moving kart: entry offset captured once, live target every calc,
        // one arrival on45, mesh hidden46, no repeated Get/arrival at51.
        coins[0].state=CoinBattle::State::Held;coins[0].owner=0;
        for(unsigned t=0;t<56;++t){owners[0].x+=2;step();
            const uint16_t cue=view.takeArrivalCues();assert(cue==(t==45?1:0));
            assert(view.unit(0).model==(t<46));
            if(t>=7)assert(std::abs(view.unit(0).scale-.42f)<1e-6f);
            if(t==45)assert(length(view.unit(0).position-owners[0])<4);
        }
        for(auto& c:coins)c.state=CoinBattle::State::Lost;step();drain();
        // Same-update acquisition debt is0/1/2; a new owner starts a fresh
        // pickup from the last visible position, not the old course placement.
        for(auto& c:coins){c.state=CoinBattle::State::Available;c.dynamic=false;}step();
        for(auto& c:coins){c.state=CoinBattle::State::Held;c.owner=0;}step();
        assert(view.unit(0).pickupDelay==0&&view.unit(1).pickupDelay==1&&view.unit(2).pickupDelay==2);
        const auto before=view.unit(2).position;coins[2].owner=1;owners[1]=before;
        step();assert(view.unit(2).frame==1&&length(view.unit(2).position-before)<1e-5f);
        for(auto& c:coins)c.state=CoinBattle::State::Lost;step();drain();
        effects->reset();assert(effects->slotCount()==RaceEffects::SlotCount);
        step();assert(effects->slotCount()==RaceEffects::SlotCount+18); // fresh generation, once only
        step();assert(effects->slotCount()==RaceEffects::SlotCount+18);
        effects->unload();assert(effects->slotCount()==RaceEffects::SlotCount&&effects->textures.empty());
    }
    std::printf("PASS ten Coin banks, all six original roots/children drawn; appearance, sky Add/damage Shine, moving pickup/scale/45 arrival/46 hide, clip retirement/reentry, mode handles/reset/unload; draws=%u peak=%u; target presentation pending\n",draws,peak);
}
