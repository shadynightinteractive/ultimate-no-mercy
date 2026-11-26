// ri_revmoves.cpp (phase3 p3folders / p3wmmoves, 2026-11-19): a never-ported Revenge man's moveset (ri_revmoves.h).
//
// The order of things is aki_import.py's: --all makes his moves.json and packed moveset (port_rev_moveset, pack_plain,
// moves_json, own_slots, no_struggle, win_pose, band_abilities), then --refresh reads them back and runs its own pass over them
// (own_slots, no_struggle, win_pose again -- a second pass usually changes nothing, but it is run, as the Python runs it --
// and the band abilities by the folder's weight, which for a man Revenge gives no weight is No Mercy's non-wrestlers' code).
#include "ri_revmoves.h"
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

constexpr int NONE = INT_MIN;
constexpr uint32_t BUF_M = 0x80780000, BUF_P = 0x80780100, BUF_S = 0x80781000, BUF_A = 0x80782000;
constexpr uint32_t VAN_UNPACK = 0x80005EFC, VAN_PACK = 0x80005EC0;
constexpr int CARRIER = 16250;                       // (fixes2.py: the Back Body Flip, a strong grapple that ends)
constexpr int NM_LADY_STANCE = 4;                    // (fixes2.py: Revenge's women stand as No Mercy's non-wrestling women)
constexpr int NM_WOMAN = 15, NM_WOMAN_WEIGHT = 500, NM_STAFF_WEIGHT = 501;   // (handlers\revenge\body.py)
const std::map<int, int> NM_CODE_LBS = { { 500, 220 }, { 501, 240 }, { 502, 400 }, { 503, 220 } };

struct Slot { int m, f2, f3, f4; bool operator==(const Slot& o) const { return m == o.m && f2 == o.f2 && f3 == o.f3 && f4 == o.f4; } };
// a moves.json slot: a No Mercy move number, or a library ID ("revenge:<pair>@<move>")
struct JSlot { std::string id; int m, f2, f3, f4; };
int played_on(const JSlot& s) {                      // (fixes2.py's `int(v.split("@")[1].split("/")[0])`, or the number)
    if (s.id.empty()) return s.m;
    size_t a = s.id.find('@');
    if (a == std::string::npos) return NONE;
    std::string t = s.id.substr(a + 1); t = t.substr(0, t.find('/'));
    return std::stoi(t);
}
std::string slot_text(const JSlot& s) { return s.id.empty() ? std::to_string(s.m) : s.id; }   // (Python's "%s")

// ---- the texts (aki_import.py) ------------------------------------------------------------------------------------------
const char* const MOVES_ABOUT =
    "his moveset, slot by slot, as the handler mapped Revenge's list (gen/mkroster.py port_rev_moveset): [move, flag, "
    "flag, flag] per slot 0-291 -- a slot that plays his own Revenge move names it in the built-in Revenge library "
    "(\"revenge:<attacker file>_<defender file>@<the No Mercy move it's played on>\": %d of his slots); abilities 0-54";
const char* const OWN_ABOUT =
    "; 2026-11-14: %d slots that had kept the reference's move (The Rock's / Kane's) have his own Revenge move of the "
    "slot's kind instead, the No Mercy move whose animation is his byte for byte (handlers\\revenge\\fixes2.py); %d "
    "kept it: no move of his is of the slot's kind";
const char* const SLAM_ABOUT =
    "; %d strong grapple slots that had No Mercy's 960, the slam a man struggles through, play his own Revenge slam "
    "on the Back Body Flip (16250), a slam that ends -- his Revenge pair from the library (fixes2.no_struggle)";
const char* const ABIL_WHY =
    "abilities No Mercy's neutral ones for the %s band (handlers\\revenge\\nm_band_abilities.json: the value most of No Mercy's "
    "own men of that band have, the weight class with them), his allies his own -- not his reference's (The Rock's / "
    "Kane's) block; Revenge's own ability file isn't mapped yet (2026-11-15)";
