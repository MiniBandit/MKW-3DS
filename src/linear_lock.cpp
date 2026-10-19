#include <3ds.h>
#include <cstddef>

// libctru's LINEAR heap allocator is not thread-safe. The menu asset loader
// creates textures and model buffers on a background thread, so every LINEAR
// allocation in the program (including Citro3D's) is routed through one lock
// with -Wl,--wrap=linear*. A zero-initialized RecursiveLock is a valid,
// unlocked lock, so this is usable before static constructors run.
extern "C" {
void* __real_linearAlloc(size_t size);
void* __real_linearMemAlign(size_t size,size_t alignment);
void* __real_linearRealloc(void* mem,size_t size);
size_t __real_linearGetSize(void* mem);
void __real_linearFree(void* mem);
u32 __real_linearSpaceFree(void);
}
namespace {
RecursiveLock linearLock;
struct Guard {
 Guard(){RecursiveLock_Lock(&linearLock);}
 ~Guard(){RecursiveLock_Unlock(&linearLock);}
};
}
extern "C" {
void* __wrap_linearAlloc(size_t size){Guard g;return __real_linearAlloc(size);}
void* __wrap_linearMemAlign(size_t size,size_t alignment){Guard g;return __real_linearMemAlign(size,alignment);}
void* __wrap_linearRealloc(void* mem,size_t size){Guard g;return __real_linearRealloc(mem,size);}
size_t __wrap_linearGetSize(void* mem){Guard g;return __real_linearGetSize(mem);}
void __wrap_linearFree(void* mem){Guard g;__real_linearFree(mem);}
u32 __wrap_linearSpaceFree(void){Guard g;return __real_linearSpaceFree();}
}

// QA: counts file opens and the time spent opening (fopen is wrapped at link).
#include <cstdio>
#include <atomic>
#include <mkw/overlay.hpp>
namespace mkw {std::atomic<unsigned> fileOpens{0};std::atomic<unsigned long long> fileOpenTicks{0};}
extern "C" {
FILE* __real_fopen(const char* path,const char* mode);
FILE* __wrap_fopen(const char* path,const char* mode){
 const u64 start=svcGetSystemTick();
 // An update's overlay file (romfs:/overlay/...) replaces the same relative
 // path when the game opens it for reading (include/mkw/overlay.hpp).
 FILE* file=nullptr;char overlayPath[192];
 if(mode&&mode[0]=='r'&&mode[1]!='+'&&mkw::overlay::resolve(path,overlayPath,sizeof overlayPath))file=__real_fopen(overlayPath,mode);
 if(!file)file=__real_fopen(path,mode);
 mkw::fileOpens.fetch_add(1,std::memory_order_relaxed);mkw::fileOpenTicks.fetch_add(svcGetSystemTick()-start,std::memory_order_relaxed);
 return file;
}
}
