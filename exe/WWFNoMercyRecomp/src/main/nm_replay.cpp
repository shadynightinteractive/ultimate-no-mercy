// [replay 2026-11-14] Recording and replaying a game, and the per-retrace hashes of its structures (a QA tool; it changes
// nothing a converted function does, and nothing at all unless one of these is set):
//
//   NM_REPLAY_RECORD=<file>   record: the four pads each time the game reads them (with the retrace they were read at),
//                             and the start's save and settings (a digest of each), into <file>. The run is in lockstep
//                             (ultramodern/lockstep.hpp), paced to the wall clock unless NM_LOCKSTEP_PACE=0.
//   NM_REPLAY_PLAY=<file>     replay: the game gets the recording's pads instead of the PC's, read for read, in lockstep,
//                             as fast as it goes (NM_LOCKSTEP_PACE=1 to watch it at the game's pace). It quits at the
//                             recording's last retrace (NM_REPLAY_END=<retrace> another one; 0 never).
//   NM_REPLAY_HASH=<file>     the hashes: one line a retrace, a hash per structure of memory_groundwork's map.json (the
//                             N64's bytes of each fixed member, big-endian: the old layout's terms) and one of all the
//                             game's memory ("ram": the 8 MB and the mods' 6 MB, as the PC holds them). Beside it
//                             <file>.members.txt (the members, in order) and <file>.members.bin (each member's hash, at
//                             each retrace where one changed): tools\replay_diff.py names the first difference by them.
//   NM_REPLAY_STRUCTS=        which structures: "engine" (the default: the in-ring engine's group), "all", or a list
//                             of structure ids and groups ("wrestler,ai,match").
//   NM_REPLAY_DUMP=<r>[,<r>]  at those retraces, the hashed members' bytes into <file>.dump_<r>.bin (the field names
//                             a difference is in: replay_diff.py)
//   NM_REPLAY_POKE=<r>:<address>:<xor>[,...]   a test of the test: at retrace r, before it is hashed, the byte at that
//                             N64 address has those bits flipped (the change must show at r, in its structure).
//   NM_HEADLESS=1             no picture: the window stays hidden and the picture's thread draws nothing; the game's own
//                             work, its frame lists read as ever, is the same. Implies lockstep, not paced.
//
// Every retrace's hash is taken at the idle thread's turn, before the retrace's messages: every game thread is waiting,
// so nothing writes the memory while it is read.
#include "nm_replay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define XXH_INLINE_ALL
#include "xxHash/xxh3.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/lockstep.hpp"
#include "arcade_host.h"
#include "librecomp/export_watch.hpp"
#include "nm_replay_map.h"

extern std::atomic_int wcw_viswaps_total;      // (events.cpp: the game's pictures since it started)

