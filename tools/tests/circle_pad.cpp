// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#include <mkw/circle_pad.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
    // Round dead zone: 20 units off centre in any direction is neutral.
    for(auto [x,y]:{std::pair{0,0},{20,0},{0,-20},{14,14},{-14,13}}){auto s=circlePadStick(x,y);assert(s.x==0&&s.y==0);}
    // Full lock at the rim, straight and diagonal.
    assert(circlePadStick(156,0).x==7&&circlePadStick(-156,0).x==-7&&circlePadStick(0,156).y==7);
    auto d=circlePadStick(110,110);assert(d.x==7&&d.y==7);
    auto d2=circlePadStick(-103,103);assert(d2.x==-7&&d2.y==7);
    // Halfway is about half lock; up only does not steer.
    auto h=circlePadStick(82,0);assert(h.x>=3&&h.x<=4&&h.y==0);
    auto u=circlePadStick(0,120);assert(u.x==0&&u.y>=6);
    // Every output stays on the lattice.
    for(int x=-160;x<=160;x+=7)for(int y=-160;y<=160;y+=7){auto s=circlePadStick(x,y);assert(s.x>=-7&&s.x<=7&&s.y>=-7&&s.y<=7);}
    std::puts("circle_pad PASS round dead zone, full lock at the rim and diagonals");
}
