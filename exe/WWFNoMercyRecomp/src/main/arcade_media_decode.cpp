// [arcade] The decoders behind arcade_media.h (2026-11-10): FFmpeg for Titantron videos and their sound, and the small
// single-file decoders (dr_mp3, dr_flac, dr_wav, stb_vorbis) for theme files. Nothing outside this file names either.
//
// Portable on purpose: no Windows calls here. The graphics chip's decoder is asked for through FFmpeg, which makes its
// own device (Direct3D 11 on Windows; the same lines take VAAPI on Linux / the Steam Deck and VideoToolbox on a Mac).
// If there's none, or it fails at any point, the video is decoded on the CPU and the log says so.
#include "arcade_media.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/version.h>
#include <libavcodec/version.h>
#include <libavformat/version.h>
#include <libswresample/version.h>
#include <libswscale/version.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
// [xplat 2026-11-12] On Linux / the Mac FFmpeg is loaded on first use, like the delay-loaded DLLs on Windows (D3): the calls below keep
// their names and go through a table (arcade_ffmpeg_lazy.h). Not defined (ARCADE_FFMPEG_LAZY=0 in CMake): linked normally.
#include "arcade_ffmpeg_lazy.h"

// The single-file decoders. No file calls of their own: a theme is read into memory first (std::ifstream takes any
// path, on every system), and decoded from there as it plays.
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "third_party/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "third_party/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "third_party/dr_wav.h"
// stb_vorbis is already in the game for the commentary (commentary/arcade_commentary.cpp holds its code): only its
// declarations here, with the same switches, so there's one copy of it in the program.
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_HEADER_ONLY
#include "commentary/stb_vorbis.c"

#if defined(_WIN32)
// The one Windows call in this file: finding FFmpeg's DLLs (ffmpeg_ready below).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace arcade_media {
namespace {

// ---- FFmpeg may not be there (2026-11-10) -------------------------------------------------------------------------------
// The game must start, and play every entrance the old way, without FFmpeg's DLLs beside it. So on Windows the exe is
// linked to them "delay-loaded" (CMakeLists.txt): Windows doesn't look for a DLL until the first call into it. Before the
// first call, this looks for all five itself, by the names this build's headers give them, once. If one is missing or
// won't load, it says so once in the log, and from then on every video (and any theme only FFmpeg reads) is refused with
// that reason, exactly as a file that can't be decoded is: the old / stock path plays. No FFmpeg function is ever called.
// The themes' own decoders (mp3, flac, wav, ogg) don't need FFmpeg and aren't affected.
// On Linux and the Mac (2026-11-12, D3) the same is done by hand: the program isn't linked to the libraries at all, and this
// opens them from lib/ beside the game (arcade_ffmpeg_lazy.cpp). With ARCADE_FFMPEG_LAZY=0 they are ordinary dependencies instead.
bool ffmpeg_ready() {
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] {
#if defined(_WIN32)
        // (in the order they need each other. The numbers are FFmpeg's own, from its headers: a newer FFmpeg renames them)
        static const char* const NAMES[] = {
            "avutil-" AV_STRINGIFY(LIBAVUTIL_VERSION_MAJOR) ".dll", "swresample-" AV_STRINGIFY(LIBSWRESAMPLE_VERSION_MAJOR) ".dll",
            "swscale-" AV_STRINGIFY(LIBSWSCALE_VERSION_MAJOR) ".dll", "avcodec-" AV_STRINGIFY(LIBAVCODEC_VERSION_MAJOR) ".dll",
            "avformat-" AV_STRINGIFY(LIBAVFORMAT_VERSION_MAJOR) ".dll" };
        for (const char* name : NAMES) {
            if (!LoadLibraryA(name)) {
                fprintf(stderr, "[media] %s isn't beside the game or won't load (Windows error %lu): Titantron videos from common files are off; entrances play the old way\n", name, (unsigned long)GetLastError());
                fflush(stderr);
                return;
            }
        }
#elif defined(ARCADE_FFMPEG_LAZY)
        std::string why;
        if (!arcade_ffmpeg_lazy::load(why)) {
            fprintf(stderr, "[media] FFmpeg: %s: Titantron videos from common files are off; entrances play the old way\n", why.c_str());
            fflush(stderr);
            return;
        }
#endif
        ready = true;
        // FFmpeg's own messages go to the game's log, errors only, and only the first forty: a damaged file can make a
        // decoder say the same thing for every packet (seen in the test: hundreds of "Header missing").
        av_log_set_callback([](void*, int level, const char* format, va_list arguments) {
            static std::atomic<int> said{ 0 };
            if (level > AV_LOG_ERROR) return;
            const int n = said.fetch_add(1);
            if (n > 40) return;
            if (n == 40) { fprintf(stderr, "[media] (more FFmpeg messages not shown)\n"); return; }
            char line[512];
            vsnprintf(line, sizeof line, format, arguments);
            fprintf(stderr, "[media] FFmpeg: %s", line);
        });
        fprintf(stderr, "[media] FFmpeg %s (%s)\n", av_version_info(), avformat_license());
        fflush(stderr);
    });
    return ready;
}
#if defined(_WIN32)
const char* const NO_FFMPEG = "can't be played: FFmpeg's DLLs aren't beside the game";
#else
const char* const NO_FFMPEG = "can't be played: FFmpeg's libraries aren't in lib/ beside the game";
#endif