const char* const WIN_WHY =
    "winning taunt (slot 235): %s was a turnbuckle taunt (No Mercy has it only in slot 232) -- played where the winner stands "
    "he climbed an invisible corner; now his own standing taunt, %s (his slot 228's) (2026-11-15)";

std::string hex_of(const Bytes& b) {
    static const char* H = "0123456789abcdef";
    std::string s; for (uint8_t c : b) { s += H[c >> 4]; s += H[c & 15]; }
    return s;
}
Bytes from_hex(const std::string& h) { Bytes b; for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back((uint8_t)std::stoul(h.substr(i, 2), nullptr, 16)); return b; }
ojson read_json(const fs::path& p) { Bytes t = read_file(p); return ojson::parse(std::string(t.begin(), t.end())); }

}  // namespace

struct RevMoves::Impl {
    Roms& R;
    const RomFiles *N = nullptr, *RV = nullptr;
    std::unique_ptr<Emu> EV;                          // No Mercy at its Superstar Select (mkroster.py EV)
    std::vector<Bytes> van_sets;
    std::map<int, std::pair<std::vector<Slot>, std::vector<int>>> van_cache;
    std::map<int, std::set<int>> CAN;                 // (mkroster.py CAN: the moves each slot's lists hold)
    std::map<int, std::vector<int>> CAN_kind;
    std::vector<int> NM1;
    std::map<int, std::set<int>> pool;                // (revmoves.slot_pool(CAN))
    std::vector<int> t;                               // (revlib.tables: Revenge's code -> list place, 279)
    std::vector<std::array<int, 3>> derived;          // (revlib.derived_table: (entry, n, step))
    std::set<int> derived_entries;
    std::vector<std::pair<int, std::vector<int>>> groups;   // (fixes2 _context "groups", dict order)
    std::map<int, std::optional<std::string>> first;  // (fixes2 _context "first": pool move -> its animation's body hash)
    std::map<int, int> pos;                           // (frozen: slot -> its place in a Revenge list)
    std::map<int, std::map<int, int>> picks;          // (frozen: moveset file -> {slot: move})
    std::map<std::string, std::map<int, std::string>> lib_ids;    // (library_ids.json)
    std::map<std::string, ojson> rec_from_rom;        // (records_damage.json)
    ojson bands;                                      // (nm_band_abilities.json "bands")
    std::map<int, int> B0;                            // (revanims.byte0_table)
    std::map<std::pair<Bytes, Bytes>, std::string> movelib_pairs;  // (fixes2.library_ids)
    std::map<int, std::set<int>> taunt_slots;
    std::map<uint32_t, std::optional<std::string>> revh_cache, nmh_cache;

    Impl(Roms& r) : R(r) {}

