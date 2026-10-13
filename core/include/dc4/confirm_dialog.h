#pragma once

#include "dc4/resource.h"
#include "dc4/scene.h"
#include <string>
#include <vector>
#include <array>

namespace dc4ui {

enum class ConfirmResult {
    None,
    Yes,
    No
};

enum class ConfirmKind {
    Save, Overwrite, Load, QuickSave, QuickLoad, NewGame, SceneReplay,
    SceneJump, Movie, ResetSettings, Title, Quit, Count
};

class ConfirmDialog {
public:
    ConfirmDialog() = default;

    bool load(const std::string& data_dir, std::string* error = nullptr);
    bool loaded() const { return loaded_; }
    void set_chip(const dc4scene::Image& chip);

    void open(const std::u16string& message,
              const std::u16string& title = u"",
              const std::u16string& yes_label = u"",
              const std::u16string& no_label = u"");
    // False means this action's "don't confirm" option was accepted earlier.
    bool open(ConfirmKind kind);
    ConfirmResult answer(bool yes);
    void close();
    bool active() const { return active_; }

    void pointer_down(int x, int y);
    ConfirmResult pointer_up(int x, int y);
    void cancel_pointer();

    void draw(int mouse_x, int mouse_y, bool down,
              const dc4scene::FontRenderer& font,
              std::vector<uint8_t>* frame) const;

    void set_language(dc4::Language lang) { language_ = lang; }
    dc4::Language language() const { return language_; }

    // Dimensions and layout matching original DC4 ADVMENUCHIP dialog (431x223)
    static constexpr int kBoxW = 431;
    static constexpr int kBoxH = 223;
    static constexpr int kBoxX = (dc4scene::kScreenW - kBoxW) / 2; // 424
    static constexpr int kBoxY = (dc4scene::kScreenH - kBoxH) / 2; // 248

    static constexpr int kYesRelX = 72;
    static constexpr int kYesRelY = 117;
    static constexpr int kYesW = 109;
    static constexpr int kYesH = 45;

    static constexpr int kNoRelX = 274;
    static constexpr int kNoRelY = 116;
    static constexpr int kNoW = 75;
    static constexpr int kNoH = 45;

    static constexpr int kChkRelX = 157;
    static constexpr int kChkRelY = 184;
    static constexpr int kChkW = 149;
    static constexpr int kChkH = 28;

    bool hit_yes(int x, int y) const;
    bool hit_no(int x, int y) const;
    bool hit_checkbox(int x, int y) const;

    bool dont_confirm() const { return dont_confirm_; }
    void set_dont_confirm(bool val) { dont_confirm_ = val; }

private:
    bool active_ = false;
    bool loaded_ = false;
    dc4scene::Image chip_; // GRP/SYSTEM/ADVMENUCHIP.CRX
    std::u16string message_;
    std::u16string title_;
    std::u16string yes_text_;
    std::u16string no_text_;
    bool pressed_yes_ = false;
    bool pressed_no_ = false;
    bool pressed_chk_ = false;
    mutable bool dont_confirm_ = false;
    dc4::Language language_ = dc4::Language::Japanese;
    ConfirmKind kind_ = ConfirmKind::Count;
    std::array<bool, size_t(ConfirmKind::Count)> skipped_{};
};

} // namespace dc4ui
