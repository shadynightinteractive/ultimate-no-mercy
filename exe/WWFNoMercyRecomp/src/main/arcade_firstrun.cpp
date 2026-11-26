// The first start's builder: the mod's game-data tables made again from the player's own ROM (firstrun, 2026-11-17).
//
// Why: a release of the game carries no game data (the user, 2026-11-14: "No game data shipped, rom(s) needed"). The mod as
// it is built (build_mod.ps1) has tables made from the games compiled into it -- the six-man ROM's functions it patches, the
// game's ten arenas' walls and maps, menu lettering, the song table, and the built-in extras' tables (the ported Superstars,
// No Mercy Plus's media, the arcade's arenas). So the release ships the mod with every one of those bytes
// zero (firstrun\mod_binary.holes: the HOLES), and a plan of how to fill them (firstrun\plan.bin, made by
// tools\firstrun\mkplan.py from the build), and this makes the mod from them on the player's PC before the game loads it.
//
// How each hole is filled:
//   - from the player's No Mercy (USA) (Rev 1) ROM: copies of its bytes as they are, as the six-man engine makes them (the
//     same words main.cpp six_engine_init applies), or of one of its files unpacked (AKI's LZSS); and between them the bytes
//     that are ours (the link's addresses, the six-man patches' own words, lists of file numbers);
//   - tables our generators COMPUTED from the game's data (the arenas' walls and maps, the lettering): made by code here,
//     the same computation (arcade_firstrun_ports.cpp);
//   - the built-in extras' tables: from content\modextra.bin, only when it and the content packs it goes with
//     (content.bin, wmport.bin) are all there and are what the plan says (their SHA-256). Those are
//     game data the No Mercy ROM can't give (other games' ROMs, captures, recordings): never in a release. Without them the
//     extras' tables stay zero, and so does their flag (mod/src/extras.c): the mod leaves the extras out.
// The image made is checked against the plan's SHA-256 (the build's own image, byte for byte, with or without the extras),
// then written as the mod (assets\mods\nomercy_arcade.nrm, which install_bundled_mods copies into mods\). Nothing the game
// runs comes from a guess: a ROM that isn't the one, or a plan applied wrong, is said and the game doesn't start.
//
// When: once, and again only when the plan (a new release) or the extras' files change (content\firstrun.json says what
// it was made from). A game folder without firstrun\ (a developer's install, deploy.ps1) is left alone.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "miniz.h"
#include "json/json.hpp"
#include "arcade_host.h"
#include "arcade_firstrun_ports.h"
#include "arcade_startup_status.h"
#include "../romimport/ri_build.h"

std::span<const uint32_t[2]> arcade_six_rom_patch();     // (main.cpp: the six-man engine's words)

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

// ---- SHA-256 (FIPS 180-4) -------------------------------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t block[64]; size_t used = 0; uint64_t total = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void add(const uint8_t* p, size_t n) {
        total += n;
        while (n) {
            const size_t take = std::min(n, size_t(64) - used);
            memcpy(block + used, p, take); used += take; p += take; n -= take;
            if (used == 64) { compress(block); used = 0; }
        }
    }
    std::array<uint8_t, 32> digest() {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (used != 56) add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - 8 * i));
        add(len, 8);
        std::array<uint8_t, 32> out;
        for (int i = 0; i < 8; i++) for (int k = 0; k < 4; k++) out[4 * i + k] = uint8_t(h[i] >> (24 - 8 * k));
        return out;
    }
};
std::array<uint8_t, 32> sha256(std::span<const uint8_t> b) { Sha256 s; s.add(b.data(), b.size()); return s.digest(); }
std::string hex(std::span<const uint8_t> b, size_t n = 32) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < b.size() && i < n; i++) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
    return s;
}

// (firstrun3 2026-11-18) Another program may hold a file in the game's folder for a moment while the builder reads or writes
// it -- an antivirus scan, a backup or sync tool, a copy of the folder -- and Windows then refuses the open or the rename.
// So each read of a file that is there, and each write (the temporary file and its rename), is tried again a few times,
// waiting a little longer each time (about 5 s in all) before it counts as failed.
template <class F> bool with_retries(F&& attempt) {
    for (int k = 0;; k++) {
        if (attempt()) return true;
        if (k == 9) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(100 * (k + 1), 1000)));
    }
}
Bytes read_all(const fs::path& p) {
    std::error_code ec;
    Bytes out;
    with_retries([&] {
        std::ifstream f(p, std::ios::binary);
        if (!f.good()) return !fs::exists(p, ec);   // (a file that isn't there: nothing to wait for)
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !f.bad();
    });
    return out;
}
bool write_all(const fs::path& p, std::span<const uint8_t> b) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = fs::path(p).concat(".tmp");
    const bool ok = with_retries([&] {
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f.good()) return false;
            f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
            if (!f.good()) return false;
        }
        fs::rename(tmp, p, ec);
        return !ec;
    });
    if (!ok) fs::remove(tmp, ec);
    return ok;
}
void log(const std::string& s) { fprintf(stderr, "[firstrun] %s\n", s.c_str()); fflush(stderr); }

