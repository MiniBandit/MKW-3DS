#pragma once
#include <array>
#include <cstdint>

namespace mkw {
// MapdataEnemyPoint constructor, USA80512900..80513118. Battle merges the
// authored incoming and outgoing connections into its successor table.
// ENPH+14 bits select the endpoint of each linked group; simply reversing
// every directed ENPT edge loses these endpoint choices and singleton rules.
inline bool battleRouteSuccessors(const uint8_t* groups,unsigned groupCount,unsigned pointCount,
                                  unsigned point,std::array<unsigned,12>& out,unsigned& count){
    count=0;if(!groups||point>=pointCount)return false;
    for(unsigned g=0;g<groupCount;++g){
        const auto* p=groups+g*16;const unsigned start=p[0],length=p[1];
        if(!length||start+length>pointCount)return false;
        if(point<start||point>=start+length)continue;
        const unsigned flags=unsigned(p[14])<<8|p[15];
        auto endpoint=[&](unsigned group,bool last){
            if(group>=groupCount||count>=out.size())return false;
            const auto* next=groups+group*16;
            if(!next[1]||unsigned(next[0])+next[1]>pointCount)return false;
            out[count++]=next[0]+(last?next[1]-1:0);return true;
        };
        unsigned slot=0;
        if(point==start){
            if(length>1)for(unsigned k=0;k<6;++k)if(p[2+k]!=255){
                if(!endpoint(p[2+k],!(flags&(1u<<slot))))return false;++slot;
            }
        }else out[count++]=point-1;
        slot=0;
        if(point==start+length-1){
            for(unsigned k=0;k<6;++k)if(p[8+k]!=255){
                if(!endpoint(p[8+k],flags&(1u<<(slot+6))))return false;++slot;
            }
        }else out[count++]=point+1;
        return count!=0;
    }
    return false;
}
}
