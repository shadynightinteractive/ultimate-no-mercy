// revplay.cpp (revmusic, 2026-11-18): WCW/nWo Revenge's songs played by us from the player's Revenge ROM -- the C++ twin of
// mod\gen\revplay.py, giving the same bytes (tools\romimport\native\revplay_check.py proves it). It moves into the game's
// first-launch builder in Phase 3; until then it is a standalone file with a test main (REVPLAY_MAIN).
//
// Revenge's music is its own sequencer driving libultra's n_audio synthesizer. This file mirrors both, step for step
// (see revplay.py for the addresses of Revenge's code each part follows):
//   Bank      proset.wbk ("N64 PtrTablesV2"): 54 VADPCM waves, their tunings
//   Song      a song file (Revenge files 0x210-0x224): tracks with an event, a volume and a pitch-bend stream
//   Player    the sequencer, one tick = 480 output samples; single-precision floats exactly as the N64 computes them
//   Synth     n_audio in 184-sample chunks: VADPCM, the microcode's 4-tap resampler, the envelope mixer, BIGROOM
//   theme()   the song as a No Mercy theme sample: mono, its loop one period of the sequence, resampled
//
// Determinism: integers everywhere except the sequencer's float / double steps (IEEE single and double, + - * / only) and
// the resampler's filter table (double + - * / and floor; no library sin, pow or exp). Build without fast-math and without
// FMA contraction (the pragmas below; MSVC: /fp:precise, the default) and the bytes are the same on every machine.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

