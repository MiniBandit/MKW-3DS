// SPDX-License-Identifier: LicenseRef-MKW3DS-Online
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-ONLINE-LICENSE.txt.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkw {
// Ghost sharing: Yaz0-family compression as the Wii's ghosts use ("Yaz1"
// magic), so a ghost fits a QR code.
std::vector<uint8_t> yazCompress(const uint8_t* data,size_t size,const char magic[4]);
}
