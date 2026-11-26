// ri_models.cpp (phase3, 2026-11-19): the source games' models by their own code (ri_models.h) -- wmmodels.py and
// revmodels.py, step for step.
#include "ri_models.h"
#include "ri_mips.h"
#include "json/json.hpp"
#include <map>

namespace ri {

using json = nlohmann::ordered_json;    // (the files' own key order, as Python's json.load keeps it)

static json load_json(const fs::path& p) {
    Bytes b = read_file(p);
    return json::parse(std::string(b.begin(), b.end()));
}
static Bytes map_image(Roms& R, const RiData& D, const std::string& name) {
    Bytes t = read_file(D.dir / "loadmaps" / (name + ".json"));
    return image_from_map(std::string(t.begin(), t.end()), R);
}

// ---- WrestleMania 2000 (wmmodels.py) --------------------------------------------------------------------------------
static const uint32_t ROSTER = 0x800411B0;        // roster place -> record pointer (76)
static const uint32_t COSTUMES = 0x80090320;      // 4 x 66-byte costume record a place; packed looks at +45 (21 bytes)
static const uint32_t RECORDS = 0x80294A10;       // the 188-byte records (the table *0x8004138C; the capture's heap block)
static const uint32_t PART_LIST = 0x800B0FC0, PART_STRUCT = 0x8008DC30, PART_COSTUME = 0x8009E8F0;
static const uint32_t POOL = 0x80700000;

// The emulator with WrestleMania 2000's hooks: a bump heap for its malloc, its file info / read answered from the ROM's
// files (unpacked), free and two other calls that only return.
struct WmEmu : Emu {
    uint32_t heap;
    WmEmu(const Bytes& img, const RomFiles& F, uint32_t heap0) : Emu(img), heap(heap0) {
        hooks[0x80000898] = [this](Emu& e) { uint32_t n = (e.r[4] + 15) & ~15u; e.r[2] = heap; heap += n; return true; };
        hooks[0x800009A0] = [](Emu&) { return true; };
        hooks[0x80003DD4] = [&F](Emu& e) { e.w32(e.r[4] + 8, (uint32_t)F(e.r[5] & 0xFFFF).size()); e.r[2] = e.r[4]; return true; };
        hooks[0x80003E98] = [&F](Emu& e) {
            const Bytes& d = F(e.r[5] & 0xFFFF); e.w32(e.r[4] + 8, (uint32_t)d.size()); e.wbytes(e.r[6], d); e.r[2] = e.r[4]; return true;
        };
        auto ret0 = [](Emu& e) { e.r[2] = 0; return true; };
        hooks[0x8002A250] = ret0; hooks[0x8002A2C0] = ret0;
    }
    Bytes image() const { return Bytes(mem(), mem() + SIZE); }
};

// Step 1: the Select image with the game's roster built (its move tables 0x800F6180, then the roster builder 0x8000ADA4).
static std::unique_ptr<WmEmu> select_built(Roms& R, const RiData& D, const std::string& game, const std::string& sel_map) {
    auto S = std::make_unique<WmEmu>(map_image(R, D, sel_map), *R.files(game), 0x80600000);
    S->call(0x800F6180, {}, {}, 0x807FF000, 50'000'000);
    S->w32(0x8004138C, RECORDS); S->wbytes(RECORDS, Bytes(76 * 188, 0));
    S->call(0x8000ADA4, {}, {}, 0x807FF000, 300'000'000);
    return S;
}

// Step 2: his 244-byte participant struct, by the game's 0x8000B048 on the built Select image (its RAM from 6 MB on put
// back after, as the Python does -- the heap pointer is not, so the calls' order matters and is kept).
static Bytes participant(WmEmu& S, uint32_t gid, uint32_t c) {
    Bytes snap(S.mem() + 0x600000, S.mem() + 0x800000);
    S.wbytes(0x807A0000, Bytes(244, 0));
    S.call(0x8000B048, { gid, c, 0x807A0000 }, {}, 0x807FF000, 20'000'000);
    Bytes p = S.rbytes(0x807A0000, 244);
    memcpy(S.mem() + 0x600000, snap.data(), snap.size());
    return p;
}

// Step 3's base: the match image + the costume records' packed looks + an empty object pool at our scratch block.
static Bytes match_base(Roms& R, const RiData& D, const WmEmu& S, const std::string& match_map) {
    Bytes m = map_image(R, D, match_map);
    for (int i = 0; i < 76 * 4; i++) {
        uint32_t a = (COSTUMES & 0x7FFFFF) + 66 * i + 45;
        memcpy(&m[a], S.mem() + a, 21);
    }
    put_be32(&m[0x55C10], POOL);                    // the object pool (0x800018FC)
    put_be16(&m[0x56580], 0); put_be16(&m[0x56582], 0);   // (its count; the registered list's count)
    return m;
}

static std::map<int, uint32_t> ids_of(const Bytes& img) {
    std::map<int, uint32_t> out;
    for (int k = 0; k < 76; k++) out[k] = be16(&img[(be32(&img[(ROSTER + 4 * k) & 0x7FFFFF]) + 4) & 0x7FFFFF]);
    return out;
}

struct Man { uint32_t gid, c; };
// Step 3: the per-slot loader 0x800144C0(slot, ID, costume), one slot after another -> the match RAM.
static Bytes load_match(const Bytes& base, const RomFiles& F, const std::vector<Man>& men, const std::vector<Bytes>& parts, int swap = -1) {
    Bytes m = base;
    if (swap >= 0) {                                 // (wmcap.py: man k's record pointer in place 29, and 29's in k's)
        uint32_t a = (ROSTER & 0x7FFFFF) + 4 * swap, b = (ROSTER & 0x7FFFFF) + 4 * 29;
        for (int i = 0; i < 4; i++) std::swap(m[a + i], m[b + i]);
    }
    uint16_t lst[40]; for (auto& x : lst) x = 0xFF;
    for (size_t i = 0; i < men.size(); i++) {
        lst[i] = (uint16_t)men[i].gid;
        memcpy(&m[(PART_STRUCT & 0x7FFFFF) + 244 * i], parts[i].data(), 244);
        m[(PART_COSTUME & 0x7FFFFF) + i] = (uint8_t)men[i].c;
    }
    for (int i = 0; i < 40; i++) put_be16(&m[(PART_LIST & 0x7FFFFF) + 2 * i], lst[i]);
    WmEmu E(m, F, 0x80600000);
    for (size_t s = 0; s < men.size(); s++) E.call(0x800144C0, { (uint32_t)s, men[s].gid, men[s].c }, {}, 0x807FF000, 200'000'000);
    return E.image();
}

void wm_models(Roms& R, const RiData& D, const std::string& want,
               const std::function<void(const std::string&, const Bytes&)>& out) {
    // (the order is wmmodels.py's full run -- the Select's heap carries from one participant call to the next)
    if (want == "wmport" || want == "rig" || want == "all") {
        if (!R.rom("wm2000")) fail("the WrestleMania 2000 ROM isn't there");
        const RomFiles& F = *R.files("wm2000");
        auto S = select_built(R, D, "wm2000", "wm_sel1");
        Bytes base = match_base(R, D, *S, "wm_match");
        auto ids = ids_of(base);
        for (int k : { 29, 41 })
            for (uint32_t c = 0; c < 4; c++) {
                Bytes p = participant(*S, 0x406, c);
                Bytes m = load_match(base, F, { { 0x406, c } }, { p }, k);
                if (want == "wmport" || want == "all") out(strf("wmport/cap/k%d_c%u/ram.bin", k, c), m);
            }
        if (want == "rig" || want == "all") {
            json idx = load_json(D.dir / "frozen" / "wm2000_rig_index.json");
            std::map<std::string, std::map<int, std::pair<int, int>>> files;     // file -> slot -> (k, c)
            for (auto it = idx.begin(); it != idx.end(); ++it) {
                std::string key = it.key();
                size_t colon = key.find(':');
                int k = std::stoi(key.substr(0, colon)), c = std::stoi(key.substr(colon + 1));
                files[it.value()[0].get<std::string>()][it.value()[1].get<int>()] = { k, c };
            }
            for (auto& [fn, slots] : files) {           // (sorted by file name, slots in order: as rig_plan)
                std::vector<Man> men; std::vector<Bytes> parts;
                for (auto& [slot, kc] : slots) men.push_back({ ids[kc.first], (uint32_t)kc.second });
                for (auto& mm : men) parts.push_back(participant(*S, mm.gid, mm.c));
                out("handlers_rig/wm2000/cap/" + fn, load_match(base, F, men, parts));
            }
        }
    }
    if (want == "baddblood" || want == "all") {
        // Badd Blood (a WrestleMania 2000 mod, the same code at every address used): Vader, Select ID 0x204 in slot 0
        if (!R.rom("baddblood")) fail("the Badd Blood ROM isn't there");
        const RomFiles& Fb = *R.files("baddblood");
        auto Sb = select_built(R, D, "baddblood", "bb_vsel");
        Bytes bbase = match_base(R, D, *Sb, "bb_match");
        for (uint32_t c = 0; c < 4; c++) {
            Bytes p = participant(*Sb, 0x204, c);
            out(strf("handlers_rig/imports2/cap/vader_c%u_ram.bin", c), load_match(bbase, Fb, { { 0x204, c } }, { p }));
        }
    }
}

// ---- WCW/nWo Revenge (revmodels.py) ---------------------------------------------------------------------------------
Bytes rev_match_image(Roms& R, const RiData& D) {
    Bytes m = map_image(R, D, "rev_bm");
    rev_boot_extra(m);                                              // the costume table the boot builds
    put_be32(&m[0x46858], 0x80700000); put_be16(&m[0x47060], 0);   // the object pool: our scratch block, empty
    return m;
}

Bytes rev_entry(const Bytes& img, uint32_t code, uint32_t costume) {
    Emu E(img);
    return E.rbytes(E.call(0x8002B7E0, { code, costume }), 42);
}

// Per slot, as in a match: the slot's two display-list arenas (0x80007A74), the model 0x8000CFD4(slot, code, attire, mask;
// the costume's colour rows on the stack), the picture step 0x8000C1B8(slot, 0) and (slot, 1). Hooks: file info 0x800035B4 /
// read 0x80003684 from the ROM's files, a bump heap for malloc 0x80000854, free 0x80000958 nothing; the part loader
// 0x8000498C logged (not handled: it runs).
RevLoad rev_load(const Bytes& img, const RomFiles& F, const std::vector<RevMan>& men) {
    Emu E(img);
    uint32_t heap = 0x80600000;
    RevLoad out;
    E.hooks[0x80000854] = [&heap](Emu& x) { uint32_t n = (x.r[4] + 15) & ~15u; x.r[2] = heap; heap += n; return true; };
    E.hooks[0x80000958] = [](Emu&) { return true; };
    E.hooks[0x800035B4] = [&F](Emu& x) { x.w32(x.r[4] + 8, (uint32_t)F(x.r[5] & 0xFFFF).size()); x.r[2] = x.r[4]; return true; };
    E.hooks[0x80003684] = [&F](Emu& x) {
        const Bytes& d = F(x.r[5] & 0xFFFF); x.w32(x.r[4] + 8, (uint32_t)d.size()); x.wbytes(x.r[6], d); x.r[2] = x.r[4]; return true;
    };
    E.hooks[0x8000498C] = [&out](Emu& x) { out.calls.push_back({ x.r[4], x.r[5], x.r[6] }); return false; };
    for (size_t slot = 0; slot < men.size(); slot++) {
        const RevMan& m = men[slot];
        Bytes e = rev_entry(img, m.code, m.costume);
        E.call(0x80007A74, { 0x8007C3D0 + 16 * (uint32_t)slot, 0x2BC, 0x4B0 });
        E.call(0x8000CFD4, { (uint32_t)slot, m.code, m.attire, m.mask },
               { (uint32_t)sx8(e[38]), (uint32_t)sx8(e[39]), (uint32_t)sx8(e[40]) }, 0x807FF000, 30'000'000);
        for (uint32_t half = 0; half < 2; half++) E.call(0x8000C1B8, { (uint32_t)slot, half }, {}, 0x807FF000, 30'000'000);
    }
    out.ram.assign(E.mem(), E.mem() + Emu::SIZE);
    return out;
}

void rev_port_models(Roms& R, const RiData& D, const std::function<void(const std::string&, const RevLoad&)>& out) {
    if (!R.rom("revenge")) fail("the Revenge ROM isn't there");
    const RomFiles& F = *R.files("revenge");
    Bytes img = rev_match_image(R, D);
    json J = load_json(D.dir / "frozen" / "revenge_caps.json");
    for (auto it = J["tags"].begin(); it != J["tags"].end(); ++it) {
        std::vector<RevMan> men;
        for (auto& s : it.value()["slots"]) men.push_back({ s[0].get<uint32_t>(), s[1].get<uint32_t>(), s[2].get<uint32_t>(), s[3].get<uint32_t>() });
        out(it.value()["dir"].get<std::string>() + "/" + it.key() + "_ram.bin", rev_load(img, F, men));
    }
}

}  // namespace ri
