#include "dc4/confirm_dialog.h"
#include "dc4/crm.h"
#include "dc4/crx.h"
#include "dc4/resource.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace dc4ui {
namespace {

constexpr int kW = dc4scene::kScreenW;
constexpr int kH = dc4scene::kScreenH;

void blend_pixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0) return;
    if (a == 255) {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 255;
        return;
    }
    const int inv = 255 - a;
    dst[0] = uint8_t((int(r) * a + int(dst[0]) * inv) / 255);
    dst[1] = uint8_t((int(g) * a + int(dst[1]) * inv) / 255);
    dst[2] = uint8_t((int(b) * a + int(dst[2]) * inv) / 255);
    dst[3] = 255;
}

void blit_subimage(const dc4scene::Image& source, int sx, int sy, int width, int height,
                   int dx, int dy, std::vector<uint8_t>* dest) {
    if (source.rgba.empty() || width <= 0 || height <= 0 || !dest) return;
    for (int y = 0; y < height; ++y) {
        const int dest_y = dy + y;
        if (dest_y < 0 || dest_y >= kH) continue;
        const int src_y = sy + y;
        if (src_y < 0 || src_y >= source.h) continue;
        for (int x = 0; x < width; ++x) {
            const int dest_x = dx + x;
            if (dest_x < 0 || dest_x >= kW) continue;
            const int src_x = sx + x;
            if (src_x < 0 || src_x >= source.w) continue;
            const uint8_t* s = source.rgba.data() + (size_t(src_y) * source.w + src_x) * 4;
            uint8_t* d = dest->data() + (size_t(dest_y) * kW + dest_x) * 4;
            blend_pixel(d, s[0], s[1], s[2], s[3]);
        }
    }
}

int char_width(char16_t ch, int size) {
    return ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size;
}

int text_width(const std::u16string& text, int size) {
    int w = 0;
    for (char16_t ch : text) w += char_width(ch, size);
    return w;
}

void fill_rect(int rx, int ry, int rw, int rh, uint32_t rgb, uint8_t alpha,
               std::vector<uint8_t>* frame) {
    const uint8_t r = uint8_t(rgb >> 16);
    const uint8_t g = uint8_t(rgb >> 8);
    const uint8_t b = uint8_t(rgb);
    const int x0 = std::max(0, rx);
    const int y0 = std::max(0, ry);
    const int x1 = std::min(kW, rx + rw);
    const int y1 = std::min(kH, ry + rh);
    for (int y = y0; y < y1; ++y) {
        uint8_t* row = frame->data() + (size_t(y) * kW + x0) * 4;
        for (int x = x0; x < x1; ++x, row += 4) {
            blend_pixel(row, r, g, b, alpha);
        }
    }
}

void stroke_rect(int rx, int ry, int rw, int rh, int thick, uint32_t rgb, uint8_t alpha,
                 std::vector<uint8_t>* frame) {
    fill_rect(rx, ry, rw, thick, rgb, alpha, frame);
    fill_rect(rx, ry + rh - thick, rw, thick, rgb, alpha, frame);
    fill_rect(rx, ry + thick, thick, rh - 2 * thick, rgb, alpha, frame);
    fill_rect(rx + rw - thick, ry + thick, thick, rh - 2 * thick, rgb, alpha, frame);
}

void draw_text(const dc4scene::FontRenderer& font, const std::u16string& text,
               int x, int top, int size, uint32_t rgb,
               std::vector<uint8_t>* frame, uint32_t outline_rgb = 0) {
    if (text.empty()) return;
    const int tw = text_width(text, size) + 8;
    const int th = size + 16;
    dc4scene::Image img;
    img.w = tw;
    img.h = th;
    img.rgba.assign(size_t(tw) * th * 4, 0);
    font.rasterize_line(text, 2, size + 2, &img.rgba, tw, th, float(size), rgb);
    if (outline_rgb != 0) {
        const uint8_t or_ = uint8_t(outline_rgb >> 16);
        const uint8_t og = uint8_t(outline_rgb >> 8);
        const uint8_t ob = uint8_t(outline_rgb);
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            if (img.rgba[i + 3] > 0) {
                const int brightness = (int(img.rgba[i]) + int(img.rgba[i + 1]) + int(img.rgba[i + 2])) / 3;
                if (brightness < 220) {
                    const int factor = brightness * 255 / 220;
                    img.rgba[i] = uint8_t((int(or_) * (255 - factor) + int(img.rgba[i]) * factor) / 255);
                    img.rgba[i + 1] = uint8_t((int(og) * (255 - factor) + int(img.rgba[i + 1]) * factor) / 255);
                    img.rgba[i + 2] = uint8_t((int(ob) * (255 - factor) + int(img.rgba[i + 2]) * factor) / 255);
                }
            }
        }
    }
    blit_subimage(img, 0, 0, tw, th, x - 2, top - 2, frame);
}

} // namespace

