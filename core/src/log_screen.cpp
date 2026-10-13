#include "dc4/log_screen.h"
#include "dc4/resource.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "dc4/crm.h"
#include "dc4/crx.h"

namespace dc4ui {

namespace {

bool read_crx(const std::string& data_dir, const char* rel, dc4scene::Image* out) {
    const std::string dir = data_dir + "/GRP/SYSTEM";
    const std::string resolved = dc4::find_file_ci(dir, rel);
    std::vector<uint8_t> buf;
    std::ifstream in(dc4::resolve_asset(resolved.empty() ? dir + "/" + rel : dir + "/" + resolved),
                     std::ios::binary);
    if (!in) return false;
    buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    dc4::CrxInfo info;
    std::vector<uint8_t> rgba;
    std::string err;
    if (!dc4::crx_decode(buf.data(), buf.size(), &rgba, &info, &err)) return false;
    out->name = rel;
    out->w = info.width;
    out->h = info.height;
    out->rgba = std::move(rgba);
    return true;
}

// SJIS speaker names in face-grid order (BKLOGCHIP cells, 8 columns from
// y=370). Cell 29 is the blank/silhouette face used for speakers not on the
// grid (unknown characters, nicknames); 30/31 are empty.
// Names verified against 0x6A speaker commands across all 512 MES scripts.
const struct { const char* sjis; int id; } kFaceNames[] = {
    {"\x88\xea\x93\x6f", 0},             // 0 一登
    {"\x97\x4c\x97\xa2\x90\xb2", 1},     // 1 有里栖
    {"\x97\x4c\x97\xa2\x8d\xe7", 2},     // 2 有里咲
    {"\x93\xf1\x94\x54", 3},             // 3 二乃
    {"\xe6\x7d\x8e\x71", 4},             // 4 諳子
    {"\x82\xd0\x82\xe6\x82\xe8", 5},     // 5 ひより
    {"\x8e\x8d\x96\xbc", 6},             // 6 詩名
    {"\x96\xa2\x89\x48", 7},             // 7 未羽
    {"\x82\xbf\x82\xe6\x8e\x71", 8},     // 8 ちよ子 (0x82E6, not 0x82E5)
    {"\x43\x48\x4f\x43\x4f", 9},         // 9 CHOCO (ASCII in scripts)
    {"\x90\x99\x95\xc0", 10},            // 10 杉並
    {"\x8a\x90\x95\xfb", 11},            // 11 叶方
    {"\x8c\xb3", 12},                    // 12 元
    {"\x90\xf2", 13},                    // 13 泉
    {"\x82\x6a\x82\x8f\x82\x94\x82\x8f\x82\x71\x82\x68", 14},  // 14 ＫｏｔｏＲＩ (fullwidth)
    {"\x8b\xd5\x97\xa2", 15},            // 15 琴里
    {"\x83\x41\x83\x8a\x83\x58", 16},    // 16 アリス
    {"\x82\xbf\x82\xa5\x82\xb5", 17},    // 17 ちぇし (script form)
    {"\x88\xb2", 18},                    // 18 梓
    {"\x83\x89\x83\x89", 19},            // 19 ララ
    {"\x83\x8a\x83\x8a", 20},            // 20 リリ
    {"\x83\x8b\x83\x8b", 21},            // 21 ルル
    {"\x8e\xd1\x8a\xf3", 22},            // 22 紗希
    {"\x8f\xae\x8f\x83", 23},            // 23 尚純 (script form)
    {"\x97\x4a\x8e\x9f", 24},            // 24 憂次
    {"\x8f\x5c\x98\x61\x8e\x71", 25},    // 25 十和子
    {"\x8e\x4f\x94\xfc", 26},            // 26 三美
    {"\x90\xb3\x89\xc0", 27},            // 27 正佳
    {"\x89\xc2\x94\xbf", 28},            // 28 可帆
};
// Extra script speaker names mapped onto grid cells (verified off the art
// where possible; 29 is the silhouette placeholder for anything unknown).
const struct { const char* sjis; int id; } kFaceAliases[] = {
    {"\x94\xfc\x8f\x74", 26},            // 美春 (Miu) -> 三美 cell
    {"\x89\xc2\x8f\x83", 28},            // 可純 -> 可帆 cell
    {"\x82\x67\x82\x68\x82\x6e\x82\x6e", 9},   // ＣＨＯＣＯ fullwidth -> CHOCO cell
};

int text_width(const std::u16string& text, int size) {
    int w = 0;
    for (char16_t ch : text) w += ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size;
    return w;
}

}  // namespace

LogRow capture_log_row(dc4scene::SceneRunner& scene, int line) {
    LogRow row;
    row.name = dc4scene::sjis_to_utf16(scene.speaker());
    row.text = scene.visible_text();
    row.voice = scene.last_voice();
    row.line = line;
    scene.capture_state(&row.snapshot);
    row.thumb = scene.capture_thumbnail(LogRow::kThumbW, LogRow::kThumbH);
    return row;
}

std::vector<std::u16string> LogScreen::text_lines(const std::u16string& text, bool latin) {
    const int size = latin ? 18 : 22;
    // The Japanese MES already lays out its lines. Only wrap unusually long
    // lines at the row's right edge; 545px incorrectly split a 25-cell line.
    const int wrap = latin ? 545 : 720;
    auto space = [](char16_t ch) { return ch == u' ' || ch == u'\t' || ch == u'\u3000'; };
    auto cjk = [](char16_t ch) {
        return (ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xAC00 && ch <= 0xD7AF) ||
               (ch >= 0xFF00 && ch <= 0xFFEF);
    };
    std::u16string normalized;
    for (size_t i = 0; i < text.size(); ++i) {
        const char16_t ch = text[i];
        if (ch == u'\r') continue;
        if (ch == u'\n' && latin) {
            if (!normalized.empty() && !space(normalized.back()) &&
                i + 1 < text.size() && !space(text[i + 1]) &&
                text[i + 1] != u'\n' && text[i + 1] != u'\r') normalized.push_back(u' ');
        } else normalized.push_back(ch);
    }
    std::vector<std::u16string> lines;
    size_t begin = 0;
    while (begin < normalized.size()) {
        const size_t newline = normalized.find(u'\n', begin);
        const size_t end = newline == std::u16string::npos ? normalized.size() : newline;
        if (begin == end) lines.emplace_back();
        while (begin < end) {
            size_t take = begin, last_break = begin;
            int width = 0;
            while (take < end) {
                const char16_t ch = normalized[take];
                const int cell = ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size;
                if (width + cell > wrap && take > begin) {
                    if (last_break > begin) take = last_break;
                    break;
                }
                width += cell;
                if (space(ch)) {
                    size_t next = take + 1;
                    while (next < end && space(normalized[next])) ++next;
                    last_break = next;
                } else if (cjk(ch) || ch == u'-' || ch == u'/' || ch == u'\\') last_break = take + 1;
                ++take;
            }
            lines.push_back(normalized.substr(begin, take - begin));
            begin = take;
        }
        if (newline == std::u16string::npos) break;
        begin = newline + 1;
    }
    return lines;
}

bool LogScreen::load(const std::string& data_dir) {
    if (!read_crx(data_dir, "BKLOGBACK.CRX", &back_)) return false;
    if (!read_crx(data_dir, "BKLOGCHIP.CRX", &chip_)) return false;
    face_names_.clear();
    for (const auto& entry : kFaceNames)
        face_names_.push_back(dc4scene::sjis_to_utf16(entry.sjis));
    alias_names_.clear();
    for (const auto& entry : kFaceAliases)
        alias_names_.push_back({dc4scene::sjis_to_utf16(entry.sjis), entry.id});
    return true;
}

int LogScreen::face_id(const std::u16string& name) const {
    if (name.empty()) return -1;  // narration: no face at all
    for (size_t i = 0; i < face_names_.size(); ++i)
        if (face_names_[i] == name) return int(i);
    for (const auto& alias : alias_names_)
        if (alias.first == name) return alias.second;
    int rom = dc4::romanized_face_id(name);
    if (rom >= 0) return rom;
    return -2;  // unknown speaker: silhouette cell 29 + dynamic name text
}

void LogScreen::blit(const dc4scene::Image& img, int sx, int sy, int sw, int sh,
                     int dx, int dy, std::vector<uint8_t>* frame) const {
    for (int y = 0; y < sh; ++y) {
        const int fy = dy + y;
        if (fy < 0 || fy >= dc4scene::kScreenH) continue;
        const int iy = sy + y;
        if (iy < 0 || iy >= img.h) continue;
        for (int x = 0; x < sw; ++x) {
            const int fx = dx + x;
            if (fx < 0 || fx >= dc4scene::kScreenW) continue;
            const int ix = sx + x;
            if (ix < 0 || ix >= img.w) continue;
            const uint8_t* src = &img.rgba[(size_t(iy) * img.w + ix) * 4];
            if (!src[3]) continue;
            uint8_t* dst = &(*frame)[(size_t(fy) * dc4scene::kScreenW + fx) * 4];
            const int a = src[3];
            for (int c = 0; c < 3; ++c)
                dst[c] = uint8_t((int(src[c]) * a + int(dst[c]) * (255 - a)) / 255);
            dst[3] = 255;
        }
    }
}

// White glyphs with the font's built-in dark outline, matching the message
// window. Rasterizes a tight bounds buffer like the shell's overlay_text.
void LogScreen::draw_text(const dc4scene::FontRenderer& font, const std::u16string& text,
                          int x, int baseline, std::vector<uint8_t>* frame, int size) const {
    if (text.empty()) return;
    const int width = std::min(text_width(text, size) + 8, dc4scene::kScreenW - x + 2);
    if (width <= 0) return;
    const int height = size + 16;
    std::vector<uint8_t> pixels(size_t(width) * height * 4, 0);
    font.rasterize_line(text, 2, size + 8, &pixels, width, height, float(size));
    const int top = baseline - size - 8;
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const int dx = x - 2 + px, dy = top + py;
            if (dx < 0 || dy < 0 || dx >= dc4scene::kScreenW || dy >= dc4scene::kScreenH) continue;
            const uint8_t* src = &pixels[(size_t(py) * width + px) * 4];
            if (!src[3]) continue;
            uint8_t* dst = &(*frame)[(size_t(dy) * dc4scene::kScreenW + dx) * 4];
            for (int c = 0; c < 3; ++c)
                dst[c] = uint8_t((int(src[c]) * src[3] + int(dst[c]) * (255 - src[3])) / 255);
            dst[3] = 255;
        }
    }
}

