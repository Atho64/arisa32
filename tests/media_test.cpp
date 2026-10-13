#include "dc4/movie.h"
#include "dc4/scene.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
struct Script {
    std::vector<uint8_t> data{1,0,0,0,3,0,0,0,0,0,0xB6,0xAA,0};
    void string(uint8_t op, const char* text) {
        data.push_back(op);
        for (; *text; ++text) data.push_back(uint8_t(*text));
        data.push_back(0);
    }
    void text(const char* text) {
        data.push_back(0x6B);
        for (; *text; ++text) data.push_back(uint8_t(*text - 0x20));
        data.push_back(0);
    }
    void wait(uint8_t ticks) { data.insert(data.end(), {0x15, ticks, 0}); }
};
void audio_test() {
    dc4::AudioEngine audio;
    CHECK(audio.play_pcm(0, std::vector<int16_t>(88200, 12000), 44100, 2, {true,0,0}));
    std::vector<int16_t> output(200);
    audio.render_pcm(output.data(), 100);
    CHECK(output.front() == 12000);
    const double before = audio.channel_position(0);
    CHECK(audio.begin_movie_audio(44100));
    std::vector<float> samples(200, .25f);
    CHECK(audio.queue_movie_audio(samples.data(), 100));
    audio.pause_movie_audio(true);
    audio.render_pcm(output.data(), 100);
    CHECK(output.front() == 0 && audio.movie_audio_time() == 0);
    CHECK(audio.movie_audio_queued() == 100);
    audio.pause_movie_audio(false);
    audio.render_pcm(output.data(), 100);
    CHECK(output.front() > 8100 && output.front() < 8300);
    CHECK(audio.movie_audio_queued() == 0);
    CHECK(audio.channel_position(0) == before);
    const double end = audio.movie_audio_time();
    audio.render_pcm(output.data(), 100); // underflow is silence, never old music
    CHECK(output.front() == 0 && audio.movie_audio_time() == end);
    audio.end_movie_audio();
    audio.render_pcm(output.data(), 100);
    CHECK(output.front() == 12000 && audio.channel_position(0) > before);
    audio.set_master_volume(50);
    audio.set_user_volume(dc4::AudioGroup::Bgm, 50);
    audio.render_pcm(output.data(), 100);
    CHECK(output.front() == 3000);
    audio.set_group_volume(dc4::AudioGroup::Bgm, 100);
    audio.render_pcm(output.data(), 100);
    CHECK(output.front() == 3000); // script volume cannot override user gain
    CHECK(audio.seek_channel_fraction(0, .5));
    CHECK(audio.channel_fraction(0) == .5 && audio.channel_duration_seconds(0) == 1);
    CHECK(!audio.seek_channel_fraction(0, -1));
    CHECK(!audio.seek_channel_fraction(0, 2));
    audio.set_master_volume(100); audio.set_user_volume(dc4::AudioGroup::Bgm, 100);
    audio.set_duck_music_for_voice(true);
    CHECK(audio.play_pcm(8, std::vector<int16_t>(44100, 0), 44100, 2));
    std::vector<int16_t> long_output(8820);
    audio.render_pcm(long_output.data(), 4410);
    CHECK(long_output.back() >= 2900 && long_output.back() <= 3100);
    audio.stop_group(dc4::AudioGroup::Voice);
    long_output.resize(26460);
    audio.render_pcm(long_output.data(), 13230);
    CHECK(long_output.back() == 12000);
    CHECK(audio.begin_movie_audio(48000));
    std::vector<float> over(44100 * 8 + 2);
    CHECK(!audio.queue_movie_audio(over.data(), over.size() / 2));
    audio.end_movie_audio();
}
void scene_test(const fs::path& root) {
    Script script;
    script.text("BEFORE"); script.string(0x52, "fixture"); script.wait(3); script.text("AFTER");
    script.data.push_back(0);
    write(root / "MES/movie.mes", script.data);
    dc4scene::SceneRunner scene(root.string());
    dc4::AudioEngine audio;
    scene.set_audio(&audio, nullptr);
    scene.set_animation_enabled(true);
    scene.set_effects_enabled(false); // disabling visual effects preserves waits
    CHECK(scene.load_script((root / "MES/movie.mes").string()));
    CHECK(scene.advance_to_next_text() && scene.current_line() == "BEFORE");
    CHECK(audio.play_pcm(0, std::vector<int16_t>(200, 100), 44100, 2, {true,0,0}));
    CHECK(!scene.advance_to_next_text());
    CHECK(scene.pending_movie() == "fixture" && !scene.scene_ended());
    std::vector<uint8_t> snapshot;
    std::string snapshot_error;
    CHECK(!scene.capture_state(&snapshot, &snapshot_error));
    CHECK(!audio.channel_playing(0));
    CHECK(!scene.advance_to_next_text() && scene.pending_movie() == "fixture");
    scene.update(100000);
    CHECK(scene.pending_movie() == "fixture");
    scene.finish_movie();
    CHECK(!scene.advance_to_next_text() && scene.waiting_for_time());
    CHECK(!scene.capture_state(&snapshot, &snapshot_error));
    scene.update(599);
    CHECK(!scene.script_ready() && !scene.advance_to_next_text());
    scene.update(1);
    CHECK(scene.script_ready());
    CHECK(scene.advance_to_next_text() && scene.current_line() == "AFTER");
    CHECK(scene.last_error().empty());
    CHECK(!scene.advance_to_next_text() && scene.scene_ended());
    CHECK(scene.load_script((root / "MES/movie.mes").string()));
    CHECK(scene.advance_to_next_text());
    CHECK(!scene.advance_to_next_text()); scene.finish_movie();
    CHECK(!scene.advance_to_next_text()); scene.finish_script_wait();
    CHECK(scene.advance_to_next_text() && scene.current_line() == "AFTER");
}
void movie_test(const fs::path& root) {
    fs::create_directories(root / "MOVIE");
    const auto fixture = fs::path(__FILE__).parent_path() / "fixtures/media-test.mpg";
    fs::copy_file(fixture, root / "MOVIE/fixture.mpg", fs::copy_options::overwrite_existing);
    dc4::MoviePlayer movie;
    std::string error;
    CHECK(movie.open(root.string(), "FIXTURE", nullptr, &error));
    CHECK(movie.active() && movie.width() == 64 && movie.height() == 48);
    std::vector<uint8_t> frame;
    movie.draw_rgba(&frame, 128, 72);
    CHECK(frame.size() == 128 * 72 * 4 && frame[3] == 255);
    const auto first = frame;
    movie.set_paused(true); movie.update(1000);
    CHECK(movie.time() == 0);
    movie.set_paused(false);
    for (int i = 0; i < 100 && movie.active(); ++i) movie.update(20);
    CHECK(movie.finished() && movie.decoded_frames() >= 10 && movie.error().empty());
    CHECK(movie.decoded_audio_frames() > 10000 && movie.audio_peak() > .01f);
    movie.draw_rgba(&frame, 128, 72);
    CHECK(frame != first);
    CHECK(movie.open(root.string(), "fixture.mpg", nullptr, &error));
    movie.update(100); movie.close();
    CHECK(movie.finished() && !movie.active());
    CHECK(!movie.open(root.string(), "../fixture", nullptr, &error));
    CHECK(!movie.open(root.string(), "C:\\movie", nullptr, &error));
    CHECK(!movie.open(root.string(), "missing", nullptr, &error));
    write(root / "MOVIE/bad.mpg", std::vector<uint8_t>(65536, 255));
    CHECK(!movie.open(root.string(), "bad", nullptr, &error));
    std::ifstream file(fixture, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    for (size_t n : {size_t(0),size_t(8),size_t(64),size_t(2048),bytes.size()/2}) {
        write(root / "MOVIE/truncated.mpg", {bytes.begin(), bytes.begin()+n});
        if (movie.open(root.string(), "truncated", nullptr, &error)) {
            for (int i=0; i<200 && movie.active(); ++i) movie.update(25);
            CHECK(movie.finished());
        }
    }
}
void supplied_test(const std::string& root, bool full) {
    for (const auto& file : fs::directory_iterator(fs::path(root) / "MOVIE")) {
        if (file.path().extension() != ".mpg") continue;
        dc4::MoviePlayer movie;
        std::string error;
        CHECK(movie.open(root, file.path().filename().string(), nullptr, &error));
        if (!movie.active()) { std::cerr << error << '\n'; continue; }
        const int limit = full ? 30000 : 80;
        for (int i = 0; i < limit && movie.active(); ++i) movie.update(40);
        CHECK(movie.decoded_frames() > 20 && movie.error().empty());
        CHECK(movie.decoded_audio_frames() > 1000 && movie.audio_peak() > .0001f);
        if (full) CHECK(movie.finished());
        std::cout << file.path().filename().string() << " " << movie.width() << 'x' << movie.height()
                  << " duration=" << movie.duration() << " decoded=" << movie.decoded_frames()
                  << " audio_frames=" << movie.decoded_audio_frames() << " peak=" << movie.audio_peak() << '\n';
    }
    dc4scene::SceneRunner scene(root);
    scene.set_animation_enabled(true);
    CHECK(scene.load_script(root + "/MES/ed_asa.mes"));
    int waits = 0;
    while (waits < 11 && !scene.scene_ended()) {
        scene.advance_to_next_text();
        if (scene.waiting_for_time()) { ++waits; scene.draw(-1); scene.update(20000); }
        else break;
    }
    CHECK(waits >= 9);
    CHECK(scene.load_script(root + "/MES/med.mes"));
    for (int i=0; i<10 && !scene.credits_active(); ++i) {
        scene.advance_to_next_text(); scene.update(200);
    }
    CHECK(scene.credits_active());
    scene.draw(-1);
    scene.update(10000); scene.draw(-1);
    CHECK(scene.last_error().empty());
    scene.update(125000);
    CHECK(!scene.credits_active() && scene.script_ready());
}
}
int main(int argc, char** argv) {
    auto root = fs::temp_directory_path() / ("dc4-media-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    audio_test(); scene_test(root); movie_test(root);
    if (argc > 1) supplied_test(argv[1], argc > 2);
    fs::remove_all(root);
    if (!failures) std::cout << "media tests passed\n";
    return failures ? 1 : 0;
}
