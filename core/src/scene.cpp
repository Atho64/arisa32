#include "dc4/scene.h"
#include "dc4/resource.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dc4scene {

using namespace dc4;

SjisDecoder g_sjis_decoder = nullptr;

std::u16string sjis_to_utf16(const std::string& s) {
    if (prefers_utf8_text()) {
        std::u16string decoded;
        if (decode_utf8(s, &decoded)) return decoded;
    }
    if (g_sjis_decoder) {
        std::u16string w = g_sjis_decoder(s);
        if (is_latin()) {
            for (char16_t& ch : w) {
                if (ch == u'\u3000') ch = u' ';
            }
        }
        return w;
    }
#ifdef _WIN32
    if (s.empty()) return std::u16string();
    int wlen = MultiByteToWideChar(932, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::u16string w(size_t(wlen), 0);
    MultiByteToWideChar(932, 0, s.c_str(), int(s.size()),
                        reinterpret_cast<wchar_t*>(&w[0]), wlen);
    if (is_latin()) {
        for (char16_t& ch : w) {
            if (ch == u'\u3000') ch = u' ';
        }
    }
    return w;
#else
    // Fallback: lead byte + trail byte from the Shift-JIS table. Enough for
    // kana and the common kanji ranges; platforms can override with a real
    // converter.
    std::u16string w;
    w.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            w.push_back(char16_t(c));
            ++i;
        } else if (c >= 0xA1 && c <= 0xDF) {
            w.push_back(char16_t(0xFF61 + (c - 0xA1)));  // halfwidth katakana
            ++i;
        } else if (i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            unsigned int code = ((c - 0x81) * 188) + (d < 0x7F ? d - 0x40
                                  : d < 0x9F ? d - 0x41 : d - 0x9E);
            // JIS X 0208 -> UTF-16 for the kana block; kanji need a table.
            if (code == 0) w.push_back(is_latin() ? u' ' : 0x3000);
            else if (code < 0x1E00) w.push_back(char16_t(0x3040 + code));
            else w.push_back(0x25A1);
            i += 2;
        } else {
            ++i;
        }
    }
    if (is_latin()) {
        for (char16_t& ch : w) {
            if (ch == u'\u3000') ch = u' ';
        }
    }
    return w;
#endif
}

// ---------------------------------------------------------------------------

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = fopen(resolve_asset(path).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->assign(size_t(sz > 0 ? sz : 0), 0);
    bool ok = sz <= 0 || fread(out->data(), 1, out->size(), f) == out->size();
    fclose(f);
    return ok;
}

// Pixel-centred bilinear sampling, matching the original D3D texture filtering.
void blit(std::vector<uint8_t>* dst, int dst_w, int dst_h, int dx, int dy,
          const Image& src, float scale, int alpha) {
    if (!src.valid() || alpha <= 0 || scale <= 0) return;
    int sw = int(float(src.w) * scale), sh = int(float(src.h) * scale);
    if (sw <= 0 || sh <= 0) return;
    int x0 = std::max(dx, 0), y0 = std::max(dy, 0);
    int x1 = std::min(dx + sw, dst_w), y1 = std::min(dy + sh, dst_h);
    // UI sheets and unscaled sprites need no resampling. This path is also
    // pixel-identical to the filtered path at integer, unit-scale positions.
    if (scale == 1.0f) {
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const uint8_t* s = &src.rgba[(size_t(y - dy) * src.w + x - dx) * 4];
            const int sa = int(s[3]) * alpha / 255;
            if (!sa) continue;
            uint8_t* d = &(*dst)[(size_t(y) * dst_w + x) * 4];
            for (int k = 0; k < 3; ++k) d[k] = uint8_t((s[k] * sa + d[k] * (255 - sa)) / 255);
            d[3] = uint8_t(sa + int(d[3]) * (255 - sa) / 255);
        }
        return;
    }
    const float inv = float(src.w) / float(sw);
    const float invy = float(src.h) / float(sh);
    for (int y = y0; y < y1; ++y) {
        uint8_t* drow = &(*dst)[size_t(y) * dst_w * 4];
        for (int x = x0; x < x1; ++x) {
            const float u = std::clamp((x - dx + 0.5f) * inv - 0.5f, 0.0f, float(src.w - 1));
            const float v = std::clamp((y - dy + 0.5f) * invy - 0.5f, 0.0f, float(src.h - 1));
            const int ix = int(u), iy = int(v);
            const float fx = u - ix, fy = v - iy;
            uint8_t sample[4];
            const uint8_t* p = &src.rgba[(size_t(iy) * src.w + ix) * 4];
            const uint8_t* q = &src.rgba[(size_t(iy) * src.w + std::min(ix + 1, src.w - 1)) * 4];
            const uint8_t* r = &src.rgba[(size_t(std::min(iy + 1, src.h - 1)) * src.w + ix) * 4];
            const uint8_t* t = &src.rgba[(size_t(std::min(iy + 1, src.h - 1)) * src.w + std::min(ix + 1, src.w - 1)) * 4];
            for (int k = 0; k < 4; ++k)
                sample[k] = uint8_t(std::lround((p[k] * (1 - fx) + q[k] * fx) * (1 - fy)
                                             + (r[k] * (1 - fx) + t[k] * fx) * fy));
            const uint8_t* s = sample;
            uint8_t sa = uint8_t(int(s[3]) * alpha / 255);
            if (sa == 0) continue;
            uint8_t* d = &drow[size_t(x) * 4];
            if (sa == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            } else {
                int ia = 255 - sa;
                d[0] = uint8_t((s[0] * sa + d[0] * ia) / 255);
                d[1] = uint8_t((s[1] * sa + d[1] * ia) / 255);
                d[2] = uint8_t((s[2] * sa + d[2] * ia) / 255);
                d[3] = uint8_t(sa + int(d[3]) * ia / 255);
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------

bool FontRenderer::load(const std::string& path, int font_index, float pixel_size) {
    if (!read_file(path, &font_data_)) return false;
    int offset = stbtt_GetFontOffsetForIndex(font_data_.data(), font_index);
    if (offset < 0) return false;
    stbtt_fontinfo* f = new stbtt_fontinfo();
    if (!stbtt_InitFont(f, font_data_.data(), offset)) {
        delete f;
        return false;
    }
    delete static_cast<stbtt_fontinfo*>(font_);
    font_ = f;
    glyphs_.clear();
#ifdef _WIN32
    for (auto& item : native_fonts_) DeleteObject(static_cast<HFONT>(item.second));
    native_fonts_.clear();
    if (native_dc_) DeleteDC(static_cast<HDC>(native_dc_));
    native_dc_ = nullptr;
    native_family_.clear();
    // Use the same GDI rasterizer as DC4 for the bundled Windows MS Gothic.
    // Arbitrary caller-supplied fonts retain the portable stb path.
    std::string filename = std::filesystem::path(path).filename().string();
    for (char& ch : filename) ch = char(std::tolower((unsigned char)ch));
    if (filename == "msgothic.ttc") {
        native_family_ = u"\uff2d\uff33 \u30b4\u30b7\u30c3\u30af";
        native_dc_ = CreateCompatibleDC(nullptr);
    }
#endif
    pixel_size_ = pixel_size;
    scale_ = stbtt_ScaleForPixelHeight(f, pixel_size);
    return true;
}

FontRenderer::~FontRenderer() {
    delete static_cast<stbtt_fontinfo*>(font_);
#ifdef _WIN32
    for (auto& item : native_fonts_) DeleteObject(static_cast<HFONT>(item.second));
    if (native_dc_) DeleteDC(static_cast<HDC>(native_dc_));
#endif
}

const FontRenderer::Glyph& FontRenderer::glyph(char16_t ch, int size, bool wide) const {
    const int font_key = size + (wide ? 65536 : 0);
    auto key = std::make_pair(font_key, ch);
    auto found = glyphs_.find(key);
    if (found != glyphs_.end()) return found->second;
    Glyph g;
#ifdef _WIN32
    if (native_dc_) {
        auto& handle = native_fonts_[font_key];
        if (!handle) handle = CreateFontW(size, wide ? size * 2 : 0, 0, 0, FW_NORMAL, 0, 0, 0,
            SHIFTJIS_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
            DEFAULT_PITCH | FF_MODERN, reinterpret_cast<const wchar_t*>(native_family_.c_str()));
        HDC dc = static_cast<HDC>(native_dc_);
        HGDIOBJ old = SelectObject(dc, handle);
        MAT2 matrix{};
        matrix.eM11.value = matrix.eM22.value = 1;
        GLYPHMETRICS metrics{};
        DWORD bytes = GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &matrix);
        if (bytes != GDI_ERROR && bytes > 0) {
            std::vector<uint8_t> raw(bytes);
            GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &metrics, bytes, raw.data(), &matrix);
            g.w = int(metrics.gmBlackBoxX); g.h = int(metrics.gmBlackBoxY);
            const int cell = (ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size) * (wide ? 2 : 1);
            g.x = metrics.gmptGlyphOrigin.x + (cell - metrics.gmCellIncX) / 2;
            g.y = -metrics.gmptGlyphOrigin.y;
            g.coverage.resize(size_t(g.w) * g.h);
            const int stride = (g.w + 3) & ~3;
            if (size_t(stride) * g.h > raw.size()) { g.w = g.h = 0; g.coverage.clear(); }
            for (int y = 0; y < g.h; ++y)
                for (int x = 0; x < g.w; ++x)
                    g.coverage[size_t(y) * g.w + x] = uint8_t(std::min(255, int(raw[size_t(y) * stride + x]) * 255 / 64));
        }
        SelectObject(dc, old);
        return glyphs_.emplace(key, std::move(g)).first->second;
    }
#endif
    const auto* f = static_cast<const stbtt_fontinfo*>(font_);
    // EM scaling keeps a Japanese cell at the requested width on Android.
    const float sc = stbtt_ScaleForMappingEmToPixels(f, float(size));
    const float sx = wide ? sc * 2 : sc;
    int x1, y1;
    stbtt_GetCodepointBitmapBox(f, ch, sx, sc, &g.x, &g.y, &x1, &y1);
    g.w = x1 - g.x; g.h = y1 - g.y;
    if (g.w > 0 && g.h > 0) {
        g.coverage.resize(size_t(g.w) * g.h);
        stbtt_MakeCodepointBitmap(f, g.coverage.data(), g.w, g.h, g.w, sx, sc, ch);
    }
    return glyphs_.emplace(key, std::move(g)).first->second;
}

void FontRenderer::rasterize_line(const std::u16string& text, int x0, int baseline,
                                  std::vector<uint8_t>* rgba, int buf_w,
                                  int buf_h, float pixel_size, uint32_t rgb, bool wide) const {
    if (!font_) return;
    if (!rgba || buf_w <= 0 || buf_h <= 0 || rgba->size() < size_t(buf_w) * buf_h * 4) return;
    const int ps = int(pixel_size > 0 ? pixel_size : pixel_size_);
    std::vector<uint8_t> mask(size_t(buf_w) * buf_h, 0);
    int x = x0;
    for (char16_t ch : text) {
        const Glyph& g = glyph(ch, ps, wide);
        for (int ry = 0; ry < g.h; ++ry) for (int rx = 0; rx < g.w; ++rx) {
            int px = x + g.x + rx, py = baseline + g.y + ry;
            if (px < 0 || py < 0 || px >= buf_w || py >= buf_h) continue;
            auto& value = mask[size_t(py) * buf_w + px];
            value = std::max(value, g.coverage[size_t(ry) * g.w + rx]);
        }
        x += (ch < 0x100 || ch == u'\u3000' || (ch >= 0xFF61 && ch <= 0xFF9F) ? ps / 2 : ps) * (wide ? 2 : 1);
    }
    // Build the outline from the completed coverage mask. The old per-pixel
    // outline overwrote neighbouring white strokes, making glyphs look broken.
    for (int y = 0; y < buf_h; ++y) for (int px = 0; px < buf_w; ++px) {
        const size_t i = size_t(y) * buf_w + px;
        uint8_t edge = mask[i];
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int xx = px + dx, yy = y + dy;
            if (xx >= 0 && yy >= 0 && xx < buf_w && yy < buf_h)
                edge = std::max(edge, mask[size_t(yy) * buf_w + xx]);
        }
        if (!edge) continue;
        uint8_t* d = &(*rgba)[i * 4];
        const int alpha = edge + mask[i] * (255 - edge) / 255;
        const int white = alpha ? mask[i] * 255 / alpha : 0;
        for (int k = 0; k < 3; ++k) {
            const int component = int((rgb >> (16 - 8 * k)) & 255);
            d[k] = uint8_t((white * alpha * component / 255 + d[k] * d[3] * (255 - alpha) / 255) / std::max(1, alpha + int(d[3]) * (255 - alpha) / 255));
        }
        d[3] = uint8_t(alpha + d[3] * (255 - alpha) / 255);
    }
}