namespace revplay {

typedef std::vector<uint8_t> Bytes;
static inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline int16_t be16s(const uint8_t* p) { return (int16_t)(uint16_t)(p[0] << 8 | p[1]); }
static inline int s8(int v) { v &= 0xFF; return v & 0x80 ? v - 256 : v; }
static inline int s16(int64_t v) { v &= 0xFFFF; return (int)(v & 0x8000 ? v - 65536 : v); }
static inline int64_t s32(int64_t v) { v &= 0xFFFFFFFFLL; return v & 0x80000000LL ? v - 0x100000000LL : v; }
static inline int64_t clamp16(int64_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
static inline int64_t vmulf(int64_t x, int64_t g) { return (x * g + 0x4000) >> 15; }

// ------------------------------------------------------------------------------------------------- the ROM's data
static const uint32_t REV_CODE = 0x80000400u - 0x1000u;
static const uint32_t RESAMPLE_LUT = 0x8003A670u, EQPOWER = 0x800370F0u;
static const char BANK_MAGIC[] = "N64 PtrTablesV2";    // + its terminating 0: 16 bytes (Redux leaves the bank's name blank)
static const size_t BANK_MAGIC_LEN = 16;
// Where the game's own boot code says its music bank and its files are: each a lui / addiu pair (the ROM address it loads),
// read from the code so a patched Revenge is played from its own data -- WCW/nWo Revenge Redux (decision C, 2026-11-18;
// reduxmusic.cpp) moves the bank, its samples and the file table past vanilla's 16 MB. (revplay.py BANK_AT ... FILES_BASE)
struct CodePair { uint32_t hi_at, lo_at, hi_op, lo_op; };
static const CodePair BANK_AT = { 0x80003134u, 0x80003138u, 0x3C11u, 0x2631u };      // the bank's header (s1)
static const CodePair SAMPLES_AT = { 0x800031D4u, 0x800031D8u, 0x3C02u, 0x2442u };   // the bank's sample data (v0)
static const CodePair FILES_AT = { 0x800034C8u, 0x800034CCu, 0x3C04u, 0x2484u };     // the file table (a0)
static const uint32_t FILES_SIZE = 0x800034A4u;                                       // its size in bytes (li a0)
static const CodePair FILES_BASE = { 0x80003510u, 0x80003514u, 0x3C14u, 0x2694u };   // the files' offsets from here (s4)
static bool code_word(const Bytes& rom, uint32_t a, uint32_t& w) {
    if (a < REV_CODE || (uint64_t)(a - REV_CODE) + 4 > rom.size()) return false;
    w = be32(&rom[a - REV_CODE]); return true;
}
static bool code_addr(const Bytes& rom, const CodePair& p, int64_t& addr) {
    uint32_t hi, lo;
    if (!code_word(rom, p.hi_at, hi) || !code_word(rom, p.lo_at, lo) || hi >> 16 != p.hi_op || lo >> 16 != p.lo_op) return false;
    addr = ((int64_t)(hi & 0xFFFF) << 16) + s16(lo);
    return true;
}
// the file table as the boot code reads it (vanilla Revenge: 0xCE2752, 7658 entries, base 0xDAC50 -- aki.romtable's row)
struct Files { int64_t tab = 0, base = 0; uint32_t n = 0; };
static bool game_files(const Bytes& rom, Files& f) {
    uint32_t op;
    if (!code_word(rom, FILES_SIZE, op) || (op >> 16 != 0x2404 && op >> 16 != 0x3404)) return false;
    int64_t size = op >> 16 == 0x2404 ? s16(op) : (int64_t)(op & 0xFFFF);
    if (!code_addr(rom, FILES_AT, f.tab) || !code_addr(rom, FILES_BASE, f.base) || size <= 0) return false;
    f.n = (uint32_t)(size / 4);
    return f.tab >= 0 && (uint64_t)f.tab + 4ull * f.n <= rom.size();
}

// AKI's LZSS
static Bytes lzss(const uint8_t* src, size_t len, uint32_t n) {
    Bytes out(n); uint8_t ring[0x1000]; memset(ring, 0, sizeof ring);
    uint32_t r = 0xFEE, o = 0, flags = 0; size_t i = 4;
    while (o < n && i < len) {
        flags >>= 1;
        if (!(flags & 0x100)) { flags = src[i] | 0xFF00; i++; }
        if (flags & 1) {
            if (i >= len) break;
            uint8_t c = src[i++]; out[o++] = c; ring[r] = c; r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= len) break;
            uint32_t lo = src[i], hi = src[i + 1]; i += 2;
            uint32_t pos = lo | ((hi & 0xF0) << 4), ln = (hi & 0xF) + 3;
            for (uint32_t k = 0; k < ln; k++) {
                uint8_t c = ring[(pos + k) & 0xFFF];
                if (o < n) out[o++] = c;
                ring[r] = c; r = (r + 1) & 0xFFF;
            }
        }
    }
    return out;
}
static bool rev_file(const Bytes& rom, uint32_t i, Bytes& out) {
    Files ft;
    if (!game_files(rom, ft) || i < 1 || i >= ft.n) return false;
    uint32_t t0 = be32(&rom[(size_t)ft.tab + 4 * (i - 1)]), t1 = be32(&rom[(size_t)ft.tab + 4 * i]);
    uint64_t a = (uint64_t)(t0 & ~1u) + (uint64_t)ft.base, b = (uint64_t)(t1 & ~1u) + (uint64_t)ft.base;
    if (b < a || b > rom.size()) return false;
    if ((t0 & 1) && b - a >= 4) {
        uint32_t dn = be32(&rom[a]);
        if (dn < 0x400000) { out = lzss(&rom[a], (size_t)(b - a), dn); return true; }
    }
    out.assign(rom.begin() + a, rom.begin() + b);
    return true;
}

static std::vector<int64_t> vadpcm_decode(const uint8_t* data, const int16_t* book, int f0, int f1, int64_t l1, int64_t l2) {
    std::vector<int64_t> out;
    out.reserve((size_t)(f1 - f0) * 16);
    for (int f = f0; f < f1; f++) {
        int h = data[9 * f], scale = h >> 4, pred = h & 15;
        int rs = scale < 12 ? 12 - scale : 0;
        const int16_t* cb1 = book + 16 * pred; const int16_t* cb2 = cb1 + 8;
        int64_t res[16];
        for (int k = 0; k < 8; k++) {
            int b = data[9 * f + 1 + k];
            res[2 * k] = (int64_t)(int16_t)(uint16_t)((b & 0xF0) << 8) >> rs;
            res[2 * k + 1] = (int64_t)(int16_t)(uint16_t)((b & 0x0F) << 12) >> rs;
        }
        for (int half = 0; half < 16; half += 8) {
            const int64_t* src = res + half;
            for (int i = 0; i < 8; i++) {
                int64_t acc = (src[i] << 11) + cb1[i] * l1 + cb2[i] * l2;
                for (int j = 0; j < i; j++) acc += cb2[j] * src[i - j - 1];
                out.push_back(clamp16(acc >> 11));
            }
            l1 = out[out.size() - 2]; l2 = out[out.size() - 1];
        }
    }
    return out;
}

struct Wave { std::vector<int64_t> first, seg; bool looped = false; };   // the samples up to the loop's end; the loop

struct Bank {
    std::vector<float> tuning;
    std::vector<Wave> waves;
    bool load(const Bytes& rom, std::string& err) {
        int64_t at64 = 0, base = 0;
        if (!code_addr(rom, BANK_AT, at64) || at64 <= 0 || (uint64_t)at64 + 0x30 > rom.size() ||
            memcmp(&rom[(size_t)at64], BANK_MAGIC, BANK_MAGIC_LEN)) { err = "the music bank where the game's code says"; return false; }
        if (!code_addr(rom, SAMPLES_AT, base)) { err = "the samples' address in Revenge's code"; return false; }
        const size_t at = (size_t)at64;
        auto w = [&](size_t o) { return be32(&rom[at + o]); };
        uint32_t n = w(0x20), keys = w(0x24), tunes = w(0x28), wv = w(0x2C);
        for (uint32_t i = 0; i < n; i++) {
            float t = (float)((double)(float)s8(rom[at + tunes + 4 * i]) / 100.0);
            t = t + (float)s8(rom[at + keys + i] - 48);
            tuning.push_back(t);
            size_t o = at + w(wv + 4 * i);
            uint32_t wb = be32(&rom[o]), ln = be32(&rom[o + 4]), typ = rom[o + 8];
            uint32_t loop = be32(&rom[o + 12]), book = be32(&rom[o + 16]);
            if (typ != 0) { err = "a wave that is not VADPCM"; return false; }
            int64_t start = (wb & 0xFF000000u) == 0xFF000000u ? (int64_t)wb : (int64_t)wb + base;
            int order = (int)be32(&rom[at + book]), npred = (int)be32(&rom[at + book + 4]);
            if (order != 2) { err = "a VADPCM book of order other than 2"; return false; }
            std::vector<int16_t> bk((size_t)(order * npred * 8));
            for (size_t k = 0; k < bk.size(); k++) bk[k] = be16s(&rom[at + book + 8 + 2 * k]);
            if (start < 0 || (uint64_t)start + ln > rom.size()) { err = "a wave past the ROM's end"; return false; }
            const uint8_t* data = &rom[(size_t)start];
            int nf = (int)(ln / 9);
            Wave wave;
            if (!loop) wave.first = vadpcm_decode(data, bk.data(), 0, nf, 0, 0);
            else {
                uint32_t ls = be32(&rom[at + loop]), le = be32(&rom[at + loop + 4]);
                int32_t lc = (int32_t)be32(&rom[at + loop + 8]);
                if (lc != -1) { err = "a loop that ends"; return false; }
                int64_t st14 = be16s(&rom[at + loop + 12 + 28]), st15 = be16s(&rom[at + loop + 12 + 30]);
                wave.first = vadpcm_decode(data, bk.data(), 0, (int)((le + 15) / 16), 0, 0);
                wave.first.resize(le);
                int f0 = (int)(ls / 16);
                std::vector<int64_t> s = vadpcm_decode(data, bk.data(), f0, (int)((le + 15) / 16), st14, st15);
                wave.seg.assign(s.begin() + (ls - 16 * f0), s.begin() + (le - 16 * f0));
                wave.looped = true;
            }
            waves.push_back(std::move(wave));
        }
        return true;
    }
};

struct Song {
    Bytes d;
    uint32_t ntracks = 0, env = 0, drums = 0;
    struct T { int num; uint32_t a, b, c; };
    std::vector<T> tracks;
    bool load(const Bytes& data) {
        d = data;
        if (d.size() < 24) return false;
        ntracks = be32(&d[0]);
        uint32_t A = be32(&d[4]), B = be32(&d[8]), C = be32(&d[12]);
        env = be32(&d[16]); drums = be32(&d[20]);
        for (uint32_t i = 0; i < ntracks; i++) {
            uint32_t a = be32(&d[A + 4 * i]);
            if (a) tracks.push_back({ (int)i, a, be32(&d[B + 4 * i]), be32(&d[C + 4 * i]) });
        }
        return true;
    }
};

// ------------------------------------------------------------------------------------------------- the sequencer
static const int TEMPO0 = 24576 / 60, RAMP_US = 16666, MASTER = 32767;
enum Kind { START, STOP, PITCH, VOL, PAN, FX };
struct Ev { int voice; Kind kind; int64_t a; float f; };

struct Track {
    int num = 0;
    uint32_t pc = 0, bp = 0, cp = 0;
    int wave_due = -1;
    uint64_t now = 0, bt = 0, ct = 0, next = 0, start = 0, release = 0;
    float bend = 0.0f, last_pitch = 99.9f, pitch = 0.0f, fine = 0.0f, porta_from = 0.0f, porta_at = 0.0f;
    float rate1 = 1.0f, rate2 = (float)(1.0 / 255.0), rate3 = (float)(1.0 / 15.0);
    int64_t step = 0;
    float bend_scale = 0.03125f, bend_raw = 0.0f, fine_last = 0.0f;
    int voltrack = 128, sent_vol = 0xFFFF, bcount = 1, ccount = 1, dur = 1;
    int tempo_scale = 128, tempo = TEMPO0, tempo_base = TEMPO0;
    int elapsed = 0, fixed_dur = 0, instr = 0, panscale = 128, gate_abs = 0, gate_rel = 0, read_dur_once = 0, porta = 0;
    int transpose = 0, no_transpose = 0, vel = 0, vol = 127, pan = 64, sent_pan = 255;
    int e_period = 1, e_l0 = 0, e_l1 = 127, e_l2 = 127, e_phase = 0, e_level = 0, e_count = 0, e_t1 = 1, e_t2 = 255, e_t3 = 15;
    int voice_on = 0, fx = 0, fx_base = 0, sent_fx = 255, e_rel_level = 0;
    int tr_on = 0, tr_off = 0, tr_count = 0, tr_val = 0, tr_amt = 0, vel_each = 0, vel_default = 127, legato = 0, tie = 0;
    struct Loop { uint32_t pc, bp, cp; int vol, count, bend; int bcount, ccount; };
    std::vector<Loop> loops;
};

static float pow2(float x) {          // Revenge's 2^x (0x80015858)
    const double c1 = 0.693147180559945, c2 = 0.240226506959101, c3 = 0.0555041086648216, c4 = 0.00961812910762848,
                 c5 = 0.00133335581464284, c6 = 0.000154035303933816;
    if (x == 0.0f) return 1.0f;
    bool neg = !(0.0f < x);
    if (neg) x = -x;
    float x2 = x * x;
    double a = (double)x * c1, b = (double)x2 * c2, c = (double)(float)(x2 * x) * c3;
    float x4 = x2 * x2;
    double d = (double)x4 * c4, e = (double)(float)(x4 * x) * c5, g = (double)(float)(x4 * x2) * c6;
    double r = a + 1.0; r += b; r += c; r += d; r += e; r += g;
    return neg ? (float)(1.0 / r) : (float)r;
}

struct Player {
    const Song& song; const Bank& bank;
    std::vector<Track> tracks;
    std::vector<Ev> out;
    std::string err;
    // (song_loop's instruments: the endless loops' entries and passes)
    bool watch = false;
    std::vector<int64_t> loop_start; std::vector<std::vector<uint64_t>> loop_ends;

