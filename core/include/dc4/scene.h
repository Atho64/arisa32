// Portable scene runner for the D.C.4 reimplementation.
//
// Renders into a software RGBA framebuffer (kScreenW x kScreenH) so the same
// code runs on the desktop shell, in headless tests and on Android — the only
// platform-specific part left is presenting the finished frame.
//
// Executed graphics commands (see docs/render-progress.md for evidence/limits):
//   0x0F u8,u8   select transition mode
//   0x3C u8+str  load an image into a slot (1..3 background, 4..8 characters,
//                19..23 the same characters via the parallel path, 25..27 cuts)
//   0x8B u16x4   sprite scale + centre position
//   0x49 str     commit a background and its queued character changes
//   0x9D u16x4   scripted camera framing
// VM control flow and audio are handled by MesVm and SceneAudio.
#pragma once
#include "dc4/audio.h"
#include "dc4/crm.h"
#include "dc4/crx.h"
#include "dc4/mesfile.h"
#include "dc4/messtream.h"
#include "dc4/mesvm.h"
#include "dc4/scene_audio.h"
#include "dc4/voice.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dc4scene {

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;

// Decodes a byte string in code page 932 to UTF-16. Windows uses
// ::MultiByteToWideChar; other platforms install their own decoder through
// g_sjis_decoder (the Android shell routes it through Java's "Shift_JIS").
using SjisDecoder = std::u16string (*)(const std::string&);
extern SjisDecoder g_sjis_decoder;
std::u16string sjis_to_utf16(const std::string& s);

// stb_truetype wrapper rasterizing one UTF-16 line into an RGBA buffer.
class FontRenderer {
public:
    FontRenderer() = default;
    ~FontRenderer();
    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;
    bool load(const std::string& path, int font_index, float pixel_size);
    // Renders white glyphs starting at x0 with the given baseline.
    // pixel_size overrides the font's own size (the engine uses 22px dialogue
    // and 18px names from one face). A dark outline is drawn under the glyphs.
    void rasterize_line(const std::u16string& text, int x0, int baseline,
                        std::vector<uint8_t>* rgba, int buf_w, int buf_h,
                        float pixel_size = 0, uint32_t rgb = 0xFFFFFF,
                        bool wide = false) const;

private:
    struct Glyph {
        int w = 0, h = 0, x = 0, y = 0;
        std::vector<uint8_t> coverage;
    };
    const Glyph& glyph(char16_t ch, int size, bool wide) const;
    mutable std::map<std::pair<int, char16_t>, Glyph> glyphs_;
    mutable std::map<int, void*> native_fonts_;
    void* native_dc_ = nullptr;
    std::u16string native_family_;
    std::vector<uint8_t> font_data_;
    void* font_ = nullptr;   // ::stbtt_fontinfo*
    float scale_ = 0;
    float pixel_size_ = 0;
};

struct Image {
    std::string name;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool valid() const { return !rgba.empty(); }
};

// One of the engine's BST sprite records.
struct BstSprite {
    std::string name;      // last script name, for "--" reuse
    std::string image;     // last resolved reference
    Image img;
    float scale = 1.0f;    // 0x8B operand 0, percent
    int x = kScreenW / 2, y = kScreenH / 2;
    // 0x8B mode-0 "current" position. The native pins the frame point (x, y)
    // at screen (cur + (x, y)): at scale 1 the canvas centre simply lands at
    // cur + (inner + size/2), and the 0x8B position acts as the zoom anchor.
    int cur_x = 0, cur_y = 0;
    // Canvas centre in the 1280-tall BST frame (inner offset + size/2).
    float frame_cx = 0, frame_cy = 0;
    int alpha = 255;
    bool visible = false;
    int z = 0;
    // 0x8E slide-in/out motion: an offset from the 0x8B position that decays
    // to zero over the scripted duration (sub_447620/sub_447700).
    struct Slide {
        int from_dx = 0, from_dy = 0;
        double elapsed_ms = 0, duration_ms = 0;
        bool active() const { return duration_ms > 0 && elapsed_ms < duration_ms; }
    } slide;
    // 0x8C mode 0x80: the effect layer (a BST\CUT* cut-in) fades out through
    // its wipe mask; sub_447B00 queues alpha 255 -> 0 at 510/frames per frame,
    // so the sprite is fully transparent halfway through the duration.
    struct Fade {
        double rate_per_frame = 0;
        double elapsed_frames = 0, duration_frames = 0;
        bool active() const { return duration_frames > 0 && elapsed_frames < duration_frames; }
    } fade;
};

