// [arcade] The media player (2026-11-10): a folder wrestler's entrance theme and Titantron video, straight from common
// files, with no conversion to the game's own formats (theme.bin / video.bin: an N64 song, 64 x 64 at 7.5 pictures a
// second). The decoders are behind arcade_media.h; this file is everything else.
//
// The user's rules:
//   1. A theme file: the theme plays and the video is silent. No theme file and a video with sound: the video's sound
//      plays. No theme and a silent video: the game's own song, as before.
//   2. A video loops for as long as the entrance lasts. (So does the theme, as the game's songs do.)
//   3. Old converted files and everything stock play exactly as before: nothing here runs unless the entering
//      wrestler's folder has a common-format file.
//
// How it runs. The mod calls recomp_arcade_entrance_media(id, variant, on) when the entering wrestler's song starts and
// when it stops. One thread decodes: it keeps half a second of sound and a few pictures ahead, and goes back to the
// start of the file at its end. The sound and the picture of a video are read through two separate file handles, each
// keeping only its own packets, so a slow picture never holds the sound up. The sound is mixed in where the game's
// sound leaves for the speakers (main.cpp queue_samples). Pictures are due by the game's own clock, so the tests'
// fast-forward and a paused game keep the video in step; the renderer asks for the picture due now each time it draws.
//
// WCW_MEDIA=0 turns all of this off. For tests: WCW_MEDIA_HW=0 decodes on the CPU only, WCW_MEDIA_VOLUME=0..200 is the
// level of the theme against the game's sound (100 unless set), WCW_MEDIA_MAXW the widest picture kept (1024).
#include "arcade_media.h"
#include "arcade_host.h"   // (arcade::shown: paths for the log, privacy 2026-11-13)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace arcade_media {
namespace {

constexpr int QUEUE_PICTURES = 4;           // decoded ahead (a 1024 x 576 picture is 2.4 MB)
// Seconds of sound decoded ahead. 3, not 0.5 (2026-11-11): one thread decodes both the sound and the pictures, and a picture step
// can hold it up -- moving to the CPU decoder mid-video decodes up to the resume point in one go, about half a second -- and with
// half a second of sound in hand the theme fell silent for about 0.36 s (QA, "ran dry 19 times"). Sound costs next to nothing to
// decode (hundreds of times real time) and 3 s of it is under 1 MB, so it's simply kept well ahead of any such step.
constexpr double SOUND_AHEAD = 3.0;
constexpr double SOUND_LOW = 1.0;           // under this much in hand, the sound is filled before any more pictures
constexpr double SYNC_SLACK = 0.15;         // a video's own sound may be this far from its picture before it's pulled back
constexpr double LATE_SLACK = 0.25;         // the decoder is this far behind the clock: it passes pictures over to catch up

// The sound is at the file's rate, the speakers at the game's: a windowed-sinc resampler, 16 taps. (A straight line
// between samples, as the commentary's voice uses, is fine for speech; music taken from 44.1 or 48 kHz down to the
// game's 32 kHz needs the high end filtered off, or it folds back as a hiss.)
struct Resampler {
    static constexpr int TAPS = 16, PHASES = 128;
    float table[PHASES + 1][TAPS];
    double built_for = -1;
    void build(double step) {       // step: file samples per speaker sample
        if (step == built_for) return;
        built_for = step;
        const double cut = step > 1.0 ? 0.95 / step : 0.95;
        for (int p = 0; p <= PHASES; p++) {
            double sum = 0, h[TAPS];
            for (int i = 0; i < TAPS; i++) {
                const double x = double(i - (TAPS / 2 - 1)) - double(p) / PHASES;
                const double a = 3.14159265358979323846 * x * cut;
                const double sinc = std::fabs(a) < 1e-9 ? 1.0 : std::sin(a) / a;
                const double w = x / (TAPS / 2);
                const double window = std::fabs(w) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos(3.14159265358979323846 * w) + 0.08 * std::cos(2 * 3.14159265358979323846 * w);
                h[i] = sinc * window; sum += h[i];
            }
            for (int i = 0; i < TAPS; i++) table[p][i] = float(h[i] / sum);
        }
    }
};

}  // namespace

