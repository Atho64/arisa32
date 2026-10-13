// XPCM audio header (CIRCUS adv32, D.C.4)
// Two variants recovered from DC4.EXE (Audio_OpenAndDecode 0x452A40):
//  - VOICE (28-byte header): "XPCM" + u32 data_size + u32 0 + WAVEFORMATEX(16) + PCM16
//  - MUSIC (16-byte header): "XPCM" + u32 decoded_pcm_size + u32 0x00000505
//    + u32 data_len + raw Ogg Vorbis stream
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dc4 {

enum class XpcmCodec { Unknown, Pcm, Adpcm, Vorbis };

struct XpcmInfo {
    uint32_t data_size = 0;    // payload bytes after the header
    uint16_t format_tag = 0;   // 1 = PCM, 5 = vorbis (music variant: tag from u32@8 low)
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t avg_bytes_sec = 0;
    uint16_t block_align = 0;
    uint16_t bits = 0;
    size_t header_size = 0;    // 16 (music) or 28 (voice/wavefmt)
    XpcmCodec codec = XpcmCodec::Unknown;
    bool has_wavefmt = false;
    uint32_t decoded_pcm_size = 0;  // music variant: u32@4
};

// Parses the header of a blob starting with "XPCM". Returns false if not XPCM.
bool parse_xpcm(const uint8_t* data, size_t size, XpcmInfo* out);

// Extracts the payload (ogg stream for music, raw PCM for voice).
bool xpcm_payload(const uint8_t* data, size_t size, std::vector<uint8_t>* out);

}  // namespace dc4