std::string av_error(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

// FFmpeg takes file names as UTF-8 on every system.
std::string utf8_of(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- the graphics chip's decoder ----------------------------------------------------------------------------------------
// Which of FFmpeg's hardware decoders this system has. The one place the systems differ.
AVHWDeviceType hardware_type() {
#if defined(_WIN32)
    return AV_HWDEVICE_TYPE_D3D11VA;
#elif defined(__APPLE__)
    return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
    return AV_HWDEVICE_TYPE_VAAPI;
#endif
}

// The device, made once and kept: making one takes tens of milliseconds, and every video can share it.
AVBufferRef* hardware_device() {
    static std::mutex lock;
    static AVBufferRef* device = nullptr;
    static bool tried = false;
    std::lock_guard<std::mutex> hold(lock);
    if (!tried) {
        tried = true;
        const int r = av_hwdevice_ctx_create(&device, hardware_type(), nullptr, nullptr, 0);
        if (r < 0) {
            device = nullptr;
            fprintf(stderr, "[media] no hardware video decoder (%s: %s): videos are decoded on the CPU\n", av_hwdevice_get_type_name(hardware_type()), av_error(r).c_str());
        }
        else fprintf(stderr, "[media] hardware video decoder: %s\n", av_hwdevice_get_type_name(hardware_type()));
        fflush(stderr);
    }
    return device;
}

// The pixel format this decoder hands out when it decodes on the graphics chip, or none.
AVPixelFormat hardware_format_of(const AVCodec* codec) {
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) return AV_PIX_FMT_NONE;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == hardware_type()) return config->pix_fmt;
    }
}

// ---- a video ------------------------------------------------------------------------------------------------------------
class FfmpegVideo final : public MediaVideo {
public:
    ~FfmpegVideo() override { close(); }

    bool open(const std::filesystem::path& file, const VideoOptions& options) {
        file_ = file;
        options_ = options;
        if (!open_file()) return false;
        if (options_.hardware && open_decoder(true)) return true;
        return open_decoder(false);
    }

    const VideoInfo& info() const override { return info_; }
    const std::string& why() const override { return why_; }

    bool next(Picture& out) override {
        for (;;) {
            const int got = avcodec_receive_frame(codec_, frame_);
            if (got == 0) {
                const int made = convert(out);
                if (made == SKIPPED) continue;
                if (made == FAILED) {
                    // (the graphics chip gave a frame that can't be brought back to memory: the CPU from here on)
                    if (info_.hardware && fall_back()) continue;
                    return false;
                }
                if (info_.hardware && options_.test_fail_hardware_after > 0 && --options_.test_fail_hardware_after == 0) fall_back();
                return true;
            }
            if (got == AVERROR_EOF) return false;
            if (got != AVERROR(EAGAIN)) {
                if (info_.hardware && fall_back()) continue;
                why_ = "decoding failed: " + av_error(got);
                return false;
            }
            // The decoder wants more of the file.
            const int read = av_read_frame(format_, packet_);
            if (read < 0) {
                avcodec_send_packet(codec_, nullptr);       // (the end of the file: what the decoder still holds comes out)
                continue;
            }
            if (packet_->stream_index == stream_) {
                const int sent = avcodec_send_packet(codec_, packet_);
                if (sent < 0 && sent != AVERROR(EAGAIN) && sent != AVERROR_EOF) {
                    av_packet_unref(packet_);
                    // A hardware decoder often refuses at the first packet (a profile or size the chip doesn't do).
                    if (info_.hardware && fall_back()) continue;
                    if (++bad_packets_ > 64) { why_ = "decoding failed: " + av_error(sent); return false; }
                    continue;
                }
            }
            av_packet_unref(packet_);
        }
    }

    void skip_to(double at) override {
        // More than a second to make up: straight to the key frame before that time, not through every picture on the way.
        // Only if that key frame is ahead of where the decoder is. (Found by the test, 2026-11-10: a file with a key frame
        // every 8 s, in fast-forward: each jump landed back on the key frame it had already passed, and the decoder never
        // caught up.) A file with no index of its key frames is decoded straight through.
        if (at - std::max(0.0, last_at_) > 1.0) {
            AVStream* st = format_->streams[stream_];
            const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
            const int64_t want = start + int64_t(at / av_q2d(st->time_base));
            const int entry = av_index_search_timestamp(st, want, AVSEEK_FLAG_BACKWARD);
            const AVIndexEntry* key = entry >= 0 ? avformat_index_get_entry(st, entry) : nullptr;
            if (key && double(key->timestamp - start) * av_q2d(st->time_base) > std::max(0.0, last_at_) + 0.5 &&
                av_seek_frame(format_, stream_, want, AVSEEK_FLAG_BACKWARD) >= 0) { avcodec_flush_buffers(codec_); last_at_ = -1; }
        }
        skip_until_ = at;
        // (pictures nothing else depends on aren't even decoded while catching up)
        if (codec_ && !info_.hardware) codec_->skip_frame = AVDISCARD_NONREF;
    }

    bool use_cpu() override { return info_.hardware && fall_back("is too slow here"); }

