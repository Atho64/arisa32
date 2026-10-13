#include "dc4/scene_audio.h"
#include "dc4/bgm.h"
#include "dc4/resource.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace dc4 {
namespace {
std::string lower(std::string name) {
    for (char& c : name) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    }
    return name;
}
std::string resolve(const std::string& base, std::string relative) {
    namespace fs = std::filesystem;
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (fs::path(relative).is_absolute() || relative.find("..") != std::string::npos) return {};
    const auto selected = resolve_asset((fs::path(base) / relative).string());
    if (asset_exists(selected)) return selected;
    fs::path path(base);
    std::error_code error;
    for (const auto& component : fs::path(relative)) {
        // Resource names are relative to AdvData.
        if (component == ".." || component.has_root_path()) return {};
        auto direct = path / component;
        if (fs::exists(direct, error)) { path = direct; continue; }
        bool found = false;
        for (fs::directory_iterator i(path, error), end; !error && i != end; i.increment(error)) {
            if (lower(i->path().filename().string()) == lower(component.string())) {
                path = i->path();
                found = true;
                break;
            }
        }
        if (!found) return {};
    }
    return path.string();
}
bool read_bytes(const std::string& path, std::vector<uint8_t>* bytes) {
    std::ifstream file(resolve_asset(path), std::ios::binary);
    if (!file) return false;
    bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}
uint32_t u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
        (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}
bool read_table(const std::vector<uint8_t>& bytes, size_t* at, size_t stride,
                uint32_t maximum, std::vector<std::string>* names) {
    if (*at > bytes.size() || bytes.size() - *at < 4) return false;
    uint32_t count = u32(bytes, *at);
    *at += 4;
    if (count > maximum || count > (bytes.size() - *at) / stride) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const char* start = reinterpret_cast<const char*>(bytes.data() + *at);
        const char* end = static_cast<const char*>(std::memchr(start, 0, 32));
        if (!end) return false;
        names->emplace_back(start, end);
        *at += stride;
    }
    return true;
}
std::string audio_name(std::string name) {
    if (std::filesystem::path(name).extension().empty()) name += ".pcm";
    return name;
}
uint32_t ticks_ms(unsigned ticks) {
    // Native ramps are per-update ticks. 60 Hz is the desktop playback clock.
    return uint32_t((uint64_t(ticks) * 1000 + 30) / 60);
}
AudioPlayback bgm_options(const std::string& name, int mode, bool alternate) {
    int start = 0, end = 0;
    std::string normal = lower(name);
    bgm_loop_for(normal.substr(normal.find_last_of('/') + 1), &start, &end);
    return {alternate ? mode == 0 : mode < 2, size_t(std::max(start, 0)), size_t(std::max(end, 0))};
}
int se_channel(const std::string& name, bool loop, int free_channel) {
    std::string normal = lower(name);
    if (normal.rfind("se/201", 0) == 0) return 7;
    if (normal.rfind("se/200", 0) == 0 || normal.rfind("se/202", 0) == 0 ||
        normal.rfind("se/473", 0) == 0) return 6;
    return loop ? 2 : free_channel;
}
bool safe_resource_name(const std::string& name) {
    if (name.empty() || name.size() > 260 || name.find('\0') != std::string::npos) return false;
    std::string normal = lower(name);
    if (normal.front() == '/' || normal.find(':') != std::string::npos) return false;
    for (const auto& part : std::filesystem::path(normal))
        if (part == "..") return false;
    return true;
}
void volume_state(SceneAudioGroupSnapshot* group, int percent, uint32_t milliseconds) {
    group->volume_target_db = percent ? double(percent) / 2 - 50 : -100;
    group->volume_remaining_frames = uint64_t(milliseconds) * AudioEngine::kOutputRate / 1000;
    if (!group->volume_remaining_frames) group->volume_db = group->volume_target_db;
}
}  // namespace