// Blit a raw RGBA buffer (the captured scene thumbnail) into the frame.
void LogScreen::blit_image(const std::vector<uint8_t>& rgba, int w, int h,
                           int dx, int dy, std::vector<uint8_t>* frame) const {
    if (rgba.size() != size_t(w) * h * 4) return;
    for (int y = 0; y < h; ++y) {
        const int fy = dy + y;
        if (fy < 0 || fy >= dc4scene::kScreenH) continue;
        for (int x = 0; x < w; ++x) {
            const int fx = dx + x;
            if (fx < 0 || fx >= dc4scene::kScreenW) continue;
            const uint8_t* src = &rgba[(size_t(y) * w + x) * 4];
            if (!src[3]) continue;
            uint8_t* dst = &(*frame)[(size_t(fy) * dc4scene::kScreenW + fx) * 4];
            const int a = src[3];
            for (int c = 0; c < 3; ++c)
                dst[c] = uint8_t((int(src[c]) * a + int(dst[c]) * (255 - a)) / 255);
            dst[3] = 255;
        }
    }
}

void LogScreen::draw(const std::vector<LogRow>& rows, int scroll, bool thumbnails,
                     int mouse_x, int mouse_y, const dc4scene::FontRenderer& font,
                     std::vector<uint8_t>* frame) const {
    if (!loaded()) return;
    // Opaque full-screen background: title, rules, track and arrows are baked in.
    blit(back_, 0, 0, back_.w, back_.h, 0, 0, frame);

    const int count = int(rows.size());
    for (int slot = 0; slot < kVisibleRows; ++slot) {
        const int index = scroll + slot;
        if (index < 0 || index >= count) continue;
        const LogRow& row = rows[size_t(index)];
        const int top = kTopY + slot * kRowPitch;

        // Row bar (sub_404EE0 0x4057F4: chip (0,220)-(1203,343) at (20, top+14)).
        blit(chip_, 0, 220, 1203, 123, 20, top + 14, frame);

        // Speaker face at (20, top+18); the pink name plate is baked into
        // each grid cell. Speakers not on the grid (unknown characters,
        // nicknames) use the silhouette cell 29 plus a dynamic name text.
        // Narration (empty name) shows no face, matching the original.
        const int id = face_id(row.name);
        if (id >= 0) {
            const int cx = 151 * (id % 8);
            const int cy = 121 * (id / 8) + 370;
            blit(chip_, cx, cy, 150, 120, 20, top + 18, frame);
        } else if (id == -2) {
            // Unknown speaker: silhouette placeholder (cell 29) + name text.
            blit(chip_, 151 * (29 % 8), 121 * (29 / 8) + 370, 150, 120, 20, top + 18, frame);
            int bytes = 0;
            for (char16_t ch : row.name) bytes += ch < 0x100 ? 1 : 2;
            const int x = bytes >= 20 ? 75 - 3 * bytes : 48 - 4 * bytes;
            draw_text(font, row.name, x, top + 36, frame);
        }

        // The original keeps the speaker portrait/name visible and draws a
        // separate 16:9 scene preview between it and the scene-jump button.
        if (thumbnails && jump_hit(mouse_x, mouse_y, scroll, rows.size()) == index)
            blit_image(row.thumb, LogRow::kThumbW, LogRow::kThumbH, 184, top + 38, frame);

        // Keep the Japanese script's newlines and indentation. The supplied
        // original row has 36px line spacing; translations keep their reflow.
        {
            const int font_size = latin_mode_ ? 18 : 22;
            const int line_pitch = latin_mode_ ? 24 : 36;
            int baseline = top + (latin_mode_ ? 44 : 49);
            for (const auto& line : text_lines(row.text, latin_mode_)) {
                if (baseline >= top + kRowPitch) break;
                draw_text(font, line, 480, baseline, frame, font_size);
                baseline += line_pitch;
            }
        }

        // Scene-jump button (0x405ABD): region (328,top+28)-(488,top+62),
        // art (362,52)-(488,87), hover (492,52)-(618,87).
        const bool jump_hover = jump_hit(mouse_x, mouse_y, scroll, rows.size()) == index;
        blit(chip_, jump_hover ? 492 : 362, 52, 126, 35, 328, top + 28, frame);
        // Voice-replay button (0x405B7E), only when the row has a voice.
        if (!row.voice.empty()) {
            const bool voice_hover = voice_hit(mouse_x, mouse_y, scroll, rows.size()) == index;
            blit(chip_, voice_hover ? 492 : 362, 12, 126, 34, 328, top + 68, frame);
        }
    }

    // Thumbnail checkbox (0x4058C9): box (0,6)-(29,37) at (327,44), hover
    // (31,6)-(61,37), label (76,0)-(310,38) at (356,38), check (310,0)-(341,32).
    const bool check_hover = checkbox_hit(mouse_x, mouse_y);
    blit(chip_, check_hover ? 31 : 0, 6, 29, 31, 327, 44, frame);
    blit(chip_, 76, 0, 234, 38, 356, 38, frame);
    if (thumbnails) blit(chip_, 310, 0, 31, 32, 327, 44, frame);

    // Back button (sub_4010F0): art (640,8)-(744,47) at (1100,672),
    // hover (640,47)-(744,89); region (1100,672)-(1216,712).
    blit(chip_, 640, back_hit(mouse_x, mouse_y) ? 47 : 8, 104, 39, 1100, 672, frame);

    // Scroll thumb (Backlog_InitGraphics 0x406061): art (1200,38)-(1224,68)
    // at x=1232, y = 82 + 582 * scrolled fraction over the TOTAL row count
    // (native: 582 * current / total), hidden with <= 4 rows.
    if (rows.size() > kVisibleRows)
        blit(chip_, 1200, 38, 24, 30, 1232, thumb_y(rows.size(), scroll), frame);
}