// ---- the player's ROM -----------------------------------------------------------------------------------------------
// In the ROM's own (.z64) byte order, whatever order the file is in; empty if it isn't an N64 ROM.
Bytes rom_order(Bytes b) {
    if (b.size() < 0x40 || b.size() % 4) return {};
    if (b[0] == 0x80 && b[1] == 0x37 && b[2] == 0x12 && b[3] == 0x40) return b;
    if (b[0] == 0x37 && b[1] == 0x80 && b[2] == 0x40 && b[3] == 0x12) { for (size_t i = 0; i + 1 < b.size(); i += 2) std::swap(b[i], b[i + 1]); return b; }
    if (b[0] == 0x40 && b[1] == 0x12 && b[2] == 0x37 && b[3] == 0x80) {
        for (size_t i = 0; i + 3 < b.size(); i += 4) { std::swap(b[i], b[i + 3]); std::swap(b[i + 1], b[i + 2]); }
        return b;
    }
    return {};
}
// The No Mercy ROM the plan was made against (its SHA-256 in .z64 order), from where the game looks for one: the copy the
// game kept on an earlier start, the roms folder, the game's folder (main.cpp standalone_select_rom's places).
Bytes find_rom(const std::array<uint8_t, 32>& want) {
    std::vector<fs::path> where;
    std::error_code ec;
    const fs::path kept = arcade::app_folder() / "wwf.nomercy.us.z64";
    if (fs::is_regular_file(kept, ec)) where.push_back(kept);
    for (const fs::path& folder : { arcade::program_folder() / "roms", arcade::program_folder(), arcade::app_folder() }) {
        for (const auto& e : fs::directory_iterator(folder, ec)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(tolower((unsigned char)c));
            if (e.is_regular_file(ec) && (ext == ".z64" || ext == ".n64" || ext == ".v64") && e.file_size(ec) == 32u * 1024 * 1024) where.push_back(e.path());
        }
    }
    for (const fs::path& p : where) {
        Bytes b = rom_order(read_all(p));
        if (!b.empty() && sha256(b) == want) { log("the ROM: " + arcade::shown(p)); return b; }
    }
    return {};
}

// AKI's LZSS, as the game's file code unpacks a file (handlers/aki/romtable.py; tools/firstrun/mkplan.py lzss): a 4 KB ring
// from 0xFEE, a flag byte for every 8 items (low bit first: 1 a byte as it is, 0 a 12-bit place and a 4-bit length + 3).
Bytes lzss(std::span<const uint8_t> src, size_t n) {
    Bytes out(n, 0);
    uint8_t ring[0x1000] = {};
    size_t r = 0xFEE, i = 4, o = 0;
    unsigned flags = 0;
    while (o < n && i < src.size()) {
        flags >>= 1;
        if (!(flags & 0x100)) flags = src[i++] | 0xFF00u;
        if (flags & 1) {
            if (i >= src.size()) break;
            const uint8_t c = src[i++]; out[o++] = c; ring[r] = c; r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= src.size()) break;
            const unsigned lo = src[i], hi = src[i + 1]; i += 2;
            const unsigned pos = lo | ((hi & 0xF0u) << 4), len = (hi & 0xFu) + 3;
            for (unsigned k = 0; k < len; k++) {
                const uint8_t c = ring[(pos + k) & 0xFFF];
                if (o < n) out[o++] = c;
                ring[r] = c; r = (r + 1) & 0xFFF;
            }
        }
    }
    return out;
}
uint32_t be32(std::span<const uint8_t> b, size_t at) { return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]; }
// File i (1 .. 19458) of No Mercy (Rev 1)'s file table, unpacked (the table: 0x16C31D8, 19459 entries, from 0x1BD150).
Bytes aki_file(std::span<const uint8_t> rom, uint32_t i) {
    constexpr uint32_t TABLE = 0x16C31D8, ENTRIES = 19459, BASE = 0x1BD150;
    if (i < 1 || i >= ENTRIES) return {};
    const uint32_t t0 = be32(rom, TABLE + 4 * (i - 1)), t1 = be32(rom, TABLE + 4 * i);
    const size_t a = std::min<size_t>((t0 & ~1u) + size_t(BASE), rom.size()), b = std::min<size_t>((t1 & ~1u) + size_t(BASE), rom.size());
    std::span<const uint8_t> raw = b > a ? rom.subspan(a, b - a) : std::span<const uint8_t>();
    if ((t0 & 1) && raw.size() >= 4) {
        const uint32_t n = be32(raw, 0);
        if (n <= 0x400000u) return lzss(raw, n);
    }
    return Bytes(raw.begin(), raw.end());
}