namespace {

constexpr uint32_t RECORDING_VERSION = 1;
constexpr uint32_t RAM_SPAN = 0x800000u;                  // the N64's 8 MB
constexpr uint32_t MOD_RAM = 0x81000000u;                 // where the mods are loaded (librecomp addresses.hpp mod_rdram_start)
// 6 MB of it: the mods' code and data (3.3 MB: the runtime's heap starts at 0x81350BE0 with this mod) and the start of the
// heap they allocate from (NM_REPLAY_MOD_SPAN another, in bytes)
uint32_t mod_span = 0x600000u;
// (firstrun 2026-11-17, QA) NM_REPLAY_RAM_SKIP=<from>-<to>[,...]: N64 addresses in the mods' memory left out of the "ram" hash
// (each widened to whole words) -- the built-in extras' tables, zero in a mod made without them and the build's in one made
// with: comparing the two runs then says whether anything else ever differed. Unset: nothing left out.
std::vector<std::pair<uint32_t, uint32_t>> ram_skip;

struct PadRecord { uint32_t retrace, read; OSContPad pads[4]; };

struct Selected { int member; int structure; };           // a member of the map, and which of the hashed structures it is in

struct State {
    bool any = false;
    // recording
    FILE* record = nullptr;
    uint32_t reads = 0;
    // replay
    bool play = false;
    std::vector<PadRecord> pads;
    uint32_t end_retrace = 0, recorded_end = 0;
    uint64_t want_save = 0, want_settings = 0;
    uint32_t diverged = 0;
    // hashes
    FILE* hashes = nullptr;
    FILE* members = nullptr;
    std::string hash_path;
    std::vector<std::string> structures;                  // the hashed structures, in the log's column order
    std::vector<Selected> selected;
    std::vector<uint64_t> member_hash, member_last;
    std::vector<uint8_t> bytes;
    bool ram = true;
    uint32_t ram_every = 1;
    std::vector<uint32_t> dumps;
    struct Poke { uint32_t retrace, address; uint8_t bits; };
    std::vector<Poke> pokes;
    bool headless = false;
    bool quit_asked = false;
    // what the host's exports handed the mods since the last retrace (librecomp/export_watch.hpp): how many calls, and a
    // digest of which export and what it returned, in order
    uint32_t host_calls = 0;
    uint64_t host_digest = 0;
} st;

// The host's other inputs a mod reads: answers taken straight from SDL (the arcade's coin and START, the keyboard and
// mouse for the camera mod, the right stick). Each answer is only a return value, so it is recorded with the pads and
// put back in a replay, call for call.
constexpr const char* INPUT_EXPORTS[] = {
    "recomp_arcade_pad_buttons", "recomp_arcade_pad_learn_start", "recomp_get_key_down", "recomp_get_mouse_dx",
    "recomp_get_mouse_dy", "recomp_get_mouse_wheel", "recomp_get_right_analog_x", "recomp_get_right_analog_y",
    "recomp_get_right_stick_click",
};
struct InputAnswer { uint32_t retrace; uint8_t kind; uint64_t r2; uint64_t f0; };
std::vector<InputAnswer> input_answers;      // (a replay's, from the recording)
size_t input_next = 0;
int input_kind(int index, const char* name) {
    static int8_t kinds[512];
    static bool filled = false;
    if (!filled) { filled = true; std::memset(kinds, -2, sizeof kinds); }
    if (index < 0 || index >= 512) return -1;
    if (kinds[index] == -2) {
        kinds[index] = -1;
        for (int k = 0; k < (int)(sizeof INPUT_EXPORTS / sizeof INPUT_EXPORTS[0]); k++) if (!strcmp(name, INPUT_EXPORTS[k])) kinds[index] = (int8_t)k;
    }
    return kinds[index];
}

void on_export(int index, const char* name, uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const int kind = input_kind(index, name);
    if (kind >= 0) {
        const uint32_t retrace = (uint32_t)ultramodern::lockstep::retraces();
        if (st.play) {
            if (input_next < input_answers.size()) {
                const InputAnswer& a = input_answers[input_next++];
                if ((a.kind != kind || a.retrace != retrace) && st.diverged++ < 8)
                    fprintf(stderr, "[replay] host input %zu: recorded %s at retrace %u, now %s at %u -- the replay has gone another way\n",
                            input_next - 1, INPUT_EXPORTS[a.kind], a.retrace, name, retrace);
                ctx->r2 = (gpr)a.r2;
                std::memcpy(&ctx->f0, &a.f0, sizeof a.f0);
            } else {
                ctx->r2 = 0;           // (past the recording's end: nothing pressed)
                std::memset(&ctx->f0, 0, sizeof(uint64_t));
            }
        }
        if (st.record) {
            const uint8_t tag = 'I', k = (uint8_t)kind;
            uint64_t r2 = (uint64_t)ctx->r2, f0 = 0;
            std::memcpy(&f0, &ctx->f0, sizeof f0);
            fwrite(&tag, 1, 1, st.record); fwrite(&retrace, 4, 1, st.record); fwrite(&k, 1, 1, st.record);
            fwrite(&r2, 8, 1, st.record); fwrite(&f0, 8, 1, st.record);
        }
    }
    const uint64_t v[2] = { (uint64_t)index, (uint64_t)ctx->r2 };
    st.host_digest = XXH3_64bits_withSeed(v, sizeof v, st.host_digest);
    st.host_calls++;
}

// FNV-1a over a file's bytes (0 when there is none): enough to tell that a replay starts from another save or settings
uint64_t file_digest(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return 0;
    uint64_t h = 0xCBF29CE484222325ull;
    char buf[65536];
    while (in.read(buf, sizeof buf) || in.gcount() > 0) {
        for (std::streamsize i = 0; i < in.gcount(); i++) { h ^= (uint8_t)buf[i]; h *= 0x100000001B3ull; }
        if (!in) break;
    }
    return h;
}
// the save the game started from (librecomp reads it before the first retrace) and the mods' settings
uint64_t save_digest() { return file_digest(ultramodern::get_save_file_path()); }
uint64_t settings_digest() {
    uint64_t h = 0;
    std::error_code ec;
    for (const char* name : { "mod_config/nomercy_arcade.json", "mods.json" }) {
        h = h * 31 + file_digest(arcade::program_folder() / name);
        h = h * 31 + file_digest(arcade::app_folder() / name);
    }
    (void)ec;
    return h;
}

// The N64's bytes of [a, e), as the game reads them: rdram keeps each 32-bit word in the PC's order (byte n of the
// N64's memory at n ^ 3), so a whole word is the word's value, turned big-endian (a byte swap on a little-endian PC,
// nothing on a big-endian one), and the odd bytes at either end are taken one by one.
inline uint32_t big_endian(uint32_t w) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return w;
#else
    return __builtin_bswap32(w);
#endif
}
void n64_bytes(const uint8_t* rdram, uint32_t a, uint32_t e, std::vector<uint8_t>& out) {
    out.resize(e - a);
    size_t k = 0;
    uint32_t x = a;
    for (; x < e && (x & 3u); x++) out[k++] = rdram[(x ^ 3u) - 0x80000000u];
    const uint8_t* src = rdram + (x - 0x80000000u);
    uint8_t* dst = out.data() + k;
    const uint32_t words = (e - x) / 4;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w; std::memcpy(&w, src + 4 * i, 4);
        w = big_endian(w);
        std::memcpy(dst + 4 * i, &w, 4);
    }
    k += 4 * words; x += 4 * words;
    for (; x < e; x++) out[k++] = rdram[(x ^ 3u) - 0x80000000u];
}

