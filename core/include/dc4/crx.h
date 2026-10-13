// CRX (CRXG) image decoder for CIRCUS adv32 (D.C.4).
// Spec per docs/findings.md; validated against the game's own assets.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dc4 {

struct CrxInfo {
    int16_t offset_x = 0, offset_y = 0;
    uint16_t width = 0, height = 0;
    uint16_t compression = 0;   // 1 = LZ77, 2/3 = zlib
    uint16_t flags = 0;         // bit 0x10: int32 compressed-size field present
    uint16_t colors = 0;        // 0 = 24bpp, 1 = 32bpp, else 8bpp palette (0x102 = RGBA pal)
    uint16_t mode = 0;          // alpha post-processing selector
    int bpp = 0;                // 8, 24 or 32
};

// Decodes a CRXG image to its native pixel buffer: stride*height bytes in the
// file's own channel order, plus the 256-entry RGBA palette for 8bpp images.
// This is the level CRXD differential blending operates on.
bool crx_decode_raw(const uint8_t* data, size_t size, std::vector<uint8_t>* pixels,
                    CrxInfo* info, int* stride, std::vector<uint8_t>* palette = nullptr,
                    std::string* err = nullptr);

// Converts a raw buffer produced by crx_decode_raw into tightly-packed RGBA.
void crx_to_rgba(const CrxInfo& info, int stride, const std::vector<uint8_t>& pixels,
                 const std::vector<uint8_t>& palette, std::vector<uint8_t>* rgba);

// Decodes a CRXG image to tightly-packed RGBA (32-bit) output.
// Returns false and fills *err on failure.
bool crx_decode(const uint8_t* data, size_t size,
                std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err = nullptr);

}  // namespace dc4
