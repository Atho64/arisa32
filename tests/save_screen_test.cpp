#include "dc4/save_screen.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using dc4ui::SaveActionType;
using dc4ui::SaveMode;
using dc4ui::SaveScreen;
namespace {
int failures = 0;
void check(bool value, const char* expression, int line) {
    if (!value) { std::cerr << line << ": " << expression << '\n'; ++failures; }
}
#define CHECK(value) check(bool(value), #value, __LINE__)

bool is_action(dc4ui::SaveAction a, SaveActionType type, int index = -1) {
    return a.type == type && a.index == index;
}
dc4ui::SaveAction click(SaveScreen& screen, int x, int y) {
    screen.pointer_down(x, y);
    return screen.pointer_up(x, y);
}

void geometry_and_navigation() {
    SaveScreen screen;
    CHECK(screen.page() == 0);
    CHECK(screen.mode() == SaveMode::Save);
    // Native 0x7E rows enumerate down the left, then down the right column.
    CHECK(screen.slot_hit(152, 95) == 0);
    CHECK(screen.slot_hit(601, 200) == 0);
    CHECK(screen.slot_hit(152, 563) == 4);
    CHECK(screen.slot_hit(617, 95) == 5);
    CHECK(screen.slot_hit(1066, 668) == 9);
    CHECK(screen.slot_hit(602, 95) == -1);
    CHECK(screen.slot_hit(616, 95) == -1);
    CHECK(screen.slot_hit(152, 201) == -1);
    CHECK(screen.slot_hit(152, 211) == -1);
    CHECK(screen.slot_hit(1067, 669) == -1);

    std::set<int> all_slots;
    for (int page = 0; page < 10; ++page) {
        screen.set_page(page);
        for (int row = 0; row < 10; ++row) {
            const int slot = screen.slot_hit(row < 5 ? 200 : 700, 100 + (row % 5) * 117);
            CHECK(slot == SaveScreen::slot_index(page, row));
            all_slots.insert(slot);
        }
    }
    CHECK(all_slots.size() == 100);
    CHECK(*all_slots.begin() == 0);
    CHECK(*all_slots.rbegin() == 99);
    CHECK(screen.slot_hit(617, 563) == 99);
    CHECK(is_action(click(screen, 1240, 40), SaveActionType::Page, 0));
    CHECK(screen.page() == 0);
    CHECK(is_action(click(screen, 780, 40), SaveActionType::Page, 9));
    CHECK(screen.page() == 9);
    CHECK(is_action(click(screen, 810, 30), SaveActionType::Page, 0));
    CHECK(is_action(click(screen, 810, 30), SaveActionType::None));
    CHECK(is_action(click(screen, 850, 700), SaveActionType::Mode, 1));
    CHECK(screen.mode() == SaveMode::Load);
    CHECK(screen.page() == 0);
    CHECK(is_action(click(screen, 850, 700), SaveActionType::None));
    CHECK(is_action(click(screen, 730, 700), SaveActionType::Mode, 0));
    CHECK(is_action(screen.hit_test(1279, 683), SaveActionType::Back));
    CHECK(is_action(screen.hit_test(1280, 683), SaveActionType::None));
    CHECK(is_action(screen.hit_test(-1, 40), SaveActionType::None));
    CHECK(is_action(screen.hit_test(800, 720), SaveActionType::None));

    screen.set_page(INT_MAX); CHECK(screen.page() == 9);
    screen.set_page(INT_MIN); CHECK(screen.page() == 0);
    screen.change_page(-1); CHECK(screen.page() == 9);
    screen.change_page(INT_MAX); CHECK(screen.page() == 6);
    screen.change_page(INT_MIN); CHECK(screen.page() == 8);
    for (int invalid : {-1, 10, INT_MIN, INT_MAX}) {
        CHECK(SaveScreen::slot_index(invalid, 0) == -1);
        CHECK(SaveScreen::slot_index(0, invalid) == -1);
    }
}

void input_lifecycle() {
    SaveScreen screen;
    CHECK(is_action(screen.pointer_up(200, 100), SaveActionType::None));
    screen.pointer_down(200, 100);
    CHECK(is_action(screen.pointer_up(700, 100), SaveActionType::None));
    screen.pointer_down(200, 100);
    screen.cancel_pointer();
    CHECK(is_action(screen.pointer_up(200, 100), SaveActionType::None));
    screen.pointer_down(200, 100);
    screen.set_page(1);
    CHECK(is_action(screen.pointer_up(200, 100), SaveActionType::None));
    screen.set_page(0);
    CHECK(is_action(click(screen, 200, 100), SaveActionType::Slot, 0));
    CHECK(is_action(screen.pointer_up(200, 100), SaveActionType::None));

    // Two controls with the same Page action must not share an armed click.
    screen.pointer_down(1240, 40);
    CHECK(is_action(screen.pointer_up(850, 40), SaveActionType::None));
    CHECK(screen.page() == 0);
    screen.pointer_down(850, 40);
    CHECK(is_action(screen.pointer_up(1240, 40), SaveActionType::None));
    CHECK(screen.page() == 0);

    screen.set_mode(SaveMode::Load);
    CHECK(screen.slot_hit(200, 100) == 0);  // Geometry exists, action does not.
    CHECK(is_action(click(screen, 200, 100), SaveActionType::None));
    screen.set_slot_occupied(0, true);
    CHECK(is_action(click(screen, 200, 100), SaveActionType::Slot, 0));
    screen.pointer_down(200, 100);
    screen.set_slot_occupied(0, false);
    CHECK(is_action(screen.pointer_up(200, 100), SaveActionType::None));
    screen.set_slot_occupied(-1, true);
    screen.set_slot_occupied(100, true);
    screen.set_slot_occupied(INT_MAX, true);
    screen.set_slot_occupied(INT_MIN, true);
    CHECK(is_action(click(screen, 200, 100), SaveActionType::None));
    screen.set_slot_occupied(99, true);
    screen.set_page(9);
    CHECK(is_action(click(screen, 700, 600), SaveActionType::Slot, 99));

    CHECK(is_action(click(screen, 360, 30), SaveActionType::None));
    CHECK(is_action(click(screen, 500, 30), SaveActionType::None));
    CHECK(is_action(click(screen, 620, 30), SaveActionType::None));
    screen.set_shortcuts(true, false, true);
    CHECK(is_action(click(screen, 360, 30), SaveActionType::None));
    CHECK(is_action(click(screen, 500, 30), SaveActionType::LatestLoad));
    CHECK(is_action(click(screen, 620, 30), SaveActionType::QuickLoad));
    screen.set_shortcuts(true, true, true);
    CHECK(is_action(click(screen, 360, 30), SaveActionType::AutoLoad));
    screen.pointer_down(620, 30);
    screen.set_shortcuts(false, false, false);
    CHECK(is_action(screen.pointer_up(620, 30), SaveActionType::None));
    screen.set_mode(SaveMode::Save);
    screen.set_shortcuts(true, true, true);
    CHECK(is_action(click(screen, 500, 30), SaveActionType::None));

    CHECK(is_action(click(screen, 950, 700), SaveActionType::None));
    CHECK(is_action(click(screen, 1100, 700), SaveActionType::None));
    CHECK(is_action(click(screen, 1200, 700), SaveActionType::None));
    screen.set_navigation(true, true, true);
    CHECK(is_action(click(screen, 950, 700), SaveActionType::Config));
    CHECK(is_action(click(screen, 1100, 700), SaveActionType::Title));
    CHECK(is_action(click(screen, 1200, 700), SaveActionType::Quit));

    screen.set_confirm_enabled(true);
    CHECK(is_action(click(screen, 1100, 700), SaveActionType::None));
    CHECK(screen.confirming());
    CHECK(is_action(click(screen, 720, 380), SaveActionType::None));
    CHECK(!screen.confirming());
    CHECK(is_action(click(screen, 1100, 700), SaveActionType::None));
    CHECK(screen.confirming());
    CHECK(is_action(click(screen, 539 + 40, 368 + 16), SaveActionType::Title));
    CHECK(!screen.confirming());
}

using Color = std::array<uint8_t, 3>;
Color atlas_color(int x, int y) {
    return {uint8_t(x % 251), uint8_t(y % 251), uint8_t((x + y) % 251)};
}
void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) throw std::runtime_error("Cannot write SaveScreen test fixture");
}
// Literal CRX, not a screenshot of the implementation: every atlas source
// coordinate has known color so tests detect crop and destination mistakes.
void write_crx(const fs::path& path, int width, int height, Color solid = {}, bool atlas = false) {
    std::vector<uint8_t> bytes{'C','R','X','G',0,0,0,0};
    auto word = [&](int v) { bytes.push_back(uint8_t(v)); bytes.push_back(uint8_t(v >> 8)); };
    word(width); word(height); word(1); word(0); word(0); word(0);
    const int stride = (width * 3 + 3) & ~3;
    std::vector<uint8_t> raw(size_t(stride) * height, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const Color c = atlas ? atlas_color(x, y) : solid;
        const size_t at = size_t(y) * stride + x * 3;
        raw[at] = c[2]; raw[at + 1] = c[1]; raw[at + 2] = c[0];
    }
    for (size_t i = 0; i < raw.size(); i += 8) {
        bytes.push_back(255);
        bytes.insert(bytes.end(), raw.begin() + i, raw.begin() + std::min(i + 8, raw.size()));
    }
    write_bytes(path, bytes);
}
bool pixel_is(const std::vector<uint8_t>& frame, int x, int y, Color expected) {
    if (frame.size() != size_t(1280) * 720 * 4) return false;
    const size_t at = (size_t(y) * 1280 + x) * 4;
    return frame[at] == expected[0] && frame[at + 1] == expected[1] &&
           frame[at + 2] == expected[2] && frame[at + 3] == 255;
}

