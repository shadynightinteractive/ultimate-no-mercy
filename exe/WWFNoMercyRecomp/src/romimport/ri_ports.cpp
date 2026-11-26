// ri_ports.cpp (phase3 p3ports, 2026-11-19): the built-in ports' tables (ri_ports.h).
//
// Layout: the shared state of gen/mkroster.py (its LATE table, its overrides, No Mercy's packing and movesets through
// MkRosterBase) and of data/revport/revmoves.py (the Revenge lists, the slots' places, the pools, the files' body hashes);
// then each step as a function of its own, in the Python's order of work, its texts made as the Python makes them.
#include "ri_ports.h"
#include "ri_data.h"
#include "ri_hdrfill.h"
#include "ri_images.h"
#include "ri_json.h"
#include "ri_mips.h"
#include "ri_movelib.h"
#include "ri_wmmoves.h"
#include "ri_wmport.h"
#include <algorithm>
#include <climits>
#include <map>
#include <optional>
#include <regex>
#include <set>

namespace ri {

namespace {

constexpr int NONE = INT_MIN;                 // Python's None where an int is expected

// ---- gen/mkroster.py LATE: the Superstars after Benoit's place, in the table's order (ext_late index = position) ---------
// Ours (the developer's choices: names, IDs, music and video slots, heights / weights where the source has none, the
// reference Superstar whose moves stand in, the allies). `video_of`: the video is wmport's for that name (WM_VIDEO).
struct Late {
    const char* key; const char* name; const char* short_name; int id;
    int music0, music1, video0, video1;      // (video -1: WM_VIDEO[video_of])
    int h, w;                                // (-1: No Mercy Plus's, from its record)
    int nm;                                  // No Mercy Plus V3 ordinal (-1: none)
    int wm[3];                               // WrestleMania 2000 (ordinal, moves file, abilities file) ({-1}: none)
    int ref;                                 // the reference ordinal (-1: none)
    int rev;                                 // the Revenge moveset file (-1: none)
    std::vector<int> allies;
    int same = -1;                           // (No Mercy's ordinal whose moveset is his exactly: gen/mkroster.py same=; none now)
    const char* video_of = nullptr;
    const char* base = nullptr;              // (a second ID, gen/revbanks.py: his base's key)
};
const std::vector<Late>& late_table() {
    static const std::vector<Late> L = [] {
        std::vector<Late> v = {
            { "jarrett", "JEFF JARRETT", "JARRETT", 0x030B, 14, 14, -1, -1, 13, 130, -1, { 29, 0x3F8, 0x3F7 }, 12, -1, {}, -1, "jarrett" },
            // (nmplus 2026-11-19, the user: no item may need two source games -- Droz, Mideon and the 1999 Undertaker are No
            //  Mercy Plus's alone: their moves no longer filled from WrestleMania 2000's (its ordinals 31 / 14 / 6), whose own
            //  Droz, Mideon and Undertaker are its imported wrestlers; a slot No Mercy Plus's moves leave empty takes the
            //  reference's move, as for its other men)
            { "droz", "DROZ", "DROZ", 0x0509, 7, 7, 45, 45, 16, 170, 75, { -1, -1, -1 }, 40, -1, {} },
            { "mideon", "MIDEON", "MIDEON", 0x050A, 43, 43, 47, 47, 15, 188, 79, { -1, -1, -1 }, 34, -1, {} },
            { "meanie", "THE BLUE MEANIE", "MEANIE", 0x060B, 29, 29, -1, -1, 13, 192, -1, { 41, 0x40C, 0x40B }, 26, -1, {}, -1, "meanie" },
            { "petegas", "PETE GAS", "GAS", 0x030C, 58, 58, 50, 50, -1, -1, 80, { -1, -1, -1 }, 13, -1, {} },
            { "rodney", "RODNEY", "RODNEY", 0x030D, 58, 58, 50, 50, -1, -1, 81, { -1, -1, -1 }, 11, -1, {} },
            { "joeyabs", "JOEY ABS", "ABS", 0x030E, 58, 58, 50, 50, -1, -1, 82, { -1, -1, -1 }, 21, -1, {} },
            { "taker99", "UNDERTAKER", "UNDERTAKER", 0x000A, 59, 59, 51, 51, -1, -1, 83, { -1, -1, -1 }, 3, -1, {} },
            { "raven", "RAVEN", "RAVEN", 0x060C, 60, 60, 49, 49, -1, -1, 84, { -1, -1, -1 }, 41, -1, {} },
            { "regal", "WILLIAM REGAL", "REGAL", 0x030F, 61, 61, 54, 54, -1, -1, 86, { -1, -1, -1 }, 25, -1, {} },
            { "tiger", "TIGER ALI SINGH", "SINGH", 0x0409, 62, 62, 52, 52, -1, -1, 89, { -1, -1, -1 }, 22, -1, {} },
            { "brethart", "BRET HART", "HART", 0x060D, 63, 63, 0, 0, 13, 134, -1, { -1, -1, -1 }, 15, 0x143, { 0x060E, 0x0603 } },
            { "neidhart", "JIM NEIDHART", "NEIDHART", 0x060E, 64, 64, 0, 0, 14, 173, -1, { -1, -1, -1 }, 34, 0x1C1, { 0x060D, 0x0603 } },
            { "hogan", "HOLLYWOOD HOGAN", "HOGAN", 0x0105, 65, 65, 0, 0, 19, 175, -1, { -1, -1, -1 }, 1, 0x17F, { 0x0106, 0x0107 } },
            { "nash", "KEVIN NASH", "NASH", 0x0106, 66, 66, 0, 0, 25, 267, -1, { -1, -1, -1 }, 3, 0x1BE, { 0x0107, 0x0105, 0x0102 } },
            { "hall", "SCOTT HALL", "HALL", 0x0107, 67, 67, 0, 0, 19, 187, -1, { -1, -1, -1 }, 10, 0x173, { 0x0106, 0x0105, 0x0102 } },
            { "savage", "RANDY SAVAGE", "SAVAGE", 0x0108, 68, 68, 0, 0, 13, 130, -1, { -1, -1, -1 }, 9, 0x1EB, { 0x0106, 0x0105, 0x0107 } },
            { "hennig", "MR. PERFECT", "PERFECT", 0x0109, 69, 69, 0, 0, 15, 162, -1, { -1, -1, -1 }, 19, 0x17C, { 0x0105, 0x0107, 0x0106 } },
            { "giant", "THE GIANT", "GIANT", 0x010A, 70, 70, 0, 0, 28, 440, -1, { -1, -1, -1 }, 4, 0x167, { 0x0105, 0x0106, 0x0107 } },
            { "sting", "STING", "STING", 0x010B, 71, 71, 0, 0, 15, 152, -1, { -1, -1, -1 }, 1, 0x1F7, { 0x010C, 0x010D } },
            { "luger", "LEX LUGER", "LUGER", 0x010C, 72, 72, 0, 0, 16, 170, -1, { -1, -1, -1 }, 21, 0x1A9, { 0x010B, 0x0106, 0x0108 } },
            { "goldberg", "GOLDBERG", "GOLDBERG", 0x010D, 73, 73, 0, 0, 15, 185, -1, { -1, -1, -1 }, 2, 0x16D, { 0x010B, 0x010C } },
            { "ricksteiner", "RICK STEINER", "R. STEINER", 0x010E, 74, 74, 0, 0, 11, 148, -1, { -1, -1, -1 }, 34, 0x1DC, { 0x010F } },
            { "scottsteiner", "SCOTT STEINER", "S. STEINER", 0x010F, 75, 75, 0, 0, 13, 135, -1, { -1, -1, -1 }, 19, 0x1EE, { 0x010E, 0x0105, 0x0107 } },
            { "booker", "BOOKER T", "BOOKER", 0x0110, 76, 76, 0, 0, 15, 158, -1, { -1, -1, -1 }, 23, 0x140, { 0x0111 } },
            { "stevieray", "STEVIE RAY", "S. RAY", 0x0111, 77, 77, 0, 0, 17, 192, -1, { -1, -1, -1 }, 33, 0x1F4, { 0x0110 } },
            { "rey", "REY MYSTERIO", "REY", 0x0112, 78, 78, 0, 0, 6, 55, -1, { -1, -1, -1 }, 48, 0x1B8, { 0x0106, 0x010C, 0x010B } },
            { "piper", "RODDY PIPER", "PIPER", 0x0113, 79, 79, 0, 0, 13, 127, -1, { -1, -1, -1 }, 6, 0x1D0, { 0x010B } },
            { "bischoff", "ERIC BISCHOFF", "BISCHOFF", 0x0114, 80, 80, 0, 0, 12, 85, -1, { -1, -1, -1 }, 61, 0x13A, { 0x0105, 0x0106, 0x0107, 0x010A } },
        };
        // gen/revbanks.py BANKS: a Revenge port's second ID -- his base's entry under a new key and ID
        static const struct { const char* key; const char* base; int id; } BANKS[] = {
            { "brethart_b", "brethart", 0x060F }, { "savage_b", "savage", 0x0115 }, { "scottsteiner_b", "scottsteiner", 0x0116 },
            { "booker_b", "booker", 0x0117 }, { "stevieray_b", "stevieray", 0x0118 }, { "rey_b", "rey", 0x0119 },
        };
        for (auto& b : BANKS) {
            Late e = *std::find_if(v.begin(), v.end(), [&](const Late& x) { return std::string(x.key) == b.base; });
            e.key = b.key; e.id = b.id; e.base = b.base;
            v.push_back(e);
        }
        // (no2k15, 2026-11-23: the two WWE 2K15 test slots that ended this list -- stonecold15 0x011A, rock15 0x011B -- are
        //  removed, as in gen/mkroster.py)
        return v;
    }();
    return L;
}
std::string base_key(const Late& e) { return e.base ? e.base : e.key; }   // (revbanks copies REV_WEIGHT / POSE / OVERRIDE)

// ---- gen/mkroster.py's Revenge tables (by base key: a second ID has its base's) -------------------------------------------
struct Ovr { int m; bool flags; int f2, f3, f4; };     // slot -> move, or (move, its three flags)
const std::map<std::string, std::map<int, Ovr>> REV_OVERRIDE = {
    { "neidhart", { { 157, { 11750, true, 0, 0, 1 } } } },
    { "brethart", { { 25, { 16250 } }, { 50, { 930 } } } },
    { "hogan", { { 30, { 16250 } }, { 94, { 3390 } }, { 188, { 5690 } }, { 213, { 8670 } }, { 214, { 9120 } } } },
    { "nash", { { 25, { 16250 } }, { 50, { 1020 } }, { 188, { 5690 } } } },
    { "savage", { { 25, { 16250 } }, { 50, { 1020 } }, { 188, { 6200 } } } },
    { "hennig", { { 25, { 16250 } }, { 50, { 380 } }, { 235, { 15020 } } } },
    { "giant", { { 30, { 16250 } } } },
    { "sting", { { 188, { 6200 } }, { 235, { 15350 } } } },
    { "goldberg", { { 188, { 6200 } }, { 235, { 15370 } } } },
    { "luger", { { 235, { 9880 } } } },
    { "ricksteiner", { { 235, { 15360 } } } },
    { "scottsteiner", { { 30, { 16250 } }, { 235, { 15470 } } } },
    { "booker", { { 235, { 15380 } } } },
    { "rey", { { 235, { 15410 } } } },
};
const std::map<std::string, std::array<int, 3>> REV_WEIGHT = {
    { "brethart", { 1, 1, 0 } }, { "neidhart", { 1, 1, 0 } }, { "hogan", { 1, 1, 0 } }, { "nash", { 1, 1, 0 } },
    { "hall", { 1, 1, 0 } }, { "savage", { 1, 1, 0 } }, { "hennig", { 1, 1, 0 } }, { "giant", { 2, 0, 0 } },
    { "sting", { 1, 1, 0 } }, { "luger", { 1, 1, 0 } }, { "goldberg", { 1, 1, 0 } }, { "ricksteiner", { 1, 1, 0 } },
    { "scottsteiner", { 1, 1, 0 } }, { "booker", { 1, 1, 0 } }, { "stevieray", { 1, 1, 0 } }, { "piper", { 1, 1, 0 } },
};
const std::map<std::string, int> REV_POSE = { { "nash", 3 }, { "luger", 0 } };
const std::set<std::string> REV_NO_STRUGGLE = { "hogan", "nash", "hall", "savage", "hennig", "giant", "sting", "luger", "goldberg",
                                                "ricksteiner", "scottsteiner", "booker", "stevieray", "rey", "piper", "bischoff" };

// ---- gen/revanims.py's constants ----------------------------------------------------------------------------------------
// REV_OWN: slot -> Revenge code: Bret Hart's strong grapple 15 his own Revenge move (code 8: its five variants). (By key, not
// copied to a second ID: revbanks copies only mkroster's three tables.)
const std::map<std::string, std::map<int, int>> REV_OWN = { { "brethart", { { 15, 8 } } } };
constexpr uint32_t NM_MREC = 0x1EF, NM_MRECIX = 0x1F1;    // No Mercy's move records (36 bytes) and their index (s16 per move)
constexpr uint32_t GOES_ON = 0x1E00;                      // (a move record's flags: it goes on to the next move)
constexpr int BLOCK0 = 239, BLOCK1 = 263;                 // (Revenge's continuation blocks: codes 239-262, 8 files each)
constexpr uint32_t REV_OFF = 0x110B;                      // (revmoves.py OFF: a Revenge list value + this = its file)

std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
std::string hex2(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s; for (size_t i = 0; i < n; i++) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
    return s;
}
std::string latin1_utf8(const std::string& s) {
    std::string o;
    for (char ch : s) {
        uint8_t c = (uint8_t)ch;
        if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}
ojson read_json_file(const fs::path& p) { Bytes t = read_file(p); return ojson::parse(std::string(t.begin(), t.end())); }

}  // namespace

// ======================================================================================================================
struct Ports::Impl {
    Roms& R;
    fs::path data;
    const RomFiles *N = nullptr, *RV = nullptr;
    std::unique_ptr<MkRosterBase> B;                 // (mkroster.py's module state)
    std::map<int, std::array<int, 3>> pos;           // (revmoves.slot_positions: slot -> (list place, exact votes, votes))
    std::map<int, std::set<int>> pool;               // (revmoves.slot_pool(CAN))
    struct Pick { int m; std::string how; double d; };
    std::map<int, std::map<int, Pick>> picks;        // (frozen: moveset file -> slot -> port_rev_moveset's taken predict)
    std::map<int, std::map<int, std::optional<int>>> carriers;   // (frozen: moveset file -> slot -> revanims' predict)
    std::vector<int> NM1;                            // (file 0x1F0, s16)
    std::map<uint32_t, std::optional<std::string>> revh_c, nmh_c;
    std::map<std::string, std::set<int>> twins;      // (revanims.py _TWINS)
    bool twins_made = false;
    Bytes van_img, nm_img;                           // (the RAM images as made: No Mercy at its Select, No Mercy Plus at its)
    // (nmplus 2026-11-19) No Mercy Plus is a source game of its own: without it, its men's rows are written as zeros of the
    // build's shape (the layouts in firstrun\hdrfill.bin: shape), so the rows of the others keep their places
    const HdrFill* shape = nullptr;
    bool nmp() const { return NP != nullptr; }
    // (c2hands 2026-11-19) WrestleMania 2000 likewise: without it, Jeff Jarrett's and the Meanie's rows are zeros of the build's
    // shape (their moves are WrestleMania 2000's, their models wmport's from it); the Revenge ports' are made as always.
    // (arenasplit 2026-11-24) With it and without Revenge, Jarrett's and the Meanie's rows are made: wmport is made from either
    bool wm() const { return R.files("wm2000") != nullptr; }
    // (romsets 2026-11-24) WCW/nWo Revenge likewise: without it the Revenge ports' rows are zeros of the build's shape (their
    // movesets are Revenge's, their models wmport's from it) -- (arenasplit 2026-11-24: Jarrett's and the Meanie's are
    // WrestleMania 2000's alone: wmport is made from either game) -- so a player with No Mercy Plus V3 and no Revenge still has No Mercy Plus's men (before, every built-in pack needed
    // Revenge, and with No Mercy Plus alone the first start left all of its men out)
    bool rev() const { return RV != nullptr; }
    std::string zero_row(const char* symbol, size_t row, size_t rows) {
        if (!shape) fail("ports: a source game isn't there and the build's layouts weren't given (firstrun\hdrfill.bin)");
        const uint32_t size = hdr_symbol_size(*shape, symbol);
        if (!size || size % rows) fail("ports: the build's %s isn't %zu rows", symbol, rows);
        const long n = hdr_leaves_in(*shape, symbol, (int64_t)(size / rows * row), (int64_t)(size / rows * (row + 1)));
        if (n < 0) fail("ports: the build has no %s", symbol);
        return hdr_zero_group((size_t)n);
    }