    // ---- No Mercy's moveset code (as ri_wmmoves: mkroster.py unpack / pack on EV) ---------------------------------------
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
            uint32_t p = BUF_S + 6 * (uint32_t)i;
            E.w16(p, slots[i].m); E.w8(p + 2, slots[i].f2); E.w8(p + 3, slots[i].f3); E.w8(p + 4, slots[i].f4);
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
        if (o < 0 || o >= (int)van_sets.size() || van_sets[o].empty()) fail("revmoves: No Mercy has no moveset at ordinal %d", o);
        return van_cache[o] = nm_unpack(van_sets[o].data(), van_sets[o].data() + 0xB8);
    }
    int ord_id(int o) { return EV->r16(EV->r32(0x80047124 + 16 * o)); }     // (aki_import.py ord_id: his game number)
    bool in_can(int i, int v) const { auto it = CAN.find(i); return it != CAN.end() && it->second.count(v); }

    // ---- the files' body hashes (revmoves.py _body_hash: md5 of the bytes past the first, a file over 8 bytes) ------------
    static std::optional<std::string> body_hash(const Bytes* d) {
        if (!d || d->size() <= 8) return std::nullopt;
        return md5_hex(d->data() + 1, d->size() - 1);
    }
    const std::optional<std::string>& revh(uint32_t a) {
        auto it = revh_cache.find(a);
        if (it != revh_cache.end()) return it->second;
        return revh_cache[a] = a ? body_hash(RV->get(a)) : std::nullopt;
    }
    const std::optional<std::string>& nmhash(uint32_t f) {
        auto it = nmh_cache.find(f);
        if (it != nmh_cache.end()) return it->second;
        return nmh_cache[f] = body_hash(N->get(f));
    }
    std::vector<int> revlist(int f) {                 // (revmoves.revlist: his file f + 2, 763 u16)
        const Bytes& d = (*RV)(f + 2);
        if (d.size() != 2 * 763) fail("revmoves: Revenge's list file 0x%X isn't 763 entries", f + 2);
        std::vector<int> o; for (int i = 0; i < 763; i++) o.push_back(be16(d, 2 * i));
        return o;
    }
    static int at(const std::vector<int>& v, long i) {
        if (i < 0) i += (long)v.size();
        if (i < 0 || i >= (long)v.size()) fail("revmoves: list index %ld out of range", i);
        return v[(size_t)i];
    }

    // ---- revlib.py Anims (no_struggle): Revenge's animation files as No Mercy plays them ----------------------------------
    bool an_ok(int f) {
        const Bytes* d = RV->get(f);
        if (!d || d->empty()) fail("revmoves: Revenge file 0x%X has no bytes", f);
        return B0.count((*d)[0]) > 0;
    }
    Bytes an_bytes(int f) { Bytes d = (*RV)(f); d[0] = (uint8_t)B0.at(d[0]); return d; }

    // ---- set up -----------------------------------------------------------------------------------------------------------
    void init(const fs::path& ri_data, const fs::path& rev_dir) {
        N = R.files("nomercy"); RV = R.files("revenge");
        auto nrom = R.rom("nomercy");
        if (!N || !RV || !nrom) fail("revmoves: the No Mercy and Revenge ROMs are needed");
        // No Mercy at its Select (mkroster.py): the image, the rig's hook out of the unpack taken away, its lists, movesets
        Bytes van = make_image("van_sel", ri_data / "loadmaps", R);
        put_be32(&van[0x5EFC], 0x27BDFFE0); put_be32(&van[0x5F00], 0xAFB00010);
        EV = std::make_unique<Emu>(van);
        Emu& E = *EV;
        std::vector<std::pair<int, std::vector<int>>> layout;
        for (uint32_t p = E.r32(0x800442DC); sx16(E.r16(p)) != -1; p += 10) {
            std::vector<int> cs; for (int i = 0; i < 4; i++) cs.push_back(sx16(E.r16(p + 2 + 2 * i)));
            layout.push_back({ sx16(E.r16(p)), cs });
        }
        int ncat = 0; for (auto& [s, cs] : layout) for (int c : cs) ncat = std::max(ncat, c + 1);
        std::vector<std::vector<int>> vals;
        for (int c = 0; c < ncat; c++) {
            int w = sx16(E.r16(0x800442E4 + 2 * c)); uint32_t tb = E.r32(0x80045500 + 4 * c);
            std::vector<int> v; for (int k = 0; k < (1 << w); k++) v.push_back(E.r16(tb + 2 * k));
            vals.push_back(v);
        }
        for (auto& [s, cs] : layout) {
            std::set<int> S; for (int c : cs) S.insert(vals[c].begin(), vals[c].end());
            CAN[s] = S;
        }
        uint32_t vm = E.r32(0x80047E94);
        for (int o = 0; o < 84; o++) van_sets.push_back(o == 14 ? Bytes() : E.rbytes(vm + o * 0xD8, 0xD8));
        // NM1 (file 0x1F0), the slots' pool (revmoves.slot_pool), each pool move's animation hash (fixes2 _context first)
        const Bytes& f1 = (*N)(0x1F0);
        for (size_t o = 0; o + 1 < f1.size(); o += 2) NM1.push_back(sx16(be16(f1, o)));
        for (int s = 0; s < 292; s++) {
            std::set<int> p;
            if (CAN.count(s)) { for (int v : CAN[s]) if (v != 0 && v != 0xFFFF) p.insert(v); }
            else for (int o = 0; o < 84; o++) {
                if (van_sets[o].empty()) continue;
                int v = van_unpacked(o).first[s].m;
                if (v != 0 && v != 0xFFFF) p.insert(v);
            }
            pool[s] = p;
        }
        for (auto& [s, ms] : pool)
            for (int m : ms) if (m >= 0 && m < (int)NM1.size() && NM1[m] > 0) first[m] = nmhash(NM1[m] + 0x3322);
        // where No Mercy's movesets hold each taunt (fixes2.win_pose)
        for (int o = 0; o < 84; o++) {
            if (van_sets[o].empty()) continue;
            auto& s = van_unpacked(o).first;
            for (int k = 224; k < 236; k++) if (s[k].m != 0 && s[k].m != 0xFFFF) taunt_slots[s[k].m].insert(k);
        }
        // revlib.tables: Revenge in a match, its code -> list place table (0x80030570)
        Bytes bm = make_image("rev_bm", ri_data / "loadmaps", R);
        for (int i = 0; i < 279; i++) t.push_back(sx16(be16(bm, 0x30570 + 2 * i)));
        // revlib.derived_table (from the No Mercy ROM): only the entries with to <= 0
        {
            Bytes start;
            for (int e : { 5, 10, 15 }) for (int x : { e, -4, 2 }) { start.push_back((uint8_t)((x >> 8) & 0xFF)); start.push_back((uint8_t)(x & 0xFF)); }
            const Bytes& rom = *nrom;
            auto it = std::search(rom.begin(), rom.end(), start.begin(), start.end());
            if (it == rom.end()) fail("revlib: the derived entries' table isn't in the No Mercy ROM");
            for (size_t a = it - rom.begin();; a += 6) {
                int e = sx16(be16(rom, a)), to = sx16(be16(rom, a + 2)), step = sx16(be16(rom, a + 4));
                if (e == -1) break;
                if (to <= 0) derived.push_back({ e, std::max(1, -to), step });
            }
        }
        for (auto& [e, n, st] : derived) {
            std::vector<int> kids; for (int k = 1; k <= n; k++) { derived_entries.insert(e + k); kids.push_back(e + k); }
            bool found = false;
            for (auto& g : groups) if (g.first == e) { g.second = kids; found = true; }
            if (!found) groups.push_back({ e, kids });
        }
        // revanims.byte0_table: Revenge's first byte -> No Mercy's, over the animations both games have
        {
            std::map<Bytes, OrderedCounter<int>> nm;
            for (uint32_t f = 0x3322; f < 0x4400; f++) {
                const Bytes* d = N->get(f);
                if (d && d->size() > 8) nm[Bytes(d->begin() + 1, d->end())].add((*d)[0]);
            }
            std::map<int, OrderedCounter<int>> m;
            for (uint32_t f = 0x110B; f < 0x1D00; f++) {
                const Bytes* d = RV->get(f);
                if (!d || d->size() <= 8) continue;
                auto it = nm.find(Bytes(d->begin() + 1, d->end()));
                if (it == nm.end()) continue;
                for (auto& [b, c] : it->second.items) m[(*d)[0]].add(b, c);
            }
            for (auto& [a, c] : m) B0[a] = c.most();
        }
        // fixes2.library_ids: the library's pair moves without a hold, by their two animations' bytes (the first ID wins)
        {
            Bytes b = movelib_revenge(R, rev_dir / "library_index.json");
            auto u32 = [&](size_t a) { return (uint32_t)b[a] | (uint32_t)b[a + 1] << 8 | (uint32_t)b[a + 2] << 16 | (uint32_t)b[a + 3] << 24; };
            if (b.size() < 32 || std::string(b.begin(), b.begin() + 8) != "NMMOVLIB" || u32(8) != 1) fail("revmoves: movelib_revenge.bin isn't one");
            uint32_t n = u32(12), na = u32(16), sbytes = u32(24);
            size_t mv = 32, an = 32 + 16 * (size_t)n, st = an + 44 * (size_t)na, da = st + sbytes;
            auto anim = [&](uint32_t k) { size_t o = da + u32(an + 44 * k); return Bytes(b.begin() + o, b.begin() + o + u32(an + 44 * k + 4)); };
            for (uint32_t i = 0; i < n; i++) {
                size_t a = mv + 16 * i;
                uint32_t name = u32(a), pair = u32(a + 8), block = u32(a + 12);
                if (pair == 0xFFFFFFFF || block != 0xFFFFFFFF) continue;
                size_t s = st + name, e = s; while (b[e]) e++;
                // (2026-11-20, revmoves2: by each animation's bytes after the first -- the library's first byte is the No Mercy
                // twin's where it has one, ri_movelib.cpp xref; fixes2.py the same)
                Bytes pa = anim(pair), pd = anim(pair + 1);
                movelib_pairs.emplace(std::make_pair(Bytes(pa.begin() + 1, pa.end()), Bytes(pd.begin() + 1, pd.end())), std::string(b.begin() + s, b.begin() + e));
            }
        }
        // our data
        ojson fz = read_json(ri_data / "frozen" / "rev_moveset_picks.json");
        for (auto& [s, v] : fz["positions"].items()) pos[std::stoi(s)] = v[0].get<int>();
        for (auto& [f, m] : fz["picks"].items()) { auto& d = picks[(int)std::stoul(f, nullptr, 16)]; for (auto& [s, v] : m.items()) d[std::stoi(s)] = v.get<int>(); }
        ojson li = read_json(rev_dir / "library_ids.json");
        for (auto& [sid, m] : li.items()) {
            if (sid == "about") continue;
            auto& d = lib_ids[sid]; for (auto& [s, v] : m["ids"].items()) d[std::stoi(s)] = v.get<std::string>();
        }
        ojson rd = read_json(ri_data / "frozen" / "records_damage.json");
        for (auto& [sid, v] : rd.items()) rec_from_rom[sid] = v;
        bands = read_json(rev_dir / "nm_band_abilities.json")["bands"];
    }

    // ---- revenge\game.py pack_plain -----------------------------------------------------------------------------------
    std::pair<Bytes, int> pack_plain(const std::vector<Slot>& slots, const std::vector<int>& ab, int ref) {
        auto rslots = van_unpacked(ref).first;
        std::vector<Slot> fixed;
        for (int i = 0; i < (int)slots.size(); i++) { Slot s = slots[i]; if (CAN.count(i) && !CAN[i].count(s.m)) s.m = rslots[i].m; fixed.push_back(s); }
        Bytes packed;
        for (int k = 0; k < 4; k++) {
            packed = nm_pack(fixed, ab);
            auto back = nm_unpack(packed.data(), packed.data() + 0xB8).first;
            std::vector<int> bad; for (int i = 0; i < 292; i++) if (!(back[i] == fixed[i])) bad.push_back(i);
            if (bad.empty()) break;
            for (int i : bad) fixed[i] = rslots[i];
        }
        int n = 0; for (size_t i = 0; i < fixed.size(); i++) n += !(fixed[i] == slots[i]);
        return { packed, n };
    }
    // fixes2.py repack_abilities: the moves' bytes as they are, the abilities packed again (checked by the game's unpacking)
    std::string repack_abilities(const std::string& hex, const std::vector<int>& ab) {
        Bytes b = from_hex(hex);
        auto un = nm_unpack(b.data(), b.data() + 0xB8);
        Bytes nb(b.begin(), b.begin() + 0xB8), pk = nm_pack(un.first, ab);
        nb.insert(nb.end(), pk.begin() + 0xB8, pk.end());
        auto back = nm_unpack(nb.data(), nb.data() + 0xB8);
        if (back.second != ab || !(back.first == un.first)) fail("revmoves: the packing changes his abilities");
        return hex_of(nb);
    }

    // ---- gen/mkroster.py port_rev_moveset (no overrides, no allies: a never-ported man), predict from the frozen picks -----
    void port_rev_moveset(int rev_file, int ref, std::vector<Slot>& out, std::vector<int>& ab) {
        auto& [rslots, rab] = van_unpacked(ref);
        auto pk = picks.find(rev_file);
        if (pk == picks.end()) fail("revmoves: moveset file 0x%X isn't in rev_moveset_picks.json (revmoves_freeze.py write)", rev_file);
        out = rslots;
        for (int i = 0; i < 292; i++) {
            int v = rslots[i].m;
            if (pos.count(i) && v != 0 && v != 0xFFFF) { auto p = pk->second.find(i); if (p != pk->second.end()) v = p->second; }
            if (v == 0xFFFF && CAN.count(i) && !CAN[i].count(0xFFFF)) v = 0;
            out[i] = { v, rslots[i].f2, rslots[i].f3, rslots[i].f4 };
        }
        ab = rab;
        for (int j = 31; j < 35; j++) ab[j] = 0;
    }

    // ---- fixes2.py own_slots ---------------------------------------------------------------------------------------------
    int own_slots(std::vector<JSlot>& out, int rev_file, int ref_ord, int& n_left) {
        const auto& ref = van_unpacked(ref_ord).first;
        std::vector<int> rl = revlist(rev_file);
        std::map<std::optional<std::string>, std::vector<std::pair<int, int>>> his;   // (his animation's content -> [(code, place)])
        for (size_t code = 0; code + 1 < t.size(); code++)
            for (int q = t[code]; q < t[code + 1]; q++) {
                int v = at(rl, q);
                if (v) his[revh(v + 0x110B)].push_back({ (int)code, q });
            }
        int changed = 0; n_left = 0;
        for (int s = 0; s < (int)out.size(); s++) {
            const JSlot& o = out[s];
            if (!o.id.empty() || o.m == 0 || o.m == 0xFFFF || o.m != ref[s].m || derived_entries.count(s)) continue;
            bool lib = false;
            for (auto& g : groups) if (g.first == s) for (int d : g.second) lib = lib || !out[d].id.empty();
            if (lib) continue;
            std::vector<std::array<int, 4>> cand;
            auto ps = pool.find(s);
            if (ps != pool.end())
                for (int m : ps->second) {
                    auto h = first.find(m);
                    if (h == first.end() || !h->second) continue;
                    auto hs = his.find(h->second);
                    if (hs == his.end()) continue;
                    for (auto& [code, q] : hs->second) {
                        auto pp = pos.find(s);
                        cand.push_back({ (pp != pos.end() && pp->second == q) ? 0 : 1, q == t[code] ? 0 : 1, m, code });
                    }
                }
            if (cand.empty()) { n_left++; continue; }
            bool same = false; for (auto& c : cand) same = same || c[2] == o.m;
            if (same) continue;
            std::sort(cand.begin(), cand.end());
            if (cand[0][2] != o.m) { out[s].m = cand[0][2]; changed++; }
        }
        return changed;
    }

    // ---- fixes2.py no_struggle -------------------------------------------------------------------------------------------
    int no_struggle(std::vector<JSlot>& out, int rev_file) {
        std::vector<int> rl = revlist(rev_file);
        int changed = 0;
        for (auto& [s, kids] : groups) {
            int on = played_on(out[s]);
            if (on == NONE || on < 960 || on >= 970 || !pos.count(s)) continue;
            bool lib = false; for (int d : kids) lib = lib || !out[d].id.empty();
            if (lib) continue;
            int j = pos[s];
            if (!(at(rl, j) && at(rl, j + 1))) continue;
            int fa = rl[j] + 0x110B, fd = rl[j + 1] + 0x110B;
            if (!(an_ok(fa) && an_ok(fd))) continue;
            Bytes ba = an_bytes(fa), bd = an_bytes(fd);
            auto it = movelib_pairs.find({ Bytes(ba.begin() + 1, ba.end()), Bytes(bd.begin() + 1, bd.end()) });
            if (it == movelib_pairs.end() || it->second.empty()) continue;
            out[s].id = strf("%s@%d", it->second.c_str(), CARRIER);
            changed++;
        }
        return changed;
    }

    // ---- fixes2.py win_pose ----------------------------------------------------------------------------------------------
    std::optional<std::pair<std::string, std::string>> win_pose(std::vector<JSlot>& out) {
        JSlot& w = out[235];
        int m = w.id.empty() ? w.m : NONE;
        if (!w.id.empty() && w.id.find('@') != std::string::npos) m = played_on(w);
        auto ts = taunt_slots.find(m);
        if (m == NONE || ts == taunt_slots.end() || ts->second != std::set<int>{ 232 }) return std::nullopt;
        for (int s : { 228, 229, 230 }) {
            JSlot& o = out[s];
            if (!o.id.empty() || (o.m != 0 && o.m != 0xFFFF)) {
                std::string old = slot_text(w);
                w.id = o.id; w.m = o.m;
                return std::make_pair(old, slot_text(o));
            }
        }
        return std::nullopt;
    }

    // ---- fixes2.py band_of / band_abilities ------------------------------------------------------------------------------
    static std::string band_of(std::optional<int> lbs, bool woman) {
        if (woman) return "woman";
        if (!lbs) return "heavy";
        return *lbs < 231 ? "light" : (*lbs >= 400 ? "super" : "heavy");
    }
    std::vector<int> band_abilities(std::optional<int> lbs, bool woman, const std::vector<int>& allies) {
        std::string b = band_of(lbs, woman);
        std::vector<int> ab = bands[b]["abilities"].get<std::vector<int>>();
        if (b == "woman") ab[1] = NM_LADY_STANCE;
        for (int k = 0; k < 4; k++) ab[31 + k] = k < (int)allies.size() ? allies[k] : 0;
        return ab;
    }
};

