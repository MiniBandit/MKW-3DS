// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
// MPK1 -> MPK2: the same assets and names, each stored in LZ4-compressed
// 64 KiB pieces (include/mkw/mpk2_entry.hpp) when that saves at least a
// tenth, else as is. Data starts on 512-byte SD sectors. Every compressed
// entry is decoded again here, piece by piece as the game does, and compared.
//
// MPK2 (little endian): "MPK2", u32 count, u32 namesBytes, count entries of
// {u32 nameOffset, u32 offset, u32 size, u32 stored} sorted by name (stored:
// compressed bytes, 0 = not compressed), NUL-terminated names, then the data.
//
//   mpk2 input.pak output.pak
#include <mkw/mpk2_entry.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
uint32_t u32(const unsigned char* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put32(std::vector<unsigned char>& out,uint32_t v){for(int i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));}
int fail(const char* message){std::fprintf(stderr,"mpk2: %s\n",message);return 1;}
}

int main(int argc,char** argv){
    if(argc!=3)return fail("usage: mpk2 input.pak output.pak");
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<unsigned char> pak((std::istreambuf_iterator<char>(in)),{});
    if(pak.size()<12||std::memcmp(pak.data(),"MPK1",4))return fail("input is not an MPK1 archive");
    const uint32_t count=u32(&pak[4]),namesBytes=u32(&pak[8]);
    const std::size_t table=12,names=table+std::size_t(count)*12;
    if(names+namesBytes>pak.size())return fail("damaged MPK1 index");
    struct Entry{uint32_t name,offset,size,stored=0;std::vector<uint8_t> packed;};
    std::vector<Entry> entries(count);
    std::size_t raw=0,stored=0;unsigned compressed=0;
    for(uint32_t i=0;i<count;++i){
        auto& e=entries[i];const unsigned char* r=&pak[table+i*12];
        e.name=u32(r);e.offset=u32(r+4);e.size=u32(r+8);
        if(e.name>=namesBytes||std::size_t(e.offset)+e.size>pak.size())return fail("entry outside the archive");
        raw+=e.size;
        if(e.size>=4096){
            auto packed=mkw::mpk2::encode(&pak[e.offset],e.size);
            if(packed.size()*10<=std::size_t(e.size)*9){
                const std::uint32_t n=mkw::mpk2::pieces(e.size);
                const std::uint8_t* table=packed.data()+4;const std::uint8_t* body=table+4*std::size_t(n);
                std::vector<uint8_t> piece(mkw::mpk2::Piece);
                for(std::uint32_t k=0;k<n;++k){
                    const auto sp=mkw::mpk2::span(table,k);
                    const std::uint32_t length=k+1<n?mkw::mpk2::Piece:e.size-k*mkw::mpk2::Piece;
                    if(!mkw::mpk2::decodePiece(sp,body+sp.start,k,e.size,piece.data())||std::memcmp(piece.data(),&pak[e.offset+std::size_t(k)*mkw::mpk2::Piece],length))
                        return fail("an entry did not decode back to its original bytes");
                }
                e.stored=uint32_t(packed.size());e.packed=std::move(packed);++compressed;
            }
        }
        stored+=e.stored?e.stored:e.size;
    }
    std::vector<unsigned char> out;
    out.insert(out.end(),{'M','P','K','2'});put32(out,count);put32(out,namesBytes);
    const std::size_t header=12+std::size_t(count)*16+namesBytes;
    // Data in the archive's original order, each entry on a 512-byte sector.
    std::size_t at=(header+511)&~std::size_t(511);
    std::vector<uint32_t> offsets(count);
    for(uint32_t i=0;i<count;++i){
        offsets[i]=uint32_t(at);
        const std::size_t bytes=entries[i].stored?entries[i].stored:entries[i].size;
        at=(at+bytes+511)&~std::size_t(511);
        if(at>0xffffffffull)return fail("archive over 4 GiB");
    }
    for(uint32_t i=0;i<count;++i){put32(out,entries[i].name);put32(out,offsets[i]);put32(out,entries[i].size);put32(out,entries[i].stored);}
    out.insert(out.end(),pak.begin()+names,pak.begin()+names+namesBytes);
    std::FILE* f=std::fopen(argv[2],"wb");if(!f)return fail("cannot write the output");
    bool ok=std::fwrite(out.data(),1,out.size(),f)==out.size();
    std::size_t written=out.size();
    const std::vector<unsigned char> zero(512,0);
    for(uint32_t i=0;ok&&i<count;++i){
        if(offsets[i]>written)ok=std::fwrite(zero.data(),1,offsets[i]-written,f)==offsets[i]-written;
        written=offsets[i];
        const auto& e=entries[i];
        const unsigned char* data=e.stored?e.packed.data():&pak[e.offset];
        const std::size_t bytes=e.stored?e.stored:e.size;
        ok=ok&&std::fwrite(data,1,bytes,f)==bytes;written+=bytes;
    }
    if(std::fclose(f))ok=false;
    if(!ok)return fail("writing the output failed");
    std::printf("{\"entries\":%u,\"compressed\":%u,\"raw_bytes\":%zu,\"stored_bytes\":%zu,\"archive_bytes\":%zu}\n",count,compressed,raw,stored,written);
    return 0;
}
