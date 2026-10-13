#include "dc4/audio.h"
#include "dc4/xpcm.h"
#include "dc4/resource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "stb_vorbis.c"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace dc4 {
namespace {
bool valid_channel(int channel) { return channel >= 0 && channel < 12; }
int group_for(int channel) { return channel < 2 ? 0 : channel < 8 ? 1 : 2; }
bool valid_group(AudioGroup group) { return unsigned(group) < 3; }
double gain(double db) {
    return db <= -100.0 ? 0 : db >= 0 ? 1 : std::pow(10.0, db / 20.0);
}

// Walk page boundaries, not arbitrary OggS strings inside packet payloads.
bool normalize_ogg(std::vector<uint8_t>* bytes) {
    size_t pos = 0;
    while (pos < bytes->size()) {
        if (bytes->size() - pos < 27 || std::memcmp(bytes->data() + pos, "OggS", 4))
            return false;
        size_t segments = (*bytes)[pos + 26];
        if (bytes->size() - pos - 27 < segments) return false;
        size_t length = 27 + segments;
        for (size_t i = 0; i < segments; ++i) length += (*bytes)[pos + 27 + i];
        if (length > bytes->size() - pos) return false;
        // D.C.4 uses version 2. stb_vorbis does not check page CRCs.
        if ((*bytes)[pos + 4] == 2) (*bytes)[pos + 4] = 0;
        pos += length;
    }
    return pos != 0;
}
}  // namespace

bool decode_xpcm_pcm(const uint8_t* data, size_t size,
                     std::vector<int16_t>* out, uint32_t* rate,
                     uint16_t* channels) {
    if (!data || !out || !rate || !channels) return false;
    out->clear();
    *rate = 0;
    *channels = 0;
    XpcmInfo info;
    if (!parse_xpcm(data, size, &info) || info.header_size > size ||
        info.data_size > size - info.header_size) return false;
    const uint8_t* payload = data + info.header_size;
    size_t length = info.data_size;
    if (info.codec == XpcmCodec::Vorbis) {
        if (length > size_t(std::numeric_limits<int>::max())) return false;
        std::vector<uint8_t> fixed(payload, payload + length);
        if (!normalize_ogg(&fixed)) return false;
        int error = 0;
        stb_vorbis* decoder = stb_vorbis_open_memory(
            fixed.data(), int(fixed.size()), &error, nullptr);
        if (!decoder) return false;
        stb_vorbis_info vi = stb_vorbis_get_info(decoder);
        if ((vi.channels != 1 && vi.channels != 2) || !vi.sample_rate) {
            stb_vorbis_close(decoder);
            return false;
        }
        int16_t block[8192];
        for (;;) {
            int frames = stb_vorbis_get_samples_short_interleaved(
                decoder, vi.channels, block, 8192);
            if (!frames) break;
            out->insert(out->end(), block, block + frames * vi.channels);
        }
        error = stb_vorbis_get_error(decoder);
        stb_vorbis_close(decoder);
        if (error || out->empty()) { out->clear(); return false; }
        *rate = vi.sample_rate;
        *channels = uint16_t(vi.channels);
        return true;
    }
    if (info.codec != XpcmCodec::Pcm || !info.sample_rate ||
        (info.channels != 1 && info.channels != 2) ||
        (info.bits != 8 && info.bits != 16)) return false;
    size_t sample_bytes = info.bits / 8;
    if (!length || length % (sample_bytes * info.channels)) return false;
    out->resize(length / sample_bytes);
    for (size_t i = 0; i < out->size(); ++i) {
        (*out)[i] = info.bits == 8 ? int16_t((int(payload[i]) - 128) * 256)
            : int16_t(uint16_t(payload[2 * i]) | (uint16_t(payload[2 * i + 1]) << 8));
    }
    *rate = info.sample_rate;
    *channels = info.channels;
    return true;
}

void AudioEngine::Ramp::set(double next, uint32_t milliseconds) {
    restore(value, next, uint64_t(milliseconds) * kOutputRate / 1000);
}
void AudioEngine::Ramp::restore(double current, double next, uint64_t frames) {
    value = current;
    target = next;
    remaining = size_t(frames);
    step = remaining ? (target - value) / double(remaining) : 0;
    if (!remaining) value = target;
}
void AudioEngine::Ramp::advance() {
    if (remaining && !--remaining) value = target;
    else if (remaining) value += step;
}

