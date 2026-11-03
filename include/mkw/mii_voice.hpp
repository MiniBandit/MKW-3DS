#pragma once
#include <mkw/mii_appearance.hpp>
#include <mkw/race_voice_tables.hpp>
namespace mkw {
// USA808648c0 / 80861dfc: favourite colour, size, sex -> one of 24 voices.
inline unsigned miiVoiceIndex(const MiiAppearance& a){
 constexpr unsigned color[]={0,1,2,3,0,1,2,3,0,1,2,3};
 const unsigned sum=a.height+a.weight,size=sum>150?2:sum>104?0:1;
 return color[a.favoriteColor%12]*3+size+(a.female?12:0);
}
inline constexpr unsigned MiiVoiceRanges[2][29][2]={
 {{4009,4011},{4012,4014},{4018,4020},{4015,4017},{3973,3975},{3949,3951},{3952,3957},{914,914},{3964,3966},{3961,3963},{914,914},{3958,3960},{4030,4032},{3970,3972},{3967,3969},{3976,3978},{3979,3981},{3988,3990},{3985,3987},{3982,3984},{3994,3996},{3991,3993},{3997,3999},{4000,4002},{914,914},{914,914},{4003,4005},{4006,4008},{4063,4065}},
 {{914,914},{914,914},{4061,4061},{4060,4060},{4045,4047},{4033,4035},{4036,4038},{914,914},{914,914},{4039,4041},{914,914},{914,914},{4062,4062},{914,914},{914,914},{914,914},{4042,4044},{4051,4053},{914,914},{4048,4050},{914,914},{4054,4056},{4057,4057},{4058,4058},{914,914},{914,914},{4059,4059},{914,914},{4066,4066}},
};
// USA808638d4: 24 menu Mii ranges, shared by driver and vehicle confirmation.
inline constexpr unsigned MiiMenuVoiceRanges[24][2]={{995,997},{998,1000},{1001,1003},{1004,1006},{1007,1009},{1010,1012},{1013,1015},{1016,1018},{1019,1021},{1022,1024},{1025,1027},{1028,1030},{1031,1033},{1034,1036},{1037,1039},{1040,1042},{1043,1045},{1046,1048},{1049,1051},{1052,1054},{1055,1057},{1058,1060},{1061,1063},{1064,1066}};
inline unsigned raceVoiceRange(unsigned character,bool local,unsigned type,unsigned end,unsigned mii){return character<24?voice::Range[character][local?0:1][type][end]:MiiVoiceRanges[local?0:1][type][end]+118*mii;}
// USA8085f4b8 delegates nonlocal remapping to the common character path.
inline int raceVoiceRemap(unsigned character,bool local,unsigned type){return character<24?voice::Remap[character][local?1:0][type]:local?(type==10?6:type==7?19:int(type)):voice::Remap[0][0][type];}
}