    Impl(Roms& r, const fs::path& d) : R(r), data(d) {}

    void init() {
        N = R.files("nomercy"); RV = R.files("revenge");
        // (c2hands: WrestleMania 2000 optional; romsets 2026-11-24: Revenge too, when No Mercy Plus V3 is there -- rev() above)
        // (arenasplit 2026-11-24: or WrestleMania 2000 -- Jarrett and the Meanie)
        if (!N || (!RV && !R.files("nmplus_v3") && !R.files("wm2000")))
            fail("ports: the No Mercy ROM and the Revenge, WrestleMania 2000 or No Mercy Plus V3 ROM are needed");
        B = std::make_unique<MkRosterBase>(R, data);
        van_img.assign(B->ev().mem(), B->ev().mem() + Emu::SIZE);   // (No Mercy at its Select as mkroster's module leaves it)
        const Bytes& f1 = (*N)(0x1F0);
        for (size_t o = 0; o + 1 < f1.size(); o += 2) NM1.push_back(sx16(be16(f1, o)));
        // revmoves.slot_pool(CAN): the slot's packing lists' moves, else every move No Mercy's own movesets have there
        for (int s = 0; s < 292; s++) {
            std::set<int> p;
            if (B->can_has(s)) { for (int v : B->can_order(s)) if (v != 0 && v != 0xFFFF) p.insert(v); }
            else for (int o = 0; o < 84; o++) {
                if (B->van_set(o).empty()) continue;
                int v = B->van_unpacked(o).first[s].m;
                if (v != 0 && v != 0xFFFF) p.insert(v);
            }
            pool[s] = p;
        }
        ojson fz = read_json_file(data / "frozen" / "rev_moveset_picks.json");
        for (auto& [s, v] : fz["positions"].items()) pos[std::stoi(s)] = { v[0].get<int>(), v[1].get<int>(), v[2].get<int>() };
        ojson pp = read_json_file(data / "frozen" / "rev_port_picks.json");
        for (auto& [f, m] : pp["picks"].items()) {
            auto& d = picks[(int)std::stoul(f, nullptr, 16)];
            for (auto& [s, v] : m.items()) d[std::stoi(s)] = { v[0].get<int>(), v[1].get<std::string>(), v[2].get<double>() };
        }
        for (auto& [f, m] : pp["carriers"].items()) {
            auto& d = carriers[(int)std::stoul(f, nullptr, 16)];
            for (auto& [s, v] : m.items()) d[std::stoi(s)] = v.is_null() ? std::nullopt : std::optional<int>(v.get<int>());
        }
    }

