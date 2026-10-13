#include "dc4/scene.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool value, const char* expression, int line) {
    if (!value) { std::cerr << line << ": " << expression << '\n'; ++failures; }
}
#define CHECK(value) check(bool(value), #value, __LINE__)
void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) throw std::runtime_error("cannot create test fixture");
}
struct Script {
    std::vector<uint8_t> bytes{1,0,0,0,3,0,0,0,0,0,0xB6,0xAA,0};
    void word(uint16_t value) { bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8)); }
    void camera(uint16_t scale) { bytes.push_back(0x9D); word(0); word(scale); word(uint16_t(-100)); word(300); }
    void text(const std::string& text) { bytes.push_back(0x6B); for (unsigned char ch : text) bytes.push_back(uint8_t(ch - 0x20)); bytes.push_back(0); }
    void words(uint8_t op, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
        bytes.push_back(op); word(a); word(b); word(c); word(d);
    }
    void string(uint8_t op, const std::string& value) {
        bytes.push_back(op); bytes.insert(bytes.end(), value.begin(), value.end()); bytes.push_back(0);
    }
    void slot(uint8_t index, const std::string& value) {
        bytes.push_back(0x3C); bytes.push_back(index); bytes.insert(bytes.end(), value.begin(), value.end()); bytes.push_back(0);
    }
};
// Tiny literal-only CRX fixture: RGB quadrants expose framing and stale layers
// without depending on proprietary assets or another image library.
void write_image(const fs::path& path, int width, int height) {
    std::vector<uint8_t> bytes{'C','R','X','G',0,0,0,0};
    auto word = [&](uint16_t value) { bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8)); };
    word(uint16_t(width)); word(uint16_t(height)); word(1); word(0); word(0); word(0);
    const int stride = (width * 3 + 3) & ~3;
    std::vector<uint8_t> pixels(size_t(stride) * height, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const size_t at = size_t(y) * stride + x * 3;
        pixels[at] = uint8_t(y < height / 2 ? 40 : 240);
        pixels[at + 1] = 50;
        pixels[at + 2] = uint8_t(x < width / 2 ? 240 : 40);
    }
    for (size_t i = 0; i < pixels.size(); i += 8) {
        bytes.push_back(255);
        bytes.insert(bytes.end(), pixels.begin() + i, pixels.begin() + std::min(i + 8, pixels.size()));
    }
    write_file(path, bytes);
}
void fixture_round_trip(const fs::path& root) {
    Script script;
    write_image(root / "GRP/BG/test.CRX", 64, 36);
    script.slot(1, "BG\\test"); script.string(0x49, "BG\\test");
    script.camera(100); script.text("FIRST\nLINE"); script.camera(160); script.text("SECOND"); script.camera(100); script.text("THIRD"); script.bytes.push_back(0);
    write_file(root / "MES/fixture.mes", script.bytes);
    std::vector<uint8_t> names(236, 0); names[0] = 7;
    write_file(root / "GRP/NAMES.DAT", names);
    dc4scene::SceneRunner scene(root.string());
    CHECK(scene.load_script((root / "MES/fixture.mes").string()));
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == "FIRST\nLINE");
    CHECK(scene.visible_text_length() == 9); CHECK(scene.camera_scale() == 1);
    scene.draw(-1); const auto normal_frame = scene.frame();
    CHECK(scene.advance_to_next_text()); CHECK(scene.camera_scale() == 1.6f);
    scene.draw(-1); const auto frame = scene.frame();
    CHECK(frame != normal_frame);
    scene.draw(-1); CHECK(scene.frame() == frame); // cached scene is identical
    const auto save = root / "saves/roundtrip.dc4sav";
    CHECK(scene.save_state(save.string()));
    CHECK(scene.advance_to_next_text()); CHECK(scene.camera_scale() == 1);
    scene.draw(-1); CHECK(scene.frame() == normal_frame);
    CHECK(scene.load_state(save.string())); CHECK(scene.current_line() == "SECOND"); CHECK(scene.camera_scale() == 1.6f);
    scene.draw(-1); CHECK(scene.frame() == frame);
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == "THIRD");
    // Truncation/corruption must not erase or advance the live scene.
    std::ifstream input(save, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    auto damaged = bytes; damaged.back() ^= 1;
    write_file(root / "saves/corrupt.dc4sav", damaged);
    CHECK(!scene.load_state((root / "saves/corrupt.dc4sav").string()));
    CHECK(scene.current_line() == "THIRD"); CHECK(scene.camera_scale() == 1);
    for (size_t length : {size_t(0), size_t(8), size_t(23), bytes.size() - 1}) {
        write_file(root / "saves/truncated.dc4sav", {bytes.begin(), bytes.begin() + length});
        CHECK(!scene.load_state((root / "saves/truncated.dc4sav").string())); CHECK(scene.current_line() == "THIRD");
    }
}
void graphics_and_text_regressions(const fs::path& root) {
    // A small off-centre character exposes the native placement model: the
    // 0x8B place stores the zoom anchor (tgt) and the mode-0 op the current
    // position (cur); the canvas centre lands at cur + tgt*(1-s) + frame*s.
    write_image(root / "GRP/BST/sprite.CRX", 64, 36);
    Script script;
    script.slot(4, "BST\\sprite"); script.words(0x8B, 100, 640, 360, 16);
    script.words(0x8B, 640, 360, 0, 0);
    script.text("SPRITE"); script.camera(160); script.text("ZOOM");
    script.slot(4, "-"); script.text("HIDDEN");
    script.slot(4, "--"); script.text("REUSED");
    script.string(0x49, ":n"); script.slot(1, "BG\\test");
    script.string(0x49, "BG\\test"); script.text("COMMIT");
    script.string(0x49, ":r"); script.slot(1, "BG\\test");
    script.slot(4, "--"); script.string(0x49, "BG\\test"); script.text("QUEUED");
    script.words(0x8B, 0, 0, 0, 128); script.text("OPCODE_HIDE");
    script.slot(4, "--"); script.words(0x8B, 100, uint16_t(-10), 360, 16); script.text("SIGNED");
    script.bytes.push_back(0);
    write_file(root / "MES/graphics.mes", script.bytes);
    dc4scene::SceneRunner scene(root.string()); scene.set_message_visible(false);
    CHECK(scene.load_script((root / "MES/graphics.mes").string()));
    CHECK(scene.advance_to_next_text()); CHECK(scene.sprite(0).visible);
    scene.draw(-1);
    // cur(640,360) + tgt(640,360)*(1-1) + frame(32,18)*1 -> TL (640,360).
    CHECK(scene.frame()[(size_t(360) * 1280 + 640) * 4] > 200);
    CHECK(scene.frame()[(size_t(360) * 1280 + 639) * 4] == 0);
    CHECK(scene.advance_to_next_text()); CHECK(scene.camera_scale() == 1.6f);
    scene.draw(-1);
    // Background pivot412,588 -> character pivot432,528. The native affine
    // composition moves the sprite's top-left 640,360 to 765,259 at scale1.6.
    CHECK(scene.frame()[(size_t(259) * 1280 + 765) * 4] > 200);
    CHECK(scene.frame()[(size_t(259) * 1280 + 764) * 4] == 0);
    const auto zoom = scene.frame();
    CHECK(scene.save_state((root / "saves/sprite.dc4sav").string()));
    CHECK(scene.advance_to_next_text()); CHECK(!scene.sprite(0).visible);
    CHECK(scene.load_state((root / "saves/sprite.dc4sav").string())); scene.draw(-1);
    CHECK(scene.frame() == zoom);
    CHECK(scene.advance_to_next_text()); CHECK(!scene.sprite(0).visible);
    CHECK(scene.advance_to_next_text()); CHECK(scene.sprite(0).visible);
    CHECK(scene.advance_to_next_text()); CHECK(!scene.sprite(0).visible); CHECK(scene.camera_scale() == 1.6f);
    CHECK(scene.advance_to_next_text()); CHECK(scene.sprite(0).visible); CHECK(scene.camera_scale() == 1);
    CHECK(scene.advance_to_next_text()); CHECK(!scene.sprite(0).visible);
    CHECK(scene.advance_to_next_text()); CHECK(scene.sprite(0).x == -10);

#ifdef _WIN32
    dc4scene::FontRenderer font;
    CHECK(font.load("C:/Windows/Fonts/msgothic.ttc", 0, 22));
    Script text;
    text.text("@b1@t20@w1@bt1@d1HELLO");
    text.text("@crA@cgB@cbC@c2D@cwE");
    text.text("@lAAAA@zB@mC");
    text.text("@wA@mB"); text.bytes.push_back(0);
    write_file(root / "MES/richtext.mes", text.bytes);
    dc4scene::SceneRunner rich(root.string()); rich.set_font(&font);
    CHECK(rich.load_script((root / "MES/richtext.mes").string()));
    CHECK(rich.advance_to_next_text()); CHECK(rich.visible_text() == u"HELLO"); CHECK(rich.visible_text_length() == 5);
    CHECK(rich.advance_to_next_text()); CHECK(rich.visible_text() == u"ABCDE"); rich.draw(-1);
    const auto colours = rich.frame();
    bool red = false, green = false, blue = false, palette_red = false, white = false;
    for (int y = 560; y < 600; ++y) for (int x = 283; x < 338; ++x) {
        const size_t at = (size_t(y) * 1280 + x) * 4;
        const auto r = colours[at], g = colours[at + 1], b = colours[at + 2];
        if (x < 294 && r > 180 && g < 20 && b < 20) red = true;
        if (x >= 294 && x < 305 && g > 180 && r < 20 && b < 20) green = true;
        if (x >= 305 && x < 316 && b > 180 && r < 20 && g < 20) blue = true;
        if (x >= 316 && x < 327 && r > 180 && g < 20 && b < 20) palette_red = true;
        if (x >= 327 && r > 180 && g > 180 && b > 180) white = true;
    }
    CHECK(red && green && blue && palette_red && white);
    // Reveal counts printed characters only; hidden commands take no time.
    rich.draw(1);
    CHECK(rich.frame()[(size_t(582) * 1280 + 310) * 4 + 2] == 0);
    CHECK(rich.advance_to_next_text()); rich.draw(-1);
    bool large = false, beyond_default_width = false;
    for (int y = 560; y < 596; ++y) for (int x = 283; x < 365; ++x) {
        const auto r = rich.frame()[(size_t(y) * 1280 + x) * 4];
        if (y < 576 && r > 180) large = true;
        if (x > 339 && r > 180) beyond_default_width = true;
    }
    CHECK(large && beyond_default_width);
    CHECK(rich.advance_to_next_text()); CHECK(rich.visible_text() == u"AB"); rich.draw(-1);
    bool wide = false;
    for (int y = 570; y < 596; ++y) for (int x = 305; x < 327; ++x)
        if (rich.frame()[(size_t(y) * 1280 + x) * 4] > 180) wide = true;
    CHECK(wide);
#endif
}
void animation_regressions(const fs::path& root) {
    Script script;
    script.slot(1, "BG\\test"); script.string(0x49, "BG\\test");
    script.text("INITIAL");
    script.words(0x99, 5, 0, 0, 0); script.words(0x9D, 1, 160, uint16_t(-100), 300); script.text("ANIMATED");
    script.words(0x9D, 0, 130, 0, 0); script.text("IMMEDIATE");
    script.words(0x99, 0, 0, 0, 0); script.words(0x9D, 1, 180, 0, 0); script.text("ZERO_DURATION");
    script.words(0x99, 1, 0, 0, 0);
    script.words(0x9D, 1, 140, 40, 20); script.words(0x9D, 1, 120, 80, 40); script.text("QUEUED");
    script.words(0x99, 5, 0, 0, 0); script.words(0x9D, 1, 150, 0, 0); script.text("RETURN_TEST");
    script.words(0x9D, 1, 100, 200, 300); script.text("RETURN"); script.bytes.push_back(0);
    write_file(root / "MES/animation.mes", script.bytes);
    dc4scene::SceneRunner scene(root.string()); scene.set_message_visible(false);
    scene.set_animation_enabled(true);
    CHECK(scene.load_script((root / "MES/animation.mes").string()));
    CHECK(scene.advance_to_next_text()); scene.draw(-1); const auto initial = scene.frame();
    CHECK(scene.advance_to_next_text()); CHECK(scene.animating()); CHECK(scene.camera_scale() == 1);
    scene.update(500); CHECK(std::abs(scene.camera_scale() - 1.3f) < 0.0001f);
    scene.draw(-1); const auto midway = scene.frame(); CHECK(midway != initial);
    CHECK(scene.save_state((root / "saves/animation.dc4sav").string()));
    scene.update(500); CHECK(!scene.animating()); CHECK(scene.camera_scale() == 1.6f);
    scene.draw(-1); const auto target = scene.frame(); CHECK(target != midway);
    CHECK(scene.load_state((root / "saves/animation.dc4sav").string()));
    CHECK(scene.animating()); CHECK(std::abs(scene.camera_scale() - 1.3f) < 0.0001f);
    scene.draw(-1); CHECK(scene.frame() == midway);
    scene.update(500); scene.draw(-1); CHECK(scene.frame() == target); CHECK(!scene.animating());
    CHECK(scene.advance_to_next_text()); CHECK(!scene.animating()); CHECK(scene.camera_scale() == 1.3f);
    CHECK(scene.advance_to_next_text()); CHECK(!scene.animating()); CHECK(scene.camera_scale() == 1.8f);
    CHECK(scene.advance_to_next_text()); CHECK(scene.animating());
    scene.update(300); CHECK(std::abs(scene.camera_scale() - 1.3f) < 0.0001f); CHECK(scene.animating());
    CHECK(scene.save_state((root / "saves/queued.dc4sav").string()));
    scene.update(100); CHECK(scene.camera_scale() == 1.2f); CHECK(!scene.animating());
    CHECK(scene.load_state((root / "saves/queued.dc4sav").string()));
    scene.update(100); CHECK(scene.camera_scale() == 1.2f); CHECK(!scene.animating());
    CHECK(scene.advance_to_next_text()); scene.finish_animations(); CHECK(scene.camera_scale() == 1.5f);
    CHECK(scene.advance_to_next_text()); scene.update(1000); CHECK(scene.camera_scale() == 1); CHECK(!scene.animating());
    scene.draw(-1); CHECK(scene.frame() == initial);
    // Headless callers settle animated effects, including loaded in-flight saves.
    dc4scene::SceneRunner headless(root.string()); headless.set_message_visible(false);
    CHECK(headless.load_state((root / "saves/animation.dc4sav").string()));
    CHECK(!headless.animating()); CHECK(headless.camera_scale() == 1.6f);
    headless.draw(-1); CHECK(headless.frame() == target);
    // Opting out settles immediately; a later advance cannot retain stale work.
    CHECK(scene.load_state((root / "saves/animation.dc4sav").string()));
    scene.set_animation_enabled(false); CHECK(!scene.animating()); CHECK(scene.camera_scale() == 1.6f);
}
void memory_snapshot_regressions(const fs::path& root) {
    Script script;
    script.slot(1, "BG\\test"); script.string(0x49, "BG\\test");
    script.camera(140); script.text("CAPTURED");
    script.camera(180); script.text("LIVE"); script.bytes.push_back(0);
    write_file(root / "MES/memory.mes", script.bytes);
    dc4scene::SceneRunner scene(root.string()); scene.set_message_visible(false);
    CHECK(scene.load_script((root / "MES/memory.mes").string())); CHECK(scene.advance_to_next_text());
    scene.draw(-1); const auto captured_frame = scene.frame();
    std::vector<uint8_t> snapshot; std::string error = "old error";
    CHECK(scene.capture_state(&snapshot, &error)); CHECK(error.empty()); CHECK(snapshot.size() > 24);
    CHECK(scene.advance_to_next_text()); scene.draw(-1); const auto live_frame = scene.frame();
    CHECK(live_frame != captured_frame); CHECK(scene.current_line() == "LIVE");
    std::vector<uint8_t> live; CHECK(scene.capture_state(&live, &error));
    const auto unchanged_after_failure = [&](const std::vector<uint8_t>& broken) {
        CHECK(!scene.restore_state(broken, &error)); CHECK(!error.empty());
        CHECK(scene.current_line() == "LIVE"); CHECK(scene.camera_scale() == 1.8f);
        scene.draw(-1); CHECK(scene.frame() == live_frame);
        std::vector<uint8_t> after; CHECK(scene.capture_state(&after)); CHECK(after == live);
    };
    unchanged_after_failure({});
    auto corrupt = snapshot; corrupt.back() ^= 1; unchanged_after_failure(corrupt);
    // Recompute the envelope so these corruptions exercise late parser checks,
    // after the candidate runner has already reloaded scripts and images.
    const auto reseal = [](std::vector<uint8_t>& bytes) {
        const uint32_t length = uint32_t(bytes.size() - 24);
        for (int i = 0; i < 4; ++i) bytes[12 + i] = uint8_t(length >> (i * 8));
        uint64_t hash = 14695981039346656037ull;
        for (size_t i = 24; i < bytes.size(); ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
        for (int i = 0; i < 8; ++i) bytes[16 + i] = uint8_t(hash >> (i * 8));
    };
    auto invalid_flag = snapshot; invalid_flag[invalid_flag.size() - 5] = 2; reseal(invalid_flag);
    unchanged_after_failure(invalid_flag);
    auto oversized_chat = snapshot; oversized_chat[oversized_chat.size() - 4] = 201; reseal(oversized_chat);
    unchanged_after_failure(oversized_chat);
    auto trailing_data = snapshot; trailing_data.push_back(1); reseal(trailing_data);
    unchanged_after_failure(trailing_data);
    // A save from another revision of the same-named script must also fail
    // without affecting the currently running instance and its resources.
    auto revised = script.bytes; revised[revised.size() - 3] ^= 1;
    write_file(root / "MES/memory.mes", revised);
    unchanged_after_failure(snapshot);
    write_file(root / "MES/memory.mes", script.bytes);
    CHECK(scene.restore_state(snapshot, &error)); CHECK(error.empty()); CHECK(scene.current_line() == "CAPTURED");
    CHECK(scene.camera_scale() == 1.4f); scene.draw(-1); CHECK(scene.frame() == captured_frame);
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == "LIVE");
    CHECK(!scene.capture_state(nullptr, &error)); CHECK(!error.empty()); CHECK(scene.current_line() == "LIVE");
    // An uninitialized runner cannot overwrite an existing caller snapshot.
    dc4scene::SceneRunner empty(root.string()); std::vector<uint8_t> sentinel{1,2,3};
    CHECK(!empty.capture_state(&sentinel, &error)); CHECK(sentinel == std::vector<uint8_t>({1,2,3}));
}
void font_regression() {
#ifdef _WIN32
    dc4scene::FontRenderer font;
    if (!font.load("C:/Windows/Fonts/msgothic.ttc", 0, 22)) { std::cout << "MS Gothic unavailable; GDI test skipped\n"; return; }
    std::vector<uint8_t> blank(256 * 64 * 4, 0);
    font.rasterize_line(u" \u3000 ", 2, 30, &blank, 256, 64, 22);
    CHECK(std::all_of(blank.begin(), blank.end(), [](uint8_t b) { return b == 0; }));
    font.rasterize_line(u"\u3000A\u732b", 2, 30, &blank, 256, 64, 22);
    size_t whites = 0;
    for (size_t i = 0; i < blank.size(); i += 4) if (blank[i] > 220 && blank[i + 3] > 200) ++whites;
    CHECK(whites > 20);
#endif
}
void game_round_trip(const fs::path& data, const fs::path& root) {
    dc4scene::FontRenderer font;
    CHECK(font.load("C:/Windows/Fonts/msgothic.ttc", 0, 22));
    dc4scene::SceneRunner scene(data.string()); scene.set_font(&font);
    CHECK(scene.load_sheets((data / "MES/start.mes").string()));
    CHECK(scene.load_ui_script((data / "MES/win_size.mes").string()));
    CHECK(scene.load_script((data / "MES/dc4_kyo20190204a1.mes").string()));
    for (int i = 0; i < 24; ++i) CHECK(scene.advance_to_next_text());
    CHECK(scene.camera_scale() == 1.4f);
    CHECK(scene.advance_to_next_text()); CHECK(scene.camera_scale() == 1.6f);
    CHECK(scene.visible_text().find(u"\u732b") != std::u16string::npos);
    scene.set_wait_marker(true); scene.draw(-1); const auto frame = scene.frame();
    CHECK(scene.save_state((root / "reference.dc4sav").string()));
    CHECK(scene.advance_to_next_text()); CHECK(scene.camera_scale() == 1.0f);
    const auto next_line = scene.current_line();
    CHECK(scene.load_state((root / "reference.dc4sav").string())); scene.draw(-1);
    CHECK(scene.frame() == frame); CHECK(scene.camera_scale() == 1.6f);
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == next_line);
    int lines = 0;
    while (lines++ < 40 && scene.advance_to_next_text()) { if (scene.speaker().size() && lines > 20) break; }
    CHECK(scene.last_error().empty()); CHECK(!scene.scene_ended());

    dc4scene::SceneRunner choice(data.string()); choice.set_font(&font);
    CHECK(choice.load_sheets((data / "MES/start.mes").string()));
    CHECK(choice.load_script((data / "MES/dc4_dc99990101a.mes").string()));
    lines = 0; while (lines++ < 200 && choice.advance_to_next_text()) {}
    CHECK(choice.waiting_for_choice()); CHECK(choice.choices().size() == 2);
    choice.draw(-1); const auto choice_frame = choice.frame();
    CHECK(choice.save_state((root / "choice.dc4sav").string()));
    CHECK(choice.choose(0)); CHECK(choice.advance_to_next_text()); const auto branch1 = choice.current_line();
    CHECK(choice.load_state((root / "choice.dc4sav").string())); CHECK(choice.waiting_for_choice());
    choice.draw(-1); CHECK(choice.frame() == choice_frame);
    CHECK(choice.choose(1)); CHECK(choice.advance_to_next_text()); CHECK(choice.current_line() != branch1);
}
}
void chip_regressions(const fs::path& root) {
    // 0x3C slot 19 registers a chip sheet under the raw slot id (it must not
    // render as a full-screen layer); 0x7A + 0x77 blits one chip top-left
    // anchored at 1:1; a background commit clears the retained chips.
    write_image(root / "GRP/SYSTEM/chip.CRX", 40, 30);
    write_image(root / "GRP/BG/after.CRX", 64, 36);
    Script script;
    script.slot(19, "SYSTEM\\chip");
    script.text("NOSHEET");
    script.words(0x7A, 100, 400, 0, 0);
    script.words(0x77, 20, 10, 19, 1);
    script.text("CHIP");
    script.slot(1, "BG\\after"); script.string(0x49, "BG\\after");
    script.text("CLEARED");
    script.bytes.push_back(0);
    write_file(root / "MES/chips.mes", script.bytes);
    dc4scene::SceneRunner scene(root.string()); scene.set_message_visible(false);
    CHECK(scene.load_script((root / "MES/chips.mes").string()));
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == "NOSHEET");
    scene.draw(-1);
    const uint8_t* bare = &scene.frame()[(size_t(600) * 1280 + 1100) * 4];
    CHECK(bare[0] == 0 && bare[1] == 0 && bare[2] == 0);
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == "CHIP");
    scene.draw(-1);
    // The 24bpp fixture decodes BGR, so the sheet's top-left quadrant reads
    // (240,50,40) here.
    const uint8_t* chip = &scene.frame()[(size_t(405) * 1280 + 105) * 4];
    CHECK(chip[0] == 240 && chip[1] == 50 && chip[2] == 40);
    CHECK(scene.advance_to_next_text()); CHECK(scene.current_line() == "CLEARED");
    scene.draw(-1);
    const uint8_t* cleared = &scene.frame()[(size_t(405) * 1280 + 105) * 4];
    CHECK(cleared[0] == 240 && cleared[1] == 50 && cleared[2] == 240);
}
int main(int argc, char** argv) {
    try {
        const fs::path root = fs::current_path() / "scene-test-data" /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fixture_round_trip(root); graphics_and_text_regressions(root); chip_regressions(root); animation_regressions(root); memory_snapshot_regressions(root); font_regression();
        if (argc > 1) game_round_trip(argv[1], root);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    std::cout << (failures ? "Scene regressions FAILED\n" : "Scene regressions passed\n");
    return failures ? 1 : 0;
}