struct Player::State {
    double (*clock)() = nullptr;
    std::thread thread;
    mutable std::mutex lock;
    std::condition_variable wake;
    bool quit = false;

    // What to play next (start) and which request is the current one. Anything decoded for an older one is thrown away.
    struct Request { std::filesystem::path theme, video; bool video_sound = false; VideoOptions options; };
    Request request; bool request_new = false; uint64_t generation = 0;

    // The sound: decoded frames at the file's rate, waiting for the mixer. pcm_base is the place in the whole
    // (looped) stream of pcm's first frame; play_at is where the mixer is, a fraction of a frame included.
    std::vector<int16_t> pcm; uint64_t pcm_base = 0; double play_at = 0; int pcm_rate = 0;
    bool sound_on = false, sound_follows_video = false, sound_ending = false;
    float gain = 0; double fade_seconds = 0;
    std::chrono::steady_clock::time_point ending_since;
    Resampler resampler;

    // The pictures: decoded ahead, each due at `at` seconds from the clock's start.
    std::deque<std::shared_ptr<Picture>> queue; std::shared_ptr<Picture> current;
    bool video_on = false, clock_running = false; double clock_start = 0; uint64_t serial = 0;

    PlayerStats stats;

    // The thread's own (no lock needed).
    std::unique_ptr<MediaVideo> video; std::unique_ptr<MediaAudio> sound;
    double loop_length = 0; bool loop_measured = false;     // the video's length: as the file states it, then as measured at its first end
    uint64_t video_loop = 0, sound_loop = 0, sound_made = 0; double last_at = 0, last_lasts = 0;
    int late_on_hardware = 0;
    std::vector<int16_t> chunk;

    void run();
    void open(const Request& r, uint64_t gen);
    bool more_sound(uint64_t gen);
    bool more_picture(uint64_t gen);
};

Player::Player(double (*clock)()) : s_(std::make_unique<State>()) {
    s_->clock = clock;
    s_->thread = std::thread([this] { s_->run(); });
}

Player::~Player() {
    { std::lock_guard<std::mutex> hold(s_->lock); s_->quit = true; }
    s_->wake.notify_all();
    if (s_->thread.joinable()) s_->thread.join();
}

int Player::start(const std::filesystem::path& theme, const std::filesystem::path& video, bool video_has_sound, const VideoOptions& options) {
    std::lock_guard<std::mutex> hold(s_->lock);
    State& s = *s_;
    s.request = { theme, video, false, options };
    // Rule 1: a theme file silences the video; without one the video's own sound plays, if it has any.
    s.request.video_sound = theme.empty() && !video.empty() && video_has_sound;
    s.request_new = true; s.generation++;
    // Whatever was on stops now. The picture counts as playing from this moment (the renderer leaves the screen dark
    // until the first one is decoded, a few hundredths of a second).
    s.pcm.clear(); s.pcm_base = 0; s.play_at = 0; s.sound_on = false; s.sound_ending = false; s.gain = 0;
    s.queue.clear(); s.current.reset(); s.clock_running = false;
    s.video_on = !video.empty();
    s.stats = PlayerStats{};
    s.wake.notify_all();
    return (theme.empty() ? 0 : PLAYS_THEME) | (video.empty() ? 0 : PLAYS_VIDEO) | (s.request.video_sound ? PLAYS_VIDEO_SOUND : 0);
}

void Player::stop(double fade_seconds) {
    std::lock_guard<std::mutex> hold(s_->lock);
    State& s = *s_;
    s.video_on = false; s.queue.clear(); s.current.reset(); s.clock_running = false;
    s.request_new = false;                      // (a start not yet taken up: forgotten)
    s.generation++;
    if (s.sound_on && !s.sound_ending) { s.sound_ending = true; s.fade_seconds = std::max(0.03, fade_seconds); s.ending_since = std::chrono::steady_clock::now(); }
    s.wake.notify_all();
}

bool Player::video_playing() const {
    std::lock_guard<std::mutex> hold(s_->lock);
    return s_->video_on;
}

PlayerStats Player::stats() const {
    std::lock_guard<std::mutex> hold(s_->lock);
    return s_->stats;
}