    // ---- revmoves.py: the lists, the files' body hashes -------------------------------------------------------------
    std::vector<int> revlist(int f) {
        const Bytes& d = (*RV)(f + 2);
        if (d.size() != 2 * 763) fail("ports: Revenge's list file 0x%X isn't 763 entries", f + 2);
        std::vector<int> o; for (int i = 0; i < 763; i++) o.push_back(be16(d, 2 * i));
        return o;
    }
    static uint32_t ranim(int v) { return v ? (uint32_t)v + REV_OFF : 0; }
    static std::optional<std::string> body_hash(const Bytes* d) {
        if (!d || d->size() <= 8) return std::nullopt;
        return md5_hex(d->data() + 1, d->size() - 1);
    }
    const std::optional<std::string>& revh(uint32_t a) {
        auto it = revh_c.find(a);
        if (it != revh_c.end()) return it->second;
        return revh_c[a] = a ? body_hash(RV->get(a)) : std::nullopt;
    }
    const std::optional<std::string>& nmhash(uint32_t f) {
        auto it = nmh_c.find(f);
        if (it != nmh_c.end()) return it->second;
        return nmh_c[f] = body_hash(N->get(f));
    }
    std::string vn(int m) { return B->vname(m); }          // (vnames.get(m, ""), Latin-1)

    // ---- gen/mkroster.py port_rev_moveset (predict answered from the frozen table) --------------------------------------
    void port_rev_moveset(const Late& e, std::vector<std::string>& report, std::vector<MoveSlot>& out, std::vector<int>& ab) {
        const std::string bk = base_key(e);
        auto [rslots, rab] = B->van_unpacked(e.ref);
        auto pk = picks.find(e.rev);
        if (pk == picks.end()) fail("ports: moveset file 0x%X isn't in rev_port_picks.json (revanims_freeze.py write)", e.rev);
        auto ov = REV_OVERRIDE.find(bk);
        out = rslots;
        for (int i = 0; i < 292; i++) {
            std::string how = "reference"; int v = rslots[i].m; std::string d = "None";
            const Ovr* fl = nullptr;
            const Ovr* o = nullptr;
            if (ov != REV_OVERRIDE.end()) { auto it = ov->second.find(i); if (it != ov->second.end()) o = &it->second; }
            if (o) { v = o->m; how = "override"; if (o->flags) fl = o; }
            else if (pos.count(i) && rslots[i].m != 0 && rslots[i].m != 0xFFFF) {
                auto p = pk->second.find(i);
                if (p != pk->second.end()) { v = p->second.m; how = p->second.how; d = py_float_repr(p->second.d); }
            }
            if (how != "reference") {
                int rm = rslots[i].m;
                report.push_back(strf("  %s slot %3d: Revenge list %3d -> %5d %-28s (%s %s; reference %d %s)", e.key, i,
                                      pos.count(i) ? pos[i][0] : -1, v, vn(v - pymod(v, 10)).c_str(), how.c_str(), d.c_str(), rm,
                                      vn(rm - pymod(rm, 10)).c_str()));
            }
            if (v == 0xFFFF && B->can_has(i) && !B->in_can(i, 0xFFFF)) v = 0;
            out[i] = fl ? MoveSlot{ v, fl->f2, fl->f3, fl->f4 } : MoveSlot{ v, rslots[i].f2, rslots[i].f3, rslots[i].f4 };
        }
        ab = rab;
        for (int j = 0; j < 4; j++) ab[31 + j] = j < (int)e.allies.size() ? e.allies[j] : 0;
        auto w = REV_WEIGHT.find(bk); if (w != REV_WEIGHT.end()) for (int j = 0; j < 3; j++) ab[2 + j] = w->second[j];
        auto p = REV_POSE.find(bk); if (p != REV_POSE.end()) ab[1] = p->second;
    }
    static int pymod(int a, int b) { int r = a % b; return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r; }

    // ---- gen/revanims.py ------------------------------------------------------------------------------------------------
    uint32_t nm_flags(int m) {                          // (No Mercy's move m's record flags: 0x1EF[36 * ix + 0x20])
        int ri = sx16(be16((*N)(NM_MRECIX), 2 * (size_t)m));
        if (ri < 0) fail("ports: move %d has no record", m);
        return be32((*N)(NM_MREC), 36 * (size_t)ri + 0x20);
    }
    const std::set<int>& twin_moves(uint32_t a) {
        if (!twins_made) {
            for (size_t b = 0; b + 10 < NM1.size(); b += 10)
                for (size_t k = b; k < b + 10; k++)
                    if (NM1[k] > 0) { auto& h = nmhash(NM1[k] + 0x3322); if (h) twins[*h].insert((int)b); }
            twins_made = true;
        }
        static const std::set<int> none;
        auto& h = revh(a);
        if (!h) return none;
        auto it = twins.find(*h);
        return it == twins.end() ? none : it->second;
    }
    bool slot_kind_ok(int slot, uint32_t a) {
        const std::set<int>& tw = twin_moves(a);
        if (!tw.empty()) {
            const std::set<int>& pl = pool[slot];
            bool any = false; for (int x : tw) if (pl.count(x)) { any = true; break; }
            if (!any) return false;
        }
        int lo = -1, hi = 1 << 20;
        for (auto& [s, v] : pos) {
            if (v[1] < 3) continue;
            if (s < slot) lo = std::max(lo, v[0]);
            if (s > slot) hi = std::min(hi, v[0]);
        }
        return lo <= pos[slot][0] && pos[slot][0] <= hi;
    }
    std::vector<Bytes> keys_of(uint32_t f) {           // (_keys: a key every 2 frames, 0x62 bytes, after the 4-byte header)
        const Bytes& d = (*RV)(f);
        std::vector<Bytes> k;
        for (size_t i = 0; i < (d.size() - 4) / 0x62; i++) k.push_back(Bytes(d.begin() + 4 + 0x62 * i, d.begin() + 4 + 0x62 * (i + 1)));
        if (k.empty()) fail("ports: Revenge file 0x%X has no key frame", f);
        return k;
    }
    static long kdist(const Bytes& a, const Bytes& b) {
        long s = 0; for (size_t i = 0; i < std::min(a.size(), b.size()); i++) s += std::abs((int)a[i] - (int)b[i]);
        return s;
    }
    struct BlockR { int block = NONE; long score = 0, next = 0; };
    BlockR rev_block(const std::vector<int>& rl, const std::vector<int>& t, int c) {
        int p = t[c];
        if (!rl[p] || !rl[p + 1]) return {};
        Bytes ga = keys_of(ranim(rl[p])).back(), gd = keys_of(ranim(rl[p + 1])).back();
        struct Sc { long s; int b; Bytes body; };
        std::vector<Sc> sc;
        for (int b = BLOCK0; b < BLOCK1; b++) {
            bool all = true; for (int k = 0; k < 8; k++) if (!rl[t[b] + k]) all = false;
            if (!all) continue;
            const Bytes& d = (*RV)(ranim(rl[t[b]]));
            sc.push_back({ kdist(ga, keys_of(ranim(rl[t[b]]))[0]) + kdist(gd, keys_of(ranim(rl[t[b] + 1]))[0]), b, Bytes(d.begin() + 1, d.end()) });
        }
        if (sc.empty()) return {};
        std::stable_sort(sc.begin(), sc.end(), [](const Sc& x, const Sc& y) { return x.s != y.s ? x.s < y.s : x.b < y.b; });
        const Sc& best = sc[0];
        long nxt = 1L << 30;
        for (auto& s : sc) if (s.body != best.body) { nxt = s.s; break; }
        bool ok = best.s < 8000 && (double)best.s * 1.8 < (double)nxt;
        return { ok ? best.b : NONE, best.s, nxt };
    }

