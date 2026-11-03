#include <mkw/water_mesh_index.hpp>
#include <cassert>
#include <cstdio>
using V=std::array<uint32_t,11>;
int main(){
 std::vector<V> mesh;assert(mkw::indexWaterMesh(mesh).empty());
 for(unsigned n:{1u,65536u,65537u}) {
  mesh.resize(n);for(unsigned i=0;i<n;++i){mesh[i]={};mesh[i][0]=i;}
  const auto original=mesh;const auto indices=mkw::indexWaterMesh(mesh);
  if(n>65536){assert(indices.empty()&&mesh==original);continue;}
  assert(indices.size()==n&&mesh.size()==n);
  for(unsigned i=0;i<n;++i)assert(mesh[indices[i]]==original[i]);
 }
 // All attributes participate, including float sign bits and displacement.
 mesh.assign(9,V{});mesh[1][0]=0x80000000;mesh[2][9]=1;mesh[3][10]=255;
 const auto original=mesh;const auto indices=mkw::indexWaterMesh(mesh);
 assert(mesh.size()==4);
 for(unsigned i=0;i<indices.size();++i)assert(mesh[indices[i]]==original[i]);
 puts("water indexing: exact attributes, duplicates, u16 boundary and oversized fallback PASS");
}
