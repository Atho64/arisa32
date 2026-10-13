#include "dc4/resource.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>

namespace dc4 {
namespace {
namespace fs = std::filesystem;
std::mutex resource_mutex;
fs::path base_root, overlay_root;
bool utf8_text = false;
std::map<std::string, std::string> resolutions;
std::string folded(std::string value) {
    for (char& ch : value) {
        if (ch == '\\') ch = '/';
        else if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    }
    return value;
}
fs::path absolute_path(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::error_code error;
    auto absolute = fs::absolute(fs::u8path(path), error);
    return (error ? fs::u8path(path) : absolute).lexically_normal();
}
fs::path case_path(fs::path root, const fs::path& relative) {
    std::error_code error;
    for (const auto& part : relative) {
        if (part == ".") continue;
        if (part == ".." || part.has_root_path()) return {};
        const auto direct = root / part;
        if (fs::exists(direct, error)) { root = direct; continue; }
        error.clear();
        bool found = false;
        for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
            if (folded(it->path().filename().u8string()) == folded(part.u8string())) {
                root = it->path(); found = true; break;
            }
        }
        if (!found) return {};
    }
    return root;
}
Language current_language = Language::Japanese;
std::map<std::string, std::string> ui_translations_id;
std::map<std::string, std::string> ui_translations_en;
std::map<std::u16string, std::u16string> ui_u16_id;
std::map<std::u16string, std::u16string> ui_u16_en;
bool ui_translations_loaded = false;

void init_builtin_ui_translations() {
    if (ui_translations_loaded) return;
    ui_translations_loaded = true;
    ui_translations_id = {
        {"◆タイトル名◆", "◆Nama Judul◆"},
        {"\x81\x9f\x83\x5e\x83\x43\x83\x67\x83\x8b\x96\xbc\x81\x9f", "◆Nama Judul◆"},
        {"実行エラー", "Terjadi kesalahan eksekusi"},
        {"\x8e\xc0\x8d\x73\x83\x47\x83\x89\x81\x5b", "Terjadi kesalahan eksekusi"},
        {"」を実行できません！", "」tidak dapat dijalankan!"},
        {"タイトル名なし", "Tanpa nama judul"},
        {"\x83\x5e\x83\x43\x83\x67\x83\x8b\x96\xbc\x82\xc8\x82\xb5", "Tanpa nama judul"},
        {"接続に失敗しました。", "Koneksi gagal."},
        {"\x90\xda\x91\xb1\x82\xc9\x8e\xb8\x94\x73\x82\xb5\x82\xdc\x82\xb5\x82\xbd\x81\x42", "Koneksi gagal."},
        {"ネットに接続しますよろしいですか？", "Ingin menyambung ke internet?"},
        {"D.C.4 ～ダ・カーポ4～", "D.C.4 ~Da Capo 4~"},
        {"空良", "Sora"},
        {"\x8b\xf3\x97\xc7", "Sora"},
        {"櫻井", "Sakurai"},
        {"\x9f\x4e\x88\xe4", "Sakurai"},
        {"戻る", "Kembali"},
        {"\x96\xdf\x82\xe9", "Kembali"},
        {"決定", "Pilih"},
        {"\x8c\x88\x92\xe8", "Pilih"},
        {"キャンセル", "Batal"},
        {"\x83\x4c\x83\x83\x83\x93\x83\x5a\x83\x8b", "Batal"},
        {"はい", "Ya"},
        {"\x82\xcd\x82\xa2", "Ya"},
        {"いいえ", "Tidak"},
        {"\x82\xa2\x82\xa2\x82\xa6", "Tidak"},
        {"終了しますか？", "Ingin keluar?"},
        {"\x8f\x49\x97\xb9\x82\xb5\x82\xdc\x82\xb7\x82\xa9\x81\x48", "Ingin keluar?"},
        {"Menu data could not be loaded", "Data menu tidak dapat dimuat"},
        {"Save/Load artwork unavailable", "Grafik Simpan/Muat tidak tersedia"},
        {"Saved snapshot", "Data tersimpan"},
        {"Saved data", "Data tersimpan"},
        {"No Data", "Tidak ada data"},
        {"Back", "Kembali"},
        {"SAVE", "SIMPAN"},
        {"LOAD", "MUAT"},
        {"CONFIG", "PENGATURAN"},
        {"TITLE", "JUDUL"},
        {"END", "KELUAR"},
        {"QUIT", "KELUAR"},
        {"AUTO", "OTOMATIS"},
        {"NEW", "TERBARU"},
        {"QLOAD", "MUAT CEPAT"},
        {"QSAVE", "SIMPAN CEPAT"},
        {"LOG", "CATATAN"},
        {"BACKLOG", "CATATAN"},
        {"SKIP", "LEWATI"},
        // Story/chapter titles
        {"学生特権", "Hak Istimewa Pelajar"},
        {"\x8a\x77\x90\xb6\x93\xc1\x8c\xa0", "Hak Istimewa Pelajar"},
        {"秘されしモノ", "Sesuatu yang Tersembunyi"},
        {"\x94\xe9\x82\xb3\x82\xea\x82\xb5\x83\x82\x83\x6d", "Sesuatu yang Tersembunyi"},
        {"願いの軌跡", "Jejak Harapan"},
        {"\x8a\xe8\x82\xa2\x82\xcc\x8b\x4f\x90\xd5", "Jejak Harapan"},
        {"奇跡を願っても", "Meski Mengharap Keajaiban"},
        {"\x8a\xef\x90\xd5\x82\xf0\x8a\xe8\x82\xc1\x82\xc4\x82\xe0", "Meski Mengharap Keajaiban"},
        {"今この手の中の幸福", "Kebahagiaan di Tangan Ini Sekarang"},
        {"\x8d\xa1\x82\xb1\x82\xcc\x8e\xe8\x82\xcc\x92\x86\x82\xcc\x8d\x4b\x95\x9f", "Kebahagiaan di Tangan Ini Sekarang"},
        {"愛という幸福", "Kebahagiaan Bernama Cinta"},
        {"恋という奇跡", "Keajaiban Bernama Cinta"},
        {"変わるもの、変わらないもの", "Yang Berubah, Yang Tak Berubah"},
        {"世界で一番大切な人", "Orang Paling Berharga di Dunia"}
    };

    ui_translations_en = {
        {"◆タイトル名◆", "◆Title Name◆"},
        {"\x81\x9f\x83\x5e\x83\x43\x83\x67\x83\x8b\x96\xbc\x81\x9f", "◆Title Name◆"},
        {"実行エラー", "Execution Error"},
        {"\x8e\xc0\x8d\x73\x83\x47\x83\x89\x81\x5b", "Execution Error"},
        {"タイトル名なし", "Untitled"},
        {"\x83\x5e\x83\x43\x83\x67\x83\x8b\x96\xbc\x82\xc8\x82\xb5", "Untitled"},
        {"戻る", "Back"},
        {"\x96\xdf\x82\xe9", "Back"},
        {"決定", "Select"},
        {"\x8c\x88\x92\xe8", "Select"},
        {"キャンセル", "Cancel"},
        {"\x83\x4c\x83\x83\x83\x93\x83\x5a\x83\x8b", "Cancel"},
        {"はい", "Yes"},
        {"\x82\xcd\x82\xa2", "Yes"},
        {"いいえ", "No"},
        {"\x82\xa2\x82\xa2\x82\xa6", "No"},
        {"終了しますか？", "Do you want to quit?"},
        {"\x8f\x49\x97\xb9\x82\xb5\x82\xdc\x82\xb7\x82\xa9\x81\x48", "Do you want to quit?"}
    };

    ui_u16_id = {
        {u"No Data", u"Tidak ada data"},
        {u"Saved data", u"Data tersimpan"},
        {u"Saved snapshot", u"Data tersimpan"},
        {u"SAVE", u"SIMPAN"},
        {u"LOAD", u"MUAT"},
        {u"Back", u"Kembali"},
        {u"AUTO", u"OTOMATIS"},
        {u"NEW", u"BARU"},
        {u"QLOAD", u"M.CEPAT"},
        {u"CONFIG", u"PENGATURAN"},
        {u"TITLE", u"JUDUL"},
        {u"END", u"KELUAR"},
        {u"戻る", u"Kembali"},
        {u"決定", u"Pilih"},
        {u"キャンセル", u"Batal"},
        {u"はい", u"Ya"},
        {u"いいえ", u"Tidak"},
        {u"Save/Load artwork unavailable", u"Grafik Simpan/Muat tidak tersedia"},
        {u"Menu data could not be loaded", u"Data menu tidak dapat dimuat"}
    };

    ui_u16_en = {
        {u"戻る", u"Back"},
        {u"決定", u"Select"},
        {u"キャンセル", u"Cancel"},
        {u"はい", u"Yes"},
        {u"いいえ", u"No"}
    };
}
}  // namespace

