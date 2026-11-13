// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#include <mkw/drift_button.hpp>
#include <cassert>
#include <cstdio>
using mkw::DriftButton;
// A hop starts on the frame bit 8 appears (Kinoko KartState::calcInput).
static bool hops(unsigned before,unsigned after){return (after&8)&&!(before&8);}
int main(){
    {// A held, then R: the normal hop and drift.
        DriftButton d;unsigned a=d.bits(true,false,false,false),b=d.bits(true,false,false,true);
        assert(a==1&&b==11&&hops(a,b));
        assert(d.bits(true,false,false,true)==11); // held: drifting, no second hop
    }
    {// R held first (braking), then A: accelerate, no hop. Then a new R press hops.
        DriftButton d;unsigned a=d.bits(false,false,false,true);assert(a==2);
        unsigned b=d.bits(true,false,false,true);assert(b==1&&!hops(a,b));
        unsigned c=d.bits(true,false,false,true);assert(c==1);
        unsigned e=d.bits(true,false,false,false);assert(e==1);
        unsigned f=d.bits(true,false,false,true);assert(f==11&&hops(e,f));
    }
    {// Letting go of A during a drift, then pressing it again: no hop.
        DriftButton d;d.bits(true,false,false,false);unsigned a=d.bits(true,false,false,true);assert(a==11);
        unsigned b=d.bits(false,false,false,true);assert(b==2);
        unsigned c=d.bits(true,false,false,true);assert(c==1&&!hops(b,c));
    }
    {// A and R pressed on the same frame: a hop, as before.
        DriftButton d;unsigned a=d.bits(false,false,false,false),b=d.bits(true,false,false,true);assert(b==11&&hops(a,b));
    }
    {// B and L pass through; R alone still brakes.
        DriftButton d;assert(d.bits(false,true,true,false)==6);assert(d.bits(false,false,false,true)==2);
        d.reset();assert(d.bits(true,false,false,true)==11);
    }
    std::puts("drift_button PASS");
}
