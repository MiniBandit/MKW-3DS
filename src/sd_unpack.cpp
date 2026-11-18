// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
// First start of the one-CIA install: copy the CIA's game files to the SD
// card so quick update CIAs (which replace the CIA) still find them.
#include <mkw/sd_unpack.hpp>
#include <3ds.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace mkw {
namespace {
struct Entry {std::string path;uint64_t size;};
constexpr const char* Marker="unpack.txt";
constexpr const char* Content="content.txt";

std::string join(const char* root,const std::string& relative){
    std::string out=root;
    if(!out.empty()&&out.back()!='/')out+='/';
    return out+relative;
}
// Every game file under the RomFS root, except the update overlay and the
// content stamp (written last).
bool list(const char* root,const std::string& relative,std::vector<Entry>& out,uint64_t& total){
    DIR* dir=opendir(join(root,relative).c_str());
    if(!dir)return false;
    bool ok=true;
    while(dirent* e=readdir(dir)){
        if(!std::strcmp(e->d_name,".")||!std::strcmp(e->d_name,".."))continue;
        const std::string path=relative.empty()?std::string(e->d_name):relative+"/"+e->d_name;
        struct stat st{};
        if(stat(join(root,path).c_str(),&st)!=0){ok=false;break;}
        if(S_ISDIR(st.st_mode)){
            if(path=="overlay")continue;
            if(!list(root,path,out,total)){ok=false;break;}
        }else if(path!=Content&&path!=Marker){
            out.push_back({path,uint64_t(st.st_size)});total+=uint64_t(st.st_size);
        }
    }
    closedir(dir);
    return ok;
}
void readLine(const std::string& path,char (&text)[80]){
    text[0]=0;
    if(FILE* f=std::fopen(path.c_str(),"r")){if(!std::fgets(text,sizeof text,f))text[0]=0;std::fclose(f);}
    text[std::strcspn(text,"\r\n")]=0;
}
bool writeLine(const std::string& path,const char* line){
    FILE* f=std::fopen(path.c_str(),"w");if(!f)return false;
    bool ok=std::fprintf(f,"%s\n",line)>0;if(std::fclose(f))ok=false;
    return ok;
}
void makeParents(const char* root,const std::string& relative){
    for(std::size_t at=relative.find('/');at!=std::string::npos;at=relative.find('/',at+1))
        mkdir(join(root,relative.substr(0,at)).c_str(),0777);
}
uint64_t sdFree(){
    FS_ArchiveResource r{};
    if(R_FAILED(FSUSER_GetArchiveResource(&r,SYSTEM_MEDIATYPE_SD)))return ~0ull;
    return uint64_t(r.freeClusters)*r.clusterSize;
}
// The text console draws into the top framebuffer, which the LCD only shows
// once it is presented: every message is presented as soon as it is printed.
void present(){gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
double gb(uint64_t bytes){return double(bytes)/(1024.0*1024.0*1024.0);}
void bar(uint64_t done,uint64_t total){
    const unsigned width=30,filled=total?unsigned(done*width/total):width;
    char line[64];unsigned n=0;line[n++]='[';
    for(unsigned i=0;i<width;++i)line[n++]=i<filled?'#':'.';
    line[n++]=']';line[n]=0;
    std::printf("\x1b[14;3H%s %3u%%  \x1b[15;3H%.2f of %.2f GB   ",line,unsigned(total?done*100/total:100),gb(done),gb(total));
    present();
}
// Message, then wait for A (the game then starts from the CIA).
void notice(){
    std::printf("\n\n  Press A to start the game.");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_A)break;present();}
}
void restoreGraphics(){gfxExit();gfxInitDefault();}
}