AudioEngine::~AudioEngine() { shutdown(); }
bool AudioEngine::init() {
    if (device_) return true;
    auto* device = new ma_device();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = kOutputRate;
    config.dataCallback = data_callback;
    config.pUserData = this;
    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        delete device;
        return false;
    }
    if (ma_device_start(device) != MA_SUCCESS) {
        ma_device_uninit(device);
        delete device;
        return false;
    }
    device_ = device;
    return true;
}
void AudioEngine::shutdown() {
    if (device_) {
        auto* device = static_cast<ma_device*>(device_);
        ma_device_uninit(device);
        delete device;
        device_ = nullptr;
    }
    stop_all();
    end_movie_audio();
}
void AudioEngine::data_callback(ma_device* device, void* out, const void*, unsigned frames) {
    static_cast<AudioEngine*>(device->pUserData)->render_pcm(static_cast<int16_t*>(out), frames);
}

bool AudioEngine::play_pcm(int channel, std::vector<int16_t> pcm, uint32_t rate,
                           uint16_t channels, const AudioPlayback& options) {
    if (!valid_channel(channel) || !rate || (channels != 1 && channels != 2) ||
        pcm.empty() || pcm.size() % channels) return false;
    if (channels == 1) {
        std::vector<int16_t> stereo(pcm.size() * 2);
        for (size_t i = 0; i < pcm.size(); ++i) stereo[2 * i] = stereo[2 * i + 1] = pcm[i];
        pcm.swap(stereo);
    }
    Channel replacement;
    replacement.pcm = std::move(pcm);
    replacement.end = replacement.pcm.size() / 2;
    replacement.loop = options.loop;
    replacement.playing = true;
    replacement.rate_step = double(rate) / kOutputRate;
    if (options.loop && options.loop_end_frames && options.loop_end_frames <= replacement.end)
        replacement.end = options.loop_end_frames;
    if (options.loop_start_frames < replacement.end)
        replacement.loop_start = options.loop_start_frames;
    {
        // Allocate/decode before the callback lock; destroy old PCM after it.
        // The callback never clears or resizes PCM vectors.
        std::lock_guard<std::mutex> lock(mutex_);
        replacement.volume = group_volume_[group_for(channel)];
        replacement.pan = group_pan_[group_for(channel)];
        std::swap(channels_[channel], replacement);
    }
    return true;
}
bool AudioEngine::play_blob(int channel, const uint8_t* data, size_t size,
                            const AudioPlayback& options) {
    std::vector<int16_t> pcm;
    uint32_t rate;
    uint16_t channels;
    return decode_xpcm_pcm(data, size, &pcm, &rate, &channels) &&
        play_pcm(channel, std::move(pcm), rate, channels, options);
}
bool AudioEngine::play_file(int channel, const std::string& path, const AudioPlayback& options) {
    if (!valid_channel(channel)) return false;
    std::ifstream file(resolve_asset(path), std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return play_blob(channel, bytes.data(), bytes.size(), options);
}
bool AudioEngine::play_bgm_file(const std::string& path, size_t start, size_t end, bool loop) {
    return play_file(0, path, {loop, start, end});
}
void AudioEngine::stop_bgm() { stop_group(AudioGroup::Bgm); }
void AudioEngine::fade_out_bgm(uint32_t fade_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!channels_[0].playing) return;
    std::swap(channels_[0], channels_[1]);
    channels_[0].playing = false;
    stop_channel_locked(1, fade_ms);
}
bool AudioEngine::play_voice_blob(const uint8_t* data, size_t size) {
    return play_blob(8, data, size);
}

