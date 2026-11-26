// [arcade] Entrance themes and Titantron videos played straight from common files (2026-11-10).
//
// Two layers, so nothing else in the game knows which decoder is behind them:
//   - the decoders (arcade_media_decode.cpp): MediaVideo (pictures of a video file) and MediaAudio (16-bit stereo sound of
//     a theme file, or of a video file). Today: FFmpeg for videos and their sound, dr_mp3 / dr_flac / dr_wav / stb_vorbis
//     for themes. Portable: the same code is meant to build on Linux, Mac and the Steam Deck.
//   - the player (arcade_media.cpp): one thread that keeps a little sound and a few pictures decoded ahead, loops them,
//     and hands them out by the game's clock. The game's side is at the end of this file.
//
// The stand-alone test (media_groundwork/mediatest.cpp) builds both files without the game (ARCADE_MEDIA_STANDALONE).
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace arcade_media {

// ---- the decoders -----------------------------------------------------------------------------------------------------
// One picture of a video: RGBA, 8 bits each, top row first. `at` is when it's due, in seconds from the video's start,
// from the picture's own timestamp (a file's stated frame rate can be wrong: plan.md, the WebM that said 12 for 24).
struct Picture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    double at = 0, lasts = 0;
    uint64_t serial = 0;        // (the player's: a new number for each picture handed to the renderer)
};

struct VideoOptions {
    bool hardware = true;       // Try the graphics chip's decoder first. false: decode on the CPU.
    int max_width = 1024;       // Pictures wider than this are made smaller as they're converted (0: never). The Titantron's
                                // picture is 512 x 256, so more than twice that only costs time and shimmers when drawn small.
    int cpu_threads = 4;        // For decoding on the CPU.
    int test_fail_hardware_after = 0;   // The stand-alone test only: after this many pictures, behave as if the hardware
                                        // decoder had failed, to prove the switch to the CPU part-way through a file.
};

struct VideoInfo {
    int width = 0, height = 0;          // the file's own size
    int out_width = 0, out_height = 0;  // the size of the pictures handed out
    double length = 0;                  // seconds, as the file states it (0: not stated)
    double rate = 0;                    // pictures a second, as the file states it (only for the log)
    bool has_sound = false;
    bool hardware = false;              // decoding on the graphics chip now
    std::string codec, decoder;         // for the log: "h264", "d3d11va" / "cpu"
};

class MediaVideo {
public:
    virtual ~MediaVideo() = default;
    virtual const VideoInfo& info() const = 0;
    // The next picture. false: the end of the video (or it can't go on: why() says).
    virtual bool next(Picture& out) = 0;
    // Back to the start, for the loop.
    virtual bool rewind() = 0;
    // The player has fallen behind: pictures due before `at` are passed over (not converted, not handed out).
    virtual void skip_to(double at) = 0;
    // The hardware decoder can't keep time here (a busy or weak graphics chip): the rest of the video on the CPU.
    // false: it's on the CPU already, or the CPU decoder can't start.
    virtual bool use_cpu() = 0;
    virtual const std::string& why() const = 0;
};

class MediaAudio {
public:
    virtual ~MediaAudio() = default;
    virtual int rate() const = 0;               // samples a second
    // Up to `frames` frames of 16-bit stereo into out. Fewer than asked (0 at last): the end.
    virtual size_t read(int16_t* out, size_t frames) = 0;
    virtual bool rewind() = 0;
    virtual const char* decoder() const = 0;    // for the log
};

// Each returns nothing if the file can't be opened or decoded, with the reason in why.
std::unique_ptr<MediaVideo> open_video(const std::filesystem::path& file, const VideoOptions& options, std::string& why);
std::unique_ptr<MediaAudio> open_video_sound(const std::filesystem::path& file, std::string& why);
std::unique_ptr<MediaAudio> open_theme(const std::filesystem::path& file, std::string& why);

// ---- the player -------------------------------------------------------------------------------------------------------
enum : int { PLAYS_THEME = 1, PLAYS_VIDEO = 2, PLAYS_VIDEO_SOUND = 4 };

struct PlayerStats {
    uint64_t pictures_decoded = 0, pictures_shown = 0, pictures_late = 0;   // late: decoded after their time had passed, dropped
    uint64_t video_loops = 0, sound_loops = 0;
    uint64_t sound_frames_mixed = 0, sound_resyncs = 0, sound_starved = 0;
    bool hardware = false;
};

class Player {
public:
    // clock: the game's time in seconds. Pictures are due by it, so fast-forward and pause stay in step.
    explicit Player(double (*clock)());
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Starts an entrance's files (either can be empty). Whatever was playing stops at once. The user's rule 1 is applied
    // here: with a theme the video is silent; with no theme the video's own sound plays, if it has any (video_has_sound).
    // Returns PLAYS_* bits for what will play.
    int start(const std::filesystem::path& theme, const std::filesystem::path& video, bool video_has_sound, const VideoOptions& options);
    // The entrance's music stops: the picture goes now, the sound fades over fade_seconds (0: at once).
    void stop(double fade_seconds);
    // The sound, added to `frames` frames of 16-bit stereo at `rate` on their way to the speakers.
    void mix(int16_t* buf, size_t frames, uint32_t rate, float volume);
    // The picture due now, or nothing (no video playing, or its first picture isn't decoded yet).
    std::shared_ptr<const Picture> picture_now();
    bool video_playing() const;
    PlayerStats stats() const;

private:
    struct State;
    std::unique_ptr<State> s_;
};

}  // namespace arcade_media

// ---- the game's side (arcade_media.cpp; not in the stand-alone test) -----------------------------------------------------
// main.cpp queue_samples: the theme's or the video's sound, mixed in beside the commentary.
void arcade_media_mix(int16_t* buf, size_t frames, uint32_t rate, float main_volume);
// arcade_view.cpp: true while a native Titantron video plays (the draw of the game's own video frame is replaced), and the
// picture due now (nothing until the first one is decoded).
bool arcade_media_video_on();
std::shared_ptr<const arcade_media::Picture> arcade_media_video_picture();
// main.cpp: the import the mod calls, recomp_arcade_entrance_media(id, variant, on).
void arcade_media_register_exports();
