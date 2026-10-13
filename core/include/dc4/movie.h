// Portable MPEG-1 Program Stream playback (video + MPEG Layer II audio).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dc4 {
class AudioEngine;
class MoviePlayer {
public:
    MoviePlayer();
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;
    bool open(const std::string& data_dir, const std::string& name,
              AudioEngine* audio = nullptr, std::string* error = nullptr);
    void close();
    void update(uint32_t elapsed_ms);
    void set_paused(bool paused);
    void draw_rgba(std::vector<uint8_t>* rgba, int width, int height) const;
    bool active() const;
    bool finished() const;
    const std::string& error() const;
    int width() const;
    int height() const;
    double time() const;
    double duration() const;
    uint64_t decoded_frames() const;
    uint64_t decoded_audio_frames() const;
    float audio_peak() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dc4