    bool rewind() override {
        // A plain seek to the start; a file that can't seek is opened again.
        const int r = av_seek_frame(format_, stream_, format_->streams[stream_]->start_time != AV_NOPTS_VALUE ? format_->streams[stream_]->start_time : 0, AVSEEK_FLAG_BACKWARD);
        if (r >= 0) {
            avcodec_flush_buffers(codec_);
            codec_->skip_frame = AVDISCARD_DEFAULT;
            last_at_ = -1; skip_until_ = 0;
            return true;
        }
        const bool was_hardware = info_.hardware;
        close();
        return open_file() && ((was_hardware && open_decoder(true)) || open_decoder(false));
    }

private:
    bool open_file() {
        const std::string name = utf8_of(file_);
        int r = avformat_open_input(&format_, name.c_str(), nullptr, nullptr);
        if (r < 0) { format_ = nullptr; why_ = "can't be opened: " + av_error(r); return false; }
        r = avformat_find_stream_info(format_, nullptr);
        if (r < 0) { why_ = "isn't a video file FFmpeg can read: " + av_error(r); return false; }
        stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream_ < 0) { why_ = "has no picture"; return false; }
        info_.has_sound = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
        // Only the picture is read from this file handle: the sound has its own (open_video_sound), so a slow picture
        // never holds the sound up.
        for (unsigned i = 0; i < format_->nb_streams; i++) if (int(i) != stream_) format_->streams[i]->discard = AVDISCARD_ALL;
        const AVStream* st = format_->streams[stream_];
        info_.width = st->codecpar->width;
        info_.height = st->codecpar->height;
        info_.codec = avcodec_get_name(st->codecpar->codec_id);
        info_.rate = st->avg_frame_rate.den ? av_q2d(st->avg_frame_rate) : 0;
        if (st->duration != AV_NOPTS_VALUE) info_.length = double(st->duration) * av_q2d(st->time_base);
        else if (format_->duration != AV_NOPTS_VALUE) info_.length = double(format_->duration) / AV_TIME_BASE;
        if (info_.width <= 0 || info_.height <= 0) { why_ = "has no picture size"; return false; }
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        memory_frame_ = av_frame_alloc();
        return true;
    }

    static AVPixelFormat pick_format(AVCodecContext* c, const AVPixelFormat* offered) {
        const FfmpegVideo* self = static_cast<const FfmpegVideo*>(c->opaque);
        for (const AVPixelFormat* f = offered; *f != AV_PIX_FMT_NONE; f++) if (*f == self->hardware_format_) return *f;
        // The chip won't take this file: FFmpeg's own choice, which is the CPU.
        return avcodec_default_get_format(c, offered);
    }

    bool open_decoder(bool hardware) {
        const AVStream* st = format_->streams[stream_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        // A format can have two decoders: AV1 has dav1d (the CPU, FFmpeg's first choice) and FFmpeg's own "av1" (the
        // graphics chip only). For the hardware try: the first one that can use this system's hardware decoder.
        if (hardware) {
            void* it = nullptr;
            while (const AVCodec* c = av_codec_iterate(&it)) {
                if (c->id == st->codecpar->codec_id && av_codec_is_decoder(c) && hardware_format_of(c) != AV_PIX_FMT_NONE) { codec = c; break; }
            }
        }
        if (!codec) { why_ = std::string("no decoder for ") + info_.codec; return false; }
        hardware_format_ = AV_PIX_FMT_NONE;
        AVBufferRef* device = nullptr;
        if (hardware) {
            hardware_format_ = hardware_format_of(codec);
            device = hardware_format_ != AV_PIX_FMT_NONE ? hardware_device() : nullptr;
            if (!device) return false;       // (not an error: the caller tries the CPU next)
        }
        avcodec_free_context(&codec_);
        codec_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_, st->codecpar);
        codec_->pkt_timebase = st->time_base;
        codec_->opaque = this;
        if (hardware) {
            codec_->hw_device_ctx = av_buffer_ref(device);
            codec_->get_format = pick_format;
        } else {
            codec_->thread_count = std::max(1, options_.cpu_threads);
        }
        const int r = avcodec_open2(codec_, codec, nullptr);
        if (r < 0) {
            avcodec_free_context(&codec_);
            if (!hardware) why_ = "the decoder can't start: " + av_error(r);
            return false;
        }
        info_.hardware = hardware;
        info_.decoder = hardware ? av_hwdevice_get_type_name(hardware_type()) : (std::string(codec->name) == info_.codec ? std::string("cpu") : std::string("cpu, ") + codec->name);
        return true;
    }

    // The graphics chip's decoder failed part-way, or can't keep time: the file again from where it is, on the CPU. Said
    // once in the log.
    bool fall_back(const char* what = "gave up") {
        fprintf(stderr, "[media] %s: the hardware decoder %s; decoding on the CPU\n", file_.filename().string().c_str(), what);
        fflush(stderr);
        const double resume = last_at_;
        if (!open_decoder(false)) return false;
        last_at_ = -1;
        const AVStream* st = format_->streams[stream_];
        const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        av_seek_frame(format_, stream_, start + (resume > 0 ? int64_t(resume / av_q2d(st->time_base)) : 0), AVSEEK_FLAG_BACKWARD);
        skip_until_ = resume + 0.0005;      // (the seek lands on a key frame before it: pictures already shown aren't handed out twice)
        return true;
    }

    enum { FAILED, MADE, SKIPPED };
    int convert(Picture& out) {
        AVFrame* f = frame_;
        if (hardware_format_ != AV_PIX_FMT_NONE && f->format == hardware_format_) {
            // The picture is in the graphics chip's memory: brought back to ours (as NV12, usually).
            av_frame_unref(memory_frame_);
            const int r = av_hwframe_transfer_data(memory_frame_, f, 0);
            if (r < 0) { why_ = "the hardware decoder's picture can't be read back: " + av_error(r); av_frame_unref(frame_); return FAILED; }
            av_frame_copy_props(memory_frame_, f);
            f = memory_frame_;
        }
        const AVStream* st = format_->streams[stream_];
        const double base = av_q2d(st->time_base);
        const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        const double step = info_.rate > 0 ? 1.0 / info_.rate : 1.0 / 30.0;
        // When it's due: its own timestamp. A picture without one follows the one before.
        double at = f->best_effort_timestamp != AV_NOPTS_VALUE ? double(f->best_effort_timestamp - start) * base : (last_at_ < 0 ? 0.0 : last_at_ + step);
        if (at < 0) at = 0;
        if (last_at_ >= 0 && at <= last_at_) at = last_at_ + 0.001;        // (never backwards)
        // A picture from before skip_to's time isn't converted: that's most of the cost of one.
        if (skip_until_ > 0 && at < skip_until_) { last_at_ = at; av_frame_unref(frame_); av_frame_unref(memory_frame_); return SKIPPED; }
        if (skip_until_ > 0) { skip_until_ = 0; codec_->skip_frame = AVDISCARD_DEFAULT; }
        last_at_ = at;
        out.at = at;
        out.lasts = f->duration > 0 ? double(f->duration) * base : step;

        // To RGBA, made smaller on the way if it's wider than the screen can use.
        int ow = f->width, oh = f->height;
        if (options_.max_width > 0 && ow > options_.max_width) { oh = std::max(2, int(int64_t(oh) * options_.max_width / ow)); ow = options_.max_width; }
        if (!scaler_ || scaler_w_ != f->width || scaler_h_ != f->height || scaler_format_ != f->format) {
            sws_freeContext(scaler_);
            scaler_ = sws_getContext(f->width, f->height, AVPixelFormat(f->format), ow, oh, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler_) { why_ = std::string("no conversion from ") + av_get_pix_fmt_name(AVPixelFormat(f->format)); av_frame_unref(frame_); av_frame_unref(memory_frame_); return FAILED; }
            scaler_w_ = f->width; scaler_h_ = f->height; scaler_format_ = f->format;
            // The file's own colours: HD files are BT.709, smaller ones BT.601, unless the file says.
            const int space = f->colorspace != AVCOL_SPC_UNSPECIFIED ? f->colorspace : (f->height >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_BT470BG);
            const int* table = sws_getCoefficients(space);
            sws_setColorspaceDetails(scaler_, table, f->color_range == AVCOL_RANGE_JPEG ? 1 : 0, sws_getCoefficients(AVCOL_SPC_BT709), 1, 0, 1 << 16, 1 << 16);
            info_.out_width = ow; info_.out_height = oh;
        }
        out.width = ow; out.height = oh;
        out.rgba.resize(size_t(ow) * oh * 4);
        uint8_t* planes[4] = { out.rgba.data(), nullptr, nullptr, nullptr };
        int pitches[4] = { ow * 4, 0, 0, 0 };
        sws_scale(scaler_, f->data, f->linesize, 0, f->height, planes, pitches);
        av_frame_unref(frame_);
        av_frame_unref(memory_frame_);
        return MADE;
    }

    void close() {
        sws_freeContext(scaler_); scaler_ = nullptr;
        avcodec_free_context(&codec_);
        if (format_) avformat_close_input(&format_);
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        av_frame_free(&memory_frame_);
    }

    std::filesystem::path file_;
    VideoOptions options_;
    VideoInfo info_;
    std::string why_;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr, * memory_frame_ = nullptr;
    SwsContext* scaler_ = nullptr;
    int scaler_w_ = 0, scaler_h_ = 0, scaler_format_ = -1;
    AVPixelFormat hardware_format_ = AV_PIX_FMT_NONE;
    int stream_ = -1, bad_packets_ = 0;
    double last_at_ = -1, skip_until_ = 0;
};

