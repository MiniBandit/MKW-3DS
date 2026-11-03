#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace mkw {
struct MiiPartVertex {std::array<std::int16_t,3> position,normal;std::array<std::int16_t,2> uv;};
struct MiiPartShape {unsigned type=0;std::array<float,9> anchors{};std::vector<MiiPartVertex> vertices;std::vector<std::uint16_t> indices;};
struct MiiPartTexture {unsigned width=0,height=0,wrapS=0,wrapT=0;std::vector<std::uint8_t> rgba;};
// Only the small directory stays resident. Callers retain the selected parts,
// not every Mii texture/model in the original resource library.
class MiiPartBank {
 struct Entry {std::uint32_t group,index,kind,offset,size;};
 std::FILE* file_=nullptr;std::vector<Entry> entries_;
 bool record(unsigned group,unsigned index,unsigned kind,std::vector<std::uint8_t>&) const;
public:
 MiiPartBank()=default;~MiiPartBank(){if(file_)std::fclose(file_);}
 MiiPartBank(const MiiPartBank&)=delete;MiiPartBank& operator=(const MiiPartBank&)=delete;
 bool open(const char* path);
 bool shape(unsigned group,unsigned index,MiiPartShape&) const;
 bool texture(unsigned group,unsigned index,MiiPartTexture&) const;
};
}