// ---------------------------------------------------------------------------

SceneRunner::SceneRunner(const std::string& data_dir) : data_(data_dir), scene_audio_(data_dir) {
    frame_.assign(size_t(kScreenW) * kScreenH * 4, 0);
    set_latin_mode(dc4::is_latin());
}

void SceneRunner::set_latin_mode(bool latin) {
    latin_mode_ = latin;
    if (latin) {
        layout_.font_size = 20;
        layout_.text_w = 750;
        layout_.line_spacing = 34;
    } else {
        layout_.font_size = 22;
        layout_.text_w = 660;
        layout_.line_spacing = 36;
    }
}

SceneRunner::~SceneRunner() = default;

bool SceneRunner::replay_voice() { return scene_audio_.replay_voice(); }
bool SceneRunner::voice_playing() const { return scene_audio_.voice_playing(); }
void SceneRunner::set_window_opacity(int percent) { window_opacity_ = std::clamp(percent, 0, 100); }
void SceneRunner::set_effects_enabled(bool enabled) {
    effects_enabled_ = enabled;
    if (!enabled) finish_animations();
}
void SceneRunner::set_effect_speed(int native_0_to_11) { effect_speed_ = std::clamp(native_0_to_11, 0, 11); }

bool SceneRunner::mes_exists(const std::string& name) const {
    return asset_exists(data_ + "/MES/" + name + ".mes");
}

bool SceneRunner::load_grp_crx(const std::string& rel, Image* out,
                               std::string* err) const {
    std::vector<uint8_t> buf;
    std::string normalized = rel;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path full = std::filesystem::path(data_) / "GRP";
    for (const auto& part : std::filesystem::path(normalized)) {
        const std::string resolved = find_file_ci(full.string(), part.string());
        full /= resolved.empty() ? part : std::filesystem::path(resolved);
    }
    if (!read_file(full.string(), &buf)) {
        if (err) *err = "open " + rel;
        return false;
    }
    CrxInfo info;
    std::vector<uint8_t> rgba;
    if (!crx_decode(buf.data(), buf.size(), &rgba, &info, err)) return false;
    out->name = rel;
    out->w = info.width;
    out->h = info.height;
    out->rgba = std::move(rgba);
    return true;
}

bool SceneRunner::load_script(const std::string& mes_path, const std::string& entry_label) {
    std::string err;
    std::string resolved = resolve_asset(mes_path);
    if (!mes_.load_file(resolved, &err)) {
        last_error_ = err;
        printf("scene: %s\n", err.c_str());
        return false;
    }
    if (!vm_.bind(mes_)) { last_error_ = vm_.error(); return false; }
    if (!entry_label.empty() && !vm_.jump(entry_label)) { last_error_ = vm_.error(); return false; }
    script_name_ = std::filesystem::path(resolved).filename().string();
    last_error_.clear();
    scene_end_ = false;
    next_script_.clear();
    current_line_.clear();
    speaker_.clear();
    pending_movie_.clear();
    effect_movie_.clear();
    script_wait_pending_ = false;
    script_wait_ms_ = 0;
    credits_active_ = false;
    credits_pages_.clear();
    credits_archive_.clear();
    credits_entries_.clear();
    input_skip_label_.clear();
    // Layer/sprite state deliberately persists: the engine keeps its graphics
    // across Mes_RunScriptByName jumps (the prologue chains a1 -> a2 -> ...).
    return true;
}

bool SceneRunner::jump_to_label(const std::string& label) {
    if (!vm_.jump(label)) { last_error_ = vm_.error(); return false; }
    scene_end_ = false;
    pending_movie_.clear();
    script_wait_pending_ = false;
    credits_active_ = false;
    return true;
}

void SceneRunner::finish_script_wait() {
    if (!script_wait_pending_) return;
    if (!input_skip_label_.empty()) {
        if (!vm_.jump(input_skip_label_)) last_error_ = vm_.error();
    }
    script_wait_ms_ = 0;
    credits_active_ = false;
    scene_dirty_ = true;
}

