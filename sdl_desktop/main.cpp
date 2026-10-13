// SDL desktop frontend. Game state and native artwork are owned by dc4core.
#include "dc4/audio.h"
#include "dc4/bgm.h"
#include "dc4/crx.h"
#include "dc4/scene.h"
#include "dc4/voice.h"
#include "dc4/log_screen.h"
#include "dc4/save_screen.h"
#include "dc4/system_menu.h"
#include "dc4/movie.h"
#include "dc4/resource.h"
#include "dc4/confirm_dialog.h"
#include "ui_script.h"

#include <SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr const char* kOpening = "dc4_kyo20190204a1.mes";
constexpr const char* kHelp =
    "Left click / Space / Enter: reveal or advance dialogue\n"
    "Right click / H: hide or show dialogue\n"
    "A: auto advance (waits for voice)   S / held Ctrl: skip\n"
    "V: replay voice   B / mouse wheel up: session backlog\n"
    "F5: quick save   F9: quick load; SAVE / LOAD: 100 numbered slots\n"
    "Save / Load: 1-9 / 0 select a slot; Left / Right change page; Tab switches mode\n"
    "Backlog: wheel / Up / Down / Page Up / Page Down; Esc to close\n"
    "Choices: click an option or press 1-9\n"
    "F11 / Alt+Enter: fullscreen   F1: this help\n"
    "Esc: close a screen, cancel auto/skip, or exit\n\n"
    "Quick saves belong to dc4engine; original game saves are not compatible.\n"
    "Title, Config and Extras use the installed game's original menus.";

struct Options {
    std::string data, scene, ui, shot, font, bgm, voice, panel, overlay, movie, encoding = "windows-31j", lang;
    int line = 0, reveal = -1, hover_x = -1, hover_y = -1, wait_frame = 0, effect_ms = -1;
    int save_page = 0;
    int config_page = 0;
    int width = dc4scene::kScreenW, height = dc4scene::kScreenH;
    bool title = false, down = false, no_audio = false, test_audio = false;
    bool debug = false, help = false, choice = false, confirm = false, config = false;
};

void usage() {
    printf("dc4_desktop [AdvData directory] [options]\n"
           "  --scene FILE        scenario basename or path (default: %s)\n"
           "  --line N            zero-based dialogue index in the scenario\n"
           "  --movie [FILE]      play movie directly (default: DC4_OP1.mpg, or op1/op2/op3)\n"
           "  --shot FILE.bmp     deterministic screenshot; no window or audio\n"
           "  --effect-ms N      sample scripted camera animation after N ms\n"
           "  --reveal N          reveal N Unicode characters in a screenshot\n"
           "  --wait-frame N      fixed wait animation frame (default: 0)\n"
           "  --hover X Y --down  screenshot pointer position/button state\n"
           "  --panel PANEL       title, extras, config, gallery, music, scenes, movies, save, load, backlog\n"
           "  --save-page N       save/load screenshot page, 1-10 (default: 1)\n"
           "  --config-page N     config screenshot page, 1-7 (default: 1)\n"
           "  --overlay DIR       translated GRP/MES/MOVIE asset overlay\n"
           "  --lang <id|en|jp>   set language (auto-locates id_Data or eng_data overlay)\n"
           "  --encoding NAME     windows-31j or utf-8 for overlay scripts\n"
           "  --window-size W H   window/capture size, with 16:9 letterboxing\n"
           "  --font FILE         Japanese TrueType font or collection\n"
           "  --no-audio          run without opening an audio device\n"
           "  --title             open the original title menu (interactive default)\n"
           "  --ui FILE           title-preview widget script override\n"
           "  --bgm FILE --voice NAME   manual audio playback override\n"
           "  --testaudio         headless three-second BGM/voice test\n"
           "  --debug --help\n\n"
           "Data: explicit path, DC4_ADVDATA, or nearby DC4/AdvData.\n", kOpening);
}

bool parse_options(int argc, char** argv, Options* o, std::string* err) {
    auto value = [&](int& i, std::string* out) {
        if (i + 1 >= argc) { *err = std::string("missing value for ") + argv[i]; return false; }
        *out = argv[++i];
        return true;
    };
    auto number = [&](int& i, int* out, int low, int high) {
        std::string s;
        if (!value(i, &s)) return false;
        auto result = std::from_chars(s.data(), s.data() + s.size(), *out);
        if (result.ec != std::errc{} || result.ptr != s.data() + s.size() || *out < low || *out > high) {
            *err = "invalid number: " + s;
            return false;
        }
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") o->help = true;
        else if (a == "--scene") { if (!value(i, &o->scene)) return false; }
        else if (a == "--ui") { if (!value(i, &o->ui)) return false; o->title = true; }
        else if (a == "--shot") { if (!value(i, &o->shot)) return false; }
        else if (a == "--panel") { if (!value(i, &o->panel)) return false; }
        else if (a == "--save-page") { if (!number(i, &o->save_page, 1, 10)) return false; }
        else if (a == "--config-page") { if (!number(i, &o->config_page, 1, 7)) return false; }
        else if (a == "--overlay") { if (!value(i, &o->overlay)) return false; }
        else if (a == "--lang") { if (!value(i, &o->lang)) return false; }
        else if (a == "--encoding") { if (!value(i, &o->encoding)) return false; }
        else if (a == "--font") { if (!value(i, &o->font)) return false; }
        else if (a == "--bgm") { if (!value(i, &o->bgm)) return false; }
        else if (a == "--voice") { if (!value(i, &o->voice)) return false; }
        else if (a == "--line") { if (!number(i, &o->line, 0, 1000000)) return false; }
        else if (a == "--effect-ms") { if (!number(i, &o->effect_ms, 0, 20000000)) return false; }
        else if (a == "--reveal") { if (!number(i, &o->reveal, -1, 1000000)) return false; }
        else if (a == "--wait-frame") { if (!number(i, &o->wait_frame, 0, 1000000)) return false; }
        else if (a == "--hover") {
            if (!number(i, &o->hover_x, -1, 32767) || !number(i, &o->hover_y, -1, 32767)) return false;
        } else if (a == "--window-size") {
            if (!number(i, &o->width, 320, 7680) || !number(i, &o->height, 180, 4320)) return false;
        } else if (a == "--title") o->title = true;
        else if (a == "--down") o->down = true;
        else if (a == "--no-audio") o->no_audio = true;
        else if (a == "--testaudio") o->test_audio = true;
        else if (a == "--choice" || a == "--map") {
            if (o->scene.empty()) {
                o->scene = "dc4_kyo20190208c1_m0.mes";
                o->line = 60;
            }
            o->choice = true;
        } else if (a == "--confirm") o->confirm = true;
        else if (a == "--config") o->config = true;
        else if (a == "--movie") {
            std::string m = "DC4_OP1.mpg";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                m = argv[++i];
            }
            fs::path mp(m);
            m = mp.filename().string();
            std::string lower = m;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (lower == "1" || lower == "op1" || lower == "dc4_op1") m = "DC4_OP1.mpg";
            else if (lower == "2" || lower == "op2" || lower == "dc4_op2") m = "DC4_OP2.mpg";
            else if (lower == "3" || lower == "op3" || lower == "dc4_op3") m = "DC4_OP3.mpg";
            else if (lower == "mv1" || lower == "mv001" || lower == "dc4_mv001") m = "dc4_mv001.mpg";
            else if (lower == "kotori1" || lower == "uta1") m = "KotoRI_uta01.mpg";
            else if (lower == "kotori2" || lower == "uta2") m = "KotoRI_uta02.mpg";
            else if (!mp.has_extension()) m += ".mpg";
            o->movie = m;
        }
        else if (!a.empty() && a[0] != '-' && o->data.empty()) o->data = a;
        else { *err = "unknown option: " + a; return false; }
    }
    if (o->title && !o->scene.empty()) { *err = "--title/--ui cannot be combined with --scene"; return false; }
    if (!o->movie.empty() && !o->scene.empty()) { *err = "--movie cannot be combined with --scene"; return false; }
    if (o->shot.empty() && o->reveal >= 0) { *err = "--reveal requires --shot"; return false; }
    const std::vector<std::string> panels{"save","load","backlog","title","extras","config","gallery","music","scenes","movies"};
    if(!o->panel.empty() && o->shot.empty()){*err="--panel requires --shot";return false;}
    if (!o->panel.empty() && std::find(panels.begin(),panels.end(),o->panel)==panels.end()) {
        *err = "Unknown --panel screen"; return false;
    }
    if (o->save_page && o->panel != "save" && o->panel != "load") {
        *err = "--save-page requires --panel save or --panel load"; return false;
    }
    if (o->test_audio && (o->no_audio || !o->shot.empty())) {
        *err = "--testaudio cannot be combined with --no-audio or --shot";
        return false;
    }
    return true;
}

bool is_data(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p / "GRP", ec) && fs::is_directory(p / "MES", ec);
}