// ---- the sound of a video (or of a sound file the small decoders don't read) --------------------------------------------
class FfmpegAudio final : public MediaAudio {
public:
    ~FfmpegAudio() override { close(); }

    bool open(const std::filesystem::path& file, std::string& why) {
        const std::string name = utf8_of(file);
        int r = avformat_open_input(&format_, name.c_str(), nullptr, nullptr);
        if (r < 0) { format_ = nullptr; why = "can't be opened: " + av_error(r); return false; }
        r = avformat_find_stream_info(format_, nullptr);
        if (r < 0) { why = "isn't a file FFmpeg can read: " + av_error(r); return false; }
        stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (stream_ < 0) { why = "has no sound"; return false; }
        for (unsigned i = 0; i < format_->nb_streams; i++) if (int(i) != stream_) format_->streams[i]->discard = AVDISCARD_ALL;
        const AVStream* st = format_->streams[stream_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) { why = std::string("no decoder for ") + avcodec_get_name(st->codecpar->codec_id); return false; }
        codec_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_, st->codecpar);
        codec_->pkt_timebase = st->time_base;
        r = avcodec_open2(codec_, codec, nullptr);
        if (r < 0) { why = "the sound decoder can't start: " + av_error(r); return false; }
        rate_ = codec_->sample_rate;
        if (rate_ <= 0) { why = "has no sample rate"; return false; }
        name_ = std::string("ffmpeg ") + codec->name;
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        return true;
    }

    int rate() const override { return rate_; }
    const char* decoder() const override { return name_.c_str(); }

    size_t read(int16_t* out, size_t frames) override {
        size_t done = 0;
        while (done < frames) {
            if (held_at_ < held_.size()) {
                const size_t n = std::min(frames - done, (held_.size() - held_at_) / 2);
                std::memcpy(out + done * 2, held_.data() + held_at_, n * 2 * sizeof(int16_t));
                held_at_ += n * 2; done += n;
                continue;
            }
            if (!decode_more()) break;
        }
        return done;
    }

    bool rewind() override {
        const AVStream* st = format_->streams[stream_];
        if (av_seek_frame(format_, stream_, st->start_time != AV_NOPTS_VALUE ? st->start_time : 0, AVSEEK_FLAG_BACKWARD) < 0) return false;
        avcodec_flush_buffers(codec_);
        held_.clear(); held_at_ = 0; ended_ = false;
        return true;
    }

