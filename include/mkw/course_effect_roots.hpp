#pragma once
namespace mkw {
template<class Effects> bool courseEffectRootsPresent(const Effects& effects,unsigned course){
switch(course){
case 8: return effects.find("rk_weed")>=0;
case 1: return effects.find("rk_choropuMoko1")>=0&&effects.find("rk_choropuMoko0")>=0&&effects.find("rk_weed")>=0;
case 2: return effects.find("rk_weed")>=0;
case 4: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_woodbox")>=0&&effects.find("rk_block")>=0&&effects.find("rk_steam")>=0&&effects.find("rk_alarm")>=0&&effects.find("rk_mud")>=0;
case 0: return effects.find("rk_weed")>=0;
case 5: return effects.find("rk_weed")>=0;
case 6: return effects.find("rk_dkCannon")>=0&&effects.find("rk_dkCannonSnw")>=0&&effects.find("rk_snowHDirt")>=0&&effects.find("rk_snowRoad0")>=0&&effects.find("rk_snowRoad1")>=0;
case 7: return effects.find("rk_truckChimSmkWCalc")>=0&&effects.find("rk_truckChimSmkW_A")>=0&&effects.find("rk_truckChimSmkW_B")>=0&&effects.find("rk_truckChimSmkW1T")>=0&&effects.find("rk_truckChimSmkW1TS")>=0;
case 9: return effects.find("rk_stoneTile")>=0;
case 15: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_epropeller")>=0&&effects.find("rk_epropellerS")>=0&&effects.find("rk_weed")>=0&&effects.find("rk_stone")>=0;
case 11: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_hanaSmk")>=0&&effects.find("rk_leaf_effect")>=0&&effects.find("rk_leaf_effectDash")>=0&&effects.find("rk_karehayamaS")>=0&&effects.find("rk_karehayama0S")>=0&&effects.find("rk_truckChimSmkWCalc")>=0&&effects.find("rk_truckChimSmkW_A")>=0&&effects.find("rk_truckChimSmkW_B")>=0&&effects.find("rk_truckChimSmkW1T")>=0&&effects.find("rk_truckChimSmkW1TS")>=0&&effects.find("rk_weed")>=0;
case 3: return effects.find("rk_pochaYogan")>=0&&effects.find("rk_pochaYoganS")>=0&&effects.find("rk_volcTail")>=0&&effects.find("rk_volcFire_L")>=0&&effects.find("rk_volcErupt")>=0&&effects.find("rk_volcBrk")>=0&&effects.find("rk_stoneVolc")>=0;
case 14: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_sanbo")>=0;
case 10: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_weed")>=0;
case 12: return effects.find("rk_koopaBallS")>=0&&effects.find("rk_pochaYogan")>=0&&effects.find("rk_pochaYoganS")>=0&&effects.find("rk_weed")>=0&&effects.find("rk_mud")>=0;
case 13: return effects.find("rk_StarRing")>=0&&effects.find("rk_entry")>=0;
case 16: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_weed")>=0;
case 20: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0;
case 25: return true;
case 26: return effects.find("rk_weed")>=0;
case 27: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_pgnSpin_s")>=0&&effects.find("rk_pgnSlider_s")>=0&&effects.find("rk_pgnSpin_m_l")>=0;
case 31: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_crabHide")>=0&&effects.find("rk_crabPocha")>=0&&effects.find("rk_crab")>=0&&effects.find("rk_weed")>=0;
case 23: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_woodbox")>=0&&effects.find("rk_weed")>=0&&effects.find("rk_mud")>=0;
case 18: return effects.find("rk_mud")>=0;
case 21: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_sanbo")>=0;
case 30: return effects.find("rk_pochaYogan")>=0&&effects.find("rk_pochaYoganS")>=0;
case 29: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_weed")>=0&&effects.find("rk_mud")>=0;
case 17: return effects.find("rk_weed")>=0;
case 24: return true;
case 22: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_choropuMoko1")>=0&&effects.find("rk_choropuMoko0")>=0&&effects.find("rk_weed")>=0&&effects.find("rk_flower")>=0;
case 19: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("rk_DKrockGC")>=0&&effects.find("rk_weed")>=0;
case 28: return effects.find("rk_pochaYogan")>=0&&effects.find("rk_pochaYoganS")>=0&&effects.find("rk_weed")>=0;
case 33: return true;
case 32: return effects.find("rk_pochaWater")>=0&&effects.find("rk_pochaWaterS")>=0&&effects.find("W_rk_truckChimSmkW_A")>=0&&effects.find("W_rk_truckChimSmkW_B")>=0&&effects.find("W_rk_truckChimSmkW1T")>=0&&effects.find("W_rk_truckChimSmkW1TS")>=0;
case 35: return true;
case 34: return true;
case 36: return true;
case 39: return true;
case 40: return true;
case 41: return true;
case 37: return true;
case 38: return true;
default:return false;
}
}
}