void configure_resources(const std::string& base, const std::string& overlay,
                         const std::string& encoding) {
    std::lock_guard<std::mutex> lock(resource_mutex);
    base_root = base.empty() ? fs::path() : absolute_path(base);
    overlay_root = overlay.empty() ? fs::path() : absolute_path(overlay);
    utf8_text = folded(encoding) == "utf-8" || folded(encoding) == "utf8";
    resolutions.clear();
    init_builtin_ui_translations();
    if (!overlay.empty()) {
        std::string ov = folded(overlay);
        if (ov.find("indonesian") != std::string::npos || ov.find("id_data") != std::string::npos ||
            ov.find("/id") != std::string::npos || ov.find("\\id") != std::string::npos) {
            current_language = Language::Indonesian;
        } else if (ov.find("english") != std::string::npos || ov.find("eng_data") != std::string::npos ||
                   ov.find("/en") != std::string::npos || ov.find("\\en") != std::string::npos) {
            current_language = Language::English;
        }
    } else {
        current_language = Language::Japanese;
    }
}

std::string resolve_asset(const std::string& requested) {
    if (requested.empty()) return {};
    std::lock_guard<std::mutex> lock(resource_mutex);
    const auto cached = resolutions.find(requested);
    if (cached != resolutions.end()) return cached->second;
    const auto path = absolute_path(requested);
    auto result = path;
    auto relative = path.lexically_relative(base_root);
    // lexical_relative is case-sensitive, whereas the original engine isn't.
    if (!base_root.empty()) {
        const auto prefix = folded(base_root.generic_u8string()) + '/';
        const auto full = path.generic_u8string();
        if (folded(full).compare(0, prefix.size(), prefix) == 0)
            relative = fs::u8path(full.substr(prefix.size()));
        else relative.clear();
    } else relative.clear();
    if (!relative.empty() && *relative.begin() != "..") {
        const std::string category = folded(relative.begin()->u8string());
        if (!overlay_root.empty() && (category == "grp" || category == "mes" || category == "movie")) {
            const auto translated = case_path(overlay_root, relative);
            std::error_code error;
            if (!translated.empty() && fs::is_regular_file(translated, error)) {
                const auto text = translated.u8string();
                resolutions[requested] = text;
                return text;
            }
        }
        const auto original = case_path(base_root, relative);
        if (!original.empty()) result = original;
    } else {
        const auto found = case_path(path.root_path(), path.relative_path());
        if (!found.empty()) result = found;
    }
    const auto text = result.u8string();
    std::error_code error;
    if (fs::is_regular_file(result, error)) resolutions[requested] = text;
    return text;
}