std::string resolve_data(const std::string& requested) {
    if (!requested.empty()) {
        fs::path p(requested);
        if (!is_data(p) && is_data(p / "AdvData")) p /= "AdvData";
        return is_data(p) ? fs::absolute(p).lexically_normal().string() : "";
    }
    if (const char* env = std::getenv("DC4_ADVDATA")) if (*env) return resolve_data(env);
    std::vector<fs::path> roots{fs::current_path()};
    if (char* base = SDL_GetBasePath()) { roots.emplace_back(base); SDL_free(base); }
    for (fs::path p : roots) {
        for (int depth = 0; depth < 7 && !p.empty(); ++depth) {
            for (const fs::path& candidate : {p / "AdvData", p / "DC4" / "AdvData"})
                if (is_data(candidate)) return fs::absolute(candidate).lexically_normal().string();
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    return "";
}

std::string script_path(const std::string& data, const std::string& name) {
    fs::path p(name);
    if (!p.has_extension()) p += ".mes";
    if (!p.is_absolute() && !fs::is_regular_file(p)) p = fs::path(data) / "MES" / p;
    return p.string();
}

template<class T, void (*Free)(T*)> using SdlPtr = std::unique_ptr<T, decltype(Free)>;
using Window = SdlPtr<SDL_Window, SDL_DestroyWindow>;
using Renderer = SdlPtr<SDL_Renderer, SDL_DestroyRenderer>;
using Texture = SdlPtr<SDL_Texture, SDL_DestroyTexture>;
using Surface = SdlPtr<SDL_Surface, SDL_FreeSurface>;

Texture texture_from_rgba(SDL_Renderer* ren, const std::vector<uint8_t>& rgba, int w, int h) {
    Surface surface(SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(rgba.data()),
        w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
    Texture t(surface ? SDL_CreateTextureFromSurface(ren, surface.get()) : nullptr, SDL_DestroyTexture);
    if (t) SDL_SetTextureBlendMode(t.get(), SDL_BLENDMODE_BLEND);
    return t;
}

Texture load_texture(SDL_Renderer* ren, const std::string& path, std::string* err) {
    std::ifstream in(dc4::resolve_asset(path), std::ios::binary);
    if (!in) { *err = "cannot open " + path; return Texture(nullptr, SDL_DestroyTexture); }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), {}), rgba;
    dc4::CrxInfo info;
    if (!dc4::crx_decode(raw.data(), raw.size(), &rgba, &info, err)) return Texture(nullptr, SDL_DestroyTexture);
    Texture t = texture_from_rgba(ren, rgba, info.width, info.height);
    if (!t) *err = SDL_GetError();
    return t;
}

struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } };
struct AudioLifetime {
    dc4::AudioEngine engine;
    ~AudioLifetime() { engine.shutdown(); }
};

bool play_voice(dc4::AudioEngine& audio, dc4::VoiceArchive& archive, const std::string& name) {
    const dc4::VoiceEntry* entry = archive.find(name);
    std::vector<uint8_t> blob;
    return entry && archive.read_entry(*entry, &blob, nullptr) && audio.play_voice_blob(blob.data(), blob.size());
}

bool load_font(dc4scene::FontRenderer* font, const std::string& requested) {
    if (!requested.empty()) return font->load(requested, 0, 21.0f);
    if (const char* env = std::getenv("DC4_FONT")) if (font->load(env, 0, 21.0f)) return true;
    const char* windir = std::getenv("WINDIR");
    const fs::path fonts = fs::path(windir ? windir : "C:/Windows") / "Fonts";
    for (const char* face : {"msgothic.ttc", "meiryo.ttc", "YuGothR.ttc"})
        if (font->load((fonts / face).string(), 0, 21.0f)) return true;
    return false;
}

fs::path save_directory() {
    if (const char* env = std::getenv("DC4_SAVE_DIR")) if (*env) return fs::absolute(env);
    if (char* base = SDL_GetBasePath()) {
        fs::path path = fs::path(base) / "saves";
        SDL_free(base);
        return path;
    }
    throw std::runtime_error("Cannot locate the executable directory for saves");
}

bool is_original_save_directory(const fs::path& directory, const std::string& data) {
    auto normalized = [](const fs::path& path) {
        std::string result = fs::weakly_canonical(path).generic_string();
#ifdef _WIN32
        for (char& ch : result) ch = char(std::tolower(static_cast<unsigned char>(ch)));
#endif
        while (result.size() > 1 && result.back() == '/') result.pop_back();
        return result;
    };
    const std::string actual = normalized(directory);
    const fs::path data_path(data);
    for (const auto& path : {data_path / "savedata", data_path.parent_path() / "savedata"}) {
        const std::string original = normalized(path);
        if (actual == original || actual.compare(0, original.size() + 1, original + '/') == 0)
            return true;
    }
    return false;
}

std::string save_fingerprint(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > 4 * 1024 * 1024) return "";
    file.seekg(0);
    uint64_t hash = 14695981039346656037ull;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount())
        for (std::streamsize i = 0; i < file.gcount(); ++i) { hash ^= uint8_t(buffer[i]); hash *= 1099511628211ull; }
    if (!file.eof()) return "";
    std::ostringstream result; result << std::hex << hash;
    return result.str();
}

std::string to_utf8(const std::u16string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t ch = text[i];
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
            ch = 0x10000 + ((ch - 0xD800) << 10) + (text[++i] - 0xDC00);
        else if (ch >= 0xD800 && ch <= 0xDFFF) ch = 0xFFFD;
        if (ch < 0x80) result.push_back(char(ch));
        else if (ch < 0x800) { result.push_back(char(0xC0 | (ch >> 6))); result.push_back(char(0x80 | (ch & 63))); }
        else if (ch < 0x10000) {
            result.push_back(char(0xE0 | (ch >> 12))); result.push_back(char(0x80 | ((ch >> 6) & 63))); result.push_back(char(0x80 | (ch & 63)));
        } else {
            result.push_back(char(0xF0 | (ch >> 18))); result.push_back(char(0x80 | ((ch >> 12) & 63)));
            result.push_back(char(0x80 | ((ch >> 6) & 63))); result.push_back(char(0x80 | (ch & 63)));
        }
    }
    return result;
}

std::u16string from_utf8(const std::string& text) {
    std::u16string result;
    for (size_t i = 0; i < text.size();) {
        const uint8_t lead = uint8_t(text[i++]);
        uint32_t ch = lead;
        int count = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : (lead & 0xF8) == 0xF0 ? 3 : -1;
        if (count < 0 || i + size_t(count) > text.size()) return {};
        if (count) ch = lead & ((1u << (6 - count)) - 1);
        for (int n = 0; n < count; ++n) { const uint8_t c = uint8_t(text[i++]); if ((c & 0xC0) != 0x80) return {}; ch = (ch << 6) | (c & 63); }
        if ((count == 1 && ch < 0x80) || (count == 2 && ch < 0x800) || (count == 3 && ch < 0x10000) ||
            ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) return {};
        if (ch <= 0xFFFF) result.push_back(char16_t(ch));
        else { ch -= 0x10000; result.push_back(char16_t(0xD800 + (ch >> 10))); result.push_back(char16_t(0xDC00 + (ch & 1023))); }
    }
    return result;
}