SceneAudio::SceneAudio(const std::string& data_dir) : data_(data_dir) {}
void SceneAudio::set_character_volume(size_t id, int percent, bool muted) {
    if (id >= character_volume_.size()) return;
    character_volume_[id] = muted ? 0 : std::clamp(percent, 0, 100);
    if (audio_ && id == voice_character_)
        for (int channel=8; channel<12; ++channel) audio_->set_channel_user_volume(channel, character_volume_[id]);
}
void SceneAudio::apply_character_volume(const std::string& name) {
    // Original charv_set slots 0..11. ASU/ASA share Arisu's control;
    // HYR/HIY share Hiyori's. Unmapped guest voices use the 'other' group.
    static const char* actors[] = {"asa","nin","sor","hyr","sii","miu","tiy","sgn","kan","izu","ktr","che"};
    std::string actor = lower(name.substr(name.find_last_of('_') + 1, 3));
    if (actor == "asu") actor = "asa";
    if (actor == "hiy") actor = "hyr";
    voice_character_ = 15;
    for (size_t i = 0; i < 12; ++i) if (actor == actors[i]) voice_character_ = i;
    if (audio_) for (int channel = 8; channel < 12; ++channel)
        audio_->set_channel_user_volume(channel, character_volume_[voice_character_]);
}
void SceneAudio::stop_for_movie() {
    if (audio_) for (auto group : {AudioGroup::Bgm, AudioGroup::Se, AudioGroup::Voice})
        audio_->stop_group(group);
    state_.bgm_id = -1;
    state_.bgm_playing = false;
    state_.bgm_position_frames = 0;
    state_.looped_se.erase(std::remove_if(state_.looped_se.begin(), state_.looped_se.end(),
        [](const auto& se) { return se.channel < 6; }), state_.looped_se.end());
    state_.last_voice_name.clear();
    last_voice_.clear();
    pending_bgm_fade_ = false;
}
void SceneAudio::fail(const std::string& message) {
    last_error_ = message;
    std::fprintf(stderr, "scene audio: %s\n", message.c_str());
}
bool SceneAudio::load_names(std::string* error) {
    std::vector<uint8_t> bytes;
    std::vector<std::string> bgm, se;
    size_t at = 228;  // u32 version + 224 bytes of character names
    bool valid = read_bytes(resolve(data_, "GRP/NAMES.DAT"), &bytes) &&
        bytes.size() >= at && u32(bytes, 0) == 7 &&
        read_table(bytes, &at, 48, 400, &bgm) && read_table(bytes, &at, 36, 600, &se);
    if (!valid) {
        last_error_ = "invalid or missing GRP/NAMES.DAT version 7 audio tables";
        if (error) *error = last_error_;
        return false;
    }
    bgm_names_.swap(bgm);
    se_names_.swap(se);
    return true;
}
void SceneAudio::reset_state() {
    if (audio_) audio_->stop_all();
    state_ = SceneAudioSnapshot{};
    pending_bgm_fade_ = false;
    last_voice_.clear();
    last_error_.clear();
    if (audio_) for (int i = 0; i < 3; ++i) {
        audio_->set_group_volume(AudioGroup(i), 100);
        audio_->set_group_pan(AudioGroup(i), 0);
    }
}
bool SceneAudio::play_bgm(int id, int mode, bool alternate) {
    if (state_.saved_bgm_volume >= 0) {
        if (audio_) state_.groups = audio_->capture_snapshot().groups;
        state_.bgm_volume = state_.saved_bgm_volume;
        state_.saved_bgm_volume = -1;
        volume_state(&state_.groups[0], state_.bgm_volume, 500);
        if (audio_) audio_->set_group_volume(AudioGroup::Bgm, state_.bgm_volume, 500);
    }
    if (id == 255) {
        if (audio_) audio_->stop_bgm();
        state_.bgm_id = -1;
        state_.bgm_playing = false;
        state_.bgm_position_frames = 0;
        return true;
    }
    if (!alternate && mode == 1 && id == state_.bgm_id) return true;
    if (bgm_names_.empty() && !load_names()) { fail(last_error_); return false; }
    if (id < 0 || size_t(id) >= bgm_names_.size()) { fail("BGM index out of range"); return false; }
    std::string name = audio_name(bgm_names_[size_t(id)]);
    if (audio_) {
        AudioPlayback options = bgm_options(name, mode, alternate);
        audio_->fade_out_bgm();
        if (!audio_->play_file(0, resolve(data_, name), options)) {
            fail("cannot decode BGM " + name);
            state_.bgm_id = -1;
            state_.bgm_playing = false;
            state_.bgm_position_frames = 0;
            return false;
        }
    }
    state_.bgm_id = id;
    state_.bgm_mode = mode;
    state_.bgm_alternate = alternate;
    state_.bgm_playing = true;
    state_.bgm_position_frames = 0;
    return true;
}
bool SceneAudio::play_se(int id, bool loop) {
    if (id == -2) {
        if (audio_) audio_->stop_group(AudioGroup::Voice);
        last_voice_.clear();
        state_.last_voice_name.clear();
        return true;
    }
    if (id == -1) {
        if (audio_) audio_->stop_group(AudioGroup::Se, 500);
        auto& entries = state_.looped_se;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [](const auto& entry) { return entry.channel < 6; }), entries.end());
        return true;
    }
    if (se_names_.empty() && !load_names()) { fail(last_error_); return false; }
    if (id < 0 || size_t(id) >= se_names_.size()) { fail("SE index out of range"); return false; }
    std::string name = audio_name(se_names_[size_t(id)]);
    std::string normal = lower(name);
    if (normal.find("silent") != std::string::npos) {
        if (audio_) {
            audio_->stop_group(AudioGroup::Se);
            audio_->stop_channel(6);
            audio_->stop_channel(7);
        }
        state_.looped_se.clear();
    }
    int channel = se_channel(name, loop, audio_ ? audio_->free_channel(AudioGroup::Se) : 3);
    if (audio_ && !audio_->play_file(channel, resolve(data_, name), {loop, 0, 0})) {
        fail("cannot decode SE " + name);
        return false;
    }
    auto& entries = state_.looped_se;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [channel](const auto& entry) { return entry.channel == channel; }), entries.end());
    if (loop) entries.push_back({id, channel, 0});
    return true;
}
bool SceneAudio::read_voice(const std::string& name, std::vector<uint8_t>* bytes) const {
    if (!safe_resource_name(name)) return false;
    VoiceArchive local_archive;
    VoiceArchive* archive = voices_;
    if (!archive && local_archive.open(resolve(data_, "VOICE.DAT"), nullptr)) archive = &local_archive;
    const VoiceEntry* entry = archive ? archive->find(name) : nullptr;
    return entry ? archive->read_entry(*entry, bytes, nullptr)
                 : read_bytes(resolve(data_, audio_name(name)), bytes);
}
bool SceneAudio::play_voice(const std::string& name) {
    if (!safe_resource_name(name)) { fail("invalid voice resource name"); return false; }
    if (!audio_) {
        state_.last_voice_name = name;
        last_voice_.clear();
        return true;
    }
    std::vector<uint8_t> bytes;
    std::vector<int16_t> pcm;
    uint32_t rate = 0;
    uint16_t channels = 0;
    if (!read_voice(name, &bytes) || !decode_xpcm_pcm(bytes.data(), bytes.size(), &pcm, &rate, &channels)) {
        fail("cannot decode voice " + name);
        return false;
    }
    audio_->stop_group(AudioGroup::Voice);
    if (!audio_->play_pcm(audio_->free_channel(AudioGroup::Voice), std::move(pcm), rate, channels)) return false;
    apply_character_volume(name);
    state_.last_voice_name = name;
    last_voice_.swap(bytes);
    return true;
}
bool SceneAudio::replay_voice() {
    if (!audio_ || state_.last_voice_name.empty()) return false;
    if (last_voice_.empty()) return play_voice(state_.last_voice_name);
    audio_->stop_group(AudioGroup::Voice);
    if (!audio_->play_voice_blob(last_voice_.data(), last_voice_.size())) return false;
    apply_character_volume(state_.last_voice_name);
    return true;
}

