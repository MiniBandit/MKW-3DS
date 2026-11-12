#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
namespace mkw {
// Bounded frame-local actor lists: stable merge order without heap allocation.
// Equality keeps the left/original entry first, as std::stable_sort does.
template<class T,std::size_t N,class Less>
void boundedStableSort(std::array<T,N>& values,std::size_t count,Less less){
    assert(count<=N);
    if(count<2)return;
    std::array<T,N> scratch;
    T* src=values.data();T* dst=scratch.data();
    for(std::size_t width=1;width<count;width*=2){
        for(std::size_t base=0;base<count;base+=2*width){
            std::size_t left=base,right=std::min(base+width,count);
            const auto middle=right,end=std::min(base+2*width,count);
            for(std::size_t out=base;out<end;++out)
                dst[out]=right<end&&(left==middle||less(src[right],src[left]))?src[right++]:src[left++];
        }
        std::swap(src,dst);
    }
    if(src!=values.data())std::copy_n(src,count,values.data());
}
}
