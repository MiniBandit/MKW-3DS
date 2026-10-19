#pragma once
namespace mkw {
// ObjectBank stores F * deltaGX * F, where F flips T to PICA V.
// Compose the unflipped delta after the authored GX environment SRT.
inline void animatedEnvironment(const float* base,const float* delta,float* result){
    const float gx[6]={delta[0],-delta[1],delta[1]+delta[2],-delta[3],delta[4],1-delta[4]-delta[5]};
    for(unsigned row=0;row<2;++row){
        for(unsigned col=0;col<2;++col)result[row*3+col]=gx[row*3]*base[col]+gx[row*3+1]*base[3+col];
        result[row*3+2]=gx[row*3]*base[2]+gx[row*3+1]*base[5]+gx[row*3+2];
    }
}
}