    Player(const Song& s, const Bank& b) : song(s), bank(b) {
        for (auto& t : s.tracks) { Track tr; tr.num = t.num; tr.pc = t.a; tr.bp = t.b; tr.cp = t.c; tracks.push_back(tr); }
        loop_start.assign(tracks.size(), -1); loop_ends.resize(tracks.size());
    }
    int b(Track& t) { return song.d[t.pc++]; }
    int len_(Track& t, int first) { if (first < 0x80) return first; return ((first & 0x7F) << 8) + b(t); }
    void emit(int v, Kind k, int64_t a = 0, float f = 0.0f) { out.push_back({ v, k, a, f }); }

    bool tick() {
        out.clear();
        for (size_t v = 0; v < tracks.size(); v++) {
            Track& t = tracks[v];
            if (!t.pc) continue;
            if (t.wave_due >= 0) start_voice(t, (int)v);
            t.now += (uint64_t)t.tempo;
            if (t.dur != 0x7FFF) {
                while (t.next < t.now) {
                    if (!t.pc) break;
                    if (!event(t, (int)v)) return false;
                }
                if (!t.pc) continue;
            }
            if (t.bp && t.bt < t.now) vol_stream(t);
            if (t.cp && t.ct < t.now) bend_stream(t);
            if (t.voice_on) {
                if (t.e_phase) envelope(t);
                float f20 = t.fine;
                if (t.tr_on) f20 = f20 + trill(t);
                if (t.wave_due < 0) { set_pitch(t, (int)v, f20); set_vol_pan(t, (int)v); }
            }
            t.elapsed = (int)(((t.now - t.start) >> 8) & 0xFFFF);
        }
        return true;
    }
    void start_voice(Track& t, int v) {
        if (t.voice_on) emit(v, STOP);
        t.voice_on = 1;
        emit(v, START, t.wave_due);
        t.wave_due = -1;
    }
    bool event(Track& t, int v) {
        for (;;) {
            int op = song.d[t.pc];
            if (op < 0x80) break;
            t.pc++;
            if (!command(t, op)) return false;
            if (!t.pc) {
                if (t.voice_on) { t.voice_on = 0; emit(v, VOL, 0); emit(v, STOP); }
                return true;
            }
        }
        t.porta_from = t.porta_at;
        int note = b(t);
        t.vel = t.vel_each ? b(t) : t.vel_default;
        if (t.fixed_dur) {
            if (t.read_dur_once) { t.read_dur_once = 0; t.dur = len_(t, b(t)); }
            else t.dur = t.fixed_dur;
        } else t.dur = len_(t, b(t));
        t.elapsed = 0; t.tr_val = 0;
        t.start = t.next; t.tr_count = t.tr_off;
        t.next = t.next + ((uint64_t)t.dur << 8);
        if (note == 0x60) {
            if (t.e_phase < 4) { t.e_phase = 4; t.e_count = 1; t.release = t.now; t.e_rel_level = t.e_level; }
            return true;
        }
        if (t.legato == 0) env_start(t);
        int instr = t.instr;
        if ((size_t)instr >= bank.waves.size()) { err = "an instrument the bank has not got"; return false; }
        if (!t.tie) {
            t.wave_due = instr;
            if (t.voice_on && t.sent_vol != 0) { t.sent_vol = 0; emit(v, VOL, 0); }
            else start_voice(t, v);
        }
        float p = (float)note + bank.tuning[instr];
        int tr = s8((t.transpose * (1 - t.no_transpose)) & 0xFF);
        t.pitch = p + (float)tr;
        if (t.fx != t.sent_fx) {
            t.sent_fx = t.fx;
            emit(v, FX, (t.fx_base + (((128 - t.fx_base) * t.fx) >> 7)) & 0xFF);
        }
        return true;
    }
    void env_start(Track& t) {
        if (t.dur == 0x7FFF) t.release = 0x7FFFFFFF;
        else if (t.gate_abs) t.release = t.start + ((uint64_t)t.gate_abs << 8);
        else t.release = t.next - ((uint64_t)t.gate_rel << 8);
        t.e_phase = 1; t.e_level = t.e_l0; t.e_count = t.e_period;
    }
    void set_env(Track& t, const uint8_t* e) {
        int a = e[0] ? e[0] : 1;
        t.e_period = a; t.step = 1024 / a;
        t.e_l0 = e[1];
        t.e_t1 = e[2]; t.rate1 = (float)((1.0 / (double)(float)e[2]) * (double)(float)(e[3] - t.e_l0));
        t.e_l1 = e[3];
        t.e_t2 = e[4]; t.rate2 = (float)((1.0 / (double)(float)e[4]) * (double)(float)(e[5] - e[3]));
        t.e_l2 = e[5];
        t.e_t3 = e[6]; t.rate3 = (float)(1.0 / (double)(float)e[6]);
    }
    bool command(Track& t, int op) {
        switch (op) {
        case 0x80: t.bp = t.cp = 0; t.pc = 0; t.wave_due = -1; return true;
        case 0x81: t.instr = len_(t, b(t)); return true;
        case 0x82: t.porta = b(t); if (t.porta) t.porta_at = t.pitch; return true;
        case 0x83: t.porta = 0; return true;
        case 0x84: set_env(t, &song.d[t.pc]); t.pc += 7; return true;
        case 0x85: {
            int base = (b(t) * 24576) / 120 / 60;
            for (auto& u : tracks) { u.tempo_base = base; u.tempo = (base * u.tempo_scale) >> 7; }
            return true;
        }
        case 0x86: { int hi = b(t); t.gate_rel = 0; t.gate_abs = (hi << 8) | b(t); return true; }
        case 0x87: t.gate_abs = 0; t.gate_rel = b(t); return true;
        case 0x8a: return true;
        case 0x8b: t.fixed_dur = len_(t, b(t)); return true;
        case 0x8c: t.read_dur_once = 1; return true;
        case 0x8d: t.transpose = b(t); return true;
        case 0x8e: t.no_transpose = 1; return true;
        case 0x8f: {
            float f = (float)((double)(float)s8(b(t)) / 100.0);
            t.fine = (t.fine - t.fine_last) + f; t.fine_last = f;
            return true;
        }
        case 0x90: { int n = len_(t, b(t)); set_env(t, &song.d[song.env + 7 * n]); return true; }
        case 0x91: t.legato = 1; return true;
        case 0x92: t.legato = 0; return true;
        case 0x93: t.tie = 1; return true;
        case 0x94: t.tie = 0; return true;
        case 0x95: {
            if (watch && song.d[t.pc] == 255) { size_t k = &t - &tracks[0]; if (loop_start[k] < 0) loop_start[k] = (int64_t)t.next; }
            int n = b(t);
            t.loops.push_back({ t.pc, t.bp, t.cp, t.vol, n, (int)(int64_t)t.bend_raw & 0xFF, t.bcount, t.ccount });
            return true;
        }
        case 0x96: {
            if (watch && !t.loops.empty() && t.loops.back().count == 255) loop_ends[&t - &tracks[0]].push_back(t.next);
            if (t.loops.empty()) { err = "a loop end with no loop"; return false; }
            int k = (int)t.loops.size() - 1;
            Track::Loop& L = t.loops[k];
            if (L.count != 255) {
                L.count = (L.count - 1) & 0xFF;
                if (L.count == 0) { t.loops.pop_back(); k = -1; }
            }
            if (k >= 0) {
                Track::Loop& M = t.loops[k];
                t.pc = M.pc; t.bp = M.bp; t.cp = M.cp; t.vol = M.vol;
                t.bend_raw = (float)M.bend;
                t.bend = t.bend_raw * t.bend_scale;
                t.bcount = M.bcount; t.ccount = M.ccount;
            }
            t.sent_vol = 0xFFFF; t.sent_fx = 255; t.sent_pan = 255; t.pan = 64;
            t.last_pitch = 99.9f; t.bend_scale = 0.03125f;
            return true;
        }
        case 0x97: t.tr_amt = b(t); t.tr_on = b(t); t.tr_off = b(t); return true;
        case 0x98: t.tr_on = 0; return true;
        case 0x99: t.vel_each = 1; return true;
        case 0x9a: t.vel_each = 0; return true;
        case 0x9b: t.vel_each = 0; t.vel_default = b(t); return true;
        case 0x9c: t.sent_pan = 255; t.pan = b(t) >> 1; return true;
        case 0x9d: t.pc += 2; return true;
        case 0x9f: return true;
        case 0xa0: t.pc += 1; return true;
        case 0xa2: t.fx = b(t); return true;
        case 0xa6: t.vol = b(t); return true;
        case 0xa8: t.bend = t.bend_raw * t.bend_raw; t.bend_scale = (float)((double)b(t) * 0.015625); return true;
        case 0xa9: if (b(t)) { err = "Revenge song command 0xA9 (pan sweep)"; return false; } return true;
        case 0xaa: t.pc += 1; return true;
        }
        char m[96]; snprintf(m, sizeof m, "Revenge song command 0x%02X (not used by the songs this player was made for)", op);
        err = m; return false;
    }
    static int64_t x_of(uint64_t d, int64_t step) {
        return s32((int64_t)((((d >> 8) * (uint64_t)step)) >> 10));
    }
    void envelope(Track& t) {
        if (!(t.now < t.release) && t.e_phase < 4) { t.e_phase = 4; t.e_count = 1; t.e_rel_level = t.e_level; }
        t.e_count = (t.e_count - 1) & 0xFF;
        if (t.e_count) return;
        t.e_count = t.e_period;
        int ph = t.e_phase;
        if (ph == 1) {
            int64_t x = x_of(t.now - t.start, t.step);
            if (x < t.e_t1) t.e_level = (t.e_l0 + (int)(t.rate1 * (float)x)) & 0xFF;
            else { t.e_phase = 2; t.e_level = t.e_l1; }
        } else if (ph == 2) {
            uint64_t d8 = ((t.now - t.start) >> 8) - (uint64_t)t.e_t1;
            int64_t x = s32((int64_t)((d8 * (uint64_t)t.step) >> 10));
            if (x < t.e_t2) t.e_level = (t.e_l1 + (int)(t.rate2 * (float)x)) & 0xFF;
            else { t.e_phase = 3; t.e_level = t.e_l2; }
        } else if (ph == 4) {
            int64_t x = x_of(t.now - t.release, t.step);
            if (x < t.e_t3) {
                float f = (t.rate3 * (float)x) * (float)t.e_rel_level;
                t.e_level = (t.e_rel_level - (int)f) & 0xFF;
            } else { t.e_phase = 5; t.e_level = 0; }
        }
    }
    void vol_stream(Track& t) {
        const Bytes& d = song.d;
        for (;;) {
            t.bcount = (t.bcount - 1) & 0xFFFF; t.bt += 256;
            if (t.bcount == 0) {
                int c = d[t.bp++];
                if (c < 0x80) { t.vol = c; t.bcount = 1; }
                else {
                    t.vol = c & 0x7F;
                    int n = d[t.bp++];
                    if (n < 0x80) t.bcount = n + 2;
                    else { t.bcount = (((n & 0x7F) << 8) + 2 + d[t.bp]) & 0xFFFF; t.bp++; }
                }
            }
            if (!(t.bt < t.now)) break;
        }
    }
    void bend_stream(Track& t) {
        const Bytes& d = song.d;
        for (;;) {
            t.ccount = (t.ccount - 1) & 0xFFFF; t.ct += 256;
            if (t.ccount == 0) {
                int c = d[t.cp++];
                t.bend_raw = (float)((double)(float)(c & 0x7F) - 64.0);
                t.bend = t.bend_raw * t.bend_scale;
                if (c < 0x80) t.ccount = 1;
                else {
                    int n = d[t.cp++];
                    if (n < 0x80) t.ccount = n + 2;
                    else { t.ccount = (((n & 0x7F) << 8) + 2 + d[t.cp]) & 0xFFFF; t.cp++; }
                }
            }
            if (!(t.ct < t.now)) break;
        }
    }
    float trill(Track& t) {
        t.tr_count = (t.tr_count - 1) & 0xFF;
        if (t.tr_count == 0) {
            if (s8(t.tr_val) == 0) { t.tr_val = t.tr_amt; t.tr_count = t.tr_on; }
            else { t.tr_val = 0; t.tr_count = t.tr_off; }
        }
        return (float)s8(t.tr_val);
    }
    void set_pitch(Track& t, int v, float f8) {
        float f6 = t.pitch;
        if (t.porta) {
            if (t.porta < t.elapsed) t.porta_at = f6;
            else {
                float f0 = ((t.pitch - t.porta_from) / (float)t.porta) * (float)t.elapsed;
                f6 = t.porta_from + f0; t.porta_at = f6;
            }
        }
        f6 = f6 + (f8 + t.bend);
        if (f6 == t.last_pitch) return;
        t.last_pitch = f6;
        float r = pow2((float)((double)f6 * (1.0 / 12.0)));
        if (2.0 < (double)r) { r = 2.0f; t.vel = 0; }
        emit(v, PITCH, 0, r);
    }
    void set_vol_pan(Track& t, int v) {
        uint64_t a = (uint32_t)((uint32_t)((uint32_t)(t.vol * t.e_level) * (uint32_t)t.vel) * (uint32_t)(int32_t)s16(t.voltrack));
        a >>= 13;
        if (a > 32767) a = 32767;
        a = (uint32_t)(a * MASTER) >> 15;
        if ((int64_t)a != t.sent_vol) { t.sent_vol = (int)a; emit(v, VOL, s16((int64_t)a)); }
        int p = (int)(((uint32_t)(t.pan * s16(t.panscale)) >> 7) & 0x7F);
        if (p != t.sent_pan) { t.sent_pan = p; emit(v, PAN, p); }
    }
};

// ------------------------------------------------------------------------------------------------- the synthesizer
static const int CHUNK = 184, TICK = 480, RAMP = (TICK + CHUNK / 2) / CHUNK * CHUNK, FX_LEN = 4000;
struct Section { int in, out, ff, fb, gain; };
static const Section BIGROOM[4] = { { 0, 2640, -9830, 9830, 0 }, { 880, 2160, -3276, 3276, 0x3FFF },
                                    { 2640, 3640, -3276, 3276, 0x3FFF }, { 0, 3760, 0, 8000, 0 } };

// (relclean 2026-11-19) 65535 and 2^31 are made from integers when get_rate runs (exactly the same doubles): written as
// literals, the compiler laid them in its constant pool right after get_rate's 1.0 and 8.0 -- the very 32 bytes every AKI
// ROM holds for the audio library's _getRate (the SDK's own pool), which the release audit then found in the program.
static volatile int32_t K_U16_MAX = 0xFFFF;
static volatile int64_t K_2POW31 = int64_t(1) << 31;
static void get_rate(double vol, double tgt, int64_t count, int& ratm, int& ratl) {   // n_audio _getRate
    const double k65535 = (double)K_U16_MAX, k2pow31 = (double)K_2POW31;
    if (count == 0) { if (vol <= tgt) { ratm = 0x7FFF; ratl = 0xFFFF; } else { ratm = 0; ratl = 0; } return; }
    double invn = 1.0 / (double)count;
    if (tgt < 1.0) tgt = 1.0;
    if (vol <= 0.0) vol = 1.0;
    double a = (tgt - vol) * invn * 8.0;
    int64_t m = (int64_t)a;
    double f = a - (double)s16(m);
    f = f + 1.0;
    int64_t a0 = (int64_t)f;
    f = f - (double)s16(a0);
    double f2 = f * k65535;
    ratl = k2pow31 <= f2 ? (int)((((int64_t)(f2 - k2pow31)) | 0x8000) & 0xFFFF) : (int)(((int64_t)f2) & 0xFFFF);
    ratm = s16(m - 1 + a0);
}
static int get_vol(int ivol, int64_t samples, int ratm, int ratl) {                  // n_audio _getVol
    samples >>= 3;
    if (samples == 0) return ivol;
    return s16(ivol + (s32((int64_t)ratl * samples) >> 16) + s32((int64_t)ratm * samples));
}

struct Voice {
    const Wave* stream = nullptr;
    int64_t pos = 0, pitch = 0;
    int volume = 1, pan = 64, dry = 32767, wet = 0;
    int cvol[2] = { 1, 1 }, ratm[2] = { 0, 0 }, ratl[2] = { 0, 0 };
    int64_t delta = 0, seg = 0; bool dirty = false;
    int64_t rv[2] = { 0, 0 }, rt[2] = { 0, 0 }, rs[2] = { 0, 0 };     // the microcode's ramp: value, target, step
    int64_t sample(int64_t idx) const {
        idx -= 4;
        if (idx < 0) return 0;
        if (idx < (int64_t)stream->first.size()) return stream->first[(size_t)idx];
        if (!stream->looped) return 0;
        int64_t n = (int64_t)stream->seg.size();
        return stream->seg[(size_t)((idx - (int64_t)stream->first.size()) % n)];
    }
};

static void synth_update(Voice& v, const Ev& ev, const int16_t* eq, const Bank& bank, bool first) {
    switch (ev.kind) {
    case START: v.stream = &bank.waves[(size_t)ev.a]; v.pos = 0; return;
    case STOP: v.stream = nullptr; v.volume = 1; v.dirty = true; return;
    case PITCH: {
        float r = ev.f;
        if ((double)r > 1.99996) r = 1.99996f;
        v.pitch = (int64_t)(r * 32768.0f) * 2;
        return;
    }
    default: break;
    }
    if (!first || v.delta >= v.seg) {
        v.cvol[0] = (v.volume * eq[v.pan]) >> 15; v.cvol[1] = (v.volume * eq[127 - v.pan]) >> 15;
        v.delta = v.seg;
    } else
        for (int k = 0; k < 2; k++) v.cvol[k] = get_vol(v.cvol[k], v.delta, v.ratm[k], v.ratl[k]);
    for (int k = 0; k < 2; k++) if (!v.cvol[k]) v.cvol[k] = 1;
    if (ev.kind == PAN) v.pan = (int)ev.a;
    else if (ev.kind == VOL) { v.delta = 0; v.volume = (int)((ev.a * ev.a) >> 15); v.seg = RAMP; }
    else if (ev.kind == FX) { v.dry = eq[ev.a]; v.wet = eq[127 - ev.a]; }
    v.dirty = true;
}

struct Synth {
    int16_t lut[256]; int16_t eq[128];
    std::vector<Voice> voices;
    int64_t line[FX_LEN]; int64_t w = 0;
    bool init(const Bytes& rom, size_t nvoices) {
        for (int i = 0; i < 256; i++) lut[i] = be16s(&rom[RESAMPLE_LUT - REV_CODE + 2 * i]);
        for (int i = 0; i < 128; i++) eq[i] = be16s(&rom[EQPOWER - REV_CODE + 2 * i]);
        voices.assign(nvoices, Voice());
        memset(line, 0, sizeof line);
        return lut[0] == 0x0C39 && lut[1] == 0x66AD && lut[2] == 0x0D46 && lut[3] == -33 && eq[0] == 32767 && eq[127] == 0;
    }
    void chunk(int64_t* L, int64_t* R) {
        int64_t dl[CHUNK] = { 0 }, dr[CHUNK] = { 0 }, wl[CHUNK] = { 0 }, wr[CHUNK] = { 0 };
        for (auto& v : voices) {
            if (!v.stream) continue;
            if (v.dirty) {
                int tg[2] = { (v.volume * eq[v.pan]) >> 15, (v.volume * eq[127 - v.pan]) >> 15 };
                for (int k = 0; k < 2; k++) {
                    get_rate((double)v.cvol[k], (double)tg[k], v.seg, v.ratm[k], v.ratl[k]);
                    int64_t rate = s32(((int64_t)v.ratm[k] << 16) | v.ratl[k]);
                    v.rv[k] = (int64_t)v.cvol[k] << 16; v.rt[k] = (int64_t)tg[k] << 16; v.rs[k] = rate / 8;
                }
                v.dirty = false;
            }
            for (int n = 0; n < CHUNK; n++) {
                int64_t p = v.pos + v.pitch * n, i = p >> 16;
                const int16_t* c = lut + ((p & 0xFFFF) >> 10) * 4;
                int64_t s = clamp16((v.sample(i) * c[0] + v.sample(i + 1) * c[1] + v.sample(i + 2) * c[2] + v.sample(i + 3) * c[3]) >> 15);
                int64_t vol[2];
                for (int k = 0; k < 2; k++) {
                    int64_t x = s32(v.rv[k] + v.rs[k]);
                    bool reached = v.rs[k] <= 0 ? x <= v.rt[k] : x >= v.rt[k];
                    if (reached) { x = v.rt[k]; v.rs[k] = 0; }
                    v.rv[k] = x;
                    vol[k] = s16(x >> 16);
                }
                int64_t g0 = clamp16((vol[0] * v.dry + 0x4000) >> 15), g1 = clamp16((vol[1] * v.dry + 0x4000) >> 15);
                int64_t g2 = clamp16((vol[0] * v.wet + 0x4000) >> 15), g3 = clamp16((vol[1] * v.wet + 0x4000) >> 15);
                dl[n] = clamp16(dl[n] + vmulf(s, g0)); dr[n] = clamp16(dr[n] + vmulf(s, g1));
                wl[n] = clamp16(wl[n] + vmulf(s, g2)); wr[n] = clamp16(wr[n] + vmulf(s, g3));
            }
            v.pos += v.pitch * CHUNK;
            v.delta += CHUNK;
        }
        int64_t fx[CHUNK];
        reverb(wl, wr, fx);
        for (int n = 0; n < CHUNK; n++) { L[n] = clamp16(dl[n] + vmulf(fx[n], 0x7FFF)); R[n] = clamp16(dr[n] + vmulf(fx[n], 0x7FFF)); }
    }
    int64_t at(int64_t o, int n) const { int64_t x = (w - o + n) % FX_LEN; return x < 0 ? x + FX_LEN : x; }
    void reverb(const int64_t* al, const int64_t* ar, int64_t* out) {
        for (int n = 0; n < CHUNK; n++) {
            int64_t inp = clamp16(al[n] + vmulf(al[n], -9597));
            inp = clamp16(inp + vmulf(ar[n], 0x5A82));
            line[at(0, n)] = inp;
            out[n] = 0;
        }
        for (const Section& s : BIGROOM) {
            for (int n = 0; n < CHUNK; n++) {
                int64_t b1 = line[at(s.in, n)], b2 = line[at(s.out, n)];
                if (s.ff) b2 = clamp16(b2 + vmulf(b1, s.ff));
                if (s.fb) { b1 = clamp16(b1 + vmulf(b2, s.fb)); line[at(s.in, n)] = b1; }
                line[at(s.out, n)] = b2;
                if (s.gain) out[n] = clamp16(out[n] + vmulf(b2, s.gain));
            }
        }
        w = (w + CHUNK) % FX_LEN;
    }
};

static int64_t tick_chunk(int64_t k) { return (k * TICK) / CHUNK + 1; }

static bool render(const Song& song, const Bank& bank, const Bytes& rom, int64_t nchunks, std::vector<int64_t>& mono, std::string& err) {
    Player P(song, bank);
    Synth S;
    if (!S.init(rom, P.tracks.size())) { err = "the microcode's tables"; return false; }
    mono.assign((size_t)(nchunks * CHUNK), 0);
    std::vector<std::vector<Ev>> due;
    int64_t k = 0;
    for (int64_t c = 0; c < nchunks; c++) {
        while (tick_chunk(k) <= c) {
            if (!P.tick()) { err = P.err; return false; }
            size_t at = (size_t)tick_chunk(k);
            if (due.size() <= at) due.resize(at + 1);
            due[at].insert(due[at].end(), P.out.begin(), P.out.end());
            k++;
        }
        if ((size_t)c < due.size()) {
            std::vector<char> seen(S.voices.size(), 0);
            for (const Ev& ev : due[(size_t)c]) {
                synth_update(S.voices[(size_t)ev.voice], ev, S.eq, bank, !seen[(size_t)ev.voice]);
                seen[(size_t)ev.voice] = 1;
            }
            due[(size_t)c].clear(); due[(size_t)c].shrink_to_fit();
        }
        int64_t L[CHUNK], R[CHUNK];
        S.chunk(L, R);
        for (int n = 0; n < CHUNK; n++) mono[(size_t)(c * CHUNK + n)] = L[n] + R[n];      // (L + R: halved in theme())
    }
    return true;
}

// ------------------------------------------------------------------------------------------------- the song's loop
static bool song_loop(const Song& song, const Bank& bank, int64_t& S, int64_t& U, int64_t& tempo, std::string& err) {
    Player P(song, bank);
    P.watch = true;
    if (!P.tick()) { err = P.err; return false; }
    tempo = P.tracks[0].tempo;
    for (int k = 0; k < 20000; k++) {
        bool done = true;
        for (auto& e : P.loop_ends) if (e.size() < 3) done = false;
        if (done) break;
        if (!P.tick()) { err = P.err; return false; }
        for (auto& t : P.tracks) if (t.tempo != tempo) { err = "a tempo change after the start"; return false; }
    }
    S = 0; U = 1;
    for (size_t n = 0; n < P.tracks.size(); n++) {
        auto& e = P.loop_ends[n];
        if (e.size() < 3 || P.loop_start[n] < 0) { err = "a track that does not loop for ever"; return false; }
        int64_t body = (int64_t)(e[1] - e[0]);
        if ((int64_t)(e[2] - e[1]) != body || body <= 0 || (int64_t)e[0] - P.loop_start[n] != body) { err = "an uneven loop"; return false; }
        S = std::max(S, P.loop_start[n]);
        int64_t a = U, b = body;
        while (b) { int64_t t = a % b; a = b; b = t; }
        U = U / a * body;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------- deterministic sin
static const double PI = 3.141592653589793, PI_A = 3.1415926218032837, PI_B = 3.178650954705639e-08;
static double dsin(double x) {
    double kd = std::floor(x / PI + 0.5);
    int64_t k = (int64_t)kd;
    double r = (x - kd * PI_A) - kd * PI_B;
    double r2 = r * r;
    double p = 1.0 / 121645100408832000.0;
    static const double D[8] = { 355687428096000.0, 1307674368000.0, 6227020800.0, 39916800.0, 362880.0, 5040.0, 120.0, 6.0 };
    for (double d : D) p = 1.0 / d - r2 * p;
    double s = r * (1.0 - r2 * p);
    return (k & 1) ? -s : s;
}
static int64_t rnd(double x) { return x >= 0 ? (int64_t)std::floor(x + 0.5) : -(int64_t)std::floor(-x + 0.5); }
static std::vector<int64_t> fir_table(double q, int W, int P) {
    double fc = 0.46 * q;
    std::vector<int64_t> rows((size_t)P * 2 * W);
    std::vector<double> h((size_t)2 * W);
    for (int ph = 0; ph < P; ph++) {
        for (int k = -W + 1, i = 0; k <= W; k++, i++) {
            double t = (double)k - (double)ph / (double)P;
            double s = t == 0.0 ? 2.0 * fc : dsin(2.0 * PI * fc * t) / (PI * t);
            double u = t / (double)W;
            double w = 0.42 + 0.5 * dsin(PI * u + PI / 2) + 0.08 * dsin(2.0 * PI * u + PI / 2);
            h[(size_t)i] = (-1.0 < u && u < 1.0) ? s * w : 0.0;
        }
        double tot = 0.0;
        for (double v : h) tot += v;
        int64_t* c = &rows[(size_t)ph * 2 * W];
        int64_t sum = 0; size_t big = 0;
        for (size_t i = 0; i < h.size(); i++) {
            c[i] = rnd(h[i] * 65536.0 / tot); sum += c[i];
            if (std::fabs(h[i]) > std::fabs(h[big])) big = i;
        }
        c[big] += 65536 - sum;
    }
    return rows;
}

// ------------------------------------------------------------------------------------------------- the theme sample
static const double OUT_RATE = 479.93 * 60.0176, THEME_RATE = 10787.0;
static const int64_t PEAK = 30000, FADE = 1024;
static int64_t rdiv(int64_t a, int64_t b) { int64_t q = ((a < 0 ? -a : a) * 2 + b) / (2 * b); return a < 0 ? -q : q; }

struct Result { std::vector<int16_t> pcm; int64_t loop_start = 0; double rate = 0; int64_t S = 0, U = 0, tempo = 0, Lr = 0; };

bool theme(const Bytes& rom, int file, Result& res, std::string& err) {
    Bank bank; Song song; Bytes sd;
    if (!bank.load(rom, err)) return false;
    if (!rev_file(rom, (uint32_t)file, sd) || !song.load(sd)) { err = "the song file"; return false; }
    int64_t S, U, tempo;
    if (!song_loop(song, bank, S, U, tempo, err)) return false;
    double Ls = (double)(U * TICK) / (double)tempo;
    int64_t Lr = (int64_t)std::floor(Ls * THEME_RATE / OUT_RATE / 16 + 0.5) * 16;
    int64_t num = Lr * tempo, den = U * TICK;
    int64_t o = tick_chunk(0) * CHUNK, a = tick_chunk(S / tempo) * CHUNK;
    int64_t ls = ((a - o) * num + den - 1) / den + FADE;
    ls = (ls + 15) / 16 * 16;
    int64_t n_out = ls + Lr;
    const int W = 96, P = 1024;
    std::vector<int64_t> m;
    if (!render(song, bank, rom, (o + ((n_out - 1) * den) / num + W + 1) / CHUNK + 1, m, err)) return false;
    std::vector<int64_t> H = fir_table((double)num / (double)den, W, P);
    std::vector<int64_t> y((size_t)n_out);
    for (int64_t j = 0; j < n_out; j++) {
        int64_t pn = j * den;
        int64_t ph = ((pn % num) * P * 2 + num) / (2 * num);
        int64_t i = o + pn / num + ph / P; ph %= P;
        const int64_t* h = &H[(size_t)ph * 2 * W];
        int64_t acc = 0;
        for (int k = 0; k < 2 * W; k++) acc += m[(size_t)(i - W + 1 + k)] * h[k];
        y[(size_t)j] = (acc + (1 << 16)) >> 17;
    }
    for (int64_t k = 0; k < FADE; k++) {
        int64_t e = n_out - FADE + k;
        y[(size_t)e] = rdiv(y[(size_t)e] * (FADE - 1 - k) + y[(size_t)(e - Lr)] * (k + 1), FADE);
    }
    int64_t peak = 0;
    for (int64_t v : y) peak = std::max(peak, v < 0 ? -v : v);
    res.pcm.resize((size_t)n_out);
    for (size_t j = 0; j < y.size(); j++) {
        int64_t v = peak > PEAK ? rdiv(y[j] * PEAK, peak) : y[j];
        res.pcm[j] = (int16_t)clamp16(v);
    }
    res.loop_start = ls; res.rate = (double)(Lr * tempo) * OUT_RATE / (double)(U * TICK);
    res.S = S; res.U = U; res.tempo = tempo; res.Lr = Lr;
    return true;
}

// ---- (reduxthemes, 2026-11-18) Revenge Redux's themes: a song's clip at its own speed, as loud as the old themes ---------
// The C++ twin of mod\gen\reduxmusic.py clip_theme (see there for the why): the clip of Redux's song (its sequence file's
// instrument) decoded, resampled to the theme's rate (the whole clip a whole number of 16-sample frames), its end faded out,
// a gain to the old themes' median level, the look-ahead limiter. Integers only (the gain's square root on 128 bits: the
// GCC / Clang __int128 the project's toolchains have).
static const int64_t CLIP_RATE = 11025, CLIP_THEME_RATE = 10787, CLIP_FADE = 1024, CLIP_TARGET = 8800, CLIP_CEIL = 31000;
static const int64_t REL_Q16 = 65485;          // exp(-1 / (0.120 * 10787)) in 1/65536, a constant
static const int CLIP_W = 96;
static const uint32_t SEQ0 = 0x3F00, SONG0 = 0x18, SEQ_INSTRUMENT = 342;
static int64_t fdiv(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
static int64_t isqrt128(unsigned __int128 v) {
    uint64_t lo = 0, hi = (uint64_t)1 << 42;     // (the gains here are far under 2^42)
    while (lo < hi) { uint64_t m = lo + (hi - lo + 1) / 2; if ((unsigned __int128)m * m <= v) lo = m; else hi = m - 1; }
    return (int64_t)lo;
}
static int64_t median_ms(const std::vector<int64_t>& y, int64_t W) {
    std::vector<int64_t> s;
    for (int64_t k = 0; (k + 1) * W <= (int64_t)y.size(); k++) {
        int64_t t = 0;
        for (int64_t i = k * W; i < (k + 1) * W; i++) t += y[(size_t)i] * y[(size_t)i];
        if (t > 200 * 200 * W) s.push_back(t);
    }
    if (s.empty()) return 0;
    std::sort(s.begin(), s.end());
    size_t m = s.size() / 2;
    return s.size() % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}
static std::vector<int64_t> limit(const std::vector<int64_t>& y, int64_t g, int64_t la) {
    size_t n = y.size();
    std::vector<int64_t> v(n), need(n + 2 * (size_t)la, 65536), out(n);
    for (size_t i = 0; i < n; i++) {
        v[i] = fdiv(y[i] * g + 32768, 65536);
        int64_t a = v[i] < 0 ? -v[i] : v[i];
        need[i + (size_t)la] = a <= CLIP_CEIL ? 65536 : (CLIP_CEIL * 65536) / (a > 1 ? a : 1);
    }
    int64_t cur = 65536;
    for (size_t i = 0; i < n; i++) {
        int64_t gi = need[i];
        for (size_t d = 1; d <= 2 * (size_t)la; d++) gi = std::min(gi, need[i + d]);
        cur = gi < cur ? gi : gi + fdiv((cur - gi) * REL_Q16, 65536);
        out[i] = clamp16(rdiv(v[i] * cur, 65536));
    }
    return out;
}
bool clip_theme(const Bytes& rom, int song, Result& res, std::string& err) {
    Bank bank; Bytes seq;
    if (!bank.load(rom, err)) return false;
    if (!rev_file(rom, SEQ0 + (uint32_t)song - SONG0, seq) || seq.size() <= SEQ_INSTRUMENT || seq[SEQ_INSTRUMENT] != song + 0x1E) {
        err = "Redux's song file"; return false;
    }
    int inst = seq[SEQ_INSTRUMENT];
    if (inst >= (int)bank.waves.size()) { err = "Redux's instrument"; return false; }
    const std::vector<int64_t>& x = bank.waves[(size_t)inst].first;
    int64_t n = (int64_t)x.size();
    int64_t Lr = (n * CLIP_THEME_RATE + 8 * CLIP_RATE) / (16 * CLIP_RATE) * 16, num = Lr, den = n;
    const int P = 1024;
    std::vector<int64_t> H = fir_table((double)num / (double)den, CLIP_W, P);
    std::vector<int64_t> m((size_t)(n + 2 * CLIP_W + 2), 0);
    for (int64_t i = 0; i < n; i++) m[(size_t)(CLIP_W + i)] = x[(size_t)i];
    std::vector<int64_t> y((size_t)Lr);
    for (int64_t j = 0; j < Lr; j++) {
        int64_t pn = j * den;
        int64_t ph = ((pn % num) * P * 2 + num) / (2 * num);
        int64_t i = pn / num + ph / P; ph %= P;
        const int64_t* h = &H[(size_t)ph * 2 * CLIP_W];
        int64_t acc = 0;
        for (int k = 0; k < 2 * CLIP_W; k++) acc += m[(size_t)(CLIP_W + i - CLIP_W + 1 + k)] * h[k];
        y[(size_t)j] = fdiv(acc + (1 << 15), 65536);
    }
    for (int64_t k = 0; k < CLIP_FADE; k++) { size_t e = (size_t)(Lr - CLIP_FADE + k); y[e] = rdiv(y[e] * (CLIP_FADE - 1 - k), CLIP_FADE); }
    double rate = (double)Lr * (double)CLIP_RATE / (double)n;
    int64_t W = (int64_t)(0.4 * rate), T = CLIP_TARGET * CLIP_TARGET * W, la = (int64_t)(0.005 * rate);
    auto gain = [&](int64_t med) { return isqrt128(((unsigned __int128)1 << 32) * (unsigned __int128)T / (unsigned __int128)med); };
    int64_t med = median_ms(y, W);
    if (med <= 0) { err = "a silent clip"; return false; }
    std::vector<int64_t> out = limit(y, gain(med), la);
    for (int it = 0; it < 3; it++) {
        med = median_ms(out, W);
        if (50 * (med > T ? med - T : T - med) < T || med <= 0) break;
        out = limit(out, gain(med), la);
    }
    res.pcm.resize((size_t)Lr);
    for (size_t j = 0; j < out.size(); j++) res.pcm[j] = (int16_t)clamp16(out[j]);
    res.loop_start = 0; res.rate = rate; res.S = 0; res.U = n; res.tempo = 0; res.Lr = Lr;
    return true;
}

}  // namespace revplay

#ifdef REVPLAY_MAIN
// revplay <ROM, big-endian .z64> <song file, hex> <out.pcm>: the theme sample (int16 little-endian) + a line of facts
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "revplay <rom.z64> <file hex> <out.pcm>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "no ROM\n"); return 1; }
    revplay::Bytes rom;
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) rom.insert(rom.end(), buf, buf + n);
    fclose(f);
    revplay::Result r; std::string err;
    // ("clip:<song hex>": Revenge Redux's song as clip_theme makes it, from the Redux ROM)
    const bool clip = !strncmp(argv[2], "clip:", 5);
    if (!(clip ? revplay::clip_theme(rom, (int)strtol(argv[2] + 5, nullptr, 16), r, err)
               : revplay::theme(rom, (int)strtol(argv[2], nullptr, 16), r, err))) { fprintf(stderr, "revplay: %s\n", err.c_str()); return 1; }
    FILE* o = fopen(argv[3], "wb");
    for (int16_t v : r.pcm) { uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((uint16_t)v >> 8) }; fwrite(b, 1, 2, o); }
    fclose(o);
    printf("samples %zu loop %lld rate %.17g S %lld U %lld tempo %lld Lr %lld\n", r.pcm.size(), (long long)r.loop_start, r.rate,
           (long long)r.S, (long long)r.U, (long long)r.tempo, (long long)r.Lr);
    return 0;
}
#endif
