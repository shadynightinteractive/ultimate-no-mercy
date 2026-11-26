// ri_wmmoves.cpp (phase3 p3folders / p3wmmoves, 2026-11-19): WrestleMania 2000's movesets and move library (ri_wmmoves.h).
//
// The Python this mirrors runs as one process per step: wmlib.py lib (the library and the rows) and wm2k_import.py (each
// man's moves.json and packed moveset), then wm2k_refresh.py over the folders. Every value that decides a byte is made here
// the same way, in the same order: the report lines port_wm_moveset writes and place_own / how_of read back are made as text
// and read back as the Python reads them (a line's "how" is what follows its last "(" -- even inside place_own's reasons),
// dicts keep their insertion order (OMap), max() keeps the first of equals, Counter.most_common the first inserted among
// equals (OrderedCounter), and the one Python set whose ORDER decides something (mkroster.stand_in's candidates, "cands[0]")
// is iterated in CPython 3.10's own order (PySet).
#include "ri_wmmoves.h"
#include "ri_data.h"
#include "ri_images.h"
#include "ri_json.h"
#include "ri_mips.h"
#include "ri_movelib.h"
#include <algorithm>
#include <climits>
#include <map>
#include <set>

namespace ri {

namespace {

// ---- Python's text, for the Latin-1 strings the move names are (wm2k.py decodes the games' names as latin-1) ----------
// (str.lower / isalnum / isdigit / split() on code points 0..255, as Python's Unicode tables have them)
bool py_space(uint8_t c) { return (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0; }
bool py_digit(uint8_t c) { return (c >= '0' && c <= '9') || c == 0xB2 || c == 0xB3 || c == 0xB9; }
bool py_alpha(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == 0xAA || c == 0xB5 || c == 0xBA ||
           (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || c >= 0xF8;
}
bool py_alnum(uint8_t c) { return py_alpha(c) || py_digit(c) || (c >= 0xBC && c <= 0xBE); }
std::string py_lower(const std::string& s) {
    std::string o = s;
    for (auto& ch : o) {
        uint8_t c = (uint8_t)ch;
        if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) ch = (char)(c + 0x20);
    }
    return o;
}
std::vector<std::string> py_split(const std::string& s) {
    std::vector<std::string> out; std::string w;
    for (char ch : s) {
        if (py_space((uint8_t)ch)) { if (!w.empty()) out.push_back(w); w.clear(); }
        else w += ch;
    }
    if (!w.empty()) out.push_back(w);
    return out;
}
bool py_isdigit(const std::string& w) {
    if (w.empty()) return false;
    for (char c : w) if (!py_digit((uint8_t)c)) return false;
    return true;
}
std::string latin1_to_utf8(const std::string& s) {
    std::string o;
    for (char ch : s) {
        uint8_t c = (uint8_t)ch;
        if (c < 0x80) o += (char)c;
        else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}
std::string utf8_to_latin1(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) o += (char)c;
        else if ((c & 0xE0) == 0xC0 && c <= 0xC3 && i + 1 < s.size()) { o += (char)(((c & 3) << 6) | ((uint8_t)s[i + 1] & 0x3F)); i++; }
        else fail("a move name past Latin-1");
    }
    return o;
}
// int(token): Python's int() of a decimal text (surrounding whitespace allowed)
long py_int(const std::string& t) {
    std::vector<std::string> w = py_split(t);
    if (w.size() != 1) fail("int(%s)", t.c_str());
    size_t n = 0; long v = std::stol(w[0], &n);
    if (n != w[0].size()) fail("int(%s)", t.c_str());
    return v;
}

// mkroster.py norm / stem
std::string norm(const std::string& s) {
    std::string o;
    for (char c : py_lower(s)) if (py_alnum((uint8_t)c)) o += c;
    return o;
}
std::string stem(const std::string& s) {
    std::string l = py_lower(s);
    for (auto& c : l) if (c == '/') c = ' ';
    std::string o;
    for (auto& w : py_split(l)) {
        if (py_isdigit(w) || py_isdigit(w.substr(0, w.size() - 1))) continue;
        if (!o.empty()) o += ' ';
        o += w;
    }
    return o;
}

// ---- a dict that keeps insertion order (an assignment to a key already there keeps its place) ----------------------
template <class V> struct OMap {
    std::vector<std::pair<int, V>> items;
    V* find(int k) { for (auto& it : items) if (it.first == k) return &it.second; return nullptr; }
    const V* find(int k) const { for (auto& it : items) if (it.first == k) return &it.second; return nullptr; }
    bool has(int k) const { return find(k) != nullptr; }
    V& set(int k, const V& v) { if (V* p = find(k)) { *p = v; return *p; } items.push_back({ k, v }); return items.back().second; }
    size_t size() const { return items.size(); }
};

// ---- CPython 3.10's set of ints, for its iteration order (Objects/setobject.c: set_add_entry, set_table_resize,
// set_insert_clean; LINEAR_PROBES 9, PERTURB_SHIFT 5, PySet_MINSIZE 8; an int's hash is itself) ---------------------
struct PySet {
    std::vector<long> t = std::vector<long>(8, -1);     // -1: an empty entry (the keys here are u16)
    size_t mask = 7, fill = 0, used = 0;
    static void insert_clean(std::vector<long>& tab, size_t mask, long key) {
        size_t perturb = (size_t)key, i = (size_t)key & mask;
        while (true) {
            if (tab[i] < 0) { tab[i] = key; return; }
            if (i + 9 <= mask)
                for (size_t j = 1; j <= 9; j++) if (tab[i + j] < 0) { tab[i + j] = key; return; }
            perturb >>= 5;
            i = (i * 5 + 1 + perturb) & mask;
        }
    }
    void resize(size_t minused) {
        size_t n = 8;
        while (n <= minused) n <<= 1;
        std::vector<long> nt(n, -1);
        for (long k : t) if (k >= 0) insert_clean(nt, n - 1, k);
        t.swap(nt); mask = n - 1; fill = used;
    }
    void add(long key) {
        size_t i = (size_t)key & mask, perturb = (size_t)key;
        size_t at;
        while (true) {
            if (t[i] < 0) { at = i; break; }
            if (t[i] == key) return;
            bool done = false;
            if (i + 9 <= mask)
                for (size_t j = 1; j <= 9; j++) {
                    if (t[i + j] < 0) { at = i + j; done = true; break; }
                    if (t[i + j] == key) return;
                }
            if (done) break;
            perturb >>= 5;
            i = (i * 5 + 1 + perturb) & mask;
        }
        t[at] = key; fill++; used++;
        if (fill * 5 < mask * 3) return;
        resize(used > 50000 ? used * 2 : used * 4);
    }
    std::vector<int> order() const { std::vector<int> o; for (long k : t) if (k >= 0) o.push_back((int)k); return o; }
    bool has(int k) const { for (long x : t) if (x == k) return true; return false; }
};

constexpr int NONE = INT_MIN;                // Python's None where an int is expected
constexpr int GOES_ON = 0x1E00;              // (a move record's flags: it goes on to its next step -- movex.c)
const int FRAME_AT[] = { 15, 16, 18, 20, 22, 24, 26, 28, 30, 32 };
const int SOUND_AT[] = { 20, 22, 24, 26, 28, 30 };
constexpr uint32_t WM_ANIM0 = 0x1609, WM_ANIM_END = 0x2848, WM_ANIM_RECS = 0x435, WM_MOVE_RECS = 0x3AD, WM_REC_BYTES = 32;
constexpr uint32_t NM_ANIM0 = 0x2DD9, NM_ANIM_N = 0x169B, NM_ANIM_RECS = 0x2BF, NM_MOVE_RECS = 0x1EF;
// mkroster.py: WrestleMania 2000's grapples 86, 90, 94, 98 are No Mercy's 86, 88, 90, 92 (the rest of 86..101 not mapped)
const std::map<int, int> WM_SLOT = { { 86, 86 }, { 90, 88 }, { 94, 90 }, { 98, 92 } };
const std::map<int, int> WM_SLOT_INV = { { 86, 86 }, { 88, 90 }, { 90, 94 }, { 92, 98 } };
bool in_wm_block(int i) { return i >= 86 && i < 102; }
int wm_slot(int i0) { auto it = WM_SLOT.find(i0); return it == WM_SLOT.end() ? i0 : it->second; }
int wm_slot_inv(int i) { auto it = WM_SLOT_INV.find(i); return it == WM_SLOT_INV.end() ? i : it->second; }
const char* const NOT_SAME[] = { "name", "stand-in", "reference" };
bool not_same(const std::string& h) { for (auto* s : NOT_SAME) if (h == s) return true; return false; }
const char* const PORTED[] = { "jeff_jarrett", "the_blue_meanie" };
bool is_ported(const std::string& sid) { return sid == PORTED[0] || sid == PORTED[1]; }

struct Slot { int m, f2, f3, f4; bool operator==(const Slot& o) const { return m == o.m && f2 == o.f2 && f3 == o.f3 && f4 == o.f4; } };

// No Mercy's unpacking / packing buffers (mkroster.py BUF_*) and functions
constexpr uint32_t BUF_M = 0x80780000, BUF_P = 0x80780100, BUF_S = 0x80781000, BUF_A = 0x80782000;
constexpr uint32_t VAN_UNPACK = 0x80005EFC, VAN_PACK = 0x80005EC0;

}  // namespace

// ======================================================================================================================
struct WmMoveLib::Impl {
    Roms& R;
    const RomFiles *W = nullptr, *N = nullptr;
    const RomFiles* WH = nullptr;                       // (WrestleMania 2000's files: which animations wmanim's wm_h has)
    bool bb_alone = false;                              // (arenasplit: Badd Blood's own in WrestleMania 2000's place: init_base)
    std::shared_ptr<const Bytes> wrom, nrom;
    std::vector<Who> who_;
    // the files' tables (wmanim.py WM1 / WM2 / NM1 / NM2: s16 each)
    std::vector<int> WM1, WM2, NM1, NM2;
    Bytes wrec, nrec;                                   // the move records (Moves: 0x3AD, 32 bytes; 0x1EF, 36 bytes)
    Bytes wanim_rec, nanim_rec;                         // the animations' records (Anims: 0x435 / 0x2BF, 36 bytes)
    std::map<int, std::string> WM_NAMES, NM_NAMES, vnames;     // latin-1
    std::map<std::string, int> by_hash;                 // (wmanim.py: md5 -> the lowest No Mercy animation file with it)
    std::map<int, int> nm_anim_cache;
    std::map<std::vector<int>, std::vector<int>> index; // (wmanim.py: No Mercy's ten values of a move -> its moves)
    // No Mercy's packing (mkroster.py LAYOUT, CAN)
    std::vector<std::pair<int, std::vector<int>>> LAYOUT;
    std::map<int, PySet> CAN;
    std::vector<std::vector<int>> VALS;                 // (mkroster.py VALS: each value list of the packing)
    std::map<int, std::vector<int>> CAN_kind;           // (sorted, 0 and 0xFFFF out: the "kind" range)
    // the RAM images: WrestleMania 2000's (wm2k.py RAM, a fresh copy a call) and No Mercy's at its Select (mkroster EV)
    Bytes wm2k_ram;
    std::unique_ptr<Emu> EV;
    std::vector<Bytes> van_sets;                        // (empty: van_sets[14] = None)
    std::map<int, std::pair<std::vector<Slot>, std::vector<int>>> van_cache;
    std::map<std::pair<int, int>, std::pair<std::vector<Slot>, std::vector<int>>> wm_unpack_cache;
    std::vector<WmRecord> roster, roster_img;
    // the derived entries (wmlib.py steps_of)
    std::map<int, std::vector<std::array<int, 3>>> nm_steps, wm_steps;
    std::set<int> nm_derived_entries;
    std::map<int, std::set<int>> taunt_slots;           // (fixes2.py win_pose: move -> the slots 224..235 it is in)
    bool taunts_made = false;

