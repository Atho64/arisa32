#include "dc4/crx.h"

#include <cstring>

extern "C" {
#include "miniz.h"
}

namespace dc4 {

namespace {

struct Rd {
    const uint8_t* d;
    size_t n, p = 0;
    bool ok = true;
    uint8_t u8() {
        if (p >= n) { ok = false; return 0; }
        return d[p++];
    }
    uint16_t u16() {
        if (p + 2 > n) { ok = false; return 0; }
        uint16_t v = uint16_t(d[p] | (d[p + 1] << 8));
        p += 2;
        return v;
    }
    uint32_t u32() {
        if (p + 4 > n) { ok = false; return 0; }
        uint32_t v;
        memcpy(&v, d + p, 4);
        p += 4;
        return v;
    }
};

// Compression 1: custom LZ77 with a 64 KB ring window (per GARbro spec).
void unpack_v1(Rd& s, uint8_t* out, size_t out_len) {
    std::vector<uint8_t> window(0x10000, 0);
    uint32_t flag = 0, win_pos = 0;
    size_t dst = 0;
    while (dst < out_len && s.ok) {
        flag >>= 1;
        if (!(flag & 0x100)) flag = s.u8() | 0xff00;
        if (flag & 1) {
            uint8_t dat = s.u8();
            window[win_pos] = dat;
            win_pos = (win_pos + 1) & 0xffff;
            out[dst++] = dat;
        } else {
            uint8_t control = s.u8();
            uint32_t offset, count;
            if (control >= 0xC0) {
                offset = (uint32_t(control & 3) << 8) | s.u8();
                count = 4 + ((control >> 2) & 0xF);
            } else if (control & 0x80) {
                offset = control & 0x1F;
                count = 2 + ((control >> 5) & 3);
                if (offset == 0) offset = s.u8();
            } else if (control == 0x7F) {
                count = 2 + s.u16();
                offset = s.u16();
            } else {
                offset = s.u16();
                count = control + 4;
            }
            offset = (win_pos + 0x10000 - offset) & 0xFFFF;
            for (uint32_t k = 0; k < count && dst < out_len; ++k) {
                uint8_t dat = window[offset & 0xFFFF];
                offset++;
                window[win_pos] = dat;
                win_pos = (win_pos + 1) & 0xffff;
                out[dst++] = dat;
            }
        }
    }
}

// Compression 2/3: zlib stream + per-row filters (>=24bpp).
void unpack_v2(const uint8_t* raw, size_t raw_len, uint8_t* out,
               int w, int h, int stride, int bpp) {
    int pixel_size = bpp / 8;
    size_t src_stride = size_t(w) * pixel_size;
    Rd s{raw, raw_len, 0, true};
    if (bpp >= 24) {
        std::vector<uint8_t> zero_row(size_t(src_stride) + pixel_size, 0);
        for (int y = 0; y < h; ++y) {
            if (!s.ok) return;
            uint8_t ctl = s.u8();
            size_t dst = size_t(y) * stride;
            // Engine reads the previous row even on y=0 (wraps into untouched
            // zero memory); emulate with an explicit zero row.
            const uint8_t* prev = y ? out + dst - size_t(stride) : zero_row.data();
            switch (ctl) {
            case 0:  // horizontal delta
                for (int x = 0; x < src_stride; ++x) {
                    uint8_t v = s.u8();
                    out[dst + x] = x < pixel_size ? v : uint8_t(v + out[dst + x - pixel_size]);
                }
                break;
            case 1:  // vertical delta
                for (size_t x = 0; x < src_stride; ++x)
                    out[dst + x] = uint8_t(s.u8() + prev[x]);
                break;
            case 2:  // diagonal delta
                for (int x = 0; x < src_stride; ++x) {
                    uint8_t v = s.u8();
                    out[dst + x] = x < pixel_size ? v
                                                  : uint8_t(v + prev[x - pixel_size]);
                }
                break;
            case 3: {  // right-to-left vertical delta
                size_t d = dst;
                const uint8_t* pv = prev;
                for (size_t x = 0; x + pixel_size < src_stride; ++x) {
                    out[d] = uint8_t(s.u8() + pv[pixel_size]);
                    ++d; ++pv;
                }
                for (int x = 0; x < pixel_size; ++x) out[dst + src_stride - pixel_size + x] = s.u8();
                break;
            }
            case 4:  // RLE per channel
                for (int ch = 0; ch < pixel_size; ++ch) {
                    size_t d = dst + ch;
                    int wd = w;
                    uint8_t val = s.u8();
                    while (wd > 0 && s.ok) {
                        out[d] = val;
                        d += pixel_size;
                        wd--;
                        if (wd == 0) break;
                        uint8_t next = s.u8();
                        if (val == next) {
                            uint8_t count = s.u8();
                            for (int j = 0; j < count; ++j) {
                                out[d] = val;
                                d += pixel_size;
                            }
                            wd -= count;
                            if (wd > 0) val = s.u8();
                        } else {
                            val = next;
                        }
                    }
                }
                break;
            default:  // row left as-is
                break;
            }
        }
    } else {  // 8bpp raw rows
        size_t dst = 0;
        for (int y = 0; y < h; ++y) {
            if (!s.ok) return;
            memcpy(out + dst, raw + s.p, src_stride);
            s.p += src_stride;
            dst += size_t(stride);
        }
    }
}

}  // namespace

bool crx_decode_raw(const uint8_t* data, size_t size, std::vector<uint8_t>* pixels,
                    CrxInfo* info, int* stride_out, std::vector<uint8_t>* palette_out,
                    std::string* err) {
    if (size < 0x14 || memcmp(data, "CRXG", 4) != 0) {
        if (err) *err = "not CRXG";
        return false;
    }
    CrxInfo i;
    memcpy(&i.offset_x, data + 4, 2);
    memcpy(&i.offset_y, data + 6, 2);
    memcpy(&i.width, data + 8, 2);
    memcpy(&i.height, data + 10, 2);
    memcpy(&i.compression, data + 12, 2);
    memcpy(&i.flags, data + 14, 2);
    memcpy(&i.colors, data + 16, 2);
    memcpy(&i.mode, data + 18, 2);
    i.bpp = i.colors == 0 ? 24 : i.colors == 1 ? 32 : 8;
    if (i.compression < 1 || i.compression > 3) {
        if (err) *err = "bad compression";
        return false;
    }
    *info = i;

    const int stride = (i.width * i.bpp / 8 + 3) & ~3;
    if (stride_out) *stride_out = stride;
    pixels->assign(size_t(stride) * i.height, 0);
    std::vector<uint8_t> palette;
    Rd s{data, size, 0x14, true};

    if (i.bpp == 8) {
        int n = i.colors > 0x100 ? 0x100 : i.colors;
        int csz = i.colors == 0x102 ? 4 : 3;
        palette.resize(size_t(n) * 4, 0);
        for (int k = 0; k < n; ++k) {
            uint8_t r = s.u8(), g = s.u8(), b = s.u8();
            if (csz == 4) s.u8();
            if (r == 0xFF && g == 0 && b == 0xFF) g = 0xFF;  // magenta transparency quirk
            palette[k * 4] = r;
            palette[k * 4 + 1] = g;
            palette[k * 4 + 2] = b;
            palette[k * 4 + 3] = 0xFF;
        }
    }
    if (i.compression >= 3) {
        uint32_t count = s.u32();
        s.p += size_t(count) * 0x10;
    }
    if (i.flags & 0x10) s.u32();

    if (i.compression == 1) {
        unpack_v1(s, pixels->data(), pixels->size());
    } else {
        // Inflated stream can exceed h*stride (filter ctl bytes, RLE headroom).
        std::vector<uint8_t> inflated(size_t(i.height) * (size_t(stride) * 2 + 16) + 4096, 0);
        uLongf dlen = uLongf(inflated.size());
        if (uncompress(inflated.data(), &dlen, data + s.p, uLong(size - s.p)) != Z_OK) {
            if (err) *err = "zlib uncompress failed";
            return false;
        }
        inflated.resize(dlen);
        unpack_v2(inflated.data(), dlen, pixels->data(), i.width, i.height, stride, i.bpp);
    }
    if (palette_out) *palette_out = palette;
    return true;
}

void crx_to_rgba(const CrxInfo& info, int stride, const std::vector<uint8_t>& pixels,
                 const std::vector<uint8_t>& palette, std::vector<uint8_t>* rgba) {
    rgba->assign(size_t(info.width) * info.height * 4, 0);
    for (uint32_t y = 0; y < info.height; ++y) {
        for (uint32_t x = 0; x < info.width; ++x) {
            uint8_t* o = &(*rgba)[(size_t(y) * info.width + x) * 4];
            if (info.bpp == 8) {
                uint8_t idx = pixels[size_t(y) * stride + x];
                memcpy(o, &palette[size_t(idx) * 4], 4);
            } else if (info.bpp == 24) {
                const uint8_t* p = &pixels[size_t(y) * stride + x * 3];
                o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 0xFF;  // BGR -> RGBA
            } else {
                const uint8_t* p = &pixels[size_t(y) * stride + x * 4];
                if (info.mode != 1) {
                    uint8_t a = p[0], b = p[1], g = p[2], r = p[3];
                    uint8_t flip = (info.mode == 2) ? 0x00 : 0xFF;
                    o[0] = r; o[1] = g; o[2] = b; o[3] = uint8_t(a ^ flip);
                } else {
                    o[0] = p[3]; o[1] = p[2]; o[2] = p[1]; o[3] = p[0];
                }
            }
        }
    }
}

bool crx_decode(const uint8_t* data, size_t size,
                std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err) {
    std::vector<uint8_t> pixels, palette;
    int stride = 0;
    if (!crx_decode_raw(data, size, &pixels, info, &stride, &palette, err)) return false;
    crx_to_rgba(*info, stride, pixels, palette, rgba);
    return true;
}

}  // namespace dc4