int AudioEngine::free_channel(AudioGroup group) const {
    if (!valid_group(group)) return -1;
    if (group == AudioGroup::Bgm) return 0;
    int first = group == AudioGroup::Se ? 3 : 8;
    int last = group == AudioGroup::Se ? 5 : 11;
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = first; i < last; ++i) if (!channels_[i].playing) return i;
    return last;
}
bool AudioEngine::channel_playing(int channel) const {
    if (!valid_channel(channel)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_[channel].playing;
}
bool AudioEngine::group_playing(AudioGroup group) const {
    if (!valid_group(group)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 12; ++i)
        if (group_for(i) == int(group) && channels_[i].playing) return true;
    return false;
}
double AudioEngine::channel_position(int channel) const {
    if (!valid_channel(channel)) return -1;
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_[channel].position;
}
bool AudioEngine::seek_channel(int channel, double source_frame) {
    if (!valid_channel(channel) || !std::isfinite(source_frame) || source_frame < 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Channel& sound = channels_[channel];
    if (sound.pcm.empty() || source_frame > double(sound.end) ||
        (sound.loop && source_frame == double(sound.end))) return false;
    sound.position = source_frame;
    if (source_frame == double(sound.end)) sound.playing = false;
    return true;
}
bool AudioEngine::seek_channel_fraction(int channel, double fraction) {
    if (!valid_channel(channel) || !std::isfinite(fraction) || fraction < 0 || fraction > 1) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& sound = channels_[channel];
    if (sound.pcm.empty()) return false;
    sound.position = fraction * sound.end;
    if (sound.position == double(sound.end)) {
        if (sound.loop) sound.position = sound.loop_start;
        else sound.playing = false;
    }
    return true;
}
double AudioEngine::channel_fraction(int channel) const {
    if (!valid_channel(channel)) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& sound = channels_[channel];
    return sound.end ? sound.position / double(sound.end) : 0;
}
double AudioEngine::channel_duration_seconds(int channel) const {
    if (!valid_channel(channel)) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& sound = channels_[channel];
    return sound.end / (kOutputRate * sound.rate_step);
}
AudioMixerSnapshot AudioEngine::capture_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AudioMixerSnapshot result;
    for (size_t i = 0; i < channels_.size(); ++i) {
        result.channels[i].playing = channels_[i].playing;
        result.channels[i].position_frames = channels_[i].position;
    }
    for (size_t i = 0; i < result.groups.size(); ++i) {
        result.groups[i] = {group_volume_[i].value, group_volume_[i].target,
            group_volume_[i].remaining, group_pan_[i].value, group_pan_[i].target,
            group_pan_[i].remaining};
    }
    return result;
}
bool AudioEngine::valid_group_snapshot(const SceneAudioGroupSnapshot& group) {
    constexpr uint64_t maximum_ramp = uint64_t(kOutputRate) * 86400;
    auto valid = [maximum_ramp](double value, double target, uint64_t frames, double low, double high) {
        return std::isfinite(value) && std::isfinite(target) && value >= low && value <= high &&
            target >= low && target <= high && frames <= maximum_ramp &&
            (frames || value == target);
    };
    return valid(group.volume_db, group.volume_target_db, group.volume_remaining_frames, -100, 0) &&
        valid(group.pan, group.pan_target, group.pan_remaining_frames, -10000, 10000);
}
bool AudioEngine::restore_channels(std::vector<AudioPreparedChannel> prepared,
                                   const std::array<SceneAudioGroupSnapshot, 3>& groups,
                                   std::string* error) {
    auto invalid = [error](const char* message) { if (error) *error = message; return false; };
    if (prepared.size() > 12) return invalid("too many audio channels in snapshot");
    AudioEngine replacement;  // no device: all preparation happens off the callback
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!valid_group_snapshot(groups[i])) return invalid("invalid audio group controls in snapshot");
        const auto& group = groups[i];
        replacement.group_volume_[i].restore(group.volume_db, group.volume_target_db,
                                              group.volume_remaining_frames);
        replacement.group_pan_[i].restore(group.pan, group.pan_target, group.pan_remaining_frames);
    }
    std::array<bool, 12> used{};
    for (auto& item : prepared) {
        if (!valid_channel(item.channel) || used[size_t(item.channel)])
            return invalid("invalid or duplicate audio channel in snapshot");
        used[size_t(item.channel)] = true;
        if (!replacement.play_pcm(item.channel, std::move(item.pcm), item.rate, item.channels, item.options))
            return invalid("invalid decoded audio in snapshot restore");
        if (!replacement.seek_channel(item.channel, item.position_frames))
            return invalid("audio snapshot position is outside the source track");
        if (item.playing && !replacement.channel_playing(item.channel))
            return invalid("playing audio snapshot points to the end of a track");
        if (!item.playing) replacement.stop_channel(item.channel);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        channels_.swap(replacement.channels_);
        group_volume_.swap(replacement.group_volume_);
        group_pan_.swap(replacement.group_pan_);
    }
    if (error) error->clear();
    return true;
}
void AudioEngine::stop_channel_locked(int channel, uint32_t fade_ms) {
    Channel& sound = channels_[channel];
    if (!fade_ms) sound.playing = false;
    else if (sound.playing) {
        sound.volume.set(sound.volume.value >= -30 ? -30 : -100, fade_ms);
        sound.stop_after_fade = true;
    }
}
void AudioEngine::stop_channel(int channel, uint32_t fade_ms) {
    if (!valid_channel(channel)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    stop_channel_locked(channel, fade_ms);
}
void AudioEngine::stop_group(AudioGroup group, uint32_t fade_ms) {
    if (!valid_group(group)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 12; ++i)
        if (group_for(i) == int(group) && i != 6 && i != 7)
            stop_channel_locked(i, fade_ms);
}
void AudioEngine::stop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel& sound : channels_) sound.playing = false;
}
void AudioEngine::set_group_volume(AudioGroup group, int percent, uint32_t fade_ms) {
    if (!valid_group(group)) return;
    percent = std::clamp(percent, 0, 100);
    std::lock_guard<std::mutex> lock(mutex_);
    double db = percent ? double(percent) / 2 - 50 : -100;
    group_volume_[int(group)].set(db, fade_ms);
    for (int i = 0; i < 12; ++i) if (group_for(i) == int(group)) {
        channels_[i].volume.set(db, fade_ms);
        channels_[i].stop_after_fade = false;
    }
}
void AudioEngine::set_group_pan(AudioGroup group, int pan) { fade_group_pan(group, pan, 0); }
void AudioEngine::fade_group_pan(AudioGroup group, int target, uint32_t duration_ms) {
    if (!valid_group(group)) return;
    target = std::clamp(target, -10000, 10000);
    std::lock_guard<std::mutex> lock(mutex_);
    group_pan_[int(group)].set(target, duration_ms);
    for (int i = 0; i < 12; ++i)
        if (group_for(i) == int(group)) channels_[i].pan.set(target, duration_ms);
}

