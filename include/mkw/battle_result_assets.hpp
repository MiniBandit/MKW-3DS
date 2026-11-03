#pragma once
#include <mkw/race_flow_layout.hpp>
namespace mkw {
// Native BattleWin, ResultBTTeamNULL and ResultTeamPoint. Separate mode bank.
inline constexpr const char* BattleResultTexture="textures/battle_result_font.t3x";
inline constexpr FlowPane FlowBattleWinPanes[]={
    {"RootPane",-1,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,640.f,480.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"count_down_null",0,0,3,4,255,-1,-3.07636583e-09f,-4.1018211e-09f,0.f,0.f,1.f,1.f,100.f,100.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"text_00",1,2,3,4,221,0,0.f,0.f,100.f,0.f,1.78724837f,1.78724837f,250.f,200.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},4,1,43.f,51.f,0.f},
    {"text_light",1,2,3,4,0,1,-0.f,-0.f,100.f,0.f,2.f,2.f,250.f,200.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},4,1,43.f,51.f,0.f},
};
inline constexpr FlowMaterial FlowBattleWinMaterials[]={
    {"text_00",{{255,255,255,0}},{{255,107,109,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"text_light",{{255,255,255,0}},{{255,255,255,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
};
inline constexpr FlowClip FlowBattleWinClips[]={
    {"Fade","Hide",2.f,1.f,false,-1,0,1}, // new_record_fade_in_before.brlan
    {"Fade","Fadein",170.f,1.f,false,0,1,20}, // new_record_fade_in.brlan
};
inline constexpr FlowTrack FlowBattleWinTracks[]={
    {1,4,0,true,0,2},
    {2,0,6,false,2,5},
    {2,0,7,false,7,5},
    {2,1,16,false,12,5},
    {3,0,6,false,17,2},
    {3,0,7,false,19,2},
    {3,1,16,false,21,6},
    {0,2,4,false,27,1},
    {0,2,5,false,28,1},
    {0,2,6,false,29,1},
    {0,2,8,false,30,1},
    {0,2,9,false,31,3},
    {0,2,10,false,34,3},
    {0,2,11,false,37,1},
    {1,2,4,false,38,1},
    {1,2,5,false,39,1},
    {1,2,6,false,40,1},
    {1,2,8,false,41,1},
    {1,2,9,false,42,1},
    {1,2,10,false,43,1},
    {1,4,0,true,44,1},
};
inline constexpr FlowKey FlowBattleWinKeys[]={
    {0.f,0.f,0.f},{1.f,1.f,0.f},{0.f,90.f,-9.81111145f},{9.f,1.70000005f,-9.81111145f},
    {9.f,1.70000005f,0.00134228135f},{158.f,1.89999998f,0.00134228135f},{158.f,1.89999998f,5.30064249f},{0.f,90.f,-9.81111145f},
    {9.f,1.70000005f,-9.81111145f},{9.f,1.70000005f,0.00134228135f},{158.f,1.89999998f,0.00134228135f},{158.f,1.89999998f,5.30064249f},
    {0.f,0.f,28.333334f},{9.f,255.f,28.333334f},{9.f,255.f,0.f},{49.f,255.f,0.f},
    {159.f,0.f,0.f},{9.f,1.70000005f,0.00999999885f},{39.f,2.f,0.00999999885f},{9.f,1.70000005f,0.00999999885f},
    {39.f,2.f,0.00999999885f},{0.f,0.f,0.f},{8.f,0.f,0.f},{8.f,0.f,255.f},
    {9.f,255.f,255.f},{9.f,255.f,-1.7708106f},{17.f,0.f,-39.1246185f},{9.f,255.f,0.f},
    {9.f,255.f,0.f},{9.f,255.f,0.f},{14.f,255.f,0.f},{14.f,255.f,0.f},
    {39.f,155.f,-1.81143999f},{159.f,77.f,0.f},{14.f,255.f,0.f},{77.f,105.f,-1.22758615f},
    {159.f,77.f,0.f},{39.f,255.f,0.f},{19.f,255.f,0.f},{19.f,255.f,0.f},
    {19.f,255.f,0.f},{99.f,255.f,0.f},{99.f,255.f,0.f},{99.f,255.f,0.f},
    {0.f,1.f,0.f},
};
inline constexpr FlowLayoutDef FlowBattleWinLayout{FlowBattleWinPanes,4,FlowBattleWinMaterials,2,FlowBattleWinClips,2,FlowBattleWinTracks,FlowBattleWinKeys};
inline constexpr FlowPane FlowBattleWinPointPanes[]={
    {"RootPane",-1,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,608.f,456.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"total_point_null",0,0,1,4,255,-1,-0.f,-0.f,0.f,0.f,1.f,1.f,30.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"pts_sha",1,2,1,4,255,3,0.5f,-28.f,0.f,0.f,1.f,1.f,70.f,50.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},4,1,34.4000015f,40.7999992f,0.f},
    {"point_sha",1,2,1,4,255,2,-0.5f,-1.5f,0.f,0.f,1.f,1.f,100.f,70.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},4,1,51.6000023f,61.2000008f,0.f},
    {"pts",1,2,1,4,255,1,0.5f,-26.f,0.f,0.f,1.f,1.f,70.f,50.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},4,1,34.4000015f,40.7999992f,0.f},
    {"point",1,2,1,4,255,0,-0.f,2.5f,0.f,0.f,1.f,1.f,100.f,70.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},4,1,51.6000023f,61.2000008f,0.f},
};
inline constexpr FlowMaterial FlowBattleWinPointMaterials[]={
    {"point",{{0,0,0,0}},{{230,70,0,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"pts",{{0,0,0,0}},{{180,30,0,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"point_sha",{{0,0,0,0}},{{0,0,0,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"pts_sha",{{0,0,0,0}},{{0,0,0,255}},-1,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
};
inline constexpr FlowClip FlowBattleWinPointClips[]={
    {"team","blue",100.f,1.f,true,-1,0,6}, // common_w054_team_big_total_point_blue_team.brlan
    {"team","red",99.f,1.f,true,-1,6,6}, // common_w054_team_big_total_point_red_team.brlan
};
inline constexpr FlowTrack FlowBattleWinPointTracks[]={
    {0,2,8,false,0,1},
    {0,2,9,false,1,1},
    {0,2,10,false,2,1},
    {1,2,8,false,3,1},
    {1,2,9,false,4,1},
    {1,2,10,false,5,1},
    {0,2,8,false,6,1},
    {0,2,9,false,7,1},
    {0,2,10,false,8,1},
    {1,2,8,false,9,1},
    {1,2,9,false,10,1},
    {1,2,10,false,11,1},
};
inline constexpr FlowKey FlowBattleWinPointKeys[]={
    {1.f,0.f,0.f},{1.f,180.f,0.f},{1.f,255.f,0.f},{1.f,80.f,0.f},
    {1.f,80.f,0.f},{1.f,255.f,0.f},{-1.f,230.f,0.f},{-1.f,70.f,0.f},
    {-1.f,0.f,0.f},{-1.f,180.f,0.f},{-1.f,30.f,0.f},{-1.f,0.f,0.f},
};
inline constexpr FlowLayoutDef FlowBattleWinPointLayout{FlowBattleWinPointPanes,6,FlowBattleWinPointMaterials,4,FlowBattleWinPointClips,2,FlowBattleWinPointTracks,FlowBattleWinPointKeys};
inline constexpr FlowControl FlowBattleWinControl={0.f,170.f,1.f,1.f,10.f};
inline constexpr FlowControl FlowBattleColumns[2]={{-150.f,-20.f,1.f,1.f,45.f},{150.f,-20.f,1.f,1.f,50.f}};
inline constexpr FlowControl FlowBattleWinPoints[2]={{0.f,130.f,1.f,1.5f,0.f},{0.f,130.f,1.f,1.5f,0.f}};
inline constexpr const char* FlowBattleWinText[3]={"The red team wins!","The blue team wins!","Tie"};
inline constexpr const char* FlowBattleWinsUnit="wins";
inline constexpr FlowGlyph FlowBattleWinGlyphs[]={
{char16_t(32),190,107,0,12,12},
{char16_t(33),145,107,13,0,14},
{char16_t(48),228,1,27,-2,24},
{char16_t(49),103,107,19,1,24},
{char16_t(50),59,54,26,-1,24},
{char16_t(51),78,1,28,-3,24},
{char16_t(52),108,1,28,-2,24},
{char16_t(53),138,1,28,-2,24},
{char16_t(54),1,54,27,-2,24},
{char16_t(55),87,54,26,-1,24},
{char16_t(56),168,1,28,-2,24},
{char16_t(57),30,54,27,-2,24},
{char16_t(84),198,1,28,-2,23},
{char16_t(97),115,54,26,-1,23},
{char16_t(98),143,54,26,-2,21},
{char16_t(100),171,54,26,-2,23},
{char16_t(101),199,54,26,-2,21},
{char16_t(104),227,54,25,-1,22},
{char16_t(105),160,107,13,0,12},
{char16_t(108),175,107,13,0,12},
{char16_t(109),41,1,35,-3,29},
{char16_t(110),1,107,25,-1,23},
{char16_t(114),124,107,19,-2,15},
{char16_t(115),55,107,24,-3,19},
{char16_t(116),81,107,20,-2,15},
{char16_t(117),28,107,25,-2,21},
{char16_t(119),1,1,38,-3,32},
};
inline constexpr FlowFont FlowBattleWinFont{0,256,256,51,42,51,51,FlowBattleWinGlyphs,27};
inline constexpr FlowGlyph FlowBattleNumberRedGlyphs[]={
{char16_t(32),251,63,0,16,16},
{char16_t(48),1,1,48,-1,46},
{char16_t(49),51,1,48,-1,46},
{char16_t(50),101,1,48,-1,46},
{char16_t(51),151,1,48,-1,46},
{char16_t(52),201,1,48,-1,46},
{char16_t(53),1,63,48,-1,46},
{char16_t(54),51,63,48,-1,46},
{char16_t(55),101,63,48,-1,46},
{char16_t(56),151,63,48,-1,46},
{char16_t(57),201,63,48,-1,46},
};
inline constexpr FlowFont FlowBattleNumberRed{1,256,128,60,48,64,64,FlowBattleNumberRedGlyphs,11};
inline constexpr FlowGlyph FlowBattleNumberBlueGlyphs[]={
{char16_t(32),251,63,0,16,16},
{char16_t(48),1,1,48,-1,46},
{char16_t(49),51,1,48,-1,46},
{char16_t(50),101,1,48,-1,46},
{char16_t(51),151,1,48,-1,46},
{char16_t(52),201,1,48,-1,46},
{char16_t(53),1,63,48,-1,46},
{char16_t(54),51,63,48,-1,46},
{char16_t(55),101,63,48,-1,46},
{char16_t(56),151,63,48,-1,46},
{char16_t(57),201,63,48,-1,46},
};
inline constexpr FlowFont FlowBattleNumberBlue{2,256,128,60,48,64,64,FlowBattleNumberBlueGlyphs,11};
inline constexpr FlowPane FlowBattleMarkRedPanes[]={
    {"RootPane",-1,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,640.f,480.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"capsule_null",0,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,40.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"capsule02",1,1,1,4,255,1,-0.f,-0.f,0.f,0.f,0.800000012f,0.800000012f,40.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,0.625f,0.f,0.f,0.625f,0.625f,0.625f}},0,0,0.f,0.f,0.f},
    {"capsule01",1,1,1,4,255,0,-0.f,-0.f,0.f,0.f,1.f,1.f,40.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,0.625f,0.f,0.f,0.625f,0.625f,0.625f}},0,0,0.f,0.f,0.f},
    {"light",1,1,1,4,0,3,-0.f,-0.f,0.f,0.f,1.13081598f,1.13081598f,32.f,32.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"flash_light",1,1,1,4,255,2,-0.f,-0.f,0.f,0.f,0.f,0.f,32.f,32.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
};
inline constexpr FlowMaterial FlowBattleMarkRedMaterials[]={
    {"capsule01",{{0,0,0,0}},{{255,255,255,255}},3,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"capsule02",{{0,0,0,0}},{{255,255,255,255}},3,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"flash_light",{{255,0,0,0}},{{255,226,191,255}},4,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"light",{{255,0,0,0}},{{255,189,189,255}},4,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
};
inline constexpr FlowClip FlowBattleMarkRedClips[]={
    {"OnOff","On",40.f,1.f,true,-1,0,1}, // glass_dot_red_light_on.brlan
    {"OnOff","Off",9.f,1.f,true,-1,1,1}, // glass_dot_red_light_off.brlan
    {"Loop","Loop",40.f,1.f,true,-1,2,3}, // glass_dot_red_flash_loop.brlan
    {"Loop","Stop",9.f,1.f,true,-1,5,3}, // glass_dot_red_flash_stop.brlan
};
inline constexpr FlowTrack FlowBattleMarkRedTracks[]={
    {4,1,16,false,0,1},
    {4,1,16,false,1,1},
    {5,0,6,false,2,4},
    {5,0,7,false,6,4},
    {5,1,16,false,10,3},
    {5,0,6,false,13,1},
    {5,0,7,false,14,1},
    {5,1,16,false,15,1},
};
inline constexpr FlowKey FlowBattleMarkRedKeys[]={
    {0.f,255.f,0.f},{0.f,0.f,0.f},{0.f,0.f,0.0987779573f},{39.f,3.85234046f,0.0310852248f},
    {39.f,3.85234046f,-0.200000048f},{40.f,0.f,-3.85234046f},{0.f,0.f,0.0987779573f},{39.f,3.85234046f,0.0310852248f},
    {39.f,3.85234046f,-0.200000048f},{40.f,0.f,-3.85234046f},{1.f,255.f,0.f},{39.f,10.f,0.f},
    {40.f,255.f,0.f},{4.f,0.f,0.f},{4.f,0.f,0.f},{-1.f,255.f,0.f},
};
inline constexpr FlowLayoutDef FlowBattleMarkRedLayout{FlowBattleMarkRedPanes,6,FlowBattleMarkRedMaterials,4,FlowBattleMarkRedClips,4,FlowBattleMarkRedTracks,FlowBattleMarkRedKeys};
inline constexpr FlowPane FlowBattleMarkBluePanes[]={
    {"RootPane",-1,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,640.f,480.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"capsule_null",0,0,1,4,255,-1,0.f,0.f,0.f,0.f,1.f,1.f,40.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"capsule02",1,1,1,4,255,1,-0.f,-0.f,0.f,0.f,0.800000012f,0.800000012f,40.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,0.625f,0.f,0.f,0.625f,0.625f,0.625f}},0,0,0.f,0.f,0.f},
    {"capsule01",1,1,1,4,255,0,-0.f,-0.f,0.f,0.f,1.f,1.f,40.f,40.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,0.625f,0.f,0.f,0.625f,0.625f,0.625f}},0,0,0.f,0.f,0.f},
    {"light",1,1,1,4,0,3,-0.f,-0.f,0.f,0.f,1.13081598f,1.13081598f,32.f,32.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
    {"flash_light",1,1,1,4,255,2,-0.f,-0.f,0.f,0.f,0.f,0.f,32.f,32.f,{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},{{0.f,0.f,1.f,0.f,0.f,1.f,1.f,1.f}},0,0,0.f,0.f,0.f},
};
inline constexpr FlowMaterial FlowBattleMarkBlueMaterials[]={
    {"capsule01",{{0,0,0,0}},{{255,255,255,255}},3,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"capsule02",{{0,0,0,0}},{{255,255,255,255}},3,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"flash_light",{{0,232,241,0}},{{198,255,255,255}},4,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
    {"light",{{0,0,255,0}},{{0,255,255,255}},4,0,0,{{0.f,0.f,0.f,1.f,1.f}}},
};
inline constexpr FlowClip FlowBattleMarkBlueClips[]={
    {"OnOff","On",40.f,1.f,true,-1,0,1}, // glass_dot_blue_light_on.brlan
    {"OnOff","Off",9.f,1.f,true,-1,1,1}, // glass_dot_blue_light_off.brlan
    {"Loop","Loop",40.f,1.f,true,-1,2,3}, // glass_dot_blue_flash_loop.brlan
    {"Loop","Stop",9.f,1.f,true,-1,5,3}, // glass_dot_blue_flash_stop.brlan
};
inline constexpr FlowTrack FlowBattleMarkBlueTracks[]={
    {4,1,16,false,0,1},
    {4,1,16,false,1,1},
    {5,0,6,false,2,4},
    {5,0,7,false,6,4},
    {5,1,16,false,10,3},
    {5,0,6,false,13,1},
    {5,0,7,false,14,1},
    {5,1,16,false,15,1},
};
inline constexpr FlowKey FlowBattleMarkBlueKeys[]={
    {0.f,255.f,0.f},{0.f,0.f,0.f},{0.f,0.f,0.0987779573f},{39.f,3.85234046f,0.0310852248f},
    {39.f,3.85234046f,-0.200000048f},{40.f,0.f,-3.85234046f},{0.f,0.f,0.0987779573f},{39.f,3.85234046f,0.0310852248f},
    {39.f,3.85234046f,-0.200000048f},{40.f,0.f,-3.85234046f},{1.f,255.f,0.f},{39.f,10.f,0.f},
    {40.f,255.f,0.f},{4.f,0.f,0.f},{4.f,0.f,0.f},{-1.f,255.f,0.f},
};
inline constexpr FlowLayoutDef FlowBattleMarkBlueLayout{FlowBattleMarkBluePanes,6,FlowBattleMarkBlueMaterials,4,FlowBattleMarkBlueClips,4,FlowBattleMarkBlueTracks,FlowBattleMarkBlueKeys};
inline constexpr const char* BattleResultTextures[]={"textures/battle_result_font.t3x","textures/battle_result_number_red.t3x","textures/battle_result_number_blue.t3x","textures/battle_result_tt_glass_sphere.t3x","textures/battle_result_tt_star_01.t3x"};
}
