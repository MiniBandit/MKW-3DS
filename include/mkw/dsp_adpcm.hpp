#pragma once
#include <cstdint>

namespace mkw {
// Nintendo DSP ADPCM: 8-byte frames, a header byte (predictor, scale) and 14
// nibbles, high nibble first. Bit-exact with tools/cook_menu_movies.py's
// decoder, which made the PCM the game played before MADP files: the same
// arithmetic, widened so the sums can't overflow.
inline void decodeDspAdpcm(const unsigned char* in,unsigned samples,const std::int16_t coefficients[16],
                           int history1,int history2,std::int16_t* out,unsigned stride){
 for(unsigned i=0;i<samples;in+=8){
  const unsigned predictor=(in[0]>>4)&7;const std::int64_t scale=std::int64_t(1)<<(in[0]&15);
  const std::int64_t c1=coefficients[predictor*2],c2=coefficients[predictor*2+1];
  for(unsigned k=0;k<14&&i<samples;++k,++i){
   int nibble=k&1?in[1+(k>>1)]&15:in[1+(k>>1)]>>4;if(nibble>=8)nibble-=16;
   const std::int64_t value=(nibble*scale*2048+c1*history1+c2*history2+1024)>>11;
   const int sample=value<-32768?-32768:value>32767?32767:int(value);
   out[std::size_t(i)*stride]=std::int16_t(sample);history2=history1;history1=sample;
  }
 }
}
// MADP (tools/cook_menu_movies.py cook_audio): a movie's original DSP ADPCM.
// Header (32 bytes, little-endian): "MADP", u32 version 1, u16 channels,
// u16 0, u32 rate, u32 samples, u32 packets, u32 data offset (32), u32 0.
// Then each THP audio packet: u16 samples, u16 bytes per channel; per channel
// 16 s16 coefficients, s16 history1, s16 history2; per channel its bytes.
inline constexpr unsigned MadpHeaderBytes=32,MadpChannelHeaderBytes=36;
inline unsigned madpPacketBytes(unsigned channels,unsigned bytesPerChannel){return 4+channels*(MadpChannelHeaderBytes+bytesPerChannel);}
}