bool ConfirmDialog::load(const std::string& data_dir, std::string* error) {
    const std::string dir = data_dir + "/GRP/SYSTEM";
    const std::string name = "ADVMENUCHIP.CRX";
    const std::string resolved = dc4::find_file_ci(dir, name);
    const std::string path = dc4::resolve_asset(dir + "/" + (resolved.empty() ? name : resolved));
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "Cannot open " + path;
        return false;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>()};
    dc4::CrxInfo info;
    std::string decode_error;
    if (!dc4::crx_decode(bytes.data(), bytes.size(), &chip_.rgba, &info, &decode_error)) {
        if (error) *error = "Cannot decode ADVMENUCHIP.CRX: " + decode_error;
        return false;
    }
    chip_.name = name;
    chip_.w = info.width;
    chip_.h = info.height;
    loaded_ = chip_.valid() && chip_.w >= kBoxW && chip_.h >= kBoxH;
    return loaded_;
}

void ConfirmDialog::set_chip(const dc4scene::Image& chip) {
    chip_ = chip;
    loaded_ = chip_.valid() && chip_.w >= kBoxW && chip_.h >= kBoxH;
}

void ConfirmDialog::open(const std::u16string& message,
                         const std::u16string& title,
                         const std::u16string& yes_label,
                         const std::u16string& no_label) {
    active_ = true;
    message_ = message;
    title_ = title;
    yes_text_ = yes_label;
    no_text_ = no_label;
    kind_ = ConfirmKind::Count;
    dont_confirm_ = false;
    cancel_pointer();
}

bool ConfirmDialog::open(ConfirmKind kind) {
    if(kind==ConfirmKind::Count)return false;
    if(skipped_[size_t(kind)])return false;
    static const char16_t* prompts[][3] = {
        {u"セーブしますか？",u"Save your progress?",u"Simpan kemajuan permainan?"},
        {u"上書きしてよろしいですか？",u"Overwrite this save data?",u"Timpa data simpanan ini?"},
        {u"ロードしますか？",u"Load this saved game?",u"Muat simpanan ini?"},
        {u"クイックセーブしますか？",u"Quick-save your progress?",u"Simpan cepat kemajuan permainan?"},
        {u"クイックロードしますか？",u"Load the quick save?",u"Muat simpanan cepat?"},
        {u"ゲームを開始しますか？",u"Start a new game?",u"Mulai permainan baru?"},
        {u"このシーンを再生しますか？",u"Play this scene?",u"Putar adegan ini?"},
        {u"このシーンにジャンプしますか？",u"Return to this backlog scene?",u"Kembali ke adegan dalam riwayat ini?"},
        {u"ムービーを再生しますか？",u"Play this movie?",u"Putar video ini?"},
        {u"設定を初期化しますか？",u"Reset settings to their defaults?",u"Kembalikan pengaturan awal?"},
        {u"タイトルに戻りますか？",u"Return to title?",u"Kembali ke judul?"},
        {u"終了しますか？",u"Exit the game?",u"Keluar dari permainan?"}
    };
    const int lang=language_==dc4::Language::English?1:language_==dc4::Language::Indonesian?2:0;
    open(prompts[size_t(kind)][lang]);kind_=kind;return true;
}

ConfirmResult ConfirmDialog::answer(bool yes) {
    if(!active_)return ConfirmResult::None;
    if(yes&&kind_!=ConfirmKind::Count&&dont_confirm_)skipped_[size_t(kind_)]=true;
    close();return yes?ConfirmResult::Yes:ConfirmResult::No;
}

