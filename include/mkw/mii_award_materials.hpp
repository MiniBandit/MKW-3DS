#pragma once
namespace mkw {
struct MiiAwardMaterial {const char* stem;unsigned first,count;bool skin;};
inline constexpr MiiAwardMaterial MiiAwardMaterials[]={
 {"sa_mii_m",597,705,false},
 {"sa_mii_f",666,639,false},
 {"sb_mii_m",960,306,false},
 {"sb_mii_f",138,1038,false},
 {"sb_mii_f",1176,462,true},
 {"ma_mii_m",726,735,false},
 {"ma_mii_f",744,681,false},
 {"mb_mii_m",960,306,false},
 {"mb_mii_f",138,1038,false},
 {"mb_mii_f",1176,462,true},
 {"la_mii_m",756,735,false},
 {"la_mii_f",780,675,false},
 {"lb_mii_m",960,306,false},
 {"lb_mii_f",138,1038,false},
 {"lb_mii_f",1176,462,true},
};
}