    // ---- Anims (wmlib.py): WrestleMania 2000's animation files as No Mercy plays them --------------------------------
    std::map<int, int> nm_of;                           // (WM2000 file -> No Mercy file, the very bytes)
    std::map<int, int> tail_of;                         // ((2026-11-20, revmoves2) WM2000 file -> No Mercy file, all bytes but the first)
    std::vector<std::map<int, int>> conv;
    std::map<int, int> snd, b0;
    std::map<std::pair<Bytes, Bytes>, int> content_ix; // (bytes, record) -> content number
    std::vector<std::pair<const Bytes*, Bytes>> contents;
    std::vector<std::unique_ptr<Bytes>> owned;
    std::map<int, int> get_cache;

    // ---- the library -----------------------------------------------------------------------------------------------
    struct LibMove {
        std::string id; int b, fa; bool has_cont; std::vector<int> cont; std::vector<int> key; bool sh0, sh1;
        std::vector<int> same_as; std::set<std::string> who; OrderedCounter<int> use;
        uint32_t pair_ix = 0, block_ix = 0; int base = 0;
    };
    std::vector<std::unique_ptr<LibMove>> lib;
    std::map<std::vector<int>, LibMove*> by_key;
    std::map<int, LibMove*> of_base;                    // (nullptr: None)
    std::vector<std::pair<int, std::string>> none;
    std::vector<int> bases;
    size_t n_steps_lib = 0;
    std::vector<std::pair<int, std::optional<std::map<int, int>>>> recmap;
    size_t rec_pairs = 0;

    struct HowT { int wmv, nmv; std::string how; };
    struct Mapped {
        std::vector<Slot> slots; std::vector<int> ab; std::map<int, HowT> how; std::map<int, Slot> placed_was;
        std::map<int, int> wm_at;
    };
    std::map<std::string, Mapped> maps;
    struct Id { std::string id; int wm, was, on; std::string how; };
    struct Var { int base, k, wv, nv, on; std::string why; LibMove* x = nullptr; };
    typedef std::vector<std::pair<int, int>> Rec;       // (record byte, value), in record_map's order
    struct SlotIds { OMap<Id> ids; OMap<std::string> kept, same; std::map<int, Rec> recs; };

    Impl(Roms& r) : R(r) {}

    // ---- small readers -------------------------------------------------------------------------------------------
    static std::vector<int> s16s(const Bytes& b) { std::vector<int> o; for (size_t i = 0; i + 1 < b.size(); i += 2) o.push_back(sx16(be16(b, i))); return o; }
    static int at(const std::vector<int>& v, long i) {      // v[i] as Python indexes a list (an IndexError: stop)
        if (i < 0) i += (long)v.size();
        if (i < 0 || i >= (long)v.size()) fail("wmmoves: list index %ld out of range", i);
        return v[(size_t)i];
    }
    const Bytes& wfile(uint32_t f) const { static const Bytes empty; const Bytes* b = W->get(f); return b ? *b : empty; }

    // wm2k.py _names / _by_move: a game's move names by move number
    static std::map<int, std::string> names_of(const RomFiles& F, std::initializer_list<uint32_t> files, uint32_t recfile) {
        std::vector<std::string> names;
        for (uint32_t f : files) {
            const Bytes& d = F(f);
            int n = be16(d, 0) / 2;
            for (int i = 0; i < n; i++) {
                uint32_t o = be16(d, 2 * i);
                auto z = std::find(d.begin() + o, d.end(), 0);
                if (z == d.end()) fail("wmmoves: a name without its end");
                names.push_back(std::string(d.begin() + o, z));
            }
        }
        const Bytes& d = F(recfile);
        std::map<int, std::string> out;
        for (size_t i = 0; i < d.size() / 20; i++) {
            size_t k = be16(d, 20 * i);
            if (k < names.size() && !names[k].empty()) out[10 * (int)i] = names[k];
        }
        return out;
    }
    static std::string get_name(const std::map<int, std::string>& m, int k, const std::string& dflt = "") {
        auto it = m.find(k); return it == m.end() ? dflt : it->second;
    }

    // ---- wmanim.py nm_anim / nm_move -----------------------------------------------------------------------------------
    int nm_anim(int v) {
        if (v <= 0) return v;
        auto c = nm_anim_cache.find(v);
        if (c != nm_anim_cache.end()) return c->second;
        // (wmanim.wm_h: a file WrestleMania 2000 has (hashes.pkl), hashed as the run's files have it -- use_rom_files /
        //  baddblood\game.py moveset put the game's own bytes in for the animations 0x1609..0x2847; the rest stay
        //  WrestleMania 2000's)
        int r = NONE;
        uint32_t f = (uint32_t)(v + 0x19DB);
        const Bytes* b = WH->get(f);
        if (b) {
            static const Bytes empty;
            if (W != WH && f >= WM_ANIM0 && f < WM_ANIM_END) b = W->get(f) ? W->get(f) : &empty;
            auto it = by_hash.find(md5_hex(*b)); if (it != by_hash.end()) r = it->second - 0x3322;
        }
        nm_anim_cache[v] = r;
        return r;
    }
    std::vector<int> nm_move(int m) {
        int base = m - m % 10;
        std::vector<int> t;
        bool any = false;
        for (int i = base; i < base + 10 && i < (int)WM1.size(); i++) {
            int x = nm_anim(WM1[i]);
            if (x == NONE) return {};
            any |= x > 0;
            t.push_back(x);
        }
        if (!any) return {};
        auto it = index.find(t);
        std::vector<int> o;
        if (it != index.end()) for (int x : it->second) o.push_back(x + m % 10);
        return o;
    }

    // ---- the MIPS runs: WrestleMania 2000's unpacking (wm2k.py unpack_moves), No Mercy's (mkroster.py unpack / pack) ---
    const std::pair<std::vector<Slot>, std::vector<int>>& wm_unpack(int mv_file, int ab_file) {
        auto key = std::make_pair(mv_file, ab_file);
        auto it = wm_unpack_cache.find(key);
        if (it != wm_unpack_cache.end()) return it->second;
        Emu E(wm2k_ram);                                   // (a fresh copy of the image a call, as wm2k.py's Emu(RAM))
        E.wbytes(0x80780000, wfile(mv_file)); E.wbytes(0x80780100, wfile(ab_file));
        E.wbytes(0x80781000, Bytes(0x800)); E.wbytes(0x80782000, Bytes(0x80));
        E.call(0x80004D0C, { 0x80780000, 0x80781000, 0x80780100, 0x80782000 });
        std::vector<Slot> s; std::vector<int> a;
        for (int i = 0; i < 292; i++) { uint32_t p = 0x80781000 + 6 * i; s.push_back({ E.r16(p), E.r8(p + 2), E.r8(p + 3), E.r8(p + 4) }); }
        for (int i = 0; i < 50; i++) a.push_back(sx16(E.r16(0x80782000 + 2 * i)));
        return wm_unpack_cache[key] = { s, a };
    }
    std::pair<std::vector<Slot>, std::vector<int>> nm_unpack(const uint8_t* moves, const uint8_t* params) {
        Emu& E = *EV;
        E.wbytes(BUF_M, moves, 0xB8); E.wbytes(BUF_P, params, 0x20);
        E.wbytes(BUF_S, Bytes(0x800)); E.wbytes(BUF_A, Bytes(0x80));
        E.call(VAN_UNPACK, { BUF_M, BUF_S, BUF_P, BUF_A });
        std::vector<Slot> s; std::vector<int> a;
        for (int i = 0; i < 292; i++) { uint32_t p = BUF_S + 6 * i; s.push_back({ E.r16(p), E.r8(p + 2), E.r8(p + 3), E.r8(p + 4) }); }
        for (int i = 0; i < 55; i++) a.push_back(sx16(E.r16(BUF_A + 2 * i)));
        return { s, a };
    }
    Bytes nm_pack(const std::vector<Slot>& slots, const std::vector<int>& ab) {
        Emu& E = *EV;
        E.wbytes(BUF_S, Bytes(0x800)); E.wbytes(BUF_A, Bytes(0x80)); E.wbytes(BUF_M, Bytes(0xB8)); E.wbytes(BUF_P, Bytes(0x20));
        for (size_t i = 0; i < slots.size(); i++) {
            E.w16(BUF_S + 6 * (uint32_t)i, slots[i].m); E.w8(BUF_S + 6 * (uint32_t)i + 2, slots[i].f2);
            E.w8(BUF_S + 6 * (uint32_t)i + 3, slots[i].f3); E.w8(BUF_S + 6 * (uint32_t)i + 4, slots[i].f4);
        }
        for (size_t i = 0; i < ab.size(); i++) E.w16(BUF_A + 2 * (uint32_t)i, ab[i] & 0xFFFF);
        E.call(VAN_PACK, { BUF_S, BUF_M, BUF_A, BUF_P });
        Bytes o = E.rbytes(BUF_M, 0xB8), p = E.rbytes(BUF_P, 0x20);
        o.insert(o.end(), p.begin(), p.end());
        return o;
    }
    const std::pair<std::vector<Slot>, std::vector<int>>& van_unpacked(int o) {
        auto it = van_cache.find(o);
        if (it != van_cache.end()) return it->second;
        if (o < 0 || o >= (int)van_sets.size() || van_sets[o].empty()) fail("wmmoves: No Mercy has no moveset at ordinal %d", o);
        return van_cache[o] = nm_unpack(van_sets[o].data(), van_sets[o].data() + 0xB8);
    }