bool SceneRunner::advance_to_next_text() {
    if (scene_end_ || vm_.waiting_for_choice() || !pending_movie_.empty() || waiting_for_time()) return false;
    last_error_.clear();
    script_wait_pending_ = false;
    finish_animations();
    scene_dirty_ = true;
    std::string prev_speaker = speaker_;
    std::string prev_line = current_line_;
    speaker_.clear();
    current_line_.clear();
    std::fill(pending_sprites_.begin(), pending_sprites_.end(), false);
    pending_scene_blit_ = false;
    MesCommand c;
    size_t instructions = 0, transfers = 0;
    while (vm_.next(&c)) {
        if (++instructions > 100000) {
            last_error_ = "scene instruction budget exhausted before a dialogue or choice";
            scene_end_ = true;
            return false;
        }
        using K = CmdKind;
        if (observer_) observer_(c, script_name_);
        if (scene_audio_.dispatch(c)) continue;
        if (c.kind == K::Str && c.op == 0x52) {
            scene_audio_.stop_for_movie();
            pending_movie_ = c.str;
            if (!pending_movie_.empty()) return false;
            continue;
        }
        if (c.kind == K::ParamStr && c.op == 0x43 && (c.p1 == 2 || c.p1 == 3)) {
            input_skip_label_ = c.str;
            continue;
        }
        if (c.kind == K::Param2 && c.op == 0x15 && animation_enabled_) {
            // MesVM+0x7D8 = 200*p1 milliseconds; p2 enables input checking.
            if (c.p1 == 254) continue; // effect/audio wait is already host-driven
            script_wait_ms_ = c.p1 == 255 || (!c.p1 && c.p2) ? -1 : 200.0 * c.p1;
            if (script_wait_ms_ != 0) {
                script_wait_pending_ = true;
                return false;
            }
        }
        if (c.kind == K::Param2 && c.op == 0x1E && c.p1 == 9 && animation_enabled_) {
            // MED sets bank3[0]=125 seconds before the native ED.PCK scroller.
            const int seconds = vm_.read_variable(3, 0);
            if (seconds > 0 && seconds <= 3600 && start_credits(uint32_t(seconds) * 1000)) {
                script_wait_pending_ = true;
                script_wait_ms_ = credits_duration_ms_;
                return false;
            }
            continue;
        }
        // 0x7A queues a chip blit; the attached 0x77 supplies w/h/sheet/state.
        // Anything else between them cancels the pending blit.
        if (pending_scene_blit_ && !(c.kind == K::Words4 && c.op == 0x77))
            pending_scene_blit_ = false;
        if (c.kind == K::Words4 && c.op == 0x7A) {
            pending_scene_chip_ = UiChip{int16_t(c.words[0]), int16_t(c.words[1]),
                                         c.words[2], c.words[3], 0, 0, 0, true};
            pending_scene_blit_ = true;
            continue;
        }
        if (c.kind == K::Words4 && c.op == 0x77 && pending_scene_blit_) {
            pending_scene_chip_.w = c.words[0];
            pending_scene_chip_.h = c.words[1];
            pending_scene_chip_.sheet = c.words[2];
            scene_chips_.push_back(pending_scene_chip_);
            pending_scene_blit_ = false;
            if (debug_)
                printf("  7A chip dst(%d,%d) src(%d,%d) %dx%d sheet %d\n",
                       pending_scene_chip_.dst_x, pending_scene_chip_.dst_y,
                       pending_scene_chip_.src_x, pending_scene_chip_.src_y,
                       pending_scene_chip_.w, pending_scene_chip_.h,
                       pending_scene_chip_.sheet);
            continue;
        }
        if (c.kind == K::Param2 && c.op == 0x0F) {
            // 0x0F(32) before a background load selects a crossfade; the old
            // composed frame is the fade source. Other values select
            // instant-cut families; 0x0F(0) after 0xB4 ends the sequence.
            // Route ending cards commit with 0x0F(0,3), without 0xB4.
            if (c.p1 == 0 && c.p2 && transition_pending_) start_transition(20 * (int(c.p2) + 1));
            transition_type_ = c.p1;
            if (c.p1 == 32) snapshot_for_transition();
        } else if (c.kind == K::ParamStr && c.op == 0x3C) {
            // Slot 21 carries MSK\ wipe masks for the 0x0F(21,x) transition;
            // they never render as scene content.
            std::string up;
            up.reserve(c.str.size());
            for (char ch : c.str) up.push_back(char(std::toupper((unsigned char)ch)));
            if (c.p1 == 21 && up.rfind("MSK\\", 0) == 0) {
                Image img;
                std::string err;
                if (load_grp_crx(c.str, &img, &err)) mask_ = std::move(img);
            } else {
                if (up.find("KTR.PCK") != std::string::npos || up.find("KTR01") != std::string::npos) {
                    effect_movie_ = "KotoRI_uta01.mpg";
                } else if (up.find("KTR02.PCK") != std::string::npos || up.find("KTR02") != std::string::npos) {
                    effect_movie_ = "KotoRI_uta02.mpg";
                }
                load_slot(c.p1, c.str);
            }
        } else if (c.kind == K::Words4 && c.op == 0x95) {
            if (!effect_movie_.empty()) {
                scene_audio_.stop_for_movie();
                pending_movie_ = effect_movie_;
                effect_movie_.clear();
                return false;
            }
        } else if (c.kind == K::Words4 && c.op == 0x8B) {
            set_sprite_transform(c.words[0], c.words[1], c.words[2], c.words[3]);
        } else if (c.kind == K::Words4 && c.op == 0x8E) {
            // Sprite slide-in/out (MesVM 0x42DA46 -> sub_447620/447700).
            queue_sprite_slide(c.words[0], c.words[1], c.words[2], c.words[3]);
        } else if (c.kind == K::Words4 && c.op == 0x8C) {
            // Effect-layer fade (MesVM case 140 at 0x42D6CF -> sub_447B00):
            // the BST\CUT* cut-in fades out through its wipe mask.
            queue_sprite_effect(c.words[0], c.words[1], c.words[2], c.words[3]);
        } else if (c.kind == K::Words4 && c.op == 0xAB) {
            // COMU phone chat manager (MesVM case 171 at 0x430710).
            comu_command(c.words[0], c.words[1], c.words[2], c.words[3]);
        } else if (c.kind == K::Words4 && c.op == 0xB4) {
            // Crossfade trigger after the layer commit (0x42B28E -> 0x40EF50
            // enables the effect engine for the next update pass).
            mark_transition_frame(0xB4);
        } else if (c.kind == K::Param2 && c.op == 0x36) {
            mark_transition_frame(0x36);
        } else if (c.kind == K::Param2 && c.op == 0x37) {
            // Commit queued slot-1 layer (MesVM 0x430379 -> 0x4227E0).
            mark_transition_frame(0x37);
        } else if (c.kind == K::Words4 && c.op == 0x99) {
            camera_duration_ = c.words[0];
        } else if (c.kind == K::Words4 && c.op == 0x9D && c.words[0] < 2) {
            apply_camera(c.words[0], c.words[1], c.words[2], c.words[3]);
        } else if (c.kind == K::Str && (c.op == 0x4F || c.op == 0x50)) {
            if (++transfers > 64) {
                last_error_ = "too many script transfers without dialogue";
                scene_end_ = true;
                return false;
            }
            std::string name = c.str;
            std::string system_name = name;
            std::transform(system_name.begin(), system_name.end(), system_name.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
            if (system_name.size() > 4 && system_name.substr(system_name.size()-4) == ".mes") system_name.resize(system_name.size()-4);
            if (system_name == "main" || system_name == "start" || system_name == "config" ||
                system_name == "save" || system_name == "load" || system_name == "cgmode" ||
                system_name == "music" || system_name == "scemode" || system_name == "sce_end") {
                next_script_ = c.str;
                scene_end_ = true;
                return false;
            }
            if (name.size() < 4 || name.substr(name.size() - 4) != ".mes") name += ".mes";
            if (debug_) printf("scene jump -> %s\n", name.c_str());
            if (!load_script(data_ + "/MES/" + name)) { scene_end_ = true; return false; }
        } else if (c.kind == K::Str && c.op == 0x49) {
            if (c.str == ":n") { preserve_scene_ = true; continue; }
            if (c.str == ":r") { preserve_scene_ = false; continue; }
            if (c.str.empty() || c.str[0] == ':') continue;
            // 0x3C prepares slot 1; 0x49 commits its background. The 0x0F
            // selector also contains transition modes (e.g.32), not layer IDs.
            if (c.str.find('\\') != std::string::npos || c.str.find('/') != std::string::npos) {
                load_slot(1, c.str);
                // Native background commit releases old characters, then
                // applies characters queued in this same command batch. The
                // retained 0x7A chips (the date HUD) also go away here: the
                // calendar shows on the eyecatch, not during dialogue.
                scene_chips_.clear();
                for (size_t i = 0; i < sprites_.size(); ++i)
                    if (!pending_sprites_[i]) sprites_[i].visible = false;
            }
        } else if (c.kind == K::Text) {
            // 0x6A is the speaker name shown on the plate, 0x6B.. the line.
            if (c.op == 0x6A) {
                speaker_ = c.str;
                continue;
            }
            if (c.op == 0x6D) {
                // A chat message while a COMU group is open (0x430882 ->
                // sub_4581D0); outside a group 0x6D is a secondary log sink.
                if (comu_group_) comu_message(c.str);
                continue;
            }
            current_line_ = c.str;
            return true;
        }
    }
    if (vm_.waiting_for_choice()) {
        speaker_ = prev_speaker;
        current_line_ = prev_line;
        return false;
    }
    last_error_ = vm_.error();
    if (!last_error_.empty()) printf("VM: %s\n", last_error_.c_str());
    scene_end_ = true;
    return false;
}

// Walks a UI script and collects its chips. Flow control is ignored: the
// widget definitions are laid out linearly in win_size.mes / main.mes. Only
// the bottom-bar widgets are kept — the right-hand drawer's chips are
// preloaded by the script but stay hidden until the player opens it.
// Reads a script's 0x3D sheet registrations. The engine registers each at
// index slot + 37 (MesVM case 61), which is what 0x77 then refers to.
bool SceneRunner::load_sheets(const std::string& mes_path) {
    dc4::MesFile ui;
    std::string err;
    if (!ui.load_file(mes_path, &err)) {
        printf("ui: %s\n", err.c_str());
        return false;
    }
    dc4::MesStream stream(ui);
    dc4::MesCommand c;
    int n = 0;
    while (stream.next(&c)) {
        if (c.kind == dc4::CmdKind::ParamStr && c.op == 0x3D) {
            Image img;
            std::string lower = c.str;
            for (char& ch : lower) ch = char(std::tolower((unsigned char)ch));
            if (load_grp_crx(lower + ".crx", &img, &err)) {
                sheets_[c.p1 + 37] = std::move(img);
                ++n;
            } else if (debug_) {
                printf("  sheet %d: %s (%s)\n", c.p1 + 37, c.str.c_str(),
                       err.c_str());
            }
        }
    }
    printf("ui: %d sheets registered\n", n);
    return true;
}

// Walks a UI script and collects its chips. Flow control is ignored: the
// widget definitions are laid out linearly in win_size.mes / main.mes. Only
// the bottom-bar widgets are kept — the right-hand drawer's chips are
// preloaded by the script but stay hidden until the player opens it.
bool SceneRunner::load_ui_script(const std::string& mes_path) {
    dc4::MesFile ui;
    std::string err;
    if (!ui.load_file(mes_path, &err)) {
        printf("ui: %s\n", err.c_str());
        return false;
    }
    dc4::MesStream stream(ui);
    dc4::MesCommand c;
    UiChip pending;
    bool from_widget = false;   // 0x73 belongs to a widget, 0x7A draws outright
    bool saw_rect = false;
    static_chips_.clear();
    buttons_.clear();
    while (stream.next(&c)) {
        using K = dc4::CmdKind;
        if (c.kind == K::ParamStr && c.op == 0x3D) {
            Image img;
            std::string lower = c.str;
            for (char& ch : lower) ch = char(std::tolower((unsigned char)ch));
            if (load_grp_crx(lower + ".crx", &img, &err))
                sheets_[c.p1 + 37] = std::move(img);
            continue;
        }
        if (c.kind == K::Words4 && c.op == 0x72) {
            // Hit rect: only widgets in the bottom bar band are always on.
            saw_rect = int16_t(c.words[1]) >= 690 && int16_t(c.words[1]) < kScreenH;
            if (saw_rect) {
                UiButton b;
                b.x = c.words[0];
                b.y = c.words[1];
                b.w = c.words[2] - c.words[0];
                b.h = c.words[3] - c.words[1];
                buttons_.push_back(b);
            }
            pending = UiChip();
            continue;
        }
        if (c.kind == K::Str && c.op == 0x54) {
            if (saw_rect && !buttons_.empty() && buttons_.back().action.empty())
                buttons_.back().action = c.str;
            continue;
        }
        if (c.kind == K::Words4 && (c.op == 0x73 || c.op == 0x7A)) {
            pending = UiChip{int16_t(c.words[0]), int16_t(c.words[1]), c.words[2], c.words[3],
                             0, 0, 0, true};
            from_widget = c.op == 0x73;
            continue;
        }
        if (c.kind == K::Words4 && c.op == 0x77 && pending.valid) {
            pending.w = c.words[0];
            pending.h = c.words[1];
            pending.sheet = c.words[2];
            int state = c.words[3] == 99 ? 0 : c.words[3] == 1 ? 1
                        : c.words[3] == 2 ? 2 : -1;
            if (pending.dst_y >= 690 && pending.dst_y < kScreenH) {
                if (from_widget && state >= 0) {
                    if (!buttons_.empty()) buttons_.back().state[state] = pending;
                } else {
                    static_chips_.push_back(pending);
                }
            }
            pending.valid = false;
        }
    }
    // Conditional branches contain repeated definitions of disabled buttons.
    // Retain one active rectangle per action, and the closed bar's :n button.
    std::vector<UiButton> unique;
    for (const auto& button : buttons_) {
        if (button.action.empty()) continue;
        auto old = std::find_if(unique.begin(), unique.end(), [&](const UiButton& b) { return b.action == button.action; });
        if (old == unique.end()) unique.push_back(button);
        else if (button.action == ":n") *old = button;
    }
    buttons_ = std::move(unique);
    printf("ui: %zu buttons, %zu static chips, %zu sheets\n", buttons_.size(),
           static_chips_.size(), sheets_.size());
    return true;
}

bool SceneRunner::show_message_window(const std::string& path) {
    std::string err;
    if (!load_grp_crx(path, &meswin_, &err)) {
        printf("meswin: %s\n", err.c_str());
        return false;
    }
    return true;
}

void SceneRunner::load_layer(int layer, const std::string& path) {
    if (path.empty()) return;
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower((unsigned char)c));
    Image img;
    std::string err;
    if (!load_grp_crx(lower + ".crx", &img, &err)) {
        if (debug_) printf("  bg: %s (%s)\n", path.c_str(), err.c_str());
        return;
    }
    img.name = path;
    layers_[layer] = std::move(img);
}

