// ri_content.cpp (phase3, 2026-11-19): the content pack (ri_content.h) -- mkcontent.py, step for step.
#include "ri_content.h"
#include "ri_images.h"
#include "ri_themes.h"
#include "ri_vadpcm.h"
#include "json/json.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <variant>

namespace ri {

namespace {
struct Inst { std::string name; bool numbered; uint32_t ni; uint32_t off, len; Bytes wave; uint32_t level, tune, loop; };
struct SongRow { int song, inst; Bytes script; int at; };     // (an empty script: a row whose source game isn't there)

// An extra music slot's source: a slot of No Mercy Plus's entrance table, Revenge's song, Redux's song for a name.
struct Slot { std::string key; int kind; int nm_slot; std::string arg; };   // kind 0 nm, 1 rev, 2 redux
}  // namespace

// (content2 2026-11-19, the user: the added themes play through OUR sound script, not No Mercy Plus's) -- the bytes of the
// mod's media.c theme_script (docs\THEME_SCRIPT.md: the game's script format, and why each byte), its instrument byte at 9.
// 17 bytes: the length every row of the content index has had (firstrun\hdrfill.bin's layout counts a row's leaves).
static const uint8_t THEME_SCRIPT[17] = { 0x84, 0x01, 0x00, 0x01, 0xFF, 0x01, 0xFF, 0x0F,  0x81, 0x00,  0xA3, 0x00, 0x00,
                                          0x1F,  0xFF, 0xFF,  0x80 };
static const int THEME_SCRIPT_AT = 9;

ContentPack make_content(Roms& R, const fs::path& maps, const std::string& roster_base_json, const std::string& roster_ext_h,
                         size_t user_music_rows, const ContentShape* shape) {
    ContentPack out;
    auto nmp = R.rom("nmplus_v3"), wm = R.rom("wm2000"), rv = R.rom("revenge"), van = R.rom("nomercy");
    if (!van) fail("content.bin: the No Mercy ROM isn't there");
    // (content2 2026-11-19) per source game: what a source that isn't there gives is left out -- its rows empty, in their places
    // (the build's: shape), its instruments and frames not in the pack
    if (!nmp && !shape) fail("content.bin: without No Mercy Plus V3, the build's layout of the index is needed");
    auto redux = rv ? R.rom("revenge_redux") : nullptr;
    // No Mercy Plus V3's RAM with its music bank (nmbank.pkl)
    NmBank nb{};
    if (nmp) nb = nmplus_bank(make_image("nm_pick", maps, R), R);
    const Bytes& n = nb.ram;
    const uint32_t B = nb.B, C = nb.C, NM_SONGS = 0x80470700;
    auto u32 = [&](uint32_t a) { return be32(&n[a & 0x7FFFFF]); };
    auto u16 = [&](uint32_t a) { return (uint32_t)be16(&n[a & 0x7FFFFF]); };
    auto s16 = [&](uint32_t a) { return sx16(be16(&n[a & 0x7FFFFF])); };
    auto script = [&](uint32_t k) {
        uint32_t p = u32(B + 0x18 + 8 * k), q = p;
        while (n[q & 0x7FFFFF] != 0x80) q++;
        return Bytes(n.begin() + (p & 0x7FFFFF), n.begin() + (q & 0x7FFFFF) + 1);
    };
    auto inst_of = [&](const Bytes& sc, uint32_t& ni) {
        size_t i = std::find(sc.begin(), sc.end(), 0x81) - sc.begin();
        if (i >= sc.size()) fail("content.bin: a theme script without its instrument byte");
        ni = u16(u32(B + 0x14) + 2 * sc[i + 1]);
        return (int)i + 1;
    };
    Bytes pack;
    std::vector<Inst> inst_rows;
    std::vector<uint32_t> insts;                  // No Mercy Plus instrument numbers, in our order
    std::vector<SongRow> songs;
    auto pad16 = [&]() { pack.resize(pack.size() + ((16 - pack.size() % 16) % 16), 0); };
    const uint32_t P = nmp ? u32(C + 0x2C) : 0, LV = nmp ? u32(C + 0x24) : 0, TU = nmp ? u32(C + 0x28) : 0;
    auto nm_inst_row = [&](uint32_t ni) {
        const Bytes& rom = *nmp;
        uint32_t e = u32(P + 4 * ni);
        Bytes w(0xD0, 0);
        memcpy(&w[0], &n[e & 0x7FFFFF], 0x18);
        memcpy(&w[0x18], &n[u32(e + 0x10) & 0x7FFFFF], 0x88);              // (book / loop may be shared: copied into ours)
        if (u32(e + 0x0C)) memcpy(&w[0xA0], &n[u32(e + 0x0C) & 0x7FFFFF], 0x2C);
        uint32_t base = u32(e), ln = u32(e + 4), off = (uint32_t)pack.size();
        pack.insert(pack.end(), rom.begin() + base, rom.begin() + base + ln); pad16();
        inst_rows.push_back({ "", true, ni, off, ln, w, n[(LV & 0x7FFFFF) + ni], u32(TU + 4 * ni), u32(e + 0x0C) ? 1u : 0u });
    };
    const SongRow none{ 0, 0, Bytes(), 0 };
    // ---- themes: songs 23..66 (the music slots' themes), and Funaki's (NM+ music slot 87, song 109) played as song 68
    if (!nmp) {     // (without it: their rows empty, as many as the build has before the late four's)
        if (shape->songs < shape->ext_slots + 4) fail("content.bin: the build's layout of the index");
        songs.assign(shape->songs - shape->ext_slots - 4, none);
    } else {
        std::vector<int> ss; for (int s = 23; s < 67; s++) ss.push_back(s); ss.push_back(68);
        for (int s : ss) {
            int k = s16(NM_SONGS + 2 * (s == 68 ? 109 : s));
            if (!(k >= 0 && k < 0x2C0)) continue;
            Bytes sc = script(k); uint32_t ni; int at = inst_of(sc, ni);
            if (std::find(insts.begin(), insts.end(), ni) == insts.end()) insts.push_back(ni);
            songs.push_back({ s, (int)(std::find(insts.begin(), insts.end(), ni) - insts.begin()), sc, at });
        }
    }
    for (uint32_t ni : insts) nm_inst_row(ni);
    // ---- the late four's themes over their songs (Jarrett / Meanie from WrestleMania 2000's bank, Droz / Mideon NM+'s)
    Bytes wr;
    if (wm) wr = make_image("wm2k_ram", maps, R);
    auto w32 = [&](uint32_t a) { return be32(&wr[a & 0x7FFFFF]); };
    auto w16 = [&](uint32_t a) { return (uint32_t)be16(&wr[a & 0x7FFFFF]); };
    const uint32_t WB = wm ? w32(0x800571EC) : 0, WC = wm ? w32(WB + 0x10) : 0;
    // the added themes' script: ours (THEME_SCRIPT, above) -- it was No Mercy Plus's song 23's until content2
    const Bytes tmpl(THEME_SCRIPT, THEME_SCRIPT + sizeof THEME_SCRIPT);
    const int at23 = THEME_SCRIPT_AT;
    auto wm_theme = [&](int sound) {
        const Bytes& wrom = *wm;
        uint32_t wp = w32(WB + 0x18 + 8 * sound);
        Bytes wsc(wr.begin() + (wp & 0x7FFFFF), wr.begin() + (wp & 0x7FFFFF) + 24);
        size_t i = std::find(wsc.begin(), wsc.end(), 0x81) - wsc.begin();
        uint32_t wni = w16(w32(WB + 0x14) + 2 * wsc[i + 1]);
        uint32_t e = w32(w32(WC + 0x2C) + 4 * wni), base = w32(e), ln = w32(e + 4);
        Bytes w(0xD0, 0);
        memcpy(&w[0], &wr[e & 0x7FFFFF], 0x18);
        memcpy(&w[0x18], &wr[w32(e + 0x10) & 0x7FFFFF], 0x88);
        if (w32(e + 0x0C)) memcpy(&w[0xA0], &wr[w32(e + 0x0C) & 0x7FFFFF], 0x2C);
        uint32_t off = (uint32_t)pack.size();
        pack.insert(pack.end(), wrom.begin() + base, wrom.begin() + base + ln); pad16();
        inst_rows.push_back({ strf("WM2000 %u", wni), false, 0, off, ln, w, wr[(w32(WC + 0x24) & 0x7FFFFF) + wni], w32(w32(WC + 0x28) + 4 * wni), w32(e + 0x0C) ? 1u : 0u });
        return (int)inst_rows.size() - 1;
    };
    auto nm_theme = [&](int nm_song, Bytes& sc, int& at) -> int {
        int k = s16(NM_SONGS + 2 * nm_song);
        if (!(k >= 0 && k < 0x2C0)) fail("content.bin: No Mercy Plus song %d", nm_song);
        sc = script(k); uint32_t ni; at = inst_of(sc, ni);
        if (std::find(insts.begin(), insts.end(), ni) == insts.end()) { insts.push_back(ni); nm_inst_row(ni); return (int)inst_rows.size() - 1; }
        for (size_t r = 0; r < inst_rows.size(); r++) if (inst_rows[r].numbered && inst_rows[r].ni == ni) return (int)r;
        fail("content.bin: instrument %u", ni);
    };
    const uint32_t WM_ENTRANCES = 0x8010206C;
    auto wm_slot_sound = [&](int slot) { return sx16(be16(&wr[(WM_ENTRANCES + 12 * slot + 8) & 0x7FFFFF])); };
    if (wm && (wm_slot_sound(23) != 22 || wm_slot_sound(30) != 35)) fail("content.bin: WrestleMania 2000's entrance table");
    const int CT_SONG_LATE0 = (int)songs.size();
    struct Late { int song; bool wmsrc; int x; };
    const Late LATE_SONGS[] = { { 36, true, 23 }, { 29, false, 102 }, { 65, false, 95 }, { 51, true, 30 } };
    for (auto& l : LATE_SONGS) {
        if (l.wmsrc) { if (wm) songs.push_back({ l.song, wm_theme(wm_slot_sound(l.x)), tmpl, at23 }); else songs.push_back(none); }
        else if (nmp) { Bytes sc; int at; int j = nm_theme(l.x, sc, at); songs.push_back({ l.song, j, sc, at }); }
        else songs.push_back(none);
    }
    // ---- more music slots (58 on: songs 80 on, sounds 200 on)
    const int EXT_SLOT0 = 58, SONG_EXT0 = 80, SOUND_EXT0 = 200;
    std::vector<Slot> EXT_SLOTS = { { "posse", 0, 66, "" }, { "taker99", 0, 81, "" }, { "raven", 0, 83, "" }, { "regal", 0, 84, "" }, { "tiger", 0, 13, "" } };
    for (const char* k : { "brethart", "neidhart", "hogan", "nash", "hall", "savage", "hennig", "giant", "sting", "luger", "goldberg",
                           "ricksteiner", "scottsteiner", "booker", "stevieray", "rey", "piper", "bischoff" })
        EXT_SLOTS.push_back({ k, 1, 0, "0214" });
    const uint32_t NM_ENTR = 0x22C1F0;
    // the music files (none natively), then Redux's themes for the ports Redux gives a song (in order by name)
    std::vector<std::pair<int, int>> user_music;
    if (redux) {
        std::vector<std::string> music;
        for (auto& p : redux_ports()) if (redux_song_of(*redux, p.name)) music.push_back(p.name);
        std::sort(music.begin(), music.end());
        // roster_names(): the base roster's names and shorts, then roster_ext.h's added Superstars' (name, short)
        std::vector<std::pair<std::string, int>> rn;
        auto base = nlohmann::ordered_json::parse(roster_base_json);
        for (auto& r : base) rn.push_back({ r["name"].get<std::string>(), r["id"].get<int>() });
        for (auto& r : base) rn.push_back({ r["short"].get<std::string>(), r["id"].get<int>() });
        std::regex re("\\{ 0x([0-9A-Fa-f]{4}), \\d+, \"([^\"]+)\", \"([^\"]+)\"");
        for (auto it = std::sregex_iterator(roster_ext_h.begin(), roster_ext_h.end(), re); it != std::sregex_iterator(); ++it) {
            int id = (int)strtol((*it)[1].str().c_str(), nullptr, 16);
            rn.push_back({ (*it)[2].str(), id }); rn.push_back({ (*it)[3].str(), id });
        }
        for (auto& name : music) {
            std::string key = norm_name(name);
            std::vector<int> ids;
            for (auto& [nm_, i] : rn) if (norm_name(nm_) == key) ids.push_back(i);
            if (ids.empty()) { out.log.push_back("Revenge Redux's theme for " + name + ": no Superstar of that name -- left out"); continue; }
            EXT_SLOTS.push_back({ "redux:" + name, 2, 0, name });
            user_music.push_back({ ids[0], EXT_SLOT0 + (int)EXT_SLOTS.size() - 1 });
        }
    }
    // (gen/revbanks.py: each second look's ID plays his base's music file)
    const std::pair<int, int> BANKS[] = { { 0x060D, 0x060F }, { 0x0108, 0x0115 }, { 0x010F, 0x0116 }, { 0x0110, 0x0117 }, { 0x0111, 0x0118 }, { 0x0112, 0x0119 } };
    for (auto& [base_id, id] : BANKS) {
        auto snapshot = user_music;
        for (auto& [i, sl] : snapshot) if (i == base_id) user_music.push_back({ id, sl });
    }
    while (user_music.size() < user_music_rows) user_music.push_back({ 0xFFFF, 0 });   // (nmplus: no Redux -- no one's)
    // their entrance effects: No Mercy Plus's (its slot's; the added ones its slot 66's, plain lights); without it, the added
    // ones the same plain lights as the game's own music slot 5 has them (the base ROM's entrance table: RAM 0x800F50D0, Rev 1
    // ROM 0x72A80 -- No Mercy Plus's slot 66 is a copy of it), No Mercy Plus's slots none
    std::vector<uint32_t> ext_fx;
    const uint32_t VAN_ENTR = 0x72A80;
    if (!nmp && (van->size() < VAN_ENTR + 12 * 6 || be32(&(*van)[VAN_ENTR]) != 0x0016F1F0u || be32(&(*van)[VAN_ENTR + 12]) != 0x0017F1F0u))
        fail("content.bin: the No Mercy ROM's entrance table isn't where Rev 1 has it");
    for (auto& s : EXT_SLOTS)
        ext_fx.push_back(nmp ? be32(&(*nmp)[NM_ENTR + 12 * (s.kind == 0 ? s.nm_slot : 66) + 4])
                             : s.kind == 0 ? 0u : be32(&(*van)[VAN_ENTR + 12 * 5 + 4]));
    // a theme sample as one of these instruments (theme.py's wave record; tune 0xC2400000, level 0, loops)
    auto sample_inst = [&](const std::vector<int16_t>& pcm, int64_t ls, double rate, const std::string& what) {
        VadpcmOut v = vadpcm_encode(pcm.data(), pcm.size(), (int)std::llround(rate));
        std::vector<int16_t> dec = vadpcm_decode(v.data, v.book, v.samples);
        Bytes w(0xD0, 0);
        put_be32(&w[4], (uint32_t)v.data.size()); w[9] = 1;
        memcpy(&w[0x18], v.book.data(), v.book.size());
        put_be32(&w[0xA0], (uint32_t)ls); put_be32(&w[0xA4], v.samples); put_be32(&w[0xA8], 0xFFFFFFFF);
        for (int i = 0; i < 16; i++) put_be16(&w[0xAC + 2 * i], ls >= 16 ? (uint16_t)dec[(size_t)(ls - 16 + i)] : 0);
        uint32_t off = (uint32_t)pack.size();
        pack.insert(pack.end(), v.data.begin(), v.data.end()); pad16();
        inst_rows.push_back({ what + " (" + strf("%.1f", v.samples / rate) + " s, loop from " + strf("%.1f", ls / rate) + " s)", false, 0, off,
                              (uint32_t)v.data.size(), w, 0, 0xC2400000u, 1 });
        return (int)inst_rows.size() - 1;
    };
    std::map<std::string, int> rev_insts;
    std::map<int, int> redux_insts;
    const int CT_SONG_EXT0 = (int)songs.size();
    for (size_t i = 0; i < EXT_SLOTS.size(); i++) {
        const Slot& s = EXT_SLOTS[i];
        int j; Bytes sc = tmpl; int at = at23;
        if (s.kind == 2) {
            int song = *redux_song_of(*redux, s.arg);
            if (!redux_insts.count(song)) {
                revplay::Result r; std::string err;
                if (!revplay::clip_theme(*redux, song, r, err)) fail("Redux song %02X: %s", song, err.c_str());
                redux_insts[song] = sample_inst(r.pcm, r.loop_start, r.rate, strf("Revenge Redux song 0x%02X", song));
            }
            j = redux_insts[song];
        } else if (s.kind == 1) {
            if (!rv) { songs.push_back(none); continue; }    // (Revenge's song, without Revenge: empty)
            if (!rev_insts.count(s.arg)) {
                revplay::Result r; std::string err;
                if (!revplay::theme(*rv, (int)strtol(s.arg.c_str(), nullptr, 16), r, err)) fail("Revenge song %s: %s", s.arg.c_str(), err.c_str());
                rev_insts[s.arg] = sample_inst(r.pcm, r.loop_start, r.rate, "Revenge song " + s.arg);
            }
            j = rev_insts[s.arg];
        } else if (nmp) j = nm_theme(sx16(be16(&(*nmp)[NM_ENTR + 12 * s.nm_slot])), sc, at);
        else { songs.push_back(none); continue; }          // (No Mercy Plus's slot, without it: empty)
        songs.push_back({ SONG_EXT0 + (int)i, j, sc, at });
    }
    std::vector<int> GAME_SONGS;
    for (int k = 0; k < 80; k++) GAME_SONGS.push_back(sx16(be16(&(*van)[0x800480D8 - 0x80000400 + 0x1000 + 2 * k])));
    if (GAME_SONGS[23] != 0 || GAME_SONGS[25] != 2) fail("content.bin: the game's song table");
    const size_t THEMES_SIZE = pack.size();
    // ---- videos 1..57: 72 frames each (files 0x6500 + 72 (v - 1) + i), CI4 ones widened to CI8, each picture once (No Mercy
    // Plus's: without it none -- the index then has no ct_video, all zero)
    const size_t FRAMES_OFF = pack.size();
    std::map<std::string, int> uniq;
    std::vector<std::vector<int>> vids;
    for (int v = 1; nmp && v <= 57; v++) {
        const RomFiles& F = *R.files("nmplus_v3");
        std::vector<int> row;
        for (int i = 0; i < 72; i++) {
            Bytes d = F(0x6500 + 72 * (v - 1) + i);
            if (d.size() != 4624) {
                static const uint8_t SIG[4] = { 0x04, 0x02, 0x00, 0x10 }, HDR2[8] = { 0x08, 0x02, 0x01, 0, 0, 0, 0, 0 };
                if (d.size() != 2096 || memcmp(&d[8], SIG, 4)) fail("content.bin: Titantron frame %d/%d", v, i);
                Bytes c(d.begin(), d.begin() + 8);
                c.insert(c.end(), HDR2, HDR2 + 8);
                c.insert(c.end(), d.begin() + 16, d.begin() + 48);
                c.resize(c.size() + 480, 0);
                for (size_t b = 48; b < d.size(); b++) { c.push_back(d[b] >> 4); c.push_back(d[b] & 15); }
                d = c;
            }
            std::string h = md5_hex(d);
            auto it = uniq.find(h);
            if (it == uniq.end()) { it = uniq.emplace(h, (int)uniq.size()).first; pack.insert(pack.end(), d.begin(), d.end()); }
            row.push_back(it->second);
        }
        vids.push_back(row);
    }
    out.bin = pack;
    // ---- the index: content_index.h, as mkcontent.py writes it (text mode: CRLF)
    std::vector<std::string> L;
    L.push_back("// Generated by gen/mkcontent.py from No Mercy Plus V3: the content pack's index (build/content.bin).");
    L.push_back(strf("#define CONTENT_SIZE 0x%zXu", pack.size()));
    L.push_back(strf("#define CONTENT_THEMES_SIZE 0x%zXu", THEMES_SIZE));
    L.push_back(strf("#define CONTENT_FRAMES 0x%zXu   // 64 x 64 CI8 TEX frames, 4624 bytes each, from here", FRAMES_OFF));
    L.push_back("#define CT_FRAME_SIZE 4624");
    L.push_back(strf("#define CT_INSTRUMENTS %zu", inst_rows.size()));
    L.push_back(strf("#define CT_SONGS %zu", songs.size()));
    L.push_back(strf("#define CT_SONG_LATE0 %d   // then Jeff Jarrett's, Droz's, Mideon's, The Blue Meanie's: each only while he's on", CT_SONG_LATE0));
    L.push_back("#define CT_SONG_LATE_N 4");
    L.push_back(strf("#define CT_SONG_EXT0 %d   // then the songs of the music slots made (EXT_SLOT0 on): always theirs", CT_SONG_EXT0));
    L.push_back(strf("#define EXT_SLOT0 %d", EXT_SLOT0));
    {
        std::string s = strf("#define EXT_SLOTS_N %zu   // ", EXT_SLOTS.size());
        for (size_t i = 0; i < EXT_SLOTS.size(); i++) s += (i ? ", " : "") + strf("%zu ", EXT_SLOT0 + i) + EXT_SLOTS[i].key;
        L.push_back(s);
    }
    L.push_back(strf("#define SONG_EXT0 %d", SONG_EXT0));
    L.push_back(strf("#define SOUND_EXT0 %d   // (the music bank's sounds: the game's 200, then these)", SOUND_EXT0));
    L.push_back(strf("#define SONGS_N %zu", SONG_EXT0 + EXT_SLOTS.size()));
    L.push_back("// the music files' themes (gen_data/music): the Superstar (ID), the music slot his entrance plays");
    L.push_back(strf("#define USER_MUSIC_N %zu", user_music.size()));
    {
        std::string s;
        for (size_t i = 0; i < user_music.size(); i++) s += (i ? ", " : "") + strf("{ 0x%04X, %d }", user_music[i].first, user_music[i].second);
        L.push_back(strf("static const u16 user_music[%zu][2] = { ", std::max<size_t>(1, user_music.size())) + (s.empty() ? "{ 0xFFFF, 0 }" : s) + " };");
    }
    L.push_back("// the music slots made: their entrance effects (No Mercy Plus's for them)");
    {
        std::string s;
        for (size_t i = 0; i < ext_fx.size(); i++) s += (i ? ", " : "") + strf("0x%X", ext_fx[i]);
        L.push_back("static const u32 ext_slot_fx[EXT_SLOTS_N] = { " + s + " };");
    }
    L.push_back("// The song table (the game's 0x800480D8, longer): s16 per song, its sound (or a file, >= 0x2C0; -1 none)");
    {
        std::string s;
        for (size_t i = 0; i < GAME_SONGS.size(); i++) s += (i ? ", " : "") + std::to_string(GAME_SONGS[i]);
        for (size_t i = 0; i < EXT_SLOTS.size(); i++) s += ", " + std::to_string(SOUND_EXT0 + (int)i);
        L.push_back("s16 media_songs[SONGS_N + FOLDER_THEMES] = { " + s + " };");
    }
    L.push_back("#define CT_VIDEOS 57");
    L.push_back("#define CT_VIDEO_FRAMES 72");
    L.push_back("// No Mercy Plus's theme instruments: pack offset, length, its wave table (0xD0: base, len, type /");
    L.push_back("// flags, loop, book; book +0x18, loop +0xA0), level, tuning (f32 bits), has a loop");
    L.push_back("typedef struct { u32 off, len; u8 wave[0xD0]; u8 level; u32 tune; u8 loop; } CtInstrument;");
    L.push_back("static const CtInstrument ct_inst[CT_INSTRUMENTS] = {");
    for (auto& r : inst_rows) {
        std::string w;
        for (size_t i = 0; i < r.wave.size(); i++) w += (i ? "," : "") + strf("0x%02X", r.wave[i]);
        L.push_back(strf("    { 0x%X, 0x%X, { ", r.off, r.len) + w + strf(" }, %u, 0x%08X, %u },  // ", r.level, r.tune, r.loop) +
                    (r.numbered ? strf("its instrument %u", r.ni) : r.name));
    }
    L.push_back("};");
    L.push_back("// The songs (23..66: music slots 1..44; 68: slot 46, Funaki's) and the sound script each plays (its instrument byte at");
    L.push_back("// .at is the index into ct_inst, set to the bank's number when installed)");
    L.push_back("typedef struct { u8 song, inst, at, len; u8 script[24]; } CtSong;");
    L.push_back("static const CtSong ct_song[CT_SONGS] = {");
    for (auto& s : songs) {
        if (s.script.size() > 24) fail("content.bin: a script longer than 24 bytes");
        std::string b;
        for (size_t i = 0; i < s.script.size(); i++) b += (i ? "," : "") + strf("0x%02X", s.script[i]);
        // (content2: an empty row as many leaves as a made one -- the layout counts them: 17 zeros, length 0)
        if (s.script.empty()) for (size_t i = 0; i < sizeof THEME_SCRIPT; i++) b += i ? ",0x00" : "0x00";
        L.push_back(strf("    { %d, %d, %d, %zu, { ", s.song, s.inst, s.at, s.script.size()) + b + " } },");
    }
    L.push_back("};");
    if (!vids.empty()) {
    L.push_back("// Titantron videos 1..57: each frame's number in the pack (frame f at CONTENT_FRAMES + f * 4624)");
    L.push_back("static const u16 ct_video[CT_VIDEOS][CT_VIDEO_FRAMES] = {");
    for (auto& row : vids) {
        std::string s;
        for (size_t i = 0; i < row.size(); i++) s += (i ? "," : "") + std::to_string(row[i]);
        L.push_back("    { " + s + " },");
    }
    L.push_back("};");
    }
    for (auto& l : L) out.index_h += l + "\r\n";
    out.log.push_back(strf("pack %zu bytes: themes %zu (%zu instruments, %zu songs), %zu unique frames", pack.size(), THEMES_SIZE, inst_rows.size(), songs.size(), uniq.size()));
    return out;
}

}  // namespace ri
