// ri_roms.cpp (phase3, 2026-11-19): the player's ROMs by fingerprint (ri_roms.h; tools\romimport\roms.py).
#include "ri_roms.h"
#include "ri_vcdiff.h"
#include "miniz.h"
#include <algorithm>

namespace ri {

// game -> fingerprint and file table. The fingerprints are the ROMs the project's tables and the tested folders were made
// from (roms.py SHA1); the tables were measured once per game (handlers\aki\romtable.py GAMES).
static const GameInfo GAMES[] = {
    { "nomercy",       "91cee3d096f4a76644d8b35b9aead6448909abd1", 0x16C31D8, 19459, 0x1BD150 },   // WWF No Mercy (USA) (Rev 1)
    { "nomercy10",     "3566d9b5723291937582453ad0453ce517cfc358", 0, 0, 0 },                      // v1.0: No Mercy Plus V3's base
    { "nmplus_v3",     "9f67445f44db5eb464f629f6a49719753bf52abe", 0x2BBC4D0, 0x7802, 0x5CD1B0 },  // No Mercy Plus V3 (v1.0 + its xdelta)
    { "revenge",       "e1711a2511394b9357b5f1ac8ca5cc17bd674836", 0xCE2752, 7658, 0xDAC50 },      // WCW/nWo Revenge (USA)
    { "wm2000",        "442d417a52ed672ca1a47e7261a5414debb1e27a", 0x11778BE, 0xA120 / 4, 0x144AA0 },   // WWF WrestleMania 2000 (USA)
    { "baddblood",     "aa8efede5a93cbd943b34fc2a46fe780a1e54825", 0x1137FAA, 0xA120 / 4, 0x144AA0 },   // WWF WarZone - Badd Blood (USA) (v1.0)
    { "revenge_redux", "0695b127b654a1d6b79ffe7e62fb8f2981c26d5c", 0, 0, 0 },                      // WCW/nWo Revenge Redux 05/20/2026
};
const char* const ALL_GAMES[] = { "nomercy", "nomercy10", "nmplus_v3", "revenge", "wm2000", "baddblood", "revenge_redux", nullptr };

// No Mercy Plus V3's patch (over v1.0): found by its name, as roms.py does, or by its own fingerprint
static const char* NMPLUS_PATCH_NAME = "no mercy plus v3.xdelta";
static const char* NMPLUS_PATCH_SHA1 = "61a67d187a0d4fd34380bdfb93c7e9baafb04633";
// WCW/nWo Revenge Redux: Revenge + this xdelta (alone in the folder or in the .zip it comes as)
static const char* REDUX_PATCH_SHA1 = "9341b5224eeab62cacfb80d4323bf0afc602e722";

const GameInfo* game_info(const std::string& name) {
    for (auto& g : GAMES) if (name == g.name) return &g;
    return nullptr;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static bool ends_with(const std::string& s, const char* e) {
    size_t n = strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}

// ---- AKI's LZSS -----------------------------------------------------------------------------------------------------
// A 4 KB ring from 0xFEE, a flag byte for every 8 items (low bit first): 1 = a byte as it is, 0 = a 12-bit ring place and
// a 4-bit length + 3. The packed data starts after its u32 unpacked size; the unpacker stops where the data ends.
Bytes aki_lzss(const uint8_t* src, size_t sn, size_t n) {
    Bytes out(n);
    uint8_t ring[0x1000] = {};
    size_t r = 0xFEE, i = 4, o = 0;
    unsigned flags = 0;
    while (o < n && i < sn) {
        flags >>= 1;
        if (!(flags & 0x100)) flags = src[i++] | 0xFF00;
        if (flags & 1) {
            if (i >= sn) break;
            uint8_t c = src[i++];
            out[o++] = c; ring[r] = c; r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= sn) break;
            unsigned lo = src[i], hi = src[i + 1]; i += 2;
            unsigned pos = lo | ((hi & 0xF0) << 4), ln = (hi & 0xF) + 3;
            for (unsigned k = 0; k < ln; k++) {
                uint8_t c = ring[(pos + k) & 0xFFF];
                if (o < n) out[o++] = c;
                ring[r] = c; r = (r + 1) & 0xFFF;
            }
        }
    }
    return out;
}

// ---- a game's files -------------------------------------------------------------------------------------------------
RomFiles::RomFiles(std::shared_ptr<const Bytes> rom, const GameInfo& g) : rom_(std::move(rom)), n_(g.entries), base_(g.base) {
    if ((size_t)g.table + 4ull * n_ > rom_->size()) fail("%s: the file table is past the ROM's end", g.name);
    t_.resize(n_);
    for (uint32_t i = 0; i < n_; i++) t_[i] = be32(rom_->data() + g.table + 4 * i);
}

const Bytes* RomFiles::get(uint32_t i) const {
    if (i < 1 || i >= n_) return nullptr;
    auto it = cache_.find(i);
    if (it == cache_.end()) {
        std::optional<Bytes> v;
        uint64_t a = (uint64_t)(t_[i - 1] & ~1u) + base_, b = (uint64_t)(t_[i] & ~1u) + base_;
        if (!(b < a || b > rom_->size())) {
            const uint8_t* raw = rom_->data() + a;
            size_t rn = (size_t)(b - a);
            if ((t_[i - 1] & 1) && rn >= 4) {
                uint32_t dn = be32(raw);
                if (dn < 0x400000) v = aki_lzss(raw, rn, dn);
                else v = Bytes(raw, raw + rn);
            } else v = Bytes(raw, raw + rn);
        }
        it = cache_.emplace(i, std::move(v)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const Bytes& RomFiles::operator()(uint32_t i) const {
    const Bytes* b = get(i);
    if (!b) fail("file 0x%X has no bytes", i);
    return *b;
}

// ---- reading a ROM file ---------------------------------------------------------------------------------------------
static Bytes big_endian(Bytes b) {
    if (b.size() >= 4 && b[0] == 0x37 && b[1] == 0x80 && b[2] == 0x40 && b[3] == 0x12) {          // .v64: bytes swapped in pairs
        for (size_t i = 0; i + 1 < b.size(); i += 2) std::swap(b[i], b[i + 1]);
    } else if (b.size() >= 4 && b[0] == 0x40 && b[1] == 0x12 && b[2] == 0x37 && b[3] == 0x80) {   // .n64: little-endian words
        for (size_t i = 0; i + 3 < b.size(); i += 4) { std::swap(b[i], b[i + 3]); std::swap(b[i + 1], b[i + 2]); }
    }
    return b;
}

// a zip's members by name filter (in the archive's own order), read in memory
template <class F> static void zip_members(const Bytes& z, F&& each) {
    mz_zip_archive za; memset(&za, 0, sizeof za);
    if (!mz_zip_reader_init_mem(&za, z.data(), z.size(), 0)) return;
    mz_uint n = mz_zip_reader_get_num_files(&za);
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st) || st.m_is_directory) continue;
        if (!each(za, i, std::string(st.m_filename), (uint64_t)st.m_uncomp_size)) break;
    }
    mz_zip_reader_end(&za);
}
static bool zip_read(mz_zip_archive& za, mz_uint i, Bytes& out) {
    size_t n = 0;
    void* p = mz_zip_reader_extract_to_heap(&za, i, &n, 0);
    if (!p) return false;
    out.assign((uint8_t*)p, (uint8_t*)p + n);
    mz_free(p);
    return true;
}

Bytes read_rom_file(const fs::path& p) {
    Bytes b;
    if (!read_file_if(p, b)) return {};
    if (ends_with(lower(p.filename().string()), ".zip")) {
        Bytes rom;
        zip_members(b, [&](mz_zip_archive& za, mz_uint i, const std::string& name, uint64_t) {
            std::string l = lower(name);
            if (ends_with(l, ".z64") || ends_with(l, ".n64") || ends_with(l, ".v64")) { zip_read(za, i, rom); return false; }
            return true;
        });
        return big_endian(std::move(rom));
    }
    return big_endian(std::move(b));
}

// ---- the folder -----------------------------------------------------------------------------------------------------
// Every file in the folder and below, as os.walk gives them: a folder's files (sorted by name) before its subfolders'.
static void walk(const fs::path& d, std::vector<fs::path>& out) {
    std::vector<fs::path> files, dirs;
    std::error_code ec;
    for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) dirs.push_back(it->path());
        else files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a.filename().u8string() < b.filename().u8string(); });
    for (auto& f : files) out.push_back(f);
    for (auto& s : dirs) walk(s, out);
}

