// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
// First start of the one-CIA install: copy the CIA's game files to the SD
// card so quick update CIAs (which replace the CIA) still find them.
#include <mkw/sd_unpack.hpp>
#include <3ds.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mkw {
namespace {
struct Entry {std::string path;uint64_t size;};
constexpr const char* Marker="unpack.txt";
constexpr const char* Content="content.txt";
// The player chose to play from the CIA for this content (one line: its stamp).
constexpr const char* NoCopy="no-copy.txt";
// Earlier versions' game folders are moved here before a copy (see below).
constexpr const char* OldFiles="old-files";

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
// Each folder is made once per copy: asking the card again for every file
// is one more slow round trip per file.
void makeParents(const char* root,const std::string& relative){
    static std::vector<std::string> made;
    for(std::size_t at=relative.find('/');at!=std::string::npos;at=relative.find('/',at+1)){
        const auto folder=relative.substr(0,at);
        bool done=false;for(const auto& m:made)done|=m==folder;
        if(done)continue;
        mkdir(join(root,folder).c_str(),0777);made.push_back(folder);
    }
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
// Copy speed: bytes copied this start and when the copy began.
uint64_t copyStartTick=0,copyStartDone=0;
double secondsSince(uint64_t tick){return double(svcGetSystemTick()-tick)/double(SYSCLOCK_ARM11);}
void bar(uint64_t done,uint64_t total){
    const unsigned width=30,filled=total?unsigned(done*width/total):width;
    char line[64];unsigned n=0;line[n++]='[';
    for(unsigned i=0;i<width;++i)line[n++]=i<filled?'#':'.';
    line[n++]=']';line[n]=0;
    std::printf("\x1b[14;3H%s %3u%%  \x1b[15;3H%.2f of %.2f GB   ",line,unsigned(total?done*100/total:100),gb(done),gb(total));
    const double seconds=copyStartTick?secondsSince(copyStartTick):0;
    const double rate=seconds>2?double(done-copyStartDone)/seconds:0;
    if(rate>0)std::printf("\x1b[16;3H%.1f MB/s, about %u min left    ",rate/1048576.0,unsigned(double(total-done)/rate/60.0)+1);
    present();
}
// sd:/3ds/mkw-3ds/first-start-copy.txt: what the copy did and how fast,
// for reports from consoles (written a line at a time, rarely).
std::string copyLogPath;
void copyLog(const char* format,...){
    if(copyLogPath.empty())return;
    FILE* f=std::fopen(copyLogPath.c_str(),"a");if(!f)return;
    va_list args;va_start(args,format);std::vfprintf(f,format,args);va_end(args);
    std::fputc('\n',f);std::fclose(f);
}
// Message, then wait for A (the game then starts from the CIA).
void notice(){
    std::printf("\n\n  Press A to start the game.");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_A)break;present();}
}
void restoreGraphics(){gfxExit();gfxInitDefault();}
bool isDirectory(const std::string& path){struct stat st{};return stat(path.c_str(),&st)==0&&S_ISDIR(st.st_mode);}
// The game folders the copy writes (Race, models, textures...), moved aside
// whole first. A FAT folder never shrinks: 0.7.0's loose files left models/
// and textures/ megabytes long, and the console reads a whole folder for
// every file it looks up or creates there, so checking and copying into them
// took long enough to look like a black-screen hang. A move is one rename;
// the new files then go into fresh, small folders. Saves, ghosts and
// settings are top-level files or other folders and stay. Returns how many
// folders moved.
unsigned moveOldFolders(const char* sd,const std::vector<Entry>& files){
    // Since 0.8.0 the game files are all inside the packs, so the folders
    // 0.7.0 and earlier filled with loose game files are named here too.
    // Saves, ghosts and settings are never in them.
    std::vector<std::string> folders={"Race","audio","models","movies","textures","mii"};
    for(const auto& f:files){
        const auto slash=f.path.find('/');if(slash==std::string::npos)continue;
        const auto top=f.path.substr(0,slash);
        bool seen=false;for(const auto& d:folders)seen|=d==top;
        if(!seen)folders.push_back(top);
    }
    unsigned moved=0;bool made=false;
    for(const auto& folder:folders){
        const auto from=join(sd,folder);
        if(!isDirectory(from))continue;
        if(!made){mkdir(join(sd,OldFiles).c_str(),0777);made=true;}
        // old-files/models, or models-2 ... when an earlier move left one.
        for(unsigned n=1;n<100;++n){
            const auto to=join(sd,std::string(OldFiles)+"/"+folder+(n>1?"-"+std::to_string(n):""));
            if(isDirectory(to))continue;
            if(rename(from.c_str(),to.c_str())==0)++moved;
            break;
        }
    }
    return moved;
}
// Names and sizes of the files already on the SD card, read a folder at a
// time (FSDIR_Read hands back 64 entries with their sizes per call). Asking
// file by file (stat) opens and closes each one: about 1,900 of those took
// minutes on hardware. Only the game's own folders are read.
void listCard(FS_Archive card,const std::string& folder,const std::string& relative,bool recurse,
              const std::vector<std::string>& gameFolders,std::unordered_map<std::string,uint64_t>& out){
    Handle dir=0;
    if(R_FAILED(FSUSER_OpenDirectory(&dir,card,fsMakePath(PATH_ASCII,folder.c_str()))))return;
    FS_DirectoryEntry entries[64];
    for(;;){
        u32 read=0;
        if(R_FAILED(FSDIR_Read(dir,&read,64,entries))||!read)break;
        for(u32 i=0;i<read;++i){
            char name[262];unsigned n=0;
            for(;n<sizeof name-1&&entries[i].name[n];++n)name[n]=entries[i].name[n]<128?char(entries[i].name[n]):'?';
            name[n]=0;
            const std::string path=relative.empty()?std::string(name):relative+"/"+name;
            if(entries[i].attributes&FS_ATTRIBUTE_DIRECTORY){
                bool game=!relative.empty();
                if(!game)for(const auto& g:gameFolders)game|=g==name;
                if(recurse&&game)listCard(card,folder+"/"+name,path,true,gameFolders,out);
            }else out[path]=entries[i].fileSize;
        }
    }
    FSDIR_Close(dir);
}
}