    // ---- mkroster.py fits / stand_in ---------------------------------------------------------------------------------
    bool in_can(int i, int v) const { auto it = CAN.find(i); return it != CAN.end() && it->second.has(v); }
    bool fits(int i, int v) const {
        auto it = CAN.find(i);
        if (it == CAN.end()) return false;
        if (it->second.has(v)) return true;
        auto& k = CAN_kind.at(i);
        return !k.empty() && k.front() <= v && v <= k.back();
    }
    size_t n_can_moves(int i) const { auto it = CAN_kind.find(i); return it == CAN_kind.end() ? 0 : it->second.size(); }
    int stand_in(int slot, const std::string& nmname, int ref) {
        if (nmname.empty() || nmname == "None" || !CAN.count(slot)) return ref;
        std::string st = stem(nmname);
        std::vector<int> order = CAN.at(slot).order();      // (the set's own iteration order: cands[0] depends on it)
        std::vector<int> cands;
        for (int m : order) if (m != 0 && m != 0xFFFF && stem(get_name(vnames, m)) == st) cands.push_back(m);
        if (cands.empty()) {
            std::vector<std::string> wv = py_split(st);
            std::set<std::string> words(wv.begin(), wv.end());
            std::vector<std::pair<int, int>> scored;
            for (int m : order) {
                if (m == 0 || m == 0xFFFF) continue;
                std::vector<std::string> mw = py_split(stem(get_name(vnames, m)));
                std::set<std::string> ms(mw.begin(), mw.end());
                int n = 0; for (auto& w : words) n += (int)ms.count(w);
                scored.push_back({ n, m });
            }
            std::pair<int, int> best = { 0, 0 };
            if (!scored.empty()) best = *std::max_element(scored.begin(), scored.end());
            if (best.first >= std::max(1, (int)words.size() - 1) && best.first >= 1)
                for (auto& [sc, m] : scored) if (sc == best.first) cands.push_back(m);
        }
        if (cands.empty()) return ref;
        return std::find(cands.begin(), cands.end(), ref) != cands.end() ? ref : cands[0];
    }

    // ---- mkroster.py port_wm_moveset (report lines as the Python writes them) -----------------------------------------
    std::string nm_name_of(int v) { int b = v - v % 10; return get_name(vnames, b, get_name(NM_NAMES, b)); }
    void port_wm_moveset(const std::string& key, int mv_file, int ab_file, int ref_ord, std::vector<std::string>& report,
                         std::vector<Slot>& out, std::vector<int>& ab) {
        auto& [wslots, wab] = wm_unpack(mv_file, ab_file);
        auto& [rslots, rab] = van_unpacked(ref_ord);
        // (names -> No Mercy's moves, first No Mercy's names files' then names.pkl's, each by move number)
        std::map<std::string, std::vector<int>> ix;
        for (auto* src : { &NM_NAMES, &vnames })
            for (auto& [k, v] : *src) {
                if (k % 10) continue;
                auto& l = ix[norm(v)];
                if (std::find(l.begin(), l.end(), k) == l.end()) l.push_back(k);
            }
        out = rslots;
        for (int i0 = 0; i0 < (int)wslots.size(); i0++) {
            int m = wslots[i0].m, f2 = wslots[i0].f2, f3 = wslots[i0].f3, f4 = wslots[i0].f4;
            if (in_wm_block(i0) && !WM_SLOT.count(i0)) continue;
            int i = wm_slot(i0);
            std::string name = (m != 0 && m != 0xFFFF) ? get_name(WM_NAMES, m - m % 10) : "";
            int v; std::string how;
            if (m == 0 || m == 0xFFFF) { v = m; how = "none"; }
            else {
                v = NONE;
                std::vector<int> c;
                for (int x : nm_move(m)) if (fits(i, x)) c.push_back(x);
                if (!c.empty()) {
                    v = c[0];
                    for (int x : c) if (in_can(i, x)) { v = x; break; }
                    how = "animation";
                }
                if (v == NONE) {
                    std::string n0 = norm(name);
                    for (const std::string& k : { n0, n0 + "01" }) {
                        c.clear();
                        auto it = ix.find(k);
                        if (it != ix.end()) for (int x : it->second) if (fits(i, x + m % 10)) c.push_back(x + m % 10);
                        if (!c.empty()) {
                            v = c[0];
                            for (int x : c) if (in_can(i, x)) { v = x; break; }
                            how = "name"; break;
                        }
                    }
                }
                if (v == NONE) {
                    v = CAN.count(i) ? stand_in(i, name, rslots[i].m) : rslots[i].m;
                    how = v != rslots[i].m ? "stand-in" : "reference";
                    if (how == "reference") { f2 = rslots[i].f2; f3 = rslots[i].f3; f4 = rslots[i].f4; }
                }
            }
            if (how != "animation")
                report.push_back(strf("  %s slot %3d: WM2000 %5d %-34s -> %5d %-28s (%s)", key.c_str(), i, m, name.c_str(), v,
                                      nm_name_of(v).c_str(), how.c_str()));
            if (v == 0xFFFF && CAN.count(i) && !CAN.at(i).has(0xFFFF)) v = 0;
            out[i] = { v, f2, f3, f4 };
        }
        ab = rab;
        for (int j = 0; j < 31; j++) ab[j] = wab[j];
        ab[18] = 3; ab[19] = 0;
        for (int j = 31; j < 39; j++) ab[j] = 0;
        static const int AMAP[][2] = { { 39, 35 }, { 40, 36 }, { 42, 37 }, { 43, 38 }, { 44, 39 }, { 45, 40 }, { 46, 41 },
                                       { 47, 42 }, { 49, 44 }, { 50, 45 }, { 51, 46 }, { 52, 47 }, { 53, 48 }, { 54, 49 } };
        for (auto& p : AMAP) ab[p[0]] = wab[p[1]];
        ab[41] = 0; ab[48] = 0;
        for (int j = 0; j < 3; j++) ab[j] = rab[j];
    }

    // ---- the move records (wmlib.py Moves; game.py place_own's wr / nr) ------------------------------------------------
    const uint8_t* wm_rec(int m) const {         // None: nullptr
        int r = (m >= 0 && m < (int)WM2.size()) ? WM2[m] : 0;
        if (r <= 0) return nullptr;
        if ((size_t)WM_REC_BYTES * r + WM_REC_BYTES > wrec.size()) fail("wmmoves: WM2000 move record %d past its file", r);
        return &wrec[WM_REC_BYTES * r];
    }
    const uint8_t* nm_rec(int m) const {
        int r = (m >= 0 && m < (int)NM2.size()) ? NM2[m] : 0;
        if (r <= 0) return nullptr;
        if ((size_t)36 * r + 36 > nrec.size()) fail("wmmoves: No Mercy move record %d past its file", r);
        return &nrec[36 * r];
    }
    uint32_t wm_flags(int m) const {               // (Moves.wm_flags: WM2[m] unchecked, as the Python)
        int r = at(WM2, m);
        if (r <= 0) return 0;
        if ((size_t)WM_REC_BYTES * r + 0x20 > wrec.size()) fail("wmmoves: WM2000 move record %d past its file", r);
        return be32(wrec, WM_REC_BYTES * r + 0x1C);
    }
    uint32_t nm_flags(int m) const { const uint8_t* p = nm_rec(m); return p ? be32(p + 0x20) : 0; }
    std::pair<bool, bool> wm_shape(int b) const {
        bool g0 = wm_flags(b) & GOES_ON;
        bool g1 = at(WM1, b + 1) > 0 && (wm_flags(b) & GOES_ON) && (wm_flags(b + 1) & GOES_ON);
        return { g0, g1 };
    }
    std::pair<bool, bool> nm_shape(int m) const {
        bool g0 = nm_flags(m) & GOES_ON;
        return { g0, g0 && (nm_flags(m + 1) & GOES_ON) };
    }