void ConfirmDialog::close() {
    active_ = false;
    cancel_pointer();
}

bool ConfirmDialog::hit_yes(int x, int y) const {
    const int bx = kBoxX + kYesRelX;
    const int by = kBoxY + kYesRelY;
    if (x >= bx && x < bx + kYesW && y >= by && y < by + kYesH) return true;
    return false;
}

bool ConfirmDialog::hit_no(int x, int y) const {
    const int bx = kBoxX + kNoRelX;
    const int by = kBoxY + kNoRelY;
    if (x >= bx && x < bx + kNoW && y >= by && y < by + kNoH) return true;
    return false;
}

bool ConfirmDialog::hit_checkbox(int x, int y) const {
    const int bx = kBoxX + kChkRelX;
    const int by = kBoxY + kChkRelY;
    return x >= bx && x < bx + kChkW && y >= by && y < by + kChkH;
}

void ConfirmDialog::pointer_down(int x, int y) {
    if (!active_) return;
    pressed_yes_ = hit_yes(x, y);
    pressed_no_ = hit_no(x, y);
    pressed_chk_ = hit_checkbox(x, y);
}

ConfirmResult ConfirmDialog::pointer_up(int x, int y) {
    if (!active_) return ConfirmResult::None;
    const bool was_yes = pressed_yes_ && hit_yes(x, y);
    const bool was_no = pressed_no_ && hit_no(x, y);
    const bool was_chk = pressed_chk_ && hit_checkbox(x, y);
    cancel_pointer();
    if (was_chk) {
        dont_confirm_ = !dont_confirm_;
        return ConfirmResult::None;
    }
    if (was_yes) {
        return answer(true);
    }
    if (was_no) {
        return answer(false);
    }
    return ConfirmResult::None;
}

void ConfirmDialog::cancel_pointer() {
    pressed_yes_ = false;
    pressed_no_ = false;
    pressed_chk_ = false;
}

