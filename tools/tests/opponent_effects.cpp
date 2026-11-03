#include <mkw/opponent_effects.hpp>
#include <mkw/kart_effect_lod.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <cassert>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
using namespace mkw;
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}

int main(int argc,char**argv){
    assert(argc==2);
    auto effects=std::make_unique<RaceEffects>();assert(effects->load(argv[1]));
    OpponentDrivingEffects driving;assert(driving.bind(*effects));
    std::vector<ef::Material> materials(effects->system().resources.size());
    for(size_t i=0;i<materials.size();++i)ef::translateMaterial(effects->system().resources[i].desc.draw,materials[i]);
    std::vector<ef::TextureSize> textures;
    for(const auto&t:effects->textures)textures.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
    std::set<std::string> visible;
    auto draw=[&](){
        ef::DrawList list;ef::buildDrawList(effects->system(),materials,textures.data(),textures.size(),ef::Mtx34::identity(),list);
        for(const auto&v:list.vertices)assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
        for(const auto&b:list.batches)if(b.count){assert(b.count%3==0);visible.insert(b.res->name);}
    };
    OpponentDrivingInput in;in.pose.origin={0,0,-1000};in.rearLeft={-30,0,-1100};in.rearRight={30,0,-1100};
    in.leftFloor=in.rightFloor=true;in.vehicle=0;in.boost=true;in.driftState=2;
    for(unsigned tick=0;tick<30;++tick){driving.step(*effects,1,in,false);effects->step();draw();}
    assert(effects->alive(OpponentDrivingEffects::FirstSlot));
    // Turning on orange replaces blue emission without corrupting other slots.
    in.driftState=3;
    for(unsigned tick=0;tick<30;++tick){driving.step(*effects,1,in,false);effects->step();draw();}
    // Every original remote bike direction uses one reduced spark at the rear.
    effects->reset();driving.reset();in.bike=true;in.vehicle=18;in.wheelie=in.canWheelie=true;in.driftState=2;
    for(int direction:{-1,0,1}){
        in.driftDirection=direction;
        for(unsigned tick=0;tick<30;++tick){driving.step(*effects,11,in,false);effects->step();draw();}
    }
    for(const char* root:{"rk_miniTurboS","rk_driftSpark1L_Spark01S","rk_driftSpark1R_Spark01S",
            "rk_driftSpark2L_Spark01S","rk_driftSpark2R_Spark01S","rk_driftSpark1LB_Spark01S",
            "rk_driftSpark1RB_Spark01S","rk_driftSpark1CB_Spark01S","rk_wheelie"}){
        if(!visible.count(root))std::fprintf(stderr,"missing rendered original root: %s\n",root);
        assert(visible.count(root));
    }
    // Original LOD gives three nearby CPU cars full effects, not every CPU.
    effects->reset();driving.reset();
    KartEffectLod lod;std::array<bool,12> local{};local[0]=true;
    std::array<EffectClip,12> clip{};for(unsigned i=0;i<12;++i)clip[i].distance=100.f*i;
    lod.reset(12,local);lod.update(0,0,local,clip);
    effects->create(0,effects->find("rk_miniTurbo"),in.pose,0);
    in.bike=false;in.vehicle=0;in.driftState=2;
    for(unsigned i=1;i<12;++i)driving.step(*effects,i,in,lod.lod(i));
    effects->step();
    for(unsigned i=1;i<12;++i)assert(effects->alive(OpponentDrivingEffects::FirstSlot+(i-1)*12)==(i<=3));
    for(const auto* e=effects->system().active.head;e;e=e->next)assert(e->group==0||(e->group>=3&&e->group<=5));
    assert(effects->alive(0));
    // Leaving the visibility budget kills only the selected CPU's handles.
    driving.step(*effects,1,in,true);assert(!effects->alive(OpponentDrivingEffects::FirstSlot));
    assert(effects->alive(OpponentDrivingEffects::FirstSlot+12)&&effects->alive(0));
    in.hidden=true;driving.step(*effects,2,in,false);assert(!effects->alive(OpponentDrivingEffects::FirstSlot+12));
    in.hidden=false;in.preview=true;driving.step(*effects,3,in,false);assert(!effects->alive(OpponentDrivingEffects::FirstSlot+24));
    // A short airborne gap follows the source's 10-tick rear-wheel grace.
    effects->reset();driving.reset();in.preview=false;in.boost=false;in.leftFloor=in.rightFloor=true;
    driving.step(*effects,1,in,false);effects->step();in.leftFloor=in.rightFloor=false;
    for(unsigned tick=0;tick<9;++tick){driving.step(*effects,1,in,false);effects->step();}
    auto emitterRunning=[&](const char*name){
        for(const auto*e=effects->system().active.head;e;e=e->next)
            for(const auto* em=e->emitters.head;em;em=em->next)
                if(em->life==ef::Active&&!std::strcmp(em->res->name,name))return true;
        return false;
    };
    assert(emitterRunning("rk_driftSpark1L_Spark01S"));
    driving.step(*effects,1,in,false);effects->step();
    assert(!emitterRunning("rk_driftSpark1L_Spark01S"));
    in.leftFloor=in.rightFloor=true;driving.step(*effects,1,in,false);effects->step();
    assert(emitterRunning("rk_driftSpark1L_Spark01S"));
    // Verify the emitted resource, not just a copied sign expression: the
    // original inside-drift branch swaps L/R but preserves the centre root.
    in.bike=true;in.vehicle=18;in.wheelie=false;in.boost=false;in.driftState=2;
    const char* bikeRoots[]={"rk_driftSpark1LB_Spark01S","rk_driftSpark1RB_Spark01S","rk_driftSpark1CB_Spark01S"};
    const unsigned expected[2][3]={{0,2,1},{1,2,0}};
    for(unsigned inside=0;inside<2;++inside)for(int direction=-1;direction<=1;++direction){
        effects->reset();driving.reset();in.insideDrift=inside;in.driftDirection=direction;
        driving.step(*effects,1,in,false);effects->step();
        for(unsigned root=0;root<3;++root)
            assert(emitterRunning(bikeRoots[root])==(root==expected[inside][direction+1]));
    }
    effects->reset();driving.reset();assert(effects->liveParticles()==0&&effects->system().effects.used()==0);
    std::puts("PASS original remote roots draw; kart blue/orange, bike L/R/centre, boost, wheelie; nearest-three LOD; slot isolation; respawn/intro hide; 10-tick floor grace; restart/reset");
}