std::shared_ptr<const Picture> Player::picture_now() {
    std::lock_guard<std::mutex> hold(s_->lock);
    State& s = *s_;
    if (!s.video_on || !s.clock_running) return nullptr;
    const double now = s.clock() - s.clock_start;
    bool took = false;
    // The newest picture that's due. Any before it that were never shown are passed over.
    while (!s.queue.empty() && s.queue.front()->at <= now) {
        if (took) s.stats.pictures_late++;
        s.current = std::move(s.queue.front()); s.queue.pop_front(); took = true;
    }
    if (took) { s.stats.pictures_shown++; s.wake.notify_all(); }
    return s.current;
}

void Player::mix(int16_t* buf, size_t frames, uint32_t rate, float volume) {
    std::lock_guard<std::mutex> hold(s_->lock);
    State& s = *s_;
    if (!s.sound_on || !s.pcm_rate || !rate) return;
    // A video's own sound starts with its first picture, and stays with it: the picture goes by the game's clock, the
    // sound by how much the speakers have taken. If they come apart (a stall, a slow device), the sound is moved.
    if (s.sound_follows_video && !s.sound_ending) {
        if (!s.clock_running) return;
        const double apart = s.play_at / s.pcm_rate - (s.clock() - s.clock_start);
        if (apart > SYNC_SLACK) { s.stats.sound_resyncs++; return; }                          // (ahead: it waits)
        if (apart < -SYNC_SLACK) { s.play_at -= apart * s.pcm_rate; s.stats.sound_resyncs++; }  // (behind: it jumps on)
    }
    const double step = double(s.pcm_rate) / rate;
    s.resampler.build(step);
    const uint64_t have_end = s.pcm_base + s.pcm.size() / 2;
    const float rise = 1.0f / (0.01f * rate), fall = float(1.0 / (std::max(0.03, s.fade_seconds) * rate));
    const int half = Resampler::TAPS / 2;
    size_t i = 0;
    for (; i < frames; i++) {
        const uint64_t k = uint64_t(s.play_at);
        if (k + half >= have_end) { s.stats.sound_starved++; break; }      // (the decoder hasn't got this far: quiet until it has)
        if (s.sound_ending) { s.gain -= fall; if (s.gain <= 0) { s.gain = 0; break; } }
        else if (s.gain < 1.0f) s.gain = std::min(1.0f, s.gain + rise);
        const float frac = float(s.play_at - double(k));
        const int phase = int(frac * Resampler::PHASES);
        const float between = frac * Resampler::PHASES - phase;
        const float* a = s.resampler.table[phase], * b = s.resampler.table[phase + 1];
        float l = 0, r = 0;
        for (int t = 0; t < Resampler::TAPS; t++) {
            const int64_t at = int64_t(k) - (half - 1) + t - int64_t(s.pcm_base);
            if (at < 0) continue;                                           // (before the very start: silence)
            const float w = a[t] + (b[t] - a[t]) * between;
            l += w * s.pcm[size_t(at) * 2]; r += w * s.pcm[size_t(at) * 2 + 1];
        }
        const float g = s.gain * volume;
        buf[i * 2] = int16_t(std::clamp(buf[i * 2] + l * g, -32768.0f, 32767.0f));
        buf[i * 2 + 1] = int16_t(std::clamp(buf[i * 2 + 1] + r * g, -32768.0f, 32767.0f));
        s.play_at += step;
    }
    s.stats.sound_frames_mixed += i;
    if (s.sound_ending && s.gain <= 0) { s.sound_on = false; s.pcm.clear(); }
    // What's been played is let go, but for the few frames the resampler still reaches back to.
    const uint64_t keep_from = uint64_t(s.play_at) > uint64_t(half) ? uint64_t(s.play_at) - half : 0;
    if (s.sound_on && keep_from > s.pcm_base + 4096) {
        const size_t drop = std::min(size_t(keep_from - s.pcm_base), s.pcm.size() / 2);
        s.pcm.erase(s.pcm.begin(), s.pcm.begin() + ptrdiff_t(drop * 2));
        s.pcm_base += drop;
    }
    s.wake.notify_all();
}

