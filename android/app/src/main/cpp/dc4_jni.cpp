// Android shell bridge. All VM, graphics, choices and audio dispatch stay in core.
#include "dc4/audio.h"
#include "dc4/log_screen.h"
#include "dc4/save_screen.h"
#include "dc4/scene.h"
#include "dc4/voice.h"
#include "dc4/resource.h"
#include "dc4/system_menu.h"
#include "dc4/movie.h"
#include "dc4/bgm.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "dc4", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "dc4", __VA_ARGS__)

namespace {
using Clock = std::chrono::steady_clock;
constexpr const char* kOpening = "dc4_kyo20190204a1.mes";
constexpr int kCharacterMs = 40;

struct Engine {
    dc4::AudioEngine audio;
    dc4::VoiceArchive voice;
    dc4scene::FontRenderer font;
    std::unique_ptr<dc4scene::SceneRunner> scene;
    std::string data_dir;
    JavaVM* vm = nullptr;
    jobject activity = nullptr;
    jmethodID decode_sjis = nullptr;
    Clock::time_point line_start = Clock::now();
    size_t text_length = 0;
    bool fully_revealed = false;
    bool pointer_down = false;
    bool pressed_widget = false;
    int pressed_choice = -1;
    std::string pressed_action;
    bool paused = false;
    bool pause_saved = false;
    std::string pause_file;
    // Increments on every line/choice entry; Java builds the backlog from it.
    size_t line_serial = 0;
    // Original-look Log screen state (dc4ui::LogScreen). Rows are recorded
    // natively as lines begin; dialogue_index counts text lines for jumps.
    dc4ui::LogScreen log;
    bool log_loaded = false;
    std::vector<dc4ui::LogRow> log_rows;
    int log_scroll = 0;
    bool log_thumbs = true;
    int log_mx = -1, log_my = -1;
    int log_drag_y = -1, log_drag_base = 0;
    bool log_drag_thumb = false, log_drag_moved = false;
    int dialogue_index = -1;
    dc4ui::SaveScreen save_screen;
    std::vector<dc4ui::SaveSlot> save_slots =
        std::vector<dc4ui::SaveSlot>(dc4ui::SaveScreen::kSlotCount);
    std::vector<uint8_t> save_frame;
    int save_mx = -1, save_my = -1;
    bool save_down = false;
    dc4ui::SystemMenu menu;
    dc4ui::ConfirmDialog confirm;
    std::function<std::string()> pending_confirm;
    std::vector<uint8_t> confirm_background;
    int confirm_x=-1,confirm_y=-1;
    bool confirm_down=false;
    dc4::MoviePlayer movie;
    bool menu_open = true, in_game = false, replay = false, movie_from_scene = false;
    bool menu_return = false, current_was_read = false;
    dc4ui::SystemMenuPage return_page = dc4ui::SystemMenuPage::Title;
    Clock::time_point tick = Clock::now();
    std::string save_dir, line_key;
    std::set<std::string> read_lines;
    int menu_x = -1, menu_y = -1, log_pressed = 0;
    bool menu_down = false;
    bool menu_music = false;
    bool movie_suspended = false;
};

// Java serializes native access: input/render on its render thread, lifecycle
// only after joining that thread. The audio callback uses the core mixer lock.
Engine* g_engine = nullptr;
struct LaunchConfig { std::string saves, overlay, encoding = "windows-31j", font, language; } g_config;

std::string request_confirm(Engine* e,dc4ui::ConfirmKind kind,std::function<std::string()> accepted) {
    if(e->confirm.active())return {};
    e->confirm.set_language(dc4::get_language());
    if(!e->confirm.open(kind))return accepted();
    e->pending_confirm=std::move(accepted);e->confirm_background.clear();
    e->confirm_x=e->confirm_y=-1;e->confirm_down=false;return {};
}
std::string answer_confirm(Engine* e,bool yes) {
    e->confirm.answer(yes);auto accepted=std::move(e->pending_confirm);e->pending_confirm={};
    e->confirm_background.clear();e->confirm_down=false;e->tick=Clock::now();
    return yes&&accepted?accepted():std::string{};
}

void save_profile(Engine* e) {
    if (e->save_dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(e->save_dir, ec);
    e->menu.save_profile(e->save_dir + "/system-profile.dat");
    std::ofstream out(e->save_dir + "/read-lines.txt", std::ios::binary);
    for (const auto& key : e->read_lines) out << key << '\n';
}

void sync_progress(Engine* e) {
    e->menu.sync_progress(*e->scene);
}

void apply_config(Engine* e) {
    const auto c = e->menu.config();
    e->audio.set_master_volume(c.master_enabled ? c.master_volume : 0);
    e->audio.set_user_volume(dc4::AudioGroup::Bgm, c.music_enabled ? c.music_volume : 0);
    e->audio.set_user_volume(dc4::AudioGroup::Se, c.se_enabled ? c.se_volume : 0);
    e->audio.set_user_volume(dc4::AudioGroup::Voice, c.voice_enabled ? c.voice_volume : 0);
    e->audio.set_movie_volume(c.movie_enabled ? c.movie_volume : 0);
    e->audio.set_duck_music_for_voice(c.duck_music_for_voice,e->menu.variable(99,206));
    if (e->scene) {
        e->scene->set_window_opacity(c.window_opacity);
        e->scene->set_effects_enabled(c.effects != 0);
        e->scene->set_effect_speed(c.effect_speed);
        for (size_t i = 0; i < c.character_volume.size(); ++i)
            e->scene->set_character_volume(i,c.character_volume[i],c.character_muted[i]);
    }
}

std::string jstr_to_std(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* p = env->GetStringUTFChars(s, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(s, p);
    return out;
}

std::u16string jstr_to_utf16(JNIEnv* env, jstring s, size_t limit) {
    if (!s) return {};
    const jchar* chars = env->GetStringChars(s, nullptr);
    if (!chars) return {};
    const size_t count = std::min(size_t(env->GetStringLength(s)), limit);
    std::u16string result(reinterpret_cast<const char16_t*>(chars), count);
    env->ReleaseStringChars(s, chars);
    return result;
}

std::u16string decode_sjis(const std::string& in) {
    Engine* e = g_engine;
    if (!e || !e->vm || !e->decode_sjis) return {};
    JNIEnv* env = nullptr;
    bool attached = false;
    if (e->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (e->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return {};
        attached = true;
    }
    std::u16string result;
    jbyteArray arr = env->NewByteArray(jsize(in.size()));
    if (arr) {
        env->SetByteArrayRegion(arr, 0, jsize(in.size()),
                               reinterpret_cast<const jbyte*>(in.data()));
        jstring out = nullptr;
        if (!env->ExceptionCheck())
            out = static_cast<jstring>(env->CallObjectMethod(e->activity, e->decode_sjis, arr));
        if (out && !env->ExceptionCheck()) {
            const jchar* chars = env->GetStringChars(out, nullptr);
            if (chars) {
                result.assign(reinterpret_cast<const char16_t*>(chars), size_t(env->GetStringLength(out)));
                env->ReleaseStringChars(out, chars);
            }
        }
        if (out) env->DeleteLocalRef(out);
        env->DeleteLocalRef(arr);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (attached) e->vm->DetachCurrentThread();
    return result;
}

void destroy_engine(JNIEnv* env) {
    Engine* e = g_engine;
    if (!e) return;
    save_profile(e);
    e->movie.close();
    e->audio.shutdown();
    dc4scene::g_sjis_decoder = nullptr;
    g_engine = nullptr;
    if (e->activity) env->DeleteGlobalRef(e->activity);
    if (!e->pause_file.empty()) std::remove(e->pause_file.c_str());
    delete e;
}

void begin_line(Engine* e) {
    e->line_start = Clock::now();
    ++e->line_serial;
    // Count rendered characters, not SJIS bytes or inline control/ruby syntax.
    e->text_length = e->scene->visible_text_length();
    e->fully_revealed = false;
    e->line_key = e->scene->script_name() + ":" + std::to_string(e->scene->script_position());
    e->current_was_read = e->read_lines.count(e->line_key) != 0;
}

int reveal_count(Engine* e) {
    if (e->fully_revealed || e->scene->waiting_for_choice()) return -1;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - e->line_start).count();
    static constexpr int timing[] = {120,100,80,65,50,40,30,24,18,12,6,0};
    const int speed = std::clamp(e->menu.config().text_speed,0,11);
    const size_t count = speed >= 11 ? e->text_length : size_t(elapsed / timing[speed]);
    if (count >= e->text_length) { e->fully_revealed = true; return -1; }
    return int(count);
}

// Appends one Log row for the current line; also the cap + scroll reset.
void record_log_row(Engine* e, int line) {
    e->log_rows.push_back(dc4ui::capture_log_row(*e->scene, line));
    if (e->log_rows.size() > 4000)
        e->log_rows.erase(e->log_rows.begin(), e->log_rows.begin() + (e->log_rows.size() - 4000));
    size_t total = 0, snapshots = 0;
    for (auto it = e->log_rows.rbegin(); it != e->log_rows.rend(); ++it) {
        total += it->snapshot.size();
        if (++snapshots > 512 || total > 16*1024*1024) { std::vector<uint8_t>().swap(it->snapshot); }
    }
    e->log_scroll = dc4ui::LogScreen::max_scroll(e->log_rows.size());
}

bool play_row_voice(Engine* e, const std::string& name) {
    if (name.empty()) return false;
    const dc4::VoiceEntry* entry = e->voice.find(name);
    std::vector<uint8_t> blob;
    return entry && e->voice.read_entry(*entry, &blob, nullptr) &&
           e->audio.play_voice_blob(blob.data(), blob.size());
}

// シーンジャンプ: rebuild the opening scene settled at the row's dialogue
// index (the same mechanism the desktop shell uses). Rows after the jump
// point are discarded; advancing re-accumulates them.
std::string jump_to_row(Engine* e, int row_index) {
    if (row_index < 0 || size_t(row_index) >= e->log_rows.size()) return "Invalid history row";
    const dc4ui::LogRow& row = e->log_rows[size_t(row_index)];
    if (row.snapshot.empty()) return "This older history row no longer has a rewind snapshot";
    std::string error;
    if (!e->scene->restore_state(row.snapshot, &error)) return "Scene jump failed: " + error;
    e->dialogue_index = row.line;
    e->log_rows.resize(size_t(row_index) + 1);
    e->log_scroll = dc4ui::LogScreen::max_scroll(e->log_rows.size());
    begin_line(e);
    e->fully_revealed = true;
    e->scene->set_message_visible(true);
    e->tick = Clock::now();
    return "";
}

bool advance_scene(Engine* e) {
    if (!e->scene->pending_movie().empty() || e->scene->waiting_for_time()) return true;
    if (!e->line_key.empty() && e->read_lines.size() < 100000) e->read_lines.insert(e->line_key);
    // Current core follows 0x4F/0x50 transfers itself, preserving scene/audio.
    // Keep the existing next_script fallback for an ended runner, checking
    // every load and preventing an unbounded no-dialogue transfer loop.
    for (int jumps = 0; jumps < 64; ++jumps) {
        if (e->scene->waiting_for_choice()) { begin_line(e); return true; }
        if (e->scene->scene_ended()) {
            return false; // Script transfers happen in core; system transfers return to the menu.
        }
        const bool line = e->scene->advance_to_next_text();
        sync_progress(e);
        if (!e->scene->pending_movie().empty() || e->scene->waiting_for_time()) return true;
        if (line || e->scene->waiting_for_choice()) begin_line(e);
        if (line) {
            ++e->dialogue_index;
            record_log_row(e, e->dialogue_index);
        }
        if (line || e->scene->waiting_for_choice()) return true;
        if (e->scene->scene_ended()) return false;
    }
    LOGE("scene transfer limit reached");
    return false;
}

bool over_widget(Engine* e, int x, int y) {
    if (!e->scene->message_visible() || e->scene->waiting_for_choice() ||
        e->scene->waiting_for_time() || e->scene->credits_active() || !e->scene->pending_movie().empty()) return false;
    for (const auto& b : e->scene->buttons())
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return true;
    return false;
}

std::string stopped_message(Engine* e) {
    return e->scene->last_error().empty() ? "End of scenario" : "Scene stopped: " + e->scene->last_error();
}

const char* kFontCandidates[] = {
    "/system/fonts/NotoSerifCJK-Regular.ttc",
    "/system/fonts/NotoSerifCJKjp-Regular.otf",
    "/system/fonts/NotoSansCJK-Regular.ttc",
    "/system/fonts/NotoSansCJKjp-Regular.otf",
    "/system/fonts/NotoSansSC-Regular.otf",
    "/system/fonts/NotoSansJP-Regular.otf",
    "/system/fonts/DroidSansJapanese.ttf",
};

// Copies a scene or menu frame into an ARGB_8888 Bitmap.
bool blit_rgba(JNIEnv* env, jobject bitmap, const std::vector<uint8_t>& frame) {
    if (frame.size() != size_t(dc4scene::kScreenW) * dc4scene::kScreenH * 4) return false;
    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.width != dc4scene::kScreenW || info.height != dc4scene::kScreenH ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 || info.stride < info.width * 4) return false;
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) return false;
    // ARGB_8888 uses RGBA memory on little-endian Android. Respect row stride.
    const size_t row_bytes = size_t(dc4scene::kScreenW) * 4;
    for (int row = 0; row < dc4scene::kScreenH; ++row)
        std::memcpy(static_cast<uint8_t*>(pixels) + size_t(row) * info.stride,
                    frame.data() + size_t(row) * row_bytes, row_bytes);
    AndroidBitmap_unlockPixels(env, bitmap);
    return true;
}

bool blit_frame(JNIEnv* env, jobject bitmap) {
    Engine* e = g_engine;
    return e && e->scene && blit_rgba(env, bitmap, e->scene->frame());
}

void refresh_menu_saves(Engine* e) {
    bool latest = false;
    for (int i = 1; i <= 100 && !latest; ++i)
        latest = std::filesystem::is_regular_file(e->save_dir + "/slot" + std::to_string(i) + ".dc4sav");
    e->menu.set_save_available(latest,
        std::filesystem::is_regular_file(e->save_dir + "/quicksave.dc4sav"),
        std::filesystem::is_regular_file(e->save_dir + "/autosave.dc4sav"));
}

std::string start_scene(Engine* e, std::string name, const std::string& label = {}, bool replay = false) {
    auto next = std::make_unique<dc4scene::SceneRunner>(e->data_dir);
    next->set_font(&e->font);
    next->set_animation_enabled(true);
    if (std::filesystem::path(name).extension().empty()) name += ".mes";
    if (!next->load_sheets(e->data_dir + "/MES/start.mes") ||
        !next->load_ui_script(e->data_dir + "/MES/win_size.mes") ||
        !next->show_message_window("SYSTEM/MESWIN.CRX") ||
        !next->load_script(e->data_dir + "/MES/" + name, label)) return next->last_error();
    e->menu.initialize_scene(*next,replay);
    e->movie.close(); e->movie_suspended = false; e->audio.stop_all();
    next->set_audio(&e->audio, &e->voice);
    next->set_observer([e](const dc4::MesCommand& c, const std::string& script) { e->menu.observe(c,script); });
    e->scene = std::move(next);
    apply_config(e);
    e->line_key.clear(); e->log_rows.clear(); e->dialogue_index = -1;
    e->menu_open = false; e->menu_return = false; e->in_game = true; e->replay = replay;
    e->menu.set_in_game(true); e->tick = Clock::now();
    if (!advance_scene(e) && !e->scene->last_error().empty()) return e->scene->last_error();
    return {};
}

std::string restore_save(Engine* e, const std::string& path) {
    std::vector<uint8_t> current;
    if (!e->scene->load_state(path)) return "Load failed: " + e->scene->last_error();
    e->movie.close();
    e->menu_open = e->menu_return = false; e->in_game = true; e->replay = false;
    e->menu.set_in_game(true); sync_progress(e); apply_config(e);
    e->line_key.clear(); begin_line(e); e->fully_revealed = true;
    e->scene->set_message_visible(true);
    e->log_rows.clear(); e->dialogue_index = 0; record_log_row(e,0);
    e->tick = Clock::now();
    return {};
}

void open_menu(Engine* e, dc4ui::SystemMenuPage page) {
    e->menu_open = true; e->menu_down = false;
    e->menu_x = e->menu_y = -1;
    e->menu.set_in_game(e->in_game);
    refresh_menu_saves(e); e->menu.open(page); e->tick = Clock::now();
}

bool play_menu_music(Engine* e, std::string asset, bool loop) {
    if (std::filesystem::path(asset).extension().empty()) asset += ".pcm";
    int first = 0, last = 0;
    dc4::bgm_loop_for(asset,&first,&last);
    e->audio.set_group_volume(dc4::AudioGroup::Bgm,100);
    return e->audio.play_bgm_file(dc4::resolve_asset(e->data_dir + "/" + asset),
        size_t(std::max(0,first)),size_t(std::max(0,last)),loop);
}

void menu_page_changed(Engine* e, dc4ui::SystemMenuPage before) {
    using P = dc4ui::SystemMenuPage;
    if (e->menu.page() == before) return;
    if (e->menu.page() == P::Music) { e->audio.stop_bgm(); e->menu_music = false; }
    else if (before == P::Music && e->menu.page() != P::Config) {
        e->menu_music = false; play_menu_music(e,"music/001.pcm",true);
    }
}

std::string menu_action(Engine* e, const dc4ui::SystemMenuAction& action) {
    using A = dc4ui::SystemMenuActionType;
    using P = dc4ui::SystemMenuPage;
    switch (action.type) {
    case A::None: return {};
    case A::Back:
        if (e->in_game) { e->menu_open = false; e->tick = Clock::now(); return "close"; }
        return "quit";
    case A::Quit: return "quit";
    case A::NewGame: case A::StartScene: {
        auto error = start_scene(e, action.asset.empty() ? kOpening : action.asset, action.label, action.type == A::StartScene);
        return error.empty() ? "start" : error;
    }
    case A::Save: case A::Load:
        if (action.type == A::Save && !e->in_game) return {};
        e->menu_return = e->menu_open; e->return_page = e->menu.page(); e->menu_open = false;
        return action.type == A::Save ? "save" : "load";
    case A::QuickLoad: case A::AutoLoad: case A::LatestLoad: {
        std::string path = e->save_dir + (action.type == A::AutoLoad ? "/autosave.dc4sav" : "/quicksave.dc4sav");
        if (action.type == A::LatestLoad) {
            path.clear(); std::filesystem::file_time_type newest{};
            for (int i = 1; i <= 100; ++i) {
                const std::string candidate = e->save_dir + "/slot" + std::to_string(i) + ".dc4sav";
                std::error_code ec;
                if (!std::filesystem::is_regular_file(candidate,ec)) continue;
                const auto time = std::filesystem::last_write_time(candidate,ec);
                if (!ec && (path.empty() || time > newest)) {path = candidate; newest = time;}
            }
            if (path.empty()) return "No saved game";
        }
        auto error = restore_save(e,path); return error.empty() ? "start" : error;
    }
    case A::SettingsChanged: apply_config(e); save_profile(e); return {};
    case A::PlayVoice: return play_row_voice(e,action.asset) ? "" : "Voice unavailable";
    case A::PlayMusic: {
        e->menu_music = play_menu_music(e,action.asset,e->menu.variable(100,26) == 0);
        return e->menu_music ? "" : "Music unavailable: " + action.asset;
    }
    case A::StopMusic: e->audio.stop_bgm(); e->menu_music = false; return {};
    case A::SeekMusic: e->audio.seek_channel_fraction(0,double(action.index)/100); return {};
    case A::PlayMovie: {
        std::string error; e->movie_from_scene = false;
        if (!e->movie.open(e->data_dir,action.asset,&e->audio,&error)) return error;
        e->tick = Clock::now(); return {};
    }
    }
    return {};
}

void tick_engine(Engine* e) {
    const auto now = Clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now-e->tick).count();
    e->tick = now;
    if (e->paused) return;
    const uint32_t elapsed = uint32_t(std::clamp<int64_t>(delta,0,250));
    if (!e->menu_open && !e->movie.active() && !e->scene->pending_movie().empty()) {
        std::string error; e->movie_from_scene = true;
        if (!e->movie.open(e->data_dir,e->scene->pending_movie(),&e->audio,&error)) {
            LOGE("movie: %s",error.c_str()); e->scene->finish_movie(); advance_scene(e);
        }
    }
    if (e->movie.active() && !e->movie_suspended) {
        e->movie.update(elapsed);
        if (e->movie.finished()) {
            e->movie.close();
            if (e->movie_from_scene) { e->scene->finish_movie(); advance_scene(e); }
        }
        return;
    }
    if (e->menu_open && e->menu_music && !e->audio.channel_playing(0)) {
        e->menu_music = false;
        menu_action(e,e->menu.activate("next"));
    }
    if (e->menu_open && e->menu.page() == dc4ui::SystemMenuPage::Music)
        e->menu.set_music_position(int(100*e->audio.channel_fraction(0)));
    if (e->menu_open || !e->in_game) return;
    e->scene->update(elapsed);
    if (e->scene->script_ready()) advance_scene(e);
    if (e->scene->scene_ended() && e->scene->last_error().empty()) {
        auto target = e->scene->next_script();
        std::transform(target.begin(),target.end(),target.begin(),[](unsigned char c){return char(std::tolower(c));});
        if (target.size() > 4 && target.substr(target.size()-4) == ".mes") target.resize(target.size()-4);
        auto page = e->replay ? dc4ui::SystemMenuPage::Scenes : dc4ui::SystemMenuPage::Title;
        if (target == "music") page = dc4ui::SystemMenuPage::Music;
        if (target == "cgmode") page = dc4ui::SystemMenuPage::Gallery;
        if (target == "scemode" || target == "sce_end") page = dc4ui::SystemMenuPage::Scenes;
        if (target == "config") page = dc4ui::SystemMenuPage::Config;
        e->in_game = false; e->audio.stop_all(); e->menu_music = false;
        save_profile(e); open_menu(e,page);
        if (page == dc4ui::SystemMenuPage::Scenes) e->menu.resume_scenes();
        if (page != dc4ui::SystemMenuPage::Music) play_menu_music(e,"music/001.pcm",true);
    }
}
}  // namespace

