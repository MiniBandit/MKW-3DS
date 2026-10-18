#include <mkw/time_trial_ghost.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace mkw {
namespace {
// Kinoko's RawGhostFile copies RkgSize bytes from an uncompressed ghost, so
// the buffer is at least that long; size is the file's own length.
bool readWhole(FILE* f,std::vector<uint8_t>& out,std::size_t& size){
    bool ok=std::fseek(f,0,SEEK_END)==0;
    const long length=ok?std::ftell(f):-1;
    ok=ok&&length>0&&length<=0x10000&&std::fseek(f,0,SEEK_SET)==0;
    if(ok){
        size=std::size_t(length);
        out.assign(std::max(size,RkgSize),0);
        ok=std::fread(out.data(),1,size,f)==size;
    }
    return ok;
}
bool readFile(const char* path,std::vector<uint8_t>& out,std::size_t& size){
    FILE* f=std::fopen(path,"rb");
    if(!f)return false;
    const bool ok=readWhole(f,out,size);
    std::fclose(f);
    return ok;
}
void bestPath(char* out,std::size_t n,unsigned course){std::snprintf(out,n,"%s/pb-%02x.rkg",TimeTrialGhost::Folder,course);}
// Every RKG in the folder whose header and CRC check out: visit(file,size,header,name).
template<class Visit> void eachGhost(Visit&& visit){
    DIR* dir=opendir(TimeTrialGhost::Folder);
    if(!dir)return;
    std::vector<uint8_t> file;
    while(const dirent* e=readdir(dir)){
        const std::size_t length=std::strlen(e->d_name);
        if(length<4||strcasecmp(e->d_name+length-4,".rkg"))continue;
        char path[300];
        std::snprintf(path,sizeof path,"%s/%s",TimeTrialGhost::Folder,e->d_name);
        std::size_t size=0;GhostHeader h;
        if(readFile(path,file,size)&&readRkgHeader(file.data(),size,h))visit(file,size,h,e->d_name);
    }
    closedir(dir);
}
}

bool TimeTrialGhost::load(unsigned course,bool(*usable)(unsigned,unsigned)){
    clear();
    uint32_t best=~0u;
    std::vector<uint8_t> section;
    std::vector<GhostFrame> frames;
    char own[16];std::snprintf(own,sizeof own,"pb-%02x.rkg",course);
    eachGhost([&](std::vector<uint8_t>& file,std::size_t size,const GhostHeader& h,const char* name){
        const uint32_t time=rkgMilliseconds(h.finish);
        if(h.course!=course||!usable(h.character,h.vehicle)||!time||time>=best)return;
        if(!rkgInputSection(file.data(),size,section)||!decodeGhostInputs(section.data(),section.size(),frames))return;
        best=time;file_.swap(file);frames_.swap(frames);header_=h;own_=!strcasecmp(name,own);
    });
    return ready();
}

void TimeTrialGhost::fastest(std::array<uint32_t,64>& times,bool(*usable)(unsigned,unsigned)){
    times={};
    auto faster=[&](const GhostHeader& h){
        const uint32_t time=rkgMilliseconds(h.finish);
        return h.course<times.size()&&time&&usable(h.character,h.vehicle)&&(!times[h.course]||time<times[h.course]);
    };
    // The header ranks a ghost. Only one that would beat its course's best so
    // far is read whole and checked (CRC-32) as eachGhost() does, so the
    // minimum over the valid ghosts is the same with far less reading.
    DIR* dir=opendir(Folder);
    if(!dir)return;
    std::vector<uint8_t> file;
    while(const dirent* e=readdir(dir)){
        const std::size_t length=std::strlen(e->d_name);
        if(length<4||strcasecmp(e->d_name+length-4,".rkg"))continue;
        char path[300];
        std::snprintf(path,sizeof path,"%s/%s",Folder,e->d_name);
        FILE* f=std::fopen(path,"rb");
        if(!f)continue;
        uint8_t head[0x10];GhostHeader h;std::size_t size=0;
        const bool candidate=std::fread(head,1,sizeof head,f)==sizeof head&&peekRkgHeader(head,sizeof head,h)&&faster(h);
        const bool whole=candidate&&readWhole(f,file,size);
        std::fclose(f);
        if(whole&&readRkgHeader(file.data(),size,h)&&faster(h))times[h.course]=rkgMilliseconds(h.finish);
    }
    closedir(dir);
}

uint32_t TimeTrialGhost::savedTime(unsigned course){
    char path[128];bestPath(path,sizeof path,course);
    std::vector<uint8_t> file;std::size_t size=0;GhostHeader h;
    return readFile(path,file,size)&&readRkgHeader(file.data(),size,h)?rkgMilliseconds(h.finish):0;
}

// A temporary file first, so a pulled card keeps the previous ghost.
bool TimeTrialGhost::save(const GhostRecorder& run,const GhostInfo& info){
    std::vector<uint8_t> rkg(RkgSize);
    if(!buildRkg(run,info,rkg.data()))return false;
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/mkw-3ds",0777);mkdir(Folder,0777);
    char path[128],temporary[128];
    bestPath(path,sizeof path,info.course);
    std::snprintf(temporary,sizeof temporary,"%s/pb.tmp",Folder);
    FILE* f=std::fopen(temporary,"wb");
    if(!f)return false;
    const bool ok=std::fwrite(rkg.data(),1,rkg.size(),f)==rkg.size();
    if(std::fclose(f)!=0||!ok){std::remove(temporary);return false;}
    std::remove(path);
    return std::rename(temporary,path)==0;
}
}