void ConfirmDialog::draw(int mouse_x, int mouse_y, bool down,
                         const dc4scene::FontRenderer& font,
                         std::vector<uint8_t>* frame) const {
    if (!active_ || !frame || frame->size() != size_t(kW) * kH * 4) return;

    // 1. Draw Dialog Box Frame (431x223 from ADVMENUCHIP.CRX)
    if (chip_.valid() && chip_.w >= kBoxW && chip_.h >= kBoxH) {
        blit_subimage(chip_, 0, 0, kBoxW, kBoxH, kBoxX, kBoxY, frame);
    } else {
        fill_rect(kBoxX, kBoxY, kBoxW, kBoxH, 0xD4EFFC, 245, frame);
        stroke_rect(kBoxX, kBoxY, kBoxW, kBoxH, 3, 0x00A0E9, 255, frame);
        fill_rect(kBoxX + 4, kBoxY + 4, kBoxW - 8, 48, 0x00A0E9, 255, frame);
    }

    // 2. Prompt text inside top blue banner (centered horizontally)
    std::u16string prompt = message_;
    if (prompt.empty()) prompt = title_;
    if (prompt.empty()) prompt = u"確認";

    std::vector<std::u16string> lines;
    size_t start = 0;
    while (start < prompt.size()) {
        size_t next = prompt.find(u'\n', start);
        if (next == std::u16string::npos) {
            lines.push_back(prompt.substr(start));
            break;
        }
        lines.push_back(prompt.substr(start, next - start));
        start = next + 1;
    }
    if (lines.empty()) lines.push_back(prompt);

    const int font_size = lines.size() > 1 ? 16 : 18;
    const int line_pitch = font_size + 6;
    const int total_h = int(lines.size()) * line_pitch;
    const int banner_y = kBoxY + 58 - total_h / 2;

    for (size_t li = 0; li < lines.size(); ++li) {
        const int tw = text_width(lines[li], font_size);
        const int tx = kBoxX + (kBoxW - tw) / 2;
        draw_text(font, lines[li], tx, banner_y + int(li) * line_pitch, font_size, 0xFFFFFF, frame, 0x0078D0);
    }

    // 3. Center buttons (YES / NO)
    const bool hover_yes = hit_yes(mouse_x, mouse_y);
    const bool down_yes = hover_yes && (down || pressed_yes_);

    const bool hover_no = hit_no(mouse_x, mouse_y);
    const bool down_no = hover_no && (down || pressed_no_);

    if (chip_.valid() && chip_.w >= 760 && chip_.h >= 210) {
        // Native YES button
        if (down_yes) {
            blit_subimage(chip_, 442, 68, 130, 46, kBoxX + 62, kBoxY + 114, frame);
        } else if (hover_yes) {
            blit_subimage(chip_, 439, 123, 137, 53, kBoxX + 58, kBoxY + 110, frame);
        } else {
            blit_subimage(chip_, 453, 12, 109, 39, kBoxX + 72, kBoxY + 117, frame);
        }

        // Native '/' slash separator: (727, 0, 30, 60) at (kBoxX + 210, kBoxY + 103)
        blit_subimage(chip_, 727, 0, 30, 60, kBoxX + 210, kBoxY + 103, frame);

        // Native NO button
        if (down_no) {
            blit_subimage(chip_, 611, 67, 89, 46, kBoxX + 267, kBoxY + 113, frame);
        } else if (hover_no) {
            blit_subimage(chip_, 608, 123, 95, 53, kBoxX + 264, kBoxY + 109, frame);
        } else {
            blit_subimage(chip_, 618, 11, 75, 39, kBoxX + 274, kBoxY + 116, frame);
        }

        // Native Checkbox row: [ ] 確認をしない
        const int chk_sx = dont_confirm_ ? 588 : 436;
        if(language_==dc4::Language::Japanese)
            blit_subimage(chip_, chk_sx, 181, 149, 28, kBoxX + 157, kBoxY + 184, frame);
    } else {
        // High fidelity vector fallback
        const int yes_x = kBoxX + kYesRelX;
        const int yes_y = kBoxY + kYesRelY;
        const uint32_t bg_yes = down_yes ? 0x005E73 : hover_yes ? 0x008DA8 : 0x123652;
        fill_rect(yes_x, yes_y, kYesW, kYesH, bg_yes, 255, frame);
        stroke_rect(yes_x, yes_y, kYesW, kYesH, 2, hover_yes ? 0x00FFFF : 0x00AFCB, 255, frame);
        std::u16string ylbl = yes_text_.empty() ? u"YES" : yes_text_;
        const int ytw = text_width(ylbl, 20);
        draw_text(font, ylbl, yes_x + (kYesW - ytw) / 2, yes_y + 10, 20, 0xFFFFFF, frame);

        // Slash
        draw_text(font, u"/", kBoxX + 210, kBoxY + 117, 24, 0x0090D0, frame);

        const int no_x = kBoxX + kNoRelX;
        const int no_y = kBoxY + kNoRelY;
        const uint32_t bg_no = down_no ? 0x1C2530 : hover_no ? 0x36485E : 0x1E2937;
        fill_rect(no_x, no_y, kNoW, kNoH, bg_no, 255, frame);
        stroke_rect(no_x, no_y, kNoW, kNoH, 2, hover_no ? 0xA0C0E0 : 0x48607A, 255, frame);
        std::u16string nlbl = no_text_.empty() ? u"NO" : no_text_;
        const int ntw = text_width(nlbl, 20);
        draw_text(font, nlbl, no_x + (kNoW - ntw) / 2, no_y + 10, 20, 0xFFFFFF, frame);

    }
    if(!chip_.valid() || chip_.w < 760 || chip_.h < 210 || language_!=dc4::Language::Japanese) {
        // Localized checkbox, also used when the atlas is unavailable.
        const int chk_x = kBoxX + 157;
        const int chk_y = kBoxY + 184;
        stroke_rect(chk_x, chk_y + 4, 16, 16, 2, 0x0090D0, 255, frame);
        if (dont_confirm_) {
            fill_rect(chk_x + 3, chk_y + 7, 10, 10, 0x00C0FF, 255, frame);
        }
        const auto label=language_==dc4::Language::English?u"Don't ask again":language_==dc4::Language::Indonesian?u"Jangan tanya lagi":u"確認をしない";
        draw_text(font,label, chk_x + 22, chk_y + 4, 16, 0x0070B0, frame);
    }
}

} // namespace dc4ui