extern "C" {
JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4Configure(JNIEnv* env, jobject, jstring saves, jstring overlay, jstring encoding, jstring font, jstring language) {
    if (g_engine) return;
    g_config = {jstr_to_std(env,saves),jstr_to_std(env,overlay),jstr_to_std(env,encoding),jstr_to_std(env,font),language ? jstr_to_std(env,language) : ""};
    if (g_config.language == "id") dc4::set_language(dc4::Language::Indonesian);
    else if (g_config.language == "en") dc4::set_language(dc4::Language::English);
    else if (g_config.language == "jp") dc4::set_language(dc4::Language::Japanese);
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4ValidateSaveFile(JNIEnv* env,jobject,jstring base,jstring overlay,jstring encoding,jstring path) {
    if (g_engine) return env->NewStringUTF("Close the running game before importing saves");
    const auto data = jstr_to_std(env,base);
    dc4::configure_resources(data,jstr_to_std(env,overlay),jstr_to_std(env,encoding));
    dc4scene::SceneRunner candidate(data);
    if (!candidate.load_state(jstr_to_std(env,path)))
        return env->NewStringUTF(candidate.last_error().c_str());
    return env->NewStringUTF("");
}

JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4Init(JNIEnv* env, jobject thiz, jstring data_dir) {
    if (g_engine) return JNI_TRUE;
    Engine* e = new Engine();
    g_engine = e;  // Decoder is needed during initialization/first dialogue.
    e->data_dir = jstr_to_std(env, data_dir);
    e->save_dir = g_config.saves;
    dc4::configure_resources(e->data_dir,g_config.overlay,g_config.encoding);
    if (g_config.language == "id") dc4::set_language(dc4::Language::Indonesian);
    else if (g_config.language == "en") dc4::set_language(dc4::Language::English);
    else if (g_config.language == "jp") dc4::set_language(dc4::Language::Japanese);
    env->GetJavaVM(&e->vm);
    e->activity = env->NewGlobalRef(thiz);
    jclass cls = env->GetObjectClass(thiz);
    e->decode_sjis = env->GetMethodID(cls, "decodeSjis", "([B)Ljava/lang/String;");
    env->DeleteLocalRef(cls);
    if (!e->activity || !e->decode_sjis) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("cannot initialize Java text decoder");
        destroy_engine(env);
        return JNI_FALSE;
    }
    dc4scene::g_sjis_decoder = &decode_sjis;

    std::string err;
    if (e->voice.open(e->data_dir + "/VOICE.DAT", &err))
        LOGI("VOICE.DAT: %zu entries", e->voice.entries().size());
    else LOGE("VOICE.DAT: %s", err.c_str());
    if (!e->audio.init()) LOGE("audio init failed; continuing without device output");

    bool font_ok = !g_config.font.empty() && e->font.load(g_config.font,0,22.0f);
    for (const char* path : kFontCandidates) {
        if (font_ok) break;
        if (e->font.load(path, 0, 22.0f)) {
            LOGI("font: %s", path);
            font_ok = true;
            break;
        }
    }
    if (!font_ok) LOGE("no CJK font found; text will be blank");
    e->log_loaded = e->log.load(e->data_dir);
    e->log.set_latin_mode(dc4::is_latin());
    if (!e->log_loaded) LOGE("log screen artwork unavailable");
    if (!e->menu.load(e->data_dir,&err)) {
        LOGE("system menus: %s",err.c_str()); destroy_engine(env); return JNI_FALSE;
    }
    e->menu.load_profile(e->save_dir + "/system-profile.dat");
    e->confirm.load(e->data_dir);
    std::ifstream read_file(e->save_dir + "/read-lines.txt",std::ios::binary);
    std::string read_key;
    while (e->read_lines.size() < 100000 && std::getline(read_file,read_key))
        if (read_key.size() <= 300) e->read_lines.insert(read_key);

    e->scene.reset(new dc4scene::SceneRunner(e->data_dir));
    e->scene->set_latin_mode(dc4::is_latin());
    e->scene->set_font(&e->font);
    e->scene->set_audio(&e->audio, &e->voice);
    e->scene->set_animation_enabled(true);
    e->scene->set_observer([e](const dc4::MesCommand& c,const std::string& script){e->menu.observe(c,script);});
    if (!e->scene->load_sheets(e->data_dir + "/MES/start.mes") ||
        !e->scene->load_ui_script(e->data_dir + "/MES/win_size.mes") ||
        !e->scene->show_message_window("SYSTEM/MESWIN.CRX") ||
        !e->scene->load_script(e->data_dir + "/MES/" + kOpening)) {
        LOGE("scene initialization failed (%s): %s", e->data_dir.c_str(), e->scene->last_error().c_str());
        destroy_engine(env);
        return JNI_FALSE;
    }
    apply_config(e);
    open_menu(e,dc4ui::SystemMenuPage::Title);
    play_menu_music(e,"music/001.pcm",true);
    LOGI("title ready");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4Quit(JNIEnv* env, jobject) {
    destroy_engine(env);
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4OpenMenu(JNIEnv* env,jobject,jstring name) {
    Engine* e = g_engine;
    if (!e) return env->NewStringUTF("Engine is not running");
    const auto page = jstr_to_std(env,name);
    if (page == "resume") {
        if (e->menu_return) { open_menu(e,e->return_page); e->menu_return = false; }
        e->tick = Clock::now(); return env->NewStringUTF("");
    }
    if (page == "back") return env->NewStringUTF(menu_action(e,e->menu.back()).c_str());
    using P = dc4ui::SystemMenuPage;
    if (page == "title") {
        e->movie.close(); e->movie_suspended = false; e->scene->finish_movie(); e->audio.stop_all();
        e->in_game = false; e->replay = false; save_profile(e);
        open_menu(e,P::Title); e->menu_music = false; play_menu_music(e,"music/001.pcm",true);
    } else {
        if (e->movie.active()) { e->movie_suspended = true; e->movie.set_paused(true); }
        open_menu(e,page == "gallery" ? P::Gallery : page == "music" ? P::Music :
            page == "scenes" ? P::Scenes : page == "extras" ? P::Extras : P::Config);
    }
    return env->NewStringUTF("");
}

JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4MenuActive(JNIEnv*,jobject) {
    return g_engine && (g_engine->menu_open || g_engine->movie.active());
}

JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4MenuFrame(JNIEnv* env,jobject,jobject bitmap) {
    Engine* e = g_engine;
    if (!e || e->paused) return JNI_FALSE;
    tick_engine(e);
    std::vector<uint8_t> frame;
    if (e->movie.active() && !e->movie_suspended) e->movie.draw_rgba(&frame,1280,720);
    else if (e->menu_open) e->menu.draw(e->menu_x,e->menu_y,e->menu_down,e->font,&frame);
    else { e->scene->draw(reveal_count(e)); frame = e->scene->frame(); }
    return blit_rgba(env,bitmap,frame);
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4MenuTouch(JNIEnv* env,jobject,jint action,jint x,jint y) {
    Engine* e = g_engine;
    if (!e || e->paused) return env->NewStringUTF("");
    e->menu_x = x; e->menu_y = y;
    if (e->movie.active() && !e->movie_suspended) {
        if (action == 0) e->menu_down = true;
        else if (action == 3) e->menu_down = false;
        else if (action == 1 && e->menu_down) {
            e->menu_down = false; e->movie.close();
            if (e->movie_from_scene) { e->scene->finish_movie(); advance_scene(e); }
        }
        return env->NewStringUTF("");
    }
    std::string result;
    const auto before = e->menu.page();
    if (action == 0) {e->menu_down = true; e->menu.pointer_down(x,y);}
    else if (action == 2 && e->menu_down) result = menu_action(e,e->menu.pointer_move(x,y));
    else if (action == 1) {e->menu_down = false; result = menu_action(e,e->menu.pointer_up(x,y));}
    else if (action == 3) {e->menu_down = false; e->menu.cancel_pointer();}
    if (e->menu_open) menu_page_changed(e,before);
    if (e->movie_suspended && (!e->menu_open || e->menu.page() != dc4ui::SystemMenuPage::Config)) {
        e->movie_suspended = false; e->movie.set_paused(false); e->tick = Clock::now();
    }
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4RequestConfirm(JNIEnv* env,jobject,jstring requested) {
    Engine* e=g_engine;if(!e||e->paused)return env->NewStringUTF("");
    const auto action=jstr_to_std(env,requested);
    dc4ui::ConfirmKind kind;
    if(action=="quick-save"){
        if(!e->in_game)return env->NewStringUTF("Start a game before saving");
        kind=dc4ui::ConfirmKind::QuickSave;
    }else if(action=="quick-load"){
        if(!std::filesystem::is_regular_file(e->save_dir+"/quicksave.dc4sav"))return env->NewStringUTF("No quick save");
        kind=dc4ui::ConfirmKind::QuickLoad;
    }else if(action=="title")kind=dc4ui::ConfirmKind::Title;
    else if(action=="quit")kind=dc4ui::ConfirmKind::Quit;
    else return env->NewStringUTF("");
    return env->NewStringUTF(request_confirm(e,kind,[action]{return action+"-confirmed";}).c_str());
}

JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4ConfirmActive(JNIEnv*,jobject) {
    return g_engine&&g_engine->confirm.active();
}

JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4CancelConfirmation(JNIEnv*,jobject) {
    Engine* e=g_engine;if(!e)return JNI_FALSE;
    if(e->confirm.active()){answer_confirm(e,false);return JNI_TRUE;}
    if(e->menu.confirming()){e->menu.answer_confirmation(false);return JNI_TRUE;}
    if(e->save_screen.confirming()){e->save_screen.cancel_confirm();return JNI_TRUE;}
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4ConfirmFrame(JNIEnv* env,jobject,jobject bitmap) {
    Engine* e=g_engine;if(!e||!e->confirm.active())return;
    if(e->confirm_background.empty()){
        AndroidBitmapInfo info{};void* pixels=nullptr;
        if(AndroidBitmap_getInfo(env,bitmap,&info)!=ANDROID_BITMAP_RESULT_SUCCESS||info.width!=1280||info.height!=720||
           info.format!=ANDROID_BITMAP_FORMAT_RGBA_8888||info.stride<1280*4||
           AndroidBitmap_lockPixels(env,bitmap,&pixels)!=ANDROID_BITMAP_RESULT_SUCCESS)return;
        e->confirm_background.resize(1280*720*4);
        for(int y=0;y<720;++y)std::memcpy(e->confirm_background.data()+size_t(y)*1280*4,
            static_cast<const uint8_t*>(pixels)+size_t(y)*info.stride,1280*4);
        AndroidBitmap_unlockPixels(env,bitmap);
    }
    auto frame=e->confirm_background;
    e->confirm.draw(e->confirm_x,e->confirm_y,e->confirm_down,e->font,&frame);blit_rgba(env,bitmap,frame);
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4ConfirmTouch(JNIEnv* env,jobject,jint action,jint x,jint y) {
    Engine* e=g_engine;if(!e||e->paused||!e->confirm.active())return env->NewStringUTF("");
    e->confirm_x=x;e->confirm_y=y;
    if(action==0){e->confirm_down=true;e->confirm.pointer_down(x,y);}
    else if(action==3){e->confirm_down=false;e->confirm.cancel_pointer();}
    else if(action==1){
        e->confirm_down=false;auto result=e->confirm.pointer_up(x,y);
        if(result!=dc4ui::ConfirmResult::None)return env->NewStringUTF(answer_confirm(e,result==dc4ui::ConfirmResult::Yes).c_str());
    }
    return env->NewStringUTF("");
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4PlaybackSettings(JNIEnv* env,jobject) {
    Engine* e = g_engine;
    if (!e) return env->NewStringUTF("");
    const auto c = e->menu.config();
    const int delay = (21-std::clamp(c.auto_wait,0,20))*100 + int(e->text_length)*(12-std::clamp(c.auto_speed,0,11))*8;
    const std::string result = std::to_string(delay) + ";" +
        (c.wait_for_voice ? "1" : "0") + ";" + (c.stop_auto_at_choice ? "1" : "0") + ";" +
        (c.stop_skip_at_choice ? "1" : "0") + ";" +
        ((c.skip_unread || e->current_was_read) ? "1" : "0");
    return env->NewStringUTF(result.c_str());
}

// Tap dialogue once to finish revealing it, then again to advance.
JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4Advance(JNIEnv*, jobject) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused || e->menu_open) return JNI_FALSE;
    if (e->movie.active()) {
        e->movie.close(); e->scene->finish_movie(); return advance_scene(e);
    }
    if (e->scene->waiting_for_time()) {
        e->scene->finish_script_wait(); return advance_scene(e);
    }
    if (!e->scene->message_visible()) {
        e->scene->set_message_visible(true);
        return JNI_TRUE;
    }
    if (e->scene->waiting_for_choice()) return JNI_TRUE;
    if (reveal_count(e) >= 0) { e->fully_revealed = true; return JNI_TRUE; }
    if (e->menu.config().stop_voice_on_advance) e->audio.stop_group(dc4::AudioGroup::Voice);
    return advance_scene(e) ? JNI_TRUE : JNI_FALSE;
}

// Android MotionEvent action values: DOWN=0, UP=1, MOVE=2, CANCEL=3.
// Coordinates have already been inverse-mapped from the letterboxed viewport.
JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4Touch(JNIEnv* env, jobject thiz, jint action, jint x, jint y) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return env->NewStringUTF("");
    e->scene->set_pointer(x, y, action == 0 || (action == 2 && e->pointer_down));
    if (action == 0) {
        e->pointer_down = true;
        e->pressed_choice = e->scene->choice_at(x, y);
        e->pressed_widget = over_widget(e, x, y);
        const auto* tag = e->scene->hit_test(x, y);
        e->pressed_action = tag ? *tag : "";
        return env->NewStringUTF("");
    }
    if (action == 2) return env->NewStringUTF("");
    const bool was_down = e->pointer_down;
    e->pointer_down = false;
    if (action != 1 || !was_down) {
        e->pressed_action.clear();
        e->pressed_widget = false;
        e->pressed_choice = -1;
        return env->NewStringUTF("");
    }
    std::string message;
    if (e->scene->waiting_for_choice()) {
        const int choice = e->scene->choice_at(x, y);
        if (choice >= 0 && choice == e->pressed_choice && e->scene->choose(size_t(choice))) {
            if (!advance_scene(e)) message = stopped_message(e);
        }
    } else if (e->pressed_widget || over_widget(e, x, y)) {
        const auto* tag = e->scene->hit_test(x, y);
        if (tag && *tag == e->pressed_action) {
            if (*tag == ":voice") {
                message = e->scene->replay_voice() ? ":voice" : "No voice on this line";
            } else if (*tag == ":n" || *tag == ":off" || *tag == ":close" || *tag == ":clear") {
                e->scene->set_message_visible(!e->scene->message_visible());
            } else if (*tag == ":auto" || *tag == ":rapid" || *tag == ":skip" ||
                       *tag == ":backlog" || *tag == ":qs" || *tag == ":ql" ||
                       *tag == ":ds" || *tag == ":dl" || *tag == ":option" || *tag==":aatitle") {
                // Shell-level actions (auto/skip/backlog/save UI) dispatch in Java.
                e->pressed_action.clear();
                e->pressed_widget = false;
                e->pressed_choice = -1;
                return env->NewStringUTF(tag->c_str());
            } else message = "Not available on Android: " + *tag;
        }
    } else if (x >= 0 && x < dc4scene::kScreenW && y >= 0 && y < dc4scene::kScreenH) {
        if (!Java_org_dc4engine_Dc4Activity_dc4Advance(env, thiz)) message = stopped_message(e);
    }
    e->pressed_action.clear();
    e->pressed_widget = false;
    e->pressed_choice = -1;
    return env->NewStringUTF(message.c_str());
}

// Pause the audio device while retaining mixer and decoder state in memory.
JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4SetPaused(JNIEnv* env, jobject, jboolean paused, jstring checkpoint) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused == bool(paused)) return env->NewStringUTF("");
    std::string message;
    e->pointer_down = false;
    e->pressed_action.clear();
    e->pressed_widget = false;
    e->pressed_choice = -1;
    e->scene->set_pointer(-1, -1, false);
    e->menu.cancel_pointer(); e->menu_down = false;
    e->save_screen.cancel_pointer(); e->save_down = false;
    if(paused){answer_confirm(e,false);e->menu.answer_confirmation(false);e->save_screen.cancel_confirm();}
    e->fully_revealed = true;
    e->movie.set_paused(paused || e->movie_suspended);
    if (!e->audio.set_device_paused(paused)) message = "Audio device pause/resume failed";
    e->paused = paused; e->tick = Clock::now();
    if (paused) save_profile(e);
    if (!message.empty()) LOGE("%s", message.c_str());
    return env->NewStringUTF(message.c_str());
}

JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4Render(JNIEnv* env, jobject, jobject bitmap) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return;
    tick_engine(e);
    if (e->movie.active() || e->menu_open) {
        std::vector<uint8_t> frame;
        if (e->movie.active()) e->movie.draw_rgba(&frame,1280,720);
        else e->menu.draw(e->menu_x,e->menu_y,e->menu_down,e->font,&frame);
        blit_rgba(env,bitmap,frame); return;
    }
    const int reveal = reveal_count(e);
    e->scene->set_wait_marker(reveal < 0 && !e->scene->waiting_for_choice() && !e->scene->scene_ended());
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    e->scene->set_wait_frame(int((now / 120) % 4));
    e->scene->draw(reveal);
    blit_frame(env, bitmap);
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4CurrentLine(JNIEnv* env, jobject) {
    Engine* e = g_engine;
    if (!e || !e->scene) return env->NewStringUTF("");
    // Never feed Shift-JIS bytes to NewStringUTF (which expects modified UTF-8).
    const auto text = e->scene->visible_text();
    return env->NewString(reinterpret_cast<const jchar*>(text.data()), jsize(text.size()));
}

// "line;waitingChoice;voicePlaying;animating;revealed;messageVisible;ended"
JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4Status(JNIEnv* env, jobject) {
    Engine* e = g_engine;
    if (!e || !e->scene) return env->NewStringUTF("0;0;0;0;1;0;1");
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%zu;%d;%d;%d;%d;%d;%d",
                  e->line_serial,
                  e->scene->waiting_for_choice() ? 1 : 0,
                  e->scene->voice_playing() ? 1 : 0,
                  (e->scene->animating() || e->scene->waiting_for_time() || e->movie.active()) ? 1 : 0,
                  (e->fully_revealed || reveal_count(e) < 0) ? 1 : 0,
                  e->scene->message_visible() ? 1 : 0,
                  e->scene->scene_ended() ? 1 : 0);
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4Speaker(JNIEnv* env, jobject) {
    Engine* e = g_engine;
    if (!e || !e->scene) return env->NewStringUTF("");
    const auto name = dc4scene::sjis_to_utf16(e->scene->speaker());
    return env->NewString(reinterpret_cast<const jchar*>(name.data()), jsize(name.size()));
}

// One skip step: finish the typewriter reveal and advance, as the desktop
// shell's skip loop does (reveal-complete + advance at 100 ms cadence).
JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4SkipAdvance(JNIEnv*, jobject) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return JNI_FALSE;
    if (e->menu_open || e->movie.active() || e->scene->waiting_for_time()) return JNI_TRUE;
    if (!e->menu.config().skip_unread && !e->current_was_read) return JNI_FALSE;
    if (e->scene->scene_ended()) return JNI_FALSE;
    if (!e->scene->message_visible()) {
        e->scene->set_message_visible(true);
        return JNI_TRUE;
    }
    if (e->scene->waiting_for_choice()) return JNI_TRUE;
    e->fully_revealed = true;
    e->scene->finish_animations();
    return advance_scene(e) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4Save(JNIEnv* env, jobject, jstring path) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused || !e->in_game) return env->NewStringUTF("Start a game before saving");
    if (!e->scene->save_state(jstr_to_std(env, path)))
        return env->NewStringUTF(("Save failed: " + e->scene->last_error()).c_str());
    sync_progress(e); save_profile(e);
    return env->NewStringUTF("");
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4Load(JNIEnv* env, jobject, jstring path) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return env->NewStringUTF("Engine is not running");
    const auto error = restore_save(e,jstr_to_std(env,path));
    return env->NewStringUTF(error.c_str());
}

// Renders one clean frame (message window hidden) for the save thumbnail.
JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4RenderPreview(JNIEnv* env, jobject, jobject bitmap) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return;
    const bool visible = e->scene->message_visible();
    e->scene->set_message_visible(false);
    e->scene->set_wait_marker(false);
    e->scene->draw(-1);
    e->scene->set_message_visible(visible);
    blit_frame(env, bitmap);
}

