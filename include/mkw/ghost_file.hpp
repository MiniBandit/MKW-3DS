#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkw {
// Mario Kart Wii ghost files (RKG, https://wiki.tockdom.com/wiki/RKG_(File_Format)):
// a 0x88-byte header, the run-length input section Kinoko's KPadGhostController
// reads, then a CRC-32. Written uncompressed, the size Kinoko's RawGhostFile copies.
inline constexpr std::size_t RkgSize=0x2800,RkgHeaderSize=0x88,RkgInputCapacity=0x2774;

// One frame of ghost input. buttons: accelerate 1, brake 2, item 4, drift 8;
// stick axes 0..14 (7 neutral); trick 0 none, 1 up, 2 down, 3 left, 4 right.
struct GhostFrame {uint8_t buttons=0,stickX=7,stickY=7,trick=0;};

// Encodes a racer's inputs frame by frame into the three tuple streams
// (face buttons, stick, trick) that KPadGhostButtonsStream decodes.
class GhostRecorder {
public:
    void reset();
    // Ignored once the section is full (retail cannot save such a ghost either).
    void push(const GhostFrame& frame);
    unsigned frames() const{return frames_;}
    bool full() const{return full_;}
    // The input section (three big-endian tuple counts, padding, tuples).
    // Returns its size, or 0 when it does not fit in capacity.
    std::size_t write(uint8_t* out,std::size_t capacity) const;
private:
    struct Stream {std::vector<uint16_t> tuples;uint8_t value=0;uint16_t length=0;};
    void append(Stream& s,uint8_t value,uint16_t limit);
    std::size_t bytes() const;
    std::array<Stream,3> streams_;
    unsigned frames_=0;bool full_=false;
};

struct RkgTime {uint16_t min=0;uint8_t sec=0;uint16_t ms=0;};
inline RkgTime rkgTime(uint32_t ms){return {uint16_t(ms/60000),uint8_t(ms/1000%60),uint16_t(ms%1000)};}
inline uint32_t rkgMilliseconds(RkgTime t){return t.min*60000u+t.sec*1000u+t.ms;}

struct GhostInfo {
    RkgTime finish;
    std::array<RkgTime,3> laps{};
    uint8_t course=0,character=0,vehicle=0;
    // 0 Wii Wheel, 1 Nunchuk, 2 Classic Controller, 3 GameCube.
    uint8_t controller=2;
    // 1 = the player's best time.
    uint8_t type=1;
    bool automaticDrift=false;
    uint8_t year=0,month=1,day=1; // year counted from 2000
};

// Builds a complete uncompressed RKG. False when the inputs do not fit.
bool buildRkg(const GhostRecorder& inputs,const GhostInfo& info,uint8_t* out /* RkgSize bytes */);

struct GhostHeader {
    RkgTime finish;
    uint8_t course=0,character=0,vehicle=0;
    bool automaticDrift=false,compressed=false;
};
// The header fields, after the checks Kinoko's RawGhostFile would otherwise
// fail on: magic, date ranges, input length and the closing CRC-32.
bool readRkgHeader(const uint8_t* data,std::size_t size,GhostHeader& out);
// The same fields and range checks from the first 0x10 bytes alone (no length
// or CRC-32 check): enough to rank a ghost before reading the whole file.
bool peekRkgHeader(const uint8_t* data,std::size_t size,GhostHeader& out);
// The input section, Yaz1-decompressed when the file is compressed.
bool rkgInputSection(const uint8_t* data,std::size_t size,std::vector<uint8_t>& section);

// Every frame of an uncompressed input section, as Kinoko replays it.
bool decodeGhostInputs(const uint8_t* section,std::size_t size,std::vector<GhostFrame>& frames);

uint32_t rkgCrc32(const uint8_t* data,std::size_t size);
}