private:
    // One more decoded block into held_, as 16-bit stereo at the file's own rate. false: the end.
    bool decode_more() {
        held_.clear(); held_at_ = 0;
        for (;;) {
            const int got = avcodec_receive_frame(codec_, frame_);
            if (got == 0) {
                // (made on the first block, and again if the file changes its layout part-way)
                if (!resampler_ || av_channel_layout_compare(&in_layout_, &frame_->ch_layout) || in_format_ != frame_->format) {
                    swr_free(&resampler_);
                    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                    av_channel_layout_uninit(&in_layout_);
                    av_channel_layout_copy(&in_layout_, &frame_->ch_layout);
                    in_format_ = frame_->format;
                    if (swr_alloc_set_opts2(&resampler_, &stereo, AV_SAMPLE_FMT_S16, rate_, &frame_->ch_layout, AVSampleFormat(frame_->format), frame_->sample_rate, 0, nullptr) < 0 || swr_init(resampler_) < 0) {
                        swr_free(&resampler_);
                        av_frame_unref(frame_);
                        return false;
                    }
                }
                const int room = swr_get_out_samples(resampler_, frame_->nb_samples);
                held_.resize(size_t(room) * 2);
                uint8_t* to[1] = { reinterpret_cast<uint8_t*>(held_.data()) };
                const int n = swr_convert(resampler_, to, room, const_cast<const uint8_t**>(frame_->extended_data), frame_->nb_samples);
                av_frame_unref(frame_);
                if (n < 0) { held_.clear(); return false; }
                held_.resize(size_t(n) * 2);
                if (n > 0) return true;
                continue;
            }
            if (got == AVERROR_EOF || ended_) return false;
            if (got != AVERROR(EAGAIN)) return false;
            const int r = av_read_frame(format_, packet_);
            if (r < 0) { avcodec_send_packet(codec_, nullptr); ended_ = true; continue; }
            if (packet_->stream_index == stream_) avcodec_send_packet(codec_, packet_);
            av_packet_unref(packet_);
        }
    }

    void close() {
        swr_free(&resampler_);
        av_channel_layout_uninit(&in_layout_);
        avcodec_free_context(&codec_);
        if (format_) avformat_close_input(&format_);
        av_packet_free(&packet_);
        av_frame_free(&frame_);
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwrContext* resampler_ = nullptr;
    AVChannelLayout in_layout_{};
    int in_format_ = -1;
    int stream_ = -1, rate_ = 0;
    bool ended_ = false;
    std::string name_;
    std::vector<int16_t> held_;
    size_t held_at_ = 0;
};

// ---- theme files: the single-file decoders ------------------------------------------------------------------------------
// Each reads from the file's bytes in memory and hands out 16-bit samples in the file's own channels; stereo is made here.
class SmallDecoder : public MediaAudio {
public:
    size_t read(int16_t* out, size_t frames) override {
        if (channels_ == 2) return read_own(out, frames);
        // Mono is doubled; more than two channels keep the first two.
        scratch_.resize(frames * size_t(channels_));
        const size_t n = read_own(scratch_.data(), frames);
        for (size_t i = 0; i < n; i++) {
            out[i * 2] = scratch_[i * size_t(channels_)];
            out[i * 2 + 1] = channels_ == 1 ? scratch_[i] : scratch_[i * size_t(channels_) + 1];
        }
        return n;
    }
    int rate() const override { return rate_; }

protected:
    virtual size_t read_own(int16_t* out, size_t frames) = 0;
    std::vector<uint8_t> bytes_;
    int rate_ = 0, channels_ = 0;

private:
    std::vector<int16_t> scratch_;
};

