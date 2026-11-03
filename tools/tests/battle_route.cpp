#include <mkw/battle_route.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>

using namespace mkw;
static unsigned be16(const uint8_t* p){return unsigned(p[0])<<8|p[1];}
static unsigned be32(const uint8_t* p){return unsigned(p[0])<<24|unsigned(p[1])<<16|unsigned(p[2])<<8|p[3];}
int main(int argc,char** argv){
    assert(argc==2);
    std::array<unsigned,12> links{};unsigned count=0;
    uint8_t groups[48];std::memset(groups,255,sizeof groups);
    for(unsigned i=0;i<3;++i){groups[i*16]=uint8_t(i*2);groups[i*16+1]=2;groups[i*16+14]=groups[i*16+15]=0;}
    groups[2]=1; // Group 0 entry goes back to group 1's last point.
    groups[8]=2; // Group 0 exit goes to group 2's first point.
    assert(battleRouteSuccessors(groups,3,6,0,links,count)&&count==2&&links[0]==3&&links[1]==1);
    assert(battleRouteSuccessors(groups,3,6,1,links,count)&&count==2&&links[0]==0&&links[1]==4);
    groups[15]=0x41; // Both raw link endpoint bits reversed.
    assert(battleRouteSuccessors(groups,3,6,0,links,count)&&count==2&&links[0]==2&&links[1]==1);
    assert(battleRouteSuccessors(groups,3,6,1,links,count)&&count==2&&links[0]==0&&links[1]==5);
    groups[1]=1; // Native singleton ignores its incoming ENPH links.
    assert(battleRouteSuccessors(groups,3,6,0,links,count)&&count==1&&links[0]==5);
    groups[8]=3;assert(!battleRouteSuccessors(groups,3,6,0,links,count));
    constexpr const char* keys[]={"venice_battle","block_battle","casino_battle","skate_battle","sand_battle",
        "old_CookieLand_gc","old_House_ds","old_battle4_sfc","old_battle3_gba","old_matenro_64"};
    constexpr unsigned expected[]={175,177,144,239,47,36,236,138,200,99};
    for(unsigned arena=0;arena<10;++arena){
        std::ifstream f(std::filesystem::path(argv[1])/"courses"/keys[arena]/"course.kmp",std::ios::binary);assert(f);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});const uint8_t *points=nullptr,*paths=nullptr;
        for(unsigned i=0;i<be16(data.data()+8);++i){const auto* s=data.data()+be16(data.data()+10)+be32(data.data()+16+i*4);
            if(!std::memcmp(s,"ENPT",4))points=s;if(!std::memcmp(s,"ENPH",4))paths=s;}
        assert(points&&paths);const unsigned size=be16(points+4),n=be16(paths+4);assert(size==expected[arena]);
        unsigned total=0,reversing=0;
        for(unsigned i=0;i<size;++i){assert(battleRouteSuccessors(paths+8,n,size,i,links,count));assert(count&&count<=12);
            total+=count;for(unsigned j=0;j<count;++j){assert(links[j]<size);if(links[j]<i)++reversing;}}
        assert(reversing);std::printf("arena %u: %u points, %u authored links (%u toward lower point ids)\n",32+arena,size,total,reversing);
    }
    std::puts("PASS Battle ENPH endpoint flags and singleton rules; all ten original KMP successor tables");
}
