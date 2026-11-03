#include <mkw/asset_pack.hpp>
#include <3ds.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace mkw::pack {
namespace {
struct Entry {std::uint32_t name,offset,size;};
struct Archive {
 const char* path;std::FILE* file=nullptr;
 std::vector<Entry> entries;std::vector<char> names;
 const Entry* find(const char* key)const{
  auto it=std::lower_bound(entries.begin(),entries.end(),key,[&](const Entry& e,const char* k){return std::strcmp(names.data()+e.name,k)<0;});
  return it!=entries.end()&&!std::strcmp(names.data()+it->name,key)?&*it:nullptr;
 }
};
// Menu and race assets use the original two archives.
struct Archives {
 LightLock lock;bool tried=false;
 std::array<Archive,2> list{{{"menu.pak"},{"race.pak"}}};
 Archives(){LightLock_Init(&lock);}
};
Archives& archives(){static Archives value;return value;}
std::uint32_t u32(const unsigned char* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
bool load(Archive& a){
 std::FILE* f=std::fopen(a.path,"rb");if(!f)return false;
 std::setvbuf(f,nullptr,_IONBF,0);
 unsigned char header[12];
 if(std::fread(header,1,12,f)!=12||std::memcmp(header,"MPK1",4)){std::fclose(f);return false;}
 const std::uint32_t count=u32(header+4),namesBytes=u32(header+8);
 if(!count||count>200000||!namesBytes||namesBytes>(16u<<20)){std::fclose(f);return false;}
 std::vector<unsigned char> table(size_t(count)*12);
 a.names.resize(namesBytes);a.entries.resize(count);
 if(std::fread(table.data(),1,table.size(),f)!=table.size()||std::fread(a.names.data(),1,namesBytes,f)!=namesBytes||a.names.back()!=0){std::fclose(f);a.entries.clear();return false;}
 for(std::uint32_t i=0;i<count;++i){auto& e=a.entries[i];e.name=u32(&table[i*12]);e.offset=u32(&table[i*12+4]);e.size=u32(&table[i*12+8]);
  if(e.name>=namesBytes){std::fclose(f);a.entries.clear();return false;}}
 a.file=f;return true;
}
// With the lock held: the archive holding path, or null.
Archive* lookup(Archives& all,const char* path,const Entry** entry){
 if(!all.tried){all.tried=true;for(auto& a:all.list)load(a);}
 for(auto& a:all.list)if(a.file)if(const Entry* e=a.find(path)){*entry=e;return &a;}
 return nullptr;
}
}
void open(){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;lookup(all,"",&e);LightLock_Unlock(&all.lock);}
void close(){
 auto& all=archives();LightLock_Lock(&all.lock);
 for(auto& a:all.list){if(a.file)std::fclose(a.file);a.file=nullptr;a.entries.clear();a.names.clear();}
 all.tried=false;LightLock_Unlock(&all.lock);
}
bool contains(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const bool found=lookup(all,path,&e);LightLock_Unlock(&all.lock);return found;}
long size(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const long result=lookup(all,path,&e)?long(e->size):-1;LightLock_Unlock(&all.lock);return result;}
bool readInto(const char* path,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 const bool ok=a&&e->size==bytes&&std::fseek(a->file,long(e->offset),SEEK_SET)==0&&std::fread(destination,1,bytes,a->file)==bytes;
 LightLock_Unlock(&all.lock);return ok;
}
bool readRange(const char* path,size_t offset,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 const bool ok=a&&offset<=e->size&&bytes<=e->size-offset&&std::fseek(a->file,long(e->offset+offset),SEEK_SET)==0&&std::fread(destination,1,bytes,a->file)==bytes;
 LightLock_Unlock(&all.lock);return ok;
}
bool read(const char* path,std::vector<uint8_t>& out){
 const long bytes=size(path);if(bytes<0)return false;
 out.resize(size_t(bytes));return readInto(path,out.data(),out.size());
}
}
