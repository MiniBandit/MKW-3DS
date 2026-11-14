// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#pragma once
#include <cstdint>
namespace mkw {
// The one-CIA install. The full CIA carries the game files in its RomFS and
// the game runs from there. A quick update CIA is the same title, so
// installing one replaces this RomFS; the update then reads the game files
// from the SD card. So on its first start the full CIA copies its game files
// to the card (sdmc:/3ds/mkw-3ds), once, with a progress screen.
//
// Only adds and replaces game files: saves, ghosts and settings in the same
// folder are never touched. content.txt is written last, so a copy that was
// interrupted (power, HOME > Close, SD card out) resumes on the next start;
// unpack.txt names the content being copied, so only files of that same
// content are kept when resuming (an older version's files are replaced).
enum class UnpackResult {
    Copied,       // the SD card now has a matching copy
    NotNeeded,    // it already had one
    NoSpace,      // not enough free space; the game runs from the CIA
    Failed,       // a file could not be read or written; runs from the CIA
    Interrupted,  // the system asked the game to close
};
// romfs: the mounted full-CIA RomFS root ("romfs:/"); sd: the SD folder;
// content: the content stamp both must carry (MKW_CONTENT); version for the screen.
// Uses the top screen's text console and resets the 2D graphics afterwards
// (gfxExit + gfxInitDefault), so call it before the renderer starts.
UnpackResult unpackGameFiles(const char* romfs,const char* sd,const char* content,const char* version);
}