// ---- the plan (tools/firstrun/mkplan.py writes it; little-endian) ----------------------------------------------------
struct Hole { uint32_t start, size; uint8_t extras; std::string name; };
struct PortHole { uint32_t start, size; std::string port, name; };
struct Pack { std::string name; std::array<uint8_t, 32> sha; uint64_t size; };
struct Plan {
    uint32_t image_size = 0, rom_size = 0;
    std::array<uint8_t, 32> rom_sha, holed_sha, base_sha, full_sha;
    std::vector<Pack> packs;
    std::vector<Hole> holes;
    std::vector<PortHole> ports;
    std::span<const uint8_t> ops;    // (the ops, as they are in the file)
    Bytes file;
};
struct Reader {
    std::span<const uint8_t> b; size_t at = 0; bool bad = false;
    bool need(size_t n) { if (at + n > b.size()) bad = true; return !bad; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v; memcpy(&v, b.data() + at, 4); at += 4; return v; }
    uint64_t u64() { if (!need(8)) return 0; uint64_t v; memcpy(&v, b.data() + at, 8); at += 8; return v; }
    uint8_t u8() { if (!need(1)) return 0; return b[at++]; }
    std::array<uint8_t, 32> sha() { std::array<uint8_t, 32> s{}; if (need(32)) { memcpy(s.data(), b.data() + at, 32); at += 32; } return s; }
    std::string str(size_t n) { if (!need(n)) return {}; std::string s(reinterpret_cast<const char*>(b.data() + at), n); at += n; s.resize(strnlen(s.c_str(), n)); return s; }
};
bool read_plan(const fs::path& p, Plan& plan, std::string& why) {
    plan.file = read_all(p);
    Reader r{ plan.file };
    if (plan.file.size() < 8 || memcmp(plan.file.data(), "UNMPLAN1", 8)) { why = "not a plan"; return false; }
    r.at = 8;
    if (r.u32() != 1) { why = "a plan of another version"; return false; }
    plan.image_size = r.u32(); plan.rom_size = r.u32();
    plan.rom_sha = r.sha(); plan.holed_sha = r.sha(); plan.base_sha = r.sha(); plan.full_sha = r.sha();
    const uint32_t np = r.u32();
    for (uint32_t i = 0; i < np && !r.bad; i++) { Pack k; k.name = r.str(32); k.sha = r.sha(); k.size = r.u64(); plan.packs.push_back(k); }
    const uint32_t nh = r.u32();
    for (uint32_t i = 0; i < nh && !r.bad; i++) { Hole h; h.start = r.u32(); h.size = r.u32(); h.extras = r.u8(); r.at += 3; h.name = r.str(32); plan.holes.push_back(h); }
    const uint32_t nports = r.u32();
    for (uint32_t i = 0; i < nports && !r.bad; i++) { PortHole h; h.start = r.u32(); h.size = r.u32(); h.port = r.str(16); h.name = r.str(32); plan.ports.push_back(h); }
    const uint32_t nops = r.u32();
    (void)nops;
    if (r.bad) { why = "a plan cut short"; return false; }
    plan.ops = std::span<const uint8_t>(plan.file).subspan(r.at);
    return true;
}

// The stage sets' files made (file id -> bytes): apply_base makes them, write_stage_pack writes them.
std::map<uint32_t, Bytes> stage_files;

// content\wmport_base.bin: the mod's pack as the game's own arenas need it -- wmport.bin's size, the stage sets' files at
// their places in it (the pack's file table, wmp_files: {offset, size} for file 0x5000 + row, in the mod's image), zeros
// elsewhere. The game's pack loader reads it in wmport.bin's place when that isn't there (arcade_packs.cpp).
bool write_stage_pack(const Plan& plan, const Bytes& image, const fs::path& to, std::string& why) {
    const Pack* wm = nullptr;
    for (const Pack& k : plan.packs) if (k.name == "wmport.bin") wm = &k;
    // (the table's rows of the stage sets' files are a base hole; the rows before them, the extras' hole just before it -- so
    //  the table starts where that one does)
    const Hole* table = nullptr;
    uint32_t table_start = 0;
    for (const Hole& h : plan.holes) if (h.name == "wmp_files" && !h.extras) { table = &h; break; }
    if (table) for (const Hole& h : plan.holes) if (h.name == "wmp_files" && h.extras && h.start + h.size == table->start) table_start = h.start;
    if (!wm || !wm->size || !table || !table_start) { why = "the plan has no pack table"; return false; }
    Bytes pack(size_t(wm->size), 0);
    for (const auto& [id, data] : stage_files) {
        const size_t row = table_start + size_t(id - 0x5000) * 8;
        if (row < table->start || row + 8 > size_t(table->start) + table->size) { why = "a stage set's file outside the pack table's rows"; return false; }
        const uint32_t off = be32(image, row), size = be32(image, row + 4);
        if (size != data.size() || uint64_t(off) + size > pack.size()) { why = "a stage set's file doesn't fit its place in the pack"; return false; }
        memcpy(pack.data() + off, data.data(), size);
    }
    if (!write_all(to, pack)) { why = "can't write " + arcade::shown(to); return false; }
    return true;
}

