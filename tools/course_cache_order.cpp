// Course parts' triangle order for the post-transform vertex cache, cooked.
//
//     course_cache_order <part.gmdl> [...]
//
// For each course part GMDL it runs what Model::enableStaticCulling(true) and
// Model::enableCacheOrder do at course load (mkw/vertex_cache_order.hpp
// courseCacheOrder: the spatial contact order, its 128-triangle culling
// blocks, Tipsify inside each order-safe block) and writes <part>.cord beside
// it: "CORD", version 1, index count, FNV-1a of the contact order, then the
// reordered indices. The game uses the file only when the contact order it
// builds at load hashes the same, and computes the order itself otherwise,
// so a stale or missing file only costs load time. A part whose order
// doesn't change gets no file (an old one is removed). One JSON line per part.
#include <mkw/vertex_cache_order.hpp>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Vertex {mkw::Vec3 position;};
bool readFile(const char* path,std::vector<uint8_t>& out){
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    std::fseek(f,0,SEEK_END);const long n=std::ftell(f);std::fseek(f,0,SEEK_SET);
    out.resize(n>0?size_t(n):0);const bool ok=n>0&&std::fread(out.data(),1,out.size(),f)==out.size();
    std::fclose(f);return ok;
}
uint32_t u32(const std::vector<uint8_t>& d,size_t at){uint32_t v;std::memcpy(&v,d.data()+at,4);return v;}
}

int main(int argc,char** argv){
    if(argc<2){std::fprintf(stderr,"usage: course_cache_order <part.gmdl> [...]\n");return 2;}
    int failures=0;
    for(int a=1;a<argc;++a){
        const std::string path=argv[a];
        std::vector<uint8_t> d;
        if(!readFile(path.c_str(),d)||d.size()<24||std::memcmp(d.data(),"GMDL",4)){std::fprintf(stderr,"%s: not a GMDL\n",path.c_str());++failures;continue;}
        const uint32_t version=u32(d,4),nv=u32(d,8),ni=u32(d,12),ns=u32(d,16),nt=u32(d,20);
        const uint32_t record=version==3?44:version==4?56:version==5?92:0;
        if(!record){std::fprintf(stderr,"%s: GMDL version %u\n",path.c_str(),version);++failures;continue;}
        const size_t vat=24+size_t(ns)*record+size_t(nt)*64,iat=vat+size_t(nv)*44;
        if(iat+size_t(ni)*2>d.size()){std::fprintf(stderr,"%s: truncated\n",path.c_str());++failures;continue;}
        std::vector<Vertex> vertices(nv);
        for(uint32_t v=0;v<nv;++v)std::memcpy(&vertices[v].position,d.data()+vat+size_t(v)*44,12);
        std::vector<uint16_t> indices(ni),contact(ni),cache(ni);
        std::memcpy(indices.data(),d.data()+iat,size_t(ni)*2);
        std::vector<uint32_t> first(ns),count(ns),flags(ns);
        bool bad=false;
        for(uint32_t s=0;s<ns;++s){
            const size_t at=24+size_t(s)*record;first[s]=u32(d,at);count[s]=u32(d,at+4);flags[s]=u32(d,at+12);
            if(size_t(first[s])+count[s]>ni)bad=true;
        }
        for(uint16_t i:indices)if(i>=nv)bad=true;
        if(bad){std::fprintf(stderr,"%s: index or submesh range out of bounds\n",path.c_str());++failures;continue;}
        mkw::cacheorder::BlockStats stats;
        mkw::cacheorder::courseCacheOrder(vertices.data(),indices.data(),ni,first.data(),count.data(),flags.data(),ns,contact.data(),cache.data(),stats);
        std::string out=path;
        if(out.size()>5&&out.compare(out.size()-5,5,".gmdl")==0)out.replace(out.size()-5,5,".cord");else out+=".cord";
        const bool changed=cache!=contact;
        if(changed){
            const uint32_t head[4]={0x44524F43u,1u,ni,mkw::cacheorder::hashIndices(contact.data(),ni)}; // "CORD" little-endian
            FILE* f=std::fopen(out.c_str(),"wb");
            if(!f||std::fwrite(head,sizeof head,1,f)!=1||std::fwrite(cache.data(),2,ni,f)!=ni){if(f)std::fclose(f);std::fprintf(stderr,"%s: write failed\n",out.c_str());++failures;continue;}
            if(std::fclose(f)){std::fprintf(stderr,"%s: write failed\n",out.c_str());++failures;continue;}
        }else std::remove(out.c_str());
        std::printf("{\"part\": \"%s\", \"indices\": %u, \"fifo16_before\": %u, \"fifo16_after\": %u, \"blocks_kept\": %u, \"written\": %s}\n",
                    path.c_str(),ni,stats.before,stats.after,stats.kept,changed?"true":"false");
    }
    return failures?1:0;
}