// ---- the thread -------------------------------------------------------------------------------------------------------
void Player::State::open(const Request& r, uint64_t gen) {
    video.reset(); sound.reset();
    loop_length = 0; loop_measured = false; video_loop = 0; sound_loop = 0; sound_made = 0; last_at = 0; last_lasts = 0;
    late_on_hardware = 0;
    std::string why;
    bool follows = false;
    if (!r.video.empty()) {
        video = open_video(r.video, r.options, why);
        if (!video) { fprintf(stderr, "[media] %s %s: no native video\n", arcade::shown(r.video).c_str(), why.c_str()); }
        else {
            const VideoInfo& v = video->info();
            loop_length = v.length;
            fprintf(stderr, "[media] video %s: %s %dx%d, %.2f s, decoded by %s\n", arcade::shown(r.video).c_str(), v.codec.c_str(), v.width, v.height, v.length, v.decoder.c_str());
        }
    }
    if (!r.theme.empty()) {
        sound = open_theme(r.theme, why);
        if (!sound) fprintf(stderr, "[media] %s %s: no native theme\n", arcade::shown(r.theme).c_str(), why.c_str());
        else fprintf(stderr, "[media] theme %s: %s, %d Hz\n", arcade::shown(r.theme).c_str(), sound->decoder(), sound->rate());
    } else if (r.video_sound && video) {
        sound = open_video_sound(r.video, why);
        follows = sound != nullptr;
        if (!sound) fprintf(stderr, "[media] %s: its sound %s: the video plays silent\n", arcade::shown(r.video).c_str(), why.c_str());
        else fprintf(stderr, "[media] no theme file: the video's own sound plays (%s, %d Hz)\n", sound->decoder(), sound->rate());
    }
    fflush(stderr);
    std::lock_guard<std::mutex> hold(lock);
    if (gen != generation) { video.reset(); sound.reset(); return; }
    if (!video) video_on = false;
    stats.hardware = video && video->info().hardware;
    pcm.clear(); pcm_base = 0; play_at = 0; gain = 0; sound_ending = false;
    pcm_rate = sound ? sound->rate() : 0;
    sound_follows_video = follows;
    sound_on = sound != nullptr;
}

// Some more sound decoded and handed to the mixer. A theme starts again the moment it ends. A video's own sound keeps
// to the video's loop: each time round it starts where the picture does (cut there if the sound is longer, quiet up to
// there if it's shorter), so the two can't drift apart over many loops.
bool Player::State::more_sound(uint64_t gen) {
    const size_t want = size_t(sound->rate() / 10);
    chunk.resize(want * 2);
    size_t got = 0;
    bool follows; { std::lock_guard<std::mutex> hold(lock); follows = sound_follows_video; }
    if (follows && loop_length > 0) {
        const uint64_t loop_end = uint64_t(double(sound_loop + 1) * loop_length * sound->rate() + 0.5);
        const size_t room = loop_end > sound_made ? size_t(std::min<uint64_t>(want, loop_end - sound_made)) : 0;
        if (room) {
            got = sound->read(chunk.data(), room);
            if (got < room) { std::fill(chunk.begin() + ptrdiff_t(got * 2), chunk.begin() + ptrdiff_t(room * 2), int16_t(0)); got = room; }   // (the sound ended before the picture)
        }
        if (sound_made + got >= loop_end) { sound->rewind(); sound_loop++; }
    } else {
        got = sound->read(chunk.data(), want);
        if (got < want) {
            if (!sound->rewind()) { if (!got) return false; }
            else sound_loop++;
            if (!got && sound_loop > 1 && sound_made == 0) return false;     // (a file with nothing in it)
        }
    }
    sound_made += got;
    std::lock_guard<std::mutex> hold(lock);
    if (gen != generation) return true;
    pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + ptrdiff_t(got * 2));
    stats.sound_loops = sound_loop;
    return true;
}