    // ---- game.py place_own: the slot rule (the very move in its slot, where the game allows it) ------------------------
    void place_own(const std::string& key, int mv_file, int ab_file, std::vector<Slot>& out, std::vector<std::string>& report,
                   std::map<int, Slot>& placed_was) {
        placed_was.clear();
        auto& ws = wm_unpack(mv_file, ab_file).first;
        std::map<int, std::array<int, 3>> flags_at;
        for (int i0 = 0; i0 < (int)ws.size(); i0++) {
            if (in_wm_block(i0) && !WM_SLOT.count(i0)) continue;
            flags_at[wm_slot(i0)] = { ws[i0].f2, ws[i0].f3, ws[i0].f4 };
        }
        auto goes = [](const uint8_t* rec, int a) { return rec && (be32(rec + a) & GOES_ON); };
        auto shape_wm = [&](int m) { bool g = goes(wm_rec(m), 0x1C); return std::make_pair(g, g && goes(wm_rec(m + 1), 0x1C)); };
        auto shape_nm = [&](int m) { bool g = goes(nm_rec(m), 0x20); return std::make_pair(g, g && goes(nm_rec(m + 1), 0x20)); };
        std::string head = "  " + key + " slot ";
        for (auto& l : report) {
            if (l.rfind(head, 0) != 0 || l.find("WM2000") == std::string::npos) continue;
            std::string how = l.substr(l.rfind('(') + 1);
            while (!how.empty() && (how.back() == ')' || how.back() == ' ' || how.back() == '\n')) how.pop_back();
            if (!not_same(how)) continue;
            int i = (int)py_int(l.substr(l.find(" slot ") + 6, l.find(':', l.find(" slot ") + 6) - (l.find(" slot ") + 6)));
            int m = (int)py_int(py_split(l.substr(l.find("WM2000") + 6))[0]);
            std::vector<int> c = nm_move(m);
            if (c.empty()) continue;
            size_t lists = n_can_moves(i);
            std::set<std::string> why;                  // (sorted(set(why)) on joining)
            if (lists <= 1) why.insert("the slot holds one move for everyone in No Mercy");
            std::vector<int> ok;
            for (int x : c) {
                if (!nm_rec(x) || !wm_rec(m)) { why.insert(strf("%d has no record", x)); continue; }
                if (nm_rec(x)[3] != wm_rec(m)[3]) {
                    why.insert(strf("No Mercy's %d is another kind of move (record byte 3: %d, WrestleMania 2000's %d)", x, nm_rec(x)[3], wm_rec(m)[3]));
                    continue;
                }
                if (shape_nm(x) != shape_wm(m)) { why.insert(strf("No Mercy's %d goes on otherwise", x)); continue; }
                ok.push_back(x);
            }
            if (lists <= 1 || ok.empty()) {
                std::string r = l;
                while (!r.empty() && py_space((uint8_t)r.back())) r.pop_back();
                r += " -- not placed: ";
                bool first = true;
                for (auto& w : why) { if (!first) r += "; "; first = false; r += w; }
                l = r;
                continue;
            }
            int x = ok[0];
            if (!placed_was.count(i)) placed_was[i] = out[i];
            auto fa = flags_at.find(i);
            out[i] = fa != flags_at.end() ? Slot{ x, fa->second[0], fa->second[1], fa->second[2] } : Slot{ x, out[i].f2, out[i].f3, out[i].f4 };
            int was = (int)py_int(py_split(l.substr(l.find("->") + 2))[0]);
            l = strf("  %s slot %3d: WM2000 %5d %-34s -> %5d %-28s (animation, placed: No Mercy's lists hold it in other slots only; was %d by %s)",
                     key.c_str(), i, m, get_name(WM_NAMES, m - m % 10).c_str(), x, nm_name_of(x).c_str(), was, how.c_str());
        }
    }

    // wmlib.py how_of: the report lines -> {slot: (WM2000 move, No Mercy move, how)}
    static std::map<int, HowT> how_of(const std::vector<std::string>& rep) {
        std::map<int, HowT> how;
        for (auto& l : rep) {
            if (l.find(" slot ") == std::string::npos || l.find("WM2000") == std::string::npos) continue;
            size_t s = l.find(" slot ") + 6;
            int i = (int)py_int(l.substr(s, l.find(':', s) == std::string::npos ? std::string::npos : l.find(':', s) - s));
            int wmv = (int)py_int(py_split(l.substr(l.find("WM2000") + 6))[0]);
            int nmv = (int)py_int(py_split(l.substr(l.find("->") + 2))[0]);
            size_t p = l.rfind('(');
            if (p == std::string::npos) fail("wmmoves: a report line without its how");
            std::string h = l.substr(p + 1);
            while (!h.empty() && (h.back() == ')' || h.back() == ' ' || h.back() == '\n')) h.pop_back();
            how[i] = { wmv, nmv, h };
        }
        return how;
    }

    // game.py moveset_of + wmlib.py mapped / wm_moves: a man's slots as the handler maps them
    Mapped& mapped(const std::string& sid, int mv_file, int ab_file, int ref_ord) {
        auto it = maps.find(sid);
        if (it != maps.end()) return it->second;
        Mapped M;
        std::vector<std::string> rep;
        port_wm_moveset(sid, mv_file, ab_file, ref_ord, rep, M.slots, M.ab);
        place_own(sid, mv_file, ab_file, M.slots, rep, M.placed_was);
        M.how = how_of(rep);
        auto& ws = wm_unpack(mv_file, ab_file).first;
        for (int i0 = 0; i0 < (int)ws.size(); i0++) {
            if (in_wm_block(i0) && !WM_SLOT.count(i0)) continue;
            M.wm_at[wm_slot(i0)] = ws[i0].m;
        }
        return maps[sid] = M;
    }

    // ---- Anims ---------------------------------------------------------------------------------------------------------
    bool ok(int f) const { return f >= (int)WM_ANIM0 && f < (int)WM_ANIM_END && wfile(f).size() > 8; }
    const uint8_t* wa_rec(int f) const {
        size_t o = 36 * (size_t)(f - WM_ANIM0);
        if (o + 36 > wanim_rec.size()) fail("wmmoves: WM2000 animation record of 0x%X past its file", f);
        return &wanim_rec[o];
    }
    const uint8_t* na_rec(int g) const {
        size_t o = 36 * (size_t)(g - NM_ANIM0);
        if (o + 36 > nanim_rec.size()) fail("wmmoves: No Mercy animation record of 0x%X past its file", g);
        return &nanim_rec[o];
    }
    void anims_init() {
        nanim_rec = (*N)(NM_ANIM_RECS); wanim_rec = wfile(WM_ANIM_RECS);
        std::map<std::string, int> nm_by;
        for (uint32_t g = NM_ANIM0; g < NM_ANIM0 + NM_ANIM_N; g++) {
            const Bytes* b = N->get(g);
            if (b) nm_by.emplace(md5_hex(*b), (int)g);
        }
        for (uint32_t f = WM_ANIM0; f < WM_ANIM_END; f++) {
            const Bytes& b = wfile(f);
            if (b.size() > 8) { auto it = nm_by.find(md5_hex(b)); if (it != nm_by.end()) nm_of[(int)f] = it->second; }
        }
        // (2026-11-20, revmoves2) a file No Mercy has but for the first byte (the header's end pose and turn): No Mercy's bytes
        // and record, the lowest-numbered such file (wmlib.py tail_of); the rules below are learned from nm_of, as before
        {
            std::map<Bytes, int> nm_tail;
            for (uint32_t g = NM_ANIM0; g < NM_ANIM0 + NM_ANIM_N; g++) {
                const Bytes* b = N->get(g);
                if (b && b->size() > 8) nm_tail.emplace(Bytes(b->begin() + 1, b->end()), (int)g);
            }
            for (uint32_t f = WM_ANIM0; f < WM_ANIM_END; f++) {
                const Bytes& b = wfile(f);
                if (b.size() <= 8 || nm_of.count((int)f)) continue;
                auto it = nm_tail.find(Bytes(b.begin() + 1, b.end()));
                if (it != nm_tail.end()) tail_of[(int)f] = it->second;
            }
        }
        // learn: per record byte the most usual No Mercy value, the sounds' numbers, an animation's first byte
        std::vector<std::map<int, OrderedCounter<int>>> m(36);
        std::vector<int> m_order;                       // (unused: dict order doesn't matter for a lookup table)
        std::map<int, OrderedCounter<int>> sm, bz;
        for (auto& [f, g] : nm_of) {
            const uint8_t* wr = wa_rec(f); const uint8_t* nr = na_rec(g);
            for (int k = 0; k < 36; k++) m[k][wr[k]].add(nr[k]);
            for (int a : SOUND_AT) if (nr[a] && nr[a] == wr[a]) sm[wr[a + 1]].add(nr[a + 1]);
            bz[wfile(f)[0]].add((*N)(g)[0]);
        }
        conv.resize(36);
        for (int k = 0; k < 36; k++) for (auto& [v, c] : m[k]) conv[k][v] = c.most();
        for (auto& [v, c] : sm) snd[v] = c.most();
        for (auto& [v, c] : bz) b0[v] = c.most();
    }
    int intern(const Bytes* data, Bytes rec) {
        auto key = std::make_pair(*data, rec);
        auto it = content_ix.find(key);
        if (it != content_ix.end()) return it->second;
        int n = (int)contents.size();
        content_ix.emplace(std::move(key), n);
        contents.push_back({ data, std::move(rec) });
        return n;
    }
    int get(int f) {                                    // (Anims.get: the content's number)
        auto c = get_cache.find(f);
        if (c != get_cache.end()) return c->second;
        int r;
        int twin = -1;                                  // (No Mercy's file: the same bytes, or (revmoves2) all but the first)
        if (auto it = nm_of.find(f); it != nm_of.end()) twin = it->second;
        else if (auto t2 = tail_of.find(f); t2 != tail_of.end()) twin = t2->second;
        if (twin >= 0) {
            const uint8_t* nr = na_rec(twin);
            r = intern(&(*N)(twin), Bytes(nr, nr + 36));
        } else {
            auto d = std::make_unique<Bytes>(wfile(f));
            auto b = b0.find((*d)[0]); if (b != b0.end()) (*d)[0] = (uint8_t)b->second;
            const uint8_t* wr = wa_rec(f);
            Bytes rec(36);
            for (int k = 0; k < 36; k++) { auto cv = conv[k].find(wr[k]); rec[k] = (uint8_t)(cv == conv[k].end() ? wr[k] : cv->second); }
            for (int k : FRAME_AT) rec[k] = wr[k];
            for (int a : SOUND_AT) { auto s = snd.find(wr[a + 1]); rec[a + 1] = (uint8_t)(s == snd.end() ? wr[a + 1] : s->second); }
            owned.push_back(std::move(d));
            r = intern(owned.back().get(), std::move(rec));
        }
        return get_cache[f] = r;
    }

    // ---- wmlib.py derived_table / steps_of ---------------------------------------------------------------------------
    static std::vector<std::array<int, 3>> derived_table(const Bytes& rom) {
        Bytes start;
        for (int e : { 5, 10, 15 }) { int v[3] = { e, -4, 2 }; for (int x : v) { start.push_back((uint8_t)((x >> 8) & 0xFF)); start.push_back((uint8_t)(x & 0xFF)); } }
        auto it = std::search(rom.begin(), rom.end(), start.begin(), start.end());
        if (it == rom.end()) fail("wmlib: the derived entries' table isn't in this ROM");
        size_t a = it - rom.begin();
        std::vector<std::array<int, 3>> out;
        while (true) {
            int e = sx16(be16(rom, a)), to = sx16(be16(rom, a + 2)), step = sx16(be16(rom, a + 4));
            if (e == -1) return out;
            out.push_back({ e, to <= 0 ? std::max(1, -to) : -to, step }); a += 6;
        }
    }
    static std::map<int, std::vector<std::array<int, 3>>> steps_of(const std::vector<std::array<int, 3>>& table) {
        std::map<int, std::vector<std::array<int, 3>>> out;
        for (auto& [e, n, step] : table)
            if (n > 0) { std::vector<std::array<int, 3>> v; for (int k = 1; k <= n; k++) v.push_back({ e + k, k, step }); out[e] = v; }
        return out;
    }