    RevAnims revanims() {
        if (!rev()) fail("ports: the Revenge ports' animations asked for without the Revenge ROM");
        RevAnims out;
        auto B0w = byte0_table(*N, *RV);
        std::map<int, int> B0; for (auto& [a, b] : B0w) B0[a] = b;
        RecConv REC = rec_conv(*N, *RV);
        Bytes ram = make_image("rev_bm", data / "loadmaps", R);     // (Revenge's code -> list table, 0x80030570)
        std::vector<int> t; for (int i = 0; i < 279; i++) t.push_back(sx16(be16(ram, 0x30570 + 2 * i)));
        const auto& LATE = late_table();
        struct FileE { std::string name; uint32_t fa, fd; int b0a, b0d; std::string hdr[2], rec[2]; };
        struct Row { std::string key; int late, slot; std::string name; int code; std::string stand_in; int move;
                     bool has_move = true; std::string cont; std::optional<int> carrier; };
        std::vector<FileE> files; std::set<std::string> file_names;
        std::vector<Row> rows;
        auto add_file = [&](const std::string& name, uint32_t fa, uint32_t fd) {
            if (file_names.count(name)) return;
            const Bytes &da = (*RV)(fa), &dd = (*RV)(fd);
            FileE f{ name, fa, fd, B0.at(da[0]), B0.at(dd[0]) };
            uint8_t ha[4] = { (uint8_t)f.b0a, da[1], da[2], da[3] }, hd[4] = { (uint8_t)f.b0d, dd[1], dd[2], dd[3] };
            f.hdr[0] = hex2(ha, 4); f.hdr[1] = hex2(hd, 4);
            Bytes ra = REC.rec(fa), rd = REC.rec(fd);
            f.rec[0] = hex2(ra.data(), 36); f.rec[1] = hex2(rd.data(), 36);
            files.push_back(f); file_names.insert(name);
        };
        auto code_of = [&](int j) { int c = -1; for (int i = 0; i < 239; i++) if (t[i] <= j) c = i; if (c < 0) fail("ports: no code before list place %d", j); return c; };
        auto known = [&](uint32_t f) { return B0.count((*RV)(f)[0]) > 0; };
        for (size_t li = 0; li < LATE.size(); li++) {
            const Late& e = LATE[li];
            if (e.rev < 0) continue;
            const std::string key = e.key;
            std::vector<std::string> rep; std::vector<MoveSlot> lx; std::vector<int> lab;
            port_rev_moveset(e, rep, lx, lab);
            std::vector<int> rl = revlist(e.rev);
            // the stand-ins ("near"): his Revenge move's own pair of animations on the stand-in move
            for (auto& l : rep) {
                if (l.find("slot") == std::string::npos || l.find("(near") == std::string::npos) continue;
                int slot = std::stoi(l.substr(l.find("slot") + 4));
                int j = pos[slot][0];
                if (j >= t[238]) continue;
                int c = code_of(j), n = t[c + 1] - t[c];
                if (j != t[c] || n < 2 || !rl[j] || !rl[j + 1]) continue;
                uint32_t fa = ranim(rl[j]), fd = ranim(rl[j + 1]);
                if (!known(fa) || !known(fd)) continue;
                std::string name = strf("%s_%04X_%04X", upper(key).c_str(), fa, fd);
                add_file(name, fa, fd);
                std::string si = l.substr(l.find("->") + 2);
                size_t a = si.find_first_not_of(" \t"), b = si.find_last_not_of(" \t");
                si = si.substr(a, b - a + 1);
                rows.push_back({ key, (int)li, slot, name, c, si, std::stoi(si) });
            }
            // his own moves where No Mercy has no copy of them (REV_OWN)
            auto ro = REV_OWN.find(key);
            if (ro != REV_OWN.end())
                for (auto& [slot, code] : ro->second)
                    for (int k = 0; k < 5; k++) {
                        int j = t[code + k];
                        if (!rl[j] || !rl[j + 1]) continue;
                        uint32_t fa = ranim(rl[j]), fd = ranim(rl[j + 1]);
                        if (!known(fa) || !known(fd)) continue;
                        std::string name = strf("%s_%04X_%04X", upper(key).c_str(), fa, fd);
                        add_file(name, fa, fd);
                        rows.push_back({ key, (int)li, slot + k, name, code + k, "(Revenge's own, on the reference's move)",
                                         B->van_unpacked(e.ref).first[slot + k].m });
                    }
            // every other slot that kept the reference's move: his own Revenge animation on it (a move's pair)
            std::set<int> done;
            for (auto& r : rows) if (r.key == key) done.insert(r.slot);
            for (auto& l : rep) {
                size_t sp = l.find("slot");
                if (sp == std::string::npos) continue;
                if (l.substr(sp + 4, 6).find(':') == std::string::npos) continue;
                done.insert(std::stoi(l.substr(sp + 4)));
            }
            auto rs = B->van_unpacked(e.ref).first;
            static const std::regex partner("double|doomsday", std::regex::icase);
            for (int slot = 0; slot < 292; slot++) {
                int rm = rs[slot].m;
                if (done.count(slot) || !pos.count(slot) || rm == 0 || rm == 0xFFFF) continue;
                if (std::regex_search(vn(rm - pymod(rm, 10)), partner)) continue;
                int j = pos[slot][0];
                if (j >= t[238] || !rl[j]) continue;
                int c = code_of(j);
                if (j != t[c]) continue;
                uint32_t fa = ranim(rl[j]);
                if (!known(fa)) continue;
                if (!slot_kind_ok(slot, fa)) continue;
                if (!(t[c + 1] - t[c] >= 2 && rl[j + 1])) continue;     // (a strike: left the reference's)
                uint32_t fd = ranim(rl[j + 1]);
                if (!known(fd)) continue;
                std::string name = strf("%s_%04X_%04X", upper(key).c_str(), fa, fd);
                add_file(name, fa, fd);
                rows.push_back({ key, (int)li, slot, name, c, strf("(his own, on the reference's %d)", rm), rm });
            }
            // the moves after them: Revenge's continuation block, or (Revenge's move ends) a move of the slot that ends
            for (auto& r : rows) {
                if (r.key != key) continue;
                int m = r.move;
                if (m == 0 || m == 0xFFFF || !(nm_flags(m) & GOES_ON)) continue;
                BlockR br = rev_block(rl, t, r.code);
                if (br.block != NONE) {
                    int d = br.block;
                    std::vector<uint32_t> fs; for (int k = 0; k < 8; k++) fs.push_back(ranim(rl[t[d] + k]));
                    bool all = true; for (auto f : fs) if (!known(f)) all = false;
                    if (!all) { out.log.push_back(strf("  %s slot %d: block %d's first byte not known -- left as it was", key.c_str(), r.slot, d)); continue; }
                    if (nm_flags(m + 1) & GOES_ON) out.log.push_back(strf("  %s slot %d: %d + 1 goes on again (not made)", key.c_str(), r.slot, m));
                    std::vector<std::string> names;
                    for (int k = 0; k < 4; k++) names.push_back(strf("%s_B%d_%04X_%04X", upper(key).c_str(), d, fs[2 * k], fs[2 * k + 1]));
                    if (!file_names.count(names[0])) for (int k = 0; k < 4; k++) add_file(names[k], fs[2 * k], fs[2 * k + 1]);
                    r.cont = names[0];
                    out.log.push_back(strf("  %s slot %3d: code %3d -> block %d (%ld, next %ld) for %d's %d", key.c_str(), r.slot, r.code, d, br.score, br.next, m, m + 1));
                } else {
                    bool any = false;
                    for (int v : pool[r.slot]) if (!(nm_flags(v) & GOES_ON)) { any = true; break; }
                    std::optional<int> pr;
                    if (any) {
                        auto cf = carriers.find(e.rev);
                        if (cf == carriers.end() || !cf->second.count(r.slot))
                            fail("ports: %s slot %d isn't in rev_port_picks.json's carriers (revanims_freeze.py write)", key.c_str(), r.slot);
                        pr = cf->second.at(r.slot);
                    }
                    if (!pr) { out.log.push_back(strf("  %s slot %d: %d goes on, Revenge's code %d doesn't, and the slot has no move that ends", key.c_str(), r.slot, m, r.code)); continue; }
                    r.carrier = *pr;
                    out.log.push_back(strf("  %s slot %3d: code %3d ends (%ld, next %ld): %d -> %d %s", key.c_str(), r.slot, r.code, br.score, br.next, m, *pr, vn(*pr - pymod(*pr, 10)).c_str()));
                }
            }
        }
        // rev_anims.json: json.dump(dict(files=files, rows=rows), indent=1) in text mode
        ojson J = ojson::object(), JF = ojson::array(), JR = ojson::array();
        for (auto& f : files) {
            ojson o = ojson::object();
            o["name"] = f.name; o["fa"] = f.fa; o["fd"] = f.fd; o["b0a"] = f.b0a; o["b0d"] = f.b0d;
            o["hdr"] = ojson::array({ f.hdr[0], f.hdr[1] }); o["rec"] = ojson::array({ f.rec[0], f.rec[1] });
            JF.push_back(o);
        }
        for (auto& r : rows) {
            ojson o = ojson::object();
            o["key"] = r.key; o["late"] = r.late; o["slot"] = r.slot; o["name"] = r.name; o["code"] = r.code;
            o["stand_in"] = latin1_utf8(r.stand_in); o["move"] = r.move;
            if (!r.cont.empty()) o["cont"] = r.cont;
            if (r.carrier) o["carrier"] = *r.carrier;
            JR.push_back(o);
        }
        J["files"] = JF; J["rows"] = JR;
        PyDump h1; h1.indent = 1;
        out.json = crlf(py_dumps(J, h1));
        // movex_data.h
        std::vector<std::string> L = {
            "// generated by gen/revanims.py: the ported Superstars' stand-in moves played with their own (Revenge's)",
            "// animations: { ext_late index, moveset entry, the attacker's file (rev_move_anims.h; the defender's the next), 0, -, -,",
            "// { the file of the move it goes on to: Revenge's block }, the move in its place (0: as it is) }",
            "#include \"rev_move_anims.h\"",
            strf("#define MOVEX_FILES_N %zu", rows.size()),
            "static const MoveX movex_files[MOVEX_FILES_N] = {" };
        for (auto& r : rows)
            L.push_back(strf("    { %d, %d, RMA_%s, 0, { 0 }, { 0 }, { %s }, %d },   // %s slot %d: Revenge code %d (stand-in %s)", r.late,
                             r.slot, r.name.c_str(), r.cont.empty() ? "0" : ("RMA_" + r.cont).c_str(), r.carrier.value_or(0),
                             r.key.c_str(), r.slot, r.code, r.stand_in.substr(0, 40).c_str()));
        L.push_back("};");
        L.push_back("// their header (the file's first 4 bytes) and record (file 0x2BF's, Revenge's made No Mercy's), by file - RMA_FIRST");
        L.push_back(strf("#define RMA_N %zu", 2 * files.size()));
        L.push_back("static const u8 rma_hdr[RMA_N + 1][4] = {");
        auto bytes_line = [](const std::string& hex) {
            std::string s = "    { ";
            for (size_t i = 0; i < hex.size(); i += 2) {
                if (i) s += ", ";
                s += "0x" + upper(hex.substr(i, 2));
            }
            return s + " },";
        };
        for (auto& f : files) for (auto& h : f.hdr) L.push_back(bytes_line(h));
        L.push_back("    { 0 } };");
        L.push_back("static const u8 rma_rec[RMA_N + 1][36] = {");
        for (auto& f : files) for (auto& h : f.rec) L.push_back(bytes_line(h));
        L.push_back("    { 0 } };");
        std::string txt;
        for (auto& l : L) txt += l + "\n";
        out.movex_h = crlf(txt);
        out.log.push_back(strf("%zu animation pairs, %zu moves", files.size(), rows.size()));
        return out;
    }