SceneAudioSnapshot SceneAudio::capture_snapshot() const {
    SceneAudioSnapshot result = state_;
    if (!audio_) return result;
    auto mixer = audio_->capture_snapshot();
    result.groups = mixer.groups;
    if (result.bgm_id >= 0) {
        result.bgm_playing = mixer.channels[0].playing;
        result.bgm_position_frames = mixer.channels[0].position_frames;
    }
    auto& effects = result.looped_se;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [&](auto& effect) {
        const auto& channel = mixer.channels[size_t(effect.channel)];
        effect.position_frames = channel.position_frames;
        return !channel.playing;
    }), effects.end());
    return result;
}

bool SceneAudio::restore_snapshot(const SceneAudioSnapshot& snapshot, bool replay,
                                  std::string* error) {
    auto invalid = [&](const std::string& reason) {
        last_error_ = "audio snapshot: " + reason;
        if (error) *error = last_error_;
        return false;
    };
    if (snapshot.version != 1) return invalid("unsupported version " + std::to_string(snapshot.version));
    if (snapshot.bgm_id < -1 || snapshot.bgm_id == 255 || snapshot.bgm_mode < 0 ||
        snapshot.bgm_mode > 32767 || snapshot.bgm_volume < 0 || snapshot.bgm_volume > 100 ||
        snapshot.saved_bgm_volume < -1 || snapshot.saved_bgm_volume > 100)
        return invalid("invalid BGM state");
    if (!std::isfinite(snapshot.bgm_position_frames) || snapshot.bgm_position_frames < 0 ||
        (snapshot.bgm_id == -1 && (snapshot.bgm_playing || snapshot.bgm_position_frames != 0)))
        return invalid("invalid BGM playback position");
    for (const auto& group : snapshot.groups)
        if (!AudioEngine::valid_group_snapshot(group)) return invalid("invalid volume or pan controls");
    if (snapshot.looped_se.size() > 3) return invalid("too many looping effects");
    if (!snapshot.last_voice_name.empty() && !safe_resource_name(snapshot.last_voice_name))
        return invalid("invalid voice resource name");
    if ((snapshot.bgm_id >= 0 || !snapshot.looped_se.empty()) &&
        (bgm_names_.empty() || se_names_.empty()) && !load_names()) return invalid(last_error_);

    std::vector<AudioPreparedChannel> prepared;
    auto prepare = [&](const std::string& name, int channel, const AudioPlayback& options,
                       double position, bool playing) {
        if (!safe_resource_name(name)) return invalid("invalid resource path " + name);
        std::vector<uint8_t> bytes;
        AudioPreparedChannel item;
        item.channel = channel;
        item.options = options;
        item.position_frames = position;
        item.playing = playing;
        if (!read_bytes(resolve(data_, audio_name(name)), &bytes) ||
            !decode_xpcm_pcm(bytes.data(), bytes.size(), &item.pcm, &item.rate, &item.channels))
            return invalid("missing or undecodable track " + name);
        prepared.push_back(std::move(item));
        return true;
    };
    if (snapshot.bgm_id >= 0) {
        if (size_t(snapshot.bgm_id) >= bgm_names_.size()) return invalid("BGM index out of range");
        const auto& name = bgm_names_[size_t(snapshot.bgm_id)];
        if (!prepare(name, 0, bgm_options(name, snapshot.bgm_mode, snapshot.bgm_alternate),
                     snapshot.bgm_position_frames, snapshot.bgm_playing)) return false;
    }
    std::array<bool, 12> seen{};
    for (const auto& effect : snapshot.looped_se) {
        if (effect.id < 0 || size_t(effect.id) >= se_names_.size()) return invalid("SE index out of range");
        if ((effect.channel != 2 && effect.channel != 6 && effect.channel != 7) ||
            seen[size_t(effect.channel)] || !std::isfinite(effect.position_frames) || effect.position_frames < 0)
            return invalid("invalid looping SE channel or position");
        seen[size_t(effect.channel)] = true;
        const auto& name = se_names_[size_t(effect.id)];
        if (effect.channel != se_channel(name, true, 2)) return invalid("SE channel does not match its resource");
        if (!prepare(name, effect.channel, {true, 0, 0}, effect.position_frames, true)) return false;
    }
    std::vector<uint8_t> voice_bytes;
    if (!snapshot.last_voice_name.empty()) {
        AudioPreparedChannel voice;
        voice.channel = 8;
        if (!read_voice(snapshot.last_voice_name, &voice_bytes) ||
            !decode_xpcm_pcm(voice_bytes.data(), voice_bytes.size(), &voice.pcm, &voice.rate, &voice.channels))
            return invalid("missing or undecodable voice " + snapshot.last_voice_name);
        if (replay) prepared.push_back(std::move(voice));
    }

    // A detached mixer performs the same complete validation in no-audio mode.
    // The live restore swaps all prepared channels under one callback lock.
    AudioEngine detached;
    std::string mixer_error;
    if (!(audio_ ? audio_ : &detached)->restore_channels(std::move(prepared), snapshot.groups, &mixer_error))
        return invalid(mixer_error);
    state_ = snapshot;
    last_voice_.swap(voice_bytes);
    pending_bgm_fade_ = false;
    last_error_.clear();
    if (error) error->clear();
    return true;
}