bool wanted(const nm_replay_map::Member& m, const std::string& list) {
    if (list == "all") return true;
    size_t at = 0;
    while (at <= list.size()) {
        size_t comma = list.find(',', at);
        if (comma == std::string::npos) comma = list.size();
        const std::string w = list.substr(at, comma - at);
        if (!w.empty() && (w == m.structure || w == m.group)) return true;
        at = comma + 1;
    }
    return false;
}

void open_hashes(const char* path) {
    st.hash_path = path;
    st.hashes = fopen(path, "w");
    if (!st.hashes) { fprintf(stderr, "[replay] can't write the hashes (%s)\n", arcade::shown(path).c_str()); return; }
    const char* list = getenv("NM_REPLAY_STRUCTS");
    const std::string want = list && *list ? list : "engine";
    for (int i = 0; i < nm_replay_map::MEMBER_COUNT; i++) {
        const auto& m = nm_replay_map::MEMBERS[i];
        if (!wanted(m, want)) continue;
        int s = 0;
        while (s < (int)st.structures.size() && st.structures[s] != m.structure) s++;
        if (s == (int)st.structures.size()) st.structures.push_back(m.structure);
        st.selected.push_back({ i, s });
    }
    // (NM_REPLAY_RAM: 0 no "ram" column; n, every n-th retrace -- it is most of the hashing's cost -- with "-" on the others)
    const char* ram = getenv("NM_REPLAY_RAM");
    st.ram_every = ram ? (uint32_t)strtoul(ram, nullptr, 0) : 1u;
    st.ram = st.ram_every != 0;
    if (const char* skip = getenv("NM_REPLAY_RAM_SKIP")) {
        for (const char* s = skip; *s;) {
            char* end = nullptr;
            const uint32_t a = (uint32_t)strtoul(s, &end, 0);
            if (*end != '-') break;
            const uint32_t b = (uint32_t)strtoul(end + 1, &end, 0);
            if (a >= MOD_RAM && b > a) ram_skip.push_back({ (a - MOD_RAM) & ~3u, ((b - MOD_RAM) + 3u) & ~3u });
            s = *end == ',' ? end + 1 : end;
            if (!*end) break;
        }
        std::sort(ram_skip.begin(), ram_skip.end());
    }
    if (const char* span = getenv("NM_REPLAY_MOD_SPAN")) mod_span = (uint32_t)std::min<unsigned long>(strtoul(span, nullptr, 0), 0x10000000ul) & ~3u;
    st.member_hash.assign(st.selected.size(), 0);
    st.member_last.assign(st.selected.size(), ~0ull);
    fprintf(st.hashes, "# nm_replay hashes 1: map %s, table %s, structures %s\n", nm_replay_map::MAP_VERSION, nm_replay_map::TABLE_DIGEST, want.c_str());
    fprintf(st.hashes, "retrace,pictures,pad_reads,host_calls,host");
    for (const std::string& s : st.structures) fprintf(st.hashes, ",%s", s.c_str());
    if (st.ram) fprintf(st.hashes, ",ram");
    fprintf(st.hashes, "\n");
    // the members, in order: what members.bin's columns and a dump's bytes are
    if (FILE* t = fopen((st.hash_path + ".members.txt").c_str(), "w")) {
        fprintf(t, "# index structure member address end kind element_size (map %s, table %s)\n", nm_replay_map::MAP_VERSION, nm_replay_map::TABLE_DIGEST);
        for (size_t k = 0; k < st.selected.size(); k++) {
            const auto& m = nm_replay_map::MEMBERS[st.selected[k].member];
            fprintf(t, "%zu\t%s\t%s\t0x%08X\t0x%08X\t%s\t%u\n", k, m.structure, m.name, m.address, m.end, m.kind[0] ? m.kind : "-", m.element_size);
        }
        fclose(t);
    }
    st.members = fopen((st.hash_path + ".members.bin").c_str(), "wb");
    if (st.members) {
        const uint32_t head[3] = { 0x4D484D4Eu /* "NMHM" */, 1u, (uint32_t)st.selected.size() };
        fwrite(head, sizeof head, 1, st.members);
    }
    fprintf(stderr, "[replay] hashing %zu structures (%zu members)%s each retrace into %s\n", st.structures.size(), st.selected.size(),
            st.ram ? " and all of memory" : "", arcade::shown(path).c_str());
}

