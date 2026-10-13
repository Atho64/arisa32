#include "dc4/log_screen.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool ok, const char* expression, int line) {
    if (!ok) { std::cerr << line << ": " << expression << '\n'; ++failures; }
}
#define CHECK(x) check(bool(x), #x, __LINE__)
using Color = std::array<uint8_t, 3>;
Color atlas(int x, int y) { return {uint8_t(x % 251), uint8_t(y % 251), uint8_t((x + y) % 251)}; }
void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot write log fixture");
}
void crx(const fs::path& path, int w, int h, bool grid, bool corners = false) {
    std::vector<uint8_t> bytes{'C','R','X','G',0,0,0,0};
    auto word = [&](int value) { bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8)); };
    word(w); word(h); word(1); word(0); word(0); word(0);
    const int stride = (w * 3 + 3) & ~3;
    std::vector<uint8_t> pixels(size_t(stride) * h, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const Color c = grid ? atlas(x,y) : corners
            ? Color{uint8_t(x < w/2 ? 240 : 20), 50, uint8_t(y < h/2 ? 30 : 220)}
            : Color{10,200,70};
        const size_t at = size_t(y) * stride + x * 3;
        pixels[at] = c[2]; pixels[at+1] = c[1]; pixels[at+2] = c[0];
    }
    for (size_t at = 0; at < pixels.size(); at += 8) {
        bytes.push_back(255);
        bytes.insert(bytes.end(), pixels.begin()+at, pixels.begin()+std::min(at+8,pixels.size()));
    }
    write(path,bytes);
}
Color pixel(const std::vector<uint8_t>& frame, int x, int y, int w = 1280) {
    const size_t at = (size_t(y) * w + x) * 4;
    return {frame.at(at),frame.at(at+1),frame.at(at+2)};
}
struct Script {
    std::vector<uint8_t> bytes{1,0,0,0,3,0,0,0,0,0,0xB6,0xAA,0};
    void string(uint8_t op, const std::string& s) {
        bytes.push_back(op); bytes.insert(bytes.end(),s.begin(),s.end()); bytes.push_back(0);
    }
    void slot(uint8_t op, uint8_t id, const std::string& s) { bytes.push_back(op); string(id,s); }
    void text(const std::string& s) {
        bytes.push_back(0x6B); for (unsigned char c : s) bytes.push_back(uint8_t(c-0x20)); bytes.push_back(0);
    }
};
void text_layout() {
    const std::u16string first = u"「いけないイタズラ、しちゃいますよ？　それでも良い";
    const std::u16string second = u"　んですか？」";
    const auto lines = dc4ui::LogScreen::text_lines(first + u"\r\n" + second, false);
    CHECK(lines == (std::vector<std::u16string>{first,second}));
    CHECK(dc4ui::LogScreen::text_lines(u"A\n\nB",false) == (std::vector<std::u16string>{u"A",u"",u"B"}));
    CHECK(dc4ui::LogScreen::text_lines(u"Hello\nworld",true) == (std::vector<std::u16string>{u"Hello world"}));
    CHECK(dc4ui::LogScreen::text_lines(u"Halo\n dunia",true) == (std::vector<std::u16string>{u"Halo dunia"}));
    const std::u16string long_jp(70,u'あ');
    const auto wrapped = dc4ui::LogScreen::text_lines(long_jp,false);
    std::u16string joined;
    for (const auto& line : wrapped) { CHECK(line.size() <= 32); joined += line; }
    CHECK(joined == long_jp);
    CHECK(dc4ui::LogScreen::text_lines(u"",false).empty());
}
void row_art(const fs::path& root) {
    dc4ui::LogScreen log;
    CHECK(log.load(root.string()));
    dc4ui::LogRow row;
    row.name = dc4scene::sjis_to_utf16("\x93\xf1\x94\x54"); // Nino, face cell 3.
    row.thumb.assign(128 * 72 * 4, 255);
    for (size_t i = 0; i < row.thumb.size(); i += 4) { row.thumb[i]=240; row.thumb[i+1]=10; row.thumb[i+2]=30; }
    const dc4scene::FontRenderer no_font;
    std::vector<uint8_t> normal(1280*720*4,255), hover=normal, disabled=normal;
    log.draw({row},0,true,-1,-1,no_font,&normal);
    log.draw({row},0,true,350,120,no_font,&hover);
    log.draw({row},0,false,350,120,no_font,&disabled);
    // Portrait/name remain the same even while a preview is visible.
    for (int y=100;y<220;++y) for (int x=20;x<170;++x)
        CHECK(pixel(normal,x,y)==pixel(hover,x,y));
    CHECK(pixel(hover,40,122)==atlas(473,392));
    CHECK(pixel(hover,184,120)==(Color{240,10,30}));
    CHECK(pixel(hover,311,191)==(Color{240,10,30}));
    CHECK(pixel(hover,183,120)==pixel(normal,183,120));
    CHECK(pixel(hover,312,191)==pixel(normal,312,191));
    CHECK(pixel(disabled,184,120)==pixel(normal,184,120));
    CHECK(pixel(disabled,40,122)==pixel(normal,40,122));
    // Scrolling/hover must select the right row's preview, including narration.
    dc4ui::LogRow narration;
    narration.thumb.assign(128*72*4,255);
    auto scrolled=normal;
    log.draw({row,narration},1,true,350,120,no_font,&scrolled);
    CHECK(pixel(scrolled,184,120)==(Color{255,255,255}));
    CHECK(pixel(scrolled,40,122)==atlas(20,246));
}
void capture(const fs::path& root) {
    crx(root/"GRP/BG/second.CRX",256,144,false,true);
    Script ui;
    ui.slot(0x3D,0,"SYSTEM\\BKLOGBACK"); ui.bytes.push_back(0);
    write(root/"MES/ui.mes",ui.bytes);
    Script script;
    script.slot(0x3C,1,"SYSTEM\\BKLOGBACK"); script.string(0x49,"SYSTEM\\BKLOGBACK"); script.text("FIRST");
    script.slot(0x3C,1,"BG\\second"); script.string(0x49,"BG\\second"); script.text("SECOND\nLINE");
    script.bytes.push_back(0);
    write(root/"MES/scene.mes",script.bytes);
    dc4scene::SceneRunner scene(root.string());
    CHECK(scene.load_sheets((root/"MES/ui.mes").string()));
    scene.set_window_opacity(100);
    CHECK(scene.load_script((root/"MES/scene.mes").string()));
    CHECK(scene.advance_to_next_text()); scene.draw(-1);
    CHECK(scene.advance_to_next_text()); // No draw: frame still belongs to FIRST.
    const auto pc=scene.script_position();
    const auto row=dc4ui::capture_log_row(scene,7);
    CHECK(row.line==7 && row.text==u"SECOND\nLINE" && !row.snapshot.empty());
    CHECK(scene.script_position()==pc && scene.message_visible());
    CHECK(row.thumb.size()==128*72*4);
    CHECK(pixel(row.thumb,0,0,128)==(Color{240,50,30}));
    CHECK(pixel(row.thumb,127,0,128)==(Color{20,50,30}));
    CHECK(pixel(row.thumb,0,71,128)==(Color{240,50,220}));
    CHECK(pixel(row.thumb,127,71,128)==(Color{20,50,220}));
    // A fresh ordinary render contains the green message window, but the
    // preview includes the scene underneath it (and all four canvas corners).
    scene.draw(-1);
    CHECK(pixel(scene.frame(),1270,710)==(Color{10,200,70}));
    CHECK(scene.capture_thumbnail(0,72).empty());
}
}
int main() {
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("dc4-log-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (!fs::create_directory(root)) throw std::runtime_error("Cannot create isolated log fixture");
        crx(root/"GRP/SYSTEM/BKLOGBACK.CRX",1280,720,false);
        crx(root/"GRP/SYSTEM/BKLOGCHIP.CRX",1224,854,true);
        text_layout(); row_art(root); capture(root);
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; ++failures; }
    std::error_code ec;
    if (fs::weakly_canonical(root,ec)==root && !ec && root.parent_path()==parent)
        fs::remove_all(root,ec);
    std::cout<<(failures?"Log regressions FAILED\n":"Log regressions passed\n");
    return failures ? 1 : 0;
}

