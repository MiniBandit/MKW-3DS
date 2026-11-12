#include <mkw/battle_spawn.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
static unsigned be16(const uint8_t* p){return unsigned(p[0])<<8|p[1];}
static unsigned be32(const uint8_t* p){return unsigned(p[0])<<24|unsigned(p[1])<<16|unsigned(p[2])<<8|p[3];}
int main(int argc,char** argv){
    assert(argc==2);
    assert(mkw::battleStartSlot(0,0,5)==5&&mkw::battleStartSlot(1,0,5)==11);
    assert(mkw::battleStartSlot(0,1,5)==0&&mkw::battleStartSlot(1,1,5)==6);
    constexpr const char* keys[]={"venice_battle","block_battle","casino_battle","skate_battle","sand_battle",
        "old_CookieLand_gc","old_House_ds","old_battle4_sfc","old_battle3_gba","old_matenro_64"};
    for(const auto* key:keys){
        std::ifstream f(std::filesystem::path(argv[1])/"courses"/key/"course.kmp",std::ios::binary);assert(f);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});const uint8_t* starts=nullptr;
        for(unsigned i=0;i<be16(data.data()+8);++i){const auto* s=data.data()+be16(data.data()+10)+be32(data.data()+16+i*4);
            if(!std::memcmp(s,"KTPT",4))starts=s;}
        assert(starts&&be16(starts+4)>=12);std::array<const uint8_t*,12> slots{};
        for(unsigned i=0;i<be16(starts+4);++i){const auto* p=starts+8+i*28;const unsigned slot=be16(p+24);
            if(slot<12){assert(!slots[slot]);slots[slot]=p;}}
        for(const auto* p:slots)assert(p);
        for(unsigned offset=0;offset<6;++offset){std::array<bool,12> used{};
            for(unsigned i=0;i<12;++i){const unsigned slot=mkw::battleStartSlot(i&1,i/2,offset);assert(slot<12&&!used[slot]);used[slot]=true;
                assert((slot>=6)==bool(i&1));assert(slots[slot]);}}
        std::printf("%s: all 12 authored team starts resolve for all 6 offsets\n",key);
    }
    std::puts("PASS original team KTPT start selection; no ordinary race-grid offset");
}