void rendering_and_failure(const fs::path& root) {
    const auto system = root / "GRP/SYSTEM";
    write_crx(system / "saveback.crx", 1280, 720, {4, 5, 6});
    write_crx(system / "loadback.crx", 1280, 720, {7, 8, 9});
    write_crx(system / "slchip.crx", 447, 627, {}, true);
    write_crx(system / "systemgoto.crx", 530, 132, {}, true);
    SaveScreen screen;
    std::string error = "old error";
    CHECK(screen.load(root.string(), &error)); CHECK(screen.loaded()); CHECK(error.empty());
    const dc4scene::FontRenderer no_font;
    std::vector<uint8_t> frame{1,2,3};
    std::vector<dc4ui::SaveSlot> slots;
    screen.draw(slots, -1, -1, false, no_font, &frame);
    CHECK(pixel_is(frame, 0, 0, {4, 5, 6}));
    CHECK(pixel_is(frame, 152, 95, atlas_color(2, 21)));
    CHECK(pixel_is(frame, 596, 95, atlas_color(446, 21)));
    CHECK(pixel_is(frame, 597, 95, {4, 5, 6})); // Native crop clips 2px beyond source width.
    CHECK(pixel_is(frame, 166, 109, atlas_color(16, 127)));
    CHECK(pixel_is(frame, 806, 24, atlas_color(6, 250))); // Selected page1.
    CHECK(pixel_is(frame, 849, 24, atlas_color(48, 210))); // Normal page2.
    CHECK(pixel_is(frame, 1170, 638, atlas_color(10, 441)));
    screen.draw(slots, 1240, 40, false, no_font, &frame);
    CHECK(pixel_is(frame, 849, 24, atlas_color(48, 210))); // Arrow hover does not hover page2.
    screen.draw(slots, 850, 40, true, no_font, &frame);
    CHECK(pixel_is(frame, 849, 24, atlas_color(48, 250)));
    screen.draw(slots, 200, 100, false, no_font, &frame);
    CHECK(pixel_is(frame, 166, 109, atlas_color(148, 127)));

    screen.set_mode(SaveMode::Load);
    screen.draw(slots, 200, 100, true, no_font, &frame);
    CHECK(pixel_is(frame, 0, 0, {7, 8, 9}));
    CHECK(pixel_is(frame, 166, 109, atlas_color(16, 127))); // Empty LOAD stays unhighlighted.
    slots.resize(100);
    slots[0].occupied = true;
    slots[0].thumbnail.w = 128; slots[0].thumbnail.h = 72;
    slots[0].thumbnail.rgba.resize(128 * 72 * 4);
    for (int y = 0; y < 72; ++y) for (int x = 0; x < 128; ++x) {
        const size_t at = (size_t(y) * 128 + x) * 4;
        slots[0].thumbnail.rgba[at] = uint8_t(x);
        slots[0].thumbnail.rgba[at + 1] = uint8_t(y);
        slots[0].thumbnail.rgba[at + 2] = 230;
        slots[0].thumbnail.rgba[at + 3] = 255;
    }
    screen.set_slot_occupied(0, true);
    screen.draw(slots, 200, 100, false, no_font, &frame);
    CHECK(pixel_is(frame, 166, 109, atlas_color(148, 127)));
    CHECK(pixel_is(frame, 168, 111, {0, 0, 230}));
    CHECK(pixel_is(frame, 292, 181, {124, 70, 230})); // Crop, not rescale whole128x72 to125x71.
    slots[0].thumbnail.w = INT_MAX; slots[0].thumbnail.h = INT_MAX;
    slots[0].thumbnail.rgba = {1,2,3,4};
    screen.draw(slots, -1, -1, false, no_font, &frame);
    CHECK(pixel_is(frame, 168, 111, atlas_color(18, 129))); // Malformed thumbnail ignored.
    screen.set_page(9);
    slots.resize(1);
    screen.draw(slots, 700, 600, true, no_font, &frame); // Missing metadata rows are empty.
    CHECK(frame.size() == size_t(1280) * 720 * 4);

    // A failed reload must clear the previously valid art, then still draw
    // a complete usable fallback. This catches stale/partially loaded screens.
    CHECK(!screen.load((root / "missing").string(), &error));
    CHECK(!screen.loaded()); CHECK(error.find("SAVEBACK") != std::string::npos);
    screen.draw({}, -1, -1, false, no_font, &frame);
    CHECK(pixel_is(frame, 0, 0, {4, 35, 68}));
    CHECK(is_action(click(screen, 1200, 640), SaveActionType::Back));
    write_bytes(system / "slchip.crx", {'C','R','X','G'});
    CHECK(!screen.load(root.string(), &error)); CHECK(!screen.loaded());
    CHECK(error.find("SLCHIP") != std::string::npos);
    write_crx(system / "slchip.crx", 2, 2, {1,2,3});
    CHECK(!screen.load(root.string(), &error)); CHECK(!screen.loaded());
    CHECK(error.find("dimensions") != std::string::npos);
}

void installed_assets(const fs::path& data) {
    SaveScreen screen;
    std::string error;
    CHECK(screen.load(data.string(), &error));
    if (!screen.loaded()) { std::cerr << error << '\n'; return; }
    const dc4scene::FontRenderer no_font;
    std::vector<uint8_t> normal, hovered;
    screen.draw({}, -1, -1, false, no_font, &normal);
    screen.draw({}, 200, 100, false, no_font, &hovered);
    CHECK(normal != hovered);
    screen.set_mode(SaveMode::Load);
    screen.draw({}, -1, -1, false, no_font, &normal);
    screen.draw({}, 200, 100, true, no_font, &hovered);
    CHECK(normal == hovered); // Empty original-art LOAD has no hover/press change.
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path root = fs::current_path() / "save-screen-test-data" /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        geometry_and_navigation(); input_lifecycle(); rendering_and_failure(root);
        if (argc > 1) installed_assets(argv[1]);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; ++failures; }
    std::cout << (failures ? "Save screen regressions FAILED\n" : "Save screen regressions passed\n");
    return failures ? 1 : 0;
}
