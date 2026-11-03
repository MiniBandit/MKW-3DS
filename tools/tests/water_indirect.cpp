#include "mkw/water_indirect.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(int argc,char** argv){
    // Original factory matrix recovered from signed BP coefficients/exponent.
    const mkw::WaterIndirectMatrix m{{185,-276,0,86,593,0},3};
    unsigned count=0;
    for(unsigned a=0;a<256;++a)for(unsigned i=0;i<256;++i){
        const auto actual=m.offset(a,i,i);
        const double s=185*(int(a)-128)-276*(int(i)-128);
        const double t=86*(int(a)-128)+593*(int(i)-128);
        // Independent arithmetic floor models arithmetic shifts, including
        // negative values that C++ division toward zero would get wrong.
        assert(actual[0]==std::floor(std::floor(s/8)/8));
        assert(actual[1]==std::floor(std::floor(t/8)/8));
        ++count;
    }
    const auto neutral=m.offset(128,128,128);
    assert(neutral[0]==0&&neutral[1]==0);
    auto left=m;left.shift=-2;
    assert(left.offset(129,128,128)[0]==92);
    std::printf("PASS: %u factory IA8 samples, signed shifts, neutral and exponent\n",count);
    if(argc==2){
        struct Reader{FILE* f;bool read(void* p,size_t n){return std::fread(p,1,n,f)==n;}} reader{std::fopen(argv[1],"rb")};
        assert(reader.f);uint32_t header[2];assert(reader.read(header,8));
        assert(header[0]==0x324e4957&&header[1]==1);
        mkw::WaterIndirectField field;assert(field.read(reader));assert(std::fgetc(reader.f)==EOF);std::fclose(reader.f);
        assert(field.width==64&&field.height==64&&field.frames==500);
        unsigned moved=0;
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
            const float u=((x+.5f)/64-field.uv[2])/field.uv[0];
            const float v=((y+.5f)/64-field.uv[5])/field.uv[4];
            const auto p=&field.abg[(y*64+x)*3];
            assert(field.sample(u,v,0)==field.matrix.offset(p[0],p[1],p[2]));
            assert(field.sample(u,v,0)==field.sample(u,v,500));
            if(field.sample(u,v,0)!=field.sample(u,v,73))++moved;
        }
        assert(moved>0);
        assert(field.levels==3&&field.maxLOD==2);
        size_t base=0;unsigned checked=0;
        for(unsigned l=0;l<field.levels;++l){
            const unsigned w=field.width>>l,h=field.height>>l;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                const auto filtered=field.filtered((x+.5f)/w,(y+.5f)/h,l);
                for(unsigned c=0;c<3;++c)assert(filtered[c]==field.abg[base+(y*w+x)*3+c]);
                ++checked;
            }
            base+=w*h*3;
        }
        const std::array<mkw::WaterIndirectField::Projected,3> p={{{0,0,1,0,0},{16,0,1,1,0},{0,16,1,0,1}}};
        assert(std::abs(field.lodAt(p,0)-std::log2(3.2f))<1.e-5f);
        // Magnification/minification clamp applies before sampling.
        assert(field.sample(.1f,.2f,0,100)==field.sample(.1f,.2f,0,2));
        std::printf("PASS: %u original mip texels and screen-space LOD gradient\n",checked);
        std::printf("PASS: cooked WIND read, 4096 original texel centres, frame wrap, %u animated samples\n",moved);
    }
}