void hash_retrace(uint8_t* rdram, uint64_t retrace) {
    std::vector<uint64_t> per(st.structures.size());
    std::vector<XXH3_state_t*> acc(st.structures.size());
    for (size_t s = 0; s < acc.size(); s++) { acc[s] = XXH3_createState(); XXH3_64bits_reset(acc[s]); }
    for (size_t k = 0; k < st.selected.size(); k++) {
        const auto& m = nm_replay_map::MEMBERS[st.selected[k].member];
        n64_bytes(rdram, m.address, m.end, st.bytes);
        st.member_hash[k] = XXH3_64bits(st.bytes.data(), st.bytes.size());
        XXH3_64bits_update(acc[st.selected[k].structure], &st.member_hash[k], sizeof(uint64_t));
    }
    for (size_t s = 0; s < acc.size(); s++) { per[s] = XXH3_64bits_digest(acc[s]); XXH3_freeState(acc[s]); }
    fprintf(st.hashes, "%llu,%d,%u,%u,%016llx", (unsigned long long)retrace, wcw_viswaps_total.load(), st.reads, st.host_calls, (unsigned long long)st.host_digest);
    st.host_calls = 0; st.host_digest = 0;
    for (uint64_t h : per) fprintf(st.hashes, ",%016llx", (unsigned long long)h);
    if (st.ram && (retrace % st.ram_every) != 0) {
        fprintf(st.hashes, ",-");
    } else if (st.ram) {
        XXH3_state_t* all = XXH3_createState();
        XXH3_64bits_reset(all);
        // (the 8 MB but the PC's own values the runtime keeps in it: the threads' host contexts, lockstep.hpp host_bytes)
        size_t holes = 0;
        const ultramodern::lockstep::HostBytes* hole = ultramodern::lockstep::host_bytes(&holes);
        uint32_t from = 0;
        for (size_t i = 0; i < holes; i++) {
            if (hole[i].offset < from || hole[i].offset + hole[i].bytes > RAM_SPAN) continue;
            XXH3_64bits_update(all, rdram + from, hole[i].offset - from);
            from = hole[i].offset + hole[i].bytes;
        }
        XXH3_64bits_update(all, rdram + from, RAM_SPAN - from);
        {
            uint32_t at = 0;                     // (the mods' memory, but what NM_REPLAY_RAM_SKIP leaves out)
            for (const auto& r : ram_skip) {
                if (r.first >= mod_span || r.first < at) { at = std::max(at, std::min(r.second, mod_span)); continue; }
                XXH3_64bits_update(all, rdram + (MOD_RAM - 0x80000000u) + at, r.first - at);
                at = std::min(r.second, mod_span);
            }
            if (at < mod_span) XXH3_64bits_update(all, rdram + (MOD_RAM - 0x80000000u) + at, mod_span - at);
        }
        fprintf(st.hashes, ",%016llx", (unsigned long long)XXH3_64bits_digest(all));
        XXH3_freeState(all);
    }
    fprintf(st.hashes, "\n");
    if (st.members && st.member_hash != st.member_last) {
        const uint32_t r = (uint32_t)retrace;
        fwrite(&r, sizeof r, 1, st.members);
        fwrite(st.member_hash.data(), sizeof(uint64_t), st.member_hash.size(), st.members);
        st.member_last = st.member_hash;
    }
    if ((retrace % 60) == 0) { fflush(st.hashes); if (st.members) fflush(st.members); }
}