RevMoves::RevMoves(Roms& R, const fs::path& ri_data_dir, const fs::path& rev_handler_dir) : p_(std::make_unique<Impl>(R)) {
    p_->init(ri_data_dir, rev_handler_dir);
}
RevMoves::~RevMoves() = default;

std::optional<RevMovesetPiece> RevMoves::man(const RevManInfo& info) {
    Impl& I = *p_;
    const RevRecord& e = *info.e;
    if (!e.moveset_file) return std::nullopt;
    int f = *e.moveset_file, ref = info.reference_ordinal;
    bool woman = info.body_type == NM_WOMAN;
    RevMovesetPiece out;
    // ======== aki_import.py revenge --all ========
    std::vector<Slot> slots; std::vector<int> ab;
    I.port_rev_moveset(f, ref, slots, ab);
    auto [packed, fell] = I.pack_plain(slots, ab, ref);
    (void)fell;                                       // (the report's only: wrestler.json has no "moveset_fallbacks" here)
    std::string moveset = hex_of(packed);
    // moves_json: his slots, the library IDs (library_ids.json), his abilities, the records (--records)
    std::vector<JSlot> js;
    for (auto& s : slots) js.push_back({ "", s.m, s.f2, s.f3, s.f4 });
    auto li = I.lib_ids.find(info.sid);
    size_t n_ids = 0;
    if (li != I.lib_ids.end()) { n_ids = li->second.size(); for (auto& [s, id] : li->second) if (s >= 0 && s < (int)js.size()) js[s].id = id; }
    std::string about = strf(MOVES_ABOUT, (int)n_ids);
    std::vector<int> abil = ab;
    std::optional<ojson> records;
    auto rr = I.rec_from_rom.find(info.sid);
    if (rr != I.rec_from_rom.end()) {
        ojson r = ojson::object();
        for (auto& [slot, rec] : rr->second.items()) if (!r.contains(slot)) r[slot] = rec;
        records = r;
    }
    int n_left = 0;
    int n_own = I.own_slots(js, f, ref, n_left);
    int n_slams = I.no_struggle(js, f);
    about += strf(OWN_ABOUT, n_own, n_left) + (n_slams ? strf(SLAM_ABOUT, n_slams) : std::string());
    if (auto wp = I.win_pose(js)) out.win_pose = strf(WIN_WHY, wp->first.c_str(), wp->second.c_str());
    std::optional<int> lbs = e.weight ? std::optional<int>(*e.weight + 100) : std::nullopt;
    std::vector<int> ab2 = I.band_abilities(lbs, woman, std::vector<int>(ab.begin() + 31, ab.begin() + 35));
    moveset = I.repack_abilities(moveset, ab2); abil = ab2;
    out.abilities = strf(ABIL_WHY, Impl::band_of(lbs, woman).c_str());
    // ======== aki_import.py revenge --refresh (his folder's moves.json and wrestler.json read back) ========
    int bid = I.ord_id(ref);                           // (his "based_on": "0x%04X" of his reference's game number)
    int ref2 = bid == 0x0004 ? 4 : (bid == 0x0702 ? 51 : 1);
    int n_own2 = I.own_slots(js, f, ref2, n_left);
    int n_slams2 = I.no_struggle(js, f);
    if (n_own2 || n_slams2 || about.find("; 2026-11-14:") == std::string::npos)
        about = about.substr(0, about.find("; 2026-11-14:")) + strf(OWN_ABOUT, n_own2, n_left) + (n_slams2 ? strf(SLAM_ABOUT, n_slams2) : std::string());
    if (auto wp = I.win_pose(js)) out.win_pose = strf(WIN_WHY, wp->first.c_str(), wp->second.c_str());
    {
        // the folder's weight: a woman's / a non-wrestler's No Mercy code, else his Revenge weight (lbs - 100)
        int weight = woman ? NM_WOMAN_WEIGHT : (!e.weight ? NM_STAFF_WEIGHT : *e.weight);
        auto c = NM_CODE_LBS.find(weight);
        std::optional<int> elbs = c != NM_CODE_LBS.end() ? c->second : std::min(weight, 499) + 100;
        Bytes b = from_hex(moveset);
        std::vector<int> old = I.nm_unpack(b.data(), b.data() + 0xB8).second;
        std::vector<int> ab3 = I.band_abilities(elbs, woman, std::vector<int>(old.begin() + 31, old.begin() + 35));
        if (ab3 != old) {
            moveset = I.repack_abilities(moveset, ab3);
            if (!abil.empty()) abil = ab3;
            out.abilities = strf(ABIL_WHY, Impl::band_of(elbs, woman).c_str());
        }
    }
    // moves.json as the refresh writes it (json.dump: one line)
    ojson j = ojson::object();
    j["about"] = about;
    ojson sl = ojson::array();
    for (auto& s : js) {
        ojson a = ojson::array();
        if (s.id.empty()) a.push_back(s.m); else a.push_back(s.id);
        a.push_back(s.f2); a.push_back(s.f3); a.push_back(s.f4);
        sl.push_back(a);
    }
    j["slots"] = sl;
    j["abilities"] = abil;
    if (records) j["records"] = *records;
    out.moves_json = py_dumps(j);
    out.moveset = moveset;
    return out;
}

}  // namespace ri
