#pragma once
namespace mkw {
// Coconut Mall escalators as retail draws them (StaticR USA 0x807f2574). The
// step strip moves rigidly with the object (Kinoko's pos carries the step
// phase sf, 0 <= sf < 20, two treads per period). Four one-step models finish
// the landings: at the top B1 (150 steps up) and A1 (160) shrink into the
// comb, at the bottom B2 (10 steps down) and A2 (20) grow out of it. Offsets
// are in the strip's moved frame; scaleY scales the piece about its base.
// A2 at scaleY 0 is a flat plate under the lower comb, drawn as retail does.
// Slots: 0 A1, 1 B1, 2 A2, 3 B2; a slot's model is escalatorA when even.
struct EscalatorPiece {bool shown;float y,z,scaleY;};
inline void escalatorPieces(float sf,EscalatorPiece (&out)[4]){
    if(sf>=0.f&&sf<10.f){
        out[0]={true,1600.f,-4800.f,(10.f-sf)/10.f};
        out[1]={true,1500.f,-4500.f,1.f};
        out[2]={true,50.f-10.f*sf,600.f,0.f};
        out[3]={true,50.f-15.f*sf,300.f,sf/10.f};
    }else{
        out[0]={false,0,0,0};
        out[1]={sf<20.f,1500.f,-4500.f,(20.f-sf)/10.f};
        out[2]={sf<20.f,100.f-15.f*sf,600.f,(sf-10.f)/10.f};
        out[3]={false,0,0,0};
    }
}
}
