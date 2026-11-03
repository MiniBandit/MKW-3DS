#pragma once
#include <mkw/license_identity.hpp>
#include <mkw/mii_face_layout.hpp>
namespace mkw {
// Canonical values decoded from libctru MiiData, without clamping or altering
// the saved 92-byte native appearance. Newer shapes remain distinguishable.
struct MiiAppearance {
 bool female=false,hairFlip=false,moleEnabled=false;
 unsigned favoriteColor=0,height=0,weight=0;
 unsigned faceType=0,skinColor=0,wrinkles=0,makeup=0,hairType=0,hairColor=0;
 unsigned eyeColor=0,eyeAspect=0,browColor=0,browAspect=0;
 unsigned noseType=0,noseScale=0,noseY=0;
 unsigned mouthType=0,mouthColor=0,mouthAspect=0;
 unsigned mustacheType=0,beardType=0,beardColor=0;
 unsigned glassesType=0,glassesColor=0,glassesScale=0,glassesY=0;
 MiiFaceFeatures face{};
};
inline bool decodeMiiAppearance(const NativeMii& data,MiiAppearance& output){
 if(data[0]!=3)return false;
 auto bits=[&](unsigned offset,unsigned shift,unsigned count){
  std::uint32_t value=0;
  for(unsigned i=0;i<(shift+count+7)/8;++i)value|=std::uint32_t(data[offset+i])<<(8*i);
  return unsigned((value>>shift)&((std::uint32_t(1)<<count)-1));
 };
 MiiAppearance m;
 m.female=bits(24,0,1);m.favoriteColor=bits(24,10,4);m.height=data[46];m.weight=data[47];
 m.faceType=bits(48,1,4);m.skinColor=bits(48,5,3);m.wrinkles=bits(49,0,4);m.makeup=bits(49,4,4);
 m.hairType=data[50];m.hairColor=bits(51,0,3);m.hairFlip=bits(51,3,1);
 m.face.eyeType=bits(52,0,6);m.eyeColor=bits(52,6,3);m.face.eyeScale=bits(52,9,4);m.eyeAspect=bits(52,13,3);
 m.face.eyeRotation=bits(52,16,5);m.face.eyeX=bits(52,21,4);m.face.eyeY=bits(52,25,5);
 m.face.browType=bits(56,0,5);m.browColor=bits(56,5,3);m.face.browScale=bits(56,8,4);m.browAspect=bits(56,12,3);
 m.face.browRotation=bits(56,16,4);m.face.browX=bits(56,20,4);m.face.browY=bits(56,24,5);
 m.noseType=bits(60,0,5);m.noseScale=bits(60,5,4);m.noseY=bits(60,9,5);
 m.mouthType=bits(62,0,6);m.mouthColor=bits(62,6,3);m.face.mouthScale=bits(62,9,4);m.mouthAspect=bits(62,13,3);
 m.face.mouthY=bits(64,0,5);m.mustacheType=bits(64,5,3);
 m.beardType=bits(66,0,3);m.beardColor=bits(66,3,3);m.face.mustacheScale=bits(66,6,4);m.face.mustacheY=bits(66,10,5);
 m.glassesType=bits(68,0,4);m.glassesColor=bits(68,4,3);m.glassesScale=bits(68,7,4);m.glassesY=bits(68,11,5);
 m.moleEnabled=bits(70,0,1);m.face.moleScale=bits(70,1,4);m.face.moleX=bits(70,5,5);m.face.moleY=bits(70,10,5);
 output=m;return true;
}
}