class Mp3Theme final : public SmallDecoder {
public:
    ~Mp3Theme() override { if (open_) drmp3_uninit(&mp3_); }
    bool open(std::vector<uint8_t>&& bytes) {
        bytes_ = std::move(bytes);
        if (!drmp3_init_memory(&mp3_, bytes_.data(), bytes_.size(), nullptr)) return false;
        open_ = true; rate_ = int(mp3_.sampleRate); channels_ = int(mp3_.channels);
        return rate_ > 0 && channels_ > 0;
    }
    bool rewind() override { return drmp3_seek_to_pcm_frame(&mp3_, 0) != 0; }
    const char* decoder() const override { return "dr_mp3"; }
protected:
    size_t read_own(int16_t* out, size_t frames) override { return size_t(drmp3_read_pcm_frames_s16(&mp3_, frames, out)); }
private:
    drmp3 mp3_{}; bool open_ = false;
};

class FlacTheme final : public SmallDecoder {
public:
    ~FlacTheme() override { if (flac_) drflac_close(flac_); }
    bool open(std::vector<uint8_t>&& bytes) {
        bytes_ = std::move(bytes);
        flac_ = drflac_open_memory(bytes_.data(), bytes_.size(), nullptr);
        if (!flac_) return false;
        rate_ = int(flac_->sampleRate); channels_ = int(flac_->channels);
        return rate_ > 0 && channels_ > 0;
    }
    bool rewind() override { return drflac_seek_to_pcm_frame(flac_, 0) != 0; }
    const char* decoder() const override { return "dr_flac"; }
protected:
    size_t read_own(int16_t* out, size_t frames) override { return size_t(drflac_read_pcm_frames_s16(flac_, frames, out)); }
private:
    drflac* flac_ = nullptr;
};

class WavTheme final : public SmallDecoder {
public:
    ~WavTheme() override { if (open_) drwav_uninit(&wav_); }
    bool open(std::vector<uint8_t>&& bytes) {
        bytes_ = std::move(bytes);
        if (!drwav_init_memory(&wav_, bytes_.data(), bytes_.size(), nullptr)) return false;
        open_ = true; rate_ = int(wav_.sampleRate); channels_ = int(wav_.channels);
        return rate_ > 0 && channels_ > 0;
    }
    bool rewind() override { return drwav_seek_to_pcm_frame(&wav_, 0) != 0; }
    const char* decoder() const override { return "dr_wav"; }
protected:
    size_t read_own(int16_t* out, size_t frames) override { return size_t(drwav_read_pcm_frames_s16(&wav_, frames, out)); }
private:
    drwav wav_{}; bool open_ = false;
};

class VorbisTheme final : public SmallDecoder {
public:
    ~VorbisTheme() override { if (vorbis_) stb_vorbis_close(vorbis_); }
    bool open(std::vector<uint8_t>&& bytes) {
        bytes_ = std::move(bytes);
        int error = 0;
        vorbis_ = stb_vorbis_open_memory(bytes_.data(), int(bytes_.size()), &error, nullptr);
        if (!vorbis_) return false;
        const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        rate_ = int(info.sample_rate); channels_ = info.channels;
        return rate_ > 0 && channels_ > 0;
    }
    bool rewind() override { return stb_vorbis_seek_start(vorbis_) != 0; }
    const char* decoder() const override { return "stb_vorbis"; }
protected:
    size_t read_own(int16_t* out, size_t frames) override {
        // (it hands out a block at a time: asked again until the buffer's full or the file ends)
        size_t done = 0;
        while (done < frames) {
            const int n = stb_vorbis_get_samples_short_interleaved(vorbis_, channels_, out + done * size_t(channels_), int((frames - done) * size_t(channels_)));
            if (n <= 0) break;
            done += size_t(n);
        }
        return done;
    }
private:
    stb_vorbis* vorbis_ = nullptr;
};

// ---- the game's own formats, from wrestler folders (2026-11-12) ---------------------------------------------------------
// theme.bin + theme.meta and video.bin (tools/wrestler_theme.py, tools/wrestler_video.py) used to be played only by the
// game's sound bank and video lists, through the mod's FOLDER_THEMES / FOLDER_VIDEOS tables of 64 each, after being
// loaded whole into the 128 MB content window at start-up. Played here instead, like a theme.mp3, they cost nothing until
// a wrestler enters and there's no count to run out of (the user: "I def don't want any limits whatsoever").

