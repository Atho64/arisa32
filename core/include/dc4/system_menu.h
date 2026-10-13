// Portable original MES-driven system menus. The host owns playback and saves.
#pragma once
#include "dc4/scene.h"
#include "dc4/confirm_dialog.h"
#include <memory>
#include <set>

namespace dc4ui {
enum class SystemMenuPage { Title, Extras, Config, Gallery, Music, Scenes };
enum class SystemMenuActionType {
    None, Back, NewGame, Load, LatestLoad, QuickLoad, AutoLoad, Save, Quit,
    SettingsChanged, PlayMusic, StopMusic, SeekMusic, PlayVoice, PlayMovie, StartScene
};
struct SystemMenuAction {
    SystemMenuActionType type = SystemMenuActionType::None;
    int index = -1;
    std::string asset; // Original script/music/voice/movie resource name.
    std::string label; // Original MES entry label for scene replay.
};
struct SystemConfig {
    int master_volume = 100, music_volume = 80, se_volume = 80;
    int voice_volume = 100, movie_volume = 100;
    bool master_enabled = true, music_enabled = true, se_enabled = true;
    bool voice_enabled = true, movie_enabled = true, duck_music_for_voice = true;
    int text_speed = 8, auto_speed = 8, auto_wait = 12; // Native 0..11 / 0..20.
    int window_opacity = 80; // Percent.
    bool skip_unread = false, stop_skip_at_choice = true;
    bool stop_auto_at_choice = true, wait_for_voice = true;
    bool stop_voice_on_advance = false, fullscreen = false;
    int effects = 1, effect_speed = 5; // 0=off, 1=on, 2=skip-dependent.
    std::array<int, 51> character_volume{};
    std::array<bool, 51> character_muted{};
};

class SystemMenu {
public:
    SystemMenu();
    ~SystemMenu();
    SystemMenu(SystemMenu&&) noexcept;
    SystemMenu& operator=(SystemMenu&&) noexcept;
    SystemMenu(const SystemMenu&) = delete;
    SystemMenu& operator=(const SystemMenu&) = delete;
    bool load(const std::string& data_dir, std::string* error = nullptr);
    bool loaded() const;
    void open(SystemMenuPage page);
    // Restore the character tab and page from the last selected replay.
    void resume_scenes();
    SystemMenuPage page() const;
    void set_in_game(bool value);
    void set_save_available(bool latest, bool quick, bool automatic = false);
    void set_config_page(int zero_based_page); // Seven original pages.
    int config_page() const;
    void pointer_down(int x, int y);
    SystemMenuAction pointer_move(int x, int y); // Updates a captured slider.
    SystemMenuAction pointer_up(int x, int y);
    void cancel_pointer();
    void scroll(int delta); // Music list and gallery variant browsing.
    SystemMenuAction back();
    SystemMenuAction activate(const std::string& original_tag);
    bool confirming() const;
    SystemMenuAction answer_confirmation(bool yes);
    void draw(int mouse_x, int mouse_y, bool down,
              const dc4scene::FontRenderer& font, std::vector<uint8_t>* frame) const;
    SystemConfig config() const;
    // Raw native variable access supports platform settings and progression.
    int variable(int bank, int index) const;
    void set_variable(int bank, int index, int value);
    // Seed a newly loaded story/replay before its first VM advance.
    void initialize_scene(dc4scene::SceneRunner& scene, bool replay = false) const;
    // Only emulator-owned profile files may be passed here.
    bool load_profile(const std::string& path, std::string* error = nullptr);
    bool save_profile(const std::string& path, std::string* error = nullptr) const;
    void mark_cg_seen(const std::string& asset);
    void mark_music_seen(const std::string& asset);
    void mark_scene_seen(const std::string& script, const std::string& label = {});
    void observe(const dc4::MesCommand& command, const std::string& script = {});
    bool sync_progress(const dc4scene::SceneRunner& scene);
    void set_music_position(int percent);
    bool cg_seen(const std::string& asset) const;
    bool music_seen(const std::string& asset) const;
    bool scene_seen(const std::string& script, const std::string& label = {}) const;
    // Geometry/introspection for accessibility and deterministic tests.
    struct Control { int x, y, w, h; std::string tag; bool enabled; };
    std::vector<Control> controls() const;
    std::string error() const;
private:
    SystemMenuAction activate_confirmed(const std::string& tag);
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace dc4ui