void Roms::scan() {
    if (scanned_) return;
    scanned_ = true;
    std::vector<fs::path> all;
    walk(folder_, all);
    for (auto& p : all) {
        std::string l = lower(p.filename().string());
        if (!(ends_with(l, ".z64") || ends_with(l, ".n64") || ends_with(l, ".v64") || ends_with(l, ".zip"))) continue;
        Bytes b = read_rom_file(p);
        if (b.empty()) continue;
        by_sha_.emplace(sha1_hex(b), p);        // (the first file with a fingerprint keeps it)
    }
}

fs::path Roms::path(const std::string& game) {
    scan();
    const GameInfo* g = game_info(game);
    if (!g) return {};
    auto it = by_sha_.find(g->sha1);
    return it == by_sha_.end() ? fs::path() : it->second;
}

std::optional<Bytes> Roms::patch(const std::string& sha) {
    std::vector<fs::path> all;
    walk(folder_, all);
    for (auto& p : all) {
        std::string l = lower(p.filename().string());
        if (ends_with(l, ".xdelta")) {
            Bytes b;
            if (read_file_if(p, b) && sha1_hex(b) == sha) return b;
        } else if (ends_with(l, ".zip")) {
            Bytes z;
            if (!read_file_if(p, z)) continue;
            std::optional<Bytes> got;
            zip_members(z, [&](mz_zip_archive& za, mz_uint i, const std::string& name, uint64_t size) {
                if (!ends_with(lower(name), ".xdelta") || size > (1ull << 28)) return true;    // (a patch, not a bomb)
                Bytes b;
                if (zip_read(za, i, b) && sha1_hex(b) == sha) { got = std::move(b); return false; }
                return true;
            });
            if (got) return got;
        }
    }
    return std::nullopt;
}

