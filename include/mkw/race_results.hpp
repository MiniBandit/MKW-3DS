#pragma once
#include <mkw/color_fader.hpp>
#include <mkw/race_teams.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
// Retail single-player VS race end, after the player crosses the line
// (docs/RACE_CAMERA_SOURCE.md, "Post-race timeline"):
//  Finished  RaceHUD shows FINISH (CtrlRaceCount FadeIn, 70 frames) and waits
//            until its counter passes 120 (AfterControlUpdate 0x80835A28).
//  HudExit   the RaceHUD page ends: CtrlRaceBase controls fly off (20 frames).
//  Update*   GPVSLeaderboardUpdate: rows slide in (ControlAnimator, BRCTR delays),
//            points count up, A/B/START or the 600-frame timeout leaves.
//  Total*    GPVSLeaderboardTotal: rows sorted by total points.
//  Menu*     VSRaceEnd: Next Race / Quit Game (final race: Next only).
//  Leaving   the OK hold is over: the race scene fades to black through the
//            scene's ColorFader (Page_changeSection 0x805DDBD0 ->
//            SectionManager_startChangeSection 0x80604C30; the section
//            changes on the fade's frame 22). The race keeps running.
// Pure state: the caller supplies ticks, inputs and the result rows.
enum class RaceFlowPhase:uint8_t {Racing,Finished,HudExit,UpdateIn,Update,UpdateOut,TotalIn,Total,TotalOut,MenuIn,Menu,Chosen,Leaving,Done};
// advance: A, B or START (Leaderboard handlers); accept: A on the end menu.
struct RaceFlowInput {bool advance=false,accept=false;};
// One result row: the racer, its retail character id and VS point totals.
struct RaceResultEntry {uint8_t racer=0,character=0;bool human=false;uint16_t previous=0,gain=0;RaceTeam team=RaceTeam::None;};
// CtrlRaceResult score state (FillScore 0x807EB0AC, fn_1_2E53E8 0x807EB174,
// OnUpdate 0x807EB428): ten times the shown score, counting by 2 per frame.
struct RaceResultCount {
 float score10=0,remaining=0,step=0;int target10=0;int shown=0;bool rising=false;
 void fill(unsigned previous,unsigned gain);
 bool busy()const{return remaining< -.05f||remaining>.05f;}
 void update();
};

class RaceResultsFlow {
public:
 enum Event:uint32_t {EventNone=0,EventFill=1,EventResultsShown=2,EventTotalsShown=4,EventMenuShown=8,EventOptionChosen=16,EventLeave=32,EventHudExit=64};
 static constexpr unsigned FinishCountFrames=70;   // go_fade_in.brlan
 static constexpr unsigned FinishHoldFrames=121;   // counter > 120 (0x80835A28)
 static constexpr unsigned HudExitFrames=20;       // 0.05 per frame (0x807E2B34)
 static constexpr unsigned SlideFrames=10;         // ControlAnimator::Calc 0x805DDECC
 static constexpr unsigned BoardTimeout=600;       // Leaderboard::OnActivate 0x8083A504
 static constexpr unsigned VSRaces=4;              // offline VS default race count
 static constexpr float SlideWidth=832.f;          // sectionMgr+0x3F0 in 16:9 (layout units)
 // VS points by finishing position, row n-1 for n racers (USA 0x8088BBC0).
 static constexpr uint8_t PointsTable[12][12]={
  {15},{15,7},{15,8,2},{15,9,4,1},{15,9,5,2,1},{15,10,6,3,1},{15,10,7,5,3,1},{15,11,8,6,4,2,1},
  {15,11,8,6,4,3,2,1},{15,12,10,8,6,4,3,2,1},{15,12,10,8,6,5,4,3,2,1},{15,12,10,8,7,6,5,4,3,2,1,0}};
 static unsigned points(unsigned racers,unsigned position){return racers&&racers<=12&&position&&position<=racers?PointsTable[racers-1][position-1]:0;}

 void reset();
 // RaceHUD::BeforeControlUpdate: the local player has just finished.
 void finish(bool timeTrial=false);
 // One 60 Hz tick; returns Event bits. EventFill asks for fill() at once.
 uint32_t step(const RaceFlowInput& in);
 // GPVSLeaderboardUpdate::FillRows: rows in finishing order.
 void fill(const RaceResultEntry* byPosition,unsigned count,bool finalRace);

