#include <mkw/retail_lighting.hpp>
#include <algorithm>
#include <cmath>

namespace mkw {
namespace {
constexpr unsigned GradientWidth=64;
// LightTexture::initialize: sTextureSize = GfxEngine::Configuration field 6
// (0x20, 0x80228FC0).
constexpr int TextureSize=32;
uint8_t channel(uint32_t rgba,unsigned i){return uint8_t(rgba>>(24-8*i));}
}

bool normalEnvironment(uint32_t word,uint32_t mode,uint32_t constant){
    if(word&retail::PrimaryEnvironment)return true;
    if(((word&retail::ChannelMask)>>retail::ChannelShift)==2)return true;
    switch(mode){
    case 0:case 3:case 4:case 7:case 11:case 12:case 13:case 14:case 15:case 19:case 21:case 22:case 23:case 24:case 25:return false;
    case 10:case 20:return !(constant&512);
    default:return true;
    }
}
std::array<float,4> viewLightFactor(const CourseLighting* course,uint32_t word,uint32_t material,Vec3 n,const float view[3][3]){
    std::array<float,4> out{1,1,1,1};
    if(!course||!(word&retail::Retail)||!(word&retail::ChannelMask))return out;
    const auto& l=course->lights[(word&retail::LightSetMask)>>retail::LightSetShift];
    const Vec3 toward=-retailLightViewDirection(l,view);
    const unsigned fn=(word&retail::DiffuseMask)>>retail::DiffuseShift;
    const float d=dot(normalized(n),toward);
    const float diffuse=fn==0?1.f:fn==1?std::max(d,-2.f):std::max(d,0.f);
    const bool on=(l.flags&0x41)==0x41,alpha=word&retail::AlphaLit;
    for(unsigned c=0;c<4;++c){
        if(c==3&&!alpha)continue;
        const float ambient=float(unsigned(.5f+float(channel(course->ambient[0],c))*channel(material,c)/255.f))/255.f;
        const float light=on&&(l.flags&(c==3?0x400:0x200))?channel(l.color,c)/255.f:0.f;
        out[c]=std::clamp(ambient+light*diffuse,0.f,1.f);
    }
    return out;
}

std::array<float,4> staticLightFactor(const CourseLighting* course,uint32_t word,uint32_t material,Vec3 n){
    const float identity[3][3]={{1,0,0},{0,1,0},{0,0,1}};
    return viewLightFactor(course,word,material,n,identity);
}

uint8_t lightGradientTexel(unsigned index,unsigned texel){
    // CpuTexture gradient (0x80217A3C) from black to white over [start,end],
    // then fillGradient's half-width rotation (0x802184CC).
    const LightGradient& g=LightGradients[index<10?index:0];
    const unsigned i=(texel+GradientWidth/2)%GradientWidth;
    if(i<=g.start)return 0;
    if(i+1>=g.end)return 255;
    float t=float(int(i)-int(g.start))/float(int(g.end)-int(g.start));
    switch(g.op){
    case 1:t=t*t;break;
    case 2:t=t*t*t*t;break;
    case 3:t=t*t*t*t;t=t*t;break;
    case 4:t=t*t*t*t;t=t*t;t=t*t;break;
    case 5:t=1.f-(t-1.f)*(t-1.f);break;
    case 6:t-=1.f;t=1.f+t*t*-t*t;break;
    default:break;
    }
    if(t<=0.f)return 0;
    if(t>=1.f)return 255;
    return uint8_t(int(0.f+t*255.f));
}

Vec3 retailLightViewDirection(const RetailLight& light,const float view[3][3]){
    const Vec3 dir{light.direction[0],light.direction[1],light.direction[2]};
    auto rotate=[&](Vec3 v){return Vec3{view[0][0]*v.x+view[0][1]*v.y+view[0][2]*v.z,
                                        view[1][0]*v.x+view[1][1]*v.y+view[1][2]*v.z,
                                        view[2][0]*v.x+view[2][1]*v.y+view[2][2]*v.z};};
    switch(light.space){
    case 2:return dir;              // authored in view space
    case 3:return {0.f,0.f,-1.f};
    case 4:return {0.f,0.f,1.f};
    case 1:{
        // Half angle between the light and the camera's forward axis
        // (C_VECHalfAngle of dir and -view row 2), negated.
        const Vec3 forward{-view[2][0],-view[2][1],-view[2][2]};
        if(length(forward)<=0.f)return rotate({0.f,0.f,1.f});
        Vec3 half=normalized(-dir,Vec3{})+normalized(-forward,Vec3{});
        if(dot(half,half)>0.f)half=normalized(half,Vec3{});
        return normalized(rotate(-half),Vec3{});
    }
    default:return normalized(rotate(dir),Vec3{});
    }
}

bool retailLightTextureDynamic(const CourseLighting& course,unsigned which){
    const auto& texture=course.textures[which&1];
    for(unsigned i=0;i<texture.count;++i){
        const auto& light=course.lights[texture.entries[i].light&15];
        if((light.flags&0x21)==0x21&&light.space<2)return true;
    }
    return false;
}

namespace {
void lightTextureMatrix(Vec3 direction,int* matrix){
    const float m[3]={-direction.x*0.484375f,-direction.y*0.484375f,-direction.z*0.484375f};
    for(unsigned i=0;i<3;++i)matrix[i]=int(m[i]*1024.f);
}
struct LightTextureTables {
    int8_t stu[TextureSize*TextureSize][3];
    uint8_t gradient[10][GradientWidth];
    LightTextureTables(){
        for(int y=0;y<TextureSize;++y)for(int x=0;x<TextureSize;++x){
            // CpuTexture::fillNormalMapSphere (0x802182E4): radius size/2,
            // normal (x-16, 16-y, sqrt(r^2-d^2)) normalized, stored as
            // 127+128*n (VecToColor 0x80228B94); indirect s,t,u = A,B,G = x,y,z.
            const float a=float(x-TextureSize/2),b=float(TextureSize/2-y),r=float(TextureSize/2);
            const float d2=std::min(a*a+b*b,r*r);
            const float z=r*r-d2>0.f?std::sqrt(r*r-d2):0.f;
            const Vec3 n=normalized(Vec3{a,b,z},Vec3{});
            const float nn[3]={n.x,n.y,n.z};
            for(int i=0;i<3;++i)stu[y*TextureSize+x][i]=int8_t(int(std::clamp(127.f+128.f*nn[i],0.f,255.f))-128);
        }
        for(unsigned g=0;g<10;++g)for(unsigned t=0;t<GradientWidth;++t)gradient[g][t]=lightGradientTexel(g,t);
    }
};
}

void retailLightTextureKey(const CourseLighting& course,unsigned which,const Vec3 viewDirections[16],int (&key)[12]){
    std::fill(key,key+12,0);
    const auto& texture=course.textures[which&1];
    unsigned count=0;
    for(unsigned e=0;e<texture.count&&count<4;++e){
        const unsigned light=texture.entries[e].light&15;
        if((course.lights[light].flags&0x21)!=0x21)continue;
        lightTextureMatrix(viewDirections[light],key+3*count++);
    }
}

void retailLightTexture(const CourseLighting& course,unsigned which,const Vec3 viewDirections[16],uint8_t* rgba){
    const auto& texture=course.textures[which&1];
    // Per light: GX TEV colour C (LightObject::CalcFinalDirColor, scaled by
    // the LTEX intensity below 1), konst K (dark colour), indirect matrix row
    // 0.484375*-direction as S1.10 with scale exponent -1 (0x8022DC24).
    struct Pass{int matrix[3];uint8_t c[3],k[3];unsigned gradient;};
    Pass passes[4];unsigned count=0;
    for(unsigned e=0;e<texture.count&&count<4;++e){
        const auto& entry=texture.entries[e];
        const auto& light=course.lights[entry.light&15];
        if((light.flags&0x21)!=0x21)continue;
        Pass& p=passes[count++];
        lightTextureMatrix(viewDirections[entry.light&15],p.matrix);
        for(unsigned i=0;i<3;++i){
            const float c=float(channel(light.color,i));
            p.c[i]=entry.intensity<1.f?uint8_t(int(c*entry.intensity)):channel(light.color,i);
            p.k[i]=channel(light.dark,i);
        }
        p.gradient=entry.gradient;
    }
    // The sphere normal map and the gradients never change: build them once
    // so a camera-relative texture costs one dot product per texel and light.
    static const LightTextureTables tables;
    for(int y=0;y<TextureSize;++y)for(int x=0;x<TextureSize;++x){
        const int8_t* stu=tables.stu[y*TextureSize+x];
        // Clear to the LightManager colour, then add each pass of at most
        // three lights (TEV-clamped) with GX_BL_ONE/GX_BL_ONE.
        int efb[3]={channel(course.clear,0),channel(course.clear,1),channel(course.clear,2)};
        int batch[3]={0,0,0};
        for(unsigned i=0;i<count;++i){
            const Pass& p=passes[i];
            // Indirect offset in 1/128 texels: (M.(s,t,u))>>3, then exponent -1.
            const int offset=((p.matrix[0]*stu[0]+p.matrix[1]*stu[1]+p.matrix[2]*stu[2])>>3)>>1;
            // TEXCOORD_NULL wrapped to 0 (ITW_0); bilinear repeat sample.
            const int position=offset-64; // texel centre (-0.5) in 1/128 units
            const int base=position>>7,fraction=position&127;
            const uint8_t* gradient=tables.gradient[p.gradient<10?p.gradient:0];
            const int t0=gradient[unsigned(base)&63],t1=gradient[unsigned(base+1)&63];
            const int g=(t0*(128-fraction)+t1*fraction)>>7;
            const int c=g+(g>>7);
            for(int ch=0;ch<3;++ch)batch[ch]=std::min(255,batch[ch]+((p.k[ch]*(256-c)+p.c[ch]*c)>>8));
            if(i%3==2||i+1==count){for(int ch=0;ch<3;++ch){efb[ch]=std::min(255,efb[ch]+batch[ch]);batch[ch]=0;}}
        }
        uint8_t* out=rgba+(y*TextureSize+x)*4;
        out[0]=uint8_t(efb[0]);out[1]=uint8_t(efb[1]);out[2]=uint8_t(efb[2]);out[3]=255;
    }
}

float retailFogFactor(unsigned type,float ze){
    // GX fog (Dolphin PixelShaderGen WriteFog): ze clamped to [0,1].
    float f=std::clamp(ze,0.f,1.f);
    switch(type&7){
    case 4:return 1.f-std::exp2(-8.f*f);
    case 5:return 1.f-std::exp2(-8.f*f*f);
    case 6:return std::exp2(-8.f*(1.f-f));
    case 7:f=1.f-f;return std::exp2(-8.f*f*f);
    case 2:return f;
    default:return 0.f;
    }
}

void RetailFogState::reset(const CourseLighting& course){
    // StaticR 0x805A30C8: the fog starts on state 0, progress 1.
    current=target=0;started=false;progress=1.f;
    value=course.fog[0];from=value;
}

void RetailFogState::step(const CourseLighting& course,int targetState,bool timed,uint32_t raceFrame){
    if(!course.fogCount)return;
    bool active=false;float speed=0.f;
    if(!timed){
        // StaticR 0x805A6B4C: the KMP AREA type 2 parameter (0 outside).
        const unsigned next=unsigned(targetState<0?0:targetState);
        if(next<course.fogCount)target=uint8_t(next);
        if(target!=current){from=value;progress=0.f;current=target;}
        active=progress<1.f;
    }else{
        // StaticR 0x805A3614: DK Summit (course 6) fades fog state 0 to 1
        // between race timer 3000 and 6000, quadratically.
        float t=0.f;
        if(raceFrame>=6000)t=1.f;
        else if(raceFrame>=3000){t=(1.f/3000.f)*(float(raceFrame)-3000.f);t*=t;}
        progress=t;active=t>0.f&&t<1.f;
        target=course.fogCount>1?1:0;
    }
    if(!active)return;
    const RetailFog& source=course.fog[current<course.fogCount?current:0];
    const RetailFog& dest=course.fog[target<course.fogCount?target:0];
    RetailFog to=dest;speed=dest.fade;
    if(timed)from=source;
    if(!dest.enabled){
        // Fading out: push the fog away, keep its colour.
        to.start=500000.f;to.end=1000000.f;to.color=value.color;
        if(speed==0.f)speed=0.03f;
    }
    if(!timed)progress+=speed;
    const bool done=progress>1.f;
    if(done)progress=1.f;
    value.start=from.start+progress*(to.start-from.start);
    value.end=from.end+progress*(to.end-from.end);
    uint32_t color=0;
    for(unsigned i=0;i<4;++i){
        const int a=channel(from.color,i),b=channel(to.color,i);
        color|=uint32_t(uint8_t(a+int8_t(int(progress*float(b-a)))))<<(24-8*i);
    }
    value.color=color;
    if(done&&!timed){value.type=dest.type;value.enabled=dest.enabled;}
}
}
