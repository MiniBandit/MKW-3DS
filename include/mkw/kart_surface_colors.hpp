#pragma once
#include <array>
#include <cstdint>

namespace mkw {
// KartEffect's +0x194 dust palette, retail USA 806ad294..806ae120.
// These are effect multipliers (including alpha), not replacement texture colors.
// Grass begins at index16 and mud at20; preserve their separate palette indices.
class KartSurfaceColors {
public:
    using Color = std::array<uint8_t, 4>;
    explicit KartSurfaceColors(unsigned course) : colors_(Default) {
        for (const auto& entry : Overrides)
            if (entry.course == course) colors_[entry.index] = entry.color;
    }
    const Color& operator[](unsigned index) const { return colors_.at(index); }
    void setSandArea(bool inside) { colors_[9]=inside?Color{207,183,85,255}:Color{231,184,87,255}; }
private:
    struct Override { unsigned course, index; Color color; };
    std::array<Color, 23> colors_;
    static constexpr std::array<Color, 23> Default{{
        {255,255,186,150}, {231,192,111,150}, {243,223,163,150}, {204,139,84,200},
        {159,125,64,200}, {255,255,186,150}, {231,231,196,100}, {243,228,120,255},
        {183,144,84,255}, {243,228,133,200}, {183,154,135,200}, {243,228,120,255},
        {183,144,84,255}, {255,208,109,255}, {187,134,48,255}, {231,192,111,150},
        {110,195,50,255}, {39,135,0,255}, {71,107,63,255}, {80,240,0,255},
        {116,77,0,255}, {99,75,45,255}, {159,112,60,255}
    }};
    static constexpr Override Overrides[]{
        {0,7,{243,228,144,255}}, {2,7,{69,117,85,128}}, {3,4,{124,80,39,255}},
        {12,7,{70,65,50,255}}, {14,7,{207,183,85,255}}, {18,8,{105,80,52,255}},
        {19,8,{111,87,57,255}}, {19,15,{231,178,95,100}}, {21,9,{231,184,87,255}},
        {22,1,{243,226,142,200}}, {24,7,{216,199,141,255}}, {26,7,{241,219,159,255}},
        {29,8,{117,85,51,255}}, {30,7,{159,97,87,255}}, {31,0,{255,238,157,150}},
        {35,3,{255,241,138,150}}, {36,0,{241,210,148,150}}, {39,7,{216,199,141,255}},
        {40,7,{255,224,176,255}},
        {0,16,{60,160,0,255}}, {1,16,{100,231,0,255}}, {2,16,{68,202,0,255}},
        {5,17,{60,195,0,255}}, {11,16,{140,120,0,255}}, {12,16,{107,77,47,255}},
        {12,21,{110,39,0,255}}, {15,16,{40,185,0,255}}, {16,16,{158,231,48,255}},
        {17,16,{85,170,0,255}}, {18,20,{82,91,67,255}}, {22,16,{50,185,0,255}},
        {23,16,{207,207,63,255}}, {23,21,{112,69,0,255}}, {26,16,{85,167,0,255}},
        {28,16,{90,175,0,255}}, {29,16,{73,158,0,255}}, {31,16,{63,183,0,255}}
    };
};
}