 RaceFlowPhase phase()const{return phase_;}
 bool active()const{return phase_!=RaceFlowPhase::Racing;}
 // Frames since finish() (FINISH animation frame), or -1.
 int finishFrame()const{return finishFrame_;}
 // HUD exit slide 0..1 (CtrlRaceBase), -1 before it starts.
 float hudSlide()const;
 bool hudGone()const{return phase_>RaceFlowPhase::HudExit;}
 // Board rows currently shown (update order or totals order).
 void setBattle(unsigned redWins,unsigned blueWins,unsigned target);
 bool battle()const{return battle_;}
 unsigned battleWins(RaceTeam team)const{return unsigned(team)<2?battleWins_[unsigned(team)]:0;}
 unsigned battleTarget()const{return battleTarget_;}
 bool teams()const{return teams_;}
 RaceTeam humanTeam()const{return humanTeam_;}
 const RaceTeamScore& teamScore(RaceTeam team)const{return teamScores_[unsigned(team)<2?unsigned(team):0];}
 // Original CalcTeamScoreDiff returns Red minus Blue; zero is a draw.
 int teamDifference()const{return int(teamScores_[0].total())-int(teamScores_[1].total());}
 RaceTeam winningTeam()const{return teamDifference()>0?RaceTeam::Red:teamDifference()<0?RaceTeam::Blue:RaceTeam::None;}
 unsigned teamShown(RaceTeam team)const{return unsigned(teamDisplayed_[unsigned(team)<2?unsigned(team):0]);}
 unsigned teamRowShown(unsigned row)const{return unsigned(teamRowDisplayed_[row]);}
 bool teamCounting()const;
 unsigned rowCount()const{return count_;}
 const RaceResultEntry& row(unsigned i)const{return rows_[i];}
 const RaceResultCount& count(unsigned i)const{return counts_[i];}
 bool totals()const{return phase_>=RaceFlowPhase::TotalIn;}
 bool boardVisible()const{return phase_>=RaceFlowPhase::UpdateIn&&phase_<=RaceFlowPhase::TotalOut;}
 // Horizontal offset (layout units) of control `delay` for the current page.
 float slideOffset(float delay)const;
 bool counting()const;
 // Original result controls: slide sounds follow each control delay; points
 // sound only when the displayed integer changes.
 int slideSound()const;
 bool pointsChanged()const{return pointsChanged_;}
 // End menu: buttons, cursor and the chosen option (0 Next, 1 Quit). The
 // page stays on screen under the fade until the section changes.
 bool menuVisible()const{return phase_>=RaceFlowPhase::MenuIn&&phase_<=RaceFlowPhase::Done;}
 // Black over the whole race screen while leaving (0 none, 1 opaque).
 float leaveFade()const{return phase_==RaceFlowPhase::Leaving?colorFaderOpacity(float(pageFrame_),false):phase_==RaceFlowPhase::Done?1.f:0.f;}
 bool timeTrial()const{return timeTrial_;}
 bool finalRace()const{return final_;}
 unsigned buttonCount()const{return final_?1u:2u;}
 unsigned cursor()const{return cursor_;}
 // The end-menu selection is owned by MenuState (UiPage::Results).
 void setCursor(unsigned cursor){if(phase_<RaceFlowPhase::Chosen)cursor_=std::min(cursor,buttonCount()-1);}
 int chosen()const{return chosen_;}
 unsigned pageFrame()const{return pageFrame_;}
 // Frames since the menu button was pressed (OK animation), or -1.
 int okFrame()const{return phase_==RaceFlowPhase::Chosen?int(pageFrame_):-1;}
 // Frames the OK animation holds the page before the section changes
 // (PushButton::GetAnimationFrameSize in RaceMenu::OnButtonClick 0x80838664).
 static constexpr unsigned ChooseFrames=31;      // common_w004_menu_text_light_01_ok.brlan
 // Fade-out frames before the section changes (EGG::ColorFader, color_fader.hpp).
 static constexpr unsigned LeaveFrames=unsigned(ColorFadeOutLastFrame);
 static constexpr unsigned MenuSlideMaxDelay=2;  // AfterMenuVS ButtonQuit delay
private:
 void enter(RaceFlowPhase phase){phase_=phase;pageFrame_=0;}
 void sortTotals();
 void sortBattleRows(bool totals);
 unsigned teamRowTarget(unsigned row)const;
 void stepTeamCounts();
 RaceFlowPhase phase_=RaceFlowPhase::Racing;
 int finishFrame_=-1;unsigned pageFrame_=0,timeout_=0,count_=0,cursor_=0;int chosen_=-1;bool final_=false,filled_=false;
 bool pointsChanged_=false;
 bool battle_=false;std::array<unsigned,2> battleWins_{};unsigned battleTarget_=2;
 bool teams_=false;RaceTeam humanTeam_=RaceTeam::None;
 std::array<RaceTeamScore,2> teamScores_{};
 std::array<float,2> teamDisplayed_{};std::array<float,12> teamRowDisplayed_{};
 bool timeTrial_=false;
 std::array<RaceResultEntry,12> rows_{};
 std::array<RaceResultCount,12> counts_{};
};
}
