#include "ui_script.h"

namespace dc4ui {

Button* ScriptUi::button_at(int x, int y) {
    for (auto& b : buttons_)
        if (point_in(b.hit, x, y)) return &b;
    return nullptr;
}

void ScriptUi::load(const dc4::MesFile& mes, size_t max_commands) {
    buttons_.clear();
    dc4::MesStream stream(mes);
    dc4::MesCommand c;
    dc4::MesCommand pending{};
    bool has_pending = false;
    size_t n = 0;
    while (n++ < max_commands && stream.next(&c)) {
        using K = dc4::CmdKind;
        if (c.kind == K::Words4 && c.op == 0x72) {
            Button b;
            b.hit = {c.words[0], c.words[1], c.words[2] - c.words[0],
                     c.words[3] - c.words[1]};
            buttons_.push_back(std::move(b));
        } else if (c.kind == K::Str && c.op == 0x54) {
            if (!buttons_.empty()) buttons_.back().action = c.str;
        } else if (c.kind == K::Words4 && c.op == 0x73) {
            pending = c;
            has_pending = true;
        } else if (c.kind == K::Words4 && c.op == 0x77 && has_pending) {
            int state_idx = c.words[3] == 99 ? 0 : c.words[3] == 1 ? 1
                            : c.words[3] == 2 ? 2 : -1;
            if (state_idx >= 0 && !buttons_.empty()) {
                Chip& chip = buttons_.back().state[state_idx];
                chip.src = {pending.words[2], pending.words[3], int(c.words[0]),
                            int(c.words[1])};
                chip.dst = {pending.words[0], pending.words[1], int(c.words[0]),
                            int(c.words[1])};
                chip.valid = true;
            }
            has_pending = false;
        }
    }
}

void ScriptUi::draw(SDL_Renderer* ren, SDL_Texture* sheet, int mouse_x,
                    int mouse_y, bool mouse_down) const {
    for (const auto& b : buttons_) {
        const Chip* chip = &b.state[0];
        if (point_in(b.hit, mouse_x, mouse_y)) {
            if (mouse_down && b.state[2].valid) chip = &b.state[2];
            else if (b.state[1].valid) chip = &b.state[1];
        }
        if (chip->valid)
            SDL_RenderCopy(ren, sheet, &chip->src, &chip->dst);
    }
}

const std::string* ScriptUi::hit_test(int x, int y) const {
    const Button* b = nullptr;
    for (const auto& btn : buttons_)
        if (point_in(btn.hit, x, y)) { b = &btn; break; }
    return b ? &b->action : nullptr;
}

}  // namespace dc4ui