// theme.bin: VADPCM, mono, at the rate the game plays a theme's samples. theme.meta: the game's 0xD0-byte wave record
// (the ADPCM book at 0x18, the loop at 0xA0: start, end, count), then level, loops, two spare bytes, tuning.
class GameTheme final : public SmallDecoder {
public:
    static constexpr int RATE = 10787;      // (mod/gen/mp3theme.py THEME_RATE: every theme is made at this rate, within 0.1%)
    bool open(const std::filesystem::path& sound, std::string& why) {
        std::ifstream meta_file(sound.parent_path() / "theme.meta", std::ios::binary);
        std::vector<uint8_t> meta((std::istreambuf_iterator<char>(meta_file)), std::istreambuf_iterator<char>());
        std::ifstream sound_file(sound, std::ios::binary);
        data_.assign((std::istreambuf_iterator<char>(sound_file)), std::istreambuf_iterator<char>());
        if (meta.size() < 0xD0 + 8) { why = "has no theme.meta of 216 bytes beside it"; return false; }
        const auto be32 = [&](size_t at) { return int32_t(uint32_t(meta[at]) << 24 | uint32_t(meta[at + 1]) << 16 | uint32_t(meta[at + 2]) << 8 | meta[at + 3]); };
        const auto be16 = [&](size_t at) { return int16_t(uint16_t(meta[at] << 8 | meta[at + 1])); };
        order_ = be32(0x18); predictors_ = be32(0x1C);
        if (order_ < 1 || order_ > 8 || predictors_ < 1 || predictors_ > 16 || 0x20 + size_t(order_ * 8 * predictors_) * 2 > 0xA0) { why = "theme.meta's ADPCM book isn't one"; return false; }
        // The book, laid out the way the N64's decoder applies it: for each predictor and each of the 8 samples out,
        // order + 8 weights over (the last `order` samples, then the 8 new residuals).
        table_.assign(size_t(predictors_) * 8 * size_t(order_ + 8), 0);
        for (int p = 0; p < predictors_; p++) {
            for (int j = 0; j < order_; j++) for (int k = 0; k < 8; k++) t(p, k, j) = be16(0x20 + 2 * size_t((p * order_ + j) * 8 + k));
            for (int k = 1; k < 8; k++) t(p, k, order_) = t(p, k - 1, order_ - 1);
            t(p, 0, order_) = 2048;
            for (int k = 1; k < 8; k++) for (int j = k; j < 8; j++) t(p, j, k + order_) = t(p, j - k, order_);
        }
        const int32_t loop_start = be32(0xA0), loop_end = be32(0xA4);
        end_ = data_.size() / 9 * 16;
        if (loop_end > 0 && size_t(loop_end) < end_) end_ = size_t(loop_end);
        loops_ = meta[0xD0 + 1] != 0 && loop_start >= 0 && size_t(loop_start) < end_;
        loop_start_ = loops_ ? size_t(loop_start) : 0;
        rate_ = RATE;
        channels_ = 1;
        if (end_ == 0) { why = "theme.bin holds no sound"; return false; }
        return true;
    }
    bool rewind() override { at_ = 0; return true; }
    const char* decoder() const override { return "VADPCM (theme.bin)"; }
protected:
    // To its end, then from the loop point on for as long as it's asked (the game's own themes loop the same way). It's
    // decoded as it plays (a frame of 9 bytes is 16 samples), and what's decoded is kept, so the loop reads it again.
    size_t read_own(int16_t* out, size_t frames) override {
        size_t done = 0;
        while (done < frames) {
            if (at_ >= end_) { if (!loops_) break; at_ = loop_start_; }
            if (at_ >= pcm_.size()) decode_frame();
            const size_t n = std::min({ frames - done, pcm_.size() - at_, end_ - at_ });
            memcpy(out + done, pcm_.data() + at_, n * sizeof(int16_t));
            done += n; at_ += n;
        }
        return done;
    }
private:
    int32_t& t(int p, int k, int j) { return table_[(size_t(p) * 8 + size_t(k)) * size_t(order_ + 8) + size_t(j)]; }
    // The next 9-byte frame into 16 samples, as the N64 SDK's vadpcm_dec makes them (checked sample for sample against it:
    // scratch\agents\nolimits\vadpcm_check).
    void decode_frame() {
        const size_t f = pcm_.size() / 16;
        const uint8_t* b = data_.data() + f * 9;
        const int scale = 1 << (b[0] >> 4), p = std::min(b[0] & 0xF, predictors_ - 1);
        int32_t ix[16], out[16], in[16];
        for (int i = 0; i < 16; i += 2) {
            const int hi = b[1 + i / 2] >> 4, lo = b[1 + i / 2] & 0xF;
            ix[i] = (hi <= 7 ? hi : hi - 16) * scale;
            ix[i + 1] = (lo <= 7 ? lo : lo - 16) * scale;
        }
        for (int half = 0; half < 2; half++) {
            for (int i = 0; i < order_; i++) in[i] = half == 0 ? last_[16 - order_ + i] : out[8 - order_ + i];
            for (int i = 0; i < 8; i++) in[order_ + i] = ix[half * 8 + i];
            for (int k = 0; k < 8; k++) {
                int64_t sum = 0;
                for (int j = 0; j < order_ + 8; j++) sum += int64_t(t(p, k, j)) * in[j];
                out[half * 8 + k] = std::clamp(int32_t(sum >> 11), -32768, 32767);     // (>> 11: floor division by 2048, as the SDK does it)
            }
        }
        for (int i = 0; i < 16; i++) { last_[i] = out[i]; pcm_.push_back(int16_t(out[i])); }
    }
    std::vector<uint8_t> data_;
    std::vector<int32_t> table_;
    std::vector<int16_t> pcm_;
    int order_ = 0, predictors_ = 0;
    int32_t last_[16] = {};
    size_t at_ = 0, end_ = 0, loop_start_ = 0;
    bool loops_ = false;
};
// video.bin: 64 x 64 pictures at 7.5 a second, each a 16-byte header, a 256-colour palette (RGBA5551, 512 bytes) and the
// picture (a byte a pixel). Read a picture at a time from the file, so a long video costs no memory either.
class GameVideo final : public MediaVideo {
public:
    static constexpr int SIDE = 64;
    static constexpr size_t FRAME_BYTES = 16 + 512 + SIDE * SIDE;
    static constexpr double RATE = 7.5;
    bool open(const std::filesystem::path& file) {
        file_.open(file, std::ios::binary | std::ios::ate);
        if (!file_.good()) { why_ = "can't be opened"; return false; }
        const uint64_t bytes = uint64_t(file_.tellg());
        if (bytes == 0 || bytes % FRAME_BYTES) { why_ = "isn't a whole number of 64 x 64 frames"; return false; }
        frames_ = size_t(bytes / FRAME_BYTES);
        info_.width = info_.out_width = SIDE;
        info_.height = info_.out_height = SIDE;
        info_.length = double(frames_) / RATE;
        info_.rate = RATE;
        info_.codec = "video.bin";
        info_.decoder = "cpu";
        return true;
    }
    const VideoInfo& info() const override { return info_; }
    bool next(Picture& out) override {
        if (at_ >= frames_) return false;
        std::vector<uint8_t> frame(FRAME_BYTES);
        file_.clear();
        file_.seekg(std::streamoff(at_ * FRAME_BYTES));
        file_.read(reinterpret_cast<char*>(frame.data()), std::streamsize(FRAME_BYTES));
        if (!file_.good()) { why_ = "couldn't be read"; return false; }
        uint8_t rgba[256][4];
        for (int c = 0; c < 256; c++) {
            const unsigned v = unsigned(frame[16 + 2 * c]) << 8 | frame[16 + 2 * c + 1];
            const auto five = [](unsigned x) { return uint8_t((x << 3) | (x >> 2)); };
            rgba[c][0] = five((v >> 11) & 31); rgba[c][1] = five((v >> 6) & 31); rgba[c][2] = five((v >> 1) & 31); rgba[c][3] = 255;
        }
        out.width = SIDE; out.height = SIDE;
        out.rgba.resize(size_t(SIDE) * SIDE * 4);
        for (int i = 0; i < SIDE * SIDE; i++) memcpy(&out.rgba[size_t(i) * 4], rgba[frame[16 + 512 + size_t(i)]], 4);
        out.at = double(at_) / RATE;
        out.lasts = 1.0 / RATE;
        at_++;
        return true;
    }
    bool rewind() override { at_ = 0; return true; }
    void skip_to(double at) override { while (at_ < frames_ && double(at_ + 1) / RATE <= at) at_++; }
    bool use_cpu() override { return false; }
    const std::string& why() const override { return why_; }
private:
    std::ifstream file_;
    size_t frames_ = 0, at_ = 0;
    VideoInfo info_;
    std::string why_;
};