// One more picture decoded and queued. At the end of the file: back to the start (rule 2), the next round's pictures
// due one length of the video later.
bool Player::State::more_picture(uint64_t gen) {
    // Behind the clock (a file this PC can't decode at its own rate, or the tests' fast-forward): pass over what's
    // already too late. More than a whole round behind: straight to the round the clock is in.
    {
        double now = -1;
        { std::lock_guard<std::mutex> hold(lock); if (clock_running) now = clock() - clock_start; }
        // (the clock is past the end before the decoder got there -- fast-forward: the length the file states will do)
        if (now >= 0 && !loop_measured && loop_length > 0 && now >= loop_length) loop_measured = true;
        if (now >= 0 && loop_measured && loop_length > 0) {
            const uint64_t round = uint64_t(now / loop_length);
            if (round > video_loop) {
                if (!video->rewind()) return false;
                video_loop = round; last_at = 0;
                std::lock_guard<std::mutex> hold(lock);
                if (gen == generation) stats.video_loops = video_loop;
            }
            now -= double(video_loop) * loop_length;
        }
        if (now - last_at > LATE_SLACK) {
            // Late twice while on the hardware decoder: it can't keep time on this PC right now (measured: reading
            // each picture back from a graphics card that's busy with something else fell under real time for 1080p60,
            // where the CPU did 4x to 12x). The CPU takes the video from here.
            if (video->info().hardware && ++late_on_hardware >= 2) {
                video->use_cpu();
                std::lock_guard<std::mutex> hold(lock);
                if (gen == generation) stats.hardware = false;
            }
            video->skip_to(now);
        }
    }
    auto p = std::make_shared<Picture>();
    if (!video->next(*p)) {
        if (!video->why().empty() && last_lasts == 0) { fprintf(stderr, "[media] the video %s\n", video->why().c_str()); fflush(stderr); return false; }
        // The end. Its length, measured: the last picture's time and how long it lasts.
        if (!loop_measured && last_at + last_lasts > 0) { loop_length = last_at + last_lasts; loop_measured = true; }
        if (!loop_measured || !video->rewind()) { fprintf(stderr, "[media] the video can't go back to its start: %s\n", video->why().c_str()); fflush(stderr); return false; }
        video_loop++; last_at = 0;
        std::lock_guard<std::mutex> hold(lock);
        if (gen == generation) stats.video_loops = video_loop;
        return true;
    }
    last_at = p->at; last_lasts = p->lasts;
    p->at += double(video_loop) * loop_length * (loop_measured ? 1.0 : 0.0);
    std::lock_guard<std::mutex> hold(lock);
    if (gen != generation) return true;
    // (The decoder can also move to the CPU by itself, on a decode error at the hardware's end of a cut file: the log's last
    //  line says which one decoded. 2026-11-11, QA case 10b said "hardware decoder" after such a move.)
    stats.hardware = video->info().hardware;
    p->serial = ++serial;
    stats.pictures_decoded++;
    if (!clock_running) { clock_running = true; clock_start = clock(); }    // (the video's time starts with its first picture)
    queue.push_back(std::move(p));
    return true;
}

void Player::State::run() {
    std::unique_lock<std::mutex> hold(lock);
    while (!quit) {
        if (request_new) {
            const Request r = request; const uint64_t gen = generation; request_new = false;
            hold.unlock(); open(r, gen); hold.lock();
            continue;
        }
        // The entrance is over: the picture's decoder goes now, the sound's once it has faded (or after three seconds,
        // if nothing's taking the sound: the tests' fast-forward has no speakers).
        if (!video_on && video) { hold.unlock(); video.reset(); hold.lock(); continue; }
        if (sound && sound_ending && (!sound_on || std::chrono::steady_clock::now() - ending_since > std::chrono::seconds(3))) {
            sound_on = false; pcm.clear();
            hold.unlock(); sound.reset(); hold.lock();
            continue;
        }
        const uint64_t gen = generation;
        const double in_hand = double(pcm_base + pcm.size() / 2) - play_at;
        const bool need_sound = sound && sound_on && !sound_ending && in_hand < SOUND_AHEAD * pcm_rate;
        const bool sound_low = need_sound && in_hand < SOUND_LOW * pcm_rate;
        const bool need_picture = video && video_on && int(queue.size()) < QUEUE_PICTURES;
        if (!need_sound && !need_picture) { wake.wait_for(hold, std::chrono::milliseconds(5)); continue; }
        hold.unlock();
        // The sound first: it's cheap, and a gap in it is heard at once. While it's low (the start, or after a long picture
        // step) only the sound is decoded, a tenth of a second a turn, until there's a second of it in hand.
        bool sound_ok = true, picture_ok = true;
        if (need_sound) sound_ok = more_sound(gen);
        if (need_picture && !sound_low) picture_ok = more_picture(gen);
        hold.lock();
        if (gen != generation) continue;
        if (!sound_ok) { sound_on = false; pcm.clear(); hold.unlock(); sound.reset(); hold.lock(); }
        if (!picture_ok) { video_on = false; queue.clear(); current.reset(); }
    }
}

}  // namespace arcade_media