void AudioEngine::set_master_volume(int percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    master_percent_ = std::clamp(percent, 0, 100);
}
void AudioEngine::set_user_volume(AudioGroup group, int percent) {
    if (!valid_group(group)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    user_percent_[int(group)] = std::clamp(percent, 0, 100);
}
void AudioEngine::set_movie_volume(int percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    movie_percent_ = std::clamp(percent, 0, 100);
}
void AudioEngine::set_duck_music_for_voice(bool enabled, int percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    duck_music_ = enabled;
    duck_percent_ = std::clamp(percent, 0, 100);
}
void AudioEngine::set_channel_user_volume(int channel, int percent) {
    if (!valid_channel(channel)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    channels_[channel].user_percent = std::clamp(percent, 0, 100);
}
bool AudioEngine::set_device_paused(bool paused) {
    if (!device_) return true;
    auto* device = static_cast<ma_device*>(device_);
    if (paused == !ma_device_is_started(device)) return true;
    // Never hold the mixer lock while stopping: stop waits for the callback.
    return (paused ? ma_device_stop(device) : ma_device_start(device)) == MA_SUCCESS;
}
int AudioEngine::master_volume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return master_percent_;
}
int AudioEngine::user_volume(AudioGroup group) const {
    if (!valid_group(group)) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    return user_percent_[int(group)];
}
bool AudioEngine::begin_movie_audio(uint32_t sample_rate) {
    if (sample_rate < 8000 || sample_rate > 96000) return false;
    std::vector<float> pcm(kMovieCapacity * 2);
    std::lock_guard<std::mutex> lock(mutex_);
    movie_pcm_.swap(pcm);
    movie_read_ = movie_count_ = 0;
    movie_fraction_ = movie_time_ = 0;
    movie_rate_ = sample_rate;
    movie_audio_ = true;
    movie_paused_ = false;
    return true;
}
bool AudioEngine::queue_movie_audio(const float* stereo, size_t frames) {
    if (!stereo || !frames || frames > kMovieCapacity) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!movie_audio_ || frames > kMovieCapacity - movie_count_) return false;
    for (size_t i = 0; i < frames; ++i) {
        size_t at = (movie_read_ + movie_count_ + i) % kMovieCapacity;
        for (size_t c = 0; c < 2; ++c) {
            const float sample = stereo[i * 2 + c];
            movie_pcm_[at * 2 + c] = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0;
        }
    }
    movie_count_ += frames;
    return true;
}
void AudioEngine::pause_movie_audio(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    movie_paused_ = paused;
}
void AudioEngine::end_movie_audio() {
    std::lock_guard<std::mutex> lock(mutex_);
    movie_audio_ = movie_paused_ = false;
    movie_count_ = movie_read_ = 0;
    movie_fraction_ = movie_time_ = 0;
}
double AudioEngine::movie_audio_time() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return movie_time_;
}
size_t AudioEngine::movie_audio_queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return movie_count_;
}