void SceneRunner::load_slot(int slot, const std::string& raw) {
    if (raw.empty() || slot == 0) return;
    bool is_dash = raw.size() >= 2 && raw[0] == '-' && raw[1] == '-';
    int idx = -1;
    if (slot >= 4 && slot <= 8) idx = slot - 4;
    else if (slot >= 25 && slot <= 27) idx = 12 + (slot - 25);
    // Slots 19-23 are preload scratch in the native sheet-handle table: a BST
    // registered there is not bound to a character layer until a text blink
    // tag swaps it into the character's current slot, so it never renders as
    // a sprite of its own.
    const bool preload = slot >= 19 && slot <= 23;

    if (raw == "-") {
        if (preload) { sheets_.erase(slot); return; }
        if (idx >= 0) { sprites_[idx].visible = false; pending_sprites_[idx] = true; }
        else { layers_.erase(slot); sheets_.erase(slot); }
        return;
    }

    std::string name = raw;
    if (is_dash) {
        if (preload) {
            auto it = sheets_.find(slot);
            if (it == sheets_.end()) return;
            name = it->second.name;
        } else if (idx >= 0 && !sprites_[idx].name.empty()) {
            name = sprites_[idx].name;
        } else {
            return;
        }
    }
    std::string up = name;
    for (char& c : up) c = char(std::toupper((unsigned char)c));
    if (up.find(".PCK") != std::string::npos) return;
    if (up.rfind("BST\\", 0) == 0 || up.rfind("CUT", 0) == 0) {
        if (preload) load_preload(slot, name);
        else if (idx >= 0) { load_bst(idx, name, slot); pending_sprites_[idx] = true; }
        return;
    }
    if (is_dash) return;
    if (up == "BG\\BLACK" || up == "BG\\WHITE") preserve_scene_ = false;
    // The :n/:r flag governs background preparation, not commit cleanup.
    // Native MES case 60 resets camera state when preparing slot 1 unless :n
    // is active; case 73 still releases old, unqueued character resources.
    if (slot == 1 && !preserve_scene_) {
        reset_camera();
    }
    if (slot != 1) {
        // Non-background images load into the sheet registry under their raw
        // slot id (the native sheet-handle table at VM+0x634): slot 19
        // SYSTEM\EYECATCH_CHIP is the source of the calendar's 0x7A blits,
        // not a full-screen layer. Visible layers are built only by the 0x49
        // commit of slot 1.
        load_sheet(slot, name);
        return;
    }
    load_layer(slot, name);
}

// Decodes an image and registers it for 0x77 chip blits under the raw id.
void SceneRunner::load_sheet(int id, const std::string& path) {
    if (path.empty()) return;
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower((unsigned char)c));
    Image img;
    std::string err;
    if (!load_grp_crx(lower + ".crx", &img, &err)) {
        if (debug_) printf("  sheet %d: %s (%s)\n", id, path.c_str(), err.c_str());
        return;
    }
    img.name = path;
    sheets_[id] = std::move(img);
}

void SceneRunner::load_bst(int idx, const std::string& name, int slot) {
    BstSprite& s = sprites_[size_t(idx)];
    if (slot >= 25 && slot <= 27) effect_sprite_ = idx;
    s.name = name;
    // The native sheet-handle table holds whatever slot last loaded, so a BST
    // load replaces any chip sheet registered under the same raw slot id.
    sheets_.erase(slot);
    // A fresh (or re-shown) layer starts opaque; a stale 0x8C fade must not
    // leave a newly loaded cut-in invisible.
    s.alpha = 255;
    s.fade = BstSprite::Fade{};
    if (s.image == name && s.img.valid()) { s.visible = true; return; }
    CrxInfo info;
    std::string err;
    if (!bst_load(data_ + "/GRP/BST", name, &s.img.rgba, &info, &err) &&
        !bst_load(data_ + "/GRP/FACE", name, &s.img.rgba, &info, &err)) {
        if (debug_) printf("  bst: %s\n", err.c_str());
        return;
    }
    s.img.name = name;
    s.img.w = info.width;
    s.img.h = info.height;
    // The canvas sits at (inner, inner+size) inside a 1280-tall registration
    // frame; its centre is what the placement math anchors around.
    s.frame_cx = info.offset_x + float(info.width) / 2;
    s.frame_cy = info.offset_y + float(info.height) / 2;
    s.image = name;
    s.visible = true;
    s.z = (slot >= 25) ? 2 : (slot >= 19 ? 1 : 0);
    if (debug_)
        printf("  3C slot %d -> sprite %d: %s %dx%d frame_c(%.1f,%.1f)\n", slot,
               idx, name.c_str(), s.img.w, s.img.h, s.frame_cx, s.frame_cy);
}

// Decodes a preloaded BST into the sheet-handle table under its raw slot id
// (19-23). The image stays invisible until a text tag swaps it into a
// character's current slot; the native blink preload works the same way.
void SceneRunner::load_preload(int slot, const std::string& name) {
    Image img;
    CrxInfo info;
    std::string err;
    if (!bst_load(data_ + "/GRP/BST", name, &img.rgba, &info, &err) &&
        !bst_load(data_ + "/GRP/FACE", name, &img.rgba, &info, &err)) {
        if (debug_) printf("  preload bst: %s\n", err.c_str());
        return;
    }
    img.w = info.width;
    img.h = info.height;
    img.name = name;
    sheets_[slot] = std::move(img);
}

void SceneRunner::set_sprite_transform(uint16_t scale_pct, uint16_t x, uint16_t y,
                                       uint16_t mode) {
    if ((mode & 0xFF) == 0xFF) return;
    if (mode & 0x80) {
        const size_t index = mode & 0x0F;
        if (index < sprites_.size()) {
            sprites_[index].visible = false;
            sprites_[index].fade = BstSprite::Fade{};
            pending_sprites_[index] = true;
        }
        return;
    }
    if (mode == 0) {
        // Native case 139 mode 0: sub_401040 -> sub_444260 stores the CURRENT
        // position (p0, p1) on the character body layer 10*(p2+1) and
        // snapshots it. This is what actually moves a sprite at scale 1.
        if (y < sprites_.size()) {
            BstSprite& s = sprites_[y];
            s.cur_x = int(int16_t(scale_pct));
            s.cur_y = int(int16_t(x));
            if (debug_)
                printf("  8B mode0 -> sprite %zu: cur (%d,%d)\n",
                       size_t(y), s.cur_x, s.cur_y);
        }
        return;
    }
    if ((mode & 0x10) == 0) return;
    int idx = mode & 0xF;
    if (idx < 0 || idx >= int(sprites_.size())) return;
    BstSprite& s = sprites_[size_t(idx)];
    s.scale = scale_pct ? float(scale_pct) / 100.0f : 1.0f;
    s.x = int(int16_t(x));
    s.y = int(int16_t(y));
    s.visible = true;
    if (debug_)
        printf("  8B -> sprite %d: scale %.2f tgt (%d,%d) [%s]\n", idx, s.scale,
               s.x, s.y, s.image.c_str());
}

// 0x0F(32) runs just before the 0x3C/0x37/0x49 batch that rebuilds the
// scene, so the old composed frame is still on screen here. Holding the
// snapshot lets draw() crossfade from it once 0xB4 fires after the commit.
void SceneRunner::snapshot_for_transition() {
    if (!animation_enabled_ || !effects_enabled_) return;
    // Compose layers only: the message window and UI chips must not be part
    // of the fade source.
    const bool visible = message_visible_;
    message_visible_ = false;
    draw(-1);
    message_visible_ = visible;
    transition_snap_ = frame_;
    transition_pending_ = true;
}

// 0xB4/0x36/0x37. Corpus evidence: every background swap is
// 0x0F(32) 0x3C bg 0x37 0x49 bg [sprites] 0xB4 — 0x37 commits slot 1 early
// (matching native 0x4227E0 redrawing all layers), and 0xB4 after the
// commit starts the crossfade the 0x0F selector chose.
void SceneRunner::mark_transition_frame(int kind) {
    if (kind == 0x37) {
        auto it = layers_.find(1);
        if (it != layers_.end() && it->second.valid()) {
            load_layer(1, it->second.name);
            for (size_t i = 0; i < sprites_.size(); ++i)
                if (!pending_sprites_[i]) sprites_[i].visible = false;
        }
        return;
    }
    if (kind == 0xB4) {
        start_transition();
        return;
    }
    // 0x36 precedes new character batches: redraw boundary only.
}

void SceneRunner::start_transition(int frames) {
    if (!animation_enabled_ || !effects_enabled_ || !transition_pending_ || transition_snap_.empty()) {
        transition_pending_ = false;
        return;
    }
    transition_pending_ = false;
    transition_kind_ = transition_type_;
    transition_elapsed_ms_ = 0;
    // sub_40E1E0 scales transition duration using bank99[12], centre=5.
    if (effect_speed_ < 5) frames += int(double(frames / 2) / 5 * (5 - effect_speed_));
    if (effect_speed_ > 5) frames -= int(double(frames / 2) / 6 * (effect_speed_ - 5));
    transition_duration_ms_ = std::max(1, frames) * 1000.0 / 60;
    transition_active_ = true;
    scene_dirty_ = true;
}

void SceneRunner::draw_transition(uint8_t* dst) {
    if (!transition_active_ || !dst || transition_snap_.size() != frame_.size()) return;
    const double t = std::min(1.0, transition_elapsed_ms_ /
                                      std::max(1.0, transition_duration_ms_));
    const int level = int(std::lround(255.0 * t));  // 0 = old frame, 255 = new
    const uint8_t* src = transition_snap_.data();
    for (size_t i = 0; i < frame_.size(); i += 4) {
        uint8_t* d = dst + i;
        for (int k = 0; k < 3; ++k)
            d[k] = uint8_t((src[i + k] * (255 - level) + d[k] * level) / 255);
    }
}