// =========================================================================================================================
// The game's side: which file is whose, and the import the mod calls. Left out of the stand-alone test.
#ifndef ARCADE_MEDIA_STANDALONE
#include <future>
#include <map>
#include "ultramodern/ultramodern.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"

std::vector<std::pair<int, std::filesystem::path>> arcade_wrestlers_folders();      // (arcade_wrestlers.cpp: each folder wrestler's ID and folder)

namespace {
using namespace arcade_media;

int env_number(const char* name, int fallback) { const char* e = getenv(name); return e && e[0] ? atoi(e) : fallback; }
bool media_on() { static const bool on = env_number("WCW_MEDIA", 1) != 0; return on; }

// The game's clock in seconds: 46,875,000 ticks a second (as main.cpp uses it). It runs faster in the tests'
// fast-forward and stops with the game, and the video with it.
double game_seconds() { return double(ultramodern::game_ticks_now()) / 46'875'000.0; }

Player& player() { static Player p(game_seconds); return p; }

// What one folder holds, found and tried once: 2026-11-12, the first time it's wanted (his entrance or song), not for
// every folder at the start. With 300 folders the start-up look took minutes, and the first entrance waited for all of
// it (the game stood still on the entrance screen: nolimits QA). Now a roster of any size costs nothing until a man
// enters, and then only his own folder is looked in.
struct Files { std::filesystem::path theme, video; bool video_sound = false; };
std::map<int, std::filesystem::path> folder_of;     // (by the wrestler's ID: every folder wrestler's, read at the start)
std::map<int, Files> files;                         // (by the wrestler's ID: those looked in so far)
// The sound driver's ticks in a second: the unit of the fade the game gives func_800037BC. 60 is assumed, not measured:
// each stop logs the ticks it was given, so a test can time a real fade against it.
constexpr double SOUND_TICKS_PER_SECOND = 60.0;

std::atomic<bool> ever_started{ false };            // (the player, and its thread, are only made at the first entrance that needs them)

std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32); return s; }

// The files in `folder` called `name`.<anything>, but for the game's own formats and notes beside them. In name order,
// so the choice between theme.mp3 and theme.ogg is the same every time.
std::vector<std::filesystem::path> named(const std::filesystem::path& folder, const char* name) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = lower(entry.path().extension().string());
        if (lower(entry.path().stem().string()) != name || ext.empty() || ext == ".bin" || ext == ".meta" || ext == ".json" || ext == ".txt") continue;
        out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// One folder's theme and video: the first of each that really decodes. One that doesn't is named in the log, and the