// A UI chip: one blit from a sheet. 0x73 queues it on a widget, 0x7A draws it
// outright; 0x77 supplies the size, the sheet id and the state (99 = normal,
// 1 = hover, 2 = pressed).
struct UiChip {
    int dst_x = 0, dst_y = 0, src_x = 0, src_y = 0;
    int w = 0, h = 0;
    int sheet = 0;
    bool valid = false;
};

// One COMU phone-chat timeline entry (MesVM case 171 / text case 109).
// Bubble rows crop GRP/SYSTEM/COMUCHIP.CRX at (420, 81*contact)..(740, ...+81)
// — avatar, name plate and blank bubble in one chip; stamps crop
// GRP/SYSTEM/comustamp.CRX at (80*stamp/8, 64*stamp%8), 79x63 per cell.
struct ComuItem {
    int contact = 0;        // contact row 0..10; ignored when stamp >= 0
    int stamp = -1;         // 0-based stamp cell when >= 0
    std::string text;       // SJIS bubble text; empty for stamps
};

// One interactive widget from the UI script (0x72 hit rect + 0x54 action).
struct UiButton {
    int x = 0, y = 0, w = 0, h = 0;
    std::string action;
    UiChip state[3];
};

// Message-window layout, measured against a 1:1 (1280x720) real screenshot.
//
// The speaker name is *plain outlined text*, not a plate — the cyan
// parallelogram stored in MESWIN.CRX at (338,197) is the choice-selection
// box. Default dialogue cells advance 22px (11px for halfwidth characters).
struct MesWinLayout {
    int bg_x = 0, bg_y = 540, bg_w = kScreenW, bg_h = kScreenH - 540;
    int text_x = 283, text_w = 660, text_baseline = 594;
    int name_x = 283, name_baseline = 540, name_font = 18;
    int font_size = 22;
    int line_spacing = 36;
    int max_lines = 2;
    // Patched MESWIN sheet: four 32x32 frames, 33px apart, starting at (36,0).
    int gear_x = 36, gear_y = 0, gear_w = 32, gear_h = 32;
};

class SceneRunner {
public:
    explicit SceneRunner(const std::string& data_dir);
    ~SceneRunner();

    void set_font(const FontRenderer* font) { font_ = font; }
    void set_debug(bool on) { debug_ = on; }
    void set_audio(dc4::AudioEngine* audio, dc4::VoiceArchive* voice) {
        audio_ = audio;
        voice_ = voice;
        scene_audio_.set_engine(audio, voice);
    }

    bool load_script(const std::string& mes_path, const std::string& entry_label = {});
    bool jump_to_label(const std::string& label);
    const std::string& script_name() const { return script_name_; }
    using CommandObserver = std::function<void(const dc4::MesCommand&, const std::string&)>;
    void set_observer(CommandObserver observer) { observer_ = std::move(observer); }
    const std::string& pending_movie() const { return pending_movie_; }
    void finish_movie() { pending_movie_.clear(); }
    // Hosts keep updating while a script wait is pending, then advance once
    // script_ready() is true. Input can finish a skippable wait/credit sequence.
    bool waiting_for_time() const { return script_wait_pending_ && script_wait_ms_ != 0; }
    bool script_ready() const { return script_wait_pending_ && script_wait_ms_ == 0; }
    void finish_script_wait();
    bool credits_active() const { return credits_active_; }
    void set_window_opacity(int percent);
    void set_effects_enabled(bool enabled);
    void set_effect_speed(int native_0_to_11);
    void set_character_volume(size_t id, int percent, bool muted = false) { scene_audio_.set_character_volume(id, percent, muted); }
    void set_latin_mode(bool latin);
    bool latin_mode() const { return latin_mode_; }
    // Advances to (and including) the next dialogue line. Speaker names
    // (0x6A) are consumed on the way and stored separately.
    bool advance_to_next_text();