bool SceneAudio::dispatch(const MesCommand& c) {
    if (pending_bgm_fade_) {
        pending_bgm_fade_ = false;
        bool extension = c.kind == CmdKind::Words4 && c.op == 0x77;
        if (audio_) audio_->stop_group(AudioGroup::Bgm,
            extension && c.words[0] ? ticks_ms(c.words[0]) : 500);
        if (extension) return true;
    }
    if (c.kind == CmdKind::Str && (c.op == 0x4C || c.op == 0x4D || c.op == 0x4E)) {
        play_voice(c.str);
        return true;
    }
    if (c.kind == CmdKind::Param2 && c.op == 0x13) {
        state_.bgm_id = -1;
        state_.bgm_playing = false;
        state_.bgm_position_frames = 0;
        pending_bgm_fade_ = c.p2 == 255;
        if (audio_ && !pending_bgm_fade_) audio_->stop_group(AudioGroup::Bgm, 500);
        return true;
    }
    if (c.kind == CmdKind::Param2 && c.op == 0x05) {
        if (c.p1 == 254) return true;  // original flushes queued audio commands
        if (audio_) state_.groups = audio_->capture_snapshot().groups;
        if (c.p1 == 255) {
            if (state_.saved_bgm_volume >= 0) state_.bgm_volume = state_.saved_bgm_volume;
            state_.saved_bgm_volume = -1;
        } else {
            state_.saved_bgm_volume = state_.bgm_volume;
            state_.bgm_volume = std::clamp(int(c.p1) * state_.bgm_volume / 100, 0, 100);
        }
        volume_state(&state_.groups[0], state_.bgm_volume, 500);
        if (audio_) audio_->set_group_volume(AudioGroup::Bgm, state_.bgm_volume, 500);
        return true;
    }
    if (c.kind != CmdKind::Words4) return false;
    if (c.op == 0x6F || c.op == 0x70) {
        play_bgm(int16_t(c.words[0]), int16_t(c.words[1]), c.op == 0x6F);
        return true;
    }
    if (c.op == 0x71) {
        play_se(int16_t(c.words[0]), c.words[1] != 0);
        return true;
    }
    if (c.op == 0xAF) {
        if (c.words[3] < 3) {
            AudioGroup group = AudioGroup(c.words[3]);
            int start = std::clamp(int16_t(c.words[0]) * 100, -10000, 10000);
            int target = std::clamp(int16_t(c.words[1]) * 100, -10000, 10000);
            auto& controls = state_.groups[c.words[3]];
            controls.pan = start;
            controls.pan_target = c.words[2] ? target : start;
            controls.pan_remaining_frames = uint64_t(ticks_ms(c.words[2])) * AudioEngine::kOutputRate / 1000;
            if (audio_) {
                audio_->set_group_pan(group, start);
                if (c.words[2]) audio_->fade_group_pan(group, target, ticks_ms(c.words[2]));
            }
        }
        return true;
    }
    return false;
}
}  // namespace dc4
