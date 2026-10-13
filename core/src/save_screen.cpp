#include "dc4/save_screen.h"
#include "dc4/resource.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#include "dc4/crm.h"
#include "dc4/crx.h"

namespace dc4ui {
namespace {

constexpr int kWidth = dc4scene::kScreenW;
constexpr int kHeight = dc4scene::kScreenH;
constexpr size_t kFrameBytes = size_t(kWidth) * kHeight * 4;
constexpr int kPageSourceX[] = {2, 44, 86, 128, 170, 211, 253, 295, 337, 378};
constexpr int kPageWidth[] = {41, 41, 41, 41, 40, 41, 41, 41, 40, 39};

bool contains(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x < right && y >= top && y < bottom;
}

bool same_action(SaveAction a, SaveAction b) {
    return a.type == b.type && a.index == b.index;
}

bool valid_image(const dc4scene::Image& image) {
    return image.w > 0 && image.h > 0 &&
           size_t(image.w) <= std::numeric_limits<size_t>::max() / 4 / size_t(image.h) &&
           image.rgba.size() >= size_t(image.w) * size_t(image.h) * 4;
}

bool read_crx(const std::string& data_dir, const char* name,
              int min_width, int min_height, dc4scene::Image* image,
              std::string* error) {
    const std::string dir = data_dir + "/GRP/SYSTEM";
    const std::string resolved = dc4::find_file_ci(dir, name);
    std::ifstream in(dc4::resolve_asset(dir + "/" + (resolved.empty() ? name : resolved)), std::ios::binary);
    if (!in) {
        if (error) *error = std::string("Cannot open SYSTEM/") + name;
        return false;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>()};
    dc4::CrxInfo info;
    std::string decode_error;
    if (!dc4::crx_decode(bytes.data(), bytes.size(), &image->rgba, &info, &decode_error)) {
        if (error) *error = std::string("Cannot decode SYSTEM/") + name + ": " + decode_error;
        return false;
    }
    image->name = name;
    image->w = info.width;
    image->h = info.height;
    if (!valid_image(*image) || image->w < min_width || image->h < min_height) {
        if (error) *error = std::string("Invalid Save/Load artwork dimensions: SYSTEM/") + name;
        return false;
    }
    return true;
}

void blend(const uint8_t* source, uint8_t* dest) {
    const int alpha = source[3];
    for (int c = 0; c < 3; ++c)
        dest[c] = uint8_t((int(source[c]) * alpha + int(dest[c]) * (255 - alpha)) / 255);
    dest[3] = 255;
}

void blit(const dc4scene::Image& image, int sx, int sy, int width, int height,
          int dx, int dy, std::vector<uint8_t>* frame) {
    if (!valid_image(image)) return;
    const int left = std::max({0, -sx, -dx});
    const int top = std::max({0, -sy, -dy});
    const int right = std::min({width, image.w - sx, kWidth - dx});
    const int bottom = std::min({height, image.h - sy, kHeight - dy});
    if (left >= right || top >= bottom) return;
    for (int y = top; y < bottom; ++y) {
        const uint8_t* source = image.rgba.data() + (size_t(sy + y) * image.w + sx + left) * 4;
        uint8_t* dest = frame->data() + (size_t(dy + y) * kWidth + dx + left) * 4;
        for (int x = left; x < right; ++x, source += 4, dest += 4)
            blend(source, dest);
    }
}

void fill_rect(int x, int y, int width, int height, uint32_t rgb,
               std::vector<uint8_t>* frame) {
    for (int py = std::max(0, y); py < std::min(kHeight, y + height); ++py) {
        for (int px = std::max(0, x); px < std::min(kWidth, x + width); ++px) {
            uint8_t* dest = frame->data() + (size_t(py) * kWidth + px) * 4;
            dest[0] = uint8_t(rgb >> 16);
            dest[1] = uint8_t(rgb >> 8);
            dest[2] = uint8_t(rgb);
            dest[3] = 255;
        }
    }
}

int char_width(char16_t ch, int size) {
    return ch < 0x100 || (ch >= 0xFF61 && ch <= 0xFF9F) ? size / 2 : size;
}

int occupied_excerpt_size(const std::u16string& text) {
    // Native @i/@y/@r selects 18/16/12px by the chapter's SJIS length,
    // including the two-byte prefix. Host excerpts use the same thresholds.
    size_t bytes = 2;
    for (char16_t ch : text) {
        bytes += ch < 0x80 || (ch >= 0xFF61 && ch <= 0xFF9F) ? 1 : 2;
        if (bytes > 34) return 12;
    }
    return bytes > 30 ? 16 : 18;
}

// Native row text has a 270px clipping area. Flatten host excerpts into its
// single chapter line, shortening long dialogue without touching save data.
std::u16string clipped_text(const std::u16string& input, int width, int size) {
    std::u16string text;
    int used = 0;
    for (char16_t ch : input) {
        if (ch == u'\r' || ch == u'\n' || ch == u'\t') ch = u' ';
        const int advance = char_width(ch, size);
        if (used + advance > width) {
            while (!text.empty() && used + size > width) {
                used -= char_width(text.back(), size);
                text.pop_back();
            }
            text.push_back(u'\u2026');
            break;
        }
        text.push_back(ch);
        used += advance;
    }
    return text;
}

void draw_text(const dc4scene::FontRenderer& font, const std::u16string& text,
               int x, int top, int width, int size, std::vector<uint8_t>* frame,
               uint32_t rgb = 0xFFFFFF) {
    if (text.empty() || width <= 0) return;
    dc4scene::Image pixels;
    pixels.w = width + 4;
    pixels.h = size + 12;
    pixels.rgba.assign(size_t(pixels.w) * pixels.h * 4, 0);
    font.rasterize_line(clipped_text(text, width, size), 2, size + 2,
                        &pixels.rgba, pixels.w, pixels.h, float(size), rgb);
    blit(pixels, 0, 0, pixels.w, pixels.h, x - 2, top - 2, frame);
}

void draw_thumbnail(const dc4scene::Image& image, int dx, int dy,
                    std::vector<uint8_t>* frame) {
    if (!valid_image(image)) return;
    // The original saves 128x72 pixels but draws the crop (0,0)-(125,71).
    // Normalize foreign host preview sizes to 128x72 before the same crop.
    for (int y = 0; y < 71; ++y) {
        const int sy = int(int64_t(y) * image.h / 72);
        for (int x = 0; x < 125; ++x) {
            const int sx = int(int64_t(x) * image.w / 128);
            blend(image.rgba.data() + (size_t(sy) * image.w + sx) * 4,
                  frame->data() + (size_t(dy + y) * kWidth + dx + x) * 4);
        }
    }
}

std::u16string page_label(int page) {
    const std::string text = std::to_string(page + 1);
    return std::u16string(text.begin(), text.end());
}

}  // namespace

bool SaveScreen::load(const std::string& data_dir, std::string* error) {
    cancel_confirm();confirm_enabled_=true;
    confirm_dialog_.load(data_dir);confirm_dialog_.set_language(language_);
    loaded_ = false;
    save_back_ = {};
    load_back_ = {};
    chip_ = {};
    system_goto_ = {};
    if (error) error->clear();
    cancel_pointer();
    dc4scene::Image save_back, load_back, chip, system_goto;
    if (!read_crx(data_dir, "SAVEBACK.CRX", 1280, 720, &save_back, error) ||
        !read_crx(data_dir, "LOADBACK.CRX", 1280, 720, &load_back, error) ||
        !read_crx(data_dir, "SLCHIP.CRX", 447, 627, &chip, error) ||
        !read_crx(data_dir, "SYSTEMGOTO.CRX", 530, 132, &system_goto, error)) return false;
    save_back_ = std::move(save_back);
    load_back_ = std::move(load_back);
    chip_ = std::move(chip);
    system_goto_ = std::move(system_goto);
    loaded_ = true;
    return true;
}

void SaveScreen::set_mode(SaveMode mode) {
    cancel_confirm();
    mode_ = mode == SaveMode::Load ? SaveMode::Load : SaveMode::Save;
    cancel_pointer();
}

void SaveScreen::set_page(int page) {
    cancel_confirm();
    page_ = std::clamp(page, 0, kPageCount - 1);
    cancel_pointer();
}

void SaveScreen::change_page(int delta) {
    set_page((page_ + delta % kPageCount + kPageCount) % kPageCount);
}

void SaveScreen::set_shortcuts(bool latest, bool automatic, bool quick) {
    latest_ = latest;
    automatic_ = automatic;
    quick_ = quick;
    cancel_pointer();
}

void SaveScreen::set_navigation(bool config, bool title, bool quit) {
    config_ = config;
    title_ = title;
    quit_ = quit;
    cancel_pointer();
}

void SaveScreen::set_slot_occupied(int index, bool occupied) {
    if (index < 0 || index >= kSlotCount) return;
    occupied_[size_t(index)] = occupied;
    if (!occupied && mode_ == SaveMode::Load && pressed_.type == SaveActionType::Slot &&
        pressed_.index == index) cancel_pointer();
}

int SaveScreen::slot_index(int page, int visible_index) {
    if (page < 0 || page >= kPageCount || visible_index < 0 || visible_index >= kSlotsPerPage)
        return -1;
    return page * kSlotsPerPage + visible_index;
}

int SaveScreen::slot_hit(int x, int y) const {
    for (int visible = 0; visible < kSlotsPerPage; ++visible) {
        const int left = visible < 5 ? 152 : 617;
        const int top = 95 + (visible % 5) * 117;
        if (contains(x, y, left, top, left + 450, top + 106))
            return slot_index(page_, visible);
    }
    return -1;
}

SaveAction SaveScreen::hit_test(int x, int y) const {
    if (!contains(x, y, 0, 0, kWidth, kHeight)) return {};
    const int slot = slot_hit(x, y);
    if (slot >= 0 && (mode_ == SaveMode::Save || occupied_[size_t(slot)]))
        return {SaveActionType::Slot, slot};
    if (contains(x, y, 772, 28, 798, 56))
        return {SaveActionType::Page, (page_ + kPageCount - 1) % kPageCount};
    if (contains(x, y, 1234, 28, 1260, 56))
        return {SaveActionType::Page, (page_ + 1) % kPageCount};
    for (int page = 0; page < kPageCount; ++page) {
        if (page != page_ && contains(x, y, 802 + 43 * page, 20, 845 + 43 * page, 60))
            return {SaveActionType::Page, page};
    }
    if (contains(x, y, 1160, 628, 1286, 684)) return {SaveActionType::Back, -1};
    if (mode_ == SaveMode::Load) {
        if (automatic_ && contains(x, y, 348, 12, 467, 63)) return {SaveActionType::AutoLoad, -1};
        if (latest_ && contains(x, y, 476, 12, 595, 63)) return {SaveActionType::LatestLoad, -1};
        if (quick_ && contains(x, y, 604, 12, 723, 63)) return {SaveActionType::QuickLoad, -1};
    }
    if (mode_ != SaveMode::Save && contains(x, y, 724, 686, 830, 719))
        return {SaveActionType::Mode, 0};
    if (mode_ != SaveMode::Load && contains(x, y, 833, 686, 939, 719))
        return {SaveActionType::Mode, 1};
    if (config_ && contains(x, y, 942, 686, 1048, 719)) return {SaveActionType::Config, -1};
    if (title_ && contains(x, y, 1051, 686, 1157, 719)) return {SaveActionType::Title, -1};
    if (quit_ && contains(x, y, 1160, 686, 1266, 719)) return {SaveActionType::Quit, -1};
    return {};
}

int SaveScreen::control_id(int x, int y) const {
    const SaveAction action = hit_test(x, y);
    if (action.type == SaveActionType::None) return 0;
    if (action.type == SaveActionType::Slot) return 1 + action.index;
    if (action.type == SaveActionType::Page) {
        if (contains(x, y, 772, 28, 798, 56)) return 101;
        if (contains(x, y, 1234, 28, 1260, 56)) return 102;
        return 110 + action.index;
    }
    if (action.type == SaveActionType::Mode) return 130 + action.index;
    return 140 + int(action.type);
}

void SaveScreen::pointer_down(int x, int y) {
    if (confirm_dialog_.active()) {
        confirm_dialog_.pointer_down(x, y);
        return;
    }
    pressed_ = hit_test(x, y);
    pressed_control_ = control_id(x, y);
    pointer_held_ = true;
}

SaveAction SaveScreen::pointer_up(int x, int y) {
    if (confirm_dialog_.active()) {
        const ConfirmResult res = confirm_dialog_.pointer_up(x, y);
        if (res == ConfirmResult::Yes) {
            return finish_confirmation(true);
        } else if (res == ConfirmResult::No) {
            return finish_confirmation(false);
        }
        return {};
    }
    const SaveAction released = hit_test(x, y);
    const bool accepted = pointer_held_ && pressed_.type != SaveActionType::None &&
                          pressed_control_ == control_id(x, y) &&
                          same_action(pressed_, released);
    cancel_pointer();
    if (!accepted) return {};
    return activate(released);
}

SaveAction SaveScreen::answer_confirmation(bool yes) {
    if(!confirming())return {};
    confirm_dialog_.answer(yes);return finish_confirmation(yes);
}
SaveAction SaveScreen::finish_confirmation(bool yes) {
    auto action=pending_action_;pending_action_={};
    if(!yes)return {};
    if(action.type==SaveActionType::Slot&&mode_==SaveMode::Load&&
       (action.index<0||action.index>=kSlotCount||!occupied_[size_t(action.index)]))return {};
    if((action.type==SaveActionType::QuickLoad&&!quick_)||
       (action.type==SaveActionType::LatestLoad&&!latest_)||
       (action.type==SaveActionType::AutoLoad&&!automatic_))return {};
    return action;
}

SaveAction SaveScreen::activate(SaveAction released) {
    if(confirming())return {};
    if(released.type==SaveActionType::Slot &&
       (released.index<0||released.index>=kSlotCount||
        (mode_==SaveMode::Load&&!occupied_[size_t(released.index)])))return {};
    if((released.type==SaveActionType::QuickLoad&&!quick_)||
       (released.type==SaveActionType::LatestLoad&&!latest_)||
       (released.type==SaveActionType::AutoLoad&&!automatic_))return {};
    if (released.type == SaveActionType::Page) set_page(released.index);
    if (released.type == SaveActionType::Mode)
        set_mode(released.index == 1 ? SaveMode::Load : SaveMode::Save);

    ConfirmKind kind=ConfirmKind::Count;
    if(released.type==SaveActionType::Slot)kind=mode_==SaveMode::Load?ConfirmKind::Load:
        occupied_[size_t(released.index)]?ConfirmKind::Overwrite:ConfirmKind::Save;
    else if(released.type==SaveActionType::QuickLoad)kind=ConfirmKind::QuickLoad;
    else if(released.type==SaveActionType::LatestLoad||released.type==SaveActionType::AutoLoad)kind=ConfirmKind::Load;
    else if(released.type==SaveActionType::Title)kind=ConfirmKind::Title;
    else if(released.type==SaveActionType::Quit)kind=ConfirmKind::Quit;
    if(confirm_enabled_&&kind!=ConfirmKind::Count){
        confirm_dialog_.set_language(language_);
        if(confirm_dialog_.open(kind)){pending_action_=released;return {};}
    }
    return released;
}

void SaveScreen::cancel_pointer() {
    pressed_ = {};
    pressed_control_ = 0;
    pointer_held_ = false;
    if (!confirm_dialog_.active()) pending_action_ = {};
    confirm_dialog_.cancel_pointer();
}

void SaveScreen::draw(const std::vector<SaveSlot>& slots, int mouse_x, int mouse_y,
                      bool down, const dc4scene::FontRenderer& font,
                      std::vector<uint8_t>* frame) const {
    if (!frame) return;
    if (frame->size() != kFrameBytes || !loaded_) {
        frame->resize(kFrameBytes);
        fill_rect(0, 0, kWidth, kHeight, 0x042344, frame);
    }
    if (loaded_) {
        const auto& background = mode_ == SaveMode::Save ? save_back_ : load_back_;
        blit(background, 0, 0, kWidth, kHeight, 0, 0, frame);
    } else {
        draw_text(font, dc4::translate_ui(mode_ == SaveMode::Save ? u"SAVE" : u"LOAD"), 12, 10, 300, 40, frame);
        draw_text(font, dc4::translate_ui(u"Save/Load artwork unavailable"), 22, 686, 660, 18, frame);
    }

    const SaveAction hovered = hit_test(mouse_x, mouse_y);
    const auto state = [&](SaveAction action) {
        if (action.type == SaveActionType::None || !same_action(action, hovered)) return 0;
        return down && (!pointer_held_ || same_action(pressed_, action)) ? 2 : 1;
    };

    const bool previous_hover = contains(mouse_x, mouse_y, 772, 28, 798, 56);
    const bool next_hover = contains(mouse_x, mouse_y, 1234, 28, 1260, 56);
    if (loaded_) {
        blit(chip_, previous_hover ? 28 : 2, 294, previous_hover ? 24 : 25, 30,
             previous_hover ? 773 : 772, 28, frame);
        blit(chip_, next_hover ? 78 : 53, 294, next_hover ? 24 : 25, 30, 1234, 28, frame);
    } else {
        draw_text(font, u"<", 773, 28, 24, 24, frame);
        draw_text(font, u">", 1234, 28, 24, 24, frame);
    }
    for (int page = 0; page < kPageCount; ++page) {
        const int x = 802 + page * 43;
        const bool selected = page == page_ || contains(mouse_x, mouse_y, x, 20, x + 43, 60);
        if (loaded_)
            blit(chip_, kPageSourceX[page], selected ? 246 : 206, kPageWidth[page],
                 selected ? 43 : 40, x, 20, frame);
        else {
            fill_rect(x, 20, 41, 40, selected ? 0xE68D12 : 0x12628D, frame);
            draw_text(font, page_label(page), x + 9, 24, 30, 26, frame);
        }
    }

    for (int visible = 0; visible < kSlotsPerPage; ++visible) {
        const int index = slot_index(page_, visible);
        const SaveSlot* slot = size_t(index) < slots.size() ? &slots[size_t(index)] : nullptr;
        const bool occupied = slot && slot->occupied;
        const bool active = (mode_ == SaveMode::Save || occupied) &&
                            state({SaveActionType::Slot, index}) != 0;
        const int x = visible < 5 ? 152 : 617;
        const int y = 95 + (visible % 5) * 117;
        if (loaded_) {
            // Native 0x40B6F0 uses the same (2,21)-(449,126) pink row for all
            // three states. SLCHIP is 447px wide, so clip its last two pixels.
            blit(chip_, 2, 21, 447, 105, x, y, frame);
            blit(chip_, active ? 148 : 16, 127, 129, 75, x + 14, y + 14, frame);
        } else {
            fill_rect(x, y, 450, 106, active ? 0x965F87 : 0x664361, frame);
            fill_rect(x + 14, y + 14, 129, 75, active ? 0x00D5EC : 0xC955A2, frame);
            fill_rect(x + 16, y + 16, 125, 71, 0x183045, frame);
        }
        if (occupied) draw_thumbnail(slot->thumbnail, x + 16, y + 16, frame);
        // sub_436280 doubles the native logical text coordinates (80,6)
        // and (80,25). Excerpts stand in for the original chapter-name table.
        const std::u16string timestamp = occupied && !slot->timestamp.empty()
            ? slot->timestamp : u"---- --/-- --:--:--";
        const std::u16string raw_excerpt = occupied
            ? (slot->excerpt.empty() ? u"Saved data" : slot->excerpt) : u"No Data";
        const std::u16string excerpt = dc4::translate_ui(raw_excerpt);
        // Native GDI text origin includes nine pixels before this font
        // renderer's baseline. Verified against the supplied LOAD capture.
        draw_text(font, timestamp, x + 160, y + 12 + 9, 270, occupied ? 18 : 20, frame);
        draw_text(font, excerpt, x + 160, y + 50 + 9, 270,
                  occupied ? occupied_excerpt_size(excerpt) : 20, frame);
    }

    const SaveActionType shortcut_types[] = {SaveActionType::AutoLoad,
        SaveActionType::LatestLoad, SaveActionType::QuickLoad};
    const bool shortcut_enabled[] = {automatic_, latest_, quick_};
    const char16_t* shortcut_labels[] = {u"AUTO", u"NEW", u"QLOAD"};
    for (int i = 0; i < 3; ++i) {
        const int x = 348 + i * 128;
        const bool enabled = mode_ == SaveMode::Load && shortcut_enabled[i];
        const int current = enabled ? state({shortcut_types[i], -1}) : 0;
        if (loaded_) {
            if (enabled)
                blit(chip_, 2 + i * 128, current ? 379 : 325, i == 2 ? 120 : 119, 53, x, 12, frame);
            else
                blit(chip_, i * 120, 576, 119, 51, x, 12, frame);
        } else {
            fill_rect(x, 12, 119, 51, !enabled ? 0x606975 : current ? 0xCA870F : 0x9A507E, frame);
            draw_text(font, dc4::translate_ui(std::u16string(shortcut_labels[i])), x + 12, 24, 100, 24, frame);
        }
    }

    const bool back_hover = state({SaveActionType::Back, -1}) != 0;
    if (loaded_)
        blit(chip_, back_hover ? 103 : 0, 431, 102, 38, 1160, 628, frame);
    else {
        fill_rect(1160, 628, 102, 38, back_hover ? 0xBE850A : 0x12628D, frame);
        draw_text(font, dc4::translate_ui(u"Back"), 1172, 631, 90, 24, frame);
    }

    const SaveAction footer_actions[] = {{SaveActionType::Mode, 0}, {SaveActionType::Mode, 1},
        {SaveActionType::Config, -1}, {SaveActionType::Title, -1}, {SaveActionType::Quit, -1}};
    const bool footer_enabled[] = {true, true, config_, title_, quit_};
    const char16_t* footer_labels[] = {u"SAVE", u"LOAD", u"CONFIG", u"TITLE", u"END"};
    for (int i = 0; i < 5; ++i) {
        const int x = 724 + i * 109;
        const bool selected = i == (mode_ == SaveMode::Save ? 0 : 1);
        const int current = state(footer_actions[i]);
        const int sy = !footer_enabled[i] ? 99 : selected ? 66 : current * 33;
        if (loaded_)
            blit(system_goto_, i * 106, sy, 106, 33, x, 686, frame);
        else {
            fill_rect(x, 686, 106, 33, !footer_enabled[i] ? 0x606975 :
                      selected || current == 2 ? 0x00AFCB : current ? 0x133E94 : 0x2767BF, frame);
            draw_text(font, dc4::translate_ui(std::u16string(footer_labels[i])), x + 10, 689, 96, 22, frame);
        }
    }

    if (confirm_dialog_.active()) {
        confirm_dialog_.draw(mouse_x, mouse_y, down, font, frame);
    }
}

}  // namespace dc4ui