void AudioEngine::render_pcm(int16_t* stereo, size_t frames) {
    if (!stereo) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t frame = 0; frame < frames; ++frame) {
        double left = 0, right = 0;
        if (movie_audio_) {
            if (!movie_paused_ && movie_count_) {
                const size_t next = (movie_read_ + (movie_count_ > 1 ? 1 : 0)) % kMovieCapacity;
                const double volume = double(master_percent_) * movie_percent_ / 10000.0;
                left = (movie_pcm_[movie_read_ * 2] * (1 - movie_fraction_) +
                    movie_pcm_[next * 2] * movie_fraction_) * 32767 * volume;
                right = (movie_pcm_[movie_read_ * 2 + 1] * (1 - movie_fraction_) +
                    movie_pcm_[next * 2 + 1] * movie_fraction_) * 32767 * volume;
                movie_time_ += 1.0 / kOutputRate;
                movie_fraction_ += double(movie_rate_) / kOutputRate;
                const size_t consumed = std::min(size_t(movie_fraction_), movie_count_);
                movie_fraction_ -= consumed;
                movie_read_ = (movie_read_ + consumed) % kMovieCapacity;
                movie_count_ -= consumed;
                if (!movie_count_) movie_fraction_ = 0;
            }
            stereo[2 * frame] = int16_t(std::clamp(std::lround(left), -32768L, 32767L));
            stereo[2 * frame + 1] = int16_t(std::clamp(std::lround(right), -32768L, 32767L));
            continue;
        }
        bool voice_active = false;
        for (int i = 8; i < 12; ++i) voice_active |= channels_[i].playing && channels_[i].user_percent > 0;
        const double duck_target = duck_music_ && voice_active && user_percent_[2] > 0 ? duck_percent_ / 100.0 : 1.0;
        const double duck_step = 1.0 / (kOutputRate * (duck_target < duck_gain_ ? .10 : .30));
        duck_gain_ += std::clamp(duck_target - duck_gain_, -duck_step, duck_step);
        for (size_t channel = 0; channel < channels_.size(); ++channel) {
            Channel& sound = channels_[channel];
            if (!sound.playing) continue;
            size_t at = size_t(sound.position);
            size_t next = at + 1 < sound.end ? at + 1 : sound.loop ? sound.loop_start : at;
            double fraction = sound.position - double(at);
            double volume = gain(sound.volume.value) * master_percent_ * user_percent_[group_for(int(channel))] / 10000.0;
            volume *= sound.user_percent / 100.0;
            if (channel < 2) volume *= duck_gain_;
            double pan = sound.pan.value / 100.0;
            double l = sound.pcm[at * 2] * (1 - fraction) + sound.pcm[next * 2] * fraction;
            double r = sound.pcm[at * 2 + 1] * (1 - fraction) + sound.pcm[next * 2 + 1] * fraction;
            left += l * volume * gain(pan > 0 ? -pan : 0);
            right += r * volume * gain(pan < 0 ? pan : 0);
            sound.position += sound.rate_step;
            if (sound.position >= double(sound.end)) {
                if (sound.loop)
                    sound.position = double(sound.loop_start) + std::fmod(
                        sound.position - double(sound.end), double(sound.end - sound.loop_start));
                else { sound.position = double(sound.end); sound.playing = false; }
            }
            sound.volume.advance();
            sound.pan.advance();
            if (sound.stop_after_fade && !sound.volume.remaining) sound.playing = false;
        }
        stereo[2 * frame] = int16_t(std::clamp(std::lround(left), -32768L, 32767L));
        stereo[2 * frame + 1] = int16_t(std::clamp(std::lround(right), -32768L, 32767L));
        for (Ramp& pan : group_pan_) pan.advance();
        for (Ramp& volume : group_volume_) volume.advance();
    }
}
}  // namespace dc4
