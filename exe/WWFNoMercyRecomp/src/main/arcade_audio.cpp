// The game's sound commands, in plain C++ (step 3 of the native port).
//
// The game's sound code writes a list of commands (decode this voice, resample it, mix it in, save the result) for
// the N64's sound chip to work through. This is what those commands mean, read from the chip's program in the ROM
// (0x41050, 0xC54 bytes; its data 0x55920) and worked out per sample: no chip registers, no vector unit.
//
// The working memory is a plain 4 KB of big-endian bytes, because the game's lists address it by the byte and load
// raw ADPCM into it. Every frame is 0x170 bytes (184 samples): this revision of the program has that fixed.
//   0x3F0   the ADPCM code books
//   0x4F0   the buffers the lists name, by offset from here (the voice: 0x4F0 or 0x660 out of the resampler)
//   0x9D0, 0xB40, 0xCB0, 0xE20    dry left, dry right, wet left, wet right, for the envelope mixer
//   0xFE0   the volumes SETVOL writes, 0xFF0 the left channel's start
//
// Proved on 2026-11-04: 86,528 tasks and 11.2 M writes to game memory over a match, the full select screens, a whole
// story, entrances and a Rumble, every byte the same as the chip's translated program, about 15% quicker. To prove it
// again: configure with -DARCADE_AUDIO_TWIN=ON (rsp/nomercy_audio.cpp back in the build) and run with WCW_AUDIO_TWIN=1:
// this on a copy of the game's memory beside that program, on every task, everything this wrote compared.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "librecomp/rsp.hpp"
#include "arcade_platform.h"     // ([xplat 2026-11-12] the proof build's stopwatch: it was windows.h's)
#include "arcade_audio_filter.h"    // ([audiotable 2026-11-18] the resampler's 64 x 4 filter, read from the player's ROM at start-up)

#ifdef ARCADE_AUDIO_TWIN
extern RspUcodeFunc nomercy_audio_ucode;
#endif

