#include <mkw/menu_ui.hpp>
#include <mkw/item_odds.hpp>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace mkw;
int main(){
 UiInput a;a.accept=true;UiInput b;b.back=true;UiInput right;right.right=true;UiInput left;left.left=true;
 MenuState s;s.selection.mode=UiMode::VS;s.enter(UiPage::VSMode);
 s.input(right);assert(s.cursor==2);s.input(a);assert(s.page==UiPage::VSRules);
 s.cursor=0;s.input(right);assert(s.selection.vsClass==3);s.input(right);assert(s.selection.vsClass==0);
 s.cursor=1;s.input(left);assert(s.selection.vsCpu==0);s.input(left);assert(s.selection.vsCpu==2);
 s.cursor=5;const unsigned expected[]={5,8,10,12,16,32,2,3,4};
 for(auto n:expected){s.input(right);assert(s.selection.vsRaces==n);}
 s.input(b);assert(s.page==UiPage::VSMode&&s.cursor==2);
 s.selection.vsCourses=1;s.enter(UiPage::Drift);s.cursor=1;s.input(a);assert(s.page==UiPage::Confirm);
 bool seen[32]{};for(unsigned n=0;n<32;++n){assert(s.selection.course<32&&!seen[s.selection.course]);seen[s.selection.course]=true;if(n<31){s.finish(false);assert(s.input(a)==UiAction::StartRace);}}
 s.input(b);assert(s.page==UiPage::Drift);
 s.selection.vsCourses=2;s.cursor=0;s.input(a);assert(s.selection.course==0);
 for(unsigned n=1;n<32;++n){s.finish(false);assert(s.input(a)==UiAction::StartRace);assert(s.selection.course==n);}
 s.finish(true);s.input(a);assert(s.page==UiPage::MainMenu);
 s.enter(UiPage::VSMode);s.cursor=1;s.input(a);assert(s.page==UiPage::Character&&s.selection.teams&&!s.unavailable);assert(s.supportsRace());s.input(a);assert(s.page==UiPage::TeamConfirm&&s.count()==2);
 const auto team=s.selection.humanTeam;s.input(right);assert(s.cursor==1);s.input(a);assert(s.page==UiPage::VSRules);s.input(b);assert(s.page==UiPage::TeamConfirm&&s.cursor==1&&s.selection.humanTeam==team);
 s.input(left);assert(s.cursor==0);s.input(a);assert(s.page==UiPage::Vehicle);s.input(b);assert(s.page==UiPage::TeamConfirm);s.input(b);assert(s.page==UiPage::Character);
 // Synthetic 4-table ItemSlot with values chosen to expose integer truncation
 // and >255 strong-item weights (the original uses u16, not u8).
 std::vector<unsigned char> bytes{4};for(int t=0;t<4;++t){bytes.push_back(12);bytes.push_back(19);for(int i=0;i<19*12;++i)bytes.push_back(100);}
 ItemOdds odds;assert(odds.load(bytes.data(),bytes.size(),2,3,1));
 assert(odds.weights(false,1)[0]==25&&odds.weights(true,12)[7]==400);
 assert(odds.load(bytes.data(),bytes.size(),2,3,2));assert(odds.weights(false,1)[0]==800&&odds.weights(false,1)[1]==25&&odds.weights(true,3)[7]==0);
 assert(odds.load(bytes.data(),bytes.size(),2,3,3));for(auto w:odds.weights(false,1))assert(w==0);
 assert(odds.load(bytes.data(),bytes.size(),2,3));assert(odds.weights(false,1)[7]==100);
 std::puts("PASS VS rule navigation, original race-count cycle, 32-course shuffle/order and source odds transforms");
}
