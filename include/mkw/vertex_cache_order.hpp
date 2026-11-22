#pragma once
// Triangle order for the PICA200 post-transform vertex cache (console,
// 2026-11-16: on as the game runs, behaves like a ~16-entry FIFO; Tipsify for
// 4 entries does as well as for 16). The course cook stores the result
// (tools/course_cache_order.cpp, <part>.cord beside each course part);
// Model::enableCacheOrder loads it, or computes it at load where it is missing
// or stale, with the same code below.
#include <mkw/visibility.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mkw::cacheorder {
// Vertices a FIFO of `size` entries shades for `run`, starting cold
// (citro3d clears the cache after every draw).
inline unsigned fifoMisses(const uint16_t* run,unsigned n,unsigned size){
    uint16_t cache[32];unsigned filled=0,head=0,misses=0;
    if(size>32)size=32;
    for(unsigned i=0;i<n;++i){
        const uint16_t v=run[i];bool hit=false;
        for(unsigned k=0;k<filled;++k)if(cache[k]==v){hit=true;break;}
        if(hit)continue;
        ++misses;
        if(filled<size)cache[filled++]=v;else{cache[head]=v;head=(head+1)%size;}
    }
    return misses;
}
// Tipsify (Sander, Nehab and Barczak 2007), as analysis-tools/mesh_report.py
// tipsify(): the same decisions, on fixed arrays (n <= MaxRun, no allocation;
// it runs per culling block at course load). Writes the same triangles to
// out, corners in their order (winding kept). Returns false, out undefined,
// if n is too long or a triangle wasn't placed exactly once (never expected;
// the caller keeps its order).
inline constexpr unsigned MaxRun=384;
inline bool tipsify(const uint16_t* run,unsigned n,unsigned size,uint16_t* out){
    if(n>MaxRun)return false;
    const unsigned tris=n/3;
    // Local ids in order of first appearance (mesh_report's dict order).
    constexpr unsigned HashSize=1024;
    uint16_t hashKey[HashSize];int16_t hashId[HashSize];
    for(auto& h:hashId)h=-1;
    uint16_t verts[MaxRun];unsigned local[MaxRun];unsigned nv=0;
    for(unsigned i=0;i<n;++i){
        unsigned h=(run[i]*2654435761u)>>22;
        while(hashId[h]>=0&&hashKey[h]!=run[i])h=(h+1)&(HashSize-1);
        if(hashId[h]<0){hashKey[h]=run[i];hashId[h]=int16_t(nv);verts[nv++]=run[i];}
        local[i]=unsigned(hashId[h]);
    }
    // Adjacency (vertex -> triangles, in triangle order) as offsets + list.
    uint16_t offset[MaxRun+1]{},adj[MaxRun],fill[MaxRun];
    for(unsigned i=0;i<tris*3;++i)++offset[local[i]+1];
    for(unsigned v=0;v<nv;++v)offset[v+1]+=offset[v];
    for(unsigned v=0;v<nv;++v)fill[v]=offset[v];
    for(unsigned t=0;t<tris;++t)for(unsigned c=0;c<3;++c)adj[fill[local[t*3+c]]++]=uint16_t(t);
    int live[MaxRun];long stamp[MaxRun];uint8_t done[MaxRun/3+1]{};
    for(unsigned v=0;v<nv;++v){live[v]=int(offset[v+1]-offset[v]);stamp[v]=-1000000000L;}
    unsigned dead[MaxRun],deadCount=0,cand[MaxRun],candCount;
    long now=long(size)+1;unsigned cursor=0,written=0,placed=0;
    long f=nv?0:-1;
    while(f>=0){
        candCount=0;
        for(unsigned k=offset[f];k<offset[f+1];++k){
            const unsigned t=adj[k];
            if(done[t])continue;
            if(written+3>tris*3||deadCount+3>MaxRun||candCount+3>MaxRun)return false;
            for(unsigned c=0;c<3;++c){
                const unsigned v=local[t*3+c];
                out[written++]=verts[v];dead[deadCount++]=v;cand[candCount++]=v;--live[v];
                if(now-stamp[v]>long(size)){stamp[v]=now;++now;}
            }
            done[t]=1;++placed;
        }
        long best=-1,bestP=-1;
        for(unsigned i=0;i<candCount;++i){const unsigned v=cand[i];if(live[v]>0){
            long p=0;if(now-stamp[v]+2*live[v]<=long(size))p=now-stamp[v];
            if(p>bestP){best=v;bestP=p;}
        }}
        if(best<0)while(deadCount){const unsigned v=dead[--deadCount];if(live[v]>0){best=v;break;}}
        if(best<0){while(cursor<nv&&live[cursor]<=0)++cursor;best=cursor<nv?long(cursor):-1;}
        f=best;
    }
    if(placed!=tris)return false;
    for(unsigned i=tris*3;i<n;++i)out[written++]=run[i];
    return true;
}
// Triangle order can't show: unblended, depth-tested and depth-written
// (Renderer's orderSafe; the depth state drawNow derives from the same flags).
inline bool orderSafe(uint32_t flags){
    return !(flags&2)&&(!(flags&(1u<<31))||((flags&(1u<<23))&&(flags&(1u<<24))));
}
// FNV-1a of an index list: a cooked order is used only for the exact list it was made from.
inline uint32_t hashIndices(const uint16_t* idx,uint32_t count){
    uint32_t h=2166136261u;const auto* b=reinterpret_cast<const uint8_t*>(idx);
    for(uint32_t i=0;i<count*2;++i)h=(h^b[i])*16777619u;
    return h;
}
struct BlockStats {uint32_t indices=0,before=0,after=0,kept=0;};
// Reorders each order-safe submesh's culling blocks of idx in place (Tipsify,
// kept only where a 16-entry FIFO shades fewer vertices). flags[i] is
// submesh i's flags. Returns the used length of idx.
inline uint32_t reorderBlocks(uint16_t* idx,const uint32_t* flags,size_t submeshes,const std::vector<SubmeshVisibility>& vis,BlockStats& stats){
    uint16_t out[MaxRun];uint32_t used=0;
    for(size_t i=0;i<submeshes&&i<vis.size();++i)for(const auto& b:vis[i].blocks){
        used=used>b.indices.first+b.indices.count?used:b.indices.first+b.indices.count;
        if(!orderSafe(flags[i])||b.indices.count>MaxRun||b.indices.count<6)continue;
        uint16_t* run=idx+b.indices.first;const unsigned n=b.indices.count;
        if(!tipsify(run,n,4,out)){++stats.kept;continue;}
        const unsigned before=fifoMisses(run,n,16),after=fifoMisses(out,n,16);
        // Never worse for the measured cache than the order it replaces.
        if(after>=before){stats.before+=before;stats.after+=before;stats.indices+=n;continue;}
        std::memcpy(run,out,n*sizeof(uint16_t));
        stats.before+=before;stats.after+=after;stats.indices+=n;
    }
    return used;
}
// The whole course-part computation as at load: the spatial contact order
// (Model::enableStaticCulling(true)), its 128-triangle blocks, then the
// reorder. contact receives the spatial order (its hash checks a cooked file),
// cache the reordered list. Vertex: anything with a Vec3 `position`.
template<class Vertex> void courseCacheOrder(const Vertex* vertices,const uint16_t* indices,uint32_t indexCount,
                                             const uint32_t* first,const uint32_t* count,const uint32_t* flags,size_t submeshes,
                                             uint16_t* contact,uint16_t* cache,BlockStats& stats){
    std::memcpy(contact,indices,indexCount*sizeof(uint16_t));
    std::vector<SubmeshVisibility> coarse;coarse.reserve(submeshes);
    for(size_t i=0;i<submeshes;++i){
        spatialShadowIndices(vertices,indices,contact,first[i],count[i]);
        coarse.push_back(buildVisibility(vertices,contact,first[i],count[i]));
    }
    std::memcpy(cache,contact,indexCount*sizeof(uint16_t));
    reorderBlocks(cache,flags,submeshes,coarse,stats);
}
}