    // ---- the library -----------------------------------------------------------------------------------------------
    std::string none_of(int b, const std::string& dflt) const {
        std::string r = dflt; bool f = false;
        for (auto& [x, w] : none) if (x == b) { r = w; f = true; }
        (void)f; return r;
    }
    LibMove* lib_move(int b) {
        auto it = of_base.find(b);
        if (it != of_base.end()) return it->second;
        int v0 = at(WM1, b), fa = v0 + 0x19DB;
        if (v0 <= 0 || !(ok(fa) && ok(fa + 1))) {
            none.push_back({ b, strf("its first step has no animation pair (value %d)", v0) }); of_base[b] = nullptr; return nullptr;
        }
        auto [go0, go1] = wm_shape(b);
        bool has_cont = false; std::vector<int> cont;
        if (go0 && at(WM1, b + 1) > 0) {
            int f1 = WM1[b + 1] + 0x19DB;
            bool all = true; for (int k = 0; k < 8; k++) all = all && ok(f1 + k);
            if (all) { has_cont = true; for (int k = 0; k < 8; k++) cont.push_back(f1 + k); }
            else none.push_back({ b, "its next step's files aren't all there: played without them" });
        }
        std::vector<int> key = { get(fa), get(fa + 1) };
        for (int c : cont) key.push_back(get(c));
        auto k = by_key.find(key);
        if (k != by_key.end()) { k->second->same_as.push_back(b); of_base[b] = k->second; return k->second; }
        auto x = std::make_unique<LibMove>();
        x->id = strf("wm2000:%d", b); x->b = b; x->fa = fa; x->has_cont = has_cont; x->cont = cont; x->key = key;
        x->sh0 = go0; x->sh1 = go1;
        LibMove* p = x.get();
        lib.push_back(std::move(x)); by_key[key] = p; of_base[b] = p;
        return p;
    }
    std::pair<bool, bool> want_of(const LibMove* x) const { return x->has_cont ? std::make_pair(x->sh0, x->sh1) : std::make_pair(false, false); }

    int same_steps(int b, int v) {
        int n = 0;
        int vb = v - v % 10;
        for (int k = 0; k < 10; k++) {
            if (b + k >= (int)WM1.size() || vb + k >= (int)NM1.size() || vb + k < 0) break;
            int x = nm_anim(WM1[b + k]), y = NM1[vb + k];
            if (x != NONE && x == y && x) n++;
        }
        return n;
    }
    bool nm_plays(const LibMove* x, int v) {
        int f = (v >= 0 && v < (int)NM1.size() && NM1[v]) ? NM1[v] + 0x3322 : NONE;
        auto of = [&](int c) { auto it = nm_of.find(c); return it == nm_of.end() ? NONE : it->second; };
        if (f == NONE || of(x->fa) != f || of(x->fa + 1) != f + 1) return false;
        if (!x->has_cont) return true;
        int g = at(NM1, v + 1) ? NM1[v + 1] + 0x3322 : NONE;
        if (g == NONE) return false;
        for (size_t k = 0; k < x->cont.size(); k++) if (of(x->cont[k]) != g + (int)k) return false;
        return true;
    }

    // wmlib.py base_ids
    void base_ids(const std::string& sid, const std::map<int, HowT>& how, bool count, SlotIds& S) {
        if (is_ported(sid)) return;
        for (auto& [i, H] : how) {
            if (!not_same(H.how)) continue;
            int wmv = H.wmv, v = H.nmv, b = wmv - wmv % 10;
            auto ob = of_base.find(b);
            if (ob == of_base.end() || !ob->second) {
                S.kept.set(i, strf("WrestleMania 2000's %d: %s", wmv, none_of(b, "no library move").c_str())); continue;
            }
            if (wmv % 10) { S.kept.set(i, strf("WrestleMania 2000's %d is a later step of a move: no library move", wmv)); continue; }
            LibMove* x = ob->second;
            if (v == 0 || v == 0xFFFF) { S.kept.set(i, strf("the slot holds no move in No Mercy (his reference's is %d)", v)); continue; }
            if (n_can_moves(i) == 1) {
                S.kept.set(i, strf("the slot holds one move for everyone in No Mercy (%d): an engine move, kept", v)); continue;
            }
            int on = v; std::string why = H.how;
            auto want = want_of(x);
            if (nm_shape(v) != want) {
                std::vector<int> c;
                for (int m = 0; m < (int)NM1.size() - 9; m += 10)
                    if (fits(i, m) && NM1[m] > 0 && nm_shape(m) == want) c.push_back(m);
                if (c.empty()) { S.kept.set(i, strf("no No Mercy move of the slot's kind goes on as WrestleMania 2000's %d does", wmv)); continue; }
                // max(c, key=(same_steps(b, m), m in CAN[i], -abs(m - v))): the first of the greatest
                auto keyf = [&](int m) { return std::make_tuple(same_steps(b, m), (int)in_can(i, m), -std::abs(m - v)); };
                on = c[0]; auto best = keyf(on);
                for (size_t j = 1; j < c.size(); j++) { auto kk = keyf(c[j]); if (kk > best) { best = kk; on = c[j]; } }
                why = strf("%s; on %d (the slot's %d goes on otherwise)", H.how.c_str(), on, v);
            }
            if (on == v && nm_plays(x, v)) {
                S.same.set(i, strf("No Mercy's %d plays WrestleMania 2000's %d's animations already", v, wmv)); continue;
            }
            S.ids.set(i, Id{ strf("%s@%d", x->id.c_str(), on), wmv, v, on, why });
            if (count) { x->who.insert(sid); x->use.add(on); }
        }
    }

    // wmlib.py variants
    OMap<Var> variants(const std::string& sid, const OMap<Id>& ids) {
        Mapped& M = maps.at(sid);
        OMap<Var> out;
        for (auto& [e, steps] : nm_steps) {
            auto wa = M.wm_at.find(e);
            int m = wa == M.wm_at.end() ? NONE : wa->second;
            const Id* id = ids.find(e);
            int v = id ? id->on : M.slots.at(e).m;
            if (m == NONE || m == 0 || m == 0xFFFF || v == 0 || v == 0xFFFF) continue;
            std::map<int, int> wsteps;
            auto ws = wm_steps.find(wm_slot_inv(e));
            if (ws != wm_steps.end()) for (auto& [d_, k, st] : ws->second) wsteps[k] = st;
            for (auto& [d, k, st] : steps) {
                int nv = v + st * k;
                if (!wsteps.count(k)) { out.set(d, Var{ e, k, NONE, nv, nv, "WrestleMania 2000's slot makes no such entry: No Mercy's kept" }); continue; }
                int wv = m + wsteps[k] * k;
                Var r{ e, k, wv, nv, nv, "" };
                if (wv >= (int)WM1.size() || at(WM1, wv) <= 0) {
                    bool no_nm = !(nv >= 0 && nv < (int)NM1.size()) || NM1[nv] <= 0;
                    r.why = no_nm ? "none" : strf("WrestleMania 2000 has no step %d of this move (its %d has no animation): No Mercy's %d kept", wv % 10, wv, nv);
                    out.set(d, r); continue;
                }
                LibMove* x = lib_move(wv);
                if (!x) { r.why = strf("WrestleMania 2000's %d: %s", wv, none_of(wv, "no library move").c_str()); out.set(d, r); continue; }
                auto want = want_of(x);
                int on = nv;
                if (!(nv >= 0 && nv < (int)NM1.size()) || NM1[nv] <= 0 || !nm_rec(nv) || nm_shape(nv) != want) {
                    std::vector<int> c;
                    for (int b = 0; b < (int)NM1.size() - 9; b += 10) {
                        int s = b + st * k;
                        if (fits(e, b) && s < (int)NM1.size() && at(NM1, s) > 0 && nm_rec(s) && nm_shape(s) == want) c.push_back(b);
                    }
                    if (c.empty()) {
                        r.why = strf("no No Mercy move of slot %d's kind has a step %d that goes on as WrestleMania 2000's %d does: No Mercy's %d kept", e, k * st, wv, nv);
                        out.set(d, r); continue;
                    }
                    auto keyf = [&](int b) { return std::make_tuple(same_steps(m - m % 10, b), (int)in_can(e, b), -std::abs(b - (v - v % 10))); };
                    int bb = c[0]; auto best = keyf(bb);
                    for (size_t j = 1; j < c.size(); j++) { auto kk = keyf(c[j]); if (kk > best) { best = kk; bb = c[j]; } }
                    on = bb + st * k;
                }
                if (on == nv && nm_plays(x, nv)) { r.why = "same"; out.set(d, r); continue; }
                r.on = on; r.why = "library"; r.x = x;
                out.set(d, r);
            }
        }
        return out;
    }

    // wmlib.py records: WrestleMania 2000's record for its move, No Mercy's way, where it says otherwise
    Rec records(int wm_move, int nm_move) const {
        const uint8_t* a = wm_rec(wm_move); const uint8_t* b = nm_rec(nm_move);
        Rec out;
        if (!a || !b) return out;
        for (auto& [k, t] : recmap) {
            int v;
            if (!t) v = a[k];
            else { auto it = t->find(a[k]); if (it == t->end()) continue; v = it->second; }
            if (v != b[k]) out.push_back({ k, v });
        }
        return out;
    }

