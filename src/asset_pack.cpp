#include <mkw/asset_pack.hpp>
#include <mkw/overlay.hpp>
#include <mkw/mpk2_entry.hpp>
#include <3ds.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace mkw::pack {
namespace {
// stored: LZ4 bytes in the archive (MPK2, tools/mpk2.cpp); 0 = as is.
struct Entry {std::uint32_t name,offset,size,stored=0;};
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
 // Compressed entries (MPK2) decode one 64 KiB piece at a time: their stored
 // pieces are read here, and range reads (VRAM textures) keep the last
 // decoded piece. 128 KiB in all, allocated on the first compressed read.
 std::uint8_t* stored=nullptr;std::uint8_t* piece=nullptr;
 const Entry* pieceEntry=nullptr;std::uint32_t pieceIndex=0;
 std::vector<std::uint8_t> table;const Entry* tableEntry=nullptr;
 std::array<Archive,2> list{{{"menu.pak"},{"race.pak"}}};
 Archives(){LightLock_Init(&lock);}
};
Archives& archives(){static Archives value;return value;}
std::uint32_t u32(const unsigned char* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
bool load(Archive& a){
 std::FILE* f=std::fopen(a.path,"rb");if(!f)return false;
 std::setvbuf(f,nullptr,_IONBF,0);
 unsigned char header[12];
 if(std::fread(header,1,12,f)!=12||(std::memcmp(header,"MPK1",4)&&std::memcmp(header,"MPK2",4))){std::fclose(f);return false;}
 const bool compressed=header[3]=='2';const std::size_t row=compressed?16:12;
 const std::uint32_t count=u32(header+4),namesBytes=u32(header+8);
 if(!count||count>200000||!namesBytes||namesBytes>(16u<<20)){std::fclose(f);return false;}
 std::vector<unsigned char> table(size_t(count)*row);
 a.names.resize(namesBytes);a.entries.resize(count);
 if(std::fread(table.data(),1,table.size(),f)!=table.size()||std::fread(a.names.data(),1,namesBytes,f)!=namesBytes||a.names.back()!=0){std::fclose(f);a.entries.clear();return false;}
 for(std::uint32_t i=0;i<count;++i){auto& e=a.entries[i];const unsigned char* r=&table[i*row];e.name=u32(r);e.offset=u32(r+4);e.size=u32(r+8);e.stored=compressed?u32(r+12):0;
  if(e.name>=namesBytes||(e.stored&&e.stored>=e.size)){std::fclose(f);a.entries.clear();return false;}}
 a.file=f;return true;
}
bool readAt(Archive& a,std::uint32_t offset,void* out,std::size_t bytes){
 return std::fseek(a.file,long(offset),SEEK_SET)==0&&std::fread(out,1,bytes,a.file)==bytes;
}
// With the lock held: a compressed entry's piece table, and the scratch.
bool prepare(Archives& all,Archive& a,const Entry& e){
 if(!all.stored){all.stored=static_cast<std::uint8_t*>(std::malloc(mpk2::Piece));all.piece=static_cast<std::uint8_t*>(std::malloc(mpk2::Piece));}
 if(!all.stored||!all.piece)return false;
 if(all.tableEntry==&e)return true;
 all.tableEntry=nullptr;const std::uint32_t n=mpk2::pieces(e.size);
 all.table.resize(4+4*std::size_t(n));
 if(!readAt(a,e.offset,all.table.data(),all.table.size())||mpk2::le32(all.table.data())!=n)return false;
 all.tableEntry=&e;return true;
}
// With the lock held: piece k of a compressed entry into out (its length).
bool readPiece(Archives& all,Archive& a,const Entry& e,std::uint32_t k,std::uint8_t* out){
 const std::uint32_t n=mpk2::pieces(e.size);
 const auto s=mpk2::span(all.table.data()+4,k);
 if(s.bytes>mpk2::Piece||std::size_t(s.start)+s.bytes+all.table.size()>e.stored)return false;
 const std::uint32_t at=e.offset+std::uint32_t(all.table.size())+s.start;
 // A piece kept as is goes straight to its destination.
 if(s.raw)return s.bytes==(k+1<n?mpk2::Piece:e.size-k*mpk2::Piece)&&readAt(a,at,out,s.bytes);
 return readAt(a,at,all.stored,s.bytes)&&mpk2::decodePiece(s,all.stored,k,e.size,out);
}
// With the lock held: the entry's bytes into destination (exactly e.size).
bool readEntry(Archives& all,Archive& a,const Entry& e,void* destination){
 auto* out=static_cast<std::uint8_t*>(destination);
 if(!e.stored)return readAt(a,e.offset,out,e.size);
 if(!prepare(all,a,e))return false;
 for(std::uint32_t k=0;k<mpk2::pieces(e.size);++k)if(!readPiece(all,a,e,k,out+std::size_t(k)*mpk2::Piece))return false;
 return true;
}
void dropCache(Archives& all){
 std::free(all.stored);std::free(all.piece);all.stored=all.piece=nullptr;
 all.pieceEntry=nullptr;all.tableEntry=nullptr;all.table.clear();all.table.shrink_to_fit();
}
// With the lock held: the archive holding path, or null.
Archive* lookup(Archives& all,const char* path,const Entry** entry){
 if(!all.tried){all.tried=true;for(auto& a:all.list)load(a);}
 // A file in the update overlay wins over the packs: report it absent so the
 // caller opens the loose path, which the fopen wrapper serves from the overlay.
 if(overlay::has(path))return nullptr;
 for(auto& a:all.list)if(a.file)if(const Entry* e=a.find(path)){*entry=e;return &a;}
 return nullptr;
}
}
void open(){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;lookup(all,"",&e);LightLock_Unlock(&all.lock);}
void close(){
 auto& all=archives();LightLock_Lock(&all.lock);
 dropCache(all);
 for(auto& a:all.list){if(a.file)std::fclose(a.file);a.file=nullptr;a.entries.clear();a.names.clear();}
 all.tried=false;LightLock_Unlock(&all.lock);
}
bool contains(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const bool found=lookup(all,path,&e);LightLock_Unlock(&all.lock);return found;}
long size(const char* path){auto& all=archives();LightLock_Lock(&all.lock);const Entry* e=nullptr;const long result=lookup(all,path,&e)?long(e->size):-1;LightLock_Unlock(&all.lock);return result;}
bool readInto(const char* path,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 const bool ok=a&&e->size==bytes&&readEntry(all,*a,*e,destination);
 LightLock_Unlock(&all.lock);return ok;
}
bool readRange(const char* path,size_t offset,void* destination,size_t bytes){
 auto& all=archives();LightLock_Lock(&all.lock);
 const Entry* e=nullptr;Archive* a=lookup(all,path,&e);
 bool ok=a&&offset<=e->size&&bytes<=e->size-offset;
 if(ok&&!e->stored)ok=readAt(*a,e->offset+std::uint32_t(offset),destination,bytes);
 else if(ok&&bytes){
  // Only the pieces the range covers, each decoded once while it is read.
  ok=prepare(all,*a,*e);
  auto* out=static_cast<std::uint8_t*>(destination);
  while(ok&&bytes){
   const std::uint32_t k=std::uint32_t(offset/mpk2::Piece),within=std::uint32_t(offset%mpk2::Piece);
   if(all.pieceEntry!=e||all.pieceIndex!=k){
    all.pieceEntry=nullptr;
    ok=readPiece(all,*a,*e,k,all.piece);
    if(ok){all.pieceEntry=e;all.pieceIndex=k;}
   }
   const std::uint32_t length=k+1<mpk2::pieces(e->size)?mpk2::Piece:e->size-k*mpk2::Piece;
   const std::size_t take=std::min<std::size_t>(bytes,length-within);
   if(ok){std::memcpy(out,all.piece+within,take);out+=take;offset+=take;bytes-=take;}
  }
 }
 LightLock_Unlock(&all.lock);return ok;
}
bool read(const char* path,std::vector<uint8_t>& out){
 const long bytes=size(path);if(bytes<0)return false;
 out.resize(size_t(bytes));return readInto(path,out.data(),out.size());
}
}
