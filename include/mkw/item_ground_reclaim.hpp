#pragma once
#include <array>
#include <cstdint>
namespace mkw {
// ObjHolder::SortObjs USA8078d33c: preferred KCL masks stay later
// in the holder, so less-preferred ground objects are reclaimed first.
inline bool groundReclaimBefore(uint32_t a,uint32_t b){
    constexpr std::array<uint32_t,15> masks{0x40,0x80,0x100,0x80000,0x200000,
        0x800,0x400000,0x1,0x800000,0x2,0x20,0x4,0x8,0x10,0x200};
    for(auto mask:masks){
        if(a&mask)return false;
        if(b&mask)return true;
    }
    return false;
}
// CanDisappearFromDuration USA80796ea8. Age is object age, not
// time on the ground; >300 is eligibility, never an unconditional lifetime.
inline bool groundReclaimEligible(uint32_t age,bool groundState,bool floor,bool exploding){
    return age>300&&groundState&&floor&&!exploding;
}
}

namespace mkw {
// World portion of ObjHolder's pointer array. Landing swaps into the registered
// prefix; removing a registered object shifts that prefix before compacting the
// remaining world objects (8078cf5c, 8078dbb0).
template<unsigned Capacity> struct GroundReclaimOrder {
    std::array<uint8_t,Capacity> slots{};
    unsigned count=0,registered=0;
    unsigned find(unsigned slot) const {
        unsigned i=0;while(i<count&&slots[i]!=slot)++i;return i;
    }
    void add(unsigned slot){if(find(slot)==count&&count<Capacity)slots[count++]=uint8_t(slot);}
    void land(unsigned slot){
        unsigned i=find(slot);if(i>=count||i<registered)return;
        const auto other=slots[registered];slots[registered++]=uint8_t(slot);slots[i]=other;
    }
    void remove(unsigned slot){
        unsigned i=find(slot);if(i==count)return;
        if(i<registered){
            --registered;
            for(;i<registered;++i)slots[i]=slots[i+1];
        }
        slots[i]=slots[--count];
    }
    template<class Eligible,class Mask,class Locked>
    void sort(unsigned phase,bool banana,Eligible eligible,Mask mask,Locked locked){
        const int end=int(registered)-(banana?3:0);
        for(unsigned j=phase;int(j)<end;j+=8){
            if(!j)continue;
            const auto a=slots[j-1],b=slots[j];
            if(locked(a)||!eligible(b))continue;
            if(!eligible(a)||groundReclaimBefore(mask(b),mask(a))){slots[j-1]=b;slots[j]=a;}
        }
    }
};
}