    // wmlib.py slot_ids
    SlotIds slot_ids(const std::string& sid, const std::map<int, HowT>& how, bool count) {
        SlotIds S;
        base_ids(sid, how, count, S);
        if (is_ported(sid)) return S;
        Mapped& M = maps.at(sid);
        for (int i = 0; i < (int)M.slots.size(); i++) {
            auto wa = M.wm_at.find(i);
            if (wa == M.wm_at.end()) continue;
            int m = wa->second;
            if (m == 0 || m == 0xFFFF || nm_derived_entries.count(i)) continue;
            if (n_can_moves(i) == 1) continue;              // (an engine slot: kept)
            auto hi = how.find(i);
            std::string h = hi == how.end() ? "animation" : hi->second.how;
            Rec r;
            if (const Id* id = S.ids.find(i)) r = records(m, id->on);
            else if ((S.same.has(i) || !not_same(h)) && M.slots[i].m != 0 && M.slots[i].m != 0xFFFF) r = records(m, M.slots[i].m);
            else continue;
            if (!r.empty()) S.recs[i] = r;
        }
        OMap<Var> vars = variants(sid, S.ids);
        for (auto& [d, st] : vars.items) {
            Rec r;
            if (st.why == "library") {
                LibMove* x = st.x;
                std::string h = strf("step %d of slot %d's WrestleMania 2000 %d", st.wv % 10, st.base, st.wv - st.wv % 10);
                if (st.on != st.nv) h += strf("; on %d (No Mercy's %d has no such step)", st.on, st.nv);
                S.ids.set(d, Id{ strf("%s@%d", x->id.c_str(), st.on), st.wv, st.nv, st.on, h });
                if (count) { x->who.insert(sid); x->use.add(st.on); }
                r = records(st.wv, st.on);
            } else if (st.why == "same") {
                S.same.set(d, strf("No Mercy's %d plays WrestleMania 2000's %d's animations already (step %d of slot %d)", st.nv, st.wv, st.wv % 10, st.base));
                r = records(st.wv, st.nv);
            } else if (st.why != "none") S.kept.set(d, st.why);
            if (!r.empty()) S.recs[d] = r;
        }
        return S;
    }

    // ---- set up (Library.__init__ and the modules it imports) ----------------------------------------------------------
    void init(const fs::path& ri_data, const fs::path& wm_wrestlers) {
        init_base(ri_data, R.files("wm2000"));
        wrom = R.rom("wm2000");
        init_library(wm_wrestlers);
    }
    // what port_wm_moveset needs: the game's files (files: WrestleMania 2000's, or a mod's of it -- Badd Blood), No Mercy's
    // (c2hands 2026-11-19) wm_optional: No Mercy's part alone when WrestleMania 2000 isn't there (the built-in ports' tables
    // without it: ri_ports -- its men's rows zero); then nothing of WrestleMania 2000's may be asked for
    void init_base(const fs::path& ri_data, const RomFiles* files, bool wm_optional = false) {
        W = files; WH = R.files("wm2000"); N = R.files("nomercy"); nrom = R.rom("nomercy");
        // (arenasplit 2026-11-24) Badd Blood without WrestleMania 2000: it is a game of WrestleMania 2000's engine, its files
        // laid out as WrestleMania 2000's -- its own files where WrestleMania 2000's were read (the animations' hashes outside
        // its own range), and its own code (its Select's image, bb_vsel) where WrestleMania 2000's unpacked the moves
        bb_alone = !WH && W && W == R.files("baddblood");
        if (bb_alone) WH = W;
        const bool wm = W && WH;
        if (!N || (!wm && !wm_optional)) fail("wmmoves: the WrestleMania 2000 and No Mercy ROMs are needed");
        // the tables
        if (wm) { WM1 = s16s(wfile(0x3AE)); WM2 = s16s(wfile(0x3AF)); }
        NM1 = s16s((*N)(0x1F0)); NM2 = s16s((*N)(0x1F1));
        if (wm) wrec = wfile(WM_MOVE_RECS);
        nrec = (*N)(NM_MOVE_RECS);
        if (wm) WM_NAMES = names_of(*W, { 0x95, 0x96, 0x97 }, 0x94);
        NM_NAMES = names_of(*N, { 0x6A, 0x6B, 0x6C, 0x6D, 0x6E }, 0x69);
        for (auto& [k, v] : move_names(R, ri_data / "names_index.json").nomercy) vnames[k] = utf8_to_latin1(v);
        for (uint32_t k = 0x2DD9; k <= 0x4473; k++) { const Bytes* b = N->get(k); if (b) by_hash.emplace(md5_hex(*b), (int)k); }
        for (int m = 0; m < (int)NM1.size() - 9; m += 10) index[std::vector<int>(NM1.begin() + m, NM1.begin() + m + 10)].push_back(m);
        // No Mercy at its Select (mkroster.py van / EV), its packing's lists (slot_lists, CAN), its movesets (van_sets)
        Bytes van = make_image("van_sel", ri_data / "loadmaps", R);
        put_be32(&van[0x5EFC], 0x27BDFFE0); put_be32(&van[0x5F00], 0xAFB00010);   // (the rig's hook out of the game's unpack)
        EV = std::make_unique<Emu>(van);
        Emu& E = *EV;
        uint32_t lay = E.r32(0x800442DC);
        for (uint32_t p = lay; sx16(E.r16(p)) != -1; p += 10) {
            std::vector<int> cs; for (int i = 0; i < 4; i++) cs.push_back(sx16(E.r16(p + 2 + 2 * i)));
            LAYOUT.push_back({ sx16(E.r16(p)), cs });
        }
        int ncat = 0; for (auto& [s, cs] : LAYOUT) for (int c : cs) ncat = std::max(ncat, c + 1);
        for (int c = 0; c < ncat; c++) {
            int w = sx16(E.r16(0x800442E4 + 2 * c));
            uint32_t t = E.r32(0x80045500 + 4 * c);
            std::vector<int> v; for (int k = 0; k < (1 << w); k++) v.push_back(E.r16(t + 2 * k));
            VALS.push_back(v);
        }
        for (auto& [s, cs] : LAYOUT) {
            PySet S;
            for (int c : cs) for (int v : VALS[c]) S.add(v);
            CAN[s] = S;
            std::vector<int> kind; for (int v : S.order()) if (v != 0 && v != 0xFFFF) kind.push_back(v);
            std::sort(kind.begin(), kind.end());
            CAN_kind[s] = kind;
        }
        uint32_t vm = E.r32(0x80047E94);
        for (int o = 0; o < 84; o++) van_sets.push_back(o == 14 ? Bytes() : E.rbytes(vm + o * 0xD8, 0xD8));
        if (wm) wm2k_ram = make_image(bb_alone ? "bb_vsel" : "wm2k_ram", ri_data / "loadmaps", R);   // (WrestleMania 2000's own code unpacks the moves files)
    }
    void init_library(const fs::path& wm_wrestlers) {
        // the 58 men (wmlib.py men: the fixes by roster index)
        std::vector<fs::path> ps;
        for (auto& de : fs::directory_iterator(wm_wrestlers))
            if (de.path().extension() == ".json" && de.path().filename().string().find(".photo") == std::string::npos) ps.push_back(de.path());
        std::sort(ps.begin(), ps.end());
        for (auto& p : ps) {
            Bytes t = read_file(p);
            auto j = nlohmann::ordered_json::parse(std::string(t.begin(), t.end()));
            if (!j.contains("string_id") || !j.contains("wm2000")) continue;
            who_.push_back({ j["string_id"].get<std::string>(), j["wm2000"]["roster_index"].get<int>(),
                             j["moveset"]["reference_ordinal"].get<int>(), is_ported(j["string_id"].get<std::string>()) });
        }
        std::stable_sort(who_.begin(), who_.end(), [](const Who& a, const Who& b) { return a.roster_index < b.roster_index; });
        roster = wm_roster(wm_main_ram(*wrom));
        roster_img = wm_roster(wm2k_ram);
        // ---- Library.__init__ ----
        anims_init();
        std::set<int> used;
        for (auto& w : who_) {
            const WmRecord& e = roster.at(w.roster_index);
            for (auto& s : wm_unpack(e.moves, e.abilities).first) if (s.m != 0 && s.m != 0xFFFF) used.insert(s.m - s.m % 10);
        }
        for (int b : used) if (nm_move(b).empty()) bases.push_back(b);
        for (int b : bases) lib_move(b);
        nm_steps = steps_of(derived_table(*nrom)); wm_steps = steps_of(derived_table(*wrom));
        for (auto& [e, st] : nm_steps) for (auto& s : st) nm_derived_entries.insert(s[0]);
        record_map();
        size_t n_base = lib.size();
        for (auto& w : who_) {
            if (w.ported) continue;
            const WmRecord& e = roster.at(w.roster_index);
            Mapped& M = mapped(w.sid, e.moves, e.abilities, w.reference_ordinal);
            SlotIds S; base_ids(w.sid, M.how, false, S);
            OMap<Var> vars = variants(w.sid, S.ids);
            for (auto& [d, st] : vars.items) if (st.wv != NONE && st.why == "library") lib_move(st.wv);
        }
        n_steps_lib = lib.size() - n_base;
    }

    // wmlib.py record_map: WrestleMania 2000's move record byte -> No Mercy's, bytes 0 .. 0x11
    void record_map(int min_n = 4, double share = 0.9, double ident = 0.99, int nbytes = 0x12) {
        std::vector<std::pair<int, int>> P;
        for (int b = 0; b < (int)WM1.size() - 9; b += 10) {
            std::vector<int> c = nm_move(b);
            if (c.empty()) continue;
            for (int k = 0; k < 10; k++)
                if (WM1[b + k] > 0 && wm_rec(b + k) && nm_rec(c[0] + k)) P.push_back({ b + k, c[0] + k });
        }
        for (int k = 0; k < nbytes; k++) {
            size_t eq = 0;
            for (auto& [a, b] : P) eq += wm_rec(a)[k] == nm_rec(b)[k];
            if ((double)eq >= ident * (double)P.size()) { recmap.push_back({ k, std::nullopt }); continue; }
            std::vector<std::pair<int, OrderedCounter<int>>> by;      // (defaultdict, first-insertion order)
            for (auto& [a, b] : P) {
                int wv = wm_rec(a)[k];
                auto it = std::find_if(by.begin(), by.end(), [&](auto& x) { return x.first == wv; });
                if (it == by.end()) { by.push_back({ wv, {} }); it = by.end() - 1; }
                it->second.add(nm_rec(b)[k]);
            }
            std::map<int, int> t;
            for (auto& [v, c] : by) {
                long sum = 0, top = 0;
                for (auto& [x, n] : c.items) { sum += n; top = std::max(top, n); }
                if (sum >= min_n && (double)top >= share * (double)sum) t[v] = c.most();
            }
            recmap.push_back({ k, t });
        }
        rec_pairs = P.size();
    }

