#include <mkw/lan_handshake.hpp>
#include <mkw/renderer.hpp>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace mkw {
namespace {
// Two original cartoon hands, drawn here so there is no art file to convert:
// a 128x128 cell each, the left one reaching right, the right one mirrored.
constexpr unsigned Cell=128,Width=Cell*2,Height=Cell;
struct Rgb{float r,g,b;};
float capsule(float px,float py,float ax,float ay,float bx,float by,float radius){
    const float dx=bx-ax,dy=by-ay,t=std::clamp(((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
    const float ex=px-(ax+dx*t),ey=py-(ay+dy*t);return std::sqrt(ex*ex+ey*ey)-radius;
}
float box(float px,float py,float x0,float y0,float x1,float y1,float round){
    const float cx=(x0+x1)*.5f,cy=(y0+y1)*.5f,hx=(x1-x0)*.5f-round,hy=(y1-y0)*.5f-round;
    const float qx=std::fabs(px-cx)-hx,qy=std::fabs(py-cy)-hy;
    return std::sqrt(std::max(qx,0.f)*std::max(qx,0.f)+std::max(qy,0.f)*std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-round;
}
// Signed distance to the hand (palm, fingers, thumb) and to its sleeve, in cell pixels.
void hand(float x,float y,float& skin,float& sleeve){
    float d=box(x,y,26,40,84,92,12);
    const float fingers[4][2]={{46,0},{57,2},{68,1},{79,-2}};
    for(const auto& f:fingers)d=std::min(d,capsule(x,y,80,f[0],114+f[1],f[0],5.5f));
    d=std::min(d,capsule(x,y,62,44,92,24,7.f));
    skin=d;sleeve=box(x,y,2,34,34,98,6);
}
void paint(std::vector<uint8_t>& pixels,unsigned ox,bool mirror,Rgb skinColour,Rgb sleeveColour){
    for(unsigned y=0;y<Cell;++y)for(unsigned x=0;x<Cell;++x){
        const float px=(mirror?float(Cell-1-x):float(x))+.5f,py=float(y)+.5f;
        float skin,sleeve;hand(px,py,skin,sleeve);
        // Draw the sleeve over the wrist, each with a dark outline 2 px wide.
        Rgb colour{0,0,0};float alpha=0;
        auto layer=[&](float d,Rgb fill){
            const float outline=std::clamp(1.5f-std::fabs(d+1.f),0.f,1.f),inside=std::clamp(.5f-d,0.f,1.f);
            if(inside<=0)return;
            const float shade=std::clamp(1.f-(py/Cell)*.25f,0.f,1.f);
            Rgb c{fill.r*shade,fill.g*shade,fill.b*shade};
            c={c.r*(1-outline)+40*outline,c.g*(1-outline)+28*outline,c.b*(1-outline)+24*outline};
            colour={colour.r*(1-inside)+c.r*inside,colour.g*(1-inside)+c.g*inside,colour.b*(1-inside)+c.b*inside};
            alpha=std::max(alpha,inside);
        };
        layer(skin,skinColour);layer(sleeve,sleeveColour);
        auto* p=&pixels[4*(y*Width+ox+x)];
        p[0]=uint8_t(colour.r);p[1]=uint8_t(colour.g);p[2]=uint8_t(colour.b);p[3]=uint8_t(alpha*255);
    }
}
float ease(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
}

void playLanHandshake(Renderer& renderer){
    std::vector<uint8_t> pixels(Width*Height*4,0);
    paint(pixels,0,false,{246,206,168},{214,58,52});
    paint(pixels,Cell,true,{200,146,104},{56,104,214});
    Texture texture;if(!texture.initRgba8(Width,Height,pixels.data(),pixels.size()))return;
    constexpr unsigned Frames=150;
    for(unsigned frame=0;frame<Frames&&aptMainLoop();++frame){
        const float t=frame/60.f;
        // Slide in (0.6 s), shake three times (1.2 s), then fade out.
        const float meet=ease(t/.6f),shake=t>.6f&&t<1.8f?std::sin((t-.6f)*3.f*6.2831853f/1.2f)*10.f:0.f;
        const float alpha=1.f-ease((t-2.f)/.5f);
        const float size=160,y=120-size*.5f+shake;
        const float leftX=-size+(200-size*.70f+size)*meet,rightX=400-(400-(200-size*.30f))*meet;
        renderer.begin({0,0,1000},{0,0,0},{0,1,0});
        renderer.drawScreenFade(1,false);
        renderer.drawScreenSpriteRegion(texture,leftX,y,size,size,0,0,.5f,1,alpha);
        renderer.drawScreenSpriteRegion(texture,rightX,y,size,size,.5f,0,1,1,alpha);
        renderer.end();
    }
}
}
