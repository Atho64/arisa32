// The original Log screen (MES/LOG.mes -> native backlog renderer, RE'd from
// DC4.EXE Backlog_InitGraphics 0x405CE0 + sub_404EE0, verified against
// in-game screenshots):
//   BKLOGBACK.CRX  full-screen background; the "Log" title, header rule,
//                  scrollbar track and scroll arrows are baked into it
//   BKLOGCHIP.CRX  row bar (0,220)-(1203,343), face grid 8x121px cells from
//                  y=370 (151px pitch, speaker name plate baked per cell),
//                  thumbnail checkbox, scene-jump/voice buttons, scroll thumb,
//                  back button
//   rows start at y=82, pitch 150; bar at (20, top+14); face at (20, top+18);
//   text x=480 (Japanese keeps authored lines); preview (184,top+38), 128x72;
//   jump button (328,top+28)-(488,top+62);
//   voice button (328,top+68)-(488,top+101); checkbox (327,44); back (1100,672)
#pragma once
#include <string>
#include <vector>

#include "dc4/scene.h"

namespace dc4ui {

struct LogRow {
    std::u16string name;    // speaker ("" = narration)
    std::u16string text;    // full dialogue, including authored line breaks
    std::string voice;      // voice entry name for replay ("" when silent)
    int line = 0;           // dialogue index, for the scene-jump button
    std::vector<uint8_t> snapshot; // Exact VM/scene state at this line, bounded by the host.
    static constexpr int kThumbW = 128, kThumbH = 72;
    std::vector<uint8_t> thumb; // Whole scene, downsampled to 128x72 RGBA;
                                // displayed beside the portrait on scene-jump hover.
};

// Shared desktop/Android capture: text, snapshot and fresh scene-only preview.
LogRow capture_log_row(dc4scene::SceneRunner& scene, int line);

class LogScreen {
public:
    bool load(const std::string& data_dir);
    bool loaded() const { return !back_.rgba.empty(); }

    void draw(const std::vector<LogRow>& rows, int scroll, bool thumbnails,
              int mouse_x, int mouse_y, const dc4scene::FontRenderer& font,
              std::vector<uint8_t>* frame) const;

    bool back_hit(int x, int y) const;
    bool checkbox_hit(int x, int y) const;
    // Absolute row index under (x,y) for the jump/voice buttons, -1 if none.
    int jump_hit(int x, int y, int scroll, size_t row_count) const;
    int voice_hit(int x, int y, int scroll, size_t row_count) const;
    // Thumb/track interaction: returns clamped scroll for a thumb drag.
    int thumb_scroll(int y, size_t row_count) const;
    bool thumb_hit(int x, int y, int scroll, size_t row_count) const;
    static int thumb_y(size_t row_count, int scroll);

    static constexpr int kRowPitch = 150;
    static constexpr int kTopY = 82;
    static constexpr int kVisibleRows = 4;
    static int max_scroll(size_t row_count) {
        return row_count <= kVisibleRows
                   ? 0
                   : int(row_count) - kVisibleRows;
    }

    void set_latin_mode(bool latin) { latin_mode_ = latin; }
    bool latin_mode() const { return latin_mode_; }
    // Japanese preserves authored lines/indentation; translations reflow words.
    static std::vector<std::u16string> text_lines(const std::u16string& text, bool latin);

private:
    void blit(const dc4scene::Image& img, int sx, int sy, int sw, int sh,
              int dx, int dy, std::vector<uint8_t>* frame) const;
    void blit_image(const std::vector<uint8_t>& rgba, int w, int h,
                    int dx, int dy, std::vector<uint8_t>* frame) const;
    void draw_text(const dc4scene::FontRenderer& font, const std::u16string& text,
                   int x, int baseline, std::vector<uint8_t>* frame, int size = 22) const;
    int face_id(const std::u16string& name) const;

    dc4scene::Image back_, chip_;
    std::vector<std::u16string> face_names_;
    std::vector<std::pair<std::u16string, int>> alias_names_;
    bool latin_mode_ = false;
};

}  // namespace dc4ui