UnpackResult unpackGameFiles(const char* romfs,const char* sd,const char* content,const char* version){
    char found[80];readLine(join(sd,Content),found);
    if(!std::strcmp(found,content))return UnpackResult::NotNeeded;
    std::vector<Entry> files;uint64_t total=0;
    if(!list(romfs,"",files,total)||files.empty())return UnpackResult::Failed;
    mkdir("sdmc:/3ds",0777);mkdir(sd,0777);
    // Resuming the same content's copy keeps files already the right size;
    // anything else (another version's files) is copied again.
    char marker[80];readLine(join(sd,Marker),marker);
    const bool resume=!std::strcmp(marker,content);
    uint64_t needed=0,done=0;
    std::vector<bool> keep(files.size(),false);
    for(std::size_t i=0;i<files.size();++i){
        struct stat st{};
        const bool present=stat(join(sd,files[i].path).c_str(),&st)==0;
        keep[i]=resume&&present&&uint64_t(st.st_size)==files[i].size;
        if(keep[i])done+=files[i].size;
        // Replacing a file frees its old size first.
        else needed+=files[i].size-(present&&uint64_t(st.st_size)<files[i].size?uint64_t(st.st_size):(present?files[i].size:0));
    }
    consoleInit(GFX_TOP,nullptr);
    std::printf("\n  Mario Kart Wii 3DS v%s\n\n",version);
    present();
    const uint64_t free=sdFree(),margin=64ull<<20;
    if(free!=~0ull&&needed+margin>free){
        std::printf("  First start: the game files are copied\n  to the SD card (sd:/3ds/mkw-3ds) so\n  quick updates can find them.\n\n");
        std::printf("  Not enough free space on the SD card:\n  this needs %.2f GB more, and %.2f GB\n  is free.\n\n",gb(needed+margin),gb(free));
        std::printf("  The game runs from the CIA for now.\n  Free some space and start it again\n  before installing a quick update.");
        notice();restoreGraphics();return UnpackResult::NoSpace;
    }
    std::printf("  First start: copying the game files to\n  the SD card (sd:/3ds/mkw-3ds), once,\n  so quick updates can find them.\n\n");
    std::printf("  This takes a few minutes. Keep the\n  console on and the SD card in.\n\n");
    std::printf("  Your saves and ghosts are not touched.");
    present();
    if(!resume&&!writeLine(join(sd,Marker),content)){
        std::printf("\x1b[17;3HCould not write to the SD card.");notice();restoreGraphics();return UnpackResult::Failed;
    }
    constexpr std::size_t Chunk=512*1024;
    std::unique_ptr<unsigned char[]> buffer(new(std::nothrow) unsigned char[Chunk]);
    if(!buffer){restoreGraphics();return UnpackResult::Failed;}
    bar(done,total);
    uint64_t shown=done;
    for(std::size_t i=0;i<files.size();++i){
        if(keep[i])continue;
        const auto& f=files[i];
        makeParents(sd,f.path);
        FILE* in=std::fopen(join(romfs,f.path).c_str(),"rb");
        FILE* out=in?std::fopen(join(sd,f.path).c_str(),"wb"):nullptr;
        bool ok=in&&out;
        while(ok){
            const std::size_t n=std::fread(buffer.get(),1,Chunk,in);
            if(n&&std::fwrite(buffer.get(),1,n,out)!=n)ok=false;
            done+=n;
            if(done-shown>=(8u<<20)){
                bar(done,total);shown=done;
                if(!aptMainLoop()){if(in)std::fclose(in);if(out)std::fclose(out);restoreGraphics();return UnpackResult::Interrupted;}
            }
            if(n<Chunk){if(std::ferror(in))ok=false;break;}
        }
        const int error=errno;
        if(in)std::fclose(in);
        if(out&&std::fclose(out))ok=false;
        if(!ok){
            std::printf("\x1b[17;3HCould not copy %.60s\n  (error %d).\n\n  The game runs from the CIA for now;\n  it tries the copy again next start.",f.path.c_str(),error);
            notice();restoreGraphics();return UnpackResult::Failed;
        }
    }
    // Last: the content stamp makes the copy count (src/main.cpp contentMatches).
    if(!writeLine(join(sd,Content),content)){
        std::printf("\x1b[17;3HCould not finish the copy.");notice();restoreGraphics();return UnpackResult::Failed;
    }
    std::remove(join(sd,Marker).c_str());
    bar(total,total);
    restoreGraphics();
    return UnpackResult::Copied;
}
}