    // ==================================================================================================================
    // gen/mkroster.py: roster_ext.h
    // ==================================================================================================================
    const RomFiles* NP = nullptr;                       // No Mercy Plus V3's files (/home/devbox/nmplus/f)
    std::unique_ptr<Emu> EN;                            // No Mercy Plus V3 at its Superstar Select (nm_pick_ram)
    std::vector<int> WIDTHS;                            // (the packing's lists' index widths)
    std::set<std::pair<int, int>> USED;                 // (the list cells the game's own movesets read)
    std::vector<std::pair<int, std::string>> vnames_l, nnames_l;   // (names.pkl, in its order, Latin-1)
    std::map<int, std::string> nnames;
    std::map<std::array<Bytes, 3>, int> rec_ix;         // (the game's move records -> the first move / 10 with them)
    std::vector<std::array<Bytes, 3>> nrecs;
    std::map<std::string, int> vname_ix;
    std::map<int, int> NM_ID;
    bool roster_ready = false;
    static constexpr uint32_t NM_STRUCTS = 0x80401010, NM_UNPACK = 0x80005F8C, VAN_UNPACK = 0x80005EFC;
    static constexpr uint32_t BUF_M = 0x80780000, BUF_P = 0x80780100, BUF_S = 0x80781000, BUF_A = 0x80782000;

    // EXT: No Mercy Plus V3's four (key, its ordinal, ID here, names, music / video slots) -- ours
    struct Ext { const char* key; int nm, id; const char* name; const char* short_name; const char* nick; int music[2], video[2]; int ref; };
    static const std::vector<Ext>& ext_table() {
        static const std::vector<Ext> E = {
            { "funaki", 65, 0x060A, "FUNAKI", "FUNAKI", "", { 32, 46 }, { 31, 55 }, 49 },
            { "gangrel", 76, 0x0408, "GANGREL", "GANGREL", "", { 11, 11 }, { 0, 46 }, 29 },
            { "mosh", 77, 0x0309, "MOSH", "MOSH", "", { 23, 23 }, { 0, 48 }, 41 },
            { "thrasher", 78, 0x030A, "THRASHER", "THRASHER", "", { 23, 23 }, { 0, 48 }, 41 },
        };
        return E;
    }
    static constexpr int ORD0 = 102;

    static std::string utf8_latin1(const std::string& s) {
        std::string o;
        for (size_t i = 0; i < s.size(); i++) {
            uint8_t c = (uint8_t)s[i];
            if (c < 0x80) o += (char)c;
            else if ((c & 0xE0) == 0xC0 && c <= 0xC3 && i + 1 < s.size()) { o += (char)(((c & 3) << 6) | ((uint8_t)s[i + 1] & 0x3F)); i++; }
            else fail("ports: a move name past Latin-1");
        }
        return o;
    }

