#include <3ds/types.h>
#include <3ds/mii.h>
#include <mkw/mii_appearance.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
static_assert(sizeof(MiiData)==92);
static_assert(offsetof(MiiData,eye_details)==52&&offsetof(MiiData,mole_details)==70);
int main(){
 std::uint32_t seed=17;
 for(unsigned n=0;n<2000;++n){
  mkw::NativeMii raw{};for(auto& b:raw){seed=seed*1664525+1013904223;b=seed>>24;}raw[0]=3;
  MiiData reference{};std::memcpy(&reference,raw.data(),raw.size());
  mkw::MiiAppearance a{};assert(mkw::decodeMiiAppearance(raw,a));
#define EQ(field,native) assert(a.field==reference.native)
  EQ(female,mii_details.sex);EQ(favoriteColor,mii_details.shirt_color);EQ(height,height);EQ(weight,width);
  EQ(faceType,face_style.shape);EQ(skinColor,face_style.skinColor);EQ(wrinkles,face_details.wrinkles);EQ(makeup,face_details.makeup);
  EQ(hairType,hair_style);EQ(hairColor,hair_details.color);EQ(hairFlip,hair_details.flip);
  EQ(face.eyeType,eye_details.style);EQ(eyeColor,eye_details.color);EQ(face.eyeScale,eye_details.scale);EQ(eyeAspect,eye_details.yscale);
  EQ(face.eyeRotation,eye_details.rotation);EQ(face.eyeX,eye_details.xspacing);EQ(face.eyeY,eye_details.yposition);
  EQ(face.browType,eyebrow_details.style);EQ(browColor,eyebrow_details.color);EQ(face.browScale,eyebrow_details.scale);EQ(browAspect,eyebrow_details.yscale);
  EQ(face.browRotation,eyebrow_details.rotation);EQ(face.browX,eyebrow_details.xspacing);EQ(face.browY,eyebrow_details.yposition);
  EQ(noseType,nose_details.style);EQ(noseScale,nose_details.scale);EQ(noseY,nose_details.yposition);
  EQ(mouthType,mouth_details.style);EQ(mouthColor,mouth_details.color);EQ(face.mouthScale,mouth_details.scale);EQ(mouthAspect,mouth_details.yscale);
  EQ(face.mouthY,mustache_details.mouth_yposition);EQ(mustacheType,mustache_details.mustache_style);
  EQ(beardType,beard_details.style);EQ(beardColor,beard_details.color);EQ(face.mustacheScale,beard_details.scale);EQ(face.mustacheY,beard_details.ypos);
  EQ(glassesType,glasses_details.style);EQ(glassesColor,glasses_details.color);EQ(glassesScale,glasses_details.scale);EQ(glassesY,glasses_details.ypos);
  EQ(moleEnabled,mole_details.enable);EQ(face.moleScale,mole_details.scale);EQ(face.moleX,mole_details.xpos);EQ(face.moleY,mole_details.ypos);
#undef EQ
 }
 mkw::NativeMii bad{};mkw::MiiAppearance a{};a.hairType=77;assert(!mkw::decodeMiiAppearance(bad,a)&&a.hairType==77);
 puts("PASS 2000 native appearances match libctru fields; newer values preserved; failed decode leaves output unchanged");
}