bool LogScreen::back_hit(int x, int y) const {
    return x >= 1100 && x < 1216 && y >= 672 && y < 712;
}

bool LogScreen::checkbox_hit(int x, int y) const {
    return x >= 327 && x < 666 && y >= 38 && y < 76;
}

static int row_at(int x, int y, int scroll, size_t row_count, int dy0, int dy1) {
    if (x < 328 || x >= 488) return -1;
    for (int slot = 0; slot < LogScreen::kVisibleRows; ++slot) {
        const int index = scroll + slot;
        if (index < 0 || index >= int(row_count)) break;
        const int top = LogScreen::kTopY + slot * LogScreen::kRowPitch;
        if (y >= top + dy0 && y < top + dy1) return index;
    }
    return -1;
}

int LogScreen::jump_hit(int x, int y, int scroll, size_t row_count) const {
    return row_at(x, y, scroll, row_count, 28, 62);
}

int LogScreen::voice_hit(int x, int y, int scroll, size_t row_count) const {
    return row_at(x, y, scroll, row_count, 68, 101);
}

int LogScreen::thumb_y(size_t row_count, int scroll) {
    if (row_count <= kVisibleRows) return kTopY;
    const int y = kTopY + 582 * std::clamp(scroll, 0, int(row_count)) / int(row_count);
    return std::clamp(y, kTopY, 634);
}

bool LogScreen::thumb_hit(int x, int y, int scroll, size_t row_count) const {
    if (row_count <= kVisibleRows || x < 1222 || x >= 1266) return false;
    const int ty = thumb_y(row_count, scroll);
    return y >= ty - 4 && y < ty + 34;
}

int LogScreen::thumb_scroll(int y, size_t row_count) const {
    if (row_count <= kVisibleRows) return 0;
    const int offset = (y - kTopY) * int(row_count) / 582;
    return std::clamp(offset, 0, max_scroll(row_count));
}

}  // namespace dc4ui
