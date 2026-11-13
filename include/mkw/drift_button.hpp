// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#pragma once
namespace mkw {
// The 3DS buttons as the race's retail input bits: 1 accelerate (A), 2 brake
// (B), 4 item (L), 8 drift (R). Kinoko starts a hop on the frame the drift
// bit appears, on the ground, whether or not the kart accelerates, so R alone
// is a brake. R becomes the drift button only when it is pressed while A is
// held, and stays it until either is let go: holding R and then pressing A
// accelerates without hopping (press R again to hop), and letting go of A
// during a drift and pressing it again does not hop either.
class DriftButton {
public:
    unsigned bits(bool a,bool b,bool l,bool r){
        const bool pressed=r&&!wasR_;wasR_=r;
        if(!r||!a)drifting_=false;
        if(pressed&&a)drifting_=true;
        unsigned out=(a?1u:0u)|(b?2u:0u)|(l?4u:0u);
        if(r)out|=drifting_?10u:a?0u:2u;
        return out;
    }
    void reset(){wasR_=drifting_=false;}
private:
    bool wasR_=false,drifting_=false;
};
}
