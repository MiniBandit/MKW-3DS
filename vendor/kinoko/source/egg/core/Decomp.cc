#include "Decomp.hh"

#include <cstring>

namespace Kinoko::EGG::Decomp {

/// @addr{0x8021997C}
s32 GetExpandSize(const u8 *src) {
    if (src[0] == 'Y' && src[1] == 'a' && src[2] == 'z') {
        return form<s32>(&src[4]);
    }

    return -1;
}

s32 DecodeSZS(const u8 *src, u8 *dst) {
    if (!fastDecode) {
        return DecodeSZSReference(src, dst);
    }

    const s32 expandSize = GetExpandSize(src);
    const u8 *in = src + 0x10;
    u8 *out = dst;
    u8 *const end = dst + expandSize;

    while (out < end) {
        const u32 code = *in++;

        // Eight literals.
        if (code == 0xff && end - out >= 8) {
            std::memcpy(out, in, 8);
            out += 8;
            in += 8;
            continue;
        }

        for (u32 mask = 0x80; mask && out < end; mask >>= 1) {
            if (code & mask) {
                *out++ = *in++;
                continue;
            }

            const u32 pair = (u32(in[0]) << 8) | in[1];
            in += 2;
            const u32 length = (pair >> 12) ? (pair >> 12) + 2 : u32(*in++) + 0x12;
            const u32 distance = (pair & 0xfff) + 1;
            if (length > u32(end - out)) {
                PANIC("Malformed compressed SZS data.");
            }

            const u8 *from = out - distance;
            // 8-byte steps read only bytes already written when the source is
            // 8 or more bytes back; the overshoot (< 8 bytes) is rewritten next.
            if (distance >= 8 && u32(end - out) >= length + 8) {
                for (u32 i = 0; i < length; i += 8) {
                    std::memcpy(out + i, from + i, 8);
                }
            } else if (distance == 1) {
                std::memset(out, *from, length);
            } else {
                for (u32 i = 0; i < length; ++i) {
                    out[i] = from[i];
                }
            }
            out += length;
        }
    }

    return expandSize;
}

/// @brief Performs YAZ0 decompression on a given buffer.
/// @return The size of the decompressed data.
/// @addr{0x80218C2C}
s32 DecodeSZSReference(const u8 *src, u8 *dst) {
    s32 expandSize = GetExpandSize(src);
    s32 srcIdx = 0x10;
    u8 code = 0;

    u8 byte;

    for (s32 destIdx = 0; destIdx < expandSize; code >>= 1) {
        if (!code) {
            code = 0x80;
            byte = src[srcIdx++];
        }

        // Direct copy (code bit = 1)
        if (byte & code) {
            dst[destIdx++] = src[srcIdx++];
        }
        // RLE compressed data (code bit = 0)
        else {
            // Lower nibble of byte1 + byte2
            s32 distToDest = (src[srcIdx] << 8) | src[srcIdx + 1];
            srcIdx += sizeof(u8) * 2;
            s32 runSrcIdx = destIdx - (distToDest & 0xfff);

            // Upper nibble of byte 1
            s32 runLen = ((distToDest >> 12) == 0) ? src[srcIdx++] + 0x12 : (distToDest >> 12) + 2;

            for (; runLen > 0; runLen--, destIdx++, runSrcIdx++) {
                if (destIdx >= expandSize) {
                    PANIC("Malformed compressed SZS data.");
                }

                dst[destIdx] = dst[runSrcIdx - 1];
            }
        }
    }
    return expandSize;
}

} // namespace Kinoko::EGG::Decomp