    const std::string& current_line() const { return current_line_; }
    const std::string& speaker() const { return speaker_; }
    bool scene_ended() const { return scene_end_; }
    const std::string& next_script() const { return next_script_; }
    const std::string& last_error() const { return last_error_; }
    bool waiting_for_choice() const { return vm_.waiting_for_choice(); }
    const std::vector<dc4::MesChoice>& choices() const { return vm_.choices(); }
    bool choose(size_t index) {
        bool ok = vm_.choose(index);
        if (ok) scene_dirty_ = true;
        return ok;
    }
    int choice_at(int x, int y) const;
    std::u16string visible_text() const;
    size_t visible_text_length() const;
    void set_pointer(int x, int y, bool down) { pointer_x_ = x; pointer_y_ = y; pointer_down_ = down; }
    void set_message_visible(bool visible) { message_visible_ = visible; }
    bool message_visible() const { return message_visible_; }
    bool replay_voice();
    bool voice_playing() const;
    // Voice file attached to the current line ("" when silent), for backlog rows.
    const std::string& last_voice() const { return scene_audio_.last_voice_name(); }
    // Default/headless execution settles effects immediately. Interactive hosts
    // opt in, feed elapsed milliseconds, and may finish effects while skipping.
    void set_animation_enabled(bool enabled);
    void update(uint32_t elapsed_ms);
    void finish_animations();
    bool animating() const { return !camera_motion_.empty(); }
    float camera_scale() const { return camera_scale_; }
    // Versioned emulator saves; never reads or writes the original DC4 saves.
    bool save_state(const std::string& path);
    bool load_state(const std::string& path);
    bool capture_state(std::vector<uint8_t>* bytes, std::string* error = nullptr);
    bool restore_state(const std::vector<uint8_t>& bytes, std::string* error = nullptr);
    int variable(uint16_t bank, uint16_t index) const { return vm_.read_variable(bank, index); }
    uint32_t script_position() const { return vm_.pos(); }
    void set_variable(uint16_t bank, uint16_t index, int value) { vm_.write_variable(bank, index, value); }

    // Loads the message-window UI script (MES/win_size.mes) and the sheets it
    // blits from (0x3D registers a sheet at index slot + 37).
    bool load_sheets(const std::string& mes_path);
    bool load_ui_script(const std::string& mes_path);
    bool show_message_window(const std::string& path);

    // Renders the current state into the frame buffer (RGBA, kScreenW*kScreenH*4).
    void draw(int reveal_chars);
    // Fresh scene-only preview, before transitions/message UI. Does not advance
    // the VM or animation clocks; dimensions must fit the logical canvas.
    std::vector<uint8_t> capture_thumbnail(int width, int height);
    // Shows the next-page gear after the text while waiting for a click.
    void set_wait_marker(bool on) { wait_marker_ = on; }
    void set_wait_frame(int frame) { wait_frame_ = frame; }
    const std::vector<uint8_t>& frame() const { return frame_; }

    // Returns the action tag of the widget under (x, y), or nullptr.
    const std::string* hit_test(int x, int y) const;

    // Test hooks.
    void dump_sprites() const;
    const BstSprite& sprite(size_t i) const { return sprites_[i]; }
    const std::vector<UiButton>& buttons() const { return buttons_; }

private:
    bool mes_exists(const std::string& name) const;
    bool load_grp_crx(const std::string& rel, Image* out, std::string* err) const;
    void load_slot(int slot, const std::string& raw);
    void load_bst(int idx, const std::string& name, int slot);
    void load_preload(int slot, const std::string& name);
    void load_layer(int layer, const std::string& path);
    void load_sheet(int id, const std::string& path);
    void set_sprite_transform(uint16_t scale_pct, uint16_t x, uint16_t y,
                              uint16_t mode);
    void draw_message_window(int reveal_chars);
    void blit_chip(const UiChip& ch, int alpha);
    void draw_text_line(const std::string& sjis, int reveal);
    void draw_choices();
    void apply_camera(uint16_t mode, uint16_t scale_pct, uint16_t x, uint16_t y);
    void reset_camera();
    // 0x8E queues a sprite slide (sub_447620/700); update() interpolates.
    void queue_sprite_slide(uint16_t scale_pct, uint16_t x, uint16_t y, uint16_t mode);
    // 0x8C: effect-layer fade (MesVM case 140 -> sub_447B00).
    void queue_sprite_effect(uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3);
    // COMU phone chat: 0xAB modes and 0x6D messages (MesVM case 171/109).
    void comu_command(uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3);
    void comu_message(const std::string& str);
    void draw_comu();
    bool start_credits(uint32_t duration_ms);
    void draw_credits();
    // 0xB4/0x36/0x37 transition frame markers (see docs/vm-progress.md).
    void mark_transition_frame(int kind);
    void start_transition(int frames = 30);
    void snapshot_for_transition();
    void draw_transition(uint8_t* dst);

