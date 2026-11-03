#include <mkw/snd_short_loop.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace mkw::snd;
static std::vector<u8> read(const char* path) {
 std::ifstream f(path, std::ios::binary); assert(f);
 return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
 assert(argc == 4);
 Archive archive; assert(archive.load(read(argv[1])));
 const auto raw = read(argv[2]);
 FILE* out = std::fopen(argv[3], "wb"); assert(out);
 unsigned count = 0, totalBytes = 0;
 for (u32 file = 0; file < archive.fileCount(); ++file) {
  const auto* f = archive.file(file);
  for (u32 index = 0; index < f->waveCount; ++index) {
   const auto* entry = archive.wave(file, index);
   WaveData w;
   assert(size_t(f->waveOffset) + entry->offset + entry->size <= raw.size());
   assert(parseRwav(raw.data() + f->waveOffset + entry->offset, entry->size, w, true));
   if (!shortAdpcmLoop(w)) continue;
   for (int channel = 0; channel < w.channels; ++channel) {
    std::vector<s16> pcm(shortLoopSamples(w), 123);
    assert(decodeShortLoop(w, channel, pcm.data()));
    const u32 header[] = {file, index, u32(channel), w.loopStart, w.loopEnd};
    assert(std::fwrite(header, sizeof header, 1, out) == 1);
    assert(std::fwrite(pcm.data(), 2, w.loopEnd, out) == w.loopEnd);
    assert(std::fwrite(pcm.data() + shortLoopOffset(w), 2, w.loopEnd - w.loopStart, out) == w.loopEnd - w.loopStart);
    // Every period is immutable and starts at the exact original loop
    // context; NDSP repeats it without a CPU refill or extra samples.
    assert(shortLoopOffset(w) % 16 == 0 && shortLoopSamples(w) % 16 == 0);
    totalBytes += shortLoopSamples(w) * 2;
    std::printf("file=%u wave=%u channel=%d initial=%u loop=%u bytes=%u\n",
                file, index, channel, w.loopEnd, w.loopEnd - w.loopStart, shortLoopSamples(w) * 2);
    WaveData invalid = w; invalid.channel[channel].bytes = 0;
    assert(!decodeShortLoop(invalid, channel, pcm.data()));
    invalid = w; invalid.loopStart = invalid.loopEnd;
    assert(!shortAdpcmLoop(invalid));
    invalid = w; invalid.streamed = true; assert(!shortAdpcmLoop(invalid));
    ++count;
   }
  }
 }
 assert(count == 14 && totalBytes == 29312);
 std::fclose(out);
 std::printf("retail short loops=%u total_pool_bytes=%u\n", count, totalBytes);
}
