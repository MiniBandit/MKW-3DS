// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#pragma once
// A compressed MPK2 entry (tools/mpk2.cpp; read by src/asset_pack.cpp): the
// asset in 64 KiB pieces, each LZ4-compressed on its own (or kept as is when
// that does not shrink it), so the game decodes one piece at a time through a
// fixed 64 KiB scratch buffer, and a range read decodes only its pieces.
//
// Layout (little endian): u32 pieces, then u32 end[pieces] (each piece's end,
// counted from the first piece's start; the top bit marks a piece kept as is),
// then the pieces. Every piece but the last holds Piece bytes of the asset.
#include <mkw/lz4_block.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mkw::mpk2 {
inline constexpr std::uint32_t Piece=64*1024,Raw=0x80000000u;
inline std::uint32_t pieces(std::uint32_t size){return (size+Piece-1)/Piece;}
inline std::uint32_t le32(const std::uint8_t* p){return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;}
// Piece k: where it starts after the table, its stored bytes, kept as is?
struct Span {std::uint32_t start,bytes;bool raw;};
inline Span span(const std::uint8_t* table,std::uint32_t k){
    const std::uint32_t begin=k?le32(table+4*(k-1))&~Raw:0,end=le32(table+4*k);
    return {begin,(end&~Raw)-begin,(end&Raw)!=0};
}
// Decodes piece k of an asset of `size` bytes into out (exactly its length).
// stored points at the piece's bytes; scratch is not needed (LZ4 reads stored).
inline bool decodePiece(const Span& s,const std::uint8_t* stored,std::uint32_t k,std::uint32_t size,std::uint8_t* out){
    const std::uint32_t length=k+1<pieces(size)?Piece:size-k*Piece;
    if(s.raw){if(s.bytes!=length)return false;std::memcpy(out,stored,length);return true;}
    return lz4Decode(stored,s.bytes,out,length);
}
// Host: the stored form of an asset. Empty when compressing saves too little.
inline std::vector<std::uint8_t> encode(const std::uint8_t* data,std::uint32_t size){
    const std::uint32_t n=pieces(size);
    std::vector<std::uint8_t> out(4+4*std::size_t(n));
    auto put=[&](std::size_t at,std::uint32_t v){for(int i=0;i<4;++i)out[at+i]=std::uint8_t(v>>(8*i));};
    put(0,n);
    std::uint32_t end=0;
    for(std::uint32_t k=0;k<n;++k){
        const std::uint32_t length=k+1<n?Piece:size-k*Piece;
        const auto packed=lz4Encode(data+std::size_t(k)*Piece,length);
        const bool raw=packed.size()>=length;
        if(raw)out.insert(out.end(),data+std::size_t(k)*Piece,data+std::size_t(k)*Piece+length);
        else out.insert(out.end(),packed.begin(),packed.end());
        end+=raw?length:std::uint32_t(packed.size());
        put(4+4*std::size_t(k),end|(raw?Raw:0));
    }
    return out;
}
}
