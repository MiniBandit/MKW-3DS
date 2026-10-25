#include <mkw/game_settings.hpp>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
GameSettings settings;
unsigned current=0;
void path(char (&out)[48],unsigned license){std::snprintf(out,sizeof out,"sdmc:/3ds/mkw-3ds/settings%u.txt",license);}
}

GameSettings& gameSettings(){return settings;}

void loadGameSettings(unsigned license){
    current=license<4?license:0;settings=GameSettings{};
    char name[48];path(name,current);
    FILE* f=std::fopen(name,"r");
    if(!f){
        // Before per-license settings the LED's off switch was led.txt (0).
        if(FILE* old=std::fopen("sdmc:/3ds/mkw-3ds/led.txt","r")){if(std::fgetc(old)=='0')settings.led=LedMode::Off;std::fclose(old);}
        return;
    }
    char line[32];
    while(std::fgets(line,sizeof line,f)){
        unsigned v=0;
        if(std::sscanf(line,"led=%u",&v)==1&&v<=3)settings.led=LedMode(v);
        else if(std::sscanf(line,"bright=%u",&v)==1&&v>=50&&v<=100)settings.brightness=uint8_t(v/10*10);
        else if(std::sscanf(line,"shadows=%u",&v)==1)settings.shadows=v!=0;
        else if(std::sscanf(line,"speedometer=%u",&v)==1)settings.speedometer=v!=0;
    }
    std::fclose(f);
}
void saveGameSettings(){
    char name[48];path(name,current);
    if(FILE* f=std::fopen(name,"w")){
        std::fprintf(f,"led=%u\nbright=%u\nshadows=%u\nspeedometer=%u\n",unsigned(settings.led),unsigned(settings.brightness),
                     settings.shadows?1u:0u,settings.speedometer?1u:0u);
        std::fclose(f);
    }
}
void cycleLedMode(){settings.led=LedMode((unsigned(settings.led)+1)%4);saveGameSettings();}
void cycleLedBrightness(){settings.brightness=settings.brightness>=100?50:uint8_t(settings.brightness+10);saveGameSettings();}
const char* ledModeLabel(LedMode mode){
    switch(mode){
    case LedMode::Off:return "LED OFF";
    case LedMode::Items:return "LED ITEMS";
    case LedMode::ItemsEvents:return "LED +EVENTS";
    case LedMode::ItemsEventsDrifts:return "LED +DRIFTS";
    }
    return "LED";
}
}