// 0x8E: sprite slide (MesVM 0x42DA46 -> sub_447620/sub_447700).
// operand0 = duration in 60 Hz ticks; operand1/2 = signed start offset from
// the 0x8B position; operand3 = sprite slot (see 0x8B mode bits), minus one.
// sub_447620 itself divides (end-start) by duration; the slot and direction
// assignment are split between sub_447620 (sprite A) and sub_447700 (sprite B).
void SceneRunner::queue_sprite_slide(uint16_t scale_pct, uint16_t x, uint16_t y,
                                     uint16_t mode) {
    const int idx = int(mode & 0x0F) - 1;
    if (idx < 0 || idx >= int(sprites_.size())) return;
    BstSprite& s = sprites_[size_t(idx)];
    const double duration_ms = double(uint32_t(scale_pct)) * 1000.0 / 60.0;
    s.slide.from_dx = int16_t(x);
    s.slide.from_dy = int16_t(y);
    s.slide.duration_ms = duration_ms;
    s.slide.elapsed_ms = animation_enabled_ && effects_enabled_ ? 0.0 : duration_ms;
    if (!animation_enabled_ || !effects_enabled_ || duration_ms <= 0) {
        // Headless/screenshots: settle instantly at the 0x8B position.
        s.slide.from_dx = 0; s.slide.from_dy = 0; s.slide.duration_ms = 0;
        s.slide.elapsed_ms = 0;
    }
    scene_dirty_ = true;
}

// 0x8C (MesVM case 140 at 0x42D6CF): mode 0x80 fades the effect layer — the
// BST\CUT* cut-in whose sheet the script loaded last (native VM+0x688) — out
// through its wipe mask. sub_447B00 queues alpha 255 -> 0 at 510/(w1*12)
// per frame, so the sprite is fully transparent halfway through; the skip
// path (VM_VarRead) removes the layer outright instead of animating.
void SceneRunner::queue_sprite_effect(uint16_t w0, uint16_t w1, uint16_t w2,
                                      uint16_t w3) {
    (void)w0; (void)w2;
    if ((w3 & 0x80) == 0) return;
    if (effect_sprite_ < 0 || effect_sprite_ >= int(sprites_.size())) return;
    BstSprite& s = sprites_[size_t(effect_sprite_)];
    const double frames = double(w1) * 12.0;
    if (!animation_enabled_ || !effects_enabled_ || frames <= 0) {
        s.fade = BstSprite::Fade{};
        s.alpha = 255;
        s.visible = false;
        scene_dirty_ = true;
        return;
    }
    s.fade.rate_per_frame = -510.0 / frames;
    s.fade.elapsed_frames = 0;
    s.fade.duration_frames = frames;
    s.alpha = 255;
    scene_dirty_ = true;
}

// --- COMU phone chat (MesVM case 171 at 0x430710, text case 109) -------------
// The native type-2 UI (sub_458940) composes GRP/SYSTEM/COMUCHIP.CRX and
// comustamp.CRX: a bg\black dim at 0.5 alpha, the phone chip crop (0,0)-
// (372,518) at (440,16), and five message slots from (468,80) with 81px pitch.
// sub_458370 parses queued strings: "「name」text" selects the contact row by
// the 11-name table at 0x492850..0x4928A8 (unknown senders fall back to row
// 1) and "@dN" shows comustamp cell N-1 — 79x63 crops on an 80px column /
// 64px row grid.

namespace {

const char* const kComuNames[11] = {
    "\x97\x4c\x97\xa2\x90\xb2",  // 有里栖
    "\x93\xf1\x94\x54",          // 二乃
    "\xe6\x7d\x8e\x71",          // 諳子
    "\x82\xd0\x82\xe6\x82\xe8",  // ひより
    "\x8e\x8d\x96\xbc",          // 詩名
    "\x96\xa2\x89\x48",          // 未羽
    "\x82\xbf\x82\xe6\x8e\x71",  // ちよ子
    "\x97\x4c\x97\xa2\x8d\xe7",  // 有里咲
    "\x88\xea\x93\x6f",          // 一登
    "\x90\x99\x95\xc0",          // 杉並
    "\x8a\x90\x95\xfb",          // 叶方
};

Image crop_image(const Image& src, int rx, int ry, int rw, int rh) {
    Image out;
    if (!src.valid()) return out;
    rw = std::min(rw, src.w - rx);
    rh = std::min(rh, src.h - ry);
    if (rw <= 0 || rh <= 0) return out;
    out.w = rw;
    out.h = rh;
    out.rgba.resize(size_t(rw) * rh * 4);
    for (int y = 0; y < rh; ++y)
        std::memcpy(out.rgba.data() + size_t(y) * rw * 4,
                    src.rgba.data() + (size_t(ry + y) * src.w + rx) * 4,
                    size_t(rw) * 4);
    return out;
}

// Blends one chat text line: dark-gray fill with a 1px white outline, like
// the in-game bubbles.
void blend_chat_text(const FontRenderer& font, const std::u16string& text,
                     int x, int baseline, std::vector<uint8_t>* frame) {
    if (text.empty()) return;
    const int size = 17;
    int width = 8;
    for (char16_t ch : text)
        width += ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size;
    width = std::min(width, kScreenW - x + 2);
    const int height = size + 16;
    if (width <= 0) return;
    std::vector<uint8_t> pixels(size_t(width) * height * 4, 0);
    font.rasterize_line(text, 2, size + 8, &pixels, width, height, float(size));
    const int top = baseline - size - 8;
    // The in-game text is white-filled glyphs with a thin (~1px) black
    // contour: the black ring is a one-pixel dilation of the coverage and the
    // white fill is the glyph itself, untouched.
    const int cw = width, chh = height;
    std::vector<uint8_t> ink(size_t(cw) * chh, 0);
    for (int py = 0; py < chh; ++py)
        for (int px = 0; px < cw; ++px)
            ink[size_t(py) * cw + px] = pixels[(size_t(py) * cw + px) * 4 + 3];
    const auto blend = [&](int dx, int dy, int fill, int coverage) {
        if (dx < 0 || dy < 0 || dx >= kScreenW || dy >= kScreenH || coverage <= 0) return;
        uint8_t* dst = &(*frame)[(size_t(dy) * kScreenW + dx) * 4];
        for (int channel = 0; channel < 3; ++channel)
            dst[channel] = uint8_t((fill * coverage + int(dst[channel]) * (255 - coverage)) / 255);
    };
    for (int py = 0; py < chh; ++py)
        for (int px = 0; px < cw; ++px) {
            if (!ink[size_t(py) * cw + px]) continue;
            const int dx = x - 2 + px, dy = top + py;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                    if (ox || oy) blend(dx + ox, dy + oy, 48, ink[size_t(py) * cw + px]);
        }
    for (int py = 0; py < chh; ++py)
        for (int px = 0; px < cw; ++px) {
            const int coverage = ink[size_t(py) * cw + px];
            if (coverage) blend(x - 2 + px, top + py, 255, coverage);
        }
}

}  // namespace

void SceneRunner::comu_command(uint16_t w0, uint16_t w1, uint16_t, uint16_t) {
    (void)w1;
    switch (w0) {
        case 1:
        case 7:  // show / begin a message group (sub_4581B0(1))
            if (!comu_chips_.valid()) {
                load_grp_crx("SYSTEM/COMUCHIP.CRX", &comu_chips_, nullptr);
                load_grp_crx("SYSTEM/comustamp.CRX", &comu_stamps_, nullptr);
            }
            comu_visible_ = true;
            comu_group_ = true;
            scene_dirty_ = true;
            break;
        case 0:  // group end: the queued messages commit (sub_458CC0)
            comu_group_ = false;
            break;
        case 98:  // hide (sub_4581B0(0)); the timeline persists
            comu_visible_ = false;
            comu_group_ = false;
            scene_dirty_ = true;
            break;
        case 65535:  // destroy (sub_4581F0) at scenario start
            comu_visible_ = false;
            comu_group_ = false;
            comu_items_.clear();
            scene_dirty_ = true;
            break;
        default:
            break;
    }
}

void SceneRunner::comu_message(const std::string& str) {
    ComuItem item;
    if (str.size() >= 4 && str.compare(0, 2, "\x81\x75", 2) == 0) {
        const size_t close = str.find("\x81\x76", 2);
        if (close != std::string::npos) {
            const std::string name = str.substr(2, close - 2);
            item.contact = 1;  // native fallback for unknown senders
            for (int i = 0; i < 11; ++i)
                if (name == kComuNames[i]) { item.contact = i; break; }
            item.text = str.substr(close + 2);
        } else {
            item.text = str;
        }
    } else if (str.size() >= 3 && str.compare(0, 2, "@d", 2) == 0) {
        item.stamp = std::atoi(str.c_str() + 2) - 1;
        if (item.stamp < 0) return;
        item.contact = -1;
    } else {
        item.contact = 1;
        item.text = str;
    }
    comu_items_.push_back(std::move(item));
    scene_dirty_ = true;
}

void SceneRunner::draw_comu() {
    if (!comu_chips_.valid()) return;
    // Dim the scene (the native bg\black layer at 0.5 alpha).
    static const Image* black = [] {
        static Image img;
        img.w = img.h = 1;
        img.rgba = {0, 0, 0, 255};
        return &img;
    }();
    blit(&frame_, kScreenW, kScreenH, 0, 0, *black, 1280.0f, 128);
    const Image phone = crop_image(comu_chips_, 0, 0, 372, 518);
    blit(&frame_, kScreenW, kScreenH, 440, 16, phone, 1.0f, 255);
    // The five visible slots show the newest timeline entries.
    const size_t count = comu_items_.size();
    const size_t first = count > 5 ? count - 5 : 0;
    for (size_t slot = 0; slot < 5; ++slot) {
        const size_t index = first + slot;
        if (index >= count) break;
        const ComuItem& item = comu_items_[index];
        const int y = 80 + 81 * int(slot);
        if (item.stamp >= 0) {
            const Image stamp = crop_image(comu_stamps_,
                                           80 * (item.stamp / 8), 64 * (item.stamp % 8), 79, 63);
            // Stamps sit at (480, 96+81*slot) — sub_4587A0's constants — one
            // step right and down from the bubble rows.
            if (stamp.valid()) blit(&frame_, kScreenW, kScreenH, 480, 96 + 81 * int(slot),
                                    stamp, 1.0f, 255);
            continue;
        }
        if (item.contact < 0 || item.contact > 10) continue;
        const Image row = crop_image(comu_chips_, 420, 81 * item.contact, 320, 81);
        if (row.valid()) blit(&frame_, kScreenW, kScreenH, 468, y, row, 1.0f, 255);
        if (item.text.empty() || !font_) continue;
        // The native splits queued strings at 28 SJIS bytes (14 characters);
        // wrap at the bubble's inner pixel width instead, two lines max, and
        // center the text block vertically in the bubble (chip rows 16..78).
        const std::u16string text = sjis_to_utf16(item.text);
        std::u16string lines[2];
        int width = 0, line = 0;
        for (char16_t ch : text) {
            if (ch == u'\r') continue;
            const int cell = ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? 8 : 17;
            if (ch == u'\n' || (line == 0 && width + cell > 240)) {
                line = 1;
                if (ch == u'\n') continue;
            }
            if (line < 2) { lines[line].push_back(ch); width += cell; }
        }
        const int line_count = lines[1].empty() ? 1 : 2;
        const int base = y + 14 + (62 - 24 * line_count) / 2 + 17;
        blend_chat_text(*font_, lines[0], 545, base, &frame_);
        blend_chat_text(*font_, lines[1], 545, base + 24, &frame_);
    }
}

