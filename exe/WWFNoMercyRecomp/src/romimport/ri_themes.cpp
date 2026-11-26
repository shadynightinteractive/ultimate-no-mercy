// ri_themes.cpp (phase3, 2026-11-19): folder themes (ri_themes.h): theme.py encode / theme_files / redux_files and
// reduxmusic.py's tables.
#include "ri_themes.h"
#include "ri_vadpcm.h"
#include <cmath>

namespace ri {

ThemeFiles theme_encode(const std::vector<int16_t>& pcm, int64_t ls, double rate) {
    (void)rate;    // (the VADPCM tools' output doesn't depend on the rate)
    VadpcmOut v = vadpcm_encode(pcm.data(), pcm.size(), (int)std::llround(rate));
    std::vector<int16_t> dec = vadpcm_decode(v.data, v.book, v.samples);
    int16_t state[16] = {};
    if (ls >= 16) for (int i = 0; i < 16; i++) state[i] = dec[(size_t)(ls - 16 + i)];
    Bytes w(0xD0, 0);
    put_be32(&w[0], 0); put_be32(&w[4], (uint32_t)v.data.size()); w[8] = 0; w[9] = 1;          // ">IIBB": 0, length, 0, 1
    memcpy(&w[0x18], v.book.data(), v.book.size());
    put_be32(&w[0xA0], (uint32_t)ls); put_be32(&w[0xA4], v.samples); put_be32(&w[0xA8], 0xFFFFFFFF);   // loop start, end, for ever
    for (int i = 0; i < 16; i++) put_be16(&w[0xAC + 2 * i], (uint16_t)state[i]);                      // the decoder's state at the loop
    ThemeFiles t;
    t.bin = v.data;
    t.meta = w;
    t.meta.insert(t.meta.end(), { 0, 1, 0, 0, 0xC2, 0x40, 0x00, 0x00 });                            // level 0, loops, 2 spare, tune
    t.samples = v.samples;
    return t;
}

// Python's "%.1f" (round half to even on the exact binary value: printf does the same)
static std::string f1(double x) { return strf("%.1f", x); }

ThemeFiles revenge_theme_files(const Bytes& rom, int song) {
    revplay::Result r; std::string err;
    if (!revplay::theme(rom, song, r, err)) fail("Revenge's song %04X: %s", song, err.c_str());
    ThemeFiles t = theme_encode(r.pcm, r.loop_start, r.rate);
    t.about = strf("Revenge's song %04X, as Revenge plays it at his entrance (", song) + f1(t.samples / r.rate) + " s, looping from " +
              f1(r.loop_start / r.rate) + " s; the 24 built-in ports' theme too)";
    return t;
}

ThemeFiles redux_theme_files(const Bytes& rom, int song) {
    revplay::Result r; std::string err;
    if (!revplay::clip_theme(rom, song, r, err)) fail("Revenge Redux's song %02X: %s", song, err.c_str());
    ThemeFiles t = theme_encode(r.pcm, r.loop_start, r.rate);
    // (reduxmusic.py clip_theme's own words -- what theme.py write_own puts in a folder's "source"."theme"; its instrument is
    //  the song's own, song + 0x1E, as clip_theme checks)
    t.about = strf("Revenge Redux's song 0x%02X (its file 0x%04X, instrument 0x%02X): its own clip at its own speed, as loud as the "
                   "earlier themes (%.1f s, looping whole)", song, 0x3F00 + song - 0x18, song + 0x1E, (double)r.Lr / r.rate);
    return t;
}

// ---- reduxmusic.py's tables -----------------------------------------------------------------------------------------
const std::vector<ReduxPort>& redux_ports() {
    // the built-in ports: (his name, his folders' IDs, his model code in Redux, the attire whose song is his)
    static const std::vector<ReduxPort> P = {
        { "Booker T", { "booker_t", "booker_t_b" }, 0x000B, 0 },
        { "Bret Hart", { "bret_hart", "bret_hart_b" }, 0x0008, 0 },
        { "Eric Bischoff", { "eric_bischoff" }, 0x020D, 0 },
        { "Goldberg", { "goldberg" }, 0x000A, 0 },
        { "Hollywood Hogan", { "hollywood_hogan" }, 0x0201, 0 },
        { "Jim Neidhart", { "jim_neidhart" }, 0x010E, 0 },
        { "Kevin Nash", { "kevin_nash" }, 0x0203, 0 },
        { "Lex Luger", { "lex_luger" }, 0x0003, 0 },
        { "Mr Perfect", { "mr_perfect" }, 0x0207, 0 },
        { "Randy Savage", { "randy_savage", "randy_savage_b" }, 0x0202, 0 },
        { "Rey Mysterio", { "rey_mysterio", "rey_mysterio_b" }, 0x0304, 0 },
        { "Rick Steiner", { "rick_steiner" }, 0x0005, 0 },
        { "Roddy Piper", { "roddy_piper" }, 0x0007, 0 },
        { "Scott Hall", { "scott_hall" }, 0x0204, 0 },
        { "Scott Steiner", { "scott_steiner", "scott_steiner_b" }, 0x0206, 2 },
        { "Stevie Ray", { "stevie_ray", "stevie_ray_b" }, 0x0108, 0 },
        { "Sting", { "sting" }, 0x0001, 1 },
        { "The Giant", { "the_giant" }, 0x0002, 0 },
    };
    return P;
}

std::string norm_name(const std::string& s) {
    std::string o;
    for (unsigned char c : s) if (isalnum(c)) o += (char)tolower(c);
    return o;
}

static const uint32_t THEME_LIST = 0xDAF60;     // Redux's ROM: (model code, 4 songs, 0) rows up to 0xFFFF
static const int SONG0 = 0x18, SONG_LAST = 0x41;

std::optional<int> redux_song_for(const Bytes& rom, uint32_t code) {
    for (uint32_t at = THEME_LIST; at + 8 <= rom.size(); at += 8) {
        uint32_t c = be16(&rom[at]);
        if (c == 0xFFFF) break;
        if (c != code) continue;                     // (the first row of a code: setdefault)
        int attire = 0;
        for (auto& p : redux_ports()) if (p.code == code) attire = p.attire;
        int s = rom[at + 2 + attire];
        if (s >= SONG0 && s <= SONG_LAST) return s;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> redux_song_of(const Bytes& rom, const std::string& name) {
    for (auto& p : redux_ports()) if (norm_name(p.name) == norm_name(name)) return redux_song_for(rom, p.code);
    return std::nullopt;
}

std::optional<uint32_t> redux_code_of_folder(const std::string& fid, const std::string& source_code) {
    for (auto& p : redux_ports()) for (auto* f : p.folder_ids) if (fid == f) return p.code;
    if (source_code.size() > 2 && (source_code[0] == '0') && (source_code[1] == 'x' || source_code[1] == 'X'))
        return (uint32_t)strtoul(source_code.c_str() + 2, nullptr, 16);
    return std::nullopt;
}

}  // namespace ri
