#pragma once
#include <cstdint>

namespace mkw {
// USA 8069cbf4..8069d1b4. Indices refer to KartEffect's per-wheel
// +0x164 category and +0x184 palette arrays. Some branches intentionally
// retain the old palette or both values; this is a state update, not a lookup.
struct KartSurfaceClassification {
    enum Kind : int { None=-1, Sand=0, Grass=1, SnowDirt=2, Water=3,
                      SnowRoad=4, Mud=5, Flower=6, Stone=7 };
    int kind=None;
    int color=0;
    void update(uint32_t floorTypes,unsigned variant,unsigned course,
                bool reducedEffects,bool driftAtLeastTwo) {
        const auto has=[&](unsigned bit){return (floorTypes&(uint32_t(1)<<bit))!=0;};
        const auto set=[&](int k,int c){kind=k;color=c;};
        if(has(1)) {
            if(variant==0)set(Sand,0);
            else if(variant==1)set(Sand,1);
            else if(variant==5&&!reducedEffects)set(Sand,2);
            else kind=None;
        } else if(has(2)&&!driftAtLeastTwo) {
            switch(variant) {
            case 0:set(Sand,3);break;
            case 1:set(Sand,4);break;
            case 2:kind=Water;break;
            case 3:set(Grass,2);break;
            case 4:set(Sand,5);break;
            case 5:set(Sand,6);break;
            case 6:case 7:kind=Stone;break;
            default:kind=None;break;
            }
        } else if(has(3)) {
            switch(variant) {
            case 0:set(Sand,7);break;
            case 1:set(Sand,8);break;
            case 5:set(Sand,9);break;
            case 7:set(Sand,10);break;
            case 2:set(Mud,0);break;
            case 6:kind=Stone;break;
            case 4:set(Grass,0);break;
            default:kind=None;break;
            }
        } else if(has(4)) {
            switch(variant) {
            case 0:set(Sand,11);break;
            case 1:set(Sand,12);break;
            case 6:set(Sand,13);break;
            case 3:kind=Flower;break;
            case 5:kind=SnowDirt;break;
            case 2:set(Mud,1);break;
            case 4:set(Grass,1);break;
            default:kind=None;break;
            }
        } else if(has(10)) {
            if(variant==7)set(Sand,14);else kind=None;
        } else if(has(0)&&(course==19||course==29)) {
            if(variant!=1)kind=None;
            else if(course==29||!reducedEffects)set(Sand,15);
        } else if(has(23)&&course==19) {
            if(variant!=2)kind=None;
            else if(!reducedEffects)set(Grass,3);
        } else if(has(22)) {
            if(variant==7)set(Mud,0);else kind=None;
        } else if(has(11)&&!driftAtLeastTwo&&variant<=3) {
            kind=Water;
        } else if(has(0)&&variant==5) {
            kind=SnowRoad;
        } else if(!has(21)) {
            kind=None;
        }
    }
};
}
