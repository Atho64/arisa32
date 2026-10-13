#include "dc4/movie.h"
#include "dc4/audio.h"
#include "dc4/resource.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace dc4 {
struct MoviePlayer::Impl {
    plm_t* decoder = nullptr;
    AudioEngine* audio = nullptr;
    std::vector<uint8_t> pixels;
    std::string failure;
    int width = 0, height = 0;
    double clock = 0, duration = 0, silent_tail = 0;
    uint64_t frames = 0;
    uint64_t audio_frames = 0;
    float audio_peak = 0;
    bool done = true, paused = false, audio_clock = false;
    static void video(plm_t*, plm_frame_t* frame, void* opaque) {
        auto& self = *static_cast<Impl*>(opaque);
        if (frame->width != unsigned(self.width) || frame->height != unsigned(self.height)) {
            self.failure = "movie dimensions changed during playback";
            return;
        }
        plm_frame_to_rgba(frame, self.pixels.data(), self.width * 4);
        ++self.frames;
    }
    static void sound(plm_t*, plm_samples_t* samples, void* opaque) {
        auto& self = *static_cast<Impl*>(opaque);
        self.audio_frames += samples->count;
        for (size_t i = 0; i < samples->count * 2; ++i)
            self.audio_peak = std::max(self.audio_peak, std::abs(samples->interleaved[i]));
        if (self.audio && self.audio_clock && !self.audio->queue_movie_audio(samples->interleaved, samples->count))
            self.failure = "movie audio queue exceeded its limit";
    }
};