void dump_retrace(uint8_t* rdram, uint64_t retrace) {
    const std::string path = st.hash_path + ".dump_" + std::to_string(retrace) + ".bin";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    for (const Selected& sel : st.selected) {
        const auto& m = nm_replay_map::MEMBERS[sel.member];
        n64_bytes(rdram, m.address, m.end, st.bytes);
        fwrite(st.bytes.data(), 1, st.bytes.size(), f);
    }
    fclose(f);
    // (and all the memory "ram" hashes, as the PC holds it: tools\ram_diff.py finds where two runs' differ)
    const std::string ram_path = st.hash_path + ".ram_" + std::to_string(retrace) + ".bin";
    if (FILE* r = fopen(ram_path.c_str(), "wb")) {
        fwrite(rdram, 1, RAM_SPAN, r);
        fwrite(rdram + (MOD_RAM - 0x80000000u), 1, mod_span, r);
        fclose(r);
    }
    fprintf(stderr, "[replay] retrace %llu: the hashed members' bytes and the memory kept (%s)\n", (unsigned long long)retrace, arcade::shown(path).c_str());
}

void on_retrace(uint8_t* rdram, uint64_t retrace) {
    if (retrace == 1) {
        // (the save and settings the game started from: kept in a recording, checked by a replay)
        const uint64_t save = save_digest(), settings = settings_digest();
        if (st.record) {
            const uint8_t tag = 'S';
            fwrite(&tag, 1, 1, st.record); fwrite(&save, 8, 1, st.record); fwrite(&settings, 8, 1, st.record);
        }
        if (st.play) {
            if (st.want_save && save != st.want_save) fprintf(stderr, "[replay] the save is not the one the recording started from: the replay may not repeat\n");
            if (st.want_settings && settings != st.want_settings) fprintf(stderr, "[replay] the settings are not the recording's: the replay may not repeat\n");
        }
    }
    for (const auto& p : st.pokes) {
        if (p.retrace != retrace) continue;
        uint8_t& b = rdram[(p.address ^ 3u) - 0x80000000u];
        fprintf(stderr, "[replay] retrace %llu: the byte at 0x%08X %02X -> %02X (the poke asked for)\n", (unsigned long long)retrace, p.address, b, uint8_t(b ^ p.bits));
        b ^= p.bits;
    }
    // (the pace: the game's time against the wall clock's, and the share of it the hashing took -- in the log each 10 s
    //  of the game's time, and at the end)
    static const auto t0 = std::chrono::steady_clock::now();
    static double hash_seconds = 0;
    const auto h0 = std::chrono::steady_clock::now();
    if (st.hashes) hash_retrace(rdram, retrace);
    hash_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - h0).count();
    if ((retrace % 600) == 0 || (st.end_retrace && retrace >= st.end_retrace)) {
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        fprintf(stderr, "[replay] retrace %llu: %.0f s of the game in %.0f s (%.2f x its pace), hashing %.1f%% of that\n", (unsigned long long)retrace,
                retrace / 60.0, wall, wall > 0 ? retrace / 60.0 / wall : 0.0, wall > 0 ? 100.0 * hash_seconds / wall : 0.0);
    }
    for (uint32_t d : st.dumps) if (d == retrace) dump_retrace(rdram, retrace);
    if (st.record) {
        // (where the game has got to, each retrace: the replay ends at the last of these, so a recording cut short by the
        //  process's end -- the game quitting by itself, the window closed, the run stopped -- still says how far it went)
        const uint8_t tag = 'E';
        const uint32_t r = (uint32_t)retrace;
        fwrite(&tag, 1, 1, st.record); fwrite(&r, 4, 1, st.record);
        if ((retrace % 60) == 0) fflush(st.record);
    }
    if (st.end_retrace && retrace >= st.end_retrace && !st.quit_asked) {
        st.quit_asked = true;
        fprintf(stderr, "[replay] retrace %llu: the end of the replay (%u pad reads, %u of them at another retrace than recorded; %llu messages from outside the game's threads)\n",
                (unsigned long long)retrace, st.reads, st.diverged, (unsigned long long)ultramodern::lockstep::foreign_messages());
        if (st.hashes) fflush(st.hashes);
        if (st.members) fflush(st.members);
        ultramodern::quit();
    }
}

