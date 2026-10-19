#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace mkw {
// Mii records the game can list, read from miis/miis.txt in the asset folder
// (the SD folder, or an update's overlay: include/mkw/overlay.hpp). Foundation
// for Mii support delivered later as an update: the loader, the file format and
// the portrait slots exist now; drawing a Mii's own face and racing as a Mii
// arrive with the update that ships the matching code and portraits.
//
// miis/miis.txt, one Mii per line, fields separated by '|'; '#' starts a comment:
//   id | name | source | portrait | data
//   id       up to 15 characters, [A-Za-z0-9_-]; names the portrait file
//   name     the Mii's name (up to 23 bytes, UTF-8)
//   source   custom, console (3DS Mii Maker) or wii (Wii save file)
//   portrait relative path of a .t3x portrait texture (may be empty)
//   data     the raw Mii data as hex (3DS Mii data 96 bytes, Wii RFLCharData 74
//            bytes); may be empty
struct MiiEntry {
    enum Source : std::uint8_t {Custom,Console,WiiSave};
    char id[16]{};
    char name[24]{};
    Source source=Custom;
    char portrait[64]{};
    std::array<std::uint8_t,96> data{};
    std::uint8_t dataBytes=0;
};
// Parses manifest text into out; returns the number of valid entries (at most
// capacity). Malformed lines are skipped. Pure function (host-testable).
unsigned parseMiiManifest(const char* text,std::size_t size,MiiEntry* out,unsigned capacity);
class MiiStore {
public:
    static constexpr unsigned Capacity=32;
    // Reads miis/miis.txt; false (and no Miis) when absent or empty.
    bool load();
    void clear(){count_=0;}
    unsigned count() const {return count_;}
    const MiiEntry* at(unsigned index) const {return index<count_?&entries_[index]:nullptr;}
    const MiiEntry* find(const char* id) const;
private:
    std::array<MiiEntry,Capacity> entries_{};
    unsigned count_=0;
};
MiiStore& miiStore();
}