// old / stock path is used for it (rule 3: nothing changes for that wrestler).
Files look_in(const std::filesystem::path& folder) {
    Files f;
    const std::string where = folder.filename().string();
    // (2026-11-12: after the common formats, the game's own: theme.bin with its theme.meta, and video.bin. They used to go
    //  through the mod's 64-slot banks only; played here they're found by his ID like the rest, with no count to run out of.)
    std::vector<std::filesystem::path> themes = named(folder, "theme");
    {
        std::error_code ec;
        if (std::filesystem::exists(folder / "theme.bin", ec) && std::filesystem::exists(folder / "theme.meta", ec)) themes.push_back(folder / "theme.bin");
    }
    for (const auto& file : themes) {
        std::string why;
        std::unique_ptr<MediaAudio> a = open_theme(file, why);
        int16_t some[2 * 1024];
        if (a && a->read(some, 1024) > 0) {
            f.theme = file;
            std::error_code ec;
            const bool old_too = file.extension() != ".bin" && std::filesystem::exists(folder / "theme.meta", ec);
            fprintf(stderr, "[media] %s: %s is his theme (%s, %d Hz)%s\n", where.c_str(), file.filename().string().c_str(), a->decoder(), a->rate(), old_too ? "; theme.bin is there too: the new file is used" : "");
            break;
        }
        fprintf(stderr, "[media] %s: %s %s: not used\n", where.c_str(), file.filename().string().c_str(), a ? "has no sound in it" : why.c_str());
    }
    std::vector<std::filesystem::path> videos = named(folder, "titantron");
    for (const auto& v : named(folder, "video")) videos.push_back(v);
    {
        std::error_code ec;
        if (std::filesystem::exists(folder / "video.bin", ec)) videos.push_back(folder / "video.bin");
    }
    for (const auto& file : videos) {
        std::string why;
        VideoOptions quick; quick.hardware = false; quick.cpu_threads = 1;      // (only to see that it decodes)
        std::unique_ptr<MediaVideo> v = open_video(file, quick, why);
        Picture first;
        if (v && v->next(first)) {
            f.video = file;
            std::string sound_why;
            std::unique_ptr<MediaAudio> a = v->info().has_sound ? open_video_sound(file, sound_why) : nullptr;
            int16_t some[2 * 1024];
            f.video_sound = a && a->read(some, 1024) > 0;
            std::error_code ec;
            const bool old_too = file.extension() != ".bin" && std::filesystem::exists(folder / "video.bin", ec);
            fprintf(stderr, "[media] %s: %s is his Titantron video (%s %dx%d, %s)%s\n", where.c_str(), file.filename().string().c_str(), v->info().codec.c_str(), v->info().width, v->info().height,
                    f.video_sound ? "with sound" : "silent", old_too ? "; video.bin is there too: the new file is used" : "");
            break;
        }
        fprintf(stderr, "[media] %s: %s %s: not used\n", where.c_str(), file.filename().string().c_str(), v ? v->why().c_str() : why.c_str());
    }
    return f;
}



void say_stats(const char* when) {
    const PlayerStats st = player().stats();
    if (!st.pictures_decoded && !st.sound_frames_mixed) return;
    fprintf(stderr, "[media] %s: pictures decoded %llu, shown %llu, passed over %llu; video loops %llu; sound loops %llu, pulled back to the picture %llu times, ran dry %llu times; %s\n", when,
            (unsigned long long)st.pictures_decoded, (unsigned long long)st.pictures_shown, (unsigned long long)st.pictures_late, (unsigned long long)st.video_loops,
            (unsigned long long)st.sound_loops, (unsigned long long)st.sound_resyncs, (unsigned long long)st.sound_starved, st.hardware ? "hardware decoder" : "CPU decoder");
    fflush(stderr);
}

std::atomic<bool> playing{ false };

// The files for a wrestler's entrance. variant_name is for the named variants planned (wrestler:variant, any name, any
// number, each able to have its own theme or video): not used yet. Today a variant is a wrestler of his own, with his
// own ID and his own folder (mod variants.c), so his ID alone finds his files.
const Files* files_of(int id, const std::string& variant_name) {
    (void)variant_name;
    auto it = files.find(id);
    if (it == files.end()) {
        const auto folder = folder_of.find(id);
        if (folder == folder_of.end()) return nullptr;      // (not a folder wrestler)
        it = files.emplace(id, look_in(folder->second)).first;
        fflush(stderr);
    }
    return (it->second.theme.empty() && it->second.video.empty()) ? nullptr : &it->second;
}