namespace {
using u32 = uint32_t;

inline int16_t clamp16(int64_t x) { return x > 32767 ? int16_t(32767) : x < -32768 ? int16_t(-32768) : int16_t(x); }
// (a x b as fractions, rounded)
inline int16_t mulf(int a, int b) { const int v = (a * b + 0x4000) >> 15; return int16_t(v > 32767 ? 32767 : v); }
// (what's already there x 0x7FFF, plus a x b: the mixers' step)
inline int16_t mix(int old, int a, int b) { const int v = (old * 0x7FFF + a * b + 0x4000) >> 15; return int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

struct WriteRec { u32 dram, len, op, at; };

struct Sound {
    uint8_t d[0x1000]{};
    uint8_t* R = nullptr;           // the game's memory (bytes: address ^ 3)
    u32 loop = 0;                   // SETLOOP's address
    std::vector<WriteRec>* rec = nullptr;
    u32 cur_op = 0, cur_at = 0;

    int16_t r16(u32 a) const { return int16_t(uint16_t(d[a & 0xFFF] << 8 | d[(a + 1) & 0xFFF])); }
    void w16(u32 a, int v) { d[a & 0xFFF] = uint8_t(v >> 8); d[(a + 1) & 0xFFF] = uint8_t(v); }
    void from_game(u32 at, u32 dram, u32 len) { dram &= 0xFFFFF8u; for (u32 i = 0; i < len; i++) d[(at + i) & 0xFFF] = R[(dram + i) ^ 3u]; }
    void to_game(u32 at, u32 dram, u32 len) {
        dram &= 0xFFFFF8u;
        for (u32 i = 0; i < len; i++) R[(dram + i) ^ 3u] = d[(at + i) & 0xFFF];
        if (rec) rec->push_back({ dram, len, cur_op, cur_at });
    }
    // A sixteen-byte load or store reaches only as far as the next sixteen-byte boundary.
    void load16(int16_t* v, u32 a) const {
        a &= 0xFFF;
        if (!(a & 15u)) { uint16_t t[8]; memcpy(t, d + a, 16); for (u32 i = 0; i < 8; i++) v[i] = int16_t(uint16_t(t[i] << 8 | t[i] >> 8)); return; }
        uint8_t b[16]; for (u32 i = 0; i < 8; i++) { b[2 * i] = uint8_t(uint16_t(v[i]) >> 8); b[2 * i + 1] = uint8_t(v[i]); }
        for (u32 i = 0; a + i <= (a | 15u); i++) b[i] = d[a + i];
        for (u32 i = 0; i < 8; i++) v[i] = int16_t(uint16_t(b[2 * i] << 8 | b[2 * i + 1]));
    }
    void store16(const int16_t* v, u32 a) {
        a &= 0xFFF;
        if (!(a & 15u)) { uint16_t t[8]; for (u32 i = 0; i < 8; i++) { const uint16_t e = uint16_t(v[i]); t[i] = uint16_t(e << 8 | e >> 8); } memcpy(d + a, t, 16); return; }
        for (u32 i = 0; a + i <= (a | 15u); i++) { const uint16_t e = uint16_t(v[i / 2]); d[a + i] = uint8_t((i & 1u) ? e : e >> 8); }
    }

    void adpcm(u32 w0, u32 w1);
    void resample(u32 w0, u32 w1);
    void envmixer(u32 w0, u32 w1);
    void mixer(u32 w0, u32 w1);
    void run(const u32* task);
};

// 9 bytes make 16 samples: a header (top: how far to shift, bottom: which code book) and 16 four-bit values, each
// predicted from the two samples before it. The state in memory: the last 32 bytes made.
void Sound::adpcm(u32 w0, u32 w1) {
    const u32 state = w0 & 0xFFFFFFu, out = (w1 & 0xFFFu) + 0x4F0u, flags = w1 >> 28;
    u32 in = ((w1 >> 12) & 0xFu) + 0x4F0u;
    int count = int((w1 >> 16) & 0xFFFu);
    int16_t last[8] = {};
    if (flags & 1u) { store16(last, out); store16(last, out + 0x10u); }
    else { from_game(out, (flags & 2u) ? loop : state, 32); load16(last, out + 0x10u); }
    u32 q = out + 0x20u;
    if (count) {
        uint8_t hdr = d[in & 0xFFF], raw[8];
        for (u32 i = 0; i < 8; i++) raw[i] = d[(in + 1 + i) & 0xFFF];
        do {
            const u32 book = 0x3F0u + (hdr & 0xFu) * 32u;
            const int shift = 12 - (hdr >> 4);
            int16_t b1[8], b2[8], s[16], a[8], c[8];
            for (u32 i = 0; i < 8; i++) { b1[i] = r16(book + 2 * i); b2[i] = r16(book + 16 + 2 * i); }
            for (u32 i = 0; i < 8; i++) { s[2 * i] = int16_t(uint16_t((raw[i] & 0xF0u) << 8)); s[2 * i + 1] = int16_t(uint16_t((raw[i] & 0x0Fu) << 12)); }
            if (shift > 0) for (int16_t& v : s) v = int16_t(v >> shift);
            const auto half = [&](int16_t* o, const int16_t* n, int l1, int l2) {
                for (u32 i = 0; i < 8; i++) {
                    u32 sum = u32(int32_t(b1[i]) * l1) + u32(int32_t(b2[i]) * l2) + u32(int32_t(n[i]) * 0x800);     // (the chip's accumulator keeps 32 bits of this)
                    for (u32 j = 0; j < i; j++) sum += u32(int32_t(b2[i - 1 - j]) * n[j]);
                    o[i] = clamp16((int64_t(int32_t(sum)) * 0x20) >> 16);
                }
            };
            half(a, s, last[6], last[7]);
            half(c, s + 8, a[6], a[7]);
            memcpy(last, c, sizeof c);
            in += 9;                                 // (the next frame's read before this one's written, as the chip has it)
            hdr = d[in & 0xFFF];
            for (u32 i = 0; i < 8; i++) raw[i] = d[(in + 1 + i) & 0xFFF];
            store16(a, q); store16(c, q + 0x10u);
            q += 0x20u;
            count -= 0x20;
        } while (count > 0);
    }
    to_game(q - 0x20u, state, 32);
}

// 184 samples out from wherever the pitch takes it, each from four in through a filter picked by the fraction.
void Sound::resample(u32 w0, u32 w1) {
    const u32 state = w0 & 0xFFFFFFu, base = ((w1 >> 2) & 0xFFFu) + 0x4E8u, pitch = (w1 >> 14) & 0xFFFFu;
    u32 out = (w1 & 3u) ? 0x660u : 0x4F0u, acc = 0;
    if (!(w1 >> 30)) {                               // (not starting: the four samples before this frame, the fraction)
        from_game(base, state, 8);
        const u32 dram = state & 0xFFFFF8u;
        acc = u32(R[(dram + 8) ^ 3u]) << 8 | R[(dram + 9) ^ 3u];
    }
    else for (u32 i = 0; i < 8; i++) d[(base + i) & 0xFFF] = 0;
    for (int left = 0x170; left > 0; left -= 0x10, out += 0x10u) {
        int16_t o[8];
        for (u32 n = 0; n < 8; n++, acc += pitch * 2u) {
            const u32 at = base + 2u * (acc >> 16);
            const int16_t* f = &arcade_audio_filter[((acc & 0xFFFFu) >> 10) * 4u];
            const int16_t p0 = mulf(r16(at), f[0]), p1 = mulf(r16(at + 2), f[1]), p2 = mulf(r16(at + 4), f[2]), p3 = mulf(r16(at + 6), f[3]);
            o[n] = clamp16(clamp16(p0 + p1) + clamp16(p2 + p3));
        }
        store16(o, out);
    }
    const u32 at = base + 2u * (acc >> 16);
    for (u32 i = 0; i < 8; i++) d[(0xFA0u + i)] = d[(at + i) & 0xFFF];
    w16(0xFA8u, int(acc & 0xFFFFu));
    to_game(0xFA0u, state, 10);
}

// The voice from 0x4F0 into the four buffers, with a volume for each side moving towards its target every 8 samples.
void Sound::envmixer(u32 w0, u32 w1) {
    static constexpr int STEP[8] = { 0x2000, 0x4000, 0x6000, 0x8000, 0xA000, 0xC000, 0xE000, 0xFFFF };
    const u32 state = w1 & 0xFFFFFFu;
    const bool start = (w0 >> 16) & 1u;
    if (!start) from_game(0xFA0u, state, 0x50);
    int16_t lv[8] = {}, rv[8] = {}, vol[8];          // (the two sides' volumes, top half)
    uint16_t lf[8] = {}, rf[8] = {};                 // (and their fractions)
    if (!start) for (u32 i = 0; i < 8; i++) {
        lv[i] = r16(0xFA0u + 2 * i); lf[i] = uint16_t(r16(0xFB0u + 2 * i));
        rv[i] = r16(0xFC0u + 2 * i); rf[i] = uint16_t(r16(0xFD0u + 2 * i));
    }
    for (u32 i = 0; i < 8; i++) vol[i] = r16(0xFE0u + 2 * i);
    const int ltarget = vol[0], lramp = vol[1], rtarget = vol[3], rramp = vol[4], dry = vol[6], wet = vol[7];
    const uint16_t lfrac = uint16_t(vol[2]), rfrac = uint16_t(vol[5]);
    const auto towards = [](int16_t* v, int ramp, int target) { for (u32 i = 0; i < 8; i++) v[i] = int16_t(ramp >= 0 ? (v[i] < target ? v[i] : target) : (v[i] > target ? v[i] : target)); };
    const auto step = [](int16_t* v, uint16_t* f, int ramp, uint16_t frac) {
        for (u32 i = 0; i < 8; i++) { const u32 low = u32(f[i]) + frac; f[i] = uint16_t(low); v[i] = clamp16(int64_t(v[i]) + ramp + (low >> 16)); }
    };
    u32 in = 0x4F0u, dl = 0x9D0u, dr = 0xB40u, wl = 0xCB0u, wr = 0xE20u;
    int left = 0x170;
    int16_t s[8], a[8], b[8], c[8], e[8];
    const auto group = [&](bool mid_step) {
        load16(s, in); load16(a, dl); load16(b, wl); load16(c, dr); load16(e, wr);
        for (u32 i = 0; i < 8; i++) { a[i] = mix(a[i], s[i], mulf(lv[i], dry)); b[i] = mix(b[i], s[i], mulf(lv[i], wet)); }
        if (mid_step) {                              // (what's saved of the left side is from before this step)
            for (u32 i = 0; i < 8; i++) { w16(0xFA0u + 2 * i, lv[i]); w16(0xFB0u + 2 * i, lf[i]); }
            step(lv, lf, lramp, lfrac);
        }
        for (u32 i = 0; i < 8; i++) { c[i] = mix(c[i], s[i], mulf(rv[i], dry)); e[i] = mix(e[i], s[i], mulf(rv[i], wet)); }
        store16(a, dl); store16(b, wl); store16(c, dr); store16(e, wr);
        in += 0x10u; dl += 0x10u; wl += 0x10u; dr += 0x10u; wr += 0x10u; left -= 0x10;
    };
    if (start) {                                     // (the first 8 samples: each from the start volume, a step's share along)
        const auto first = [](int16_t* v, uint16_t* f, int begin, int ramp, uint16_t frac) {
            for (u32 i = 0; i < 8; i++) {
                const int64_t acc = (int64_t(begin) << 16) + ((u32(STEP[i]) * frac) >> 16) + int64_t(STEP[i]) * ramp;
                v[i] = clamp16(acc >> 16);
                f[i] = (acc >> 16) >= -32768 && (acc >> 16) <= 32767 ? uint16_t(acc) : uint16_t(acc < 0 ? 0 : 0xFFFF);
            }
        };
        first(lv, lf, r16(0xFF0u), lramp, lfrac);
        first(rv, rf, int16_t(w0), rramp, rfrac);
        towards(lv, lramp, ltarget);
        towards(rv, rramp, rtarget);
        group(false);
    }
    step(lv, lf, lramp, lfrac);
    do {
        step(rv, rf, rramp, rfrac);
        towards(lv, lramp, ltarget);
        towards(rv, rramp, rtarget);
        group(true);
    } while (left > 0);
    for (u32 i = 0; i < 8; i++) { w16(0xFC0u + 2 * i, rv[i]); w16(0xFD0u + 2 * i, rf[i]); w16(0xFE0u + 2 * i, vol[i]); }
    to_game(0xFA0u, state, 0x50);
}

void Sound::mixer(u32 w0, u32 w1) {
    const int gain = int16_t(w0);
    u32 to = (w1 & 0xFFFFu) + 0x4F0u, from = (w1 >> 16) + 0x4F0u;
    int16_t a[8] = {}, b[8] = {}, o[8];
    load16(a, to); load16(b, from);
    for (int left = 0x170; left > 0; left -= 0x10) {
        for (u32 i = 0; i < 8; i++) o[i] = mix(a[i], b[i], gain);
        to += 0x10u; from += 0x10u;
        load16(a, to); load16(b, from);              // (the next read before this one's written, as the chip has it)
        store16(o, to - 0x10u);
    }
}

void Sound::run(const u32* task) {
    memset(d, 0, 0xF80);
    loop = 0;
    for (u32 i = 0; i < 16; i++) { const u32 v = task[i]; d[0xFC0 + 4 * i] = uint8_t(v >> 24); d[0xFC1 + 4 * i] = uint8_t(v >> 16); d[0xFC2 + 4 * i] = uint8_t(v >> 8); d[0xFC3 + 4 * i] = uint8_t(v); }
    const u32 list = task[12] & 0xFFFFF8u, size = task[13];
    // The chip's commands carry 24-bit addresses, so sound data has to stay in the first 16 MB of memory. The game's heap
    // can now reach far past that (see arcade_heap_service in the mod), so say so if sound data ever lands up there:
    // an address cut to 24 bits would read or write the wrong memory, with nothing else to show for it.
    static bool said_high = false;
    if (!said_high && (task[12] & 0x1F000000u)) { said_high = true; fprintf(stderr, "[audio] the sound list is past 16 MB: 0x%08X\n", task[12]); }
    for (u32 at = 0; at < size; at += 8) {
        const u32 w0 = *reinterpret_cast<const u32*>(R + list + at), w1 = *reinterpret_cast<const u32*>(R + list + at + 4);
        const u32 op = (w0 >> 24) & 0x7Fu;
        if (!said_high && (op == 4u || op == 6u || op == 11u || op == 15u) && (w1 & 0x1F000000u)) { said_high = true; fprintf(stderr, "[audio] sound data is past 16 MB: command %u, 0x%08X\n", op, w1); }
        cur_op = op; cur_at = at;
        switch (op) {
        case 0: break;
        case 1: adpcm(w0, w1); break;
        case 2: {                                    // CLEARBUFF (16 bytes at a time, at least once)
            u32 to = (w0 & 0xFFFFu) + 0x4F0u; int n = int(w1 & 0xFFFFu);
            do { for (u32 i = 0; i < 16; i++) d[(to + i) & 0xFFF] = 0; to += 16; n -= 0x10; } while (n > 0);
            break;
        }
        case 3: envmixer(w0, w1); break;
        case 4: { const u32 n = (w0 >> 12) & 0xFFFu; if (n) from_game((w0 & 0xFFFu) + 0x4F0u, w1 & 0xFFFFFFu, n); break; }
        case 5: resample(w0, w1); break;
        case 6: { const u32 n = (w0 >> 12) & 0xFFFu; if (n) to_game((w0 & 0xFFFu) + 0x4F0u, w1 & 0xFFFFFFu, n); break; }
        case 9: {                                    // SETVOL
            const u32 f = (w0 >> 16) & 0xFFu;
            if (!(f & 4u)) { w16(0xFE0u, int(w0)); w16(0xFE2u, int(w1 >> 16)); w16(0xFE4u, int(w1)); }
            else if (!(f & 2u)) { w16(0xFE6u, int(w0)); w16(0xFE8u, int(w1 >> 16)); w16(0xFEAu, int(w1)); }
            else { w16(0xFF0u, int(w0)); w16(0xFECu, int(w1 >> 16)); w16(0xFEEu, int(w1)); }
            break;
        }
        case 10: {                                   // DMEMMOVE
            u32 from = (w0 & 0xFFFFu) + 0x4F0u, to = (w1 >> 16) + 0x4F0u; int n = int(w1 & 0xFFFFu);
            do { uint8_t t[16]; for (u32 i = 0; i < 16; i++) t[i] = d[(from + i) & 0xFFF]; for (u32 i = 0; i < 16; i++) d[(to + i) & 0xFFF] = t[i]; from += 16; to += 16; n -= 0x10; } while (n > 0);
            break;
        }
        case 11: from_game(0x3F0u, w1 & 0xFFFFFFu, w0 & 0xFFFFu); break;   // LOADADPCM
        case 12: mixer(w0, w1); break;
        case 13: {                                   // INTERLEAVE
            u32 l = 0x9D0u, r = 0xB40u, o = 0x4F0u;
            for (int n = 0x170; n > 0; n -= 0x10) {
                int16_t a[8], b[8]; for (u32 i = 0; i < 8; i++) { a[i] = r16(l + 2 * i); b[i] = r16(r + 2 * i); }
                for (u32 i = 0; i < 8; i++) { w16(o + 4 * i, a[i]); w16(o + 4 * i + 2, b[i]); }
                l += 16; r += 16; o += 32;
            }
            break;
        }
        case 14: w16(0xFEAu, int(w1)); break;        // (the right side's ramp fraction alone: the chip's table sends this to the last store of SETVOL's second form)
        case 15: loop = w1 & 0xFFFFFFu; break;       // SETLOOP
        default: { static int said = 0; if (said++ < 8) fprintf(stderr, "[audio] a command nothing's known of: %u\n", op); break; }
        }
    }
}

Sound sound;
const u32* task_now = nullptr;
}  // namespace

void arcade_audio_task(const void* task) { task_now = static_cast<const u32*>(task); }

RspExitReason arcade_audio_ucode(uint8_t* rdram, [[maybe_unused]] uint32_t ucode_addr) {
    // [audiotable 2026-11-18] main.cpp's start-up fills the filter from the ROM or stops the game; never resample with none
    if (!arcade_audio_filter_ready()) { fprintf(stderr, "[audio] the resampler's filter was never read from the ROM\n"); fflush(stderr); abort(); }
#ifndef ARCADE_AUDIO_TWIN
    sound.R = rdram; sound.rec = nullptr; sound.run(task_now);
    return RspExitReason::Broke;
#else
    static const bool twin = getenv("WCW_AUDIO_TWIN") != nullptr;
    if (!twin) { sound.R = rdram; sound.rec = nullptr; sound.run(task_now); return RspExitReason::Broke; }

    static std::vector<uint8_t> copy(0x800000);
    static std::vector<WriteRec> rec;
    static uint64_t tasks = 0, writes = 0, bad = 0, said = 0, op_seen[16] = {};
    static double ms_ours = 0, ms_chip = 0;
    const double f = double(arcade::platform::ticks_per_second());
    memcpy(copy.data(), rdram, copy.size());
    rec.clear();
    sound.R = copy.data(); sound.rec = &rec;
    const uint64_t a = arcade::platform::ticks();
    sound.run(task_now);
    const uint64_t b = arcade::platform::ticks();
    const RspExitReason r = nomercy_audio_ucode(rdram, ucode_addr);
    const uint64_t c = arcade::platform::ticks();
    ms_ours += double(b - a) * 1000.0 / f;
    ms_chip += double(c - b) * 1000.0 / f;
    tasks++;
    for (const WriteRec& w : rec) {
        writes++; op_seen[w.op & 15u]++;
        for (u32 i = 0; i < w.len; i++) {
            const u32 x = (w.dram + i) ^ 3u;
            if (copy[x] == rdram[x]) continue;
            bad++;
            if (said++ < 24) {
                fprintf(stderr, "[audio] DIFFERENT: task %llu, command %u at +0x%X, game memory 0x%06X byte %u of %u: ours %02X, the chip's %02X\n", (unsigned long long)tasks, w.op, w.at, w.dram, i, w.len, copy[x], rdram[x]);
                fflush(stderr);
            }
            break;
        }
    }
    if ((tasks & 0xFFu) == 0) {
        fprintf(stderr, "[audio] twin: %llu tasks, %llu writes compared (adpcm %llu, resample %llu, envmixer %llu, save %llu), %llu different; %.3f ms a task here, %.3f the chip's program\n",
            (unsigned long long)tasks, (unsigned long long)writes, (unsigned long long)op_seen[1], (unsigned long long)op_seen[5], (unsigned long long)op_seen[3], (unsigned long long)op_seen[6], (unsigned long long)bad, ms_ours / tasks, ms_chip / tasks);
        fflush(stderr);
    }
    return r;
#endif
}