bool Roms::has_patch(const std::string& game) {
    if (game == "nmplus_v3") {
        std::vector<fs::path> all;
        walk(folder_, all);
        for (auto& f : all) if (lower(f.filename().string()) == NMPLUS_PATCH_NAME) return true;
        return patch(NMPLUS_PATCH_SHA1).has_value();
    }
    if (game == "revenge_redux") return patch(REDUX_PATCH_SHA1).has_value();
    return false;
}

std::shared_ptr<const Bytes> Roms::rom(const std::string& game) {
    auto have = roms_.find(game);
    if (have != roms_.end()) return have->second;
    if (tried_[game]) return nullptr;
    tried_[game] = true;
    const GameInfo* g = game_info(game);
    if (!g) return nullptr;
    std::shared_ptr<const Bytes> out;
    fs::path p = path(game);
    if (!p.empty()) out = std::make_shared<const Bytes>(read_rom_file(p));
    else if (game == "nmplus_v3" || game == "revenge_redux") {
        // a clean ROM + the player's patch, applied in memory, the result checked by its fingerprint
        auto base = rom(game == "nmplus_v3" ? "nomercy10" : "revenge");
        std::optional<Bytes> pb;
        if (game == "nmplus_v3") {
            std::vector<fs::path> all;
            walk(folder_, all);
            for (auto& f : all)
                if (lower(f.filename().string()) == NMPLUS_PATCH_NAME) { Bytes b; if (read_file_if(f, b)) { pb = std::move(b); break; } }
            if (!pb) pb = patch(NMPLUS_PATCH_SHA1);
        } else pb = patch(REDUX_PATCH_SHA1);
        if (base && pb) {
            Bytes made; std::string err;
            if (vcdiff::apply(*base, *pb, made, err) && sha1_hex(made) == g->sha1) out = std::make_shared<const Bytes>(std::move(made));
        }
    }
    if (out) roms_[game] = out;
    return out;
}

const RomFiles* Roms::files(const std::string& game) {
    auto it = files_.find(game);
    if (it != files_.end()) return it->second.get();
    const GameInfo* g = game_info(game);
    auto r = rom(game);
    std::unique_ptr<RomFiles> f;
    if (r && g && g->table) f = std::make_unique<RomFiles>(r, *g);
    return (files_[game] = std::move(f)).get();
}

}  // namespace ri