bool load_recording(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[replay] no recording (%s)\n", arcade::shown(path).c_str()); return false; }
    uint32_t head[2] = {};
    if (fread(head, sizeof head, 1, f) != 1 || head[0] != 0x50524D4Eu /* "NMRP" */ || head[1] != RECORDING_VERSION) {
        fprintf(stderr, "[replay] %s is not a recording of this version\n", arcade::shown(path).c_str());
        fclose(f);
        return false;
    }
    for (;;) {
        uint8_t tag = 0;
        if (fread(&tag, 1, 1, f) != 1) break;
        if (tag == 'P') {
            PadRecord r{};
            if (fread(&r.retrace, 4, 1, f) != 1 || fread(&r.read, 4, 1, f) != 1) break;
            bool ok = true;
            for (OSContPad& p : r.pads) {
                uint8_t b[6];
                if (fread(b, 6, 1, f) != 1) { ok = false; break; }
                p.button = uint16_t(b[0] | b[1] << 8); p.stick_x = int8_t(b[2]); p.stick_y = int8_t(b[3]); p.err_no = b[4];
            }
            if (!ok) break;
            st.pads.push_back(r);
        } else if (tag == 'I') {
            InputAnswer a{};
            if (fread(&a.retrace, 4, 1, f) != 1 || fread(&a.kind, 1, 1, f) != 1 || fread(&a.r2, 8, 1, f) != 1 || fread(&a.f0, 8, 1, f) != 1) break;
            if (a.kind >= sizeof INPUT_EXPORTS / sizeof INPUT_EXPORTS[0]) break;
            input_answers.push_back(a);
        } else if (tag == 'S') {
            if (fread(&st.want_save, 8, 1, f) != 1 || fread(&st.want_settings, 8, 1, f) != 1) break;
        } else if (tag == 'E') {
            if (fread(&st.recorded_end, 4, 1, f) != 1) break;
        } else {
            fprintf(stderr, "[replay] the recording has a record of an unknown kind ('%c'): read up to there\n", tag);
            break;
        }
    }
    fclose(f);
    fprintf(stderr, "[replay] playing %s: %zu pad reads, %zu host inputs, to retrace %u\n", arcade::shown(path).c_str(), st.pads.size(), input_answers.size(), st.recorded_end);
    return true;
}

std::vector<std::string> split(const char* v, char sep) {
    std::vector<std::string> out;
    if (!v) return out;
    std::string s = v, cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}

// the recording's end: the retrace the game had reached (written when the game quits, at the process's exit)
void close_recording() {
    if (!st.record) return;
    const uint8_t tag = 'E';
    const uint32_t r = (uint32_t)ultramodern::lockstep::retraces();
    fwrite(&tag, 1, 1, st.record); fwrite(&r, 4, 1, st.record);
    fclose(st.record);
    st.record = nullptr;
    fprintf(stderr, "[replay] recording ended at retrace %u (%u pad reads)\n", r, st.reads);
}

}  // namespace

bool nm_headless() {
    static const bool v = [] { const char* h = getenv("NM_HEADLESS"); return h && h[0] && !(h[0] == '0' && h[1] == '\0'); }();
    return v;
}

