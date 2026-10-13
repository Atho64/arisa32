#include "dc4/system_menu.h"
#include "dc4/resource.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dc4ui {
namespace {
using Action = SystemMenuAction;
using Type = SystemMenuActionType;
using Page = SystemMenuPage;
using Command = dc4::MesCommand;
constexpr int W = 1280, H = 720;
constexpr size_t FrameBytes = size_t(W) * H * 4;
int sw(uint16_t word) { return int(int16_t(word)); }
std::string key(std::string name) {
    for (char& c : name) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
    for (const char* ext : {".mes", ".crx", ".pcm", ".mpg"})
        if (name.size() >= 4 && name.compare(name.size() - 4, 4, ext) == 0) name.resize(name.size() - 4);
    return name;
}
bool safe_name(const std::string& name) {
    return !name.empty() && name.size() < 512 && name.front() != '/' &&
           name.find(':') == std::string::npos && name.find("..") == std::string::npos &&
           name.find('\0') == std::string::npos;
}
std::string resolve(const std::string& root, const std::string& name) {
    std::filesystem::path path(root);
    for (const auto& part : std::filesystem::path(name)) {
        std::string found = dc4::find_file_ci(path.string(), part.string());
        path /= found.empty() ? part.string() : found;
    }
    return path.string();
}
bool read_bytes(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream in(dc4::resolve_asset(path), std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
uint32_t u32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
}
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool has(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
struct Chip { std::string image; Rect src, dst; int state = 99, control = -1; };
struct Widget {
    Rect hit;
    std::string tag;
    bool enabled = true;
    int slider_bank = -1, slider_index = 0, slider_count = 0, slider_left = 0, slider_right = 0;
};
struct Caption { int x, y, size, control = -1, state = 99; std::string text; };
struct Script {
    std::vector<Command> code;
    std::map<std::string, size_t> labels;
};
void blit(const dc4scene::Image& image, const Rect& src, const Rect& dst, std::vector<uint8_t>* frame, int alpha = 255) {
    if (!image.valid() || src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) return;
    for (int y = std::max(dst.y, 0); y < std::min(dst.y + dst.h, H); ++y) {
        int sy = src.y + int(int64_t(y - dst.y) * src.h / dst.h);
        if (sy < 0 || sy >= image.h) continue;
        for (int x = std::max(dst.x, 0); x < std::min(dst.x + dst.w, W); ++x) {
            int sx = src.x + int(int64_t(x - dst.x) * src.w / dst.w);
            if (sx < 0 || sx >= image.w) continue;
            const uint8_t* s = &image.rgba[(size_t(sy) * image.w + sx) * 4];
            uint8_t* d = &(*frame)[(size_t(y) * W + x) * 4];
            int a = s[3] * alpha / 255;
            for (int c = 0; c < 3; ++c) d[c] = uint8_t((s[c] * a + d[c] * (255 - a)) / 255);
            d[3] = 255;
        }
    }
}
}

struct SystemMenu::Impl {
    std::string data, last_error, script_name, background;
    std::string replay_menu = "sce_asa";
    int replay_page = 0, replay_route = 0;
    ConfirmDialog confirm;
    std::string pending_tag;
    Page page = Page::Title, config_return = Page::Title;
    bool ready = false, in_game = false, latest = false, quick = false, automatic = false;
    std::map<std::pair<int, int>, int> vars;
    std::set<std::string> seen_cg, seen_music, seen_scene;
    std::map<std::string, Script> scripts;
    mutable std::map<std::string, dc4scene::Image> images;
    std::map<int, std::string> sheets;
    std::vector<std::string> bgm_names, cg_names, scene_names;
    std::vector<Widget> widgets;
    std::vector<Chip> chips;
    std::vector<Caption> captions;
    std::vector<Chip> music_underlay;
    std::array<Chip, 3> list_back;
    std::vector<Rect> list_rects;
    std::map<int, std::array<Chip, 3>> list_titles;
    std::set<int> playlist;
    int music_top = 0, music_selected = -1;
    int pressed = -1;
    bool held = false, gallery_view = false, movie_menu = false;
    std::vector<int> gallery_variants;
    size_t gallery_variant = 0;
    std::mt19937 random{std::random_device{}()};

    Impl() { defaults(); }
    void defaults() {
        const std::pair<int, int> defaults[] = {
            {5,1},{7,80},{8,80},{9,100},{10,1},{11,80},{12,5},{14,1},{15,1},
            {26,8},{27,8},{28,1},{29,12},{30,0},{180,1},{181,1},{190,1},{194,6},
            {200,1},{203,1},{206,50},{207,100},{208,0},{209,1},{210,1},{213,1},{225,100},
            {245,-1},{246,-1},{253,-1},{254,-1}
        };
        for (auto v : defaults) vars[{99, v.first}] = v.second;
        vars[{100,49}] = 80;
        for (int i = 0; i < 51; ++i) { vars[{94,i}] = 100; vars[{95,i}] = 0; }
    }
    int get(int b, int i) const {
        if (b == 99 && i == 4) return get(99,1);
        if (b == 99 && i == 20) return latest;
        if (b == 99 && i == 21) return quick;
        if (b == 99 && i == 22) return automatic;
        auto at = vars.find({b,i}); return at == vars.end() ? 0 : at->second;
    }
    void put(int b, int i, int value) {
        if (b < 0 || b > 100 || i < 0 || i > 4095) return;
        vars[{b,i}] = value;
        if ((b==94 || b==95) && i==50) for(int character=0;character<18;++character)vars[{b,character}]=value;
        if (b == 100 && i == 49) vars[{99,11}] = value;
        if (b == 99 && i == 11) vars[{100,49}] = value;
    }
    dc4scene::Image* image(const std::string& raw) const {
        std::string name = key(raw);
        if (!name.empty() && name.front() == '*') {
            int id = -1; try { id = std::stoi(name.substr(1)); } catch (...) {}
            if (id < 0 || size_t(id) >= cg_names.size()) return nullptr;
            name = key(cg_names[size_t(id)]);
        }
        if (!safe_name(name)) return nullptr;
        auto found = images.find(name);
        if (found != images.end()) return &found->second;
        dc4scene::Image result;
        std::vector<uint8_t> bytes;
        dc4::CrxInfo info;
        if (read_bytes(resolve(data + "/GRP", name + ".crx"), &bytes) &&
            dc4::crx_decode(bytes.data(), bytes.size(), &result.rgba, &info)) {
            result.w = info.width; result.h = info.height; result.name = name;
        }
        return &images.emplace(name, std::move(result)).first->second;
    }
    Script* script(const std::string& raw) {
        std::string name = key(raw);
        auto found = scripts.find(name);
        if (found != scripts.end()) return &found->second;
        if (!safe_name(name)) return nullptr;
        dc4::MesFile mes;
        if (!mes.load_file(resolve(data + "/MES", name + ".mes"), &last_error)) return nullptr;
        Script result;
        dc4::MesStream stream(mes); Command c;
        while (stream.next(&c) && c.op) {
            if (c.op == 0x4B) result.labels.emplace(c.str, result.code.size());
            result.code.push_back(c);
        }
        return &scripts.emplace(name, std::move(result)).first->second;
    }
    bool names() {
        std::vector<uint8_t> b;
        if (!read_bytes(resolve(data, "GRP/NAMES.DAT"), &b) || b.size() < 232 || u32(b,0) != 7) return false;
        size_t p = 228;
        auto table = [&](size_t stride, std::vector<std::string>* target) {
            if (p + 4 > b.size()) return false;
            uint32_t n = u32(b,p); p += 4;
            if (n > 2000 || n > (b.size() - p) / stride) return false;
            if (target) target->clear();
            for (uint32_t i = 0; i < n; ++i, p += stride) {
                const char* s = reinterpret_cast<const char*>(b.data() + p);
                const char* end = static_cast<const char*>(std::memchr(s,0,32));
                if (!end) return false;
                if (target) target->emplace_back(s,end);
            }
            return true;
        };
        return table(48,&bgm_names) && table(36,nullptr) && table(32,&cg_names) && table(32,&scene_names);
    }
    void progress_vars() {
        for (size_t i = 0; i < cg_names.size(); ++i) put(97,int(i),seen_cg.count(key(cg_names[i])) ? 1 : 0);
        for (size_t i = 0; i < scene_names.size(); ++i) put(98,int(i),seen_scene.count(key(scene_names[i])) ? 1 : 0);
        for (size_t i = 0; i < bgm_names.size(); ++i) put(96,int(i),seen_music.count(key(bgm_names[i])) ? 1 : 0);
    }
    void clear_layout() { widgets.clear(); chips.clear(); captions.clear(); list_rects.clear(); list_titles.clear(); }
    int hit(int x,int y) const {
        for (int i = int(widgets.size()) - 1; i >= 0; --i)
            if (widgets[size_t(i)].enabled && widgets[size_t(i)].hit.has(x,y)) return i;
        return -1;
    }
    Action run(std::string name, std::string label, bool action, int transfer_depth = 0) {
        if (transfer_depth > 8) { last_error = "Menu transfer depth exceeded"; return {}; }
        Script* source = script(name);
        if (!source) return {};
        script_name = key(name);
        size_t pc = 0;
        if (!label.empty()) {
            auto at = source->labels.find(label);
            if (at == source->labels.end()) { last_error = "Missing menu label: " + label; return {}; }
            pc = at->second;
        }
        int control = -1, list_id = -1;
        bool expect_tag = false;
        std::string entry;
        Command pending;
        Action output;
        std::map<size_t,int> visits;
        auto skip = [&](int scope) {
            while (pc < source->code.size()) {
                const auto& marker = source->code[pc++];
                if (marker.op == 0x20 && marker.p2 == scope) return;
            }
        };
        for (size_t budget = 0; pc < source->code.size() && budget < 30000; ++budget) {
            const Command c = source->code[pc++];
            const auto& w = c.words;
            if (c.op == 0x4B && ++visits[pc] > 3) { last_error = "Menu loop without event yield: " + c.str; break; }
            switch (c.op) {
            case 0x00: return output;
            case 0x0B: case 0x0C: return output;
            case 0x20: if (c.p1 == 0) skip(c.p2); break;
            case 0x86: {
                int left = get(w[0],w[1]), right = sw(w[2]), op = w[3];
                if (op & 0x80) { right = get(w[2] >> 8,w[2] & 255); op &= 127; }
                bool yes = op == 0 ? left == right : op == 1 ? left != right : op == 2 ? left > right :
                           op == 3 ? left < right : op == 4 ? left >= right : op == 5 && left <= right;
                if (pc < source->code.size() && source->code[pc].op == 2) {
                    int scope = source->code[pc++].p1; if (!yes) skip(scope);
                }
                break;
            }
            case 0x82: {
                int value = sw(w[2]);
                if (w[3] == 1) value += get(w[0],w[1]);
                else if (w[3] == 2) value = get(w[0],w[1]) - value;
                put(w[0],w[1],value); break;
            }
            case 0x83: put(w[0],w[1],get(w[2],w[3])); break;
            case 0x84: {
                int sum = 0;
                while (pc < source->code.size()) {
                    const auto& term = source->code[pc++]; if (term.op == 2) break;
                    sum += get(term.words[0],term.words[1]);
                }
                put(w[0],w[1],sum); break;
            }
            case 0x85: put(w[0],w[1],get(w[0],w[1])+get(w[2],w[3])); break;
            case 0x3C: case 0x3D:
                if (c.p1 != 0 && c.p1 != 28) {
                    sheets[c.p1 + (c.op == 0x3D ? 37 : 0)] = c.str;
                    image(c.str);
                }
                break;
            case 0x49:
                background = c.str; image(background);
                if (!c.str.empty() && c.str.front() == '*') {
                    gallery_view = true;
                    return output;
                }
                break;
            case 0x24:
                if (c.p1 == 0) { clear_layout(); control = -1; expect_tag = false; }
                break;
            case 0x1A: if (c.p1 == 0) { control = -1; expect_tag = false; } break;
            case 0x72:
                widgets.push_back({{sw(w[0]),sw(w[1]),sw(w[2])-sw(w[0]),sw(w[3])-sw(w[1])},"",true});
                control = int(widgets.size()) - 1; expect_tag = true; break;
            case 0x54:
                if (expect_tag && control >= 0) {
                    widgets[size_t(control)].tag = c.str;
                    widgets[size_t(control)].enabled = !c.str.empty();
                    expect_tag = false;
                } else {
                    auto at = source->labels.find(c.str);
                    if (at != source->labels.end()) { pc = at->second; control = -1; }
                }
                break;
            case 0x73: case 0x74: case 0x75: case 0x7A: pending = c; break;
            case 0x77: {
                if (pending.op == 0) break;
                int state = sw(w[3]);
                Chip chip{sheets[w[2]],{sw(pending.words[2]),sw(pending.words[3]),w[0],w[1]},
                    {sw(pending.words[0]),sw(pending.words[1]),w[0],w[1]},state,control};
                if (pending.op == 0x73 && control >= 0) {
                    auto& widget = widgets[size_t(control)];
                    if (state == 5) {
                        widget.slider_bank = pending.words[3]; widget.slider_index = w[0];
                        widget.slider_count = pending.words[2]; widget.slider_left = sw(pending.words[0]);
                        widget.slider_right = sw(pending.words[1]);
                    } else if (state == 6 || state == 8 || state == 10) chips.push_back(chip);
                    else if (state == 99 || state == 1 || state == 2) chips.push_back(chip);
                } else if (pending.op == 0x7A) { chip.control = -1; chips.push_back(chip); }
                else if (pending.op == 0x74 && state >= 0) list_back[state == 99 ? 0 : std::min(state,2)] = chip;
                else if (pending.op == 0x75 && list_id >= 0 && (state == 99 || state == 1 || state == 2))
                    list_titles[list_id][state == 99 ? 0 : state] = chip;
                pending = Command{}; break;
            }
            case 0x7E: list_rects.push_back({sw(w[0]),sw(w[1]),sw(w[2])-sw(w[0]),sw(w[3])-sw(w[1])}); break;
            case 0x7F: list_id = w[0]; pending = Command{}; break;
            case 0x76: case 0x87:
                if (pc < source->code.size()) {
                    const auto& text = source->code[pc++];
                    captions.push_back({sw(w[0]),sw(w[1]),c.op == 0x87 ? int(w[2]) : 20,
                        c.op == 0x76 ? control : -1,c.op == 0x76 ? int(w[3]) : 99,text.str});
                }
                break;
            case 0x51: entry = c.str; break;
            case 0x4F: case 0x50: {
                const std::string dest = key(c.str);
                if (dest.rfind("cg_",0) == 0 || dest.rfind("sce_",0) == 0) {
                    page = dest.rfind("cg_",0) == 0 ? Page::Gallery : Page::Scenes;
                    return run(dest,entry,action,transfer_depth+1);
                }
                if (dest == "main") { page = Page::Extras; return run("main","ex_main",false,transfer_depth+1); }
                if (dest.rfind("dc4_",0) == 0) return {Type::StartScene,-1,c.str,entry};
                if (dest == "save") return {Type::Save};
                if (dest == "load") return {Type::Load};
                return output;
            }
            case 0x4C: if (action) output = {Type::PlayVoice,-1,c.str,{}}; break;
            case 0x1E:
                if (c.p1 == 202) put(3,0,playlist.count(music_top+c.p2) ? 1 : 0);
                break;
            default: break;
            }
        }
        return output;
    }
    void music_rows() {
        if (page != Page::Music || movie_menu) return;
        for (size_t i = 0; i < list_rects.size(); ++i) {
            int id = music_top + int(i);
            if (list_titles.find(id) == list_titles.end()) continue;
            bool unlocked = size_t(id) < bgm_names.size() && seen_music.count(key(bgm_names[size_t(id)]));
            const Rect r = list_rects[i];
            int control = int(widgets.size());
            widgets.push_back({r,"#music:"+std::to_string(id),unlocked});
            for (int s = 0; s < 3; ++s) {
                Chip back = list_back[size_t(s)]; back.control = control; back.state = s == 0 ? 99 : s;
                back.dst.x += r.x; back.dst.y += r.y; chips.push_back(back);
                if (unlocked) {
                    Chip title = list_titles[id][size_t(s)]; title.control = control; title.state = s == 0 ? 99 : s;
                    title.dst.x += r.x; title.dst.y += r.y; chips.push_back(title);
                }
            }
        }
        widgets.push_back({{1072,216,40,384},"#music:scroll",true, -2,0,42,216,600});
    }
    void rebuild() {
        if (!ready || gallery_view) return;
        held = false; pressed = -1;
        put(9,7,in_game ? 0 : 1);
        put(9,87,latest ? 0 : 1);
        put(9,91,1);
        clear_layout();
        if (page == Page::Title) run("main","main",false);
        else if (page == Page::Extras) run("main","ex_main",false);
        else if (page == Page::Config) { put(3,97,2); run("config","menu1",false); }
        else if (page == Page::Gallery || page == Page::Scenes) run(script_name,"menu",false);
        else if (page == Page::Music) {
            if (movie_menu) run("music","qmovie",false);
            else { run("music","menu",false); music_rows(); }
        }
    }
    Action play_music(int selected, int direction = 1) {
        int count = int(list_titles.size()); if (!count) count = std::min(42,int(bgm_names.size()));
        for (int n = 0; n < count; ++n) {
            int id = (selected + direction * n + count * 2) % count;
            if (size_t(id) < bgm_names.size() && seen_music.count(key(bgm_names[size_t(id)])) &&
                (!get(100,21) || playlist.count(id))) {
                music_selected = id; put(3,11,0); put(99,187,id); put(99,189,0);
                rebuild(); return {Type::PlayMusic,id,bgm_names[size_t(id)],{}};
            }
        }
        return {};
    }
    Action slider(int id, int x, int y) {
        if (id < 0 || size_t(id) >= widgets.size()) return {};
        const auto widget = widgets[size_t(id)];
        if (widget.slider_bank == -2) {
            music_top = std::clamp((y - 216) * 36 / 384,0,36);
            rebuild(); return {};
        }
        if (widget.slider_bank < 0 || widget.slider_right <= widget.slider_left) return {};
        int max = widget.slider_count - 1;
        int value = std::clamp(int(std::lround(double(x-widget.slider_left)*max /
                        (widget.slider_right-widget.slider_left))),0,std::max(max,0));
        put(widget.slider_bank,widget.slider_index,value);
        if (widget.slider_bank == 3 && widget.slider_index == 41) put(94,get(100,55),value);
        if (page == Page::Music && widget.slider_bank == 99 && widget.slider_index == 189)
            return {Type::SeekMusic,value};
        return {Type::SettingsChanged};
    }
};

SystemMenu::SystemMenu() : p_(std::make_unique<Impl>()) {}
SystemMenu::~SystemMenu() = default;
SystemMenu::SystemMenu(SystemMenu&&) noexcept = default;
SystemMenu& SystemMenu::operator=(SystemMenu&&) noexcept = default;
bool SystemMenu::load(const std::string& data_dir,std::string* error) {
    p_->data = data_dir; p_->scripts.clear(); p_->images.clear(); p_->last_error.clear(); p_->ready = false;
    p_->confirm.load(data_dir);
    for (const char* name : {"main","config","music","cg_asa","sce_asa"})
        if (!p_->script(name)) { if (error) *error = p_->last_error; return false; }
    if (!p_->names()) { p_->last_error = "Invalid or missing version 7 GRP/NAMES.DAT"; if (error) *error = p_->last_error; return false; }
    for (const char* name : {"system/titleback02","system/titlechip01","system/configback01","system/configchip01","system/exchip"}) {
        auto* img = p_->image(name);
        if (!img || !img->valid()) { p_->last_error = std::string("Missing menu artwork: ") + name; if(error)*error=p_->last_error; return false; }
    }
    p_->ready = true; p_->progress_vars(); open(Page::Title); if(error)error->clear(); return true;
}
bool SystemMenu::loaded() const { return p_->ready; }
SystemMenuPage SystemMenu::page() const { return p_->page; }
void SystemMenu::resume_scenes() {
    cancel_pointer();
    p_->page = Page::Scenes; p_->gallery_view = p_->movie_menu = false;
    p_->script_name = p_->replay_menu;
    p_->put(3,5,p_->replay_route); p_->put(3,6,p_->replay_page);
    p_->rebuild();
}
void SystemMenu::open(SystemMenuPage page) {
    p_->confirm.close();p_->pending_tag.clear();cancel_pointer();
    if (page == Page::Config && p_->page != Page::Config) p_->config_return = p_->page;
    p_->page = page; p_->gallery_view = false; p_->movie_menu = false;
    if (page == Page::Gallery || page == Page::Scenes) {
        p_->put(3,5,0); p_->put(3,6,0); p_->script_name = page == Page::Gallery ? "cg_asa" : "sce_asa";
    }
    if (page == Page::Music) { p_->put(3,11,p_->music_selected < 0); p_->put(99,179,1); }
    p_->rebuild();
}
void SystemMenu::set_in_game(bool value) { p_->in_game = value; p_->rebuild(); }
void SystemMenu::set_save_available(bool latest,bool quick,bool automatic) {
    p_->latest=latest; p_->quick=quick; p_->automatic=automatic; p_->rebuild();
}
void SystemMenu::set_config_page(int n) { if(confirming())return;p_->put(100,54,std::clamp(n,0,6)); if(p_->page==Page::Config)p_->rebuild(); }
int SystemMenu::config_page() const { return p_->get(100,54); }
void SystemMenu::pointer_down(int x,int y) { if(confirming()){p_->confirm.pointer_down(x,y);return;}p_->held=true; p_->pressed=p_->hit(x,y); }
Action SystemMenu::pointer_move(int x,int y) {
    if(confirming())return {};
    if (!p_->held || p_->pressed < 0) return {};
    return p_->slider(p_->pressed,x,y);
}
Action SystemMenu::pointer_up(int x,int y) {
    if(confirming()){
        const auto result=p_->confirm.pointer_up(x,y);
        if(result==ConfirmResult::None)return {};
        auto tag=std::move(p_->pending_tag);p_->pending_tag.clear();
        return result==ConfirmResult::Yes?activate_confirmed(tag):Action{};
    }
    if (p_->gallery_view) { cancel_pointer(); scroll(1); return {}; }
    int pressed=p_->pressed, hit=p_->hit(x,y); bool held=p_->held;
    if (held && pressed >= 0 && size_t(pressed)<p_->widgets.size() && p_->widgets[size_t(pressed)].slider_bank != -1) {
        auto out=p_->slider(pressed,x,y); cancel_pointer(); return out;
    }
    std::string tag = held && pressed == hit && hit >= 0 ? p_->widgets[size_t(hit)].tag : "";
    cancel_pointer(); return tag.empty() ? Action{} : activate(tag);
}
void SystemMenu::cancel_pointer() { p_->held=false; p_->pressed=-1;p_->confirm.cancel_pointer(); }
void SystemMenu::scroll(int delta) {
    if (!delta||confirming()) return;
    if (p_->gallery_view && !p_->gallery_variants.empty()) {
        int n=int(p_->gallery_variants.size());
        p_->gallery_variant=size_t((int(p_->gallery_variant)+delta%n+n)%n);
        p_->background="*"+std::to_string(p_->gallery_variants[p_->gallery_variant]); p_->image(p_->background);
    } else if (p_->page==Page::Music && !p_->movie_menu) {
        p_->music_top=std::clamp(p_->music_top+delta,0,std::max(0,int(p_->list_titles.size())-6)); p_->rebuild();
    } else if (p_->page==Page::Gallery || p_->page==Page::Scenes) {
        const int count=p_->get(3,2)+1;
        p_->put(3,6,(p_->get(3,6)+delta%count+count)%count); p_->rebuild();
    }
}
Action SystemMenu::back() {
    if(confirming())return answer_confirmation(false);
    cancel_pointer();
    if(p_->gallery_view) {p_->gallery_view=false; p_->rebuild(); return {};}
    if(p_->movie_menu) {p_->movie_menu=false; p_->rebuild(); return {};}
    if(p_->page==Page::Config) {
        if(p_->in_game) return {Type::Back};
        open(p_->config_return); return {};
    }
    if(p_->page==Page::Title) return activate("end");
    if(p_->page==Page::Extras) open(Page::Title); else open(Page::Extras);
    return {};
}
Action SystemMenu::activate(const std::string& tag) {
    if(confirming())return {};
    ConfirmKind kind=ConfirmKind::Count;
    if(tag=="prologue"||tag=="sce_start"||tag=="start"||(tag=="dc"&&p_->get(100,100)))kind=ConfirmKind::NewGame;
    else if(tag=="end"||tag=="gomenu05")kind=ConfirmKind::Quit;
    else if((tag=="gomenu04"||tag=="menutitle")&&p_->in_game)kind=ConfirmKind::Title;
    else if(tag=="qload"&&p_->latest)kind=ConfirmKind::Load;
    else if(p_->page==Page::Config&&(tag=="defaultparts"||tag=="init"))kind=ConfirmKind::ResetSettings;
    else if(p_->page==Page::Music&&(tag=="movie01"||tag=="movie02"||tag=="movie03"))kind=ConfirmKind::Movie;
    else if(p_->page==Page::Scenes&&tag.size()>3&&tag.rfind("sce",0)==0&&std::isdigit(static_cast<unsigned char>(tag[3]))){
        int id=-1;try{id=std::stoi(tag.substr(3));}catch(...){}
        if(id<0||p_->get(98,id)<=0)return {};
        kind=ConfirmKind::SceneReplay;
    }
    if(kind!=ConfirmKind::Count){
        p_->confirm.set_language(dc4::get_language());
        if(p_->confirm.open(kind)){cancel_pointer();p_->pending_tag=tag;return {};}
    }
    return activate_confirmed(tag);
}
bool SystemMenu::confirming()const{return p_->confirm.active();}
Action SystemMenu::answer_confirmation(bool yes){
    if(!confirming())return {};
    p_->confirm.answer(yes);auto tag=std::move(p_->pending_tag);p_->pending_tag.clear();
    return yes?activate_confirmed(tag):Action{};
}
Action SystemMenu::activate_confirmed(const std::string& tag) {
    if(tag.empty())return {};
    if(tag=="return" || tag=="rmain" || tag=="retchk")return back();
    if(tag=="prologue")return {Type::NewGame,-1,"DC4_SEN20181213B",{}};
    if(tag=="sce_start" || tag=="start")return {Type::NewGame,-1,"DC4_KYO20190204A1",{}};
    if(tag=="dc" && p_->get(100,100))return {Type::NewGame,-1,"DC4_DC99990101A",{}};
    if(tag=="load" || tag=="gomenu02")return {Type::Load};
    if(tag=="qload")return p_->latest ? Action{Type::LatestLoad} : Action{};
    if(tag=="gomenu01")return p_->in_game ? Action{Type::Save} : Action{};
    if(tag=="end" || tag=="gomenu05")return {Type::Quit};
    if(tag=="extra") {open(Page::Extras);return {};}
    if(tag=="config" || tag=="gomenu03") {open(Page::Config);return {};}
    if(tag=="gomenu04" || tag=="menutitle") {open(Page::Title);return {};}
    if(tag=="cgmode") {open(Page::Gallery);return {};}
    if(tag=="scemode") {open(Page::Scenes);return {};}
    if(tag=="musmode") {open(Page::Music);return {};}
    if(p_->page==Page::Music) {
        if(tag=="selmovie") {p_->music_underlay=p_->chips;p_->movie_menu=true;p_->rebuild();return {};}
        if(tag=="ret_main") {p_->movie_menu=false;p_->rebuild();return {};}
        if(tag=="movie01")return {Type::PlayMovie,0,"DC4_OP1.MPG",{}};
        if(tag=="movie02")return {Type::PlayMovie,1,"DC4_OP2.MPG",{}};
        if(tag=="movie03")return {Type::PlayMovie,2,"DC4_OP3.MPG",{}};
        if(tag.rfind("#music:",0)==0) {
            int id=-1;try{id=std::stoi(tag.substr(7));}catch(...){} return id>=0?p_->play_music(id):Action{};
        }
        if(tag=="play")return p_->play_music(std::max(0,p_->music_selected));
        if(tag=="next")return p_->play_music(p_->get(100,26)==2 ? int(p_->random()%42) : p_->music_selected+1);
        if(tag=="back")return p_->play_music(p_->music_selected-1,-1);
        if(tag=="stop") {p_->put(3,11,1);p_->rebuild();return {Type::StopMusic};}
        if(tag=="allon") {for(int i=0;i<42;++i)p_->playlist.insert(i);p_->put(100,21,1);p_->rebuild();return {};}
        if(tag=="alloff") {p_->playlist.clear();p_->put(100,21,0);p_->rebuild();return {};}
        if(tag.rfind("flag",0)==0) {
            int id=-1;try{id=p_->music_top+std::stoi(tag.substr(4))-1;}catch(...){}
            if(id>=0){if(p_->playlist.count(id))p_->playlist.erase(id);else p_->playlist.insert(id);p_->rebuild();}return {};
        }
    }
    if(p_->page==Page::Gallery && tag.rfind("cg",0)==0 && tag.size()>2 && std::isdigit(static_cast<unsigned char>(tag[2]))) {
        int first=-1;try{first=std::stoi(tag.substr(2));}catch(...){}
        auto* s=p_->script(p_->script_name); if(first<0 || !s)return {};
        int end=int(p_->cg_names.size());
        // A thumbnail starts at cgN; its sum block lists every variant until
        // the next thumbnail's cgN target in the same script.
        for(const auto& c:s->code) if(c.op==0x54 && c.str.rfind("cg",0)==0 && c.str.size()>2 && std::isdigit(static_cast<unsigned char>(c.str[2]))) {
            int next=std::stoi(c.str.substr(2));if(next>first)end=std::min(end,next);
        }
        // Restrict to the exact sum preceding this thumbnail, when present.
        for(size_t i=0;i<s->code.size();++i) if(s->code[i].op==0x54 && s->code[i].str==tag && i && s->code[i-1].op==0x72) {
            std::vector<int> indexes;
            size_t begin=i>60?i-60:0;
            for(size_t j=i;j>begin;--j) {const auto& c=s->code[j-1];if(c.op==0x84&&c.words[0]==3){break;}if(c.op==0x84&&c.words[0]==97)indexes.push_back(c.words[1]);}
            if(!indexes.empty())end=*std::max_element(indexes.begin(),indexes.end())+1;
            break;
        }
        p_->gallery_variants.clear();
        for(int i=first;i<end&&size_t(i)<p_->cg_names.size();++i)if(p_->get(97,i)>0)p_->gallery_variants.push_back(i);
        if(p_->gallery_variants.empty())return {};
        p_->gallery_variant=0;p_->background="*"+std::to_string(p_->gallery_variants[0]);
        p_->gallery_view=true;p_->image(p_->background);cancel_pointer();return {};
    }
    const auto before=p_->vars;
    std::string target=tag;
    if(p_->page==Page::Scenes && tag.rfind("sce",0)==0 && tag.size()>3 && std::isdigit(static_cast<unsigned char>(tag[3]))) {
        int id=std::stoi(tag.substr(3)); if(p_->get(98,id)<=0)return {};
        // Execute the authored sceN label: unlock IDs are global, but that
        // label translates them to the route-local selector used by sce_set.
        p_->replay_menu=p_->script_name;
        p_->replay_page=p_->get(3,6);p_->replay_route=p_->get(3,5);
    }
    if(p_->page==Page::Config && (tag=="defaultparts" || tag=="init")) {
        int pg=p_->get(100,54);p_->defaults();p_->put(100,54,pg);p_->rebuild();return {Type::SettingsChanged};
    }
    if(p_->page==Page::Config && tag=="win_set") {p_->put(99,1,!p_->get(99,1));p_->rebuild();return {Type::SettingsChanged};}
    p_->put(9,91,1);
    auto out=p_->run(p_->script_name,target,true);
    if(p_->page==Page::Music)p_->music_rows();
    if(out.type==Type::StartScene)return out;
    if(p_->page==Page::Config && p_->vars!=before) {
        if(out.type==Type::None)out.type=Type::SettingsChanged;
    }
    return out;
}

void SystemMenu::draw(int mx,int my,bool down,const dc4scene::FontRenderer& font,std::vector<uint8_t>* frame) const {
    if(!frame)return;
    frame->assign(FrameBytes,0);for(size_t i=3;i<frame->size();i+=4)(*frame)[i]=255;
    if(auto* back=p_->image(p_->background)) {
        Rect src{0,0,back->w,back->h},dst{0,0,W,H};
        if(p_->gallery_view && back->h>H) {dst={0,0,back->w,back->h};}
        blit(*back,src,dst,frame);
    }
    if(p_->gallery_view)return;
    int hover=confirming()?-1:p_->hit(mx,my);
    auto state_for=[&](int control) {
        if(control<0)return 99;
        if(control==hover) return down && (!p_->held || p_->pressed==control) ? 2 : 1;
        return 99;
    };
    auto render_chip=[&](Chip chip,bool underlay) {
        if(chip.control>=0) {
            if(underlay) { if(chip.state!=99)return;chip.control=-1; }
        }
        if(chip.control>=0) {
            if(size_t(chip.control)>=p_->widgets.size())return;
            int state=state_for(chip.control);
            const auto& widget=p_->widgets[size_t(chip.control)];
            if(chip.state==6 || chip.state==8 || chip.state==10) {
                if(widget.slider_bank<0)return;
                bool active=hover==chip.control;
                if((chip.state==6&&active)||(chip.state==8&&!active)||chip.state==10)return;
                int count=std::max(widget.slider_count-1,1);
                int val=std::clamp(p_->get(widget.slider_bank,widget.slider_index),0,count);
                chip.dst.x=widget.slider_left + val*(widget.slider_right-widget.slider_left)/count - chip.dst.w/2;
            } else if(chip.state!=state)return;
        }
        if(auto* img=p_->image(chip.image))blit(*img,chip.src,chip.dst,frame);
    };
    if(p_->movie_menu)for(const auto& chip:p_->music_underlay)render_chip(chip,true);
    for(const auto& chip:p_->chips)render_chip(chip,false);
    for(const auto& c:p_->captions) {
        if(c.control>=0 && state_for(c.control)!=c.state)continue;
        std::string line=c.text;
        for(size_t at=line.find("@s");at!=std::string::npos;at=line.find("@s"))line.erase(at,2);
        line = dc4::translate_ui(line);
        std::istringstream lines(line);int y=c.y+c.size;
        while(std::getline(lines,line)) {
            line = dc4::translate_ui(line);
            font.rasterize_line(dc4scene::sjis_to_utf16(line),c.x,y,frame,W,H,float(c.size));
            y+=c.size+4;
        }
    }
    if(p_->page==Page::Music && !p_->movie_menu) {
        // Scroll thumb supplied by MUSICCHIP (0x74 state -1).
        if(auto* img=p_->image("system/musicchip"))blit(*img,{902,30,24,23},{1072,266+p_->music_top*267/36,24,23},frame);
    }
    if(!p_->ready) font.rasterize_line(dc4::translate_ui(u"Menu data could not be loaded"),50,80,frame,W,H,28);
    p_->confirm.draw(mx,my,down,font,frame);
}

SystemConfig SystemMenu::config() const {
    SystemConfig c;auto v=[&](int i){return p_->get(99,i);};
    c.master_volume=std::clamp(v(225),0,100);c.music_volume=std::clamp(v(7),0,100);
    c.se_volume=std::clamp(v(8),0,100);c.voice_volume=std::clamp(v(9),0,100);c.movie_volume=std::clamp(v(207),0,100);
    c.master_enabled=v(203)!=0;c.music_enabled=v(14)!=0;c.se_enabled=v(15)!=0;c.voice_enabled=v(5)!=0;
    c.movie_enabled=v(200)!=0;c.duck_music_for_voice=v(190)!=0;
    c.text_speed=v(26);c.auto_speed=v(27);c.auto_wait=v(29);c.window_opacity=std::clamp(p_->get(100,49),0,100);
    c.skip_unread=v(30)!=0;c.stop_skip_at_choice=v(210)!=0;c.stop_auto_at_choice=v(209)!=0;
    c.wait_for_voice=v(28)!=0;c.stop_voice_on_advance=v(208)!=0;c.fullscreen=v(1)!=0;c.effects=v(10);c.effect_speed=v(12);
    for(int i=0;i<51;++i){c.character_volume[size_t(i)]=std::clamp(p_->get(94,i),0,100);c.character_muted[size_t(i)]=p_->get(95,i)!=0;}
    return c;
}
int SystemMenu::variable(int b,int i)const{return p_->get(b,i);}
void SystemMenu::set_variable(int b,int i,int value){p_->put(b,i,value);p_->rebuild();}
void SystemMenu::initialize_scene(dc4scene::SceneRunner& scene,bool replay) const {
    for(int bank:{97,98,100}) {
        const int count=bank==97?1200:bank==98?650:150;
        for(int i=0;i<count;++i)scene.set_variable(uint16_t(bank),uint16_t(i),p_->get(bank,i));
    }
    // scemode.mes sets this before selecting a chapter. Story scripts use it
    // to bypass normal-story setup and return through SCE_END after a replay.
    scene.set_variable(9,7,replay?1:0);
}
bool SystemMenu::save_profile(const std::string& path,std::string* error)const {
    const std::string temporary=path+".tmp";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    if(!out){if(error)*error="Cannot create emulator profile";return false;}
    out<<"DC4ENGINE_PROFILE 1\n";
    for(const auto& pair:p_->vars) {
        int b=pair.first.first,i=pair.first.second;
        if(b==93||b==94||b==95||b==99||b==100)out<<"v "<<b<<' '<<i<<' '<<pair.second<<'\n';
    }
    for(const auto& s:p_->seen_cg)out<<"c "<<std::quoted(s)<<'\n';
    for(const auto& s:p_->seen_music)out<<"m "<<std::quoted(s)<<'\n';
    for(const auto& s:p_->seen_scene)out<<"s "<<std::quoted(s)<<'\n';
    for(int id:p_->playlist)out<<"p "<<id<<'\n';
    out.flush();if(!out){if(error)*error="Cannot write emulator profile";return false;}out.close();
    bool replaced=false;
#ifdef _WIN32
    replaced=MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code ec;std::filesystem::rename(temporary,path,ec);replaced=!ec;
#endif
    if(!replaced){if(error)*error="Cannot replace emulator profile";return false;}if(error)error->clear();return true;
}
bool SystemMenu::load_profile(const std::string& path,std::string* error) {
    std::error_code size_error;
    const auto bytes=std::filesystem::file_size(path,size_error);
    if(!size_error && bytes>32*1024*1024){if(error)*error="Emulator profile exceeds byte limit";return false;}
    std::ifstream in(path,std::ios::binary);if(!in){if(error)*error="Cannot open emulator profile";return false;}
    std::string header;std::getline(in,header);if(header!="DC4ENGINE_PROFILE 1"){if(error)*error="Unsupported emulator profile";return false;}
    auto vars=p_->vars;std::set<std::string> cg,music,scene;std::set<int> playlist;
    std::string line;size_t lines=0;
    while(std::getline(in,line)) {
        if(++lines>120000||line.size()>4096){if(error)*error="Emulator profile exceeds limits";return false;}
        std::istringstream row(line);char type=0;row>>type;
        if(type=='v'){int b,i,value;if(!(row>>b>>i>>value)||b<93||b>100||i<0||i>4095||std::abs(int64_t(value))>1000000){if(error)*error="Invalid profile variable";return false;}vars[{b,i}]=value;}
        else if(type=='c'||type=='m'||type=='s'){std::string s;if(!(row>>std::quoted(s))||!safe_name(s)){if(error)*error="Invalid profile asset";return false;}(type=='c'?cg:type=='m'?music:scene).insert(key(s));}
        else if(type=='p'){int id;if(!(row>>id)||id<0||id>1000){if(error)*error="Invalid playlist entry";return false;}playlist.insert(id);}
        else if(type){if(error)*error="Unknown profile record";return false;}
    }
    p_->vars=std::move(vars);p_->seen_cg=std::move(cg);p_->seen_music=std::move(music);p_->seen_scene=std::move(scene);p_->playlist=std::move(playlist);
    p_->progress_vars();p_->rebuild();if(error)error->clear();return true;
}
void SystemMenu::mark_cg_seen(const std::string& asset){if(safe_name(key(asset)))p_->seen_cg.insert(key(asset));p_->progress_vars();}
void SystemMenu::mark_music_seen(const std::string& asset){if(safe_name(key(asset)))p_->seen_music.insert(key(asset));p_->progress_vars();}
void SystemMenu::mark_scene_seen(const std::string& script,const std::string& label){
    if(safe_name(key(script))){p_->seen_scene.insert(key(script));if(!label.empty())p_->seen_scene.insert(key(script)+"#"+label);}p_->progress_vars();
}
bool SystemMenu::cg_seen(const std::string& asset)const{return p_->seen_cg.count(key(asset))!=0;}
bool SystemMenu::music_seen(const std::string& asset)const{return p_->seen_music.count(key(asset))!=0;}
bool SystemMenu::scene_seen(const std::string& script,const std::string& label)const{return p_->seen_scene.count(key(script)+(label.empty()?"":"#"+label))!=0;}
void SystemMenu::observe(const Command& command,const std::string& script){
    if(command.op==0x49){std::string name=key(command.str);if(std::find_if(p_->cg_names.begin(),p_->cg_names.end(),[&](const std::string& s){return key(s)==name;})!=p_->cg_names.end())mark_cg_seen(name);}
    if(command.op==0x70 && command.words[0]<p_->bgm_names.size())mark_music_seen(p_->bgm_names[command.words[0]]);
    if(!script.empty()&&(command.op==0x69||command.op==0x6C||command.op==0x1C))mark_scene_seen(script);
}
bool SystemMenu::sync_progress(const dc4scene::SceneRunner& scene){
    bool changed=false;
    for(size_t i=0;i<p_->cg_names.size();++i)if(scene.variable(97,uint16_t(i))>0)changed=p_->seen_cg.insert(key(p_->cg_names[i])).second||changed;
    for(size_t i=0;i<p_->scene_names.size();++i)if(scene.variable(98,uint16_t(i))>0)changed=p_->seen_scene.insert(key(p_->scene_names[i])).second||changed;
    for(int i=100;i<150;++i){int value=scene.variable(100,uint16_t(i));if(value>p_->get(100,i)){p_->put(100,i,value);changed=true;}}
    if(changed)p_->progress_vars();return changed;
}
void SystemMenu::set_music_position(int percent){p_->put(99,189,std::clamp(percent,0,100));}
std::vector<SystemMenu::Control> SystemMenu::controls()const {
    std::vector<Control> result;for(const auto& w:p_->widgets)result.push_back({w.hit.x,w.hit.y,w.hit.w,w.hit.h,w.tag,w.enabled});return result;
}
std::string SystemMenu::error()const{return p_->last_error;}
} // namespace dc4ui
