#pragma once
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>
#include "mkw/road_sampler_ranges.hpp"
namespace mkw::roadSampling {
// A split road draw only changes the primary LOD. Rebinding that sampler
// is sufficient; do not rebuild its otherwise identical lighting/TEV state.
template<class Row> bool primaryLodOnly(const Row& a,const Row& b){
    if(!a[0].valid||!b[0].valid||(a[0].minFilter<2)==(b[0].minFilter<2))return false;
    auto x=a,y=b;x[0].minFilter&=1;y[0].minFilter&=1;
    return std::memcmp(x.data(),y.data(),sizeof(x))==0;
}
// Apply once on loading the exact static course mesh. Texture allocation,
// mip payloads, vertices/indices, authored order and non-primary samplers stay
// intact. Other draws sharing an atlas keep their original sampling.
template<class Model> bool apply(Model& model,const char* path){
    const char* name=std::strrchr(path,'/');name=name?name+1:path;
    const Mesh* mesh=nullptr;
    for(const auto& r:Meshes)if(!std::strcmp(name,r.name)&&model.vertexCount==r.vertices&&
        model.indexCount==r.indices&&model.submeshes.size()==r.submeshes){mesh=&r;break;}
    if(!mesh||model.samplers.size()!=model.submeshes.size())return false;
    if(!model.uvTracks.empty()&&model.uvTracks.size()!=model.submeshes.size())return false;
    decltype(model.submeshes) subs;
    decltype(model.samplers) samplers;
    decltype(model.uvTracks) tracks;
    subs.reserve(model.submeshes.size()+mesh->count*2);
    samplers.reserve(subs.capacity());if(!model.uvTracks.empty())tracks.reserve(subs.capacity());
    bool changed=false;
    for(unsigned i=0;i<model.submeshes.size();++i){
        const auto& old=model.submeshes[i];
        std::vector<uint32_t> cuts={old.first,old.first+old.count};
        for(unsigned j=0;j<mesh->count;++j){const auto& r=mesh->ranges[j];
            if(r.texture!=old.texture||r.first>=old.first+old.count||r.first+r.count<=old.first)continue;
            cuts.push_back(std::max(old.first,r.first));cuts.push_back(std::min(old.first+old.count,r.first+r.count));
        }
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        // Zero-length authored draws still have a material/animation entry.
        if(cuts.size()==1)cuts.push_back(cuts[0]);
        for(unsigned k=0;k+1<cuts.size();++k){
            auto sub=old;sub.first=cuts[k];sub.count=cuts[k+1]-cuts[k];
            auto row=model.samplers[i];
            for(unsigned j=0;j<mesh->count;++j){const auto& r=mesh->ranges[j];
                if(r.texture==sub.texture&&sub.first>=r.first&&sub.first<r.first+r.count&&row[0].valid&&row[0].minFilter>=2){
                    row[0].minFilter&=1;changed=true;break;
                }
            }
            subs.push_back(sub);samplers.push_back(row);
            if(!model.uvTracks.empty())tracks.push_back(model.uvTracks[i]);
        }
    }
    if(!changed)return false;
    model.submeshes=std::move(subs);model.samplers=std::move(samplers);model.uvTracks=std::move(tracks);
    return true;
}
}
