#include <mkw/mii_head.hpp>
#include <mkw/mii_shape_instance.hpp>
#include <mkw/mii_face_mask.hpp>
namespace mkw {
bool nativeMiiHeadSpec(const NativeMii& native,RflHeadSpec& output){
 MiiAppearance a;if(!decodeMiiAppearance(native,a))return false;
 // The legacy-to-native bridge supplies aspect=3 for all three features.
 // RflHeadSpec uses zero to mean this field is absent from the Wii format.
 if(a.eyeAspect!=3||a.browAspect!=3||a.mouthAspect!=3||a.wrinkles||a.makeup)return false;
 if(a.faceType>=8||a.hairType>=72||a.noseType>=12||a.beardType>=4||a.glassesType>=10||
    a.face.eyeType>=50||a.face.browType>=24||a.mouthType>=25||a.mustacheType>=4||
    a.skinColor>=6||a.hairColor>=8||a.beardColor>=8||a.glassesColor>=6||a.favoriteColor>=12||
    a.eyeColor>=6||a.browColor>=8||a.mouthColor>=3)return false;
 a.eyeAspect=a.browAspect=a.mouthAspect=0;
 output={a,0};return true;
}

bool buildMiiHead(const MiiPartBank& bank,const RflHeadSpec& spec,unsigned resolution,MiiHead& out){
 const auto& a=spec.appearance;
 if(a.faceType>=8||a.hairType>=72||a.noseType>=12||a.beardType>=4||a.glassesType>=10||spec.faceFeature>=12||
    a.face.eyeType>=50||a.face.browType>=24||a.mouthType>=25||a.mustacheType>=4||a.skinColor>=6||a.hairColor>=8||a.beardColor>=8||a.glassesColor>=6||a.favoriteColor>=12)return false;
 // Native-only fields need native resources/geometry, not dropped features.
 if(a.wrinkles||a.makeup||a.eyeAspect||a.browAspect||a.mouthAspect)return false;
 MiiHead head;
 constexpr unsigned groups[]={13,5,3,8,16,0,14,9,6};
 const unsigned choices[]={a.noseType,a.hairType,a.faceType,a.hairType,a.hairType,a.beardType,a.noseType,a.faceType,0};
 for(unsigned i=0;i<9;++i)if(!bank.shape(groups[i],choices[i],head.parts[i].shape))return false;
 std::array<MiiShapePlacement,9> placement;
 if(!miiShapePlacements(head.parts[2].shape.anchors,a.noseScale,a.noseY,a.glassesScale,a.glassesY,a.hairFlip,placement))return false;
 for(unsigned i=0;i<9;++i){auto& part=head.parts[i];part.mirrored=placement[i].mirrorX;
  for(auto& v:part.shape.vertices){v.position=miiPlacePosition(v.position,placement[i]);v.normal=miiPlaceNormal(v.normal,placement[i]);}
 }
 const unsigned texturedParts[]={2,4,6,8},textureGroups[]={4,17,15,7},textureChoices[]={spec.faceFeature,a.hairType,a.noseType,a.glassesType};
 for(unsigned i=0;i<4;++i)if(!head.parts[texturedParts[i]].shape.indices.empty()&&!bank.texture(textureGroups[i],textureChoices[i],head.parts[texturedParts[i]].texture))return false;
 std::array<MiiPartTexture,5> facial;
 constexpr unsigned faceGroups[]={1,2,11,12,10};
 const unsigned faceChoices[]={a.face.eyeType,a.face.browType,a.mouthType,a.mustacheType,a.moleEnabled?1u:0u};
 for(unsigned i=0;i<5;++i)if(!bank.texture(faceGroups[i],faceChoices[i],facial[i]))return false;
 MiiMaskInput mask;mask.face=a.face;mask.eyeColor=a.eyeColor;mask.browColor=a.browColor;mask.mouthColor=a.mouthColor;mask.beardColor=a.beardColor;
 constexpr unsigned map[]={0,0,1,1,2,3,3,4};
 for(unsigned i=0;i<8;++i){auto& t=facial[map[i]];mask.textures[i]={t.width,t.height,t.rgba.data()};}
 auto& texture=head.parts[7].texture;
 if(!composeMiiFaceMask(mask,resolution,texture.rgba))return false;
 texture.width=texture.height=resolution;texture.wrapS=texture.wrapT=0;
 head.skinColor=a.skinColor;head.hairColor=a.hairColor;head.beardColor=a.beardColor;head.glassesColor=a.glassesColor;head.favoriteColor=a.favoriteColor;
 out=std::move(head);return true;
}
}