    // ---- revenge\game.py pack_plain (= baddblood\game.py pack_moveset) ----------------------------------------------
    std::pair<Bytes, int> pack_plain(const std::vector<Slot>& slots, const std::vector<int>& ab, int ref_ord) {
        auto rslots = van_unpacked(ref_ord).first;
        std::vector<Slot> fixed;
        for (int i = 0; i < (int)slots.size(); i++) {
            Slot s = slots[i];
            if (CAN.count(i) && !CAN.at(i).has(s.m)) s.m = rslots[i].m;
            fixed.push_back(s);
        }
        Bytes packed;
        for (int t = 0; t < 4; t++) {
            packed = nm_pack(fixed, ab);
            auto back = nm_unpack(packed.data(), packed.data() + 0xB8).first;
            std::vector<int> bad;
            for (int i = 0; i < 292; i++) if (!(back[i] == fixed[i])) bad.push_back(i);
            if (bad.empty()) break;
            for (int i : bad) fixed[i] = rslots[i];
        }
        int n = 0;
        for (size_t i = 0; i < fixed.size() && i < slots.size(); i++) n += !(fixed[i] == slots[i]);
        return { packed, n };
    }

    // ---- fixes2.py win_pose's table: where No Mercy's movesets (nm_sets.pkl: ordinals 0..83 but 14) hold each taunt ------
    void make_taunts() {
        if (taunts_made) return;
        for (int o = 0; o < 84; o++) {
            if (van_sets[o].empty()) continue;
            auto& s = van_unpacked(o).first;
            for (int k = 224; k < 236; k++) if (s[k].m != 0 && s[k].m != 0xFFFF) taunt_slots[s[k].m].insert(k);
        }
        taunts_made = true;
    }
};

// ---- ABOUT (wmlib.py): moves.json's "about" --------------------------------------------------------------------------
static const char* const ABOUT =
    "his moveset, slot by slot: WrestleMania 2000's moves file mapped to No Mercy's moves (gen/mkroster.py "
    "port_wm_moveset: by the very animation, then by name, else a stand-in or his reference's); a slot whose "
    "WrestleMania 2000 move No Mercy has no move for names it in the built-in WrestleMania 2000 library "
    "(\"wm2000:<its move>@<the No Mercy move it's played on>\": %d of his slots, the later steps of his moves -- a "
    "grapple's variants, the entries after it -- included; tools\\handlers\\wmlib.py); \"records\": WrestleMania "
    "2000's own record values (damage, timing) for the slots that play its moves, where its record says otherwise";

static std::string hex_of(const Bytes& b) {
    static const char* H = "0123456789abcdef";
    std::string s; for (uint8_t c : b) { s += H[c >> 4]; s += H[c & 15]; }
    return s;
}
static ojson rec_json(const std::vector<std::pair<int, int>>& r) {
    ojson o = ojson::object(); for (auto& [k, v] : r) o[std::to_string(k)] = v; return o;
}

WmMoveLib::WmMoveLib(Roms& R, const fs::path& ri_data_dir, const fs::path& wm_wrestlers_dir) : p_(std::make_unique<Impl>(R)) {
    p_->init(ri_data_dir, wm_wrestlers_dir);
}
WmMoveLib::~WmMoveLib() = default;
const std::vector<WmMoveLib::Who>& WmMoveLib::who() const { return p_->who_; }

// wm2k_import.py one() (the moveset's part) + wm2k_refresh.py
WmMoves WmMoveLib::man(const std::string& sid, const WmRecord& e, int ref) {
    Impl& I = *p_;
    Impl::Mapped& M = I.mapped(sid, e.moves, e.abilities, ref);
    Impl::SlotIds S = I.slot_ids(sid, M.how, false);
    // the packed moveset as the old slot rule had it (PLACED_WAS), packed by the game's code (pack_moveset)
    std::vector<Slot> pack_slots = M.slots;
    for (auto& [i, s] : M.placed_was) pack_slots[i] = s;
    auto [packed, fell] = I.pack_plain(pack_slots, M.ab, ref);
    // moves.json's slots (slots_with_ids) and abilities
    struct JS { std::string id; Slot s; };
    std::vector<JS> js;
    for (auto& s : M.slots) js.push_back({ "", s });
    for (auto& [i, id] : S.ids.items) js[i].id = id.id;
    std::vector<int> abil = M.ab;
    WmMoves out; out.fell = fell;
    // ---- wm2k_refresh.py: his stance WrestleMania 2000's (ability 1), his winning taunt a standing one ----
    const WmRecord& ri = I.roster_img.at(e.k);                 // (the refresh reads the roster from wm2k.py's RAM image)
    int wab1 = I.wm_unpack(ri.moves, ri.abilities).second[1];
    auto pab = I.nm_unpack(packed.data(), packed.data() + 0xB8).second;
    if (pab[1] != wab1) {
        // fixes2.py repack_abilities: the moves' bytes as they are, the abilities packed again
        auto un = I.nm_unpack(packed.data(), packed.data() + 0xB8);
        std::vector<int> nab = pab; nab[1] = wab1;
        Bytes nb(packed.begin(), packed.begin() + 0xB8);
        Bytes pk = I.nm_pack(un.first, nab);
        nb.insert(nb.end(), pk.begin() + 0xB8, pk.end());
        auto back = I.nm_unpack(nb.data(), nb.data() + 0xB8);
        if (back.second != nab || !(back.first == un.first)) fail("%s: the packing changes his abilities", sid.c_str());
        out.stance_note = strf("ability 1 (his stance) %d, WrestleMania 2000's own for him -- was %d, his reference's "
                               "(2026-11-15, wm2k_refresh.py)", wab1, pab[1]);
        packed = nb;
        if (!abil.empty()) abil[1] = wab1;
    }
    // fixes2.py win_pose
    I.make_taunts();
    {
        JS& w = js[235];
        int m = w.id.empty() ? w.s.m : NONE;
        if (!w.id.empty() && w.id.find('@') != std::string::npos) {
            std::string t = w.id.substr(w.id.find('@') + 1);
            m = (int)py_int(t.substr(0, t.find('/')));
        }
        auto ts = I.taunt_slots.find(m);
        if (m != NONE && ts != I.taunt_slots.end() && ts->second == std::set<int>{ 232 }) {
            for (int s : { 228, 229, 230 }) {
                JS& o = js[s];
                if (!o.id.empty() || (o.s.m != 0 && o.s.m != 0xFFFF)) {
                    std::string old = w.id.empty() ? std::to_string(w.s.m) : w.id;
                    std::string nw = o.id.empty() ? std::to_string(o.s.m) : o.id;
                    w.id = o.id; w.s.m = o.s.m;
                    out.win_pose_note = strf("winning taunt (slot 235): %s was a turnbuckle taunt (No Mercy has it only in slot 232) -- "
                                             "now his own standing taunt, %s (2026-11-15)", old.c_str(), nw.c_str());
                    break;
                }
            }
        }
    }
    // moves.json (wm2k_import.py moves_json, then the refresh's json.dump of it)
    ojson j = ojson::object();
    j["about"] = strf(ABOUT, (int)S.ids.size());
    ojson sl = ojson::array();
    for (auto& s : js) {
        ojson a = ojson::array();
        if (s.id.empty()) a.push_back(s.s.m); else a.push_back(s.id);
        a.push_back(s.s.f2); a.push_back(s.s.f3); a.push_back(s.s.f4);
        sl.push_back(a);
    }
    j["slots"] = sl;
    j["abilities"] = abil;
    if (!S.recs.empty()) {
        ojson r = ojson::object();
        for (auto& [i, v] : S.recs) r[std::to_string(i)] = rec_json(v);
        j["records"] = r;
    }
    out.moves_json = py_dumps(j);
    out.packed_hex = hex_of(packed);
    return out;
}

// wmlib.py build + Library.write
void WmMoveLib::write_lib(const fs::path& out) {
    Impl& I = *p_;
    for (auto& x : I.lib) { x->who.clear(); x->use = OrderedCounter<int>(); }
    struct Row { bool ported; Impl::SlotIds S; std::map<int, Impl::HowT> how; };
    std::vector<std::pair<std::string, Row>> rows;
    for (auto& w : I.who_) {
        if (w.ported) { rows.push_back({ w.sid, Row{ true, {}, {} } }); continue; }
        const WmRecord& e = I.roster.at(w.roster_index);
        Impl::Mapped& M = I.mapped(w.sid, e.moves, e.abilities, w.reference_ordinal);
        rows.push_back({ w.sid, Row{ false, I.slot_ids(w.sid, M.how, true), M.how } });
    }
    // ---- the animations, one copy each ----
    std::map<int, uint32_t> anim_ix; std::vector<int> anims;
    auto anim_of = [&](int c) { auto it = anim_ix.find(c); if (it != anim_ix.end()) return it->second; uint32_t n = (uint32_t)anims.size(); anim_ix[c] = n; anims.push_back(c); return n; };
    for (auto& xp : I.lib) {
        auto& x = *xp;
        x.pair_ix = anim_of(x.key[0]); anim_of(x.key[1]);
        if (anim_ix[x.key[1]] != x.pair_ix + 1) { x.pair_ix = (uint32_t)anims.size(); anims.push_back(x.key[0]); anims.push_back(x.key[1]); }
        if (x.has_cont) {
            std::vector<long> ix;
            bool all = true;
            for (size_t k = 2; k < x.key.size(); k++) { auto it = anim_ix.find(x.key[k]); if (it == anim_ix.end()) { all = false; ix.push_back(-1); } else ix.push_back(it->second); }
            bool run = all; for (size_t k = 0; all && k < ix.size(); k++) run = run && ix[k] == ix[0] + (long)k;
            if (run) x.block_ix = (uint32_t)ix[0];
            else { x.block_ix = (uint32_t)anims.size(); for (size_t k = 2; k < x.key.size(); k++) anims.push_back(x.key[k]); }
        } else x.block_ix = 0xFFFFFFFF;
        x.base = x.use.items.empty() ? 0 : x.use.most();
    }
    // (2026-11-20, revmoves) A double team's third animation, the man's (No Mercy asks the lead's move for file + 2:
    // wrestler_state_133_double_teamed_man): WrestleMania 2000 has it after the pair, as No Mercy does (its 4190: 0x2675 /
    // 76 the two men, 77 the man). For a move usually played in a double team's place (No Mercy's lists of entries 163-165),
    // a copy after every other animation, named in the extension after the data (ri_movelib.cpp's; wmlib.py the same).
    struct More { uint32_t row, first, n; };
    std::vector<More> more;
    for (size_t k = 0; k < I.lib.size(); k++) {
        auto& x = *I.lib[k];
        bool dt = false;
        for (int e : { 163, 164, 165 }) dt = dt || (x.base && I.in_can(e, x.base));
        if (!dt || !I.ok(x.fa + 2)) continue;
        more.push_back({ (uint32_t)k, (uint32_t)anims.size(), 1 });
        anims.push_back(I.get(x.fa + 2));
    }
    // ---- movelib_wm2000.bin: revlib.py's layout (NMMOVLIB 1) ----
    Bytes strings, data, mv, an;
    auto le32 = [](Bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); };
    auto le16 = [](Bytes& b, uint32_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    for (auto& x : I.lib) {
        uint32_t off = (uint32_t)strings.size();
        strings.insert(strings.end(), x->id.begin(), x->id.end()); strings.push_back(0);
        le32(mv, off); le16(mv, (uint32_t)x->base); le16(mv, 0); le32(mv, x->pair_ix); le32(mv, x->block_ix);
    }
    for (int c : anims) {
        const Bytes& b = *I.contents[c].first; const Bytes& rec = I.contents[c].second;
        le32(an, (uint32_t)data.size()); le32(an, (uint32_t)b.size()); an.insert(an.end(), rec.begin(), rec.end());
        data.insert(data.end(), b.begin(), b.end());
        data.resize(data.size() + ((8 - b.size() % 8) % 8), 0);
    }
    Bytes ms = mv; ms.insert(ms.end(), strings.begin(), strings.end());
    uint32_t stamp = (uint32_t)std::stoul(md5_hex(ms).substr(0, 8), nullptr, 16);
    Bytes blob = { 'N', 'M', 'M', 'O', 'V', 'L', 'I', 'B' };
    for (uint32_t v : { 1u, (uint32_t)I.lib.size(), (uint32_t)anims.size(), stamp, (uint32_t)strings.size(), (uint32_t)data.size() }) le32(blob, v);
    for (Bytes* p : { &mv, &an, &strings, &data }) blob.insert(blob.end(), p->begin(), p->end());
    if (!more.empty()) {                             // (2026-11-20, revmoves: the extension)
        for (char c : std::string("NMLIBX01")) blob.push_back((uint8_t)c);
        le32(blob, (uint32_t)more.size());
        for (auto& m : more) { le32(blob, m.row); le32(blob, m.first); le32(blob, m.n); }
    }
    fs::create_directories(out);
    write_file(out / "movelib_wm2000.bin", blob);
    // ---- move_lib_wm2000.json ----
    ojson L = ojson::object();
    for (size_t k = 0; k < I.lib.size(); k++) {
        auto& x = *I.lib[k];
        std::vector<int> fs_ = { x.fa, x.fa + 1 }; fs_.insert(fs_.end(), x.cont.begin(), x.cont.end());
        ojson o = ojson::object();
        o["place"] = (int)k + 1; o["base"] = x.base; o["wm_move"] = x.b;
        o["name"] = latin1_to_utf8(Impl::get_name(I.WM_NAMES, x.b));
        o["same_as"] = x.same_as;
        ojson f = ojson::array(), nf = ojson::array();
        for (int c : fs_) {
            f.push_back(strf("0x%04X", c));
            auto it = I.nm_of.find(c);
            if (it != I.nm_of.end()) nf.push_back(strf("0x%04X", it->second)); else nf.push_back(nullptr);
        }
        o["files"] = f; o["no_mercy_files"] = nf; o["goes_on"] = x.has_cont;
        ojson w = ojson::array(); for (auto& s : x.who) w.push_back(s);
        o["who"] = w;
        long n = 0; for (auto& [v, c] : x.use.items) n += c;
        o["n_slots"] = n;
        L[x.id] = o;
    }
    // (json.dump(indent=1) into a file opened in text mode on Windows, as wmlib.py writes them: each new line CR LF)
    auto crlf = [](const std::string& s) { std::string o; for (char c : s) { if (c == 10) o += (char)13; o += c; } return o; };
    PyDump h1; h1.indent = 1;
    write_file(out / "move_lib_wm2000.json", crlf(py_dumps(L, h1)));
    // ---- rows_wm2000.json ----
    ojson RJ = ojson::object();
    for (auto& [sid, r] : rows) {
        ojson o = ojson::object();
        o["ported"] = r.ported;
        ojson ids = ojson::object(), kept = ojson::object(), same = ojson::object(), recs = ojson::object(), hd = ojson::object();
        for (auto& [i, v] : r.S.ids.items) {
            ojson x = ojson::object(); x["id"] = v.id; x["wm"] = v.wm; x["was"] = v.was; x["on"] = v.on; x["how"] = v.how;
            ids[std::to_string(i)] = x;
        }
        for (auto& [i, v] : r.S.kept.items) kept[std::to_string(i)] = v;
        for (auto& [i, v] : r.S.same.items) same[std::to_string(i)] = v;
        for (auto& [i, v] : r.S.recs) recs[std::to_string(i)] = rec_json(v);
        for (auto& [i, H] : r.how) if (not_same(H.how)) { ojson x = ojson::object(); x["wm"] = H.wmv; x["nm"] = H.nmv; x["how"] = H.how; hd[std::to_string(i)] = x; }
        o["ids"] = ids; o["kept"] = kept; o["same"] = same; o["records"] = recs; o["handler"] = hd;
        RJ[sid] = o;
    }
    write_file(out / "rows_wm2000.json", crlf(py_dumps(RJ, h1)));
}

// ---- Badd Blood's men (baddblood\game.py moveset + pack_moveset, imports2.py moves_json) -----------------------------------
const char* const VADER_MOVES_ABOUT = "his moveset, slot by slot: Badd Blood's moves file mapped to No Mercy's moves (gen/mkroster.py "
                                      "port_wm_moveset, by animation, then by name, else a stand-in or Viscera's)";
const char* const GOLDUST_MOVES_ABOUT = "his moveset, slot by slot: Badd Blood's Goldust's moves file mapped to No Mercy's moves "
                                        "(gen/mkroster.py port_wm_moveset)";
WmPortedMoveset baddblood_moveset(Roms& R, const fs::path& ri_data_dir, const WmRecord& e, int ref, const std::string& key,
                                  const std::string& about) {
    const RomFiles* bb = R.files("baddblood");
    if (!bb) fail("wmmoves: the Badd Blood ROM isn't there");
    WmMoveLib::Impl I(R);
    I.init_base(ri_data_dir, bb);
    std::vector<std::string> rep; std::vector<Slot> slots; std::vector<int> ab;
    I.port_wm_moveset(key, e.moves, e.abilities, ref, rep, slots, ab);
    auto [packed, fell] = I.pack_plain(slots, ab, ref);
    ojson j = ojson::object();
    j["about"] = about;
    ojson sl = ojson::array();
    for (auto& s : slots) sl.push_back(ojson::array({ s.m, s.f2, s.f3, s.f4 }));
    j["slots"] = sl; j["abilities"] = ab;
    return { py_dumps(j), hex_of(packed), fell };
}


// ---- MkRosterBase: the facade over the same machinery (ri_wmmoves.h) ---------------------------------------------------------
namespace {
MovesetUnpacked to_pub(const std::pair<std::vector<Slot>, std::vector<int>>& u) {
    MovesetUnpacked o; o.second = u.second;
    for (auto& s : u.first) o.first.push_back({ s.m, s.f2, s.f3, s.f4 });
    return o;
}
std::vector<Slot> from_pub(const std::vector<MoveSlot>& v) { std::vector<Slot> o; for (auto& s : v) o.push_back({ s.m, s.f2, s.f3, s.f4 }); return o; }
}  // namespace
std::string mkroster_norm(const std::string& latin1) { return norm(latin1); }
MkRosterBase::MkRosterBase(Roms& R, const fs::path& ri_data_dir) : p_(std::make_unique<WmMoveLib::Impl>(R)) {
    p_->init_base(ri_data_dir, R.files("wm2000"), true);
}
MkRosterBase::~MkRosterBase() = default;
Emu& MkRosterBase::ev() { return *p_->EV; }
const Bytes& MkRosterBase::van_set(int o) const { return p_->van_sets.at((size_t)o); }
MovesetUnpacked MkRosterBase::nm_unpack(const uint8_t* moves, const uint8_t* params) { return to_pub(p_->nm_unpack(moves, params)); }
Bytes MkRosterBase::nm_pack(const std::vector<MoveSlot>& slots, const std::vector<int>& ab) { return p_->nm_pack(from_pub(slots), ab); }
MovesetUnpacked MkRosterBase::van_unpacked(int o) { return to_pub(p_->van_unpacked(o)); }
const std::vector<std::pair<int, std::vector<int>>>& MkRosterBase::layout() const { return p_->LAYOUT; }
const std::vector<std::vector<int>>& MkRosterBase::vals() const { return p_->VALS; }
bool MkRosterBase::can_has(int slot) const { return p_->CAN.count(slot) > 0; }
bool MkRosterBase::in_can(int slot, int v) const { return p_->in_can(slot, v); }
std::vector<int> MkRosterBase::can_order(int slot) const { return p_->CAN.at(slot).order(); }
const std::vector<int>& MkRosterBase::can_kind(int slot) const { return p_->CAN_kind.at(slot); }
int MkRosterBase::stand_in(int slot, const std::string& name, int ref) { return p_->stand_in(slot, name, ref); }
// (c2hands) WrestleMania 2000's parts: only with its ROM
static void need_wm(const RomFiles* w) { if (!w) fail("wmmoves: WrestleMania 2000's moves asked for without its ROM"); }
std::vector<int> MkRosterBase::wm_nm_move(int m) { need_wm(p_->W); return p_->nm_move(m); }
MovesetUnpacked MkRosterBase::wm_unpack(int mv_file, int ab_file) { need_wm(p_->W); return to_pub(p_->wm_unpack(mv_file, ab_file)); }
void MkRosterBase::port_wm_moveset(const std::string& key, int mv_file, int ab_file, int ref_ord, std::vector<std::string>& report,
                                   std::vector<MoveSlot>& out, std::vector<int>& ab) {
    need_wm(p_->W);
    std::vector<Slot> o;
    p_->port_wm_moveset(key, mv_file, ab_file, ref_ord, report, o, ab);
    out.clear(); for (auto& s : o) out.push_back({ s.m, s.f2, s.f3, s.f4 });
}
const std::map<int, std::string>& MkRosterBase::vnames() const { return p_->vnames; }
std::string MkRosterBase::vname(int m) const { return WmMoveLib::Impl::get_name(p_->vnames, m); }

}  // namespace ri
