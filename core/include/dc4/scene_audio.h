// Evidence-backed audio subset of the adv32 MES dispatcher.
#pragma once
#include "dc4/audio.h"
#include "dc4/messtream.h"
#include "dc4/voice.h"
#include <string>
#include <vector>

namespace dc4 {

struct SceneLoopedSeSnapshot {
    int32_t id = -1;
    int32_t channel = 2;
    double position_frames = 0;
};

// Versioned logical state for this emulator's saves, not native adv32 saves.
// Positions are fractional source-rate frames. No PCM/blob data is serialized.
struct SceneAudioSnapshot {
    uint32_t version = 1;
    int32_t bgm_id = -1;
    int32_t bgm_mode = 0;
    bool bgm_alternate = false;
    bool bgm_playing = false;
    double bgm_position_frames = 0;
    std::vector<SceneLoopedSeSnapshot> looped_se;
    std::string last_voice_name;
    int32_t bgm_volume = 100;
    int32_t saved_bgm_volume = -1;
    std::array<SceneAudioGroupSnapshot, 3> groups;
};

class SceneAudio {
public:
    explicit SceneAudio(const std::string& data_dir);
    void set_engine(AudioEngine* engine, VoiceArchive* voices) {
        audio_ = engine;
        voices_ = voices;
    }
    // True means this is a supported audio instruction, including no-audio mode.
    bool dispatch(const MesCommand& command);
    bool replay_voice();
    bool voice_playing() const { return audio_ && audio_->voice_playing(); }
    void reset_state();
    // MES 0x52 stops BGM/SE/voice, leaving native system SE channels 6/7.
    void stop_for_movie();
    void set_character_volume(size_t id, int percent, bool muted = false);
    SceneAudioSnapshot capture_snapshot() const;
    bool restore_snapshot(const SceneAudioSnapshot& snapshot, bool replay_voice = false,
                           std::string* error = nullptr);

    // NAMES.DAT metadata, also useful to a future sound gallery.
    bool load_names(std::string* error = nullptr);
    const std::vector<std::string>& bgm_names() const { return bgm_names_; }
    const std::vector<std::string>& se_names() const { return se_names_; }
    const std::string& last_error() const { return last_error_; }
    int current_bgm() const { return state_.bgm_id; }
    // Voice entry attached to the current line ("" when silent).
    const std::string& last_voice_name() const { return state_.last_voice_name; }

private:
    std::string data_, last_error_;
    AudioEngine* audio_ = nullptr;
    VoiceArchive* voices_ = nullptr;
    std::vector<std::string> bgm_names_, se_names_;
    std::vector<uint8_t> last_voice_;
    SceneAudioSnapshot state_;
    bool pending_bgm_fade_ = false;
    std::array<int, 51> character_volume_ = [] { std::array<int, 51> values{}; values.fill(100); return values; }();
    size_t voice_character_ = 15;
    void apply_character_volume(const std::string& name);
    bool play_bgm(int id, int mode, bool alternate);
    bool play_se(int id, bool loop);
    bool play_voice(const std::string& name);
    bool read_voice(const std::string& name, std::vector<uint8_t>* bytes) const;
    void fail(const std::string& message);
};
}  // namespace dc4