void SceneRunner::set_animation_enabled(bool enabled) {
    animation_enabled_ = enabled;
    if (!enabled) finish_animations();
}

void SceneRunner::reset_camera() {
    camera_motion_.clear();
    camera_scale_ = 1; camera_x_ = 512; camera_y_ = 288;
    scene_dirty_ = true;
}

void SceneRunner::apply_camera(uint16_t mode, uint16_t scale_pct, uint16_t x, uint16_t y) {
    // 0x42CF23 -> 0x425530 -> 0x423AA0. Mode 0 assigns; mode 1
    // queues sub_448210, using 12 * the 0x99 duration (default 5).
    CameraMotion motion;
    motion.from_scale = camera_scale_; motion.from_x = camera_x_; motion.from_y = camera_y_;
    if (!camera_motion_.empty()) {
        const auto& last = camera_motion_.back();
        motion.from_scale = last.to_scale; motion.from_x = last.to_x; motion.from_y = last.to_y;
    }
    motion.to_scale = std::max(1.0f, float(scale_pct) / 100.0f);
    motion.to_x = float(512 + std::clamp(int(int16_t(x)), -639, 639));
    motion.to_y = float(288 + std::clamp(int(int16_t(y)), -359, 359));
    motion.duration_ms = double(camera_duration_) * 12 * 1000 / 60;
    // Native sub_448210 does not move the pivot when returning to 100%.
    // Leaving 100% establishes the new pivot before the zoom starts.
    if (mode == 1) {
        if (motion.to_scale == 1) {
            motion.to_x = motion.from_x; motion.to_y = motion.from_y;
        } else if (motion.from_scale == 1) {
            motion.from_x = motion.to_x; motion.from_y = motion.to_y;
        }
    }
    if (!animation_enabled_ || !effects_enabled_ || mode == 0 || !camera_duration_) {
        camera_motion_.clear();
        camera_scale_ = motion.to_scale; camera_x_ = motion.to_x; camera_y_ = motion.to_y;
    } else {
        if (camera_motion_.size() >= 10) return; // matches native 0x9D queue cap
        if (camera_motion_.empty()) {
            camera_x_ = motion.from_x; camera_y_ = motion.from_y;
        }
        camera_motion_.push_back(motion);
    }
    scene_dirty_ = true;
}

void SceneRunner::update(uint32_t elapsed_ms) {
    if (!pending_movie_.empty()) return;
    if (script_wait_pending_ && script_wait_ms_ > 0)
        script_wait_ms_ = std::max(0.0, script_wait_ms_ - elapsed_ms);
    if (credits_active_) {
        credits_elapsed_ms_ = std::min(credits_duration_ms_, credits_elapsed_ms_ + elapsed_ms);
        scene_dirty_ = true;
        if (credits_elapsed_ms_ >= credits_duration_ms_) credits_active_ = false;
    }
    double remaining = elapsed_ms;
    // 0x8E slide clocks.
    for (BstSprite& s : sprites_) {
        if (!s.slide.active()) continue;
        s.slide.elapsed_ms += remaining;
        if (s.slide.elapsed_ms >= s.slide.duration_ms) {
            s.slide.from_dx = 0; s.slide.from_dy = 0;
            s.slide.duration_ms = 0; s.slide.elapsed_ms = 0;
        }
        scene_dirty_ = true;
    }
    // 0x8C effect-layer fade clocks (60 fps frame domain, like sub_447B00).
    for (BstSprite& s : sprites_) {
        if (!s.fade.active()) continue;
        s.fade.elapsed_frames += double(remaining) * 60.0 / 1000.0;
        s.alpha = int(std::max(0.0, 255.0 + s.fade.rate_per_frame * s.fade.elapsed_frames));
        if (s.alpha <= 0 || !s.fade.active()) {
            s.alpha = 255;
            s.visible = false;
            s.fade = BstSprite::Fade{};
        }
        scene_dirty_ = true;
    }
    if (transition_active_) {
        transition_elapsed_ms_ += remaining;
        if (transition_elapsed_ms_ >= transition_duration_ms_) {
            transition_active_ = false;
            transition_elapsed_ms_ = transition_duration_ms_ = 0;
            transition_snap_.clear();
            transition_snap_.shrink_to_fit();
        }
        scene_dirty_ = true;
    }
    while (!camera_motion_.empty()) {
        auto& motion = camera_motion_.front();
        const double step = std::min(remaining, motion.duration_ms - motion.elapsed_ms);
        motion.elapsed_ms += step; remaining -= step;
        const double t = std::min(1.0, motion.elapsed_ms / motion.duration_ms);
        auto interpolate = [t](float a, float b) { return float(a + (b - a) * t); };
        camera_scale_ = interpolate(motion.from_scale, motion.to_scale);
        camera_x_ = interpolate(motion.from_x, motion.to_x);
        camera_y_ = interpolate(motion.from_y, motion.to_y);
        scene_dirty_ = true;
        if (t < 1) break;
        camera_motion_.erase(camera_motion_.begin());
        if (!remaining) break;
    }
}

void SceneRunner::finish_animations() {
    for (BstSprite& s : sprites_) {
        if (s.slide.active()) {
            s.slide.from_dx = 0; s.slide.from_dy = 0;
            s.slide.duration_ms = 0; s.slide.elapsed_ms = 0;
        }
        // A fade cut short by skip/headless mode dismisses the layer.
        if (s.fade.active()) {
            s.alpha = 255;
            s.visible = false;
            s.fade = BstSprite::Fade{};
        }
    }
    if (transition_active_ || transition_pending_) {
        transition_active_ = false;
        transition_pending_ = false;
        transition_elapsed_ms_ = transition_duration_ms_ = 0;
        transition_snap_.clear();
        transition_snap_.shrink_to_fit();
    }
    if (camera_motion_.empty()) return;
    const auto& motion = camera_motion_.back();
    camera_scale_ = motion.to_scale; camera_x_ = motion.to_x; camera_y_ = motion.to_y;
    camera_motion_.clear(); scene_dirty_ = true;
}

void SceneRunner::draw(int reveal_chars) {
    if (credits_active_) { draw_credits(); return; }
    if (scene_dirty_) {
    std::fill(frame_.begin(), frame_.end(), 0);
    for (size_t i = 3; i < frame_.size(); i += 4) frame_[i] = 255;
    for (auto& kv : layers_) {
        Image& l = kv.second;
        if (l.name.find("EFF") != std::string::npos) continue;  // particles
        if (!l.valid()) continue;
        // Background art is authored at 1280x720 / 1600x900 / 1920x1080.
        float sx = float(kScreenW) / l.w, sy = float(kScreenH) / l.h;
        float sc = (sx > sy ? sx : sy) * camera_scale_;
        // Scale around the pivot selected by MES rather than permanently
        // zooming the image. 0x9D(...,100,...) returns to the unzoomed frame.
        const int dx = int(std::lround(-(camera_scale_ - 1) * camera_x_));
        const int dy = int(std::lround(-(camera_scale_ - 1) * camera_y_));
        blit(&frame_, kScreenW, kScreenH, dx, dy, l,
             sc, 255);
    }
    for (int pass = 0; pass < 3; ++pass) {
        for (const BstSprite& s : sprites_) {
            if (!s.visible || !s.img.valid() || s.z != pass) continue;
            // 0x423AA0 -> 0x41F540: characters use 80% of the camera
            // offset, then compose their own scale/position with the zoom.
            const int pivot_x = 512 + int((camera_x_ - 512) * 80 / 100);
            const int pivot_y = 288 + int((camera_y_ - 288) * 80 / 100);
            // 0x8E slide: the offset decays from from_dx/from_dy to zero.
            int slide_dx = 0, slide_dy = 0;
            if (s.slide.active()) {
                const double t = s.slide.elapsed_ms / s.slide.duration_ms;
                const double ease = 1.0 - t;  // linear decay matches sub_447620
                slide_dx = int(std::lround(s.slide.from_dx * ease));
                slide_dy = int(std::lround(s.slide.from_dy * ease));
            }
            // Native placement: the frame point (tgt) sits at screen
            // (cur + tgt); the canvas centre then lands at
            // cur + tgt*(1-s) + frame_centre*s. At scale 1 the 0x8B target
            // cancels and the mode-0 position alone moves the sprite.
            const float centre_x = float(s.cur_x + slide_dx)
                                 + float(s.x) * (1.0f - s.scale)
                                 + s.frame_cx * s.scale;
            const float centre_y = float(s.cur_y + slide_dy)
                                 + float(s.y) * (1.0f - s.scale)
                                 + s.frame_cy * s.scale;
            const int base_x = int(std::lround(centre_x - float(s.img.w) * s.scale / 2));
            const int base_y = int(std::lround(centre_y - float(s.img.h) * s.scale / 2));
            const int dx = int(std::lround(pivot_x + (base_x - pivot_x) * camera_scale_));
            const int dy = int(std::lround(pivot_y + (base_y - pivot_y) * camera_scale_));
            blit(&frame_, kScreenW, kScreenH, dx, dy, s.img,
                 s.scale * camera_scale_, s.alpha);
        }
    }
    // Retained 0x7A chip blits (the script-driven date HUD). The native draws
    // them top-left anchored at dst + the 0x88 offset, 1:1 from the sheet.
    for (const UiChip& ch : scene_chips_) blit_chip(ch, 255);
    // COMU phone chat overlays the scene, under the message window.
    if (comu_visible_) draw_comu();
    scene_frame_ = frame_;
    scene_dirty_ = false;
    } else {
        frame_ = scene_frame_;
    }
    if (transition_active_) draw_transition(frame_.data());
    if (message_visible_ && !script_wait_pending_ && pending_movie_.empty()) {
        draw_message_window(reveal_chars);
        for (const UiChip& ch : static_chips_) blit_chip(ch, 255);
        for (const UiButton& b : buttons_) {
            bool hover = pointer_x_ >= b.x && pointer_x_ < b.x + b.w && pointer_y_ >= b.y && pointer_y_ < b.y + b.h;
            int state = hover ? (pointer_down_ ? 2 : 1) : 0;
            const UiChip* ch = b.state[state].valid ? &b.state[state] : &b.state[0];
            if (ch->valid) blit_chip(*ch, 255);
        }
    }
    if (vm_.waiting_for_choice()) {
        draw_choices();
    }
}