// The base holes filled: the ops (copies, our bytes, zeros), then the ports. False + why if anything is out of place.
bool apply_base(const Plan& plan, std::span<const uint8_t> rom, std::span<const uint8_t> six, Bytes& image, std::string& why) {
    std::map<uint32_t, Bytes> files;      // (each file unpacked once; a map keeps the references handed out good)
    auto file = [&](uint32_t i) -> const Bytes& {
        auto at = files.find(i);
        if (at == files.end()) at = files.emplace(i, aki_file(rom, i)).first;
        return at->second;
    };
    Reader r{ plan.ops };
    size_t n_ops = 0;
    while (r.at < r.b.size() && !r.bad) {
        const uint8_t kind = r.u8();
        const uint32_t dst = r.u32();
        if (kind == 1) {
            const uint32_t src = r.u32(), off = r.u32(), len = r.u32();
            std::span<const uint8_t> from = src == 0 ? rom : src == 1 ? six : std::span<const uint8_t>(file(src - 1));
            if (r.bad || uint64_t(off) + len > from.size() || uint64_t(dst) + len > image.size()) { why = "a copy out of place"; return false; }
            memcpy(image.data() + dst, from.data() + off, len);
        } else if (kind == 2) {
            const uint32_t len = r.u32();
            if (!r.need(len) || uint64_t(dst) + len > image.size()) { why = "our bytes out of place"; return false; }
            memcpy(image.data() + dst, r.b.data() + r.at, len); r.at += len;
        } else if (kind == 3) {
            const uint32_t len = r.u32();
            if (r.bad || uint64_t(dst) + len > image.size()) { why = "zeros out of place"; return false; }
            memset(image.data() + dst, 0, len);
        } else { why = "an op the plan shouldn't have"; return false; }
        n_ops++;
    }
    if (r.bad) { why = "the plan's ops cut short"; return false; }
    std::vector<firstrun::Table> all;     // (every table the plan fills, base and extras, by its symbol: what a port may read)
    for (const Hole& k : plan.holes) all.push_back({ k.name, k.start + 0x81000000u, k.size });
    // the game's ten arenas' stage sets: files the mod's pack (wmport.bin) holds from 0x5000 on, made from No Mercy's own
    // (arcade_firstrun_stages.cpp) -- the ports below read them as files 0x5000 and up, and the game reads them from
    // content\wmport_base.bin (write_stage_pack) when the extras' wmport.bin isn't there
    firstrun::Rom pr{ rom, six, [&](uint32_t i) -> const Bytes& {
        static const Bytes none;
        if (i < 0x5000) return file(i);
        const auto at = stage_files.find(i);
        return at == stage_files.end() ? none : at->second;
    } };
    if (!firstrun::make_stage_files(pr, all, image, stage_files, why)) { why = "the arenas' stage sets: " + why; return false; }
    // the ports: each with every table it makes
    std::vector<std::string> done;
    for (const PortHole& h : plan.ports) {
        if (std::find(done.begin(), done.end(), h.port) != done.end()) continue;
        done.push_back(h.port);
        std::vector<firstrun::Table> tables;
        for (const PortHole& k : plan.ports) if (k.port == h.port) tables.push_back({ k.name, k.start + 0x81000000u, k.size });
        std::string w;
        if (!firstrun::run_port(h.port, pr, tables, all, image, w)) { why = "the " + h.port + " tables: " + w; return false; }
    }
    log("the mod's tables from the ROM: " + std::to_string(n_ops) + " steps, " + std::to_string(done.size()) + " made by code");
    return true;
}

// The extras: content\modextra.bin and the four packs, all there and as the plan says. Fills the image's extras holes.
bool extras_there(const Plan& plan, const fs::path& content, bool check_sha, std::string& why) {
    std::error_code ec;
    if (!fs::is_regular_file(content / "modextra.bin", ec)) { why = "content\\modextra.bin isn't there"; return false; }
    for (const Pack& k : plan.packs) {
        const fs::path p = content / k.name;
        if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != k.size) { why = "content\\" + k.name + (fs::exists(p, ec) ? " isn't the one this game was made with" : " isn't there"); return false; }
        if (check_sha && sha256(read_all(p)) != k.sha) { why = "content\\" + k.name + " isn't the one this game was made with (its SHA-256)"; return false; }
    }
    return true;
}
bool apply_extras(const Plan& plan, const fs::path& content, Bytes& image, std::string& why) {
    const Bytes x = read_all(content / "modextra.bin");
    Reader r{ x };
    if (x.size() < 8 + 32 + 4 || memcmp(x.data(), "UNMXTRA1", 8)) { why = "content\\modextra.bin isn't the extras' file"; return false; }
    r.at = 8;
    // (its payload's own SHA-256: whether it is this build's extras, the whole image's SHA-256 says after it is applied)
    const auto want = r.sha();
    if (sha256(std::span<const uint8_t>(x).subspan(r.at + 4)) != want) { why = "content\\modextra.bin is damaged"; return false; }
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && !r.bad; i++) {
        const uint32_t s = r.u32(), len = r.u32();
        const bool hole = std::any_of(plan.holes.begin(), plan.holes.end(), [&](const Hole& h) { return h.extras && h.start == s && h.size == len; });
        if (!hole || !r.need(len) || uint64_t(s) + len > image.size()) { why = "content\\modextra.bin doesn't fit this mod's holes"; return false; }
        memcpy(image.data() + s, x.data() + r.at, len); r.at += len;
    }
    if (r.bad) { why = "content\\modextra.bin is cut short"; return false; }
    return true;
}

// ---- the mod written: a zip of its three members, stored, as the game's mod loader reads them (miniz) ---------------
Bytes zip_stored(const std::vector<std::pair<std::string, std::span<const uint8_t>>>& members) {
    Bytes z, cd;
    auto put16 = [](Bytes& b, uint32_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    auto put32 = [](Bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); };
    for (const auto& [name, data] : members) {
        const uint32_t crc = uint32_t(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
        const uint32_t at = uint32_t(z.size());
        put32(z, 0x04034b50); put16(z, 20); put16(z, 0); put16(z, 0); put16(z, 0); put16(z, 0x0021);    // (1980-01-01 00:00, as the build's)
        put32(z, crc); put32(z, uint32_t(data.size())); put32(z, uint32_t(data.size())); put16(z, uint32_t(name.size())); put16(z, 0);
        z.insert(z.end(), name.begin(), name.end()); z.insert(z.end(), data.begin(), data.end());
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0x0021);
        put32(cd, crc); put32(cd, uint32_t(data.size())); put32(cd, uint32_t(data.size())); put16(cd, uint32_t(name.size()));
        put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0x01800000u); put32(cd, at);
        cd.insert(cd.end(), name.begin(), name.end());
    }
    const uint32_t cd_at = uint32_t(z.size());
    z.insert(z.end(), cd.begin(), cd.end());
    put32(z, 0x06054b50); put16(z, 0); put16(z, 0); put16(z, uint32_t(members.size())); put16(z, uint32_t(members.size()));
    put32(z, uint32_t(cd.size())); put32(z, cd_at); put16(z, 0);
    return z;
}