    void roster_init() {
        if (roster_ready) return;
        NP = R.files("nmplus_v3");               // (nmplus: may be absent -- its men's rows are zero then)
        if (NP) {
            nm_img = make_image("nm_pick", data / "loadmaps", R);
            EN = std::make_unique<Emu>(nm_img);
        }
        Emu& E = B->ev();
        for (size_t c = 0; c < B->vals().size(); c++) WIDTHS.push_back(sx16(E.r16(0x800442E4 + 2 * (uint32_t)c)));
        // the cells the game's own movesets read: every moves file the ROM's Superstars and edit slots name, and its movesets
        std::set<int> van_files;
        for (int o = 1; o < 102; o++) van_files.insert(E.r16(E.r32(0x80047124 + 16 * o) + 0xA));
        for (int f : van_files) { const Bytes& d = (*N)(f); for (auto& c : cells_of(d.data(), d.size())) USED.insert({ c[1], c[2] }); }
        for (int o = 0; o < 84; o++) {
            const Bytes& v = B->van_set(o);
            if (!v.empty()) for (auto& c : cells_of(v.data(), 0xB8)) USED.insert({ c[1], c[2] });
        }
        // the move names (names.pkl: vnames, nnames) and the records that map No Mercy Plus's moves to the game's
        MoveNames mn = move_names(R, data / "names_index.json");
        for (auto& [k, v] : mn.nomercy) vnames_l.push_back({ k, utf8_latin1(v) });
        for (auto& [k, v] : mn.nmplus) { nnames_l.push_back({ k, utf8_latin1(v) }); nnames[k] = nnames_l.back().second; }
        for (auto& [k, v] : vnames_l) vname_ix.emplace(mkroster_norm(v), k);
        auto recs = [](const RomFiles& F) {
            const Bytes &a = F(0x69), &b = F(0x1F0), &c = F(0x1F1);
            auto sl = [](const Bytes& d, size_t i) { size_t s = std::min(d.size(), 20 * i), e = std::min(d.size(), 20 * i + 20); return Bytes(d.begin() + s, d.begin() + e); };
            std::vector<std::array<Bytes, 3>> o;
            for (size_t i = 0; i < a.size() / 20; i++) o.push_back({ sl(a, i), sl(b, i), sl(c, i) });
            return o;
        };
        auto vrecs = recs(*N);
        if (NP) nrecs = recs(*NP);
        for (size_t i = 0; i < vrecs.size(); i++) rec_ix.emplace(vrecs[i], (int)i);
        // No Mercy Plus's IDs -> ours (allies in the abilities)
        NM_ID = { { 0x0005, 0x0201 }, { 0x091E, 0x0005 }, { 0x0201, 0x0000 } };
        for (auto& e : ext_table()) NM_ID[0x0900 + (e.nm - 65 + 5)] = e.id;
        for (auto& e : late_table()) if (e.nm >= 0) NM_ID[0x0900 + (e.nm - 65 + 5)] = e.id;
        roster_ready = true;
    }
    const Bytes& nmfile(uint32_t i) { return (*NP)(i); }

    // ---- the game's packing in Python (func_800075DC's format): per layout slot w + 5 bits ------------------------------
    // cells_of: the (slot, list, index) each layout slot of a packed moveset reads
    std::vector<std::array<int, 3>> cells_of(const uint8_t* b, size_t n) {
        size_t pos_ = 0;
        auto read = [&](int k) {
            int v = 0;
            for (int i = 0; i < k; i++) {
                int byte = (pos_ >> 3) < n ? b[pos_ >> 3] : 0;
                v = (v << 1) | ((byte >> (7 - (pos_ & 7))) & 1); pos_++;
            }
            return v;
        };
        std::vector<std::array<int, 3>> out;
        for (auto& [slot, cs] : B->layout()) {
            int w = 0; for (int c : cs) w = std::max(w, WIDTHS[c]);
            int v = read(w + 5);
            out.push_back({ slot, cs[(v >> w) & 3], v & ((1 << w) - 1) });
        }
        return out;
    }
    static int index_of(const std::vector<int>& v, int m) { auto it = std::find(v.begin(), v.end(), m); return it == v.end() ? -1 : (int)(it - v.begin()); }
    std::vector<int> spare_cells(int c) {
        const auto& v = B->vals()[c];
        std::vector<int> o;
        for (int i = 0; i < (int)v.size(); i++)
            if ((v[i] == 0 || v[i] == 0xFFFF) && index_of(v, v[i]) != i && !USED.count({ c, i })) o.push_back(i);
        return o;
    }
    // encode: -> the packed 0xB8 bytes, and the [(list, index, move)] cells to set while it's unpacked
    std::pair<Bytes, std::vector<std::array<int, 3>>> encode(const std::vector<MoveSlot>& slots) {
        std::map<int, std::pair<int, int>> choice;
        std::map<std::pair<int, int>, int> temp;
        for (auto& [slot, cs] : B->layout()) {
            int m = slots[slot].m;
            bool done = false;
            for (int sel = 0; sel < (int)cs.size() && !done; sel++) {
                int ix = index_of(B->vals()[cs[sel]], m);
                if (ix >= 0) { choice[slot] = { sel, ix }; done = true; }
            }
            for (int sel = 0; sel < (int)cs.size() && !done; sel++) {
                int c = cs[sel];
                for (int i : spare_cells(c)) {
                    auto t = temp.find({ c, i });
                    if (t == temp.end() || t->second == m) { temp[{ c, i }] = m; choice[slot] = { sel, i }; done = true; break; }
                }
            }
            if (!done) fail("ports: slot %d: no room for move %d", slot, m);
        }
        // pack_cells
        std::vector<int> bits;
        for (auto& [slot, cs] : B->layout()) {
            int w = 0; for (int c : cs) w = std::max(w, WIDTHS[c]);
            auto [sel, idx] = choice[slot];
            const MoveSlot& s = slots[slot];
            long v = ((long)((s.f4 << 2) | (s.f3 << 1) | s.f2) << (w + 2)) | ((long)sel << w) | idx;
            for (int i = 0; i < w + 5; i++) bits.push_back((int)((v >> (w + 4 - i)) & 1));
        }
        if (bits.size() > 0xB8 * 8) fail("ports: a packed moveset past 0xB8 bytes");
        Bytes by(0xB8);
        for (size_t i = 0; i < bits.size(); i++) by[i >> 3] |= (uint8_t)(bits[i] << (7 - (i & 7)));
        std::vector<std::array<int, 3>> cells;
        for (auto& [ci, m] : temp) cells.push_back({ ci.first, ci.second, m });
        std::sort(cells.begin(), cells.end());
        return { by, cells };
    }
    // keyed: the packed moveset (moves: our packing with its cells; abilities: the game's packing), checked by the game's
    // own unpacking with the cells set in its lists
    std::pair<Bytes, std::vector<std::array<int, 3>>> keyed(const std::string& key, const std::vector<MoveSlot>& exact, const std::vector<int>& ab) {
        auto [moves, cells] = encode(exact);
        std::vector<MoveSlot> enc = exact;            // (exact_encodable: the moves the game's lists lack left out)
        for (int i = 0; i < (int)enc.size(); i++) if (B->can_has(i) && !B->in_can(i, enc[i].m)) enc[i].m = 0;
        Bytes pk = B->nm_pack(enc, ab);
        Bytes params(pk.begin() + 0xB8, pk.end());
        Emu& E = B->ev();
        std::vector<std::pair<uint32_t, int>> tabs;
        for (auto& c : cells) { uint32_t a = E.r32(0x80045500 + 4 * (uint32_t)c[0]) + 2 * (uint32_t)c[1]; tabs.push_back({ a, E.r16(a) }); }
        for (size_t k = 0; k < cells.size(); k++) E.w16(tabs[k].first, cells[k][2]);
        MovesetUnpacked back = B->nm_unpack(moves.data(), params.data());
        for (auto& [a, old] : tabs) E.w16(a, old);
        for (int i = 0; i < 292; i++) if (!(back.first[i] == exact[i])) fail("ports: %s: the game's unpacking gives slot %d back as %d, not %d", key.c_str(), i, back.first[i].m, exact[i].m);
        if (back.second != ab) fail("ports: %s: the game's unpacking changes the abilities", key.c_str());
        moves.insert(moves.end(), params.begin(), params.end());
        return { moves, cells };
    }

    // ---- No Mercy Plus's unpacking (unpack(EN, NM_UNPACK, ...)): its buffers kept between calls, as the Python's -------
    MovesetUnpacked nm_plus_unpack(const Bytes& moves, const Bytes& params) {
        Emu& E = *EN;
        E.wbytes(BUF_M, moves.data(), std::min<size_t>(moves.size(), 0xB8)); E.wbytes(BUF_P, params.data(), std::min<size_t>(params.size(), 0x20));
        E.wbytes(BUF_S, Bytes(0x800)); E.wbytes(BUF_A, Bytes(0x80));
        E.call(NM_UNPACK, { BUF_M, BUF_S, BUF_P, BUF_A });
        MovesetUnpacked o;
        for (int i = 0; i < 292; i++) { uint32_t p = BUF_S + 6 * i; o.first.push_back({ E.r16(p), E.r8(p + 2), E.r8(p + 3), E.r8(p + 4) }); }
        for (int i = 0; i < 55; i++) o.second.push_back(sx16(E.r16(BUF_A + 2 * i)));
        return o;
    }
    MovesetUnpacked nm_plus_of(int o) {
        uint32_t s = NM_STRUCTS + o * 0x50;
        return nm_plus_unpack(nmfile(EN->r16(s + 0xA)), nmfile(EN->r16(s + 0xC)));
    }