// recomp_arcade_entrance_media(id, variant, on) -> bits: 1 a native theme plays, 2 a native video, 4 the video's own sound.
// on = 1: the entering wrestler's song starts (mod tron.c tron_music(1)); id is his ID; variant: bit 0 his song alone
// (2026-11-12: a run-in's or the winner's, mod media.c), the other bits kept for the named variants to come (0). on = 0: his song stops or fades (tron_music(0)); then id is the fade's length in
// the sound driver's ticks, as the game gave it to func_800037BC (0: at once), and variant isn't used.
void entrance_media(uint8_t* rdram, recomp_context* ctx) {
    const int32_t id = _arg<0, int32_t>(rdram, ctx), variant = _arg<1, int32_t>(rdram, ctx), on = _arg<2, int32_t>(rdram, ctx);
    if (!media_on()) { _return<int32_t>(ctx, 0); return; }
    if (!on) {
        if (playing.exchange(false)) {
            say_stats("entrance media stopped");
            fprintf(stderr, "[media] the game's song stops over %d ticks: taken as %.2f s\n", id, double(id) / SOUND_TICKS_PER_SECOND);
            player().stop(std::clamp(double(id) / SOUND_TICKS_PER_SECOND, 0.0, 10.0));
        }
        _return<int32_t>(ctx, 0);
        return;
    }

    Files use;
    if (const Files* his = files_of(id & 0xFFFF, std::string())) use = *his;
    // 2026-11-12: variant bit 0 = his song only, no video (the mod: a Rumble run-in's song, the winner's song after the bell,
    // which the game plays in the ring, not on the entrance screen). His theme file, or nothing: a video's own sound
    // isn't taken without its picture.
    const bool sound_only = (variant & 1) != 0;
    if (sound_only) { use.video.clear(); use.video_sound = false; }
    if (use.theme.empty() && use.video.empty()) {
        if (playing.exchange(false)) player().stop(0);       // (the man before him had files: they don't play on)
        _return<int32_t>(ctx, 0);
        return;
    }
    VideoOptions options;
    options.hardware = env_number("WCW_MEDIA_HW", 1) != 0;
    options.max_width = env_number("WCW_MEDIA_MAXW", 1024);
    // (QA only, 2026-11-11: WCW_MEDIA_TEST_FAIL_HW=N makes the hardware decoder "fail" after N pictures, so the move to the CPU
    //  part-way through -- the step that silenced the theme before SOUND_AHEAD was raised -- can be tested on purpose. 0: off.)
    options.test_fail_hardware_after = env_number("WCW_MEDIA_TEST_FAIL_HW", 0);
    const int bits = player().start(use.theme, use.video, use.video_sound, options);
    playing = true; ever_started = true;
    (void)variant;
    // (Neither a theme nor the video's sound: the game's own song plays. Either of them: the game's song is the silent one.)
    fprintf(stderr, "[media] %s of 0x%04X: %s%s%s\n", sound_only ? "song" : "entrance", id & 0xFFFF,
            (bits & PLAYS_THEME) ? "native theme" : (bits & PLAYS_VIDEO_SOUND) ? "the game's song silent" : "the game's own song",
            (bits & PLAYS_VIDEO) ? ", native video" : "", (bits & PLAYS_VIDEO_SOUND) ? " with its own sound" : (bits & PLAYS_VIDEO) ? " (silent)" : "");
    fflush(stderr);
    _return<int32_t>(ctx, bits);
}
}  // namespace

void arcade_media_mix(int16_t* buf, size_t frames, uint32_t rate, float main_volume) {
    if (!ever_started) return;                   // (nothing has played yet. After a stop the sound is still fading: it goes on)
    static const float level = float(std::clamp(env_number("WCW_MEDIA_VOLUME", 100), 0, 200)) / 100.0f;
    player().mix(buf, frames, rate, main_volume * level);
}

bool arcade_media_video_on() { return playing && player().video_playing(); }
std::shared_ptr<const arcade_media::Picture> arcade_media_video_picture() { return playing ? player().picture_now() : nullptr; }

void arcade_media_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_entrance_media", entrance_media);
    // (the folders' list is read here, on the main thread: arcade_wrestlers.cpp reads the folders once, with no lock)
    if (media_on()) {
        for (const auto& [id, folder] : arcade_wrestlers_folders()) folder_of[id] = folder;
        fprintf(stderr, "[media] %zu wrestler folders: each one's theme and video are looked for when he first enters\n", folder_of.size());
        fflush(stderr);
    }
    else { fprintf(stderr, "[media] WCW_MEDIA=0: themes and videos in common formats aren't used\n"); fflush(stderr); }
}
#endif
