#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <mkw/race_teams.hpp>
namespace mkw {
// Original ItemSlot.bin: count, then [rank count, item count, item-major bytes].
// Copy the selected 12-racer tables so no race-heap/archive pointer escapes reset.
// RMCU807ae914..807aeb50: compare this racer's score with the highest
// opposing individual's score. Team totals are not the selection input.
inline unsigned battleItemColumn(bool coin,unsigned player,unsigned count,
        const std::array<uint16_t,12>& scores,const std::array<RaceTeam,12>& teams){
    if(player>=count||player>=12)return 1;
    unsigned highest=0;
    for(unsigned i=0;i<std::min(count,12u);++i)
        if(!sameRaceTeam(teams[player],teams[i]))highest=std::max(highest,unsigned(scores[i]));
    const int gap=int(highest)-int(scores[player]);
    const int low=coin?3:count<=8?1:2,high=coin?7:count<=4?3:count<=8?4:5;
    return gap<=low?1:gap<=high?2:3;
}
class ItemOdds {
public:
    bool load(const void* bytes,std::size_t size,unsigned localTable,unsigned cpuTable,unsigned mode=0){
        tables_={};rows_={12,12};ready_=false;
        if(!bytes||size<1)return false;
        const auto* data=static_cast<const uint8_t*>(bytes);std::size_t offset=1;unsigned found=0;
        for(unsigned table=0;table<data[0];++table){
            if(size-offset<2)return false;
            const unsigned ranks=data[offset++],items=data[offset++];
            if(!ranks||!items||std::size_t(ranks)*items>size-offset)return false;
            for(unsigned role=0;role<2;++role)if(table==(role?cpuTable:localTable)){
                if((ranks!=12&&ranks!=3)||items!=19)return false;
                rows_[role]=ranks;
                for(unsigned item=0;item<19;++item)for(unsigned rank=0;rank<ranks;++rank)
                    tables_[role][rank][item]=adjust(data[offset+item*ranks+rank],item,mode);
                ++found;
            }
            offset+=std::size_t(ranks)*items;
        }
        ready_=found==2&&offset==size;return ready_;
    }
    bool ready() const{return ready_;}
    const std::array<uint16_t,19>& weights(bool cpu,unsigned rank)const{return tables_[cpu?1:0][std::clamp(rank,1u,rows_[cpu?1:0])-1];}
private:
    static uint16_t adjust(unsigned weight,unsigned item,unsigned mode){
        // RMCU 807AE080 initializes these categories; 808A15D0/E4 supplies
        // the Aggressive/Strategic multipliers and offsets, then truncates.
        constexpr unsigned category[]={0,1,0,1,0,1,1,2,2,2,2,2,1,1,1,2,1,2,0};
        constexpr float scale[2][3]={{.25f,1,4},{8,.25f,0}};
        constexpr unsigned bias[2][3]={{3,0,0},{0,3,0}};
        if(mode==3)return 0;
        if(mode==1||mode==2)return uint16_t((weight+bias[mode-1][category[item]])*scale[mode-1][category[item]]);
        return uint16_t(weight);
    }
    std::array<std::array<std::array<uint16_t,19>,12>,2> tables_{};
    std::array<unsigned,2> rows_{12,12};
    bool ready_=false;
};
// Shipped woodboxes use object table5, setting1=6. This table bypasses the
// VS Aggressive/Strategic multipliers (ProcessTable r7=1, USA807ac740).
class WoodboxOdds {
public:
    bool load(const void* bytes,std::size_t size){
        ready_=false;weights_={};
        if(!bytes||size<1)return false;
        const auto* data=static_cast<const uint8_t*>(bytes);std::size_t offset=1;bool found=false;
        for(unsigned table=0;table<data[0];++table){
            if(size-offset<2)return false;
            const unsigned columns=data[offset++],items=data[offset++];
            if(!columns||!items||std::size_t(columns)*items>size-offset)return false;
            if(table==5){
                if(columns!=16||items!=19)return false;
                for(unsigned item=0;item<19;++item){
                    const unsigned w=data[offset+item*columns+5];
                    if(item==2)weights_[0]=w;
                    else if(item==4)weights_[1]=w;
                    else if(w)return false; // other customized item kinds are not this verified row
                }
                found=true;
            }
            offset+=std::size_t(columns)*items;
        }
        ready_=found&&offset==size;return ready_;
    }
    bool ready()const{return ready_;}
    std::array<unsigned,2> weights()const{return weights_;}
private:
    std::array<unsigned,2> weights_{};
    bool ready_=false;
};

}
