#include "dc4/audio.h"
#include "dc4/scene_audio.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

static void check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::vector<int16_t> render(dc4::AudioEngine& audio, size_t frames) {
    std::vector<int16_t> output(frames * 2);
    audio.render_pcm(output.data(), frames);
    return output;
}
static void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(v >> (8 * i));
}

int main(int argc, char** argv) {
    using namespace dc4;
    AudioEngine audio;
    check(audio.play_pcm(0, {100, 200, 300, 400, 500}, 44100, 1, {true, 1, 4}), "loop PCM load");
    auto output = render(audio, 8);
    const int16_t looped[] = {100, 200, 300, 400, 200, 300, 400, 200};
    for (size_t i = 0; i < 8; ++i)
        check(output[2 * i] == looped[i] && output[2 * i + 1] == looped[i], "exclusive loop end and intro");
    check(audio.seek_channel(0, 1.5), "seek fractional source frame");
    check(render(audio, 1)[0] == 250, "seek keeps interpolation fraction");
    double position = audio.channel_position(0);
    check(!audio.seek_channel(0, 1000) && audio.channel_position(0) == position,
          "invalid seek leaves playback unchanged");
    check(!audio.seek_channel(0, std::numeric_limits<double>::quiet_NaN()), "reject NaN source position");
    audio.set_group_volume(AudioGroup::Bgm, 50, 100);
    audio.fade_group_pan(AudioGroup::Bgm, 5000, 100);
    render(audio, 31);
    auto mixer = audio.capture_snapshot();
    auto expected_mix = render(audio, 256);
    AudioPreparedChannel prepared;
    prepared.channel = 0;
    prepared.pcm = {100, 200, 300, 400, 500};
    prepared.rate = 44100;
    prepared.channels = 1;
    prepared.options = {true, 1, 4};
    prepared.position_frames = mixer.channels[0].position_frames;
    check(audio.restore_channels({prepared}, mixer.groups), "restore prepared mixer snapshot");
    check(render(audio, 256) == expected_mix, "restored source position and ramps produce identical samples");
    position = audio.channel_position(0);
    auto invalid_groups = mixer.groups;
    invalid_groups[0].pan = std::numeric_limits<double>::infinity();
    check(!audio.restore_channels({prepared}, invalid_groups) && audio.channel_position(0) == position,
          "invalid controls do not mutate playback");
    auto invalid_track = prepared;
    invalid_track.position_frames = 1e30;
    check(!audio.restore_channels({invalid_track}, mixer.groups) && audio.channel_position(0) == position,
          "out-of-source snapshot does not mutate playback");
    check(!audio.restore_channels({prepared, prepared}, mixer.groups) && audio.channel_position(0) == position,
          "duplicate channels do not partially replace playback");
    audio.stop_all();
    audio.set_group_volume(AudioGroup::Bgm, 100);
    audio.set_group_pan(AudioGroup::Bgm, 0);

    // The dispatcher may stage a group pan before starting a voice this slice.
    audio.set_group_pan(AudioGroup::Voice, 0);
    audio.fade_group_pan(AudioGroup::Voice, 10000, 10);
    audio.play_pcm(8, {1000}, 44100, 1, {true});
    check(render(audio, 1)[0] == 1000, "new voice begins at pan ramp start");
    render(audio, 441);
    output = render(audio, 1);
    check(output[0] == 0 && output[1] == 1000, "new voice inherits pending pan ramp");
    audio.stop_all();
    audio.set_group_pan(AudioGroup::Voice, 0);

    check(audio.play_pcm(8, {0, 1000}, 22050, 1), "22050 mono load");
    output = render(audio, 4);
    check(output[0] == 0 && output[2] == 500 && output[4] == 1000 && output[6] == 1000,
          "native-rate interpolation and mono upmix");
    check(!audio.voice_playing(), "voice completes at source-rate duration");
    check(render(audio, 1)[0] == 0, "completed channel is silent");
    check(!audio.play_pcm(12, {1, 2}, 44100, 2), "reject out-of-range channel");
    check(!audio.play_pcm(0, {1}, 44100, 2), "reject partial stereo frame");
    check(!audio.play_pcm(0, {1}, 0, 1), "reject zero sample rate");

    audio.play_pcm(0, {30000}, 44100, 1, {true});
    audio.play_pcm(8, {30000}, 44100, 1, {true});
    check(render(audio, 1)[0] == 32767, "mix saturates instead of wrapping");
    audio.stop_group(AudioGroup::Voice);
    check(audio.channel_playing(0) && !audio.voice_playing(), "voice stop preserves BGM");
    audio.set_group_volume(AudioGroup::Bgm, 50);
    check(std::abs(int(render(audio, 1)[0]) - 1687) <= 1, "native 50 percent is -25 dB");
    audio.set_group_volume(AudioGroup::Bgm, 100);
    audio.set_group_pan(AudioGroup::Bgm, 10000);
    output = render(audio, 1);
    check(output[0] == 0 && output[1] == 30000, "positive DirectSound pan attenuates left");
    audio.set_group_pan(AudioGroup::Bgm, 0);
    audio.fade_group_pan(AudioGroup::Bgm, -10000, 10);
    render(audio, 441);
    output = render(audio, 1);
    check(output[0] == 30000 && output[1] == 0, "pan ramp reaches target");
    audio.set_group_pan(AudioGroup::Bgm, 0);
    audio.fade_out_bgm(10);
    check(!audio.channel_playing(0) && audio.channel_playing(1), "outgoing music uses channel 1");
    audio.play_pcm(0, {1000}, 44100, 1, {true});
    render(audio, 441);
    check(!audio.channel_playing(1) && audio.channel_playing(0), "outgoing fade preserves new BGM");
    audio.stop_all();

    audio.play_pcm(2, {1}, 44100, 1, {true});
    audio.play_pcm(6, {1}, 44100, 1, {true});
    check(audio.free_channel(AudioGroup::Se) == 3, "one-shot SE skips looping slot 2");
    audio.stop_group(AudioGroup::Se);
    check(!audio.channel_playing(2) && audio.channel_playing(6), "SE group stop preserves system effects");
    audio.stop_all();

    std::vector<uint8_t> pcm8(30);
    pcm8[0] = 'X'; pcm8[1] = 'P'; pcm8[2] = 'C'; pcm8[3] = 'M';
    put32(pcm8, 4, 2);
    pcm8[12] = 1; pcm8[14] = 1;  // PCM mono
    put32(pcm8, 16, 22050); put32(pcm8, 20, 22050);
    pcm8[24] = 1; pcm8[26] = 8; pcm8[28] = 0; pcm8[29] = 255;
    std::vector<int16_t> samples;
    uint32_t rate = 0; uint16_t channels = 0;
    check(decode_xpcm_pcm(pcm8.data(), pcm8.size(), &samples, &rate, &channels), "PCM8 decode");
    check(rate == 22050 && channels == 1 && samples[0] == -32768 && samples[1] == 32512,
          "PCM8 unsigned to signed conversion");
    check(!decode_xpcm_pcm(pcm8.data(), 29, &samples, &rate, &channels), "reject truncated payload");

    // Concurrent replacement used to invalidate the callback's vector storage.
    std::thread callback([&audio] { for (int i = 0; i < 1000; ++i) render(audio, 64); });
    for (int i = 0; i < 1000; ++i) {
        check(audio.play_pcm(8, {int16_t(i), int16_t(-i)}, 22050, 1), "concurrent voice replace");
        audio.stop_group(AudioGroup::Voice);
    }
    callback.join();
    audio.stop_all();

    if (argc > 1) {
        SceneAudio scene(argv[1]);
        VoiceArchive voices;
        std::string error;
        check(scene.load_names(&error), error.c_str());
        check(scene.bgm_names().size() > 1 && !scene.se_names().empty(), "real NAMES.DAT tables");
        check(voices.open(std::string(argv[1]) + "/VOICE.DAT", &error), error.c_str());
        scene.set_engine(&audio, &voices);
        MesCommand command;
        command.kind = CmdKind::Words4; command.op = 0x70; command.words[0] = 1;
        scene.dispatch(command);
        check(audio.channel_playing(0) && scene.current_bgm() == 1, "script BGM resolves and decodes");
        command.op = 0x71; command.words[0] = 0;
        scene.dispatch(command);
        check(audio.group_playing(AudioGroup::Se), "SE silent entry decodes");
        command.kind = CmdKind::Str; command.op = 0x4D;
        command.str = "VOICE\\system\\system_asa011";
        scene.dispatch(command);
        check(scene.voice_playing(), "script voice archive entry decodes");
        audio.stop_group(AudioGroup::Voice);
        check(scene.replay_voice() && scene.voice_playing(), "replay remembers archive blob");
        audio.stop_group(AudioGroup::Voice);
        command = MesCommand{};
        command.kind = CmdKind::Words4; command.op = 0x71;
        command.words[0] = 5; command.words[1] = 1;
        scene.dispatch(command);
        command.kind = CmdKind::Param2; command.op = 0x05; command.p1 = 50;
        scene.dispatch(command);
        command.kind = CmdKind::Words4; command.op = 0xAF;
        command.words[0] = uint16_t(-25); command.words[1] = 25;
        command.words[2] = 60; command.words[3] = 0;
        scene.dispatch(command);
        render(audio, 1235);
        auto scene_snapshot = scene.capture_snapshot();
        check(scene_snapshot.bgm_id == 1 && scene_snapshot.bgm_playing &&
              scene_snapshot.bgm_position_frames > 0 && scene_snapshot.looped_se.size() == 1 &&
              scene_snapshot.looped_se[0].id == 5 && scene_snapshot.looped_se[0].position_frames > 0,
              "scene snapshot records BGM and looping SE source positions");
        check(!scene_snapshot.last_voice_name.empty(), "finished voice name remains replayable");
        expected_mix = render(audio, 256);
        scene.reset_state();
        check(scene.restore_snapshot(scene_snapshot, false, &error), error.c_str());
        check(!scene.voice_playing(), "restore keeps finished voice silent by default");
        check(render(audio, 256) == expected_mix, "real tracks restore with identical next mixed samples");
        position = audio.channel_position(0);
        auto bad_snapshot = scene_snapshot;
        bad_snapshot.looped_se[0].position_frames = 1e30;
        check(!scene.restore_snapshot(bad_snapshot, false, &error) && audio.channel_position(0) == position &&
              scene.current_bgm() == 1, "invalid source position preserves live scene audio");
        bad_snapshot = scene_snapshot; bad_snapshot.version = 0;
        check(!scene.restore_snapshot(bad_snapshot, false, &error) && error.find("version") != std::string::npos,
              "old audio snapshot fails explicitly");
        bad_snapshot = scene_snapshot; bad_snapshot.last_voice_name = "VOICE\\missing\\not_present";
        check(!scene.restore_snapshot(bad_snapshot, false, &error) && audio.channel_position(0) == position,
              "missing voice asset preserves live scene audio");
        check(scene.restore_snapshot(scene_snapshot, true, &error) && scene.voice_playing(),
              "explicit restore replay starts the retained voice");
        audio.stop_group(AudioGroup::Voice);
        audio.stop_channel(0);
        auto completed_snapshot = scene.capture_snapshot();
        check(!completed_snapshot.bgm_playing && scene.restore_snapshot(completed_snapshot, false, &error) &&
              !audio.channel_playing(0), "inactive BGM is not restarted by restore");

        SceneAudio headless(argv[1]);
        command = MesCommand{};
        command.kind = CmdKind::Words4; command.op = 0x70;
        command.words[0] = 1; command.words[1] = 2;
        headless.dispatch(command);
        command.op = 0x71; command.words[0] = 5; command.words[1] = 1;
        headless.dispatch(command);
        command.kind = CmdKind::Str; command.op = 0x4D;
        command.str = "VOICE\\system\\system_asa011";
        headless.dispatch(command);
        command.kind = CmdKind::Param2; command.op = 0x05; command.p1 = 75;
        headless.dispatch(command);
        command.kind = CmdKind::Words4; command.op = 0xAF;
        command.words[0] = uint16_t(-30); command.words[1] = 20;
        command.words[2] = 42; command.words[3] = 1;
        headless.dispatch(command);
        auto logical = headless.capture_snapshot();
        check(logical.bgm_id == 1 && logical.bgm_mode == 2 && logical.bgm_playing &&
              logical.looped_se.size() == 1 && !logical.last_voice_name.empty() &&
              logical.bgm_volume == 75 && logical.groups[1].pan == -3000 &&
              logical.groups[1].pan_target == 2000, "headless dispatcher preserves logical audio state");
        check(headless.restore_snapshot(logical, false, &error), error.c_str());
        check(headless.capture_snapshot().last_voice_name == logical.last_voice_name &&
              headless.capture_snapshot().bgm_mode == 2, "headless snapshot survives validation and restore");
        SceneAudio missing_data(std::string(argv[1]) + "/missing-game-data");
        check(!missing_data.restore_snapshot(logical, false, &error) && error.find("NAMES.DAT") != std::string::npos,
              "missing source data has an explanatory restore failure");
        scene.reset_state();
        check(!audio.group_playing(AudioGroup::Bgm) && !scene.voice_playing(), "new-game audio reset");
        size_t decoded_effects = 0;
        for (const auto& file : std::filesystem::directory_iterator(std::string(argv[1]) + "/SE")) {
            if (!file.is_regular_file()) continue;
            std::ifstream input(file.path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            check(decode_xpcm_pcm(bytes.data(), bytes.size(), &samples, &rate, &channels),
                  file.path().string().c_str());
            ++decoded_effects;
        }
        std::printf("real assets: %zu BGM names, %zu SE names, %zu SE files decoded, voice replay OK\n",
                    scene.bgm_names().size(), scene.se_names().size(), decoded_effects);
    }
    std::puts("audio tests passed");
    return 0;
}
