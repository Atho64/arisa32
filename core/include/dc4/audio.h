// Portable XPCM playback with the adv32 channel layout.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct ma_device;

namespace dc4 {

enum class AudioGroup { Bgm = 0, Se = 1, Voice = 2 };

struct AudioPlayback {
    bool loop = false;
    // Source-rate frames, with an exclusive end. Zero end means the whole file.
    size_t loop_start_frames = 0;
    size_t loop_end_frames = 0;
};

// Portable control state. Ramp durations are output frames, not source frames.
struct SceneAudioGroupSnapshot {
    double volume_db = 0, volume_target_db = 0;
    uint64_t volume_remaining_frames = 0;
    double pan = 0, pan_target = 0;
    uint64_t pan_remaining_frames = 0;
};

struct AudioMixerSnapshot {
    struct Channel { bool playing = false; double position_frames = 0; };
    std::array<Channel, 12> channels;
    std::array<SceneAudioGroupSnapshot, 3> groups;
};

// Temporary decoded material for an atomic mixer restore; never serialized.
struct AudioPreparedChannel {
    int channel = 0;
    std::vector<int16_t> pcm;
    uint32_t rate = 0;
    uint16_t channels = 0;
    AudioPlayback options;
    double position_frames = 0;
    bool playing = true;
};

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();
    bool play_bgm_file(const std::string& path, size_t loop_start_frames = 0,
                       size_t loop_end_frames = 0, bool loop = true);
    void stop_bgm();
    // Move current music to the native outgoing channel before replacing it.
    void fade_out_bgm(uint32_t fade_ms = 500);
    bool play_voice_blob(const uint8_t* data, size_t size);

    // 0/1 BGM, 2 looping SE, 3..5 one-shot SE, 6/7 system effects, 8..11 voice.
    bool play_file(int channel, const std::string& path,
                   const AudioPlayback& options = {});
    bool play_blob(int channel, const uint8_t* data, size_t size,
                   const AudioPlayback& options = {});
    bool play_pcm(int channel, std::vector<int16_t> pcm, uint32_t rate,
                  uint16_t channels, const AudioPlayback& options = {});
    int free_channel(AudioGroup group) const;
    bool channel_playing(int channel) const;
    bool group_playing(AudioGroup group) const;
    bool voice_playing() const { return group_playing(AudioGroup::Voice); }
    double channel_position(int channel) const;
    bool seek_channel(int channel, double source_frame);
    bool seek_channel_fraction(int channel, double fraction);
    double channel_fraction(int channel) const;
    double channel_duration_seconds(int channel) const;
    AudioMixerSnapshot capture_snapshot() const;
    // Validates/prepares every replacement before changing any live channel.
    bool restore_channels(std::vector<AudioPreparedChannel> prepared,
                           const std::array<SceneAudioGroupSnapshot, 3>& groups,
                           std::string* error = nullptr);
    static bool valid_group_snapshot(const SceneAudioGroupSnapshot& group);

    void stop_channel(int channel, uint32_t fade_ms = 0);
    // Native stop groups exclude system SE channels 6 and 7.
    void stop_group(AudioGroup group, uint32_t fade_ms = 0);
    void stop_all();
    // Native percentage mapping: 1..100 -> -49.5..0 dB; zero is silence.
    void set_group_volume(AudioGroup group, int percent, uint32_t fade_ms = 0);
    // DirectSound pan units (-10000 left, 0 centre, +10000 right).
    void set_group_pan(AudioGroup group, int pan);
    void fade_group_pan(AudioGroup group, int target, uint32_t duration_ms);

    // User configuration multiplies script volume; scripts/saves never reset it.
    void set_master_volume(int percent);
    void set_user_volume(AudioGroup group, int percent);
    void set_movie_volume(int percent);
    void set_duck_music_for_voice(bool enabled, int percent = 25);
    void set_channel_user_volume(int channel, int percent);
    bool set_device_paused(bool paused);
    int master_volume() const;
    int user_volume(AudioGroup group) const;
    bool device_ready() const { return device_ != nullptr; }

    // One isolated, bounded movie stream. Ordinary channels and their ramps
    // are suspended until end_movie_audio(). The producer is the host thread;
    // the device callback only consumes predecoded stereo PCM.
    bool begin_movie_audio(uint32_t sample_rate);
    bool queue_movie_audio(const float* stereo, size_t frames);
    void pause_movie_audio(bool paused);
    void end_movie_audio();
    double movie_audio_time() const;
    size_t movie_audio_queued() const;

    // Same mixer as the device callback; usable without opening a device.
    void render_pcm(int16_t* stereo, size_t frames);
    static constexpr uint32_t kOutputRate = 44100;

private:
    struct Ramp {
        double value = 0, target = 0, step = 0;
        size_t remaining = 0;
        void set(double next, uint32_t milliseconds);
        void restore(double current, double next, uint64_t frames);
        void advance();
    };
    struct Channel {
        std::vector<int16_t> pcm;  // source-rate interleaved stereo
        double position = 0, rate_step = 1;
        size_t loop_start = 0, end = 0;
        bool loop = false, playing = false, stop_after_fade = false;
        int user_percent = 100;
        Ramp volume, pan;
    };
    void* device_ = nullptr;
    mutable std::mutex mutex_;
    std::array<Channel, 12> channels_;
    std::array<Ramp, 3> group_volume_;
    std::array<Ramp, 3> group_pan_;
    int master_percent_ = 100;
    int movie_percent_ = 100;
    bool duck_music_ = false;
    int duck_percent_ = 25;
    double duck_gain_ = 1.0;
    std::array<int, 3> user_percent_{{100, 100, 100}};
    static constexpr size_t kMovieCapacity = kOutputRate * 4;
    std::vector<float> movie_pcm_;
    size_t movie_read_ = 0, movie_count_ = 0;
    double movie_fraction_ = 0, movie_time_ = 0;
    uint32_t movie_rate_ = kOutputRate;
    bool movie_audio_ = false, movie_paused_ = false;
    void stop_channel_locked(int channel, uint32_t fade_ms);
    static void data_callback(ma_device* dev, void* out, const void* in,
                              unsigned int frames);
};

// Decode XPCM Vorbis or raw PCM to native-rate interleaved signed PCM16.
bool decode_xpcm_pcm(const uint8_t* data, size_t size,
                     std::vector<int16_t>* out, uint32_t* rate,
                     uint16_t* channels);

}  // namespace dc4
