// Generated from original demo BRLYT/BRCTR, BMG font-1 and original icon TPLs.
#pragma once
#include <mkw/race_flow_layout.hpp>
namespace mkw {
inline constexpr const char* IntroTextures[]={"textures/intro_font.t3x","textures/intro_icon_0.t3x","textures/intro_icon_1.t3x","textures/intro_icon_2.t3x","textures/intro_icon_3.t3x","textures/intro_icon_4.t3x","textures/intro_icon_5.t3x","textures/intro_icon_6.t3x","textures/intro_icon_7.t3x","textures/intro_icon_8.t3x","textures/intro_icon_9.t3x","textures/intro_icon_10.t3x"};
inline constexpr FlowGlyph IntroGlyphs[]={
{u' ',89,160,0,12,12},
{u'\'',44,160,13,-1,12},
{u'/',406,107,21,-3,14},
{u'0',181,54,27,-2,24},
{u'1',429,107,21,1,24},
{u'2',326,54,26,-1,24},
{u'3',412,1,28,-3,24},
{u'4',442,1,28,-2,24},
{u'5',472,1,28,-2,24},
{u'6',210,54,27,-2,24},
{u'7',354,54,26,-1,24},
{u'8',1,54,28,-2,24},
{u'9',239,54,27,-2,24},
{u'A',156,1,32,-2,28},
{u'B',31,54,28,-2,23},
{u'C',319,1,29,-2,25},
{u'D',223,1,30,-2,25},
{u'E',382,54,26,-2,21},
{u'F',276,107,24,-2,20},
{u'G',255,1,30,-2,26},
{u'H',350,1,29,-2,24},
{u'J',452,107,20,-2,17},
{u'K',381,1,29,-1,26},
{u'L',302,107,24,-2,20},
{u'M',121,1,33,-2,29},
{u'N',61,54,28,-2,25},
{u'P',268,54,27,-2,22},
{u'R',91,54,28,-2,24},
{u'S',121,54,28,-3,23},
{u'T',151,54,28,-2,23},
{u'V',190,1,31,-3,26},
{u'W',1,1,41,-4,33},
{u'Y',287,1,30,-3,25},
{u'a',410,54,26,-1,23},
{u'b',438,54,26,-2,21},
{u'c',328,107,24,-1,22},
{u'd',466,54,26,-2,23},
{u'e',1,107,26,-2,21},
{u'f',474,107,20,-2,15},
{u'g',141,107,25,-2,22},
{u'h',168,107,25,-1,22},
{u'i',59,160,13,0,12},
{u'k',195,107,25,-2,20},
{u'l',74,160,13,0,12},
{u'm',84,1,35,-3,29},
{u'n',222,107,25,-1,23},
{u'o',29,107,26,-1,24},
{u'p',57,107,26,-2,21},
{u'q',85,107,26,-2,22},
{u'r',23,160,19,-2,15},
{u's',354,107,24,-3,19},
{u't',1,160,20,-2,15},
{u'u',249,107,25,-2,21},
{u'v',297,54,27,-2,23},
{u'w',44,1,38,-3,32},
{u'y',113,107,26,-3,21},
{u'z',380,107,24,-3,18},
};
inline constexpr FlowFont IntroFont{0,512,256,51,42,51,51,IntroGlyphs,57};
inline constexpr const char* IntroCourseNames[]={"Luigi Circuit","Moo Moo Meadows","Mushroom Gorge","Toad's Factory","Mario Circuit","Coconut Mall","DK Summit","Wario's Gold Mine","Daisy Circuit","Koopa Cape","Maple Treeway","Grumble Volcano","Dry Dry Ruins","Moonview Highway","Bowser's Castle","Rainbow Road","GCN Peach Beach","DS Yoshi Falls","SNES Ghost Valley 2","N64 Mario Raceway","N64 Sherbet Land","GBA Shy Guy Beach","DS Delfino Square","GCN Waluigi Stadium","DS Desert Hills","GBA Bowser Castle 3","N64 DK's Jungle Parkway","GCN Mario Circuit","SNES Mario Circuit 3","DS Peach Gardens","GCN DK Mountain","N64 Bowser's Castle","Block Plaza","Delfino Pier","Funky Stadium","Chain Chomp Wheel","Thwomp Desert","SNES Battle Course 4","GBA Battle Course 3","N64 Skyscraper","GCN Cookie Land","DS Twilight House"};
inline constexpr const char* IntroCupNames[]={"Mushroom Cup","Flower Cup","Star Cup","Special Cup","Shell Cup","Banana Cup","Leaf Cup","Lightning Cup"};
inline constexpr const char* IntroEngineNames[]={"50cc","100cc","150cc","Mirror"};
inline constexpr const char* IntroGPTemplates[]={"%s %s 1st Race","%s %s 2nd Race","%s %s 3rd Race","%s %s Final Race"};
inline constexpr const char* IntroVSTemplate="%s VS %u/%u";
inline constexpr const char* IntroTeamVSTemplate="%s Team VS Race %u/%u";
inline constexpr const char* IntroBalloonTemplate="Balloon Battle";
inline constexpr const char* IntroCoinTemplate="Coin Runners";
inline constexpr FlowPane IntroCoursePanes[]={
    {"RootPane",-1,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,608.f,456.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"course_name_null",0,0,7,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,30.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"shadow",1,2,1,4,255,0,-0.f,-1.f,0.f,0.f,1.f,1.f,200.f,35.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},5,2,25.8000011f,30.6000004f,0.f},
    {"course_name",1,2,1,4,255,1,-0.f,1.f,0.f,0.f,1.f,1.f,200.f,35.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},5,2,25.8000011f,30.6000004f,0.f},
};
inline constexpr FlowMaterial IntroCourseMaterials[]={
    {"shadow",{{0,0,0,0}},{{0,0,0,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"course_name",{{0,0,0,0}},{{192,192,192,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
};
inline constexpr FlowClip IntroCourseClips[]={
};
inline constexpr FlowTrack IntroCourseTracks[]={
};
inline constexpr FlowKey IntroCourseKeys[]={
};
inline constexpr FlowLayoutDef IntroCourseLayout{IntroCoursePanes,4,IntroCourseMaterials,2,IntroCourseClips,0,IntroCourseTracks,IntroCourseKeys};
inline constexpr FlowControl IntroCourseControl{250.f,-190.f,1.20000005f,1.20000005f,20.f};
inline constexpr FlowPane IntroCupPanes[]={
    {"RootPane",-1,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,608.f,456.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"cup_name_null",0,0,7,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,30.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"cup_icon",1,1,1,4,255,1,10.f,1.5f,0.f,0.f,1.f,1.f,64.f,64.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"cup_name",1,2,1,4,255,0,231.f,-10.f,0.f,0.f,1.f,1.f,380.666687f,35.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},3,0,25.8000011f,30.6000004f,0.f},
};
inline constexpr FlowMaterial IntroCupMaterials[]={
    {"cup_name",{{0,0,0,0}},{{255,199,58,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"cup_icon",{{0,0,0,0}},{{255,255,255,255}},1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
};
inline constexpr FlowClip IntroCupClips[]={
};
inline constexpr FlowTrack IntroCupTracks[]={
};
inline constexpr FlowKey IntroCupKeys[]={
};
inline constexpr FlowLayoutDef IntroCupLayout{IntroCupPanes,4,IntroCupMaterials,2,IntroCupClips,0,IntroCupTracks,IntroCupKeys};
inline constexpr FlowControl IntroCupControl{-340.f,170.f,1.20000005f,1.20000005f,10.f};
}