std::vector<uint8_t> SceneRunner::capture_thumbnail(int width, int height) {
    if (width <= 0 || height <= 0 || width > kScreenW || height > kScreenH) return {};
    // draw refreshes scene_frame_ before applying the crossfade and message UI.
    // In particular, do not sample frame_ left over from the preceding dialogue.
    draw(0);
    if (scene_frame_.size() != size_t(kScreenW) * kScreenH * 4) return {};
    std::vector<uint8_t> thumb(size_t(width) * height * 4, 255);
    for (int y = 0; y < height; ++y) {
        const int y0 = y * kScreenH / height, y1 = (y + 1) * kScreenH / height;
        for (int x = 0; x < width; ++x) {
            const int x0 = x * kScreenW / width, x1 = (x + 1) * kScreenW / width;
            uint32_t sum[3] = {};
            for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx) {
                const auto* pixel = &scene_frame_[(size_t(sy) * kScreenW + sx) * 4];
                for (int c = 0; c < 3; ++c) sum[c] += pixel[c];
            }
            const uint32_t count = uint32_t((x1 - x0) * (y1 - y0));
            for (int c = 0; c < 3; ++c)
                thumb[(size_t(y) * width + x) * 4 + c] = uint8_t((sum[c] + count / 2) / count);
        }
    }
    return thumb;
}

bool SceneRunner::start_credits(uint32_t duration_ms) {
    credits_pages_.clear();
    credits_entries_.clear();
    credits_archive_.clear();
    const std::string path = resolve_asset(data_ + "/GRP/ED/ED.PCK");
    std::error_code error;
    const auto length = std::filesystem::file_size(path, error);
    if (error || length > 64 * 1024 * 1024 || length < 12 || !read_file(path, &credits_archive_)) {
        last_error_ = "missing or invalid ending credits ED.PCK";
        return false;
    }
    auto u32 = [&](size_t at) {
        const auto* p = credits_archive_.data() + at;
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    };
    const uint32_t count = u32(0);
    if (!count || count > 512 || 4ull + uint64_t(count) * 8 > credits_archive_.size()) {
        last_error_ = "invalid ending credits page table";
        credits_archive_.clear();
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = u32(4 + i * 8), size = u32(8 + i * 8);
        if (offset < 4 + count * 8 || offset > credits_archive_.size() || size < 20 || size > credits_archive_.size() - offset) {
            last_error_ = "ending credits page exceeds archive";
            credits_archive_.clear(); credits_entries_.clear();
            return false;
        }
        const auto* page = credits_archive_.data() + offset;
        const int width = page[8] | (page[9] << 8), height = page[10] | (page[11] << 8);
        if (std::memcmp(page, "CRXG", 4) || width != 1280 || height != 720) {
            last_error_ = "ending credits page must be a 1280x720 CRX image";
            credits_archive_.clear(); credits_entries_.clear();
            return false;
        }
        credits_entries_.push_back({offset, size});
    }
    credits_elapsed_ms_ = 0;
    credits_scroll_ms_ = duration_ms;
    credits_duration_ms_ = duration_ms * (double(count - 1) / count) + 5000 + 200 * 1000.0 / 60;
    credits_active_ = true;
    return true;
}

void SceneRunner::draw_credits() {
    std::fill(frame_.begin(), frame_.end(), 0);
    for (size_t i = 3; i < frame_.size(); i += 4) frame_[i] = 255;
    // sub_459850: 720*page_count/duration pixels per millisecond, using
    // two adjacent 1280x720 pages. Only those two pages stay decoded.
    const double scroll_end = credits_scroll_ms_ * (double(credits_entries_.size() - 1) / credits_entries_.size());
    const double scroll = std::min(credits_elapsed_ms_, scroll_end) * (720.0 * credits_entries_.size()) / credits_scroll_ms_;
    const size_t first = size_t(scroll / 720);
    for (auto i = credits_pages_.begin(); i != credits_pages_.end();) {
        if (i->first < first || i->first > first + 1) i = credits_pages_.erase(i);
        else ++i;
    }
    for (size_t page = first; page <= first + 1 && page < credits_entries_.size(); ++page) {
        auto found = credits_pages_.find(page);
        if (found == credits_pages_.end()) {
            const auto entry = credits_entries_[page];
            Image decoded;
            CrxInfo info;
            if (!crx_decode(credits_archive_.data() + entry.first, entry.second, &decoded.rgba, &info, &last_error_)) continue;
            decoded.w = info.width; decoded.h = info.height;
            decoded.name = "ending credits";
            found = credits_pages_.emplace(page, std::move(decoded)).first;
        }
        const auto& image = found->second;
        blit(&frame_, kScreenW, kScreenH, 0, int(std::lround(double(page) * 720 - scroll)),
             image, float(kScreenW) / image.w, 255);
    }
    if (credits_elapsed_ms_ > scroll_end + 5000) {
        const double brightness = std::clamp(1.0 - (credits_elapsed_ms_ - scroll_end - 5000) / (200 * 1000.0 / 60), 0.0, 1.0);
        for (size_t i = 0; i < frame_.size(); i += 4)
            for (size_t c = 0; c < 3; ++c) frame_[i+c] = uint8_t(frame_[i+c] * brightness);
    }
}

// Copies one chip out of its sheet and blits it at full alpha.
void SceneRunner::blit_chip(const UiChip& ch, int alpha) {
    auto it = sheets_.find(ch.sheet);
    if (it == sheets_.end() || ch.w <= 0 || ch.h <= 0) return;
    const Image& s = it->second;
    Image piece;
    piece.w = ch.w;
    piece.h = ch.h;
    piece.name = "chip";
    piece.rgba.assign(size_t(ch.w) * ch.h * 4, 0);
    for (int y = 0; y < ch.h; ++y) {
        if (ch.src_y + y < 0 || ch.src_y + y >= s.h) continue;
        const int left = std::max(0, -ch.src_x), right = std::min(ch.w, s.w - ch.src_x);
        if (left >= right) continue;
        memcpy(&piece.rgba[(size_t(y) * ch.w + left) * 4],
               &s.rgba[(size_t(ch.src_y + y) * s.w + ch.src_x + left) * 4], size_t(right - left) * 4);
    }
    blit(&frame_, kScreenW, kScreenH, ch.dst_x, ch.dst_y, piece, 1.0f, alpha);
}

// The message window is the sheet's own background piece at its natural
// position plus the speaker plate above it; the bar underneath comes from the
// UI script.
void SceneRunner::draw_message_window(int reveal_chars) {
    auto win = sheets_.find(37);
    if (win == sheets_.end()) win = sheets_.find(0);
    if (win != sheets_.end()) {
        const Image& s = win->second;
        Image piece;
        piece.w = layout_.bg_w;
        piece.h = layout_.bg_h;
        piece.name = "meswin_bg";
        piece.rgba.assign(size_t(piece.w) * piece.h * 4, 0);
        for (int y = 0; y < piece.h; ++y) {
            if (layout_.bg_y + y >= s.h) break;
            memcpy(&piece.rgba[size_t(y) * piece.w * 4],
                   &s.rgba[(size_t(layout_.bg_y + y) * s.w) * 4],
                   size_t(piece.w) * 4);
        }
        blit(&frame_, kScreenW, kScreenH, layout_.bg_x, layout_.bg_y, piece,
             1.0f, (window_opacity_ * 255 + 50) / 100);
    }
    if (font_) {
        // Speaker name: plain outlined text above the window — the cyan
        // parallelogram in the sheet is the choice box, not the name plate.
        if (!speaker_.empty()) {
            Image name;
            name.w = 400;
            name.h = layout_.name_font + 10;
            name.rgba.assign(size_t(name.w) * name.h * 4, 0);
            font_->rasterize_line(sjis_to_utf16(speaker_), 0,
                                  layout_.name_font + 2, &name.rgba, name.w,
                                  name.h, layout_.name_font);
            blit(&frame_, kScreenW, kScreenH, layout_.name_x,
                 layout_.name_baseline - layout_.name_font - 2, name, 1.0f,
                 255);
        }
        if (!current_line_.empty()) draw_text_line(current_line_, reveal_chars);
    }
}