// ------------------------------------------------------ Save/Load screen

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4SlotsOpen(JNIEnv* env, jobject, jboolean loading,
                                          jboolean latest, jboolean quick) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return env->NewStringUTF("Engine is not running");
    std::string error;
    if (!e->save_screen.loaded() && !e->save_screen.load(e->data_dir, &error))
        LOGE("save screen artwork: %s", error.c_str());
    e->save_screen.set_language(dc4::get_language());
    e->save_screen.set_mode(loading ? dc4ui::SaveMode::Load : dc4ui::SaveMode::Save);
    e->save_screen.set_shortcuts(latest, false, quick);
    e->save_screen.set_navigation(true,true,true);
    if (e->menu_open) { e->menu_return = true; e->return_page = e->menu.page(); }
    e->menu_open = false;
    e->save_screen.cancel_pointer();
    e->save_mx = e->save_my = -1;
    e->save_down = false;
    // A missing sheet uses the core's functional fallback layout.
    return env->NewStringUTF(error.empty() ? "" : "Save/Load artwork unavailable; using fallback layout");
}

// Java owns emulator save paths and PNG decoding; only bounded metadata and
// thumbnail pixels cross into the shared renderer, never native game saves.
JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4SlotInfo(JNIEnv* env, jobject, jint index,
                                         jboolean occupied, jstring timestamp,
                                         jstring excerpt, jobject thumbnail) {
    Engine* e = g_engine;
    if (!e || index < 0 || index >= dc4ui::SaveScreen::kSlotCount) return;
    dc4ui::SaveSlot slot;
    slot.occupied = occupied;
    slot.timestamp = jstr_to_utf16(env, timestamp, 64);
    slot.excerpt = jstr_to_utf16(env, excerpt, 256);
    if (occupied && thumbnail) {
        AndroidBitmapInfo info{};
        if (AndroidBitmap_getInfo(env, thumbnail, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
            info.width == 128 && info.height == 72 &&
            info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 && info.stride >= info.width * 4) {
            slot.thumbnail.rgba.resize(size_t(info.width) * info.height * 4);
            void* pixels = nullptr;
            if (AndroidBitmap_lockPixels(env, thumbnail, &pixels) == ANDROID_BITMAP_RESULT_SUCCESS) {
                slot.thumbnail.w = int(info.width);
                slot.thumbnail.h = int(info.height);
                for (uint32_t row = 0; row < info.height; ++row)
                    std::memcpy(slot.thumbnail.rgba.data() + size_t(row) * info.width * 4,
                                static_cast<uint8_t*>(pixels) + size_t(row) * info.stride,
                                size_t(info.width) * 4);
                AndroidBitmap_unlockPixels(env, thumbnail);
            }
        }
    }
    e->save_screen.set_slot_occupied(index, occupied);
    e->save_slots[size_t(index)] = std::move(slot);
}

JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4SlotsFrame(JNIEnv* env, jobject, jobject bitmap) {
    Engine* e = g_engine;
    if (!e || !e->scene || e->paused) return JNI_FALSE;
    e->tick = Clock::now();
    // LOADBACK/SAVEBACK are translucent. Start from the scene every frame;
    // reusing the already composed menu would darken it on each redraw.
    const bool message_visible = e->scene->message_visible();
    e->scene->set_message_visible(false);
    e->scene->set_wait_marker(false);
    e->scene->draw(-1);
    e->scene->set_message_visible(message_visible);
    e->save_frame = e->scene->frame();
    e->save_screen.draw(e->save_slots, e->save_mx, e->save_my, e->save_down,
                        e->font, &e->save_frame);
    return blit_rgba(env, bitmap, e->save_frame) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4SlotsTouch(JNIEnv* env, jobject, jint action, jint x, jint y) {
    Engine* e = g_engine;
    if (!e || e->paused) return env->NewStringUTF("");
    e->save_mx = x; e->save_my = y;
    if (action == 0) {
        e->save_down = true;
        e->save_screen.pointer_down(x, y);
    } else if (action == 3) {
        e->save_down = false;
        e->save_mx = e->save_my = -1;
        e->save_screen.cancel_pointer();
    } else if (action == 1) {
        e->save_down = false;
        const auto selected = e->save_screen.pointer_up(x, y);
        using Action = dc4ui::SaveActionType;
        switch (selected.type) {
        case Action::Slot:
            return env->NewStringUTF(("slot:" + std::to_string(selected.index)).c_str());
        case Action::Mode: return env->NewStringUTF(selected.index == 1 ? "load" : "save");
        case Action::Back: return env->NewStringUTF("close");
        case Action::LatestLoad: return env->NewStringUTF("latest");
        case Action::QuickLoad: return env->NewStringUTF("quick");
        case Action::Config: return env->NewStringUTF("config");
        case Action::Title: return env->NewStringUTF("title");
        case Action::Quit: return env->NewStringUTF("quit");
        default: break;  // Page changes are applied by the shared controller.
        }
    }
    return env->NewStringUTF("");
}

// ------------------------------------------------------------ Log screen

// Opens the Log screen: scroll snaps to the newest row (native behavior).
JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4LogOpen(JNIEnv*, jobject) {
    Engine* e = g_engine;
    if (!e) return;
    e->log_scroll = dc4ui::LogScreen::max_scroll(e->log_rows.size());
    e->log_mx = e->log_my = -1;
    e->log_drag_y = -1;
    e->log_drag_moved = false;
    e->log_pressed = 0;
    e->tick = Clock::now();
}

// Drops all rows (used after loading a save).
JNIEXPORT void JNICALL
Java_org_dc4engine_Dc4Activity_dc4LogClear(JNIEnv*, jobject) {
    Engine* e = g_engine;
    if (!e) return;
    e->log_rows.clear();
    e->log_scroll = 0;
    if (e->in_game && !e->scene->current_line().empty()) record_log_row(e,e->dialogue_index);
}

// Renders the current scene frame with the Log screen composed over it.
JNIEXPORT jboolean JNICALL
Java_org_dc4engine_Dc4Activity_dc4LogFrame(JNIEnv* env, jobject, jobject bitmap) {
    Engine* e = g_engine;
    if (!e || !e->scene || !e->log_loaded) return JNI_FALSE;
    e->tick = Clock::now();
    const bool visible = e->scene->message_visible();
    e->scene->set_message_visible(false);
    e->scene->set_wait_marker(false);
    e->scene->draw(-1);
    e->scene->set_message_visible(visible);
    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.width != dc4scene::kScreenW || info.height != dc4scene::kScreenH ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 || info.stride < info.width * 4) return JNI_FALSE;
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
    const size_t row_bytes = size_t(dc4scene::kScreenW) * 4;
    std::vector<uint8_t> overlay(e->scene->frame().begin(), e->scene->frame().end());
    e->log.set_latin_mode(dc4::is_latin());
    e->log.draw(e->log_rows, e->log_scroll, e->log_thumbs, e->log_mx, e->log_my, e->font, &overlay);
    for (int row = 0; row < dc4scene::kScreenH; ++row)
        std::memcpy(static_cast<uint8_t*>(pixels) + size_t(row) * info.stride,
                    overlay.data() + size_t(row) * row_bytes, row_bytes);
    AndroidBitmap_unlockPixels(env, bitmap);
    return JNI_TRUE;
}

// Log screen input. Returns "" (nothing to do), "close", "check", "jump",
// "voice", or an error message to notify. Drag scrolling is handled here.
// Android MotionEvent action values: DOWN=0, UP=1, MOVE=2, CANCEL=3.
JNIEXPORT jstring JNICALL
Java_org_dc4engine_Dc4Activity_dc4LogTouch(JNIEnv* env, jobject, jint action, jint x, jint y) {
    Engine* e = g_engine;
    if (!e || !e->log_loaded) return env->NewStringUTF("");
    e->log_mx = x;
    e->log_my = y;
    const size_t count = e->log_rows.size();
    const auto control_at = [&](int px,int py) {
        if (e->log.back_hit(px,py)) return 1;
        if (e->log.checkbox_hit(px,py)) return 2;
        const int jump = e->log.jump_hit(px,py,e->log_scroll,count);
        if (jump >= 0) return 1000+jump;
        const int voice = e->log.voice_hit(px,py,e->log_scroll,count);
        return voice >= 0 ? 10000+voice : 0;
    };
    if (action == 0) {
        e->log_pressed = control_at(x,y);
        e->log_drag_y = y;
        e->log_drag_base = e->log_scroll;
        e->log_drag_moved = false;
        e->log_drag_thumb = e->log.thumb_hit(x, y, e->log_scroll, count);
        return env->NewStringUTF("");
    }
    if (action == 2) {
        if (e->log_drag_y < 0) return env->NewStringUTF("");
        if (std::abs(y - e->log_drag_y) > 12) e->log_drag_moved = true;
        if (e->log_drag_thumb) {
            e->log_scroll = e->log.thumb_scroll(y, count);
        } else {
            // Swipe: drag down shows newer rows (top index decreases).
            const int delta = (y - e->log_drag_y) / dc4ui::LogScreen::kRowPitch;
            e->log_scroll = std::clamp(e->log_drag_base - delta, 0, dc4ui::LogScreen::max_scroll(count));
        }
        return env->NewStringUTF("");
    }
    const bool had_press = e->log_drag_y >= 0;
    e->log_drag_y = -1;
    e->log_mx = e->log_my = -1;
    if (action != 1 || !e->log_drag_moved) {
        e->log_drag_thumb = false;
        if (action != 1) return env->NewStringUTF("");
    } else {
        e->log_drag_thumb = false;
        return env->NewStringUTF("");
    }
    const int pressed = e->log_pressed;
    e->log_pressed = 0;
    if (!had_press || pressed == 0 || control_at(x,y) != pressed) return env->NewStringUTF("");
    if (e->log.back_hit(x, y)) return env->NewStringUTF("close");
    if (e->log.checkbox_hit(x, y)) {
        e->log_thumbs = !e->log_thumbs;
        return env->NewStringUTF("check");
    }
    const int jump = e->log.jump_hit(x, y, e->log_scroll, count);
    if (jump >= 0) {
        return env->NewStringUTF(request_confirm(e,dc4ui::ConfirmKind::SceneJump,[e,jump]{
            const auto error=jump_to_row(e,jump);return error.empty()?std::string("jump"):error;
        }).c_str());
    }
    const int voice = e->log.voice_hit(x, y, e->log_scroll, count);
    if (voice >= 0) {
        if (!play_row_voice(e, e->log_rows[size_t(voice)].voice))
            return env->NewStringUTF("No voice on this row");
        return env->NewStringUTF("voice");
    }
    return env->NewStringUTF("");
}
}  // extern "C"
