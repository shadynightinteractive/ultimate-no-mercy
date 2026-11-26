// [arcade] Match commentary for the No Mercy Arcade mod: Jim Ross & Jerry Lawler (WWE '13's Attitude Era
// lines). The mod reports what happens in a match (recomp_arcade_comm_*); the director (director.cpp)
// picks a line; it's decoded here (stb_vorbis) and mixed into the game's sound as it's queued
// (queue_samples -> arcade_commentary_mix), the game's own sound a little lower while they talk.
// The lines are commentary.pak next to the exe (gen/mkcommentary.py); without it, nothing is said.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>
#include <fstream>
#include <filesystem>

#include "SDL.h"
#include "recompui/recompui.h"
#include "util/file.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "director.h"

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

namespace {

std::mutex mtx;
comm::Director dir;
std::vector<uint8_t> pak;
bool loaded = false;
int volume = 80;              // 0..100
std::vector<float> voice;     // the line playing, mono, at voice_rate
int voice_rate = 0;
double voice_pos = 0;         // in voice samples
float duck = 1.0f;            // the game's sound: 1 alone, lower under the voice

void load_pak() {
    // commentary.pak, then its parts .pak.1, .pak.2, ... as one
    for (int k = 0; ; k++) {
        std::filesystem::path p = recompui::file::get_program_path() / (k ? "commentary.pak." + std::to_string(k) : std::string("commentary.pak"));
        std::ifstream in(p, std::ios::binary);
        if (!in) break;
        pak.insert(pak.end(), std::istreambuf_iterator<char>(in), {});
    }
    if (pak.empty()) { fprintf(stderr, "[commentary] no commentary.pak\n"); return; }
    loaded = dir.load(pak.data(), pak.size());
    dir.seed((uint32_t)SDL_GetTicks64() | 1);
    fprintf(stderr, "[commentary] %zu lines (%s)\n", dir.clips(), loaded ? "ok" : "bad pack");
}

// start clip c (under mtx)
void start(int c, uint64_t now) {
    if (c < 0) return;
    std::pair<uint32_t, uint32_t> snd = dir.sound(c);
    int ch = 0, rate = 0;
    short* out = nullptr;
    int n = stb_vorbis_decode_memory(pak.data() + snd.first, (int)snd.second, &ch, &rate, &out);
    if (n <= 0 || !out) return;
    voice.assign((size_t)n, 0.0f);
    for (int i = 0; i < n; i++) {
        int s = 0;
        for (int j = 0; j < ch; j++) s += out[i * ch + j];
        voice[i] = (float)s / (32768.0f * ch);
    }
    free(out);
    voice_rate = rate;
    // a lead-in's name right after it ("What a kick by" ... "Kurt Angle!")
    int nm = dir.chain();
    if (nm >= 0) {
        std::pair<uint32_t, uint32_t> s2 = dir.sound(nm);
        int ch2 = 0, rate2 = 0;
        short* o2 = nullptr;
        int n2 = stb_vorbis_decode_memory(pak.data() + s2.first, (int)s2.second, &ch2, &rate2, &o2);
        if (n2 > 0 && o2 && rate2 > 0) {
            // he runs "by" into the name: the lead-in's last moment eased out, a beat, the name eased in at the
            // lead-in's level (both cut from his lines)
            const size_t fade = std::min(voice.size(), (size_t)rate * comm::CHAIN_FADE_MS / 1000);
            for (size_t i = 0; i < fade; i++) voice[voice.size() - fade + i] *= std::sqrt(1.0f - (float)(i + 1) / fade);
            voice.insert(voice.end(), (size_t)rate * comm::CHAIN_GAP_MS / 1000, 0.0f);
            const float gain = dir.chain_gain();
            const size_t fin = (size_t)rate * 5 / 1000, at = voice.size();
            const double step = (double)rate2 / rate;          // (to this clip's rate)
            for (double p = 0; p < n2 - 1; p += step) {
                int i = (int)p;
                float a = 0, b = 0;
                for (int j = 0; j < ch2; j++) { a += o2[i * ch2 + j]; b += o2[(i + 1) * ch2 + j]; }
                float f = (float)(p - i);
                size_t k = voice.size() - at;
                float in = k < fin ? (float)k / fin : 1.0f;
                voice.push_back((a + (b - a) * f) / (32768.0f * ch2) * gain * in);
            }
        }
        free(o2);
    }
    voice_pos = 0;
    dir.started(c, now);
}

// ---- the exports ---------------------------------------------------------------------------------
// recomp_arcade_comm_event(ev, a, b, arg): arg bits 0-7 the event's number, bit 8: a is a woman,
// bits 16-31 the move (the game's move number / 10; 0xFFFF none)
void comm_event(uint8_t* rdram, recomp_context* ctx) {
    int ev = _arg<0, int>(rdram, ctx), a = _arg<1, int>(rdram, ctx), b = _arg<2, int>(rdram, ctx);
    int arg = _arg<3, int>(rdram, ctx);
    std::lock_guard<std::mutex> lk(mtx);
    int r = -1;
    if (loaded) {
        uint64_t now = SDL_GetTicks64();
        int mv = (int)(((uint32_t)arg >> 16) & 0xFFFF);
        int c = dir.event(ev, a, b, arg & 0xFF, (arg & 0x100) != 0, now, mv == 0xFFFF ? -1 : mv);
        if (c >= 0) { start(c, now); r = c; }
    }
    _return(ctx, r);
}

// recomp_arcade_comm_tick(in_match): each frame (filler lines, events kept waiting for a line to end)
void comm_tick(uint8_t* rdram, recomp_context* ctx) {
    int in_match = _arg<0, int>(rdram, ctx);
    std::lock_guard<std::mutex> lk(mtx);
    if (loaded) {
        uint64_t now = SDL_GetTicks64();
        int c = dir.tick(now, in_match != 0);
        if (c >= 0) start(c, now);
    }
    // 0: quiet; a line on: 1 (who says it not known), 2 Jim Ross, 3 Jerry Lawler (the announcers at the table)
    int on = dir.playing() >= 0 && SDL_GetTicks64() < dir.until();
    char who = dir.speaker();
    _return(ctx, !on ? 0 : who == 'J' ? 2 : who == 'K' ? 3 : 1);
}

// recomp_arcade_comm_config(on, volume 0-100, talk 0-3 | 0x10 Legends of WrestleMania's lines | 0x20 not WWE '13's
// | 0x40 the AI voice's lines | 0x80 no spliced calls)
void comm_config(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard<std::mutex> lk(mtx);
    const int talk = _arg<2, int>(rdram, ctx);
    // whose lines: WWE '13's (unless 0x20), Legends' (0x10), the AI voice's (0x40)
    unsigned src = ((talk & 0x20) ? 0u : 1u) | ((talk & 0x10) ? 2u : 0u) | ((talk & 0x40) ? 4u : 0u);
    dir.config(_arg<0, int>(rdram, ctx) != 0, talk & 3, src, (talk & 0x80) == 0);
    volume = std::clamp(_arg<1, int>(rdram, ctx), 0, 100);
    if (!_arg<0, int>(rdram, ctx)) { voice.clear(); dir.stopped(); }
}

// recomp_arcade_comm_reset(): a new match (or none): quiet, timers and names forgotten
void comm_reset(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard<std::mutex> lk(mtx);
    voice.clear();
    dir.reset();
}

// recomp_arcade_comm_stop(): the line on now stops
void comm_stop(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard<std::mutex> lk(mtx);
    voice.clear();
    dir.stopped();
}

// recomp_arcade_comm_available() -> number of lines (0: no commentary.pak)
void comm_available(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, loaded ? (int)dir.clips() : 0);
}

} // namespace