namespace {
struct RubyText { size_t start, count; std::u16string reading; };
struct TextStyle {
    int size = 22;
    uint32_t rgb = 0xFFFFFF;
    bool wide = false;
    bool operator==(const TextStyle& other) const {
        return size == other.size && rgb == other.rgb && wide == other.wide;
    }
};
struct DisplayText {
    std::u16string text;
    std::vector<TextStyle> styles;
    std::vector<RubyText> ruby;
};
DisplayText display_text(const std::string& sjis, int default_size = 22) {
    const std::u16string raw = sjis_to_utf16(sjis);
    DisplayText out;
    TextStyle style; style.size = default_size;
    auto append = [&](char16_t ch) { out.text.push_back(ch); out.styles.push_back(style); };
    auto digit = [](char16_t ch) { return ch >= u'0' && ch <= u'9'; };
    // Native 0x4B31B0, read through 0x43EE78/0x43EEF1. The native
    // palette call swaps its first two stored bytes; named @cr/g/b use RGB.
    constexpr uint32_t palette[14] = {
        0x000000, 0x0000FF, 0xFF0000, 0xFF00FF, 0x00FF00, 0x00FFFF,
        0xFFFF00, 0xFFFFFF, 0x854E28, 0xCCCCFF, 0x774400, 0xFF7F7F,
        0xCCCCCC, 0xAAAAFF
    };
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == u'\r') { ++i; continue; }
        if (raw[i] == u'@' && i + 1 < raw.size()) {
            // 0x41FD70 consumes these commands before glyph rendering.
            // Their animation/audio scheduling is not yet implemented here.
            // Match longest first, so @muf and @bt1 are not font tags.
            static const std::u16string controls[] = {
                u"cmd", u"muf", u"lse", u"re1", u"re2",
                u"wb1", u"wb2", u"wb3", u"wb4", u"wb5",
                u"bi1", u"bi2", u"bi3", u"bi4", u"bi5",
                u"bo1", u"bo2", u"bo3", u"bo4", u"bo5",
                u"bt1", u"bt2", u"bt3", u"bt4", u"bt5",
                u"by1", u"by2", u"by3", u"by4", u"by5",
                u"b1", u"b2", u"b3", u"b4", u"b5",
                u"w1", u"w2", u"w3", u"w4", u"w5",
                u"bc", u"bf", u"ht", u"hy", u"mu", u"se", u"t", u"d"
            };
            bool control = false;
            for (const auto& tag : controls) {
                if (raw.compare(i + 1, tag.size(), tag) != 0) continue;
                const size_t after = i + 1 + tag.size();
                if ((tag == u"t" || tag == u"d") && (after == raw.size() || !digit(raw[after]))) continue;
                i = after;
                while (i < raw.size() && digit(raw[i])) ++i;
                control = true; break;
            }
            if (control) continue;
            const char16_t tag = raw[i + 1];
            if (tag == u'c' && i + 2 < raw.size()) {
                const char16_t value = raw[i + 2];
                if (digit(value)) {
                    int index = value - u'0'; i += 3;
                    if (i < raw.size() && digit(raw[i])) index = index * 10 + raw[i++] - u'0';
                    if (index < 14) style.rgb = palette[index];
                    continue;
                }
                if (std::u16string(u"rgbwk").find(value) != std::u16string::npos) {
                    style.rgb = value == u'r' ? 0xFF0000 : value == u'g' ? 0x00FF00 :
                        value == u'b' ? 0x0000FF : value == u'k' ? 0 : 0xFFFFFF;
                    i += 3; continue;
                }
            }
            // GDI faces recovered at 0x439BE0 and selection at 0x43EF35.
            if (std::u16string(u"mlsiryzw").find(tag) != std::u16string::npos) {
                style.size = tag == u'l' ? 32 : tag == u's' ? 20 : tag == u'i' ? 18 :
                    tag == u'r' ? 12 : tag == u'y' ? 16 : tag == u'z' ? 10 : 22;
                style.wide = tag == u'w'; i += 2; continue;
            }
        }
        if (raw[i] == u'\uff5b') {
            size_t slash = raw.find(u'\uff0f', i + 1), end = raw.find(u'\uff5d', i + 1);
            if (slash != std::u16string::npos && end != std::u16string::npos && slash < end) {
                auto body = raw.substr(slash + 1, end - slash - 1);
                out.ruby.push_back({out.text.size(), body.size(), raw.substr(i + 1, slash - i - 1)});
                for (auto ch : body) append(ch);
                i = end + 1; continue;
            }
        }
        append(raw[i++]);
    }
    return out;
}
int cell_width(char16_t ch, int size) {
    return ch < 0x100 || ch == u'\u3000' || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size;
}
int text_width(const std::u16string& text, int size) {
    int width = 0;
    for (auto ch : text) width += cell_width(ch, size);
    return width;
}
}

std::u16string SceneRunner::visible_text() const { return display_text(current_line_).text; }
size_t SceneRunner::visible_text_length() const {
    auto text = visible_text();
    return size_t(std::count_if(text.begin(), text.end(), [](char16_t ch) { return ch != u'\n'; }));
}

void SceneRunner::draw_text_line(const std::string& sjis, int reveal) {
    const auto parsed = display_text(sjis, layout_.font_size);
    struct Line { size_t start = 0, end = 0; int width = 0; };
    std::vector<Line> lines;
    auto advance_at = [&](size_t i) {
        const auto& style = parsed.styles[i];
        return cell_width(parsed.text[i], style.size) * (style.wide ? 2 : 1);
    };

    auto is_cjk = [](char16_t ch) {
        return (ch >= 0x2E80 && ch <= 0x9FFF) ||
               (ch >= 0x3040 && ch <= 0x309F) ||
               (ch >= 0x30A0 && ch <= 0x30FF) ||
               (ch >= 0xFF00 && ch <= 0xFFEF) ||
               (ch >= 0xAC00 && ch <= 0xD7AF);
    };
    auto is_space = [](char16_t ch) {
        return ch == u' ' || ch == u'\t' || ch == u'\u3000';
    };

    const size_t total = parsed.text.size();
    size_t i = 0;
    while (i < total) {
        if (parsed.text[i] == u'\n') {
            lines.push_back({i, i, 0});
            ++i;
            continue;
        }
        size_t line_start = i;
        size_t last_break = size_t(-1);
        int cur_w = 0;
        size_t cur_i = i;
        while (cur_i < total && parsed.text[cur_i] != u'\n') {
            char16_t ch = parsed.text[cur_i];
            int adv = advance_at(cur_i);
            if (cur_w + adv > layout_.text_w && cur_i > line_start) {
                if (last_break != size_t(-1) && last_break > line_start) {
                    cur_i = last_break;
                }
                break;
            }
            cur_w += adv;
            if (is_space(ch)) {
                size_t next = cur_i + 1;
                while (next < total && is_space(parsed.text[next])) ++next;
                last_break = next;
            } else if (is_cjk(ch) || ch == u'-' || ch == u'/' || ch == u'\\') {
                last_break = cur_i + 1;
            }
            ++cur_i;
        }
        int line_w = 0;
        for (size_t k = line_start; k < cur_i; ++k) line_w += advance_at(k);
        lines.push_back({line_start, cur_i, line_w});

        i = cur_i;
        if (i < total && parsed.text[i] == u'\n') {
            ++i;
        }
    }
    if (lines.empty()) lines.push_back({0, 0, 0});
    Image text_img;
    text_img.w = layout_.text_w + 8;
    text_img.h = layout_.line_spacing * int(lines.size()) + 20;
    text_img.rgba.assign(size_t(text_img.w) * text_img.h * 4, 0);
    int remaining = reveal < 0 ? 1000000 : reveal;
    int last_x = 0, last_y = 0;
    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const auto& line = lines[line_index];
        const size_t shown_end = line.start + std::min(line.end - line.start, size_t(std::max(0, remaining)));
        const int baseline = 34 + int(line_index) * layout_.line_spacing;
        int x = 0;
        for (size_t at = line.start; at < shown_end;) {
            const auto style = parsed.styles[at];
            size_t end = at + 1;
            while (end < shown_end && parsed.styles[end] == style) ++end;
            font_->rasterize_line(parsed.text.substr(at, end - at), 2 + x, baseline,
                &text_img.rgba, text_img.w, text_img.h, float(style.size), style.rgb, style.wide);
            for (; at < end; ++at) x += advance_at(at);
        }
        for (const auto& ruby : parsed.ruby) {
            if (ruby.start < line.start || ruby.start + ruby.count > shown_end) continue;
            int rx = 0, rw = 0;
            for (size_t at = line.start; at < ruby.start; ++at) rx += advance_at(at);
            for (size_t at = ruby.start; at < ruby.start + ruby.count; ++at) rw += advance_at(at);
            const int reading_width = text_width(ruby.reading, 12);
            const auto style = parsed.styles[ruby.start];
            font_->rasterize_line(ruby.reading, 2 + rx + (rw - reading_width) / 2,
                baseline - style.size, &text_img.rgba, text_img.w, text_img.h, 12, style.rgb);
        }
        if (shown_end > line.start) { last_x = x; last_y = int(line_index) * layout_.line_spacing; }
        remaining -= int(shown_end - line.start);
    }
    const int y = layout_.text_baseline - 34;
    blit(&frame_, kScreenW, kScreenH, layout_.text_x - 2, y, text_img, 1.0f, 255);
    if (wait_marker_ && (reveal < 0 || size_t(reveal) >= visible_text_length())) {
        UiChip gear{layout_.text_x + last_x + 2, layout_.text_baseline + last_y - 23,
            layout_.gear_x + (wait_frame_ % 4) * 33, layout_.gear_y,
            layout_.gear_w, layout_.gear_h, 37, true};
        blit_chip(gear, 255);
    }
}

int SceneRunner::choice_at(int x, int y) const {
    if (!vm_.waiting_for_choice()) return -1;
    const int count = int(vm_.choices().size());
    const int area_h = message_visible_ ? 480 : kScreenH;
    const int top = (area_h - count * 74) / 2;
    for (int i = 0; i < count; ++i)
        if (x >= 312 && x < 950 && y >= top + i * 74 && y < top + i * 74 + 68) return i;
    return -1;
}

void SceneRunner::draw_choices() {
    const auto& choices = vm_.choices();
    const int count = int(choices.size());
    const int area_h = message_visible_ ? 480 : kScreenH;
    const int top = (area_h - count * 74) / 2;
    for (size_t i = 0; i < choices.size(); ++i) {
        const bool hover = choice_at(pointer_x_, pointer_y_) == int(i);
        UiChip chip{312, top + int(i) * 74, 312, hover ? 245 : 171, 638, 68, 37, true};
        blit_chip(chip, 255);
        if (font_) {
            const auto text = display_text(choices[i].text).text;
            const int font_size = dc4::is_latin() ? 20 : 22;
            Image label;
            label.w = 638; label.h = 68; label.rgba.assign(size_t(label.w) * label.h * 4, 0);
            const int tw = text_width(text, font_size);
            font_->rasterize_line(text, std::max(8, (label.w - tw) / 2),
                43, &label.rgba, label.w, label.h, float(font_size));
            blit(&frame_, kScreenW, kScreenH, 312, top + int(i) * 74, label, 1.0f, 255);
        }
    }
}

const std::string* SceneRunner::hit_test(int x, int y) const {
    if (!message_visible_ || vm_.waiting_for_choice()) return nullptr;
    for (const UiButton& b : buttons_) {
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h &&
            !b.action.empty())
            return &b.action;
    }
    return nullptr;
}

void SceneRunner::dump_sprites() const {
    for (size_t i = 0; i < sprites_.size(); ++i) {
        const BstSprite& s = sprites_[i];
        if (!s.img.valid() && !s.visible) continue;
        printf("sprite %2zu %-28s %4dx%-4d scale %.2f cur(%d,%d) tgt(%d,%d) frame_c(%.0f,%.0f) z=%d %s\n",
               i, s.image.c_str(), s.img.w, s.img.h, s.scale, s.cur_x, s.cur_y,
               s.x, s.y, s.frame_cx, s.frame_cy, s.z, s.visible ? "" : "(hidden)");
    }
}

}  // namespace dc4scene