    // map_move: No Mercy Plus's move -> the game's (by the animation records, else by name); NONE + its name when neither
    std::pair<int, std::string> map_move(int m) {
        if (m == 0 || m == 0xFFFF) return { m, "same" };
        int i = m / 10;
        if (m % 10 == 0 && i < (int)nrecs.size()) { auto it = rec_ix.find(nrecs[i]); if (it != rec_ix.end()) return { it->second * 10, "anim" }; }
        auto nn = nnames.find(m);
        if (nn != nnames.end() && !nn->second.empty()) { auto it = vname_ix.find(mkroster_norm(nn->second)); if (it != vname_ix.end()) return { it->second, "name" }; }
        return { NONE, nn == nnames.end() ? std::string() : nn->second };
    }
    // wm_fill: WrestleMania 2000's move for slot i, when this game has it (its very animations)
    int wm_fill(const std::vector<MoveSlot>& wm_moves, int i) {
        static const std::map<int, int> WM_SLOT_INV = { { 86, 86 }, { 88, 90 }, { 90, 94 }, { 92, 98 } };
        bool block = i >= 86 && i < 102;
        if (block && !WM_SLOT_INV.count(i)) return NONE;
        int i0 = block ? WM_SLOT_INV.at(i) : i;
        int m = wm_moves[i0].m;
        if (m == 0 || m == 0xFFFF || !B->can_has(i)) return NONE;
        const auto& kind = B->can_kind(i);
        std::vector<int> c;
        for (int x : B->wm_nm_move(m)) if (B->in_can(i, x) || (!kind.empty() && kind.front() <= x && x <= kind.back())) c.push_back(x);
        for (int x : c) if (B->in_can(i, x)) return x;
        return c.empty() ? NONE : c[0];
    }
    std::vector<int> nm_same;                           // (NM_SAME)
    std::vector<int> body_fields(const std::vector<int>& nab, int stance, int ref_ord) {
        if (nm_same.empty()) {
            Emu& E = B->ev();
            for (int o = 1; o < 65; o++) {
                if (B->van_set(o).empty()) continue;
                int id = EN->r16(NM_STRUCTS + o * 0x50);
                if (NM_ID.count(id)) continue;
                if (id == E.r16(E.r32(0x80047124 + 16 * o))) nm_same.push_back(o);
            }
        }
        std::vector<int> like;
        for (int o : nm_same) {
            auto u = nm_plus_of(o);
            if (std::equal(u.second.begin(), u.second.begin() + 3, nab.begin()) && u.first[252].m == stance) like.push_back(o);
        }
        if (like.empty())
            for (int o : nm_same) {
                auto u = nm_plus_of(o);
                if (std::equal(u.second.begin(), u.second.begin() + 3, nab.begin())) like.push_back(o);
            }
        if (like.empty()) like = { ref_ord };
        std::vector<std::pair<int, std::vector<int>>> vals;
        for (int o : like) { auto a = B->van_unpacked(o).second; vals.push_back({ o, std::vector<int>(a.begin(), a.begin() + 3) }); }
        for (auto& [o, v] : vals) if (o == ref_ord) return v;
        OrderedCounter<std::vector<int>> C;
        for (auto& [o, v] : vals) C.add(v);
        return C.most();
    }
    // port_moveset: a No Mercy Plus V3 man's moveset in the game's moves (wm: WrestleMania 2000's moves / abilities files)
    void port_moveset(const std::string& key, int nmo, int ref_ord, const int* wm, std::vector<MoveSlot>& exact, std::vector<int>& ab) {
        static const std::map<std::string, std::map<int, int>> OVERRIDE = { { "bigshow", { { 70, 1520 }, { 104, 3590 } } } };
        MovesetUnpacked nu = nm_plus_of(nmo);
        auto& nslots = nu.first; auto& nab = nu.second;
        auto rslots = B->van_unpacked(ref_ord).first;
        std::vector<MoveSlot> wm_moves;
        if (wm) wm_moves = B->wm_unpack(wm[0], wm[1]).first;
        exact.clear();
        for (int i = 0; i < (int)nslots.size(); i++) {
            int m = nslots[i].m;
            auto [v, how] = map_move(m);
            if (m == 0xFFFF && B->can_has(i) && !B->in_can(i, 0xFFFF)) v = 0;
            if (v != NONE && v != 0 && v != 0xFFFF && B->can_has(i) && !B->in_can(i, v)) {
                const auto& kind = B->can_kind(i);
                if (!(!kind.empty() && kind.front() <= v && v <= kind.back())) { v = NONE; how = "(NM+'s own use of the slot)"; }
            }
            if (v == NONE) {
                auto ov = OVERRIDE.find(key);
                if (ov != OVERRIDE.end() && ov->second.count(i)) v = ov->second.at(i);
                if (v == NONE || v == 0) v = B->stand_in(i, how, NONE);
                if ((v == NONE || v == 0) && wm && i != 252) v = wm_fill(wm_moves, i);
                if (v == NONE || v == 0) v = rslots[i].m;
            }
            exact.push_back({ v, nslots[i].f2, nslots[i].f3, nslots[i].f4 });
        }
        for (int i = 0; i < 292; i++) if (exact[i].m == 0xFFFF && B->can_has(i) && !B->in_can(i, 0xFFFF)) exact[i].m = 0;
        ab = nab;
        std::vector<int> bf = body_fields(nab, nslots[252].m, ref_ord);
        for (int j = 0; j < 3; j++) ab[j] = bf[j];
        for (int k = 31; k < 35; k++) {
            auto it = NM_ID.find(ab[k]);
            if (it != NM_ID.end()) ab[k] = it->second;
            else if (0x905 <= ab[k] && ab[k] <= 0x91E) ab[k] = 0;
        }
        if (key == "bigshow") { ab[31] = 0x101; ab[32] = 0x902; ab[33] = 0x001; ab[34] = 0; }
    }

