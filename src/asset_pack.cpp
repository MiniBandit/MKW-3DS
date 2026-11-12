#include <mkw/asset_pack.hpp>
#include <3ds.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <cerrno>
#include <malloc.h>
#include <memory>
#include <new>
#include <unordered_map>
#ifndef MKW_RELEASE
#include <cstdarg>
#include <string>
#endif

namespace mkw {extern std::atomic<unsigned> packReads;extern std::atomic<unsigned long long> packReadBytes,packReadTicks;}
namespace mkw::pack {
// Why the last pack read failed (asset_pack.hpp; opponent-load-error.txt).
const char* lastReadFailure="";
namespace {
// stored: bytes in the file; less than size for an LZ4 block (MPK2, tools/pakpack.c).
struct Entry {std::uint32_t name,offset,size,stored;};
struct Archive {
 const char* path;std::FILE* file=nullptr;
 std::FILE* aheadFile=nullptr; // the prefetch thread's own handle (traced packs)
 std::vector<Entry> entries;std::vector<char> names;
 std::vector<std::uint32_t> ends; // each entry's end, in file order (read-ahead)
 std::uint32_t tracedEnd=0; // end of the entries laid out in traced read order (MPK2)
 const Entry* find(const char* key)const{
  auto it=std::lower_bound(entries.begin(),entries.end(),key,[&](const Entry& e,const char* k){return std::strcmp(names.data()+e.name,k)<0;});
  return it!=entries.end()&&!std::strcmp(names.data()+it->name,key)?&*it:nullptr;
 }
};
// The SD card charges about one extra request (~1.25 ms on the New 3DS) for
// each partial 512-byte sector at either end of a read. Reads up to
// stagedBytes go through a staging buffer as whole sectors and are copied
// out; above that the copy costs more than it saves.
constexpr std::uint32_t sector=512,stagedBytes=512u<<10;
// A read also takes the whole entries that follow it within readAhead bytes
// of its first sector: the game often reads neighbours next (a kart part's
// small textures), and a cached one costs no request. Pack traces put the
// saving at 0.6-1.1 s per race load; wider windows read too much unused.
// Among entries laid out in the order the game reads them (tools/pak_order.py)
// the window is tracedReadAhead: nearly everything it takes is read next.
constexpr std::uint32_t readAhead=12u<<10,tracedReadAhead=128u<<10;
// Race-load RAM cache: the races of a VS set or Grand Prix re-read the same
// racers, items, effects and Lakitu (about 23 MB), and the pause menu's
// menu.pak textures (442 KB in 44 reads, ~0.37 s on the console). Whole entries
// read inside a cache scope are kept on the heap while it can spare them, and
// later reads of them are copied from RAM. Raw bytes: loading still parses and
// lights them for the new course. A race peaks near 54 MB of heap (Bowser's
// Castle), so the cache stays under heap size - RacePeakHeap.
constexpr std::uint32_t RacePeakHeap=62u<<20,KeepFree=12u<<20;
struct Kept {std::unique_ptr<unsigned char[]> data;std::uint32_t size;bool used;};
// Prefetch, among traced entries only: while the game parses what it read, a
// second thread reads the next window on its own file handle. SD requests run
// one at a time, but they overlap the parsing. state (under lock): 0 idle,
// 1 reading [first, end), 2 holds got bytes from first.
struct Prefetch {
 Thread thread=nullptr;LightLock lock;LightEvent wake,done;
 unsigned char* buffer=nullptr;Archive* archive=nullptr;
 std::uint32_t first=0,end=0;size_t got=0;int state=0;bool quit=false;
};
// Menu and race assets use the original two archives; stream.pak holds the
// files read as streams (movies, sound), stored and sector-aligned.
struct Archives {
 LightLock lock;bool tried=false;
 // Set by the first open(), after main() moves into the asset folder:
 // openFile serves fopen from the packs only from then on.
 bool rooted=false;
 std::array<Archive,3> list{{{"menu.pak"},{"race.pak"},{"stream.pak"}}};
 unsigned char* staging=nullptr;bool sectorReads=true,readAhead=true;
 // The bytes in staging: [cachedFirst, cachedEnd) of cached's file.
 const Archive* cached=nullptr;std::uint32_t cachedFirst=0,cachedEnd=0;
 // RAM cache: keyed by keptKey (archive and entry offset).
 std::unordered_map<std::uint64_t,Kept> kept;size_t keptBytes=0,keepLimit=0;bool caching=false;
 // readRange of a compressed entry: the entry decoded whole, until its last byte is read.
 std::unique_ptr<unsigned char[]> decoded;const Archive* decodedArchive=nullptr;std::uint32_t decodedOffset=0;
 Prefetch ahead;bool prefetch=true;
#ifndef MKW_RELEASE
 // DIAG: pack-trace.txt logs every read (archive, offset, size, ms, cache hit,
 // matches a direct read) to pack-trace.csv.
 std::FILE* trace=nullptr;
#endif
 Archives(){LightLock_Init(&lock);LightLock_Init(&ahead.lock);LightEvent_Init(&ahead.wake,RESET_ONESHOT);LightEvent_Init(&ahead.done,RESET_STICKY);}
};
Archives& archives(){static Archives value;return value;}
std::uint32_t u32(const unsigned char* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
bool load(Archive& a){
 std::FILE* f=std::fopen(a.path,"rb");if(!f)return false;
 std::setvbuf(f,nullptr,_IONBF,0);
 unsigned char header[12];
 if(std::fread(header,1,12,f)!=12||(std::memcmp(header,"MPK1",4)&&std::memcmp(header,"MPK2",4))){std::fclose(f);return false;}
 // MPK2 records also hold each entry's stored size.
 const unsigned record=header[3]=='2'?16:12;
 const std::uint32_t count=u32(header+4),namesBytes=u32(header+8);
 if(!count||count>200000||!namesBytes||namesBytes>(16u<<20)){std::fclose(f);return false;}
 std::vector<unsigned char> table(size_t(count)*record);
 a.names.resize(namesBytes);a.entries.resize(count);
 if(std::fread(table.data(),1,table.size(),f)!=table.size()||std::fread(a.names.data(),1,namesBytes,f)!=namesBytes||a.names.back()!=0){std::fclose(f);a.entries.clear();return false;}
 for(std::uint32_t i=0;i<count;++i){auto& e=a.entries[i];const unsigned char* r=&table[size_t(i)*record];
  e.name=u32(r);e.offset=u32(r+4);e.size=u32(r+8);e.stored=record==16?u32(r+12)&0x7fffffffu:e.size;
  if(e.name>=namesBytes||e.stored>e.size){std::fclose(f);a.entries.clear();return false;}
  if(record==16&&(u32(r+12)>>31))a.tracedEnd=std::max(a.tracedEnd,e.offset+e.stored);}
 a.ends.resize(count);for(std::uint32_t i=0;i<count;++i)a.ends[i]=a.entries[i].offset+a.entries[i].stored;
 std::sort(a.ends.begin(),a.ends.end());
 if(a.tracedEnd)if((a.aheadFile=std::fopen(a.path,"rb")))std::setvbuf(a.aheadFile,nullptr,_IONBF,0);
 a.file=f;return true;
}
void prefetchThread(void*);
// Starts the prefetch thread once a traced pack is open.
void startPrefetch(Archives& all){
 auto& p=all.ahead;
 if(!all.prefetch||p.thread)return;
 bool traced=false;for(const auto& a:all.list)traced|=a.aheadFile!=nullptr;
 if(!traced)return;
 if(!p.buffer)p.buffer=static_cast<unsigned char*>(std::aligned_alloc(64,stagedBytes+2*sector));
 if(!all.staging)all.staging=static_cast<unsigned char*>(std::aligned_alloc(64,stagedBytes+2*sector));
 if(!p.buffer||!all.staging)return;
 // Above the reading thread's priority: it wakes as soon as its read is done.
 s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
 p.quit=false;p.state=0;p.thread=threadCreate(prefetchThread,nullptr,16u<<10,priority-1,-2,false);
}
void stopPrefetch(Archives& all){
 auto& p=all.ahead;
 if(p.thread){
  LightLock_Lock(&p.lock);p.quit=true;LightLock_Unlock(&p.lock);LightEvent_Signal(&p.wake);
  threadJoin(p.thread,U64_MAX);threadFree(p.thread);p.thread=nullptr;
 }
 std::free(p.buffer);p.buffer=nullptr;p.state=0;p.archive=nullptr;
}
// With the lock held: the archive holding path, or null.
Archive* lookup(Archives& all,const char* path,const Entry** entry){
 if(!all.tried){
  all.tried=true;for(auto& a:all.list)load(a);
#ifndef MKW_RELEASE
  // DIAG A/B: no-sector-reads.txt reads entries exactly as stored.
  if(std::FILE* off=std::fopen("sdmc:/3ds/mkw-3ds/no-sector-reads.txt","rb")){std::fclose(off);all.sectorReads=false;}
  // no-read-ahead.txt: sector reads of the entry alone.
  if(std::FILE* off=std::fopen("sdmc:/3ds/mkw-3ds/no-read-ahead.txt","rb")){std::fclose(off);all.readAhead=false;}
  if(!all.trace)if(std::FILE* on=std::fopen("sdmc:/3ds/mkw-3ds/pack-trace.txt","rb")){std::fclose(on);all.trace=std::fopen("sdmc:/3ds/mkw-3ds/pack-trace.csv","a");}
  // no-prefetch.txt: no background reads.
  if(std::FILE* off=std::fopen("sdmc:/3ds/mkw-3ds/no-prefetch.txt","rb")){std::fclose(off);all.prefetch=false;}
#endif
  startPrefetch(all);
 }
 for(auto& a:all.list)if(a.file)if(const Entry* e=a.find(path)){*entry=e;return &a;}
 return nullptr;
}
std::uint64_t keptKey(const Archives& all,const Archive& a,const Entry& e){return std::uint64_t(&a-all.list.data())<<32|e.offset;}
// With the lock held: the kept copy of e, if any.
Kept* keptEntry(Archives& all,const Archive& a,const Entry& e){
 if(all.kept.empty())return nullptr;
 auto it=all.kept.find(keptKey(all,a,e));if(it==all.kept.end())return nullptr;
 it->second.used=true;return &it->second;
}
// With the lock held: keeps a copy of e's bytes while the heap can spare them.
void keep(Archives& all,const Archive& a,const Entry& e,const void* bytes){
 if(!all.caching||all.keptBytes+e.stored>all.keepLimit||all.kept.count(keptKey(all,a,e)))return;
 const struct mallinfo m=mallinfo();
 const size_t free=envGetHeapSize()-m.arena+m.fordblks;
 if(free<size_t(e.stored)+KeepFree)return;
 std::unique_ptr<unsigned char[]> data(new(std::nothrow) unsigned char[e.stored?e.stored:1]);if(!data)return;
 std::memcpy(data.get(),bytes,e.stored);
 all.kept.emplace(keptKey(all,a,e),Kept{std::move(data),e.stored,true});all.keptBytes+=e.stored;
}
// With the lock held: bytes at offset in a's file, into destination.
bool readAtUntraced(Archives& all,Archive& a,std::uint32_t offset,void* destination,size_t bytes);
bool readAt(Archives& all,Archive& a,std::uint32_t offset,void* destination,size_t bytes){
#ifndef MKW_RELEASE
 if(all.trace){
  const bool hit=all.cached==&a&&offset>=all.cachedFirst&&offset+bytes<=all.cachedEnd;
  const auto start=svcGetSystemTick();const bool ok=readAtUntraced(all,a,offset,destination,bytes);
  const double took=double(svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11;
  // Each read is checked against a direct read of the same bytes.
  int same=-1;
  if(ok)if(void* check=std::malloc(bytes?bytes:1)){
   same=std::fseek(a.file,long(offset),SEEK_SET)==0&&std::fread(check,1,bytes,a.file)==bytes&&!std::memcmp(check,destination,bytes);std::free(check);
  }
  std::fprintf(all.trace,"%d,%lu,%lu,%.3f,%d,%d\n",int(&a-all.list.data()),(unsigned long)offset,(unsigned long)bytes,took,int(hit),same);
  return ok;
 }
#endif
 return readAtUntraced(all,a,offset,destination,bytes);
}
void prefetchThread(void*){
 auto& p=archives().ahead;
 for(;;){
  LightEvent_Wait(&p.wake);
  LightLock_Lock(&p.lock);
  if(p.quit){LightLock_Unlock(&p.lock);return;}
  Archive* a=p.archive;const std::uint32_t first=p.first,end=p.end;unsigned char* buffer=p.buffer;
  const bool reading=p.state==1;
  LightLock_Unlock(&p.lock);
  if(!reading)continue;
  size_t got=0;
  if(a&&a->aheadFile&&std::fseek(a->aheadFile,long(first),SEEK_SET)==0)got=std::fread(buffer,1,end-first,a->aheadFile);
  LightLock_Lock(&p.lock);p.got=got;p.state=2;LightLock_Unlock(&p.lock);
  LightEvent_Signal(&p.done);
 }
}
// With the lock held: asks the prefetch thread for the whole traced entries
// from `from` (a sector boundary) within tracedReadAhead, unless it's busy.
void schedulePrefetch(Archives& all,Archive& a,std::uint32_t from){
 auto& p=all.ahead;
 if(!p.thread||!a.aheadFile||from>=a.tracedEnd)return;
 std::uint32_t last=from;const std::uint32_t limit=std::min(from+tracedReadAhead,a.tracedEnd);
 for(auto it=std::upper_bound(a.ends.begin(),a.ends.end(),from);it!=a.ends.end()&&*it<=limit;++it)last=*it;
 if(last<=from)return;
 LightLock_Lock(&p.lock);
 if(p.state==1){LightLock_Unlock(&p.lock);return;}
 p.archive=&a;p.first=from;p.end=(last+sector-1)&~(sector-1);p.got=0;p.state=1;
 LightEvent_Clear(&p.done);
 LightLock_Unlock(&p.lock);
 LightEvent_Signal(&p.wake);
}
// With the lock held: serves the read from the prefetched window when it
// covers it (waiting for a read in progress), which becomes the staged window.
bool fromPrefetch(Archives& all,Archive& a,std::uint32_t offset,void* destination,size_t bytes){
 auto& p=all.ahead;
 if(!p.thread)return false;
 LightLock_Lock(&p.lock);
 const bool covers=p.state!=0&&p.archive==&a&&offset>=p.first&&offset+bytes<=p.end;
 const bool reading=p.state==1;
 LightLock_Unlock(&p.lock);
 if(!covers)return false;
 if(reading)LightEvent_Wait(&p.done);
 LightLock_Lock(&p.lock);
 const bool ok=p.state==2&&offset+bytes<=p.first+p.got;
 if(ok){
  std::swap(all.staging,p.buffer);
  all.cached=&a;all.cachedFirst=p.first;all.cachedEnd=p.first+std::uint32_t(p.got);
 }
 p.state=0;
 LightLock_Unlock(&p.lock);
 if(!ok)return false;
 std::memcpy(destination,all.staging+(offset-all.cachedFirst),bytes);
 schedulePrefetch(all,a,all.cachedEnd);
 return true;
}
bool readAtUntraced(Archives& all,Archive& a,std::uint32_t offset,void* destination,size_t bytes){
 if(all.cached==&a&&offset>=all.cachedFirst&&offset+bytes<=all.cachedEnd){std::memcpy(destination,all.staging+(offset-all.cachedFirst),bytes);return true;}
 if(fromPrefetch(all,a,offset,destination,bytes))return true;
 const std::uint32_t first=offset&~(sector-1);
 std::uint32_t end=std::uint32_t((size_t(offset)+bytes+sector-1)&~size_t(sector-1));
 if(all.sectorReads&&bytes<=stagedBytes){
  if(all.readAhead){
   // Whole following entries that end inside the window.
   std::uint32_t last=std::uint32_t(offset+bytes);
   const std::uint32_t limit=std::max(first+readAhead,std::min(first+tracedReadAhead,a.tracedEnd));
   for(auto it=std::upper_bound(a.ends.begin(),a.ends.end(),last);it!=a.ends.end()&&*it<=limit;++it)last=*it;
   end=std::max(end,(last+sector-1)&~(sector-1));
  }
  if(first!=offset||end-first!=bytes){
   if(!all.staging)all.staging=static_cast<unsigned char*>(std::aligned_alloc(64,stagedBytes+2*sector));
   if(all.staging){
    all.cached=nullptr;
    // The last sector may run past the end of the file.
    if(std::fseek(a.file,long(first),SEEK_SET)!=0){lastReadFailure="staged seek";return false;}
    const size_t got=std::fread(all.staging,1,end-first,a.file);
    if(got<offset-first+bytes){lastReadFailure="staged short read";return false;}
    all.cached=&a;all.cachedFirst=first;all.cachedEnd=first+std::uint32_t(got);
    std::memcpy(destination,all.staging+(offset-first),bytes);
    schedulePrefetch(all,a,all.cachedEnd);
    return true;
   }
  }
 }
 if(std::fseek(a.file,long(offset),SEEK_SET)!=0){lastReadFailure="direct seek";return false;}
 if(std::fread(destination,1,bytes,a.file)!=bytes){lastReadFailure="direct short read";return false;}
 return true;
}
// An LZ4 block (tools/pakpack.c) into exactly size bytes; false if malformed.
bool unlz4(const unsigned char* in,size_t n,unsigned char* out,size_t size){
 const unsigned char* const end=in+n;unsigned char* o=out;unsigned char* const outEnd=out+size;
 while(in<end){
  const unsigned token=*in++;size_t literals=token>>4;
  if(literals==15){unsigned b;do{if(in==end)return false;b=*in++;literals+=b;}while(b==255);}
  if(literals>size_t(end-in)||literals>size_t(outEnd-o))return false;
  // Short runs (most of them) as two fixed 8-byte copies when both sides have
  // room; bytes past the run are rewritten by what follows.
  if(literals<=16&&end-in>=16&&outEnd-o>=16){std::memcpy(o,in,8);std::memcpy(o+8,in+8,8);}
  else std::memcpy(o,in,literals);
  in+=literals;o+=literals;
  if(in==end)break;
  if(end-in<2)return false;
  const size_t distance=in[0]|in[1]<<8;in+=2;
  size_t length=token&15;
  if(length==15){unsigned b;do{if(in==end)return false;b=*in++;length+=b;}while(b==255);}
  length+=4;
  if(!distance||distance>size_t(o-out)||length>size_t(outEnd-o))return false;
  const unsigned char* from=o-distance;
  // 8-byte steps are exact when the match starts 8 or more bytes back: each
  // step reads only bytes already written. Nearer matches repeat bytewise.
  if(distance>=8&&size_t(outEnd-o)>=length+8)for(size_t i=0;i<length;i+=8)std::memcpy(o+i,from+i,8);
  else if(distance>=length)std::memcpy(o,from,length);
  else for(size_t i=0;i<length;++i)o[i]=from[i];
  o+=length;
 }
 return o==outEnd;
}
// With the lock held: the whole of e, decoded, into destination (e.size bytes).
bool readEntry(Archives& all,Archive& a,const Entry& e,void* destination){
 auto* out=static_cast<unsigned char*>(destination);
 const bool raw=e.stored==e.size;
 if(const Kept* k=keptEntry(all,a,e)){
  if(raw){std::memcpy(out,k->data.get(),e.size);return true;}
  return unlz4(k->data.get(),e.stored,out,e.size);
 }
 if(raw){if(!readAt(all,a,e.offset,out,e.size)){lastReadFailure="raw read";return false;}keep(all,a,e,out);return true;}
 std::unique_ptr<unsigned char[]> packed(new(std::nothrow) unsigned char[e.stored?e.stored:1]);
 // A fragmented heap (Old 3DS, after a race is released) can have the bytes
 // free but no block this size: then the compressed copy borrows LINEAR.
 struct LinearScratch{void* p=nullptr;~LinearScratch(){if(p)linearFree(p);}} borrowed;
 unsigned char* in=packed.get();
 if(!in)in=static_cast<unsigned char*>(borrowed.p=linearAlloc(e.stored?e.stored:1));
 if(!in){lastReadFailure="lz4 buffer allocation";return false;}
 if(!readAt(all,a,e.offset,in,e.stored)){lastReadFailure="lz4 read";return false;}
 if(!unlz4(in,e.stored,out,e.size)){lastReadFailure="lz4 decode";return false;}
 keep(all,a,e,in);return true;
}
}
namespace {
// openFile streams (funopen cookies).
#ifdef __LARGE64_FILES
using FilePosition=_fpos64_t;
#else
using FilePosition=fpos_t;
#endif
FilePosition seekWithin(std::uint32_t& at,std::uint32_t size,FilePosition offset,int whence){
 const long long target=(whence==SEEK_SET?0:whence==SEEK_CUR?(long long)at:(long long)size)+offset;
 if((whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END)||target<0||target>(long long)size){errno=EINVAL;return -1;}
 at=std::uint32_t(target);return target;
}
// An entry read whole: small ones (one pack read, through the staging window
// and the RAM cache) and compressed ones.
struct MemoryFile {std::unique_ptr<unsigned char[]> data;std::uint32_t size=0,at=0;};
int memoryRead(void* cookie,char* out,size_t n){
 auto& f=*static_cast<MemoryFile*>(cookie);const size_t k=std::min<size_t>(n,f.size-f.at);
 std::memcpy(out,f.data.get()+f.at,k);f.at+=std::uint32_t(k);return int(k);
}
FilePosition memorySeek(void* cookie,FilePosition offset,int whence){auto& f=*static_cast<MemoryFile*>(cookie);return seekWithin(f.at,f.size,offset,whence);}
int memoryClose(void* cookie){delete static_cast<MemoryFile*>(cookie);return 0;}
// A large stored entry (a movie, a sound stream, a course archive), read from
// its pack on its own handle so it seeks independently of every other read.
// Large reads go straight to the card; small ones (headers, records) come
// from one window of whole sectors, so they don't each cost a request.
constexpr std::uint32_t WholeEntryBytes=256u<<10,RangeWindow=4u<<10;
struct RangeFile {std::FILE* file=nullptr;std::uint32_t base=0,size=0,at=0,windowFirst=0,windowEnd=0;unsigned char window[RangeWindow];};
int rangeRead(void* cookie,char* out,size_t n){
 auto& f=*static_cast<RangeFile*>(cookie);
 const std::uint32_t bytes=std::uint32_t(std::min<size_t>(n,f.size-f.at)),from=f.base+f.at;
 if(!bytes)return 0;
 if(bytes>RangeWindow-sector){
  if(std::fseek(f.file,long(from),SEEK_SET)!=0||std::fread(out,1,bytes,f.file)!=bytes)return -1;
 }else{
  if(from<f.windowFirst||from+bytes>f.windowEnd){
   const std::uint32_t first=from&~(sector-1);f.windowFirst=f.windowEnd=0;
   if(std::fseek(f.file,long(first),SEEK_SET)!=0)return -1;
   // The last sector may run past the end of the file.
   const size_t got=std::fread(f.window,1,RangeWindow,f.file);
   if(first+got<from+bytes)return -1;
   f.windowFirst=first;f.windowEnd=first+std::uint32_t(got);
  }
  std::memcpy(out,f.window+(from-f.windowFirst),bytes);
 }
 f.at+=bytes;return int(bytes);
}
FilePosition rangeSeek(void* cookie,FilePosition offset,int whence){auto& f=*static_cast<RangeFile*>(cookie);return seekWithin(f.at,f.size,offset,whence);}
int rangeClose(void* cookie){auto* f=static_cast<RangeFile*>(cookie);if(f->file)std::fclose(f->file);delete f;return 0;}
}
bool decodeLz4(const unsigned char* in,size_t n,unsigned char* out,size_t size){return unlz4(in,n,out,size);}
std::FILE* openFile(const char* path){
 // The packs themselves open as files: no lookup, and no re-entry of the lock.
 const size_t length=std::strlen(path);
 if(length>=4&&!std::strcmp(path+length-4,".pak"))return nullptr;
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=all.rooted?lookup(all,path,&e):nullptr;
 if(!a){LightLock_Unlock(&all.lock);return nullptr;}
 std::FILE* result=nullptr;
 if(e->stored!=e->size||e->size<=WholeEntryBytes){
  const auto start=svcGetSystemTick();
  auto* f=new(std::nothrow) MemoryFile;
  if(f){f->size=e->size;f->data.reset(new(std::nothrow) unsigned char[e->size?e->size:1]);}
  if(f&&f->data&&readEntry(all,*a,*e,f->data.get()))result=funopen(f,memoryRead,nullptr,memorySeek,memoryClose);
  if(!result)delete f;
  packReads.fetch_add(1,std::memory_order_relaxed);packReadBytes.fetch_add(e->size,std::memory_order_relaxed);packReadTicks.fetch_add(svcGetSystemTick()-start,std::memory_order_relaxed);
  LightLock_Unlock(&all.lock);
 }else{
  const char* pack=a->path;const std::uint32_t base=e->offset,size=e->size;
  LightLock_Unlock(&all.lock);
  auto* f=new(std::nothrow) RangeFile;
  if(f&&(f->file=std::fopen(pack,"rb"))){
   std::setvbuf(f->file,nullptr,_IONBF,0);f->base=base;f->size=size;
   result=funopen(f,rangeRead,nullptr,rangeSeek,rangeClose);
  }
  if(!result&&f){if(f->file)std::fclose(f->file);delete f;}
 }
 // Reads reach the callbacks whole; callers may still set their own buffer.
 if(result)std::setvbuf(result,nullptr,_IONBF,0);
 return result;
}
#ifndef MKW_RELEASE
void traceMark(const char* name){auto& all=archives();LightLock_Lock(&all.lock);if(all.trace){std::fprintf(all.trace,"#%s\n",name);std::fflush(all.trace);}LightLock_Unlock(&all.lock);}
#endif
void open(){auto& all=archives();LightLock_Lock(&all.lock);all.rooted=true;const Entry* e=nullptr;lookup(all,"",&e);LightLock_Unlock(&all.lock);}
void close(){
 auto& all=archives();LightLock_Lock(&all.lock);
 stopPrefetch(all);
 for(auto& a:all.list){if(a.file)std::fclose(a.file);if(a.aheadFile)std::fclose(a.aheadFile);a.file=a.aheadFile=nullptr;a.entries.clear();a.names.clear();a.ends.clear();a.tracedEnd=0;}
 std::free(all.staging);all.staging=nullptr;all.cached=nullptr;
 all.kept.clear();all.keptBytes=0;all.caching=false;all.decoded.reset();all.decodedArchive=nullptr;
#ifndef MKW_RELEASE
 if(all.trace)std::fflush(all.trace);
#endif
 all.sectorReads=all.readAhead=all.prefetch=true;all.tried=false;LightLock_Unlock(&all.lock);
}
bool contains(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const bool found=lookup(all,path,&e);LightLock_Unlock(&all.lock);return found;}
long size(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const long result=lookup(all,path,&e)?long(e->size):-1;LightLock_Unlock(&all.lock);return result;}
bool readInto(const char* path,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 const auto start=svcGetSystemTick();
 if(!a)lastReadFailure="not in a pack";else if(e->size!=bytes)lastReadFailure="size mismatch";
 const bool ok=a&&e->size==bytes&&readEntry(all,*a,*e,destination);
 if(a){packReads.fetch_add(1,std::memory_order_relaxed);packReadBytes.fetch_add(bytes,std::memory_order_relaxed);packReadTicks.fetch_add(svcGetSystemTick()-start,std::memory_order_relaxed);}
 LightLock_Unlock(&all.lock);return ok;
}
long storedSize(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const long result=lookup(all,path,&e)?long(e->stored):-1;LightLock_Unlock(&all.lock);return result;}
bool readStored(const char* path,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 const bool ok=a&&e->stored==bytes&&readAt(all,*a,e->offset,destination,bytes);
 LightLock_Unlock(&all.lock);return ok;
}
namespace {
// The LZ4 block decoder behind both decodeLz4Frames forms. next(b) gives the
// next input byte (false at the end of the input).
template<class Next>
bool decodeLz4FramesFrom(Next&& next,size_t size,unsigned char* header,size_t head,
                         size_t frameBytes,unsigned stride,unsigned count,unsigned char* out){
 if(!frameBytes||!stride)return false;
 // LZ4 matches reach back at most 65535 bytes: a 64 KiB ring holds every
 // byte a match can copy, so the whole output never has to exist.
 std::unique_ptr<unsigned char[]> ring(new(std::nothrow) unsigned char[65536]);if(!ring)return false;
 size_t pos=0,frameOffset=0;unsigned frame=0;unsigned char* dst=count?out:nullptr;
 auto put=[&](unsigned char b){
  ring[pos&0xffff]=b;
  if(pos<head)header[pos]=b;
  else{
   if(dst)*dst++=b;
   if(++frameOffset==frameBytes){
    frameOffset=0;++frame;
    dst=frame%stride==0&&frame/stride<count?out+size_t(frame/stride)*frameBytes:nullptr;
   }
  }
  ++pos;
 };
 unsigned char token;
 while(next(token)){
  size_t literals=token>>4;unsigned char b;
  if(literals==15){do{if(!next(b))return false;literals+=b;}while(b==255);}
  if(literals>size-pos)return false;
  for(size_t i=0;i<literals;++i){if(!next(b))return false;put(b);}
  unsigned char lo,hi;
  if(!next(lo))break; // the last sequence ends after its literals
  if(!next(hi))return false;
  const size_t distance=lo|size_t(hi)<<8;
  size_t length=token&15;
  if(length==15){do{if(!next(b))return false;length+=b;}while(b==255);}
  length+=4;
  if(!distance||distance>pos||length>size-pos)return false;
  for(size_t i=0;i<length;++i)put(ring[(pos-distance)&0xffff]);
 }
 return pos==size;
}
}
bool decodeLz4Frames(const unsigned char* in,size_t n,size_t size,unsigned char* header,size_t head,
                     size_t frameBytes,unsigned stride,unsigned count,unsigned char* out){
 const unsigned char* const end=in+n;
 return decodeLz4FramesFrom([&](unsigned char& b){if(in==end)return false;b=*in++;return true;},size,header,head,frameBytes,stride,count,out);
}
bool decodeLz4Frames(const char* path,size_t size,unsigned char* header,size_t head,
                     size_t frameBytes,unsigned stride,unsigned count,unsigned char* out){
 // The stored entry read 64 KiB at a time: no buffer the size of the entry
 // (6.2 MB for the firebar stream, more than a fragmented Old 3DS heap holds).
 const long stored=storedSize(path);if(stored<=0)return false;
 constexpr size_t Chunk=65536;
 std::unique_ptr<unsigned char[]> chunk(new(std::nothrow) unsigned char[Chunk]);if(!chunk){lastReadFailure="lz4 chunk allocation";return false;}
 size_t offset=0,have=0,at=0;bool readFailed=false;
 auto next=[&](unsigned char& b){
  if(at==have){
   if(offset>=size_t(stored))return false;
   const size_t bytes=std::min(Chunk,size_t(stored)-offset);
   auto& all=archives();LightLock_Lock(&all.lock);
   const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
   const bool ok=a&&e->stored==size_t(stored)&&readAt(all,*a,e->offset+std::uint32_t(offset),chunk.get(),bytes);
   LightLock_Unlock(&all.lock);
   if(!ok){readFailed=true;return false;}
   offset+=bytes;have=bytes;at=0;
  }
  b=chunk[at++];return true;
 };
 const bool ok=decodeLz4FramesFrom(next,size,header,head,frameBytes,stride,count,out);
 if(!ok)lastReadFailure=readFailed?"lz4 chunk read":"lz4 stream decode";
 return ok&&!readFailed;
}
bool readRange(const char* path,size_t offset,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 const auto start=svcGetSystemTick();
 bool ok=false;
 if(a&&offset<=e->size&&bytes<=e->size-offset){
  if(e->stored!=e->size){
   // Compressed: decode the entry once and serve its ranges (loadVramTexture reads in order).
   if(all.decodedArchive!=a||all.decodedOffset!=e->offset||!all.decoded){
    all.decoded.reset(new(std::nothrow) unsigned char[e->size?e->size:1]);all.decodedArchive=nullptr;
    if(all.decoded&&readEntry(all,*a,*e,all.decoded.get())){all.decodedArchive=a;all.decodedOffset=e->offset;}
   }
   if((ok=all.decodedArchive==a&&all.decodedOffset==e->offset))std::memcpy(destination,all.decoded.get()+offset,bytes);
   if(!ok||offset+bytes==e->size){all.decoded.reset();all.decodedArchive=nullptr;}
  }
  else if(const Kept* k=keptEntry(all,*a,*e)){std::memcpy(destination,k->data.get()+offset,bytes);ok=true;}
  else if((ok=readAt(all,*a,std::uint32_t(e->offset+offset),destination,bytes))&&!offset&&bytes==e->size)keep(all,*a,*e,destination);
 }
 if(a){packReads.fetch_add(1,std::memory_order_relaxed);packReadBytes.fetch_add(bytes,std::memory_order_relaxed);packReadTicks.fetch_add(svcGetSystemTick()-start,std::memory_order_relaxed);}
 LightLock_Unlock(&all.lock);return ok;
}
void enableCache(size_t limit){auto& all=archives();LightLock_Lock(&all.lock);all.keepLimit=limit;LightLock_Unlock(&all.lock);}
size_t cacheLimitFor(size_t heap){return heap>RacePeakHeap?heap-RacePeakHeap:0;}
void cacheReads(bool on){auto& all=archives();LightLock_Lock(&all.lock);all.caching=on&&all.keepLimit;LightLock_Unlock(&all.lock);}
size_t trimCache(){
 auto& all=archives();LightLock_Lock(&all.lock);
 for(auto it=all.kept.begin();it!=all.kept.end();){
  if(!it->second.used){all.keptBytes-=it->second.size;it=all.kept.erase(it);}
  else{it->second.used=false;++it;}
 }
 const size_t bytes=all.keptBytes;LightLock_Unlock(&all.lock);return bytes;
}
void dropCache(){auto& all=archives();LightLock_Lock(&all.lock);all.kept.clear();all.keptBytes=0;all.caching=false;all.decoded.reset();all.decodedArchive=nullptr;LightLock_Unlock(&all.lock);}
bool read(const char* path,std::vector<uint8_t>& out){
 const long bytes=size(path);if(bytes<0)return false;
 out.resize(size_t(bytes));return readInto(path,out.data(),out.size());
}
#ifndef MKW_RELEASE
namespace {
struct Bench {
 std::FILE* out;void(*log)(const char*);
 void line(const char* format,...)__attribute__((format(printf,2,3))){
  char text[200];va_list args;va_start(args,format);std::vsnprintf(text,sizeof text,format,args);va_end(args);
  if(out){std::fputs(text,out);std::fputc('\n',out);}if(log)log(text);
 }
};
double ms(u64 ticks){return double(ticks)*1000.0/SYSCLOCK_ARM11;}
double mbps(u64 bytes,double milliseconds){return double(bytes)/1048576.0/(milliseconds/1000.0);}
// Each test reads its own 32 MB-aligned region of race.pak, so none re-reads another's bytes.
constexpr u64 region=32ull<<20;
struct ThreadRead {Handle file;u64 offset;std::uint32_t total,chunk;void* buffer;bool ok;u64 ticks;};
void threadRead(void* arg){
 auto& t=*static_cast<ThreadRead*>(arg);const u64 start=svcGetSystemTick();t.ok=true;
 for(std::uint32_t done=0;done<t.total&&t.ok;done+=t.chunk){std::uint32_t got=0;t.ok=R_SUCCEEDED(FSFILE_Read(t.file,&got,t.offset+done,t.buffer,t.chunk))&&got==t.chunk;}
 __atomic_store_n(&t.ticks,svcGetSystemTick()-start,__ATOMIC_RELEASE);
}
// Greedy LZ11 in libctru's decoder format, for the decode-speed test only.
size_t lz11(const unsigned char* in,size_t n,unsigned char* out){
 std::vector<int> head(1<<14,-1),prev(n);
 size_t o=4,flagAt=0;unsigned tokens=0;out[0]=0x11;out[1]=n&255;out[2]=(n>>8)&255;out[3]=(n>>16)&255;
 auto hash=[&](size_t i){return ((std::uint32_t(in[i])<<16|std::uint32_t(in[i+1])<<8|in[i+2])*2654435761u)>>18;};
 for(size_t i=0;i<n;){
  if(tokens%8==0){flagAt=o;out[o++]=0;}
  size_t best=0,dist=0;
  if(i+3<=n){
   const size_t limit=std::min<size_t>(n-i,65808);int k=64;
   for(int c=head[hash(i)];c>=0&&i-size_t(c)<=4096&&k--;c=prev[c]){
    size_t l=0;while(l<limit&&in[c+l]==in[i+l])++l;
    if(l>best){best=l;dist=i-size_t(c);if(l==limit)break;}
   }
  }
  if(best>=3){
   out[flagAt]|=0x80>>(tokens%8);const size_t d=dist-1;
   if(best<=16){out[o++]=((best-1)<<4)|(d>>8);out[o++]=d&255;}
   else if(best<=272){const size_t l=best-0x11;out[o++]=l>>4;out[o++]=((l&15)<<4)|(d>>8);out[o++]=d&255;}
   else{const size_t l=best-0x111;out[o++]=0x10|(l>>12);out[o++]=(l>>4)&255;out[o++]=((l&15)<<4)|(d>>8);out[o++]=d&255;}
  }else{out[o++]=in[i];best=1;}
  ++tokens;
  for(size_t j=0;j<best;++j,++i)if(i+3<=n){const auto h=hash(i);prev[i]=head[h];head[h]=int(i);}
 }
 return o;
}
}
// Mode 2: what makes the game's reads cost ~3.7 ms more each than the mode 1
// model (1.8 ms + bytes at 10.8 MB/s)? Partial sectors, depth in the file,
// destination alignment; then real entries as read now and sector-rounded.
static void alignmentBench(Bench& b,unsigned char* buffer){
 FS_Archive sdmc=0;Handle file=0;
 if(R_FAILED(FSUSER_OpenArchive(&sdmc,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,"")))||R_FAILED(FSUSER_OpenFile(&file,sdmc,fsMakePath(PATH_ASCII,"/3ds/mkw-3ds/race.pak"),FS_OPEN_READ,0))){b.line("FAIL FSUSER open");return;}
 u64 fileSize=0;FSFILE_GetSize(file,&fileSize);
 b.line("race.pak %llu bytes",(unsigned long long)fileSize);
 struct Case {const char* name;std::uint32_t head,tail,dest;bool deep;};
 const Case cases[]{{"aligned",0,0,0,false},{"offset+32",32,0,0,false},{"size+100",0,100,0,false},{"dest+8",0,0,8,false},
  {"game-like (offset+32 size+100 dest+8)",32,100,8,false},{"aligned, deep",0,0,0,true},{"game-like, deep",32,100,8,true}};
 unsigned slot=1;
 for(std::uint32_t size:{8u<<10,96u<<10})for(const Case& c:cases){
  const u64 base=c.deep?fileSize-(160ull<<20)+region*(slot%4):region*slot;++slot;
  const u64 start=svcGetSystemTick();bool ok=true;
  for(unsigned i=0;i<200&&ok;++i){std::uint32_t got=0;const std::uint32_t bytes=size+c.tail;
   ok=R_SUCCEEDED(FSFILE_Read(file,&got,base+u64(i)*(150u<<10)+c.head,buffer+c.dest,bytes))&&got==bytes;}
  const double t=ms(svcGetSystemTick()-start);
  b.line("200 reads of %6lu, %s: %s %.1f ms, %.3f ms/read",(unsigned long)size,c.name,ok?"ok":"FAIL",t,t/200);
 }
 // The same partial-sector cases through stdio (the game's path).
 if(std::FILE* f=std::fopen("race.pak","rb")){
  std::setvbuf(f,nullptr,_IONBF,0);
  for(std::uint32_t head:{0u,32u}){
   const u64 base=region*slot++,start=svcGetSystemTick();bool ok=true;
   for(unsigned i=0;i<200&&ok;++i)ok=std::fseek(f,long(base+u64(i)*(150u<<10)+head),SEEK_SET)==0&&std::fread(buffer+head/4,1,(8u<<10)+head*3,f)==(8u<<10)+head*3;
   const double t=ms(svcGetSystemTick()-start);
   b.line("stdio 200 reads of 8 KB, %s: %s %.1f ms, %.3f ms/read",head?"game-like":"aligned",ok?"ok":"FAIL",t,t/200);
  }
  std::fclose(f);
 }
 // Real entries: 300 of 2-256 KB, read as the game does, then as whole 512-byte
 // sectors into an aligned buffer, then the next 300 sector-rounded first (order check).
 std::vector<Entry> picks;
 {
  auto& all=archives();LightLock_Lock(&all.lock);const Entry* none=nullptr;lookup(all,"",&none);
  for(const Entry& e:all.list[1].entries)if(picks.size()<600&&e.size>=(2u<<10)&&e.size<=(256u<<10)&&(e.offset>>12)%5==0)picks.push_back(e);
  LightLock_Unlock(&all.lock);
 }
 auto run=[&](size_t from,bool rounded){
  u64 bytes=0;const u64 start=svcGetSystemTick();bool ok=true;
  for(size_t i=from;i<std::min(picks.size(),from+300)&&ok;++i){
   const Entry& e=picks[i];std::uint32_t got=0;
   const u64 at=rounded?e.offset&~511u:e.offset;const std::uint32_t n=rounded?std::uint32_t(((e.offset+e.size+511)&~511u)-at):e.size;
   ok=R_SUCCEEDED(FSFILE_Read(file,&got,at,buffer+(rounded?0:8),n))&&got==n;bytes+=e.size;
  }
  const double t=ms(svcGetSystemTick()-start);
  b.line("300 real entries (%llu KB), %s: %s %.1f ms, %.3f ms/read",(unsigned long long)(bytes/1024),rounded?"sector-rounded":"as stored",ok?"ok":"FAIL",t,t/300);
 };
 if(picks.size()>=600){run(0,false);run(0,true);run(300,true);run(300,false);}
 else b.line("only %u real entries picked",unsigned(picks.size()));
 FSFILE_Close(file);FSUSER_CloseArchive(sdmc);
}
void readBench(const char* resultPath,void(*log)(const char*),int mode){
 Bench b{std::fopen(resultPath,mode==2?"a":"w"),log};
 if(mode==2){
  b.line("read bench mode 2");
  if(auto* buffer=static_cast<unsigned char*>(std::aligned_alloc(0x1000,1u<<20))){alignmentBench(b,buffer);std::free(buffer);}
  b.line("read bench done");
  if(b.out)std::fclose(b.out);
  return;
 }
 constexpr std::uint32_t total=8u<<20,big=4u<<20;
 auto* buffer=static_cast<unsigned char*>(std::aligned_alloc(0x1000,big));
 if(!buffer){b.line("FAIL buffer");if(b.out)std::fclose(b.out);return;}
 s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
 unsigned slot=1;
 // 1. The game's path: unbuffered stdio, one fread per chunk.
 if(std::FILE* f=std::fopen("race.pak","rb")){
  std::setvbuf(f,nullptr,_IONBF,0);
  for(std::uint32_t chunk:{4u<<10,16u<<10,64u<<10,256u<<10,1u<<20,4u<<20}){
   const u64 start=svcGetSystemTick();bool ok=std::fseek(f,long(region*slot++),SEEK_SET)==0;
   for(std::uint32_t done=0;done<total&&ok;done+=chunk)ok=std::fread(buffer,1,chunk,f)==chunk;
   const double t=ms(svcGetSystemTick()-start);
   b.line("stdio seq 8 MB, chunk %7lu: %s %.1f ms, %.2f MB/s, %.3f ms/read",(unsigned long)chunk,ok?"ok":"FAIL",t,mbps(total,t),t/(total/chunk));
  }
  std::fclose(f);
 }else b.line("FAIL fopen race.pak");
 // 2. The FS service directly.
 FS_Archive sdmc=0;Handle file=0;
 if(R_FAILED(FSUSER_OpenArchive(&sdmc,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,"")))||R_FAILED(FSUSER_OpenFile(&file,sdmc,fsMakePath(PATH_ASCII,"/3ds/mkw-3ds/race.pak"),FS_OPEN_READ,0))){
  b.line("FAIL FSUSER open");std::free(buffer);if(b.out)std::fclose(b.out);return;
 }
 auto fsRead=[&](u64 offset,void* to,std::uint32_t bytes){std::uint32_t got=0;return R_SUCCEEDED(FSFILE_Read(file,&got,offset,to,bytes))&&got==bytes;};
 for(std::uint32_t chunk:{16u<<10,256u<<10,1u<<20,4u<<20}){
  const u64 base=region*slot++,start=svcGetSystemTick();bool ok=true;
  for(std::uint32_t done=0;done<total&&ok;done+=chunk)ok=fsRead(base+done,buffer,chunk);
  const double t=ms(svcGetSystemTick()-start);
  b.line("fsfile seq 8 MB, chunk %7lu: %s %.1f ms, %.2f MB/s",(unsigned long)chunk,ok?"ok":"FAIL",t,mbps(total,t));
 }
 // 3. The same reads into LINEAR memory.
 if(void* linear=linearAlloc(1u<<20)){
  const u64 base=region*slot++,start=svcGetSystemTick();bool ok=true;
  for(std::uint32_t done=0;done<total&&ok;done+=1u<<20)ok=fsRead(base+done,linear,1u<<20);
  const double t=ms(svcGetSystemTick()-start);
  b.line("fsfile seq 8 MB, chunk 1048576 into LINEAR: %s %.1f ms, %.2f MB/s",ok?"ok":"FAIL",t,mbps(total,t));
  linearFree(linear);
 }
 // 4. Scattered reads, as when loading many small assets: the cost per read.
 for(std::uint32_t chunk:{512u,8u<<10,64u<<10}){
  const u64 base=region*slot++,start=svcGetSystemTick();bool ok=true;
  for(unsigned i=0;i<200&&ok;++i)ok=fsRead(base+u64(i)*(150u<<10),buffer,chunk);
  const double t=ms(svcGetSystemTick()-start);
  b.line("fsfile 200 scattered reads of %5lu (150 KB apart): %s %.1f ms, %.3f ms/read",(unsigned long)chunk,ok?"ok":"FAIL",t,t/200);
 }
 // 5. Back-to-back small reads of adjacent bytes (consecutive pack entries).
 {
  const u64 base=region*slot++,start=svcGetSystemTick();bool ok=true;
  for(unsigned i=0;i<400&&ok;++i)ok=fsRead(base+u64(i)*(8u<<10),buffer,8u<<10);
  const double t=ms(svcGetSystemTick()-start);
  b.line("fsfile 400 adjacent reads of 8192: %s %.1f ms, %.3f ms/read",ok?"ok":"FAIL",t,t/400);
 }
 // 6. Re-read bytes just read: is there a cache below us?
 {
  const u64 base=region*(slot-1),start=svcGetSystemTick();bool ok=true;
  for(std::uint32_t done=0;done<(1u<<20)&&ok;done+=256u<<10)ok=fsRead(base+done,buffer,256u<<10);
  const double t=ms(svcGetSystemTick()-start);
  b.line("fsfile re-read 1 MB just read: %s %.1f ms, %.2f MB/s",ok?"ok":"FAIL",t,mbps(1u<<20,t));
 }
 // 7. Two threads reading at once (8 MB each, 1 MB chunks): do reads overlap?
 if(void* second=std::aligned_alloc(0x1000,1u<<20)){
  ThreadRead one{file,region*slot++,total,1u<<20,buffer,false,0},two{file,region*slot++,total,1u<<20,second,false,0};
  const u64 start=svcGetSystemTick();
  Thread a=threadCreate(threadRead,&one,16<<10,priority,-2,false);
  Thread c=a?threadCreate(threadRead,&two,16<<10,priority,-2,false):nullptr;
  if(a)threadJoin(a,U64_MAX);
  if(c)threadJoin(c,U64_MAX);
  const double t=ms(svcGetSystemTick()-start);
  if(a&&c)b.line("two threads, 8 MB each: %s %.1f ms (%.1f, %.1f), %.2f MB/s combined",one.ok&&two.ok?"ok":"FAIL",t,ms(one.ticks),ms(two.ticks),mbps(2*total,t));
  else b.line("FAIL thread create");
  if(a)threadFree(a);
  if(c)threadFree(c);
  std::free(second);
 }
 // 8. One thread reads while this one computes: is the core free during a read?
 {
  ThreadRead one{file,region*slot++,total,1u<<20,buffer,false,0};
  volatile std::uint32_t spin=0;u64 spins=0;
  const u64 start=svcGetSystemTick();
  if(Thread a=threadCreate(threadRead,&one,16<<10,priority-1,-2,false)){
   while(!__atomic_load_n(&one.ticks,__ATOMIC_ACQUIRE)&&svcGetSystemTick()-start<SYSCLOCK_ARM11*20ull){for(int i=0;i<10000;++i)spin=spin+1;++spins;}
   threadJoin(a,U64_MAX);threadFree(a);
   const double t=ms(svcGetSystemTick()-start);
   const u64 calibrate=svcGetSystemTick();
   for(u64 s=0;s<spins;++s)for(int i=0;i<10000;++i)spin=spin+1;
   b.line("read thread 8 MB beside CPU work on this core: %s, read %.1f ms, CPU work worth %.1f of %.1f ms",one.ok?"ok":"FAIL",ms(one.ticks),ms(svcGetSystemTick()-calibrate),t);
  }else b.line("FAIL thread create");
 }
 FSFILE_Close(file);FSUSER_CloseArchive(sdmc);
 // 9. LZ11 decode speed on real textures, with libctru's decoder.
 {
  auto& all=archives();
  LightLock_Lock(&all.lock);
  const Entry* none=nullptr;lookup(all,"",&none);
  const Archive& race=all.list[1];
  std::vector<std::string> picks;size_t want=0;
  for(const Entry& e:race.entries){
   const char* n=race.names.data()+e.name;const size_t len=std::strlen(n);
   if(want<(2u<<20)&&len>4&&!std::strcmp(n+len-4,".t3x")&&e.size>(16u<<10)&&e.size<(512u<<10)&&(e.offset>>12)%7==0){picks.emplace_back(n);want+=e.size;}
  }
  LightLock_Unlock(&all.lock);
  std::vector<unsigned char> raw,one;
  for(const auto& n:picks)if(read(n.c_str(),one))raw.insert(raw.end(),one.begin(),one.end());
  if(raw.size()>(256u<<10)&&raw.size()<(16u<<20)){
   std::vector<unsigned char> packed(raw.size()+raw.size()/8+64),back(raw.size());
   const size_t n=lz11(raw.data(),raw.size(),packed.data());
   decompressIOVec iov{back.data(),back.size()};
   const u64 start=svcGetSystemTick();
   const bool ok=decompressV(&iov,1,nullptr,packed.data(),n)&&!std::memcmp(back.data(),raw.data(),raw.size());
   const double t=ms(svcGetSystemTick()-start);
   const u64 copyStart=svcGetSystemTick();std::memcpy(back.data(),raw.data(),raw.size());
   b.line("lz11 decode %u KB of textures (ratio %.3f): %s %.1f ms incl. compare, %.1f MB/s out; memcpy %.1f ms",unsigned(raw.size()/1024),double(n)/raw.size(),ok?"ok":"FAIL",t,mbps(raw.size(),t),ms(svcGetSystemTick()-copyStart));
  }else b.line("lz11: no textures picked");
 }
 std::free(buffer);
 b.line("read bench done");
 if(b.out)std::fclose(b.out);
}
#endif
}
