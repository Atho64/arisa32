// Script-driven UI: executes the UI widget subset of the MES command stream
// (semantics recovered from gotitle/main/MESMENU scripts, verified in-game):
//   0x72 u16x4  hit rect (x1,y1,x2,y2)
//   0x54 str    action tag (":voice" etc.)
//   0x73 u16x4  draw chip: (dst_x, dst_y, src_x, src_y) in the sheet asset
//   0x77 u16x4  chip size (w, h, sheet_id, state) — 99=normal 1=hover 2=pressed
//   0x17 u16x2  opacity (not yet applied)
#pragma once
#include "dc4/mesfile.h"
#include "dc4/messtream.h"

#include <SDL.h>

#include <string>
#include <vector>

namespace dc4ui {

struct Chip {
    SDL_Rect src{0, 0, 0, 0};
    SDL_Rect dst{0, 0, 0, 0};
    bool valid = false;
};

struct Button {
    SDL_Rect hit{0, 0, 0, 0};
    std::string action;
    Chip state[3];  // index: 0=normal(99) 1=hover(1) 2=pressed(2)
};

class ScriptUi {
public:
    // Executes the widget commands of `mes` (up to max_commands).
    void load(const dc4::MesFile& mes, size_t max_commands);

    // Sheet texture = the asset the 0x73 src coords refer to (e.g. MESWIN.CRX).
    void draw(SDL_Renderer* ren, SDL_Texture* sheet, int mouse_x, int mouse_y,
              bool mouse_down) const;

    // Returns the action tag of the button under (x,y), or nullptr.
    const std::string* hit_test(int x, int y) const;

    const std::vector<Button>& buttons() const { return buttons_; }

private:
    std::vector<Button> buttons_;
    Button* button_at(int x, int y);
};

inline bool point_in(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

}  // namespace dc4ui