void nm_replay_init() {
    const char* rec = getenv("NM_REPLAY_RECORD");
    const char* play = getenv("NM_REPLAY_PLAY");
    const char* hash = getenv("NM_REPLAY_HASH");
    st.headless = nm_headless();
    if (!rec && !play && !hash && !st.headless && !ultramodern::lockstep::on()) return;
    if (!ultramodern::lockstep::on()) {
        // (a hash log or the no-graphics mode without lockstep: the hashes are still written, but a normal run's timing
        //  is the PC's, so two of them are not expected to agree)
        fprintf(stderr, "[replay] not in lockstep (NM_LOCKSTEP=1 for a repeatable run): the hashes are of this run only\n");
    }
    st.any = true;
    if (rec && *rec) {
        st.record = fopen(rec, "wb");
        if (st.record) {
            const uint32_t head[2] = { 0x50524D4Eu, RECORDING_VERSION };
            fwrite(head, sizeof head, 1, st.record);
            std::atexit(close_recording);
            fprintf(stderr, "[replay] recording the pads into %s\n", arcade::shown(rec).c_str());
        } else {
            fprintf(stderr, "[replay] can't write the recording (%s)\n", arcade::shown(rec).c_str());
        }
    }
    if (play && *play && load_recording(play)) {
        st.play = true;
        st.end_retrace = st.recorded_end;
    }
    if (const char* e = getenv("NM_REPLAY_END")) st.end_retrace = (uint32_t)strtoul(e, nullptr, 0);
    for (const std::string& d : split(getenv("NM_REPLAY_DUMP"), ',')) if (!d.empty()) st.dumps.push_back((uint32_t)strtoul(d.c_str(), nullptr, 0));
    for (const std::string& p : split(getenv("NM_REPLAY_POKE"), ',')) {
        const std::vector<std::string> f = split(p.c_str(), ':');
        if (f.size() == 3) st.pokes.push_back({ (uint32_t)strtoul(f[0].c_str(), nullptr, 0), (uint32_t)strtoul(f[1].c_str(), nullptr, 0), (uint8_t)strtoul(f[2].c_str(), nullptr, 0) });
    }
    if (hash && *hash) open_hashes(hash);
    recomp::overlays::set_export_observer(on_export);      // (before the mods load: their imports are handed out then)
    ultramodern::lockstep::set_callbacks({ .on_retrace = on_retrace });
    std::atexit([] { if (st.hashes) fflush(st.hashes); if (st.members) fflush(st.members); });
}

void nm_replay_pads(OSContPad* pads) {
    if (!st.any) return;
    const uint32_t retrace = (uint32_t)ultramodern::lockstep::retraces();
    if (st.play) {
        if (st.reads < st.pads.size()) {
            const PadRecord& r = st.pads[st.reads];
            if (r.retrace != retrace && st.diverged++ < 8)
                fprintf(stderr, "[replay] pad read %u: recorded at retrace %u, now at %u -- the replay has gone another way\n", st.reads, r.retrace, retrace);
            for (int c = 0; c < 4; c++) pads[c] = r.pads[c];
        } else {
            // (past the recording's end: the pads let go, each as connected as it last was)
            static bool said = false;
            if (!said) { said = true; fprintf(stderr, "[replay] pad read %u: past the recording's end, the pads let go\n", st.reads); }
            const OSContPad* last = st.pads.empty() ? nullptr : st.pads.back().pads;
            for (int c = 0; c < 4; c++) { pads[c].button = 0; pads[c].stick_x = 0; pads[c].stick_y = 0; pads[c].err_no = last ? last[c].err_no : pads[c].err_no; }
        }
    }
    if (st.record) {
        const uint8_t tag = 'P';
        fwrite(&tag, 1, 1, st.record); fwrite(&retrace, 4, 1, st.record); fwrite(&st.reads, 4, 1, st.record);
        for (int c = 0; c < 4; c++) {
            const uint8_t b[6] = { uint8_t(pads[c].button), uint8_t(pads[c].button >> 8), uint8_t(pads[c].stick_x), uint8_t(pads[c].stick_y), pads[c].err_no, 0 };
            fwrite(b, 6, 1, st.record);
        }
    }
    st.reads++;
}