[[noreturn]] void give_up(const std::string& why) {
    log(why);
    // (firstrun3 2026-11-18) the reason kept in content\firstrun.json with the state "failed" -- the record's other keys (the
    //  commentary builder's among them) as they are -- so the next start makes the mod again and the player can see why
    {
        const fs::path note = arcade::program_folder() / "content" / "firstrun.json";
        nlohmann::json rec = nlohmann::json::object();
        const Bytes b = read_all(note);
        try { if (!b.empty()) rec = nlohmann::json::parse(std::string(b.begin(), b.end())); } catch (const nlohmann::json::exception&) {}
        if (!rec.is_object()) rec = nlohmann::json::object();
        rec["state"] = "failed"; rec["why"] = why;
        const std::string s = rec.dump(1) + "\n";
        write_all(note, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    }
    arcade::message_box(("The game couldn't make its mod from your ROM:\n\n" + why +
                         "\n\nIt will try again the next time the game starts. See README.txt.").c_str());
    std::exit(1);
}
}  // namespace

// The six-man ROM: the player's ROM with the six-man engine's words (main.cpp's table), as six_engine_init makes it.
static Bytes six_of(std::span<const uint8_t> rom) {
    Bytes six(rom.begin(), rom.end());
    for (const auto& p : arcade_six_rom_patch()) {
        if (size_t(p[0]) + 4 > six.size()) continue;
        six[p[0]] = uint8_t(p[1] >> 24); six[p[0] + 1] = uint8_t(p[1] >> 16); six[p[0] + 2] = uint8_t(p[1] >> 8); six[p[0] + 3] = uint8_t(p[1]);
    }
    return six;
}

// A built-in content pack's size, as the plan has it (0: unknown, or no firstrun -- a developer's install): the pack loader
// keeps that much room for one that isn't there, so the packs after it are where they would be (arcade_packs.cpp).
uint64_t arcade_firstrun_pack_size(const std::string& name) {
    static Plan plan;
    static bool read = false;
    if (!read) { read = true; std::string why; if (!read_plan(arcade::program_folder() / "firstrun" / "plan.bin", plan, why)) plan.packs.clear(); }
    for (const Pack& k : plan.packs) if (k.name == name) return k.size;
    return 0;
}