// The game's sound on its way out (interleaved stereo, `frames` frames at `rate`): the voice added.
void arcade_commentary_mix(int16_t* buf, size_t frames, uint32_t rate, float main_volume) {
    std::lock_guard<std::mutex> lk(mtx);
    bool talking = !voice.empty() && voice_pos < voice.size();
    const float target = talking ? 0.72f : 1.0f;
    const float g = (volume / 100.0f) * 1.15f * main_volume;
    const double step = rate ? (double)voice_rate / rate : 1.0;
    for (size_t i = 0; i < frames; i++) {
        duck += (target - duck) * 0.0015f;
        float v = 0.0f;
        if (talking) {
            size_t k = (size_t)voice_pos;
            if (k + 1 < voice.size()) {
                float t = (float)(voice_pos - k);
                v = (voice[k] * (1.0f - t) + voice[k + 1] * t) * g;
            } else talking = false;
            voice_pos += step;
        }
        for (int c = 0; c < 2; c++) {
            float s = buf[2 * i + c] * duck + v * 32767.0f;
            buf[2 * i + c] = (int16_t)std::clamp(s, -32768.0f, 32767.0f);
        }
    }
    if (!talking && !voice.empty() && voice_pos >= voice.size()) voice.clear();
}

void arcade_commentary_init() {
    load_pak();
}

void arcade_commentary_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_comm_event", comm_event);
    recomp::overlays::register_base_export("recomp_arcade_comm_tick", comm_tick);
    recomp::overlays::register_base_export("recomp_arcade_comm_config", comm_config);
    recomp::overlays::register_base_export("recomp_arcade_comm_reset", comm_reset);
    recomp::overlays::register_base_export("recomp_arcade_comm_stop", comm_stop);
    recomp::overlays::register_base_export("recomp_arcade_comm_available", comm_available);
}
