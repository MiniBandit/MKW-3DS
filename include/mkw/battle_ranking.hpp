#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
namespace mkw {
// Shared Battle-mode805338d0 reads last position, seeds that
// order, then insertion-sorts with a strict score comparison80533b68..b94.
// Ties retain the last frame's order; balloon count is not another key.
class BattleRanking {
public:
    void reset(unsigned count){
        count_=std::min(count,12u);
        for(unsigned i=0;i<12;++i){order_[i]=uint8_t(i);ranks_[i]=uint8_t(i+1);}
    }
    void step(const std::array<uint16_t,12>& scores){
        for(unsigned i=1;i<count_;++i){const uint8_t racer=order_[i];unsigned at=i;
            while(at&&scores[order_[at-1]]<scores[racer]){order_[at]=order_[at-1];--at;}
            order_[at]=racer;
        }
        for(unsigned i=0;i<count_;++i)ranks_[order_[i]]=uint8_t(i+1);
    }
    unsigned rank(unsigned racer)const{return racer<count_?ranks_[racer]:count_;}
private:
    std::array<uint8_t,12> order_{},ranks_{};unsigned count_=0;
};
}
