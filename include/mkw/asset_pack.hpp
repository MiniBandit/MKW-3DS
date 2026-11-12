#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace mkw {
// Indexed archive of menu runtime assets (tools/pack_menu_assets.py). One
// handle stays open, so each asset costs a seek and a read rather than a file
// open and close. Paths not in the archive report false; callers then use the
// loose file. Thread-safe: the asset loader and main thread may both read.
namespace pack {
// Opens menu.pak, race.pak and stream.pak once per session. The first call
// also lets openFile serve fopen: make it once the asset folder is current.
void open();
void close();
bool contains(const char* path);
// Reads the whole asset into out (resized). False if absent or unreadable.
bool read(const char* path,std::vector<uint8_t>& out);
// A packed file as a read-only FILE (fopen calls this first:
// linear_lock.cpp __wrap_fopen), or null if no pack holds it. Small and
// compressed entries are read whole into RAM; large stored ones read from
// their pack on their own handle. Unbuffered: reads reach the pack whole.
std::FILE* openFile(const char* path);
// An LZ4 block (tools/pakpack.c) into exactly size bytes; false if malformed.
bool decodeLz4(const unsigned char* in,size_t n,unsigned char* out,size_t size);
// Size of an asset, or -1 if absent.
long size(const char* path);
// Reads exactly `bytes` (must equal size(path)) into destination.
bool readInto(const char* path,void* destination,size_t bytes);
// Why the last failed read failed ("lz4 buffer allocation", "direct short read"...).
extern const char* lastReadFailure;
// Reads `bytes` starting `offset` bytes into the asset; false past its end.
bool readRange(const char* path,size_t offset,void* destination,size_t bytes);
// An entry as stored in the pack: LZ4 block or raw bytes (-1 if absent).
long storedSize(const char* path);
bool readStored(const char* path,void* destination,size_t bytes);
// Decodes an LZ4 block of `size` bytes keeping only its first `head` bytes
// (into header) and every stride-th frame of frameBytes after them, up to
// count frames (into out, packed). Memory: a 64 KiB window, not `size`.
bool decodeLz4Frames(const unsigned char* in,size_t n,size_t size,unsigned char* header,size_t head,
                     size_t frameBytes,unsigned stride,unsigned count,unsigned char* out);
// The same, reading the pack entry `path` (an LZ4 block) 64 KiB at a time.
bool decodeLz4Frames(const char* path,size_t size,unsigned char* header,size_t head,
                     size_t frameBytes,unsigned stride,unsigned count,unsigned char* out);
// Race-load RAM cache (asset_pack.cpp): the races of a VS set or Grand Prix
// re-read the same racers, items, effects and pause menu textures. enableCache sets the most it
// may keep (0: off; cacheLimitFor gives it for a heap size). While
// cacheReads(true), whole pack entries read are kept; kept entries are
// always served from RAM. trimCache, after a load, drops entries that load
// didn't read and returns the bytes kept; dropCache frees them all.
void enableCache(size_t limit);
size_t cacheLimitFor(size_t heapBytes);
void cacheReads(bool on);
size_t trimCache();
void dropCache();
#ifndef MKW_RELEASE
// DIAG: SD read speed test on race.pak (read-bench.txt). Writes one line per
// test to resultPath and passes each line to log. Mode 1: chunk sizes,
// threads, LZ11 decode. Mode 2: sector alignment, file depth, real entries.
void readBench(const char* resultPath,void(*log)(const char*),int mode);
// DIAG: with pack-trace.txt, a "#name" line in pack-trace.csv after a load
// stage's reads (tools/pak_order.py skips it).
void traceMark(const char* name);
#endif
}
}
