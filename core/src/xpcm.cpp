#include "dc4/xpcm.h"

#include <cstring>

namespace dc4 {

static uint32_t rd_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
static uint16_t rd_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

bool parse_xpcm(const uint8_t* data, size_t size, XpcmInfo* out) {
    if (size < 16 || std::memcmp(data, "XPCM", 4) != 0) return false;
    std::memset(out, 0, sizeof(*out));
    const uint32_t f4 = rd_u32(data + 4);
    const uint32_t f8 = rd_u32(data + 8);

    if (size >= 28 && f8 == 0) {
        // Voice variant: "XPCM" | data_size | 0 | WAVEFORMATEX(16)
        XpcmInfo i{};
        i.data_size = f4;
        i.has_wavefmt = true;
        i.format_tag = rd_u16(data + 12);
        i.channels = rd_u16(data + 14);
        i.sample_rate = rd_u32(data + 16);
        i.avg_bytes_sec = rd_u32(data + 20);
        i.block_align = rd_u16(data + 24);
        i.bits = rd_u16(data + 26);
        i.header_size = 28;
        i.decoded_pcm_size = f4;
        i.codec = i.format_tag == 1 ? XpcmCodec::Pcm
                  : i.format_tag == 2 ? XpcmCodec::Adpcm
                  : XpcmCodec::Unknown;
        *out = i;
        return true;
    }

    // Music variant: "XPCM" | decoded_pcm_size | (u16 codec, u16 ch?) | data_len | ogg
    XpcmInfo i{};
    i.decoded_pcm_size = f4;
    i.format_tag = uint16_t(f8 & 0xFFFF);
    i.channels = uint16_t(f8 >> 16);
    i.data_size = rd_u32(data + 12);
    i.header_size = 16;
    i.codec = i.format_tag == 0x0505 ? XpcmCodec::Vorbis : XpcmCodec::Unknown;
    i.sample_rate = 44100;  // vorbis headers carry the real rate; engine BGM is 44.1k
    *out = i;
    return true;
}

bool xpcm_payload(const uint8_t* data, size_t size, std::vector<uint8_t>* out) {
    XpcmInfo i;
    if (!parse_xpcm(data, size, &i)) return false;
    if (size < i.header_size + i.data_size) return false;
    out->assign(data + i.header_size, data + i.header_size + i.data_size);
    return true;
}

}  // namespace dc4