// ---- when to build: the record of processed inputs (content\firstrun.json) ------------------------------------------
// (the user's rule, 2026-11-18) The builder works only on the first start, or when a later start finds a NEW input: a ROM or
// pack in roms\ (or the game's folder), a content pack in content\. What it made stays the player's own: never made again
// and never written over -- except the mod itself and content\wmport_base.bin, which the game can't run without, when a new
// release (another plan) or new extras need them made again, or they are missing. Each start first lists the inputs by size
// and date only; a file's SHA-256 is taken only when it is new or changed. Nothing new: no work and no file written. A build
// that fails is marked "failed" and tried again next start; outputs are written whole (a temporary file, then renamed).
// content\reimport.txt (the player's "make it again") makes everything again once and is removed.
namespace {
struct Input { std::string rel; uint64_t size = 0; long long time = 0; std::string sha; fs::path path; };
long long file_time(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : (long long)t.time_since_epoch().count();
}
std::vector<Input> list_inputs() {
    std::vector<Input> out;
    std::error_code ec;
    const fs::path top = arcade::program_folder();
    for (const fs::path& folder : { top / "roms", top }) {
        for (const auto& e : fs::directory_iterator(folder, ec)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(tolower((unsigned char)c));
            const bool rom = ext == ".z64" || ext == ".n64" || ext == ".v64";
            const bool pack = ext == ".zip" || ext == ".xdelta" || ext == ".rvz" || ext == ".iso";
            if (!e.is_regular_file(ec) || !(rom || (folder != top && pack))) continue;
            out.push_back({ arcade::shown(e.path()), uint64_t(e.file_size(ec)), file_time(e.path()), "", e.path() });
        }
    }
    for (const char* name : { "modextra.bin", "content.bin", "wmport.bin" }) {   // (no2k15: hd.bin, hd_rock.bin gone)
        const fs::path p = top / "content" / name;
        if (fs::is_regular_file(p, ec)) out.push_back({ arcade::shown(p), uint64_t(fs::file_size(p, ec)), file_time(p), "", p });
    }
    std::sort(out.begin(), out.end(), [](const Input& a, const Input& b) { return a.rel < b.rel; });
    return out;
}
void write_record(const fs::path& note, const nlohmann::json& rec) {
    const std::string s = rec.dump(1) + "\n";
    write_all(note, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}
}  // namespace

void arcade_firstrun_build() {
    const char* off = getenv("NM_FIRSTRUN");
    if (off && strcmp(off, "0") == 0) return;
    const fs::path top = arcade::program_folder(), dir = top / "firstrun", content = top / "content";
    const fs::path out = top / "assets" / "mods" / "nomercy_arcade.nrm", note = content / "firstrun.json", stage = content / "wmport_base.bin";
    std::error_code ec;
    if (!fs::is_regular_file(dir / "plan.bin", ec)) return;                   // (no firstrun\: a developer's install)
    Plan plan;
    std::string why;
    if (!read_plan(dir / "plan.bin", plan, why)) give_up("firstrun\\plan.bin: " + why);
    const std::string plan_sha = hex(sha256(plan.file));
    // the record, and the inputs as they are now (size and date only)
    nlohmann::json rec = nlohmann::json::object();
    {
        const Bytes b = read_all(note);
        try { if (!b.empty()) rec = nlohmann::json::parse(std::string(b.begin(), b.end())); } catch (const nlohmann::json::exception&) { rec = nlohmann::json::object(); }
        // (the file is shared with the commentary builder: its keys are kept as they are -- only this builder's own are read
        //  and written here)
        if (!rec.is_object()) rec = nlohmann::json::object();
        if (!rec.contains("inputs") || !rec["inputs"].is_object()) rec["inputs"] = nlohmann::json::object();
    }
    const bool reimport = fs::is_regular_file(content / "reimport.txt", ec);
    std::vector<Input> now;
    bool changed = false;
    auto compare_inputs = [&]() {
        now = list_inputs();
        changed = false;
        for (Input& in : now) {
            const bool known = rec.contains("inputs") && rec["inputs"].contains(in.rel);
            if (known && rec["inputs"][in.rel].value("size", uint64_t(0)) == in.size && rec["inputs"][in.rel].value("time", 0ll) == in.time)
                in.sha = rec["inputs"][in.rel].value("sha256", std::string());
            else changed = true;
        }
        if (rec.contains("inputs"))
            for (const auto& item : rec["inputs"].items())
                if (std::none_of(now.begin(), now.end(), [&](const Input& i) { return i.rel == item.key(); })) changed = true;   // (one taken out)
    };
    compare_inputs();
    // (phase3 2026-11-19) The other games' extras, made from their ROMs in roms\ by the ROM importer (src\romimport\ri_build):
    // the player's wrestler folders once per source ROM, the game's own packs from the set of ROMs there. Only when an input is
    // new or changed, the player asked (content\reimport.txt), or the last import didn't finish; then the inputs are listed
    // again (the packs it made are the mod's inputs).
    // (startup 2026-11-19) the start-up status window: at once on a first start or a re-import; otherwise once the importer or the
    // mod maker below has something to make (arcade_startup_status.h)
    using startup_status::Kind;
    if (rec.value("state", std::string()) != "done" || reimport) startup_status::say(Kind::Making, "a look at the games in roms\\");
    // (relprep 2026-11-23) a new release whose extras layout (firstrun\hdrfill.bin) is not the one content\modextra.bin was
    // made in: the importer makes modextra.bin again (ri_build.cpp, "modextra_layout"). An install from before that record
    // has none, so it counts as different once.
    // (relprep) And a pack the importer made that isn't in content\ any more: the move libraries (movelib_*.bin) are not
    // inputs above, so one taken out stayed out -- the importer makes it again (ri_build.cpp: a missing pack).
    bool layout_new = false, pack_gone = false;
    if (rec.contains("romimport") && rec["romimport"].is_object()) {
        const Bytes layout = read_all(dir / "hdrfill.bin");
        layout_new = !layout.empty() && rec["romimport"].value("modextra_layout", std::string()) != hex(sha256(layout));
        if (rec["romimport"].contains("packs") && rec["romimport"]["packs"].is_object())
            for (const auto& item : rec["romimport"]["packs"].items())
                if (!fs::is_regular_file(content / item.key(), ec)) { pack_gone = true; log("content\\" + item.key() + " isn't there: made again"); }
    }
    if (fs::is_directory(dir / "romimport", ec) && (changed || reimport || layout_new || pack_gone || rec.value("romimport_state", std::string()) != "done")) {
        rec["romimport_state"] = "failed";
        write_record(note, rec);
        ri::BuildPaths P{ top, top / "roms", content, top / "wrestlers", dir / "romimport" };
        P.say = [](ri::Say k, const std::string& t) {
            if (k == ri::Say::Plan) startup_status::plan(atoi(t.c_str()));
            else if (k == ri::Say::Step) startup_status::step();
            else startup_status::say(Kind(int(k)), t);      // (ri::Say's lines are startup_status::Kind's, in order)
        };
        try {
            const ri::BuildResult r = ri::romimport_build(P, rec, [&]() { write_record(note, rec); });
            for (const std::string& l : r.log) log("[romimport] " + l);
            rec["romimport_state"] = "done";
        } catch (const std::exception& e) {
            log(std::string("[romimport] stopped (tried again next start): ") + e.what());
        }
        write_record(note, rec);
        compare_inputs();
    }
    const bool built = rec.value("plan", std::string()) == plan_sha && rec.value("state", std::string()) == "done" &&
                       fs::is_regular_file(out, ec) && fs::is_regular_file(stage, ec);
    if (built && !changed && !reimport) return;                                // (nothing new: no work, no file written)
    // only the new and changed inputs fingerprinted
    for (Input& in : now) {
        if (!in.sha.empty()) continue;
        std::string ext = in.path.extension().string();
        for (char& c : ext) c = char(tolower((unsigned char)c));
        if (ext == ".rvz" || ext == ".iso") {
            // (a disc image is gigabytes: its size, its first and last MB -- enough to tell one from another, read in a moment;
            //  the commentary builder, which reads it, keeps its own record)
            std::ifstream f(in.path, std::ios::binary);
            Sha256 s;
            const uint64_t part = std::min<uint64_t>(in.size, 1u << 20);
            std::vector<char> buf(static_cast<size_t>(part));
            f.read(buf.data(), std::streamsize(part)); s.add(reinterpret_cast<const uint8_t*>(buf.data()), size_t(f.gcount()));
            f.clear(); f.seekg(std::streamoff(in.size - part));
            f.read(buf.data(), std::streamsize(part)); s.add(reinterpret_cast<const uint8_t*>(buf.data()), size_t(f.gcount()));
            const std::string size = std::to_string(in.size);
            s.add(reinterpret_cast<const uint8_t*>(size.data()), size.size());
            in.sha = "ends:" + hex(s.digest());
        } else {
            const Bytes b = read_all(in.path);
            const Bytes r = rom_order(b);
            in.sha = hex(sha256(r.empty() ? std::span<const uint8_t>(b) : std::span<const uint8_t>(r)));   // (a ROM in its own byte order)
        }
        log("new: " + in.rel + " (" + in.sha.substr(0, 21) + "...)");
    }
    auto inputs_json = [&]() {
        nlohmann::json j = nlohmann::json::object();
        for (const Input& in : now) j[in.rel] = { { "size", in.size }, { "time", in.time }, { "sha256", in.sha } };
        return j;
    };
    // the built-in extras: their packs there and as built. (no2k15, 2026-11-23: the WWE 2K15 test slots' packs, hd.bin and
    // hd_rock.bin, optional here before -- no ROM gave them; bit 16 -- are gone with the slots.) (phase3 2026-11-19) A pack
    // the ROM importer made from the player's source ROMs (its record) counts as there: made from fewer sources than the
    // developer's, it differs from the plan's -- its absent sources' parts zero, and the mod leaves out what needs them.
    bool extras = true, native_packs = false;   // (native: a pack the importer made, not the developer's)
    // (nmplus 2026-11-19) the content pack is No Mercy Plus V3's themes and videos: the importer makes it only with that ROM,
    // and without it the rest of the extras are in, No Mercy Plus's left out (the mod's EXTRAS_NMPLUS bit off)
    bool content_in = true;
    std::string w;
    auto importer_made = [&](const std::string& name, const std::string& sha) {
        return rec.contains("romimport") && rec["romimport"].contains("packs") && rec["romimport"]["packs"].value(name, std::string()) == sha;
    };
    for (const Pack& k : plan.packs) {
        if (k.name == "hd.bin" || k.name == "hd_rock.bin") continue;   // (no2k15: an older plan's 2K15 packs -- gone, not needed)
        const std::string rel = arcade::shown(content / k.name);
        const auto f = std::find_if(now.begin(), now.end(), [&](const Input& i) { return i.rel == rel; });
        const bool nmp_less = k.name == "content.bin" && rec.contains("romimport") && rec["romimport"].contains("packs_sources") &&
                              (rec["romimport"].value("packs_sources", 0u) & 4u) == 0;    // (the importer's, made without No Mercy Plus)
        if (f == now.end() && nmp_less) { content_in = false; continue; }
        // (romsets 2026-11-24) wmport.bin is made from Revenge (its ports' models, WrestleMania 2000's with it): when the importer
        // made the packs without Revenge (No Mercy Plus V3 without it), there is none -- the game's own arenas' stage sets come
        // from content\wmport_base.bin, as with No Mercy alone -- and the rest of the extras are in (the mod leaves out what
        // needs Revenge: its EXTRAS_REVENGE bit off). Before, a missing wmport.bin left every extra out: No Mercy Plus's men
        // too, though they need nothing of Revenge's.
        // (arenasplit 2026-11-24) wmport.bin is made from Revenge or WrestleMania 2000 now (each game's own part): there is none
        // only when the importer had neither
        const bool rev_less = k.name == "wmport.bin" && rec.contains("romimport") && rec["romimport"].contains("packs_sources") &&
                              (rec["romimport"].value("packs_sources", 0u) & 3u) == 0;    // (the importer's, made without either)
        if (f == now.end() && rev_less) continue;
        if (f == now.end()) { extras = false; w = rel + " isn't there"; break; }
        if (f->sha != hex(k.sha) && importer_made(k.name, f->sha)) native_packs = true;
        if (f->sha != hex(k.sha) && !importer_made(k.name, f->sha)) {
            extras = false; w = rel + " isn't the one this game was made with"; break;
        }
    }
    if (extras && !fs::is_regular_file(content / "modextra.bin", ec)) { extras = false; w = "content\modextra.bin isn't there"; }
    // the sources the extras are made from: one bit each (mod src arcade.h EXTRAS_*); the developer's packs: all of them
    uint32_t extras_bits = 0;
    if (extras) {
        const uint32_t ALL_ROMS = 1 | 2 | 4 | 8;                    // (Revenge, WrestleMania 2000, No Mercy Plus, Redux)
        extras_bits = rec.contains("romimport") && rec["romimport"].contains("packs_sources") && !rec["romimport"]["packs"].empty()
                          ? rec["romimport"].value("packs_sources", 0u) : ALL_ROMS;
        if (!content_in) extras_bits &= ~(4u | 8u);           // (No Mercy Plus's and Redux's themes are the content pack's)
    }
    const bool need_mod = !built || reimport || rec.value("extras_in", false) != extras || rec.value("extras_bits", 0u) != extras_bits;
    if (!need_mod) {
        log("nothing to make from the new inputs in this version (their games' importers will)");
        rec["inputs"] = inputs_json();
        write_record(note, rec);
        return;
    }
    const Bytes rom = find_rom(plan.rom_sha);
    if (rom.empty()) { log("no WWF No Mercy (USA) (Rev 1) ROM found yet: the game says where to put it"); return; }
    log("making the mod from the player's ROM (firstrun\\plan.bin " + plan_sha.substr(0, 16) + ")");
    startup_status::plan(1);
    startup_status::say(Kind::Making, "the mod's tables from your No Mercy ROM");
    rec["plan"] = plan_sha; rec["state"] = "failed";                           // (until it is done: a stop half way is tried again)
    rec["inputs"] = inputs_json();
    write_record(note, rec);
    const Bytes six = six_of(rom);
    Bytes image = read_all(dir / "mod_binary.holes");
    if (image.size() != plan.image_size || sha256(image) != plan.holed_sha) give_up("firstrun\\mod_binary.holes isn't the one this plan goes with");
    if (!apply_base(plan, rom, six, image, why)) give_up(why);
    if (sha256(image) != plan.base_sha) give_up("the mod made from this ROM isn't the one the plan says (SHA-256)");
    // (the user's rule, 2026-11-18: what is in the game folder always comes first -- a file there that this builder did not
    //  write, as its record says, is the player's and is never written over)
    auto ours = [&](const fs::path& p, const char* key) {
        if (!fs::exists(p, ec)) return true;
        const bool mine = rec.contains(key) && hex(sha256(read_all(p))) == rec.value(key, std::string());
        if (!mine) log("kept the player's own " + arcade::shown(p) + " (not this builder's: never written over)");
        return mine;
    };
    if (ours(stage, "stage_sha256")) {
        if (!write_stage_pack(plan, image, stage, why)) give_up(why);
        rec["stage_sha256"] = hex(sha256(read_all(stage)));
        write_record(note, rec);   // (firstrun3: kept as this builder's at once -- a stop before the mod is written leaves it ours)
    }
    bool with_extras = false;
    if (extras) {
        std::string w2;
        Bytes full = image;
        if (apply_extras(plan, content, full, w2)) {
            // the extras flag: the developer's build has every bit; the image is checked with the build's own flag, then given
            // the player's sources (only the flag differs). A set made from fewer sources can't match the build's image: its
            // holes were checked against the developer's by the importer's own proofs (tools\romimport\native\ri_check.py).
            const auto flag = std::find_if(plan.holes.begin(), plan.holes.end(), [](const Hole& h) { return h.name == "arcade_extras_in"; });
            const bool all = (extras_bits & 15) == 15;
            if (all && !native_packs && sha256(full) != plan.full_sha) log("the built-in extras left out: the image isn't the build's (SHA-256)");
            else {
                if (flag != plan.holes.end() && flag->size == 4) {
                    full[flag->start] = uint8_t(extras_bits >> 24); full[flag->start + 1] = uint8_t(extras_bits >> 16);
                    full[flag->start + 2] = uint8_t(extras_bits >> 8); full[flag->start + 3] = uint8_t(extras_bits);
                }
                image.swap(full); with_extras = true;
                if (!all) log("the built-in extras made from the source games there (bits " + std::to_string(extras_bits) + "): what needs the others is left out");
            }
        }
        else log("the built-in extras left out: " + w2);
    } else log("the built-in extras left out (" + w + "): the game's own Superstars, themes and arenas");
    const Bytes json = read_all(dir / "mod.json"), syms = read_all(dir / "mod_syms.bin");
    if (json.empty() || syms.empty()) give_up("firstrun\\mod.json or mod_syms.bin isn't there");
    const Bytes zip = zip_stored({ { "mod_syms.bin", syms }, { "mod_binary.bin", image }, { "mod.json", json } });
    const bool write_mod = ours(out, "nrm_sha256");
    if (write_mod) {
        if (!write_all(out, zip)) give_up("can't write " + arcade::shown(out));
        rec["nrm_sha256"] = hex(sha256(zip));
    }
    // (startup) the status window: the extras in, or left out where they were in before
    {
        std::string from;
        for (const auto& [bit, g] : { std::pair<uint32_t, const char*>{ 1, "revenge" }, { 2, "wm2000" }, { 4, "nmplus_v3" }, { 8, "revenge_redux" } })
            if (extras_bits & bit) from += std::string(from.empty() ? "" : ", ") + ri::game_title(g);
        if (with_extras) startup_status::say(Kind::Added, "the built-in extras, from " + from);
        else if (rec.value("extras_in", false)) startup_status::say(Kind::Removed, "the built-in extras: left out (" + (w.empty() ? std::string("their packs aren't all there") : w) + ")");
        startup_status::step();
    }
    rec["state"] = "done"; rec["extras_in"] = with_extras; rec["extras_bits"] = with_extras ? extras_bits : 0u; rec["mod_sha256"] = hex(sha256(image));
    rec.erase("why");                                                          // (an earlier start's reason for failing)
    write_record(note, rec);
    if (reimport) fs::remove(content / "reimport.txt", ec);
    if (write_mod) log("the mod made: " + arcade::shown(out) + (with_extras ? ", with the built-in extras" : ", without the built-in extras") +
        " (SHA-256 " + hex(sha256(image), 8) + "..., the build's)");
}
