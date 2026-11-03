#pragma once
// Short, non-frame-aligned ADPCM oscillator loops cannot be sustained by
// refilling NDSP wave buffers once per DSP frame. Decode their first pass
// and one exact loop period to PCM16, which NDSP can loop autonomously.
#include <mkw/snd_engine.hpp>
#include <algorithm>

namespace mkw::snd {
// Includes SE_RC_LAP's 340-sample oscillator: at key 81 a period is
// only 4.58 ms, below a DSP frame and unsafe for CPU bridge refills.
inline bool shortAdpcmLoop(const WaveData& w) {
 return w.format == 2 && w.loop && !w.streamed && w.loopStart % 14 &&
        w.loopStart < w.loopEnd && w.loopEnd <= 4096 &&
        w.loopEnd - w.loopStart <= 384;
}
inline bool decodeAdpcmRange(const WaveData::Channel& c, u32 first, u32 end,
                            u16 ps, s16 yn1, s16 yn2, s16* pcm) {
 if (!c.data || first > end || (end + 13) / 14 * 8 > c.bytes) return false;
 for (u32 at = first; at < end; ++at) {
  const u32 frame = at / 14 * 8, sample = at % 14;
  if (!sample) ps = c.data[frame];
  const unsigned predictor = ps >> 4;
  if (predictor >= 8) return false;
  const u8 packed = c.data[frame + 1 + sample / 2];
  const int nibble = sample & 1 ? packed & 15 : packed >> 4;
  const int value = nibble >= 8 ? nibble - 16 : nibble;
  // Multiplication avoids shifting a negative signed value.
  const int sum = value * (1 << (ps & 15)) * 2048 + 1024 +
                  c.adpcm.coef[predictor * 2] * yn1 + c.adpcm.coef[predictor * 2 + 1] * yn2;
  const s16 out = s16(std::clamp(sum >> 11, -32768, 32767));
  *pcm++ = out; yn2 = yn1; yn1 = out;
 }
 return true;
}
// Each PCM buffer begins on a 32-byte boundary in the persistent DSP pool.
inline u32 shortLoopOffset(const WaveData& w) { return (w.loopEnd + 15) & ~15u; }
inline u32 shortLoopSamples(const WaveData& w) {
 return (shortLoopOffset(w) + w.loopEnd - w.loopStart + 15) & ~15u;
}
inline bool decodeShortLoop(const WaveData& w, int channel, s16* pcm) {
 if (!shortAdpcmLoop(w) || channel < 0 || channel >= w.channels) return false;
 const auto& c = w.channel[channel];
 return decodeAdpcmRange(c, 0, w.loopEnd, c.adpcm.ps, c.adpcm.yn1, c.adpcm.yn2, pcm) &&
        decodeAdpcmRange(c, w.loopStart, w.loopEnd, c.loopParam.ps,
                         c.loopParam.yn1, c.loopParam.yn2, pcm + shortLoopOffset(w));
}
}