    std::string data_;
    std::string script_name_;
    CommandObserver observer_;
    std::string pending_movie_, input_skip_label_;
    double script_wait_ms_ = 0;
    bool script_wait_pending_ = false;
    bool credits_active_ = false;
    double credits_elapsed_ms_ = 0, credits_duration_ms_ = 0, credits_scroll_ms_ = 0;
    std::vector<uint8_t> credits_archive_;
    std::vector<std::pair<uint32_t, uint32_t>> credits_entries_;
    std::map<size_t, Image> credits_pages_;
    dc4::MesFile mes_;
    dc4::MesVm vm_;
    dc4::SceneAudio scene_audio_;
    std::vector<BstSprite> sprites_ = std::vector<BstSprite>(16);
    // The native effect layer (VM+0x688): the last BST\CUT* sprite loaded,
    // which 0x8C mode 0x80 fades out.
    int effect_sprite_ = 12;
    // COMU phone chat state. The timeline persists across AB 98 (session end)
    // and is cleared by AB 65535 at every scenario start (sub_4581F0).
    bool comu_visible_ = false, comu_group_ = false;
    std::vector<ComuItem> comu_items_;
    Image comu_chips_, comu_stamps_;
    std::map<int, Image> layers_;
    std::map<int, Image> sheets_;          // 0x3D slot + 37, and 0x3C slot raw
    std::vector<UiChip> static_chips_;     // 0x7A blits (the bar capsule)
    std::vector<UiChip> scene_chips_;      // 0x7A blits executed by the scene
    UiChip pending_scene_chip_;
    bool pending_scene_blit_ = false;
    std::vector<UiButton> buttons_;        // 0x72/0x54/0x73/0x77 widgets
    MesWinLayout layout_;
    Image meswin_;
    Image mask_;           // 0x3C slot 21 wipe mask (MSK\*.crx), not rendered
    std::vector<uint8_t> frame_;
    std::vector<uint8_t> scene_frame_;
    bool scene_dirty_ = true;
    int transition_type_ = 0;
    bool preserve_scene_ = false;
    std::vector<bool> pending_sprites_ = std::vector<bool>(16, false);
    float camera_scale_ = 1.0f;
    float camera_x_ = 512, camera_y_ = 288;
    struct CameraMotion {
        float from_scale = 1, to_scale = 1;
        float from_x = 512, from_y = 288, to_x = 512, to_y = 288;
        double elapsed_ms = 0, duration_ms = 0;
    };
    std::vector<CameraMotion> camera_motion_;
    uint16_t camera_duration_ = 5; // MES 0x99, native default 5 * 12 updates
    bool animation_enabled_ = false;
    bool effects_enabled_ = true;
    int effect_speed_ = 5;
    // Scene transition: 0x0F(32) snapshots the composed frame and selects the
    // effect; 0xB4 after the layer commit starts a crossfade to the new scene.
    std::vector<uint8_t> transition_snap_;
    int transition_kind_ = 0;
    bool transition_active_ = false;
    bool transition_pending_ = false;
    double transition_elapsed_ms_ = 0, transition_duration_ms_ = 0;
    std::string current_line_;
    std::string speaker_;
    std::string effect_movie_;
    bool scene_end_ = false;
    std::string next_script_;
    std::string last_error_;
    const FontRenderer* font_ = nullptr;
    dc4::AudioEngine* audio_ = nullptr;
    dc4::VoiceArchive* voice_ = nullptr;
    bool debug_ = false;
    bool wait_marker_ = false;
    int wait_frame_ = 0;
    int pointer_x_ = -1, pointer_y_ = -1;
    bool pointer_down_ = false;
    bool message_visible_ = true;
    int window_opacity_ = 50;
    bool latin_mode_ = false;
};

}  // namespace dc4scene
