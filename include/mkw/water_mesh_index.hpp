#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace mkw {
// Exact GPU input sharing: no quantization of position, UV, displacement or
// color. In particular, adjacent triangles with different mip-derived offsets
// remain separate. Empty indices means the input was too large for u16 and
// has been left untouched for an unindexed submission.
template<class Vertex>
std::vector<std::uint16_t> indexWaterMesh(std::vector<Vertex>& vertices) {
 static_assert(std::is_trivially_copyable_v<Vertex> && sizeof(Vertex)%4==0);
 if(vertices.size()>65536) return {};
 using Key=std::array<std::uint32_t,sizeof(Vertex)/4>;
 struct Hash {
  std::size_t operator()(const Key& key) const {
   std::uint32_t h=2166136261u;
   for(auto word:key) h=(h^word)*16777619u;
   return h;
  }
 };
 std::unordered_map<Key,std::uint16_t,Hash> seen;
 seen.reserve(vertices.size()/2);
 std::vector<std::uint16_t> indices;indices.reserve(vertices.size());
 std::size_t unique=0;
 for(std::size_t i=0;i<vertices.size();++i) {
  Key key;std::memcpy(key.data(),&vertices[i],sizeof(Vertex));
  const auto [it,added]=seen.emplace(key,static_cast<std::uint16_t>(unique));
  if(added) vertices[unique++]=vertices[i];
  indices.push_back(it->second);
 }
 vertices.resize(unique);
 return indices;
}
}