MoviePlayer::MoviePlayer() : impl_(new Impl) {}
MoviePlayer::~MoviePlayer() { close(); }
void MoviePlayer::close() {
    if (impl_->audio) impl_->audio->end_movie_audio();
    if (impl_->decoder) plm_destroy(impl_->decoder);
    impl_->decoder = nullptr;
    impl_->audio = nullptr;
    impl_->pixels.clear();
    impl_->done = true;
}
bool MoviePlayer::open(const std::string& data_dir, const std::string& name,
                       AudioEngine* audio, std::string* error) {
    close();
    *impl_ = Impl{};
    auto fail = [&](const std::string& reason) {
        impl_->failure = reason;
        close();
        if (error) *error = reason;
        return false;
    };
    // Names are stems, or relative MOVIE names with their .mpg extension.
    std::string relative = name;
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (relative.empty() || relative.find('/') != std::string::npos ||
        relative.find(':') != std::string::npos || relative.find("..") != std::string::npos ||
        relative.find('\0') != std::string::npos || relative.size() > 200)
        return fail("invalid movie resource name");
    std::string extension = std::filesystem::path(relative).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension.empty()) relative += ".mpg";
    else if (extension != ".mpg") return fail("movie resource must be an MPEG .mpg file");
    const auto path = resolve_asset(data_dir + "/MOVIE/" + relative);
    std::error_code ec;
    const auto length = std::filesystem::file_size(path, ec);
    if (ec || length < 32 || length > 1024ull * 1024 * 1024) return fail("movie is missing, empty, or too large");
    // Validate the sequence header before the decoder allocates image planes.
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> header(size_t(std::min<uint64_t>(length, 65536)));
    if (!input.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size())))
        return fail("cannot read movie header");
    bool sequence = false;
    for (size_t i = 0; i + 8 < header.size(); ++i) {
        if (header[i] || header[i + 1] || header[i + 2] != 1) continue;
        if (header[i + 3] == 0xB5 && (header[i + 4] >> 4) == 1)
            return fail("MPEG-2 movies are unsupported; expected MPEG-1");
        if (header[i + 3] != 0xB3) continue;
        const int w = (header[i + 4] << 4) | (header[i + 5] >> 4);
        const int h = ((header[i + 5] & 15) << 8) | header[i + 6];
        const int fps = header[i + 7] & 15;
        if (w < 16 || h < 16 || w > 1920 || h > 1088 || !fps || fps > 8)
            return fail("unsupported movie dimensions or frame rate");
        sequence = true;
    }
    if (!sequence) return fail("movie lacks a valid MPEG-1 sequence header");
    impl_->decoder = plm_create_with_filename(path.c_str());
    if (!impl_->decoder || !plm_has_headers(impl_->decoder)) return fail("cannot decode MPEG stream headers");
    impl_->width = plm_get_width(impl_->decoder);
    impl_->height = plm_get_height(impl_->decoder);
    if (!impl_->width || !impl_->height || impl_->width > 1920 || impl_->height > 1088)
        return fail("invalid decoded movie dimensions");
    impl_->pixels.assign(size_t(impl_->width) * impl_->height * 4, 255);
    impl_->duration = plm_get_duration(impl_->decoder);
    if (!std::isfinite(impl_->duration) || impl_->duration < 0 || impl_->duration > 4 * 3600)
        return fail("invalid movie duration");
    plm_set_loop(impl_->decoder, FALSE);
    plm_set_video_decode_callback(impl_->decoder, Impl::video, impl_.get());
    plm_set_audio_decode_callback(impl_->decoder, Impl::sound, impl_.get());
    plm_set_audio_lead_time(impl_->decoder, 0.3);
    impl_->audio_clock = audio && audio->device_ready() && plm_get_num_audio_streams(impl_->decoder) > 0;
    if (audio) {
        const int rate = plm_get_samplerate(impl_->decoder);
        if (!audio->begin_movie_audio(rate > 0 ? uint32_t(rate) : AudioEngine::kOutputRate))
            return fail("cannot initialize movie audio stream");
        impl_->audio = audio;
    }
    impl_->done = false;
    plm_decode(impl_->decoder, 0.000001); // first video frame plus 300 ms audio
    if (!impl_->failure.empty()) return fail(impl_->failure);
    if (!impl_->frames) return fail("movie contains no decodable video frame");
    if (error) error->clear();
    return true;
}
void MoviePlayer::update(uint32_t elapsed_ms) {
    if (!active() || impl_->paused) return;
    // Bounded catch-up: a resumed application must not decode minutes in one UI tick.
    const double wall = std::min(elapsed_ms, uint32_t(250)) / 1000.0;
    double target = impl_->clock + wall;
    if (impl_->audio_clock) {
        target = impl_->audio->movie_audio_time();
        // Trailers can outlast their audio track. Once the decoder reaches the
        // audio end, permit a short video tail using the host clock.
        if (!impl_->audio->movie_audio_queued()) {
            impl_->silent_tail += wall;
            target += impl_->silent_tail;
        } else impl_->silent_tail = 0;
        target = std::max(target, impl_->clock);
    }
    double delta = std::clamp(target - impl_->clock, 0.0, 0.5);
    impl_->clock += delta;
    plm_decode(impl_->decoder, delta);
    if (!impl_->failure.empty() ||
        (plm_has_ended(impl_->decoder) && (!impl_->audio_clock || !impl_->audio->movie_audio_queued())) ||
        (impl_->duration > 0 && impl_->clock > impl_->duration + 2)) {
        impl_->done = true;
        if (impl_->audio) impl_->audio->end_movie_audio();
    }
}
void MoviePlayer::set_paused(bool paused) {
    impl_->paused = paused;
    if (impl_->audio) impl_->audio->pause_movie_audio(paused);
}
void MoviePlayer::draw_rgba(std::vector<uint8_t>* rgba, int width, int height) const {
    if (!rgba || width <= 0 || height <= 0 || width > 8192 || height > 8192) return;
    rgba->assign(size_t(width) * height * 4, 0);
    for (size_t i = 3; i < rgba->size(); i += 4) (*rgba)[i] = 255;
    if (impl_->pixels.empty()) return;
    if (width == impl_->width && height == impl_->height) { *rgba = impl_->pixels; return; }
    const double scale = std::min(double(width) / impl_->width, double(height) / impl_->height);
    const int w = std::max(1, int(impl_->width * scale)), h = std::max(1, int(impl_->height * scale));
    const int x0 = (width - w) / 2, y0 = (height - h) / 2;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const size_t src = (size_t(y * impl_->height / h) * impl_->width + x * impl_->width / w) * 4;
        const size_t dst = (size_t(y + y0) * width + x + x0) * 4;
        std::copy_n(impl_->pixels.data() + src, 4, rgba->data() + dst);
    }
}
bool MoviePlayer::active() const { return impl_->decoder && !impl_->done; }
bool MoviePlayer::finished() const { return impl_->done; }
const std::string& MoviePlayer::error() const { return impl_->failure; }
int MoviePlayer::width() const { return impl_->width; }
int MoviePlayer::height() const { return impl_->height; }
double MoviePlayer::time() const { return impl_->clock; }
double MoviePlayer::duration() const { return impl_->duration; }
uint64_t MoviePlayer::decoded_frames() const { return impl_->frames; }
uint64_t MoviePlayer::decoded_audio_frames() const { return impl_->audio_frames; }
float MoviePlayer::audio_peak() const { return impl_->audio_peak; }
} // namespace dc4
