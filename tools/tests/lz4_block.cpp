// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#include <mkw/mpk2_entry.hpp>
#include <cassert>
#include <cstdio>
#include <random>
using namespace mkw;
static void roundTrip(const std::vector<uint8_t>& data){
    auto packed=lz4Encode(data.data(),data.size());
    std::vector<uint8_t> out(data.size());
    assert(lz4Decode(packed.data(),packed.size(),out.data(),out.size())&&out==data);
    if(!packed.empty()&&!data.empty()){std::vector<uint8_t> o(data.size());assert(!lz4Decode(packed.data(),packed.size()-1,o.data(),o.size()));}
    std::vector<uint8_t> bigger(data.size()+1);assert(!lz4Decode(packed.data(),packed.size(),bigger.data(),bigger.size()));
    // MPK2 pieces: decode each 64 KiB piece on its own, as the game does.
    if(data.empty())return;
    auto entry=mpk2::encode(data.data(),uint32_t(data.size()));
    const uint32_t n=mpk2::pieces(uint32_t(data.size()));assert(mpk2::le32(entry.data())==n);
    const uint8_t* table=entry.data()+4;const uint8_t* body=table+4*n;
    std::vector<uint8_t> piece(mpk2::Piece);
    for(uint32_t k=0;k<n;++k){
        const auto s=mpk2::span(table,k);const uint32_t length=k+1<n?mpk2::Piece:uint32_t(data.size())-k*mpk2::Piece;
        assert(s.bytes<=mpk2::Piece&&body+s.start+s.bytes<=entry.data()+entry.size());
        assert(mpk2::decodePiece(s,body+s.start,k,uint32_t(data.size()),piece.data()));
        assert(std::equal(piece.begin(),piece.begin()+length,data.begin()+std::ptrdiff_t(k)*mpk2::Piece));
    }
}
int main(){
    std::mt19937 rng(7);
    for(std::size_t n:{0u,1u,12u,13u,64u,4096u,65536u,65537u,200000u,300001u}){
        std::vector<uint8_t> noise(n),text(n),zeros(n,0),pattern(n);
        for(auto& b:noise)b=uint8_t(rng());
        for(std::size_t i=0;i<n;++i){text[i]="mario kart wii 3ds "[i%19];pattern[i]=uint8_t((i/7)%13+(rng()%50==0));}
        roundTrip(noise);roundTrip(text);roundTrip(zeros);roundTrip(pattern);
    }
    // Zeros shrink a lot; noise is kept as is, piece by piece.
    std::vector<uint8_t> z(300000,0);assert(mpk2::encode(z.data(),300000).size()<5000);
    std::vector<uint8_t> r(100000);for(auto& b:r)b=uint8_t(rng());auto e=mpk2::encode(r.data(),100000);assert(mpk2::span(e.data()+4,0).raw&&e.size()==4+8+100000);
    const uint8_t bad[]={0x10,'a',0x05,0x00};uint8_t o[8];assert(!lz4Decode(bad,sizeof bad,o,5));
    std::puts("lz4_block PASS round trips, MPK2 pieces, malformed input");
}
