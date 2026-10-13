#include "dc4/resource.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool value, const char* expression, int line) {
    if (!value) { std::cerr << line << ": " << expression << '\n'; ++failures; }
}
#define CHECK(value) check(bool(value), #value, __LINE__)
void put(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << contents;
    if (!out) throw std::runtime_error("cannot create resource fixture");
}
std::string get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void overlays(const fs::path& root) {
    const auto base = root / "Base", english = root / "English", indonesian = root / "Indonesian";
    put(base / "GRP/System/Window.CRX", "base-window");
    put(base / "MES/Opening.mes", "base-script");
    put(base / "MOVIE/Opening.MPG", "base-movie");
    put(base / "GRP/BG/Fallback.CRX", "base-fallback");
    put(base / "MUSIC/Track.dat", "base-music");
    put(base / "saves/slot1.dc4sav", "base-save");
    put(english / "grp/system/window.crx", "english-window");
    put(english / "mes/opening.MES", "english-script");
    put(english / "Movie/OPENING.mpg", "english-movie");
    put(english / "MUSIC/Track.dat", "do-not-overlay-music");
    put(english / "saves/slot1.dc4sav", "do-not-overlay-save");
    put(indonesian / "MES/Opening.mes", "indonesian-script");
    put(root / "Base-extra/MES/Opening.mes", "outside-script");
    put(root / "save-files/slot1.dc4sav", "private-save");

    dc4::configure_resources(base.u8string(), english.u8string(), "UTF-8");
    CHECK(dc4::prefers_utf8_text());
    CHECK(get(dc4::resolve_asset((base / "gRp/SYSTEM/WINDOW.crx").u8string())) == "english-window");
    CHECK(get(dc4::resolve_asset((base / "mes/OPENING.MES").u8string())) == "english-script");
    CHECK(get(dc4::resolve_asset((base / "movie/opening.MPG").u8string())) == "english-movie");
    CHECK(get(dc4::resolve_asset((base / "grp/bg/fallback.crx").u8string())) == "base-fallback");
    CHECK(get(dc4::resolve_asset((base / "music/track.DAT").u8string())) == "base-music");
    CHECK(get(dc4::resolve_asset((base / "saves/slot1.dc4sav").u8string())) == "base-save");
    CHECK(get(dc4::resolve_asset((root / "save-files/slot1.dc4sav").u8string())) == "private-save");
    CHECK(get(dc4::resolve_asset((root / "Base-extra/MES/Opening.mes").u8string())) == "outside-script");
    CHECK(dc4::asset_exists((base / "MES/opening.mes").u8string()));
    CHECK(!dc4::asset_exists((base / "MES/missing.mes").u8string()));
    CHECK(!dc4::asset_exists((base / "MES").u8string()));
    CHECK(dc4::resolve_asset("").empty());
    // Missing lookups are not cached: a subsequently imported resource appears.
    put(english / "MES/missing.mes", "new-script");
    CHECK(get(dc4::resolve_asset((base / "MES/missing.mes").u8string())) == "new-script");
    // Selecting a different language invalidates earlier cached paths.
    dc4::configure_resources(base.u8string(), indonesian.u8string(), "windows-31j");
    CHECK(!dc4::prefers_utf8_text());
    CHECK(get(dc4::resolve_asset((base / "mes/OPENING.MES").u8string())) == "indonesian-script");
    CHECK(get(dc4::resolve_asset((base / "GRP/System/Window.CRX").u8string())) == "base-window");
    dc4::configure_resources(base.u8string());
    CHECK(get(dc4::resolve_asset((base / "MES/Opening.mes").u8string())) == "base-script");
    // Prefix matching is insensitive to ASCII case, including base components.
    auto different_case = (base / "MES/Opening.mes").u8string();
    const auto at = different_case.find("Base");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) different_case.replace(at, 4, "bASE");
    dc4::configure_resources(base.u8string(), english.u8string());
    CHECK(get(dc4::resolve_asset(different_case)) == "english-script");
    dc4::configure_resources({});
    CHECK(get(dc4::resolve_asset((base / "GRP/system/window.crx").u8string())) == "base-window");
}
void utf8() {
    std::u16string out = u"old";
    CHECK(dc4::decode_utf8(u8"English / Indonesia / 日本語 / 😀", &out));
    CHECK(out == u"English / Indonesia / 日本語 / \U0001F600");
    CHECK(dc4::decode_utf8("\xef\xbb\xbf" "ABC", &out)); CHECK(out == u"ABC");
    CHECK(dc4::decode_utf8("A\xef\xbb\xbf" "B", &out)); CHECK(out == u"A\ufeffB");
    CHECK(dc4::decode_utf8("", &out)); CHECK(out.empty());
    CHECK(!dc4::decode_utf8("ASCII", nullptr));
    for (const auto& bytes : std::vector<std::string>{
            "\x80", "\xc0\xaf", "\xc1\x81", "\xc2", "\xc2" "A", "\xe2\x82",
            "\xe0\x80\x80", "\xed\xa0\x80", "\xed\xbf\xbf", "\xf0\x80\x80\x80",
            "\xf0\x9f\x98", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff", "valid\xff"}) {
        out = u"unchanged"; CHECK(!dc4::decode_utf8(bytes, &out)); CHECK(out == u"unchanged");
    }
    CHECK(dc4::decode_utf8("\xf4\x8f\xbf\xbf", &out)); CHECK(out == u"\U0010ffff");
}
}
int main() {
    const auto root = fs::temp_directory_path() / ("dc4-resource-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try { overlays(root); utf8(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; ++failures; }
    dc4::configure_resources({});
    std::error_code ignored; fs::remove_all(root, ignored);
    std::cout << (failures ? "Resource regressions FAILED\n" : "Resource regressions passed\n");
    return failures ? 1 : 0;
}