bool write_save_preview(dc4scene::SceneRunner& scene, const fs::path& save) {
    const fs::path image_path = save.string() + ".preview.bmp";
    const fs::path text_path = save.string() + ".preview.txt";
    const fs::path temporary = text_path.string() + ".tmp";
    auto clear = [&]() {
        std::error_code ec;
        fs::remove(text_path, ec); ec.clear(); fs::remove(image_path, ec); ec.clear(); fs::remove(temporary, ec);
    };
    // The fingerprint prevents a retained old sidecar from being shown even
    // when the OS refuses removal, or when someone replaces a save externally.
    clear();
    struct RestoreMessage {
        dc4scene::SceneRunner& scene;
        bool visible;
        ~RestoreMessage() { scene.set_message_visible(visible); }
    } restore{scene, scene.message_visible()};
    try {
        const std::string fingerprint = save_fingerprint(save);
        if (fingerprint.empty()) return false;
        scene.set_message_visible(false);
        scene.draw(-1);
        Surface full(SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(scene.frame().data()),
            1280, 720, 32, 1280 * 4, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
        Surface thumbnail(SDL_CreateRGBSurfaceWithFormat(0, 128, 72, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
        if (!full || !thumbnail) return false;
        SDL_SetSurfaceBlendMode(full.get(), SDL_BLENDMODE_NONE);
        if (SDL_BlitScaled(full.get(), nullptr, thumbnail.get(), nullptr) < 0 || SDL_SaveBMP(thumbnail.get(), image_path.string().c_str()) < 0) {
            clear(); return false;
        }
        auto excerpt = scene.visible_text();
        if (!scene.speaker().empty()) excerpt = dc4scene::sjis_to_utf16(scene.speaker()) + u": " + excerpt;
        for (char16_t& ch : excerpt) if (ch == u'\n' || ch == u'\r') ch = u' ';
        if (excerpt.size() > 256) excerpt.resize(256);
        std::ofstream text(temporary, std::ios::binary | std::ios::trunc);
        text << "DC4PREVIEW1 " << fingerprint << '\n' << to_utf8(excerpt);
        text.close();
        if (!text) { clear(); return false; }
        fs::rename(temporary, text_path);
        return true;
    } catch (...) { clear(); return false; }
}

// Session history with exact scene snapshots and original-style row previews.
struct Backlog {
    std::vector<dc4ui::LogRow> rows;
    int scroll = 0;  // oldest visible row index
    bool open = false;
    void add(dc4scene::SceneRunner& scene, int line) {
        rows.push_back(dc4ui::capture_log_row(scene, line));
        if (rows.size() > 4000) rows.erase(rows.begin(), rows.begin() + (rows.size() - 4000));
        size_t snapshot_bytes = 0, snapshot_count = 0;
        for (size_t i = rows.size(); i > 0; --i) {
            auto& bytes = rows[i-1].snapshot;
            if (bytes.empty()) continue;
            if (++snapshot_count > 512 || snapshot_bytes + bytes.size() > 16 * 1024 * 1024) bytes.clear();
            else snapshot_bytes += bytes.size();
        }
        scroll = max_scroll();
    }
    int max_scroll() const { return dc4ui::LogScreen::max_scroll(rows.size()); }
    void move(int amount) { scroll = std::clamp(scroll + amount, 0, max_scroll()); }
};

// Snapshot IO stays in the desktop host. The shared screen only sees engine
// slot metadata; original savedata is never imported or modified.
struct SaveSlots {
    bool open = false;
    int latest_slot = -1;  // Most recent numbered save, or -1 when none exist.
    std::vector<dc4ui::SaveSlot> entries{dc4ui::SaveScreen::kSlotCount};

    void refresh(const fs::path& directory, dc4ui::SaveScreen* screen) {
        latest_slot = -1;
        fs::file_time_type latest_time = fs::file_time_type::min();
        auto consider_latest = [&](const fs::path& path, int slot) {
            std::error_code ec;
            const auto modified = fs::last_write_time(path, ec);
            if (!ec && (latest_slot < 0 || modified > latest_time)) {
                latest_time = modified;
                latest_slot = slot;
            }
        };
        for (int i = 0; i < dc4ui::SaveScreen::kSlotCount; ++i) {
            auto& entry = entries[size_t(i)];
            entry = dc4ui::SaveSlot{};
            const auto path = directory / ("slot" + std::to_string(i + 1) + ".dc4sav");
            std::error_code ec;
            entry.occupied = fs::is_regular_file(path, ec);
            screen->set_slot_occupied(i, entry.occupied);
            if (!entry.occupied) continue;
            consider_latest(path, i);
            entry.timestamp = u"Saved snapshot";
            const auto modified = fs::last_write_time(path, ec);
            if (!ec) {
                const auto time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    modified - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                const auto stamp = std::chrono::system_clock::to_time_t(time);
                if (const auto* local = std::localtime(&stamp)) {
                    char formatted[64]{};
                    std::strftime(formatted, sizeof(formatted), "%Y %m/%d %H:%M:%S", local);
                    entry.timestamp.assign(formatted, formatted + std::strlen(formatted));
                }
            }
            const fs::path preview_text = path.string() + ".preview.txt";
            const auto text_size = fs::file_size(preview_text, ec);
            if (ec || text_size > 4096) continue;
            std::ifstream preview(preview_text, std::ios::binary);
            std::string marker; std::getline(preview, marker);
            const auto fingerprint = save_fingerprint(path);
            if (fingerprint.empty() || marker != "DC4PREVIEW1 " + fingerprint) continue;
            std::string excerpt((std::istreambuf_iterator<char>(preview)), {});
            entry.excerpt = from_utf8(excerpt);
            const fs::path preview_image = path.string() + ".preview.bmp";
            if (fs::file_size(preview_image, ec) > 100000 || ec) continue;
            std::ifstream bmp_header(preview_image, std::ios::binary);
            uint8_t header[26]{};
            if (!bmp_header.read(reinterpret_cast<char*>(header), sizeof(header)) || header[0] != 'B' || header[1] != 'M') continue;
            auto dimension = [&](size_t offset) {
                return uint32_t(header[offset]) | (uint32_t(header[offset + 1]) << 8) |
                    (uint32_t(header[offset + 2]) << 16) | (uint32_t(header[offset + 3]) << 24);
            };
            if (dimension(18) != 128 || dimension(22) != 72) continue;
            Surface loaded(SDL_LoadBMP(preview_image.string().c_str()), SDL_FreeSurface);
            if (!loaded || loaded->w != 128 || loaded->h != 72) continue;
            Surface rgba(SDL_ConvertSurfaceFormat(loaded.get(), SDL_PIXELFORMAT_RGBA32, 0), SDL_FreeSurface);
            if (!rgba) continue;
            entry.thumbnail.w = 128;
            entry.thumbnail.h = 72;
            entry.thumbnail.rgba.resize(128 * 72 * 4);
            for (int y = 0; y < 72; ++y) std::memcpy(entry.thumbnail.rgba.data() + y * 128 * 4,
                static_cast<const uint8_t*>(rgba->pixels) + y * rgba->pitch, 128 * 4);
        }
        std::error_code ec;
        const fs::path quicksave = directory / "quicksave.dc4sav";
        const bool quick = fs::is_regular_file(quicksave, ec);
        screen->set_shortcuts(latest_slot >= 0, false, quick);
    }
};

int run(const Options& o) {
    const std::string data = resolve_data(o.data);
    if (data.empty()) {
        fprintf(stderr, "Game data not found. Pass the AdvData directory or set DC4_ADVDATA.\n");
        return 1;
    }
    std::string overlay = o.overlay;
    if (overlay.empty() && !o.lang.empty()) {
        std::string l = o.lang;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::string target_folder = (l == "id" || l == "indonesian") ? "id_Data" :
                                    (l == "en" || l == "english") ? "eng_data" : "";
        if (!target_folder.empty()) {
            for (const auto& root : {fs::path(data).parent_path(), fs::path(data).parent_path().parent_path()}) {
                std::error_code ec;
                if (fs::is_directory(root / target_folder, ec)) {
                    overlay = (root / target_folder).string();
                    break;
                }
            }
        }
    }
    printf("AdvData: %s\n", data.c_str());
    if (!overlay.empty()) printf("Overlay: %s\n", overlay.c_str());
    dc4::configure_resources(data, overlay, o.encoding);
    if (!o.lang.empty()) {
        std::string l = o.lang;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (l == "id" || l == "indonesian") dc4::set_language(dc4::Language::Indonesian);
        else if (l == "en" || l == "english") dc4::set_language(dc4::Language::English);
        else if (l == "jp" || l == "japanese") dc4::set_language(dc4::Language::Japanese);
    }
    AudioLifetime audio;
    dc4::VoiceArchive voices;
    std::string err;
    const bool wants_audio = o.shot.empty() && !o.no_audio;
    bool audio_ready = false;
    if (wants_audio) {
        if (voices.open(data + "/VOICE.DAT", &err)) printf("VOICE.DAT: %zu entries\n", voices.entries().size());
        else fprintf(stderr, "VOICE.DAT: %s\n", err.c_str());
        audio_ready = audio.engine.init();
        if (!audio_ready) fprintf(stderr, "Audio device unavailable; continuing silently.\n");
    }
    if (o.test_audio) {
        if (!audio_ready) return 1;
        int begin = 0, end = 0;
        dc4::bgm_loop_for("001.pcm", &begin, &end);
        bool ok = audio.engine.play_bgm_file(data + "/MUSIC/001.pcm", size_t(std::max(0, begin)));
        ok = play_voice(audio.engine, voices, "VOICE\\system\\system_asa011") && ok;
        if (ok) { printf("Playing BGM 001 and system voice for three seconds.\n"); SDL_Delay(3000); }
        else fprintf(stderr, "Audio playback test failed.\n");
        return ok ? 0 : 1;
    }
    SDL_SetMainReady();
    // Screenshots use an offscreen software renderer: no desktop dependency,
    // device initialization, GPU readback-after-present, or animation race.
    if (SDL_Init(o.shot.empty() ? SDL_INIT_VIDEO | SDL_INIT_TIMER : SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SdlLifetime sdl;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    Window window(nullptr, SDL_DestroyWindow);
    Surface offscreen(nullptr, SDL_FreeSurface);
    Renderer renderer(nullptr, SDL_DestroyRenderer);
    if (!o.shot.empty()) {
        offscreen.reset(SDL_CreateRGBSurfaceWithFormat(0, o.width, o.height, 32, SDL_PIXELFORMAT_RGBA32));
        if (offscreen) renderer.reset(SDL_CreateSoftwareRenderer(offscreen.get()));
    } else {
        window.reset(SDL_CreateWindow("D.C.4 - dc4engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            o.width, o.height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
        if (window) {
            SDL_SetWindowMinimumSize(window.get(), 640, 360);
            renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
            if (!renderer) renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
        }
    }
    if (!renderer) { fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* ren = renderer.get();
    if (SDL_RenderSetLogicalSize(ren, dc4scene::kScreenW, dc4scene::kScreenH) < 0) {
        fprintf(stderr, "SDL logical size: %s\n", SDL_GetError()); return 1;
    }

    dc4scene::FontRenderer font;
    std::unique_ptr<dc4scene::SceneRunner> scene;
    Texture background(nullptr, SDL_DestroyTexture), sheet(nullptr, SDL_DestroyTexture);
    Texture frame_texture(nullptr, SDL_DestroyTexture);
    dc4ui::ScriptUi ui;
    dc4ui::LogScreen log_screen;
    dc4ui::SaveScreen save_screen;
    dc4ui::SystemMenu system_menu;
    dc4ui::ConfirmDialog global_confirm;
    dc4::MoviePlayer movie;
    bool movie_from_scene = false;
    bool music_running = false;
    bool menu_open = false, slots_from_menu = false;
    bool current_line_read = false;
    bool profile_dirty = false;
    if (!load_font(&font, o.font)) {
        fprintf(stderr, "A Japanese font is required. Install MS Gothic or pass --font FILE.\n"); return 1;
    }
    if (!system_menu.load(data, &err)) { fprintf(stderr, "System menus: %s\n", err.c_str()); return 1; }
    const fs::path profile_dir = save_directory();
    const bool profile_allowed = !is_original_save_directory(profile_dir,data);
    if (profile_allowed && fs::is_regular_file(profile_dir / "profile.dc4profile"))
        if (!system_menu.load_profile((profile_dir / "profile.dc4profile").string(), &err))
            fprintf(stderr,"Profile: %s\n",err.c_str());
    auto flush_profile = [&]() {
        if (!profile_dirty || !o.shot.empty() || !profile_allowed) return;
        try {
            fs::create_directories(profile_dir);
            if (system_menu.save_profile((profile_dir / "profile.dc4profile").string(),&err)) profile_dirty=false;
            else fprintf(stderr,"Profile: %s\n",err.c_str());
        } catch(const std::exception& e) { fprintf(stderr,"Profile: %s\n",e.what()); }
    };
    auto sync_progress = [&]() {
        if (!scene) return;
        if (system_menu.sync_progress(*scene)) profile_dirty=true;
    };
    auto observe = [&](const dc4::MesCommand& c,const std::string& script) {
        system_menu.observe(c,script); profile_dirty=true;
    };
    auto apply_settings = [&]() {
        const auto cfg=system_menu.config();
        audio.engine.set_master_volume(cfg.master_enabled?cfg.master_volume:0);
        audio.engine.set_user_volume(dc4::AudioGroup::Bgm,cfg.music_enabled?cfg.music_volume:0);
        audio.engine.set_user_volume(dc4::AudioGroup::Se,cfg.se_enabled?cfg.se_volume:0);
        audio.engine.set_user_volume(dc4::AudioGroup::Voice,cfg.voice_enabled?cfg.voice_volume:0);
        audio.engine.set_movie_volume(cfg.movie_enabled?cfg.movie_volume:0);
        audio.engine.set_duck_music_for_voice(cfg.duck_music_for_voice,system_menu.variable(99,206));
        if (scene) {
            scene->set_window_opacity(cfg.window_opacity);
            scene->set_effects_enabled(cfg.effects!=0);
            scene->set_effect_speed(cfg.effect_speed);
            for(size_t i=0;i<cfg.character_volume.size();++i)
                scene->set_character_volume(i,cfg.character_volume[i],cfg.character_muted[i]);
        }
        if(window) {
            bool full=(SDL_GetWindowFlags(window.get())&SDL_WINDOW_FULLSCREEN_DESKTOP)!=0;
            if(full!=cfg.fullscreen) SDL_SetWindowFullscreen(window.get(),cfg.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
        }
    };
    auto title_music = [&]() {
        if(!audio_ready)return;
        int begin=0,end=0;dc4::bgm_loop_for("001.pcm",&begin,&end);
        audio.engine.play_bgm_file(data+"/MUSIC/001.pcm",size_t(std::max(begin,0)),size_t(std::max(end,0)),true);
    };
    Backlog backlog;
    bool thumbnails_on = true;
    bool thumb_drag = false;
    SaveSlots slots;
    // Builds a fresh SceneRunner settled up to a dialogue index. Shared by
    // startup and the Log screen's scene-jump button. `history` (when given)
    // collects every settled line with its dialogue index.
    auto build_scene = [&](const std::string& scene_name, int line,
                           bool animate_last, Backlog* history, const std::string& entry = {}, bool replay = false) -> std::unique_ptr<dc4scene::SceneRunner> {
        auto rebuilt = std::make_unique<dc4scene::SceneRunner>(data);
        rebuilt->set_debug(o.debug || std::getenv("DC4_DEBUG"));
        rebuilt->set_font(&font);
        rebuilt->set_observer(observe);
        if (audio_ready) rebuilt->set_audio(&audio.engine, &voices);
        if (!rebuilt->load_sheets(data + "/MES/start.mes") ||
            !rebuilt->load_ui_script(data + "/MES/win_size.mes") ||
            !rebuilt->load_script(script_path(data, scene_name), entry)) {
            fprintf(stderr, "Scene initialization failed: %s\n", rebuilt->last_error().c_str());
            return nullptr;
        }
        system_menu.initialize_scene(*rebuilt,replay);
        for (int i = 0; i <= line; ++i) {
            // Settle earlier dialogue indices; animate only the requested line.
            rebuilt->set_animation_enabled(i == line ? animate_last : false);
            while (rebuilt->waiting_for_time()) rebuilt->finish_script_wait();
            while (!rebuilt->advance_to_next_text()) {
                if (rebuilt->waiting_for_time()) {
                    rebuilt->finish_script_wait();
                    continue;
                }
                if (rebuilt->waiting_for_choice() && i == line) break;
                if (i == line && !rebuilt->pending_movie().empty()) break;
                fprintf(stderr, "Cannot reach dialogue %d (stopped at %d): %s\n", line, i, rebuilt->last_error().c_str());
                return nullptr;
            }
            rebuilt->set_message_visible(true);
            if (history) history->add(*rebuilt, i);
        }
        return rebuilt;
    };
    const bool system_panel = !o.panel.empty() && o.panel!="save" && o.panel!="load" && o.panel!="backlog";
    const bool start_title = o.title || o.config || system_panel || (o.scene.empty() && o.shot.empty() && o.panel.empty() && o.movie.empty());
    if (!o.movie.empty()) {
        movie_from_scene = false;
        if (!movie.open(data, o.movie, audio_ready ? &audio.engine : nullptr, &err)) {
            fprintf(stderr, "Cannot open movie '%s': %s\n", o.movie.c_str(), err.c_str());
            return 1;
        }
    } else if (!start_title) {
        scene = build_scene(o.scene.empty() ? kOpening : o.scene, o.line,
                            o.shot.empty() || o.effect_ms >= 0, &backlog);
        if (!scene) return 1;
        if (o.choice && !scene->waiting_for_choice()) {
            scene->advance_to_next_text();
        }
        if (!o.shot.empty() && o.effect_ms >= 0) scene->update(uint32_t(o.effect_ms));
        current_line_read=system_menu.scene_seen(scene->script_name(),"@"+std::to_string(scene->script_position()));
    } else {
        menu_open=true;
        dc4ui::SystemMenuPage page=dc4ui::SystemMenuPage::Title;
        if(o.panel=="extras")page=dc4ui::SystemMenuPage::Extras;
        else if(o.panel=="config" || o.config)page=dc4ui::SystemMenuPage::Config;
        else if(o.panel=="gallery")page=dc4ui::SystemMenuPage::Gallery;
        else if(o.panel=="music" || o.panel=="movies")page=dc4ui::SystemMenuPage::Music;
        else if(o.panel=="scenes")page=dc4ui::SystemMenuPage::Scenes;
        system_menu.open(page);
        if(o.config_page)system_menu.set_config_page(o.config_page-1);
        if(o.panel=="movies")system_menu.activate("selmovie");
        if(page==dc4ui::SystemMenuPage::Title || page==dc4ui::SystemMenuPage::Extras)title_music();
    }
    if (!log_screen.load(data)) fprintf(stderr, "Log screen artwork unavailable: GRP/SYSTEM/BKLOG*.CRX\n");
    log_screen.set_latin_mode(dc4::is_latin());
    if (!save_screen.load(data, &err)) fprintf(stderr, "Save/Load screen artwork unavailable: %s\n", err.c_str());
    save_screen.set_language(dc4::get_language());
    save_screen.set_navigation(true,true,true);
    if (!global_confirm.load(data, &err)) fprintf(stderr, "Confirm dialog artwork unavailable: %s\n", err.c_str());
    global_confirm.set_language(dc4::get_language());
    frame_texture.reset(SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, dc4scene::kScreenW, dc4scene::kScreenH));
    if (!frame_texture) { fprintf(stderr, "Frame texture: %s\n", SDL_GetError()); return 1; }
    SDL_SetTextureBlendMode(frame_texture.get(), SDL_BLENDMODE_NONE);
    if(profile_allowed) {
        slots.refresh(profile_dir,&save_screen);
        system_menu.set_save_available(slots.latest_slot>=0,fs::is_regular_file(profile_dir/"quicksave.dc4sav"),fs::is_regular_file(profile_dir/"autosave.dc4sav"));
    }
    apply_settings();
    if (audio_ready && !o.bgm.empty()) {
        int begin = 0, end = 0;
        dc4::bgm_loop_for(o.bgm, &begin, &end);
        if (!audio.engine.play_bgm_file((fs::path(data) / "MUSIC" / o.bgm).string(), size_t(std::max(0, begin))))
            fprintf(stderr, "Cannot play BGM: %s\n", o.bgm.c_str());
    }
    if (audio_ready && !o.voice.empty() && !play_voice(audio.engine, voices, o.voice))
        fprintf(stderr, "Cannot play voice: %s\n", o.voice.c_str());

    bool line_done = !o.shot.empty(), auto_mode = false, skip_mode = false, run_loop = true;
    int mx = o.hover_x, my = o.hover_y;
    int dialogue_index = o.line;  // scene-jump target for newly added backlog rows
    bool mouse_down = o.down;
    Uint32 line_start = SDL_GetTicks(), line_finished = line_start, last_skip = line_start;
    Uint32 animation_tick = line_start;
    std::string status;
    auto update_title = [&]() {
        if (!window) return;
        std::string title = "D.C.4 - dc4engine";
        if (movie.active()) {
            title += " | Movie";
        } else if (menu_open) {
            static const char* names[]={"Title","Extras","Config","Gallery","Music","Scenes"};
            title += std::string(" | ")+names[int(system_menu.page())];
        } else if (slots.open) {
            title += save_screen.mode() == dc4ui::SaveMode::Load ? " | Load" : " | Save";
            title += " - page " + std::to_string(save_screen.page() + 1);
        }
        else if (backlog.open) title += " | Backlog";
        else if (auto_mode) title += " | Auto";
        else if (skip_mode) title += " | Skip";
        if (!status.empty()) title += " | " + status;
        else title += " | F1: controls";
        SDL_SetWindowTitle(window.get(), title.c_str());
    };
    auto notify = [&](const std::string& text) { status = text; printf("%s\n", text.c_str()); update_title(); };
    auto reset_line = [&]() {
        line_start = SDL_GetTicks(); line_finished = line_start; line_done = false;
        ++dialogue_index;
        backlog.add(*scene, dialogue_index);
        current_line_read=system_menu.scene_seen(scene->script_name(),"@"+std::to_string(scene->script_position()));
        sync_progress();flush_profile();
        status.clear(); update_title();
    };
    auto mark_read = [&]() {
        if(!scene || scene->current_line().empty())return;
        system_menu.mark_scene_seen(scene->script_name(),"@"+std::to_string(scene->script_position()));
        profile_dirty=true;
    };
    std::function<void()> advance_script;
    advance_script = [&]() {
        if(!scene)return;
        if(scene->advance_to_next_text()){reset_line();return;}
        sync_progress();flush_profile();
        if(!scene->pending_movie().empty()) {
            movie_from_scene=true;
            if(!movie.open(data,scene->pending_movie(),audio_ready?&audio.engine:nullptr,&err)) {
                notify("Movie: "+err);scene->finish_movie();
            }
        } else if(scene->waiting_for_time())return;
        else if(scene->waiting_for_choice())notify("Choose an option");
        else if(scene->scene_ended() && scene->last_error().empty()) {
            auto_mode=skip_mode=false;menu_open=true;system_menu.set_in_game(false);
            if(scene->variable(9,7))system_menu.resume_scenes();
            else system_menu.open(dc4ui::SystemMenuPage::Title);
            audio.engine.stop_all();title_music();update_title();
        } else if(!scene->last_error().empty())notify(scene->last_error());
    };
    auto advance = [&]() {
        if (!scene || menu_open || backlog.open || slots.open || scene->waiting_for_choice()) return;
        if(scene->waiting_for_time()){scene->finish_script_wait();advance_script();return;}
        if (!scene->message_visible()) { scene->set_message_visible(true); return; }
        if (!line_done) { line_done = true; line_finished = SDL_GetTicks(); return; }
        mark_read();
        if(system_menu.config().stop_voice_on_advance)audio.engine.stop_group(dc4::AudioGroup::Voice);
        advance_script();
    };
    auto select_choice = [&](size_t index) {
        if (!scene || !scene->waiting_for_choice() || index >= scene->choices().size()) return;
        if(system_menu.config().stop_auto_at_choice)auto_mode=false;
        if(system_menu.config().stop_skip_at_choice)skip_mode=false;
        if (!scene->choose(index)) { notify(scene->last_error()); return; }
        advance_script();
    };
    auto toggle_backlog = [&]() {
        if (!scene || menu_open) return;
        backlog.open = !backlog.open;
        backlog.scroll = backlog.max_scroll();
        auto_mode = skip_mode = false;
        status.clear(); update_title();
    };
    // シーンジャンプ: rebuild the opening scene settled at the row's dialogue.
    // History restarts at the jump point; advancing re-accumulates the rest.
    auto jump_to_row = [&](int row_index) {
        if (!scene) return;
        if(row_index<0 || size_t(row_index)>=backlog.rows.size())return;
        const dc4ui::LogRow row = backlog.rows[size_t(row_index)];
        if (row.snapshot.empty()) { notify("This older log row has no retained scene snapshot"); return; }
        if(!scene->restore_state(row.snapshot,&err)){notify("Scene jump failed: "+err);return;}
        backlog.rows.erase(backlog.rows.begin() + size_t(row_index)+1, backlog.rows.end());
        backlog.scroll = backlog.max_scroll();
        dialogue_index = row.line;
        line_done = true;
        line_start = line_finished = SDL_GetTicks();
        backlog.open = false;
        apply_settings();
        notify("Scene jump: line " + std::to_string(row.line + 1));
        update_title();
    };
    auto play_row_voice = [&](const dc4ui::LogRow& row) {
        if (row.voice.empty()) return;
        if (!play_voice(audio.engine, voices, row.voice)) notify("Voice not found: " + row.voice);
    };
    auto close_slots = [&]() {
        slots.open = false;
        menu_open=slots_from_menu;
        save_screen.cancel_pointer();
        mouse_down = false;
        status.clear(); update_title();
    };
    auto persist_state = [&](bool load, int slot) {
        if (!scene || slot >= dc4ui::SaveScreen::kSlotCount) return;
        auto_mode = skip_mode = false;
        mouse_down = false;
        save_screen.cancel_pointer();
        const std::string operation = slot < 0 ? (load ? "Quick load" : "Quick save") :
            std::string(load ? "Load" : "Save") + " - slot " + std::to_string(slot + 1);
        bool preview_ok = true;
        try {
            const fs::path directory = save_directory();
            if (is_original_save_directory(directory, data)) {
                notify("Set DC4_SAVE_DIR to a separate directory for emulator saves"); return;
            }
            const fs::path path = directory / (slot < 0 ? "quicksave.dc4sav" : "slot" + std::to_string(slot + 1) + ".dc4sav");
            // Empty Load rows are inert, including when a file disappears
            // after the screen was opened. Quick-load failures still report.
            if (slot >= 0) {
                const bool occupied = fs::is_regular_file(path);
                if (load && !occupied) return;
            }
            if (load) {
                if (!scene->load_state(path.string())) { notify(operation + " failed: " + scene->last_error()); return; }
                backlog = Backlog{};
                backlog.add(*scene, dialogue_index);
                line_done = true;
                line_start = line_finished = SDL_GetTicks();
                scene->set_message_visible(true);
                menu_open=false;slots_from_menu=false;apply_settings();
                current_line_read=system_menu.scene_seen(scene->script_name(),"@"+std::to_string(scene->script_position()));
            } else {
                fs::create_directories(directory);
                if (!scene->save_state(path.string())) { notify(operation + " failed: " + scene->last_error()); return; }
                preview_ok = write_save_preview(*scene, path);
            }
            // The native SAVE script returns to its menu after writing.
            // Quick save/load keep their existing close-on-success behavior.
            if (!load && slot >= 0 && slots.open) slots.refresh(directory, &save_screen);
            else slots.open = false;
            backlog.open = false;
            slots.refresh(directory,&save_screen);
            system_menu.set_save_available(slots.latest_slot>=0,fs::is_regular_file(directory/"quicksave.dc4sav"),fs::is_regular_file(directory/"autosave.dc4sav"));
            notify(operation + (load ? " restored" : " written") + (preview_ok ? "" : " (preview unavailable)"));
        } catch (const std::exception& e) {
            notify(operation + " failed: " + e.what());
        }
    };
    auto open_slots = [&](bool load) {
        if (!scene) {
            if(!load)return;
            scene=std::make_unique<dc4scene::SceneRunner>(data);scene->set_font(&font);scene->set_observer(observe);
            if(audio_ready)scene->set_audio(&audio.engine,&voices);
            scene->load_sheets(data+"/MES/start.mes");scene->load_ui_script(data+"/MES/win_size.mes");
        }
        auto_mode = skip_mode = false;
        backlog.open = false;
        save_screen.set_mode(load ? dc4ui::SaveMode::Load : dc4ui::SaveMode::Save);
        save_screen.cancel_pointer();
        try {
            const fs::path directory = save_directory();
            if (is_original_save_directory(directory, data)) {
                notify("Set DC4_SAVE_DIR to a separate directory for emulator saves"); return;
            }
            slots.refresh(directory, &save_screen);
        }
        catch (const std::exception& e) { notify(e.what()); return; }
        slots_from_menu=menu_open;menu_open=false;slots.open = true;
        status.clear(); update_title();
    };
    auto select_slot = [&](int index) {
        if (index < 0 || index >= dc4ui::SaveScreen::kSlotCount) return;
        const bool load = save_screen.mode() == dc4ui::SaveMode::Load;
        if (load && !slots.entries[size_t(index)].occupied) return;
        persist_state(load, index);
    };
    std::function<void()> pending_confirm;
    auto prompt = [&](dc4ui::ConfirmKind kind,std::function<void()> accepted) {
        auto_mode=skip_mode=false;mouse_down=false;
        global_confirm.set_language(dc4::get_language());
        if(global_confirm.open(kind))pending_confirm=std::move(accepted);
        else accepted();
    };
    auto finish_confirm = [&](bool yes) {
        global_confirm.answer(yes);auto accepted=std::move(pending_confirm);pending_confirm={};
        mouse_down=false;animation_tick=SDL_GetTicks();
        if(yes&&accepted)accepted();
    };
    auto prompt_quit = [&]() {prompt(dc4ui::ConfirmKind::Quit,[&]{run_loop=false;});};

    auto open_system = [&](dc4ui::SystemMenuPage page) {
        const bool from_scene=!menu_open && scene && !scene->script_name().empty();
        auto_mode=skip_mode=false;slots.open=false;backlog.open=false;mouse_down=false;
        system_menu.set_in_game(page==dc4ui::SystemMenuPage::Config && from_scene);
        system_menu.open(page);menu_open=true;
        if(page==dc4ui::SystemMenuPage::Title || page==dc4ui::SystemMenuPage::Extras)title_music();
        else if(page==dc4ui::SystemMenuPage::Music)audio.engine.stop_bgm();
        status.clear();update_title();
    };
    auto prompt_title = [&]() {prompt(dc4ui::ConfirmKind::Title,[&]{open_system(dc4ui::SystemMenuPage::Title);});};
    auto prompt_quick = [&](bool load) {
        if(load&&!fs::is_regular_file(profile_dir/"quicksave.dc4sav")){notify("No quick save");return;}
        prompt(load?dc4ui::ConfirmKind::QuickLoad:dc4ui::ConfirmKind::QuickSave,[&,load]{persist_state(load,-1);});
    };
    auto start_scene = [&](const dc4ui::SystemMenuAction& selected) {
        audio.engine.stop_all();
        Backlog history;
        auto started=build_scene(selected.asset,0,true,&history,selected.label,
                                 selected.type==dc4ui::SystemMenuActionType::StartScene);
        if(!started){notify("Cannot start "+selected.asset);return;}
        scene=std::move(started);backlog=std::move(history);dialogue_index=0;
        menu_open=slots.open=backlog.open=false;auto_mode=skip_mode=false;
        line_done=false;line_start=line_finished=SDL_GetTicks();
        current_line_read=system_menu.scene_seen(scene->script_name(),"@"+std::to_string(scene->script_position()));
        apply_settings();sync_progress();flush_profile();update_title();
    };
    auto finish_movie = [&]() {
        const bool resume=movie_from_scene;
        movie.close();movie_from_scene=false;
        if(resume && scene){scene->finish_movie();advance_script();}
        else if(!o.movie.empty() && !o.title) run_loop = false;
    };
    auto menu_action = [&](const dc4ui::SystemMenuAction& selected) {
        using T=dc4ui::SystemMenuActionType;
        switch(selected.type) {
        case T::Back:
            if(scene && !scene->script_name().empty()){menu_open=false;scene->set_message_visible(true);}
            break;
        case T::NewGame:case T::StartScene:start_scene(selected);break;
        case T::Load:open_slots(true);break;
        case T::Save:open_slots(false);break;
        case T::LatestLoad:
            open_slots(true);if(slots.latest_slot>=0)persist_state(true,slots.latest_slot);break;
        case T::QuickLoad:open_slots(true);persist_state(true,-1);break;
        case T::AutoLoad:
            open_slots(true);
            if(scene&&scene->load_state((profile_dir/"autosave.dc4sav").string())){menu_open=slots.open=false;apply_settings();}
            break;
        case T::Quit:run_loop=false;break; // Already confirmed by SystemMenu.
        case T::PlayMusic: {
            std::string name=selected.asset;std::replace(name.begin(),name.end(),'\\','/');
            if(fs::path(name).extension().empty())name+=".pcm";
            int begin=0,end=0;dc4::bgm_loop_for(fs::path(name).filename().string(),&begin,&end);
            if(audio_ready&&!audio.engine.play_bgm_file((fs::path(data)/name).string(),size_t(std::max(0,begin)),size_t(std::max(0,end)),system_menu.variable(100,26)==0))
                notify("Cannot play "+name);
            music_running=audio_ready&&audio.engine.channel_playing(0);
            break;
        }
        case T::StopMusic:audio.engine.stop_bgm();music_running=false;break;
        case T::SeekMusic:audio.engine.seek_channel_fraction(0,double(selected.index)/100);break;
        case T::PlayVoice:if(audio_ready)play_voice(audio.engine,voices,selected.asset);break;
        case T::PlayMovie:
            movie_from_scene=false;
            if(!movie.open(data,selected.asset,audio_ready?&audio.engine:nullptr,&err))notify("Movie: "+err);
            break;
        default:break;
        }
        profile_dirty=true;apply_settings();flush_profile();update_title();
    };
    auto activate_menu = [&](const std::string& tag) {
        const auto before=system_menu.page();
        menu_action(system_menu.activate(tag));
        if(menu_open&&before!=system_menu.page()){
            if(system_menu.page()==dc4ui::SystemMenuPage::Music)audio.engine.stop_bgm();
            else if(system_menu.page()==dc4ui::SystemMenuPage::Title||system_menu.page()==dc4ui::SystemMenuPage::Extras)title_music();
        }
    };
    auto save_action = [&](const dc4ui::SaveAction& selected) {
        switch (selected.type) {
        case dc4ui::SaveActionType::Back: close_slots(); break;
        case dc4ui::SaveActionType::Slot: select_slot(selected.index); break;
        case dc4ui::SaveActionType::Page:
        case dc4ui::SaveActionType::Mode:
            status.clear(); update_title(); break;
        case dc4ui::SaveActionType::LatestLoad:
            if (slots.latest_slot >= 0) persist_state(true, slots.latest_slot);
            break;
        case dc4ui::SaveActionType::QuickLoad: persist_state(true, -1); break;
        case dc4ui::SaveActionType::Config:open_system(dc4ui::SystemMenuPage::Config);break;
        case dc4ui::SaveActionType::Title:open_system(dc4ui::SystemMenuPage::Title);break;
        case dc4ui::SaveActionType::Quit:run_loop=false;break;
        default: break;
        }
    };
    auto action = [&](const std::string& tag) {
        if (!scene) return;
        if (tag == ":voice") {
            if (!scene->replay_voice()) notify("No voice on this line");
        } else if (tag == ":auto") {
            auto_mode = !scene->waiting_for_choice() && !auto_mode; skip_mode = false; status.clear();
        } else if (tag == ":rapid" || tag == ":skip") {
            skip_mode = !scene->waiting_for_choice() && !skip_mode; auto_mode = false; status.clear();
        }
        else if (tag == ":backlog" || tag==":aalog") toggle_backlog();
        else if (tag == ":n" || tag == ":off" || tag == ":close" || tag == ":clear") {
            scene->set_message_visible(!scene->message_visible()); auto_mode = skip_mode = false;
        } else if (tag == ":qs" || tag==":aaqsave") prompt_quick(false);
        else if (tag == ":ql" || tag==":aaqload") prompt_quick(true);
        else if (tag == ":ds" || tag==":aasave") open_slots(false);
        else if (tag == ":dl" || tag==":aaload") open_slots(true);
        else if (tag == ":option" || tag==":aaconfig" || tag==":aawin_size") open_system(dc4ui::SystemMenuPage::Config);
        else if(tag==":aatitle") prompt_title();
        else if(tag==":aaskip"){skip_mode=!skip_mode;auto_mode=false;}
        update_title();
    };
    auto draw = [&]() {
        const Uint32 tick = SDL_GetTicks();
        if (scene && !menu_open && !movie.active() && !scene->pending_movie().empty()) {
            movie_from_scene = true;
            if (!movie.open(data, scene->pending_movie(), audio_ready ? &audio.engine : nullptr, &err)) {
                notify("Movie: " + err); scene->finish_movie();
            }
        }
        if (scene && o.shot.empty()) {
            if (!menu_open && !global_confirm.active() && !movie.active() && !backlog.open && !slots.open && (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS))
                scene->update(tick - animation_tick);
        }
        if(movie.active() && o.shot.empty()){
            movie.update(tick-animation_tick);
            if(movie.finished())finish_movie();
        }
        animation_tick=tick;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        if (SDL_RenderClear(ren) < 0) return false;
        if(menu_open || movie.active()) {
            std::vector<uint8_t> pixels;
            if(movie.active())movie.draw_rgba(&pixels,dc4scene::kScreenW,dc4scene::kScreenH);
            else system_menu.draw(mx,my,mouse_down,font,&pixels);
            if(pixels.size()!=size_t(dc4scene::kScreenW)*dc4scene::kScreenH*4)return false;
            if (global_confirm.active()) {
                global_confirm.draw(mx, my, mouse_down, font, &pixels);
            }
            if(SDL_UpdateTexture(frame_texture.get(),nullptr,pixels.data(),dc4scene::kScreenW*4)<0)return false;
            if(SDL_RenderCopy(ren,frame_texture.get(),nullptr,nullptr)<0)return false;
        } else if (scene) {
            int reveal = o.shot.empty() ? -1 : o.reveal;
            if (o.shot.empty() && !line_done) {
                const int speed=std::clamp(system_menu.config().text_speed,0,11);
                const int ms_per_character[] = {120,100,80,65,50,40,30,24,18,12,6,0};
                reveal = speed==11 ? -1 : int((SDL_GetTicks() - line_start)/ms_per_character[speed]);
                if(speed==11){line_done=true;line_finished=SDL_GetTicks();}
                if (reveal >= int(scene->visible_text_length())) {
                    reveal = -1; line_done = true; line_finished = SDL_GetTicks();
                }
            }
            scene->set_pointer(mx, my, mouse_down);
            if(system_menu.config().effects==0)scene->finish_animations();
            const bool fully_revealed = reveal < 0 || reveal >= int(scene->visible_text_length());
            scene->set_wait_marker(fully_revealed && !scene->waiting_for_choice());
            scene->set_wait_frame(o.shot.empty() ? int(SDL_GetTicks() / 120) : o.wait_frame);
            const bool message_visible = scene->message_visible();
            if (backlog.open || slots.open) scene->set_message_visible(false);
            scene->draw(reveal);
            scene->set_message_visible(message_visible);
            const std::vector<uint8_t>* pixels = &scene->frame();
            std::vector<uint8_t> overlay;
            if (backlog.open) { overlay = *pixels; log_screen.draw(backlog.rows, backlog.scroll, thumbnails_on, mx, my, font, &overlay); pixels = &overlay; }
            else if (slots.open) {
                overlay = *pixels;
                save_screen.draw(slots.entries, mx, my, mouse_down, font, &overlay);
                pixels = &overlay;
            }
            if (global_confirm.active()) {
                overlay = *pixels;
                global_confirm.draw(mx, my, mouse_down, font, &overlay);
                pixels = &overlay;
            }
            if (SDL_UpdateTexture(frame_texture.get(), nullptr, pixels->data(), dc4scene::kScreenW * 4) < 0) return false;
            if (SDL_RenderCopy(ren, frame_texture.get(), nullptr, nullptr) < 0) return false;
        }
        return true;
    };

    if (o.confirm) {
        prompt_quit();
    }

    if (!o.shot.empty()) {
        if (scene && !scene->pending_movie().empty() && !movie.active()) {
            movie_from_scene = true;
            movie.open(data, scene->pending_movie(), audio_ready ? &audio.engine : nullptr, &err);
        }
        if (movie.active()) {
            const uint32_t target_ms = o.effect_ms >= 0 ? uint32_t(o.effect_ms) :
                                       (o.wait_frame > 0 ? uint32_t(o.wait_frame * 33) : 1500);
            for (uint32_t ms = 0; ms < target_ms && movie.active(); ms += 33) {
                movie.update(33);
            }
        }
        if (o.panel == "backlog") backlog.open = true;
        else if (o.panel == "save" || o.panel == "load") {
            open_slots(o.panel == "load");
            if (!slots.open) return 1;
            if (o.save_page) save_screen.set_page(o.save_page - 1);
            if (o.down) save_screen.pointer_down(mx, my);
        }
        if (!draw()) { fprintf(stderr, "Render failed: %s\n", SDL_GetError()); return 1; }
        // The software target owns the pixels; Present only flushes SDL's queue.
        SDL_RenderPresent(ren);
        if (SDL_SaveBMP(offscreen.get(), o.shot.c_str()) < 0) {
            fprintf(stderr, "Cannot save screenshot %s: %s\n", o.shot.c_str(), SDL_GetError()); return 1;
        }
        if (o.hover_x >= 0) {
            if (slots.open) {
                const auto hit = save_screen.hit_test(mx, my);
                if (hit.type == dc4ui::SaveActionType::Slot)
                    printf("Pointer (%d,%d): slot %d\n", mx, my, hit.index + 1);
                else printf("Pointer (%d,%d): save/load control %d\n", mx, my, int(hit.type));
            }
            else if(menu_open) {
                for(const auto& control:system_menu.controls())if(mx>=control.x&&mx<control.x+control.w&&my>=control.y&&my<control.y+control.h)
                    printf("Pointer (%d,%d): %s%s\n",mx,my,control.tag.c_str(),control.enabled?"":" (disabled)");
            } else {
                const std::string* tag = scene ? scene->hit_test(mx, my) : ui.hit_test(mx, my);
                printf("Pointer (%d,%d): %s\n", mx, my, tag ? tag->c_str() : "(no button)");
            }
        }
        if (scene && (o.debug || std::getenv("DC4_DEBUG"))) scene->dump_sprites();
        printf("Screenshot saved: %s (%dx%d)\n", o.shot.c_str(), o.width, o.height);
        return 0;
    }
    printf("%s\n", kHelp);
    update_title();
    std::string pressed_action;
    bool pressed_widget = false;
    while (run_loop) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (global_confirm.active()) {
                if (event.type == SDL_MOUSEMOTION) {
                    mx = event.motion.x; my = event.motion.y;
                } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                    mx=event.button.x;my=event.button.y;
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse_down = true;
                        global_confirm.pointer_down(mx, my);
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        finish_confirm(false);
                    }
                } else if (event.type == SDL_MOUSEBUTTONUP) {
                    mx=event.button.x;my=event.button.y;
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse_down = false;
                        const auto res = global_confirm.pointer_up(mx, my);
                        if(res!=dc4ui::ConfirmResult::None)finish_confirm(res==dc4ui::ConfirmResult::Yes);
                    }
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    const auto sym = event.key.keysym.sym;
                    if (sym == SDLK_ESCAPE || sym == SDLK_n) {
                        finish_confirm(false);
                    } else if (sym == SDLK_y || sym == SDLK_RETURN) {
                        finish_confirm(true);
                    }
                }
                if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
                    global_confirm.cancel_pointer();mouse_down=false;audio.engine.set_device_paused(true);movie.set_paused(true);
                }
                if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){
                    audio.engine.set_device_paused(false);movie.set_paused(false);animation_tick=SDL_GetTicks();
                }
                continue;
            }
            if((menu_open&&system_menu.confirming())||(slots.open&&save_screen.confirming())) {
                const bool in_menu=menu_open&&system_menu.confirming();
                auto answer=[&](bool yes){if(in_menu)menu_action(system_menu.answer_confirmation(yes));else save_action(save_screen.answer_confirmation(yes));mouse_down=false;};
                if(event.type==SDL_MOUSEMOTION){mx=event.motion.x;my=event.motion.y;}
                else if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP){
                    mx=event.button.x;my=event.button.y;
                    if(event.button.button==SDL_BUTTON_RIGHT&&event.type==SDL_MOUSEBUTTONDOWN)answer(false);
                    else if(event.button.button==SDL_BUTTON_LEFT){
                        mouse_down=event.type==SDL_MOUSEBUTTONDOWN;
                        if(mouse_down){if(in_menu)system_menu.pointer_down(mx,my);else save_screen.pointer_down(mx,my);}
                        else {if(in_menu)menu_action(system_menu.pointer_up(mx,my));else save_action(save_screen.pointer_up(mx,my));}
                    }
                }else if(event.type==SDL_KEYDOWN&&!event.key.repeat){
                    auto key=event.key.keysym.sym;
                    if(key==SDLK_ESCAPE||key==SDLK_n)answer(false);
                    else if(key==SDLK_RETURN||key==SDLK_y)answer(true);
                }else if(event.type==SDL_QUIT){answer(false);prompt_quit();}
                else if(event.type==SDL_WINDOWEVENT){
                    if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){system_menu.cancel_pointer();save_screen.cancel_pointer();mouse_down=false;audio.engine.set_device_paused(true);movie.set_paused(true);}
                    if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){audio.engine.set_device_paused(false);movie.set_paused(false);animation_tick=SDL_GetTicks();}
                }
                continue;
            }
            if (event.type == SDL_QUIT) { prompt_quit(); continue; }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                mouse_down = false; pressed_action.clear(); pressed_widget = false;
                save_screen.cancel_pointer();
                system_menu.cancel_pointer();movie.set_paused(true);audio.engine.set_device_paused(true);
                auto_mode = skip_mode = false; update_title();
            }
            if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){movie.set_paused(false);audio.engine.set_device_paused(false);animation_tick=SDL_GetTicks();}
            if (event.type == SDL_MOUSEMOTION) {
                mx = event.motion.x; my = event.motion.y;
                if (thumb_drag && backlog.open)
                    backlog.scroll = log_screen.thumb_scroll(my, backlog.rows.size());
                if(menu_open && mouse_down) {
                    auto changed=system_menu.pointer_move(mx,my);
                    if(changed.type!=dc4ui::SystemMenuActionType::None)menu_action(changed);
                }
            }
            if(movie.active()) {
                if((event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT) ||
                    (event.type==SDL_KEYDOWN && !event.key.repeat && (event.key.keysym.sym==SDLK_ESCAPE || event.key.keysym.sym==SDLK_SPACE || event.key.keysym.sym==SDLK_RETURN)))finish_movie();
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                // SDL_RenderSetLogicalSize translates event coordinates, including
                // high-DPI scale and letterboxing; do not scale these a second time.
                mx = event.button.x; my = event.button.y;
                if(menu_open) {
                    const auto before=system_menu.page();
                    if(event.button.button==SDL_BUTTON_RIGHT && event.type==SDL_MOUSEBUTTONDOWN)menu_action(system_menu.back());
                    else if(event.button.button==SDL_BUTTON_LEFT) {
                        if(event.type==SDL_MOUSEBUTTONDOWN){mouse_down=true;system_menu.pointer_down(mx,my);}
                        else {mouse_down=false;menu_action(system_menu.pointer_up(mx,my));}
                    }
                    if(menu_open&&before!=system_menu.page()) {
                        if(system_menu.page()==dc4ui::SystemMenuPage::Music){audio.engine.stop_bgm();music_running=false;}
                        else if(system_menu.page()==dc4ui::SystemMenuPage::Title||system_menu.page()==dc4ui::SystemMenuPage::Extras){title_music();music_running=false;}
                    }
                    continue;
                }
                if (slots.open) {
                    if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_MOUSEBUTTONDOWN) {
                        close_slots();
                    } else if (event.button.button == SDL_BUTTON_LEFT) {
                        if (event.type == SDL_MOUSEBUTTONDOWN) {
                            mouse_down = true;
                            save_screen.pointer_down(mx, my);
                        } else {
                            mouse_down = false;
                            save_action(save_screen.pointer_up(mx, my));
                        }
                    }
                    continue;
                }
                if (backlog.open) {
                    if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_MOUSEBUTTONDOWN) {
                        toggle_backlog();
                    } else if (event.button.button == SDL_BUTTON_LEFT) {
                        if (event.type == SDL_MOUSEBUTTONDOWN) {
                            mouse_down = true;
                            thumb_drag = log_screen.thumb_hit(mx, my, backlog.scroll, backlog.rows.size());
                        } else {
                            mouse_down = false;
                            if (thumb_drag) thumb_drag = false;
                            else if (log_screen.back_hit(mx, my)) toggle_backlog();
                            else if (log_screen.checkbox_hit(mx, my)) thumbnails_on = !thumbnails_on;
                            else {
                                const int jump = log_screen.jump_hit(mx, my, backlog.scroll, backlog.rows.size());
                                if (jump >= 0) prompt(dc4ui::ConfirmKind::SceneJump,[&,jump]{jump_to_row(jump);});
                                else {
                                    const int voice = log_screen.voice_hit(mx, my, backlog.scroll, backlog.rows.size());
                                    if (voice >= 0) play_row_voice(backlog.rows[size_t(voice)]);
                                }
                            }
                        }
                    }
                    continue;
                }
                if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_MOUSEBUTTONDOWN) {
                    if (scene) { scene->set_message_visible(!scene->message_visible()); auto_mode = skip_mode = false; update_title(); }
                }
                if (event.button.button != SDL_BUTTON_LEFT) continue;
                const std::string* tag = scene ?
                    (scene->message_visible() ? scene->hit_test(mx, my) : nullptr) : ui.hit_test(mx, my);
                bool over_widget = tag != nullptr;
                if (scene && scene->message_visible())
                    for (const auto& button : scene->buttons())
                        if (mx >= button.x && mx < button.x + button.w && my >= button.y && my < button.y + button.h)
                            over_widget = true;
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    mouse_down = true; pressed_action = tag ? *tag : ""; pressed_widget = over_widget;
                    continue;
                }
                const bool was_down = mouse_down;
                mouse_down = false;
                if (!was_down) continue;
                if (tag && *tag == pressed_action) action(*tag);
                else if (pressed_widget || over_widget) {
                    if (pressed_widget && over_widget && !tag) notify("This menu item is not available yet");
                } else if (mx >= 0 && mx < dc4scene::kScreenW && my >= 0 && my < dc4scene::kScreenH) {
                    auto_mode = skip_mode = false;
                    if (scene && scene->waiting_for_choice()) {
                        const int choice = scene->choice_at(mx, my);
                        if (choice >= 0) select_choice(size_t(choice));
                    } else advance();
                    update_title();
                }
                pressed_action.clear(); pressed_widget = false;
            }
            if (event.type == SDL_MOUSEWHEEL) {
                int delta = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
                if(menu_open){system_menu.scroll(-delta);continue;}
                if (slots.open) continue;
                if (!backlog.open && delta > 0) toggle_backlog();
                else if (backlog.open) backlog.move(-delta);
            }
            if (event.type != SDL_KEYDOWN) continue;
            const auto key = event.key.keysym.sym;
            if(!event.key.repeat && (key==SDLK_F11 || (key==SDLK_RETURN && (event.key.keysym.mod&KMOD_ALT)))) {
                system_menu.set_variable(99,1,!system_menu.config().fullscreen);profile_dirty=true;apply_settings();flush_profile();continue;
            }
            if(menu_open) {
                if(event.key.repeat)continue;
                if(key==SDLK_ESCAPE) {
                    if (system_menu.page() == dc4ui::SystemMenuPage::Title) prompt_quit();
                    else menu_action(system_menu.back());
                }
                else if(key==SDLK_LEFT || key==SDLK_PAGEUP)system_menu.scroll(-1);
                else if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN)system_menu.scroll(1);
                else if(key==SDLK_SPACE || key==SDLK_RETURN) {
                    if(system_menu.page()==dc4ui::SystemMenuPage::Title)activate_menu("prologue");
                    else {system_menu.pointer_down(mx,my);menu_action(system_menu.pointer_up(mx,my));}
                } else if(key>=SDLK_1&&key<=SDLK_7 && system_menu.page()==dc4ui::SystemMenuPage::Config)system_menu.set_config_page(int(key-SDLK_1));
                else if(key==SDLK_F1)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"dc4engine controls",kHelp,window.get());
                else if(key==SDLK_F8)prompt_quit();
                continue;
            }
            if ((key == SDLK_F5 || key == SDLK_F9) && !event.key.repeat) {
                prompt_quick(key == SDLK_F9); continue;
            }
            if (slots.open) {
                if (event.key.repeat) continue;
                save_screen.cancel_pointer();
                mouse_down = false;
                if (key == SDLK_ESCAPE) close_slots();
                else if (key >= SDLK_1 && key <= SDLK_9)
                    save_action(save_screen.activate({dc4ui::SaveActionType::Slot,dc4ui::SaveScreen::slot_index(save_screen.page(), int(key - SDLK_1))}));
                else if (key == SDLK_0) save_action(save_screen.activate({dc4ui::SaveActionType::Slot,dc4ui::SaveScreen::slot_index(save_screen.page(), 9)}));
                else if (key == SDLK_LEFT || key == SDLK_PAGEUP || key == SDLK_RIGHT || key == SDLK_PAGEDOWN) {
                    save_screen.change_page(key == SDLK_LEFT || key == SDLK_PAGEUP ? -1 : 1);
                    status.clear(); update_title();
                } else if (key == SDLK_TAB) {
                    save_screen.set_mode(save_screen.mode() == dc4ui::SaveMode::Save ? dc4ui::SaveMode::Load : dc4ui::SaveMode::Save);
                    status.clear(); update_title();
                }
                continue;
            }
            if (backlog.open) {
                if (key == SDLK_UP || key == SDLK_PAGEUP) backlog.move(key == SDLK_UP ? -1 : -4);
                else if (key == SDLK_DOWN || key == SDLK_PAGEDOWN) backlog.move(key == SDLK_DOWN ? 1 : 4);
                else if (key == SDLK_ESCAPE || key == SDLK_b || key == SDLK_SPACE || key == SDLK_RETURN) toggle_backlog();
                continue;
            }
            if (event.key.repeat) continue;
            if (key == SDLK_F11 || (key == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT))) {
                const bool full = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                if (SDL_SetWindowFullscreen(window.get(), full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) < 0) notify(SDL_GetError());
            } else if (key == SDLK_ESCAPE) {
                if (auto_mode || skip_mode) { auto_mode = skip_mode = false; update_title(); }
                else prompt_quit();
            } else if (key == SDLK_F8) prompt_quit();
            else if (key == SDLK_F1) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "dc4engine controls", kHelp, window.get());
            else if (key == SDLK_b) toggle_backlog();
            else if (scene && scene->waiting_for_choice() && key >= SDLK_1 && key <= SDLK_9) select_choice(size_t(key - SDLK_1));
            else if (key == SDLK_SPACE || key == SDLK_RETURN) { auto_mode = skip_mode = false; advance(); update_title(); }
            else if (key == SDLK_a) action(":auto");
            else if (key == SDLK_s) action(":rapid");
            else if (key == SDLK_v) action(":voice");
            else if (key == SDLK_h) action(":off");
        }
        if (!run_loop) break;
        // Refresh the pointer after a resize, when no motion event is required.
        int wx = 0, wy = 0;
        SDL_GetMouseState(&wx, &wy);
        float lx = 0, ly = 0;
        SDL_RenderWindowToLogical(ren, wx, wy, &lx, &ly);
        mx = int(lx); my = int(ly);
        if (!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MOUSE_FOCUS)) mx = my = -1;
        if(scene && !global_confirm.active() && !menu_open && !backlog.open && !slots.open && !movie.active()) {
            if(!scene->pending_movie().empty()){
                movie_from_scene=true;
                if(!movie.open(data,scene->pending_movie(),audio_ready?&audio.engine:nullptr,&err)){notify("Movie: "+err);scene->finish_movie();advance_script();}
            } else if(scene->script_ready())advance_script();
        }
        if(menu_open && system_menu.page()==dc4ui::SystemMenuPage::Music && !movie.active()) {
            system_menu.set_music_position(int(audio.engine.channel_fraction(0)*100));
            if(music_running&&!audio.engine.channel_playing(0)){music_running=false;menu_action(system_menu.activate("next"));}
        }
        if (scene && !global_confirm.active() && !menu_open && !movie.active() && !backlog.open && !slots.open && scene->message_visible() && !scene->waiting_for_choice() && !scene->waiting_for_time() && !scene->scene_ended()) {
            const Uint32 now = SDL_GetTicks();
            const bool focused = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
            const bool skip = focused && (skip_mode || (SDL_GetModState() & KMOD_CTRL));
            const auto cfg=system_menu.config();
            if(skip && !cfg.skip_unread && !current_line_read){skip_mode=false;}
            else if (skip && now - last_skip >= 100) { line_done = true; advance(); scene->finish_animations(); last_skip = now; }
            const Uint32 auto_delay=Uint32(std::max(0,21-cfg.auto_wait)*100 + scene->visible_text_length()*std::max(0,12-cfg.auto_speed)*8);
            if (auto_mode && focused && line_done && now - line_finished >= auto_delay && (!cfg.wait_for_voice || !scene->voice_playing()) && !scene->animating()) advance();
        }
        if (!draw()) { fprintf(stderr, "Render failed: %s\n", SDL_GetError()); return 1; }
        SDL_RenderPresent(ren);
        const Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 16) SDL_Delay(16 - elapsed);
    }
    mark_read();flush_profile();movie.close();
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, &options, &error)) {
        fprintf(stderr, "%s\n", error.c_str()); usage(); return 2;
    }
    if (options.help) { usage(); return 0; }
#ifdef _WIN32
    if (!options.shot.empty() || options.test_audio)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (options.debug || std::getenv("DC4_DEBUG")) std::setvbuf(stdout, nullptr, _IONBF, 0);
    try { return run(options); }
    catch (const std::exception& e) { fprintf(stderr, "dc4engine: %s\n", e.what()); return 1; }
}