bool asset_exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::u8path(resolve_asset(path)), error);
}
bool prefers_utf8_text() {
    std::lock_guard<std::mutex> lock(resource_mutex);
    return utf8_text;
}
bool decode_utf8(const std::string& bytes, std::u16string* output) {
    if (!output) return false;
    std::u16string result;
    for (size_t i = 0; i < bytes.size();) {
        const unsigned char lead = bytes[i++];
        uint32_t code = lead;
        int count = 0;
        if (lead < 0x80) count = 0;
        else if (lead >= 0xC2 && lead <= 0xDF) { count = 1; code &= 0x1F; }
        else if (lead >= 0xE0 && lead <= 0xEF) { count = 2; code &= 0x0F; }
        else if (lead >= 0xF0 && lead <= 0xF4) { count = 3; code &= 7; }
        else return false;
        if (i + count > bytes.size()) return false;
        for (int n = 0; n < count; ++n) {
            const unsigned char byte = bytes[i++];
            if ((byte & 0xC0) != 0x80) return false;
            code = (code << 6) | (byte & 63);
        }
        if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
            (count == 3 && code < 0x10000) || code > 0x10FFFF ||
            (code >= 0xD800 && code <= 0xDFFF)) return false;
        if (code == 0xFEFF && result.empty()) continue;
        if (code <= 0xFFFF) result.push_back(char16_t(code));
        else { code -= 0x10000; result.push_back(char16_t(0xD800 + (code >> 10))); result.push_back(char16_t(0xDC00 + (code & 1023))); }
    }
    *output = std::move(result);
    return true;
}