    std::string roster_ext_h() {
        roster_init();
        // (WM_VIDEO: gen/wmport.py's video numbers for the WrestleMania 2000 men -- frozen\wmport_videos.json, as make_data.py)
        ojson wv = read_json_file(data / "frozen" / "wmport_videos.json");
        const auto& LATE = late_table();
        std::vector<std::string> out = { "// generated by gen/mkroster.py from No Mercy Plus V3: the roster's new Superstars",
                                         strf("#define EXT_N %zu", ext_table().size()), strf("#define EXT_ORD0 %d", ORD0), "" };
        std::vector<std::string> rows;
        std::vector<std::pair<Bytes, std::vector<std::array<int, 3>>>> keys;
        std::vector<bool> absent;                        // (nmplus: a row whose source game isn't there -- zeros)
        for (size_t k = 0; k < ext_table().size(); k++) {
            const Ext& e = ext_table()[k];
            uint32_t a = NM_STRUCTS + e.nm * 0x50;           // (its first costume's music, video, height, weight)
            if (!nmp()) {
                rows.push_back(strf("    { 0x%04X, %d, \"%s\", \"%s\", \"%s\", { %d, %d }, { %d, %d }, 0, 0 },   // NM+ ordinal %d (No Mercy Plus isn't there)",
                                    e.id, ORD0 + (int)k, e.name, e.short_name, e.nick, e.music[0], e.music[1], e.video[0], e.video[1], e.nm));
                keys.push_back({ Bytes(0xD8), {} }); absent.push_back(true);
                continue;
            }
            int music = EN->r8(a + 4), video = EN->r8(a + 5), height = EN->r8(a + 6), weight = EN->r16(a + 8);
            std::vector<MoveSlot> exact; std::vector<int> ab;
            port_moveset(e.key, e.nm, e.ref, nullptr, exact, ab);
            keys.push_back(keyed(e.key, exact, ab)); absent.push_back(false);
            rows.push_back(strf("    { 0x%04X, %d, \"%s\", \"%s\", \"%s\", { %d, %d }, { %d, %d }, %d, %d },   // NM+ ordinal %d (its music %d, video %d)",
                                e.id, ORD0 + (int)k, e.name, e.short_name, e.nick, e.music[0], e.music[1], e.video[0], e.video[1], height, weight,
                                e.nm, music, video));
        }
        if (nmp()) {   // Big Show (No Mercy Plus ordinal 5), Kane the reference
            std::vector<MoveSlot> exact; std::vector<int> ab;
            port_moveset("bigshow", 5, 4, nullptr, exact, ab);
            keys.push_back(keyed("bigshow", exact, ab)); absent.push_back(false);
        } else { keys.push_back({ Bytes(0xD8), {} }); absent.push_back(true); }   // (without it: Kane's moves, roster.c)
        std::vector<std::array<int, 2>> hw;
        for (auto& e : LATE) {
            int h = e.h, w = e.w;
            if ((e.nm >= 0 && !nmp()) ||                 // (one of No Mercy Plus's, which isn't there: zeros)
                (e.rev >= 0 && !rev()) ||                // (romsets: a Revenge port, Revenge isn't there)
                (e.nm < 0 && e.rev < 0 && e.same < 0 && !wm())) {   // (c2hands: one of WrestleMania 2000's -- arenasplit 2026-11-24:
                                                                    //  Jarrett / the Meanie need nothing of Revenge's)
                hw.push_back({ std::max(h, 0), std::max(w, 0) });
                keys.push_back({ Bytes(0xD8), {} }); absent.push_back(true);
                continue;
            }
            if (e.nm >= 0 && h < 0) { uint32_t s_ = NM_STRUCTS + e.nm * 0x50; h = EN->r8(s_ + 6); w = EN->r16(s_ + 8); }
            hw.push_back({ h, w });
            std::vector<MoveSlot> lx; std::vector<int> lab;
            if (e.same >= 0) { auto u = B->van_unpacked(e.same); lx = u.first; lab = u.second; }
            else if (e.nm >= 0) port_moveset(e.key, e.nm, e.ref, e.wm[0] >= 0 ? e.wm + 1 : nullptr, lx, lab);
            else if (e.rev >= 0) {
                std::vector<std::string> rep;
                port_rev_moveset(e, rep, lx, lab);
                // (the taunts played where he stands never one of the corner's / apron's / outside's: slots 231-234 hold those)
                std::set<int> spot;
                for (int k : { 231, 232, 233, 234 }) for (int v : B->can_order(k)) if (v != 0 && v != 0xFFFF) spot.insert(v);
                for (int i : { 227, 228, 229, 230, 235 }) if (spot.count(lx[i].m)) fail("ports: %s: a corner / apron / outside taunt played where he stands (slot %d)", e.key, i);
                if (REV_NO_STRUGGLE.count(base_key(e)))
                    for (int i = 0; i < 292; i++) if (lx[i].m >= 960 && lx[i].m <= 969) fail("ports: %s: the struggling body slam in slot %d", e.key, i);
            } else {
                std::vector<std::string> rep;
                B->port_wm_moveset(e.key, e.wm[1], e.wm[2], e.ref, rep, lx, lab);
            }
            keys.push_back(keyed(e.key, lx, lab)); absent.push_back(false);
        }
        size_t l0 = keys.size() - LATE.size();
        std::vector<size_t> late_row;
        for (size_t k = 0; k < LATE.size(); k++) {
            size_t r = k;
            if (LATE[k].base) for (size_t j = 0; j < LATE.size(); j++) if (std::string(LATE[j].key) == LATE[k].base) { r = j; break; }
            late_row.push_back(l0 + r);
            if (keys[l0 + k] != keys[l0 + r]) fail("ports: %s: a second ID's moveset isn't his base's", LATE[k].key);
        }
        out.push_back("typedef struct { u16 id; u8 ord; const char* name; const char* short_name; const char* nick;");
        out.push_back("                 u8 music[2], video[2];   // [0] the game's, [1] with No Mercy Plus's themes and videos");
        out.push_back("                 u8 height; u16 weight; } ExtWrestler;   // as a record has them: inches over 5'0\", pounds - 100");
        out.push_back("static const ExtWrestler ext_wrestlers[EXT_N] = {");
        for (auto& r : rows) out.push_back(r);
        out.push_back("};");
        out.push_back("// after Benoit: Jeff Jarrett, Droz, Mideon, The Blue Meanie");
        out.push_back(strf("#define EXT_LATE_N %zu", LATE.size()));
        out.push_back("static const ExtWrestler ext_late[EXT_LATE_N] = {");
        for (size_t k = 0; k < LATE.size(); k++) {
            const Late& e = LATE[k];
            int v0 = e.video0, v1 = e.video1;
            if (e.video_of) v0 = v1 = wv.at(e.video_of).get<int>();
            out.push_back(strf("    { 0x%04X, %d, \"%s\", \"%s\", \"\", { %d, %d }, { %d, %d }, %d, %d },", e.id,
                               ORD0 + (int)ext_table().size() + 1 + (int)k, e.name, e.short_name, e.music0, e.music1, v0, v1, hw[k][0], hw[k][1]));
        }
        out.push_back("};");
        out.push_back("// whose moveset each of them has: his own row of moveset_key, or -- a second ID, gen/revbanks.py -- his base's (roster.c moves_whose)");
        std::string lr;
        for (size_t k = 0; k < late_row.size(); k++) lr += (k ? ", " : "") + std::to_string(late_row[k]);
        out.push_back("static const u8 ext_late_moveset[EXT_LATE_N] = { " + lr + " };");
        out.push_back("// the movesets as the roster keeps them, packed (the new Superstars', then Big Show's): the game");
        out.push_back("// unpacks one right with the cells listed set in its lists of moves while it does (roster.c)");
        out.push_back(strf("#define MOVESETS_N %zu", keys.size()));
        out.push_back(strf("#define MOVESET_BIG_SHOW %zu", keys.size() - LATE.size() - 1));
        out.push_back(strf("#define MOVESET_LATE0 %zu   // (the Superstars after Benoit's, in ext_late's order)", keys.size() - LATE.size()));
        out.push_back("static const u8 moveset_key[MOVESETS_N][0xD8] = {");
        for (auto& [p, c] : keys) {
            std::string s = "    { ";
            for (size_t i = 0; i < p.size(); i++) s += strf(i ? ", 0x%02X" : "0x%02X", p[i]);
            out.push_back(s + " },");
        }
        out.push_back("};");
        size_t mc = 1; for (auto& [p, c] : keys) mc = std::max(mc, c.size());
        out.push_back("typedef struct { u8 list, cell; u16 move; } MoveCell;");
        out.push_back(strf("#define MOVESET_CELLS %zu", mc));
        std::string nc;
        for (size_t k = 0; k < keys.size(); k++) nc += (k ? ", " : "") + std::to_string(keys[k].second.size());   // (an absent row: 0)
        out.push_back("static const u8 moveset_ncells[MOVESETS_N] = { " + nc + " };");
        out.push_back("static const MoveCell moveset_cells[MOVESETS_N][MOVESET_CELLS] = {");
        for (size_t k = 0; k < keys.size(); k++) {
            const auto& c = keys[k].second;
            if (absent[k]) { out.push_back("    " + zero_row("moveset_cells", k, keys.size()) + ","); continue; }
            std::string s = "    { ";
            for (size_t i = 0; i < c.size(); i++) s += strf(i ? ", { %d, %d, %d }" : "{ %d, %d, %d }", c[i][0], c[i][1], c[i][2]);
            out.push_back(s + " },");
        }
        out.push_back("};");
        std::string txt;
        for (auto& l : out) txt += l + "\n";
        return crlf(txt);
    }

#include "ri_ports_parts.inc"
#include "ri_ports_movelib.inc"
};

Ports::Ports(Roms& R, const fs::path& ri_data_dir) : p_(std::make_unique<Impl>(R, ri_data_dir)) { p_->init(); }
Ports::~Ports() = default;
Ports::RevAnims Ports::revanims() { return p_->revanims(); }
void Ports::set_shape(const HdrFill* layouts) { p_->shape = layouts; }
std::string Ports::roster_ext_h() { return p_->roster_ext_h(); }
Ports::Parts Ports::mkparts() { return p_->mkparts(); }
std::string Ports::move_lib_h(const Bytes& movelib_revenge_bin, const std::string& rev_anims_json) { return p_->move_lib_h(movelib_revenge_bin, rev_anims_json); }
std::string Ports::parts_ovr_h(const std::vector<std::pair<std::pair<std::string, int>, Bytes>>& wm_models) { return p_->parts_ovr_h(wm_models); }

}  // namespace ri
