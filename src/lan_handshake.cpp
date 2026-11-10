#include <mkw/lan_handshake.hpp>
#include <mkw/lan_handshake_icon.hpp>
#include <3ds.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern "C" const u8 default_font_bin[]; // libctru's 8x8 console font: 256 glyphs, high bit leftmost

namespace mkw {
namespace {
constexpr unsigned TexW=512,TexH=256,ScreenW=400,ScreenH=240,Columns=ScreenW/8,Rows=ScreenH/8;
constexpr unsigned IconTexW=256,IconTexH=128;
float ease(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
}
struct LanTerminalState {
    struct Line{std::string text;bool bright;};
    std::vector<Line> lines;Texture text,icon;bool dirty=true;
};

LanTerminal::LanTerminal(Renderer& renderer):renderer_(renderer),state_(std::make_unique<LanTerminalState>()){
    // The icon is white: only its coverage varies.
    std::vector<uint8_t> pixels(IconTexW*IconTexH*4,0);
    for(unsigned y=0;y<LanHandshakeIconHeight;++y)for(unsigned x=0;x<LanHandshakeIconWidth;++x){
        auto* p=&pixels[4*(y*IconTexW+x)];p[0]=p[1]=p[2]=255;p[3]=LanHandshakeIcon[y*LanHandshakeIconWidth+x];
    }
    // A solid block beside the icon for the terminal cursor.
    for(unsigned y=0;y<8;++y)for(unsigned x=200;x<208;++x){auto* p=&pixels[4*(y*IconTexW+x)];p[0]=p[1]=p[2]=p[3]=255;}
    state_->icon.initRgba8(IconTexW,IconTexH,pixels.data(),pixels.size());
}
LanTerminal::~LanTerminal()=default;

void LanTerminal::add(const char* line,bool bright){
    auto& lines=state_->lines;
    std::string text(line);
    // Wrap long lines at the terminal width.
    do{lines.push_back({text.substr(0,Columns),bright});text=text.size()>Columns?"  "+text.substr(Columns):"";}while(!text.empty());
    if(lines.size()>Rows-1)lines.erase(lines.begin(),lines.end()-(Rows-1));
    state_->dirty=true;
}

void LanTerminal::rebuild(){
    std::vector<uint8_t> pixels(TexW*TexH*4,0);
    auto glyph=[&](unsigned column,unsigned row,unsigned char c,const uint8_t rgb[3]){
        for(unsigned y=0;y<8;++y){const uint8_t bits=default_font_bin[c*8+y];
            for(unsigned x=0;x<8;++x)if(bits&(0x80>>x)){auto* p=&pixels[4*((row*8+y)*TexW+column*8+x)];p[0]=rgb[0];p[1]=rgb[1];p[2]=rgb[2];p[3]=255;}}
    };
    const uint8_t green[3]={120,230,120},white[3]={255,255,255};
    for(unsigned r=0;r<state_->lines.size();++r){const auto& line=state_->lines[r];
        for(unsigned c=0;c<line.text.size()&&c<Columns;++c)glyph(c,r,static_cast<unsigned char>(line.text[c]),line.bright?white:green);}
    state_->text.reset();state_->text.initRgba8(TexW,TexH,pixels.data(),pixels.size());state_->dirty=false;
}

void LanTerminal::frame(float connectedSeconds){
    if(state_->dirty)rebuild();
    renderer_.begin({0,0,1000},{0,0,0},{0,1,0});
    renderer_.drawScreenFade(1,false);
    const float textAlpha=connectedSeconds<0?1.f:1.f-.65f*ease(connectedSeconds/.3f);
    renderer_.drawScreenSpriteRegion(state_->text,0,0,float(ScreenW),float(ScreenH),0,0,float(ScreenW)/TexW,float(ScreenH)/TexH,textAlpha);
    if(connectedSeconds<0){
        // A blinking cursor under the last line while waiting.
        if(unsigned(svcGetSystemTick()/(SYSCLOCK_ARM11/2))&1u)renderer_.drawScreenSpriteRegion(state_->icon,0,float(state_->lines.size()*8),8,8,200.f/IconTexW,0,208.f/IconTexW,8.f/IconTexH,1);
    }else{
        // Grows in, then shakes three times.
        const float t=connectedSeconds,grow=.6f+.4f*ease(t/.3f);
        const float shake=t>.3f&&t<1.5f?std::sin((t-.3f)*3.f*6.2831853f/1.2f)*8.f:0.f;
        const float w=LanHandshakeIconWidth*1.4f*grow,h=LanHandshakeIconHeight*1.4f*grow;
        renderer_.drawScreenSpriteRegion(state_->icon,(ScreenW-w)*.5f,(ScreenH-h)*.5f+shake,w,h,0,0,
            float(LanHandshakeIconWidth)/IconTexW,float(LanHandshakeIconHeight)/IconTexH,ease(t/.2f));
    }
    renderer_.end();
}
}
