#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
namespace mkw {
// Load-only index remapping. Animation keeps its authored vertex buffers and
// ranges; only identical future draw inputs in the SAME track domains share
// an index. UV/visibility splits and bone matrices are therefore preserved.
template<class Vertex> unsigned shareSkinDrawIndices(const Vertex* vertices,const float* bones,const uint64_t* domains,unsigned count,uint16_t* indices,unsigned indexCount){
 if(!vertices||!domains||!indices||!count||count>65535)return 0;
 std::unique_ptr<uint16_t[]> order(new(std::nothrow)uint16_t[count]),representative(new(std::nothrow)uint16_t[count]);
 if(!order||!representative)return 0;
 auto compare=[&](unsigned a,unsigned b){
  const int v=std::memcmp(vertices+a,vertices+b,sizeof(Vertex));if(v)return v;
  if(bones){const int bone=std::memcmp(bones+a,bones+b,sizeof(float));if(bone)return bone;}
  return domains[a]<domains[b]?-1:domains[a]>domains[b]?1:0;
 };
 for(unsigned i=0;i<count;++i)order[i]=uint16_t(i);
 std::sort(order.get(),order.get()+count,[&](uint16_t a,uint16_t b){const int c=compare(a,b);return c?c<0:a<b;});
 unsigned unique=0;
 for(unsigned at=0;at<count;){const auto first=order[at++];representative[first]=first;++unique;
  while(at<count&&!compare(first,order[at]))representative[order[at++]]=first;
 }
 if(unique==count)return 0;
 for(unsigned i=0;i<indexCount;++i)indices[i]=representative[indices[i]];
 return count-unique;
}
}