void set_language(Language lang) {
    std::lock_guard<std::mutex> lock(resource_mutex);
    current_language = lang;
}

Language get_language() {
    std::lock_guard<std::mutex> lock(resource_mutex);
    return current_language;
}

bool is_latin() {
    return get_language() != Language::Japanese;
}

std::string translate_ui(const std::string& text) {
    std::lock_guard<std::mutex> lock(resource_mutex);
    init_builtin_ui_translations();
    if (current_language == Language::Indonesian) {
        auto it = ui_translations_id.find(text);
        if (it != ui_translations_id.end()) return it->second;
    } else if (current_language == Language::English) {
        auto it = ui_translations_en.find(text);
        if (it != ui_translations_en.end()) return it->second;
    }
    return text;
}

std::u16string translate_ui(const std::u16string& text) {
    std::lock_guard<std::mutex> lock(resource_mutex);
    init_builtin_ui_translations();
    if (current_language == Language::Indonesian) {
        auto it = ui_u16_id.find(text);
        if (it != ui_u16_id.end()) return it->second;
    } else if (current_language == Language::English) {
        auto it = ui_u16_en.find(text);
        if (it != ui_u16_en.end()) return it->second;
    }
    return text;
}

int romanized_face_id(const std::u16string& name) {
    if (name.empty()) return -1;
    std::string s;
    for (char16_t ch : name) {
        if (ch < 0x80) s.push_back(char(std::tolower(int(ch))));
        else return -2;
    }
    static const std::pair<const char*, int> table[] = {
        {"ichito", 0},
        {"arisu", 1},
        {"arisa", 2},
        {"nino", 3},
        {"sorane", 4},
        {"hiyori", 5},
        {"shiina", 6},
        {"miu", 7},
        {"chiyoko", 8},
        {"choco", 9},
        {"suginami", 10},
        {"kanata", 11},
        {"gen", 12},
        {"izumi", 13},
        {"kotori", 15},
        {"alice", 16},
        {"cheshi", 17},
        {"choeshi", 17},
        {"azusa", 18},
        {"lala", 19},
        {"lili", 20},
        {"lulu", 21},
        {"saki", 22},
        {"naoki", 23},
        {"naozumi", 23},
        {"yuji", 24},
        {"yuuji", 24},
        {"reiji", 24},
        {"towako", 25},
        {"mitsumi", 26},
        {"masayoshi", 27},
        {"kaho", 28},
        {"kasumi", 28},
    };
    for (const auto& entry : table) {
        if (s == entry.first) return entry.second;
    }
    return -2;
}
}  // namespace dc4


