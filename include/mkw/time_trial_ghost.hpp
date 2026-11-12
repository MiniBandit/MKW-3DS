// From ThePhilosophicalOne's retail-accuracy work (MiniBandit/MKW-3DS PR #76), MIT.
#pragma once
#include <mkw/ghost_file.hpp>
#include <array>
#include <vector>

namespace mkw {
// Time Trial ghosts on the SD card. A finished run faster than the course's
// saved ghost replaces it (pb-XX.rkg, XX the Kinoko course id in hex). A race
// loads the fastest usable RKG in the folder for its course, so ghosts copied
// there from a Wii or Dolphin save race too.
class TimeTrialGhost {
public:
    static constexpr const char* Folder="sdmc:/3ds/mkw-3ds/ghosts";
    // usable: whether a character/vehicle pair can be drawn and raced.
    bool load(unsigned course,bool(*usable)(unsigned character,unsigned vehicle));
    void clear(){file_.clear();frames_.clear();own_=false;}
    bool ready() const{return !file_.empty();}
    // The loaded ghost is this player's own saved run (pb-XX.rkg): retail's
    // personal-best ghost of the current license (GlobalContext +0x3C4 == 1,
    // +0x3CC == the license; RacePage_beforeCalc 0x80856CEC).
    bool own() const{return own_;}
    // The RKG for RaceConfig::setGhost (at least RkgSize bytes).
    const uint8_t* rkg() const{return file_.data();}
    const GhostHeader& header() const{return header_;}
    // The ghost's item button on its input frame n (0: the first frame after
    // the countdown starts), read ahead of Kinoko's ghost pad.
    bool item(unsigned frame) const{return frame<frames_.size()&&(frames_[frame].buttons&4);}
    // The fastest usable ghost per Kinoko course id in milliseconds (0: none).
    static void fastest(std::array<uint32_t,64>& times,bool(*usable)(unsigned character,unsigned vehicle));
    // The saved personal best for a course in milliseconds (0: none).
    static uint32_t savedTime(unsigned course);
    static bool save(const GhostRecorder& run,const GhostInfo& info);
    // MKW-3DS: every ghost for a course, for the ghost page to browse: the
    // player's best (pb-XX.rkg), the other RKGs in the folder (friends', a
    // Wii's), fastest first, then the disc's staff and expert staff ghosts.
    enum Kind:uint8_t {Own,Other,Staff,Expert};
    struct Entry {char path[96];GhostHeader header;Kind kind;char name[24];};
    static std::vector<Entry> list(unsigned course,unsigned menuCourse,bool(*usable)(unsigned character,unsigned vehicle));
    // One ghost by path (an Entry's).
    bool loadPath(const char* path,bool(*usable)(unsigned character,unsigned vehicle));
private:
    std::vector<uint8_t> file_;
    std::vector<GhostFrame> frames_;
    GhostHeader header_;
    bool own_=false;
};
}