UnpackResult unpackGameFiles(const char* romfs,const char* sd,const char* content,const char* version,bool ask){
    char found[80];readLine(join(sd,Content),found);
    if(!std::strcmp(found,content))return UnpackResult::NotNeeded;
    char skipped[80];readLine(join(sd,NoCopy),skipped);
    if(!ask&&!std::strcmp(skipped,content))return UnpackResult::Skipped;
    // The screen first: everything below reads the SD card.
    uint64_t phaseTick=svcGetSystemTick();
    consoleInit(GFX_TOP,nullptr);
    std::printf("\n  Mario Kart Wii 3DS v%s\n\n  Checking the SD card...",version);
    present();
    std::vector<Entry> files;uint64_t total=0;
    if(!list(romfs,"",files,total)||files.empty()){restoreGraphics();return UnpackResult::Failed;}
    mkdir("sdmc:/3ds",0777);mkdir(sd,0777);
    // Resuming the same content's copy keeps files already the right size;
    // anything else (another version's files) is copied again, into fresh
    // folders: the old ones move to old-files first.
    char marker[80];readLine(join(sd,Marker),marker);
    const bool resume=!std::strcmp(marker,content);
    copyLogPath=join(sd,"first-start-copy.txt");
    if(FILE* f=std::fopen(copyLogPath.c_str(),resume?"a":"w"))std::fclose(f);
    copyLog("v%s content %s: %u files, %.2f GB, %s (listed in %.1f s)",version,content,unsigned(files.size()),gb(total),resume?"resuming":"new copy",secondsSince(phaseTick));
    phaseTick=svcGetSystemTick();
    const unsigned moved=resume?0:moveOldFolders(sd,files);
    copyLog("old game folders moved to old-files: %u (%.1f s)",moved,secondsSince(phaseTick));
    FS_Archive card=0;
    if(R_FAILED(FSUSER_OpenArchive(&card,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,"")))){copyLog("FAIL SD archive");restoreGraphics();return UnpackResult::Failed;}
    // "sdmc:/3ds/mkw-3ds" -> "/3ds/mkw-3ds" inside the SD archive.
    const char* sdInArchive=std::strncmp(sd,"sdmc:",5)?sd:sd+5;
    std::vector<std::string> gameFolders;
    for(const auto& f:files){const auto slash=f.path.find('/');if(slash==std::string::npos)continue;
        const auto top=f.path.substr(0,slash);bool seen=false;for(const auto& g:gameFolders)seen|=g==top;if(!seen)gameFolders.push_back(top);}
    phaseTick=svcGetSystemTick();
    std::unordered_map<std::string,uint64_t> onCard;
    listCard(card,sdInArchive,"",true,gameFolders,onCard);
    copyLog("files already on the card: %u (%.1f s)",unsigned(onCard.size()),secondsSince(phaseTick));
    uint64_t needed=0,done=0;
    std::vector<bool> keep(files.size(),false);
    for(std::size_t i=0;i<files.size();++i){
        const auto found=onCard.find(files[i].path);
        const bool present=found!=onCard.end();const uint64_t size=present?found->second:0;
        keep[i]=resume&&present&&size==files[i].size;
        if(keep[i])done+=files[i].size;
        // Replacing a file frees its old size first.
        else needed+=files[i].size-(present&&size<files[i].size?size:(present?files[i].size:0));
    }
    consoleClear();
    std::printf("\n  Mario Kart Wii 3DS v%s\n\n",version);
    present();
    const uint64_t free=sdFree(),margin=64ull<<20;
    if(free!=~0ull&&needed+margin>free){
        std::printf("  First start: the game files are copied\n  to the SD card (sd:/3ds/mkw-3ds) so\n  quick updates can find them.\n\n");
        std::printf("  Not enough free space on the SD card:\n  this needs %.2f GB more, and %.2f GB\n  is free.\n\n",gb(needed+margin),gb(free));
        if(moved)std::printf("  Older game files were moved to\n  sd:/3ds/mkw-3ds/old-files: deleting\n  that folder on a computer frees space.\n\n");
        std::printf("  The game runs from the CIA for now.\n  Free some space and start it again\n  before installing a quick update.");
        FSUSER_CloseArchive(card);notice();restoreGraphics();return UnpackResult::NoSpace;
    }
    // A new copy: the player chooses (a copy resumed after an interruption was chosen already).
    if(!resume){
        std::printf("  First start: copy the game files to\n  the SD card? (%.2f GB, a few minutes,\n  only once)\n\n",gb(total));
        std::printf("  A  Copy them. Quick updates (QR code)\n     then install in seconds.\n\n");
        std::printf("  B  Don't copy: play from the CIA and\n     save the space. To update, install\n     a new full CIA from your builder.\n\n");
        std::printf("  Changed your mind? Hold Y while the\n  game starts to be asked again.");
        present();
        bool copy=false;
        while(aptMainLoop()){
            hidScanInput();const u32 down=hidKeysDown();
            if(down&KEY_A){copy=true;break;}
            if(down&KEY_B)break;
            present();
        }
        if(!copy){
            FSUSER_CloseArchive(card);
            if(!aptMainLoop()){restoreGraphics();return UnpackResult::Interrupted;}
            writeLine(join(sd,NoCopy),content);copyLog("player chose to play from the CIA");
            restoreGraphics();return UnpackResult::Skipped;
        }
        std::remove(join(sd,NoCopy).c_str());
        consoleClear();
        std::printf("\n  Mario Kart Wii 3DS v%s\n\n",version);
    }
    std::printf("  First start: copying the game files to\n  the SD card (sd:/3ds/mkw-3ds), once,\n  so quick updates can find them.\n\n");
    std::printf("  This takes a while, once. Keep the\n  console on and the SD card in.\n\n");
    std::printf("  Your saves and ghosts are not touched.");
    if(moved)std::printf("\x1b[18;3HOlder game files moved to\n  sd:/3ds/mkw-3ds/old-files: you can\n  delete that folder on a computer.");
    present();
    if(!resume&&!writeLine(join(sd,Marker),content)){
        std::printf("\x1b[17;3HCould not write to the SD card.");FSUSER_CloseArchive(card);notice();restoreGraphics();return UnpackResult::Failed;
    }
    // Straight to the 3DS file system: the C library's buffered files can
    // pass large reads and writes on in small pieces, each paying the SD
    // card's whole delay (on hardware the first copy ran at about 33 KB/s).
    // The RomFS is read unbuffered, each SD file gets its full size first
    // (no cluster-by-cluster growth) and both move 1 MiB at a time.
    constexpr std::size_t Chunk=1024*1024;
    std::unique_ptr<unsigned char[]> buffer(new(std::nothrow) unsigned char[Chunk]);
    if(!buffer){FSUSER_CloseArchive(card);copyLog("FAIL buffer");restoreGraphics();return UnpackResult::Failed;}
    copyStartTick=svcGetSystemTick();copyStartDone=done;
    bar(done,total);
    uint64_t shown=done,logged=done;
    auto fail=[&](const char* what,const std::string& path,long code){
        copyLog("FAIL %s %s (%ld)",what,path.c_str(),code);
        std::printf("\x1b[17;3HCould not copy %.60s\n  (%s, %ld).\n\n  The game runs from the CIA for now;\n  it tries the copy again next start.",path.c_str(),what,code);
        FSUSER_CloseArchive(card);notice();restoreGraphics();return UnpackResult::Failed;
    };
    for(std::size_t i=0;i<files.size();++i){
        if(keep[i])continue;
        if(!aptMainLoop()){FSUSER_CloseArchive(card);copyLog("interrupted at %.2f GB",gb(done));restoreGraphics();return UnpackResult::Interrupted;}
        const auto& f=files[i];
        const uint64_t fileTick=svcGetSystemTick();
        makeParents(sd,f.path);
        const int in=open(join(romfs,f.path).c_str(),O_RDONLY);
        if(in<0)return fail("read",f.path,errno);
        const std::string target=std::string(sdInArchive)+"/"+f.path;
        Handle out=0;
        Result rc=FSUSER_OpenFile(&out,card,fsMakePath(PATH_ASCII,target.c_str()),FS_OPEN_WRITE|FS_OPEN_CREATE,0);
        if(R_SUCCEEDED(rc))rc=FSFILE_SetSize(out,f.size);
        if(R_FAILED(rc)){close(in);if(out)FSFILE_Close(out);return fail("create",f.path,long(rc));}
        uint64_t offset=0;bool ok=true;long code=0;
        while(offset<f.size){
            const ssize_t n=read(in,buffer.get(),Chunk);
            if(n<=0){ok=false;code=errno;break;}
            u32 written=0;
            rc=FSFILE_Write(out,&written,offset,buffer.get(),u32(n),0);
            if(R_FAILED(rc)||written!=u32(n)){ok=false;code=long(rc);break;}
            offset+=uint64_t(n);done+=uint64_t(n);
            if(done-shown>=(8u<<20)){bar(done,total);shown=done;}
            if(done-logged>=(64u<<20)){
                const double seconds=secondsSince(copyStartTick);
                copyLog("%.2f GB at %.0f s, %.2f MB/s (%s)",gb(done),seconds,seconds>0?double(done-copyStartDone)/seconds/1048576.0:0.0,f.path.c_str());
                logged=done;
            }
        }
        close(in);
        rc=FSFILE_Close(out);
        if(ok&&R_FAILED(rc)){ok=false;code=long(rc);}
        if(!ok)return fail("write",f.path,code);
        const double seconds=secondsSince(fileTick);
        if(seconds>2)copyLog("slow file %s: %.1f MB in %.1f s",f.path.c_str(),double(f.size)/1048576.0,seconds);
    }
    FSUSER_CloseArchive(card);
    copyLog("copied in %.0f s",secondsSince(copyStartTick));
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
