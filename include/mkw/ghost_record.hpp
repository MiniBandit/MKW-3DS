#pragma once
// Records the player's Time Trials inputs and writes them as an uncompressed
// RKG ghost (the Wii's ghost file, read back by Kinoko's KPadGhostController).
// Header layout: vendor/kinoko/source/game/system/GhostFile.hh.
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace mkw {
class GhostRecorder {
public:
 // RKG_HEADER_SIZE + RKG_UNCOMPRESSED_INPUT_DATA_SECTION_SIZE: the 0x2800
 // buffer Kinoko's RawGhostFile holds, less the file's trailing CRC-32.
 static constexpr std::size_t HeaderSize=0x88,InputLimit=0x2774;
 struct Frame {std::uint8_t face,stick,trick;};
 void reset(){frames_.clear();frames_.reserve(5*60*60);}
 // One simulation tick of the ghost stream: face buttons (1 accelerate,
 // 2 brake/drift, 8 hop after accelerating), the stick as raw 0..14 values
 // (7 centred) and the trick (0 none, 1 up, 2 down, 3 left, 4 right).
 void frame(unsigned face,unsigned stickX,unsigned stickY,unsigned trick){
  frames_.push_back({std::uint8_t(face&0xb),std::uint8_t((stickX&15)<<4|(stickY&15)),std::uint8_t(trick&7)});
 }
 // The port fires the player's mushroom before the karts move; Kinoko's
 // KartItem fires a ghost's after them (ItemDirector follows the karts in
 // RaceScene::calcEngines). Pressing the ghost's item button one tick early
 // gives its boost the same first tick.
 void itemUsedThisTick(){if(!frames_.empty())frames_.back().face|=4;}
 std::size_t frames()const{return frames_.size();}
 // Whole file: header, input section, CRC-32. Empty when the inputs don't
 // fit the Wii's ghost buffer.
 struct Info {unsigned course,vehicle,character,year,month,day;bool automatic;std::uint32_t finishMs;std::array<std::uint32_t,5> lapMs;unsigned laps;};
 std::vector<std::uint8_t> build(const Info& info)const{
  std::vector<std::uint16_t> face,stick,trick;
  // Face and stick: (value << 8) | frames, up to 255 frames a tuple.
  auto runs=[&](std::vector<std::uint16_t>& out,auto value,unsigned limit,auto pack){
   std::size_t i=0;
   while(i<frames_.size()){
    const auto v=value(frames_[i]);std::size_t n=1;
    while(i+n<frames_.size()&&value(frames_[i+n])==v&&n<limit)++n;
    out.push_back(pack(v,unsigned(n)));i+=n;
   }
  };
  runs(face,[](const Frame& f){return f.face;},255,[](unsigned v,unsigned n){return std::uint16_t(v<<8|n);});
  runs(stick,[](const Frame& f){return f.stick;},255,[](unsigned v,unsigned n){return std::uint16_t(v<<8|n);});
  // Trick: 0x70 of the high byte is the trick, its low nibble and the low
  // byte a 12-bit frame count.
  runs(trick,[](const Frame& f){return f.trick;},4095,[](unsigned v,unsigned n){return std::uint16_t((v<<12)|n);});
  const std::size_t inputBytes=8+2*(face.size()+stick.size()+trick.size());
  if(frames_.empty()||inputBytes>InputLimit)return {};
  std::vector<std::uint8_t> b(HeaderSize+inputBytes+4,0);
  auto put32=[&](std::size_t at,std::uint32_t v){for(unsigned k=0;k<4;++k)b[at+k]=std::uint8_t(v>>(24-8*k));};
  auto put16=[&](std::size_t at,unsigned v){b[at]=std::uint8_t(v>>8);b[at+1]=std::uint8_t(v);};
  auto time=[](std::uint32_t ms){return (ms/60000)<<25|(ms/1000%60)<<18|(ms%1000)<<8;};
  b[0]='R';b[1]='K';b[2]='G';b[3]='D';
  put32(4,time(info.finishMs)|(info.course&63)<<2);
  put32(8,(info.vehicle&63)<<26|(info.character&63)<<20|(info.year%100)<<13|(info.month&15)<<9|(info.day&31)<<4);
  // Ghost type 1 (the player's best time), uncompressed, drift type.
  put32(12,1u<<10|(info.automatic?1u:0u)<<17|std::uint32_t(inputBytes));
  b[0x10]=std::uint8_t(info.laps);
  for(unsigned lap=0;lap<5;++lap){const auto t=time(info.lapMs[lap]);b[0x11+3*lap]=std::uint8_t(t>>24);b[0x12+3*lap]=std::uint8_t(t>>16);b[0x13+3*lap]=std::uint8_t(t>>8);}
  b[0x34]=0xff;b[0x35]=0xff;b[0x36]=0xff;b[0x37]=0xff; // no location
  std::size_t at=HeaderSize;
  put16(at,unsigned(face.size()));put16(at+2,unsigned(stick.size()));put16(at+4,unsigned(trick.size()));at+=8;
  for(const auto* stream:{&face,&stick,&trick})for(auto v:*stream){put16(at,v);at+=2;}
  std::uint32_t c=~0u;
  for(std::size_t i=0;i<at;++i){c^=b[i];for(unsigned k=0;k<8;++k)c=(c>>1)^(0xedb88320u&-(c&1));}
  put32(at,~c);
  return b;
 }
private:
 std::vector<Frame> frames_;
};
}