std::string lower_extension(const std::filesystem::path& file) {
    std::string e = file.extension().string();
    for (char& c : e) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return e;
}

}  // namespace

std::unique_ptr<MediaVideo> open_video(const std::filesystem::path& file, const VideoOptions& options, std::string& why) {
    if (lower_extension(file) == ".bin") {     // (2026-11-12: a folder's video.bin, the game's own format: no FFmpeg needed)
        auto v = std::make_unique<GameVideo>();
        if (v->open(file)) return v;
        why = v->why();
        return nullptr;
    }
    if (!ffmpeg_ready()) { why = NO_FFMPEG; return nullptr; }
    auto v = std::make_unique<FfmpegVideo>();
    if (!v->open(file, options)) { why = v->why().empty() ? "can't be opened" : v->why(); return nullptr; }
    return v;
}

std::unique_ptr<MediaAudio> open_video_sound(const std::filesystem::path& file, std::string& why) {
    if (!ffmpeg_ready()) { why = NO_FFMPEG; return nullptr; }
    auto a = std::make_unique<FfmpegAudio>();
    if (!a->open(file, why)) return nullptr;
    return a;
}

std::unique_ptr<MediaAudio> open_theme(const std::filesystem::path& file, std::string& why) {
    const std::string ext = lower_extension(file);
    if (ext == ".bin") {      // (2026-11-12: a folder's theme.bin with its theme.meta, the game's own format)
        auto t = std::make_unique<GameTheme>();
        if (t->open(file, why)) return t;
        return nullptr;
    }
    if (ext == ".mp3" || ext == ".flac" || ext == ".wav" || ext == ".ogg") {
        std::ifstream in(file, std::ios::binary);
        if (!in.good()) { why = "can't be opened"; return nullptr; }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (ext == ".mp3") { auto t = std::make_unique<Mp3Theme>(); if (t->open(std::move(bytes))) return t; }
        else if (ext == ".flac") { auto t = std::make_unique<FlacTheme>(); if (t->open(std::move(bytes))) return t; }
        else if (ext == ".wav") { auto t = std::make_unique<WavTheme>(); if (t->open(std::move(bytes))) return t; }
        else { auto t = std::make_unique<VorbisTheme>(); if (t->open(std::move(bytes))) return t; }
        // (an .ogg that's Opus, a .wav with an odd codec inside: FFmpeg below gets a try before giving up)
    }
    // Any other kind of sound file (m4a, wma, opus ...): FFmpeg is in the game for the videos, so it reads these too.
    if (!ffmpeg_ready()) { why = NO_FFMPEG; return nullptr; }
    std::string ffmpeg_why;
    auto a = std::make_unique<FfmpegAudio>();
    if (a->open(file, ffmpeg_why)) return a;
    why = ffmpeg_why;
    return nullptr;
}

}  // namespace arcade_media
