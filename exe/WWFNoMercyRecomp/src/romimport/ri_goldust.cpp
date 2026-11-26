// ri_goldust.cpp (phase3 p3goldust, 2026-11-19): Goldust's folder in the "Imported" collection -- see ri_goldust.h.
// Mirrors tools\handlers\imports2.py goldust() and the parts of tools\handlers\handlers\customs\nmplus.py it calls; each
// function below names its Python.
#include "ri_goldust.h"
#include "ri_arenascan.h"
#include "ri_images.h"
#include "ri_json.h"
#include "ri_mips.h"
#include "ri_wmmedia.h"
#include "ri_wmmoves.h"
#include "ri_wmroster.h"
#include "ri_wmimport.h"
#include "rr_core.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace ri {
namespace {

// ---- No Mercy Plus V3, as customs\nmplus.py reads it -------------------------------------------------------------------------
const uint32_t NM_STRUCTS = 0x80401010;     // its per-ordinal costume structs (0x14 bytes, 4 per ordinal; gen/mkroster.py)
const uint32_t BUILD = 0x80018ACC;          // its model builder (the game's func_80018A3C, 0x90 on)
// The appearance entry (0x26 bytes, file 1's format) -> the unpacked looks struct the builder reads (nmplus.py SRC2UN)
const int SRC2UN[38][2] = {
    { 0, 0 }, { 1, 1 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 }, { 8, 9 }, { 9, 0xA }, { 0xA, 0xB },
    { 0x1C, 0xC }, { 0x1D, 0xF }, { 0x1E, 0x12 }, { 0x1F, 0x13 }, { 0x20, 0x15 }, { 0x21, 0x18 }, { 0x22, 0x1B },
    { 0xC, 0x1E }, { 0xD, 0x1F }, { 0x11, 0x21 }, { 0x12, 0x22 }, { 0x13, 0x24 }, { 0x14, 0x25 }, { 0xF, 0x27 },
    { 0x10, 0x28 }, { 0x15, 0x2A }, { 0x16, 0x2B }, { 0x17, 0x2D }, { 0x18, 0x2E }, { 0x19, 0x30 }, { 0x1A, 0x31 },
    { 0x1B, 0x32 }, { 0xE, 0x33 }, { 0x23, 0x36 }, { 0x24, 0x37 }, { 0xB, 0x39 }, { 0x25, 0x3C } };
// the appearance entry's fields by index (nmplus.py APP_FIELDS)
const char* const APP_FIELDS[38] = { "body", "skin", "ring", "ring_c1", "ring_c2", "upper", "upper_c1", "upper_c2", "entr",
    "entr_c1", "entr_c2", "weapon", "gloves", "gloves_c", "tattoo", "wrist", "wrist_c", "lelbow", "lelbow_c", "relbow",
    "relbow_c", "lknee", "lknee_c", "rknee", "rknee_c", "boots", "boots_c1", "boots_c2", "head", "face", "hair", "hair_c",
    "front_hair", "facial_hair", "mask", "hat", "hat_c", "photo" };
int field(const char* name) {
    for (int i = 0; i < 38; i++) if (!std::strcmp(APP_FIELDS[i], name)) return i;
    fail("no looks field %s", name);
}
// His parts by No Mercy Plus's own lists (its Mall's two Goldust packs, file 0x4AA9): costumes = the two packs as sold, then
// each ring attire with the other pack's face paint; hair "Short 1-A", no front hair (nmplus.py RING, MASK, BOOTS, HAIR ...)
const int RING[2] = { 139, 140 }, MASK[2] = { 36, 37 }, BOOTS = 65, HAIR = 2, FRONT_HAIR = 0;
const int COSTUMES[4][2] = { { RING[0], MASK[0] }, { RING[1], MASK[1] }, { RING[0], MASK[1] }, { RING[1], MASK[0] } };
// the game states the builder runs in (gen/mkovr.py): three globals (GLOB_N) and the descriptor's byte 0x254
const int STATES[4][4] = { { 0, 0, 0, 0 }, { 1, 1, 3, 0x20 }, { 1, 0, 3, 0 }, { 0, 1, 2, 0x20 } };
const uint32_t GLOB_N[3] = { 0x800A9EA0, 0x800931E4, 0x80099508 };
bool keep_ours(int k) { return k >= 0x254 && k < 0x258; }      // (KEEP_OURS: the descriptor's own flags, never a file)
// the fields a costume keeps from its base wrestler, named (nmplus.py BASE_KEEPS: check_from_base stops on any other)
const char* const BASE_KEEPS[] = { "body", "skin", "head", "face", "hair_c", "photo" };

ojson load_ojson(const fs::path& p) { Bytes b = read_file(p); return ojson::parse(std::string(b.begin(), b.end())); }
std::string hexs(const Bytes& b) { std::string s; for (uint8_t x : b) s += strf("%02x", x); return s; }

// NMPlus.costume_struct(o, c) -> the fields used here (names entry, appearance number)
struct CostumeStruct { int appearance, names; };
CostumeStruct costume_struct(const Bytes& ram, int o, int c) {
    uint32_t a = (NM_STRUCTS + o * 0x50 + c * 0x14) & 0x7FFFFF;
    return { (int)be16(ram, a + 0xE), (int)be16(ram, a + 0x10) };
}
// NMPlus.name(entry): file 2's u16 offset table, then the string; nullopt where the Python raises (roster_names: None)
std::optional<std::string> nm_name(const RomFiles& files, int entry) {
    const Bytes* f = files.get(2);
    if (!f || (size_t)(2 * entry + 2) > f->size()) return std::nullopt;
    size_t o = be16(*f, 2 * entry);
    if (o >= f->size()) return std::nullopt;
    auto e = std::find(f->begin() + o, f->end(), (uint8_t)0);
    if (e == f->end()) return std::nullopt;
    return std::string(f->begin() + o, e);
}
// NMPlus.appearance(e): file 1's 0x26-byte entry
Bytes appearance(const RomFiles& files, int e) {
    const Bytes& f = files(1);
    return Bytes(f.begin() + 0x26 * e, f.begin() + 0x26 * e + 0x26);
}
// nmplus.py un_of: the appearance entry -> the 0x3F-byte looks struct
Bytes un_of(const Bytes& app) {
    Bytes un(0x3F, 0);
    for (auto& p : SRC2UN) un[p[1]] = app[p[0]];
    return un;
}
// NMPlus.model(un, st): its builder on the looks struct, in one of the game's states -> the 0x270-byte descriptor
Bytes nm_model(const Bytes& ram, const Bytes& un, const int st[4]) {
    Emu E(ram);
    for (int i = 0; i < 3; i++) E.w8(GLOB_N[i], st[i]);
    Bytes u = un; u.push_back(0);
    E.wbytes(0x80781000, u); E.wbytes(0x80782000, Bytes(0x270, 0)); E.w8(0x80782000 + 0x254, st[3]);
    E.call(BUILD, { 0x80781000, 0x80782000 }, {}, 0x807FF000, 3'000'000);
    return E.rbytes(0x80782000, 0x270);
}

// ---- the mod's remap, frozen -------------------------------------------------------------------------------------------------
// The Python reads every looks value it takes from STOCK knowledge through modtables.Remap (tools\handlers\work\nmplus\
// remap.json, made by modcheck.py tables: aki\nmprobe.py runs both games' model builders on every value 0-255 of every looks
// field, about 20,000 builds). For goldust the remap is asked two things: set_app_stock's claims ("0 means none here as in
// the game": Remap.stock, which stops if not) and tame's to_stock (the game's number that builds what the mod's does).
// No Mercy Plus V3 is a fixed patch (its ROM known by fingerprint), so those answers are constants: they are frozen in
// frozen\nmplus_goldust_remap.json (part numbers only; producer: modcheck.py tables), and
// tools\romimport\native\check_goldust_remap.py proves the Python with that table makes the same looks and passes the same
// claims. A question the table doesn't hold stops the import (the handler changed: run that check again).
struct FrozenRemap {
    ojson j;
    void stock(const char* name, int v) const {        // Remap.stock("parts." + name, v): the stock meaning is the mod's
        std::string t = std::string("parts.") + name;
        const ojson& ok = j["stock_meaning_ok"];
        if (!ok.contains(t) || std::find(ok[t].begin(), ok[t].end(), ojson(v)) == ok[t].end())
            fail("goldust: the frozen remap has no word on %s %d (frozen\\nmplus_goldust_remap.json: run its check)", t.c_str(), v);
    }
    // Remap.to_stock: the stock value (std::nullopt: the mod's own -- the Python's ModTableChanged)
    std::optional<int> to_stock(const char* name, int v) const {
        std::string t = std::string("parts.") + name, k = std::to_string(v);
        const ojson& m = j["to_stock"];
        if (!m.contains(t) || !m[t].contains(k))
            fail("goldust: the frozen remap has no word on %s %d (frozen\\nmplus_goldust_remap.json: run its check)", t.c_str(), v);
        if (m[t][k].is_null()) return std::nullopt;
        return m[t][k].get<int>();
    }
};

// nmplus.py set_app(base, **kw) / set_app_stock(remap, base, why, **kw)
void set(Bytes& a, const char* name, int v) { a[field(name)] = (uint8_t)v; }
void set_stock(Bytes& a, const FrozenRemap& rm, const char* name, int v) { rm.stock(name, v); set(a, name, v); }

// nmplus.py goldust_apps: his four costumes' appearance entries in No Mercy Plus's numbers -- its Test's with the Goldust
// packs and his hair, nothing else on ("none" / "plain" as the game's numbers mean them: each a checked stock claim)
std::vector<Bytes> goldust_apps(const Bytes& base, const FrozenRemap& rm) {
    std::vector<Bytes> out;
    for (auto& rc : COSTUMES) {
        Bytes a = base;
        set(a, "ring", rc[0]); set(a, "mask", rc[1]); set(a, "boots", BOOTS); set(a, "hair", HAIR); set(a, "front_hair", FRONT_HAIR);
        for (const char* f : { "ring_c1", "ring_c2", "upper", "upper_c1", "upper_c2", "entr", "entr_c1", "entr_c2", "weapon",
                               "gloves", "gloves_c", "tattoo", "wrist", "wrist_c", "lelbow", "lelbow_c", "relbow", "relbow_c",
                               "lknee", "lknee_c", "rknee", "rknee_c", "boots_c1", "boots_c2", "facial_hair", "hat", "hat_c" })
            set_stock(a, rm, f, 0);
        // check_from_base: a field still the base's (and not 0) must be one BASE_KEEPS names
        std::string left;
        for (int i = 0; i < 38; i++) {
            bool keep = false;
            for (const char* k : BASE_KEEPS) if (!std::strcmp(k, APP_FIELDS[i])) keep = true;
            if (!keep && a[i] == base[i] && base[i] != 0) left += strf(" %s %d", APP_FIELDS[i], base[i]);
        }
        if (!left.empty()) fail("goldust: still the base wrestler's:%s -- name it in BASE_KEEPS or set it", left.c_str());
        out.push_back(a);
    }
    return out;
}

// nmplus.py tame(app, photo, remap, log): the looks the game itself reads beside the folder model -- No Mercy Plus's parts
// past the game's tables put to the game's plain ones, the rest kept through the remap (a mod-only value: 0; body / skin stop)
Bytes tame(const Bytes& app, int photo, const FrozenRemap& rm) {
    Bytes b = app;
    std::set<int> here;
    if (b[2] >= 0x80) { b[2] = 0; here.insert(2); }          // ring attire: the game's first (the model has his own)
    if (b[5] >= 0x7D) { b[5] = 0x7D; here.insert(5); }       // upper body: none
    if (b[0x1D] >= 0x76) { b[0x1D] = 0; here.insert(0x1D); }
    if (b[0x19] >= 64) { b[0x19] = 1; here.insert(0x19); }   // boots: past the game's 6 bits
    b[0x1E] = 2; b[0x20] = 0; b[0x21] = 0; b[0x22] = 0;      // hair, front hair, beard, mask: ones the game reads well
    b[0x25] = (uint8_t)photo;
    for (int i : { 0x1E, 0x20, 0x21, 0x22, 0x25 }) here.insert(i);
    for (int i = 0; i < 38; i++) {
        if (here.count(i)) continue;
        std::optional<int> s = rm.to_stock(APP_FIELDS[i], b[i]);
        if (!s) {
            if (i <= 1) fail("goldust: %s %d is No Mercy Plus's own (no stock part builds it)", APP_FIELDS[i], b[i]);
            b[i] = 0;
        } else b[i] = (uint8_t)*s;
    }
    return b;
}

// nomercy\game.py looks_of: an appearance entry -> the folder's 38 looks fields, in the unpacked struct's order (SRC2UN's
// targets ascending -- the table above is already in that order)
ojson looks_of(const Bytes& app) {
    std::vector<std::pair<int, int>> by_off;
    for (auto& p : SRC2UN) by_off.push_back({ p[1], p[0] });
    std::sort(by_off.begin(), by_off.end());
    ojson out = ojson::array();
    for (auto& [off, a] : by_off) out.push_back((int)app[a]);
    return out;
}

// ---- the photo: aki\photo.py photo_from_picture_rr ---------------------------------------------------------------------------
// One of his model's pictures (an AKI TEX picture + its RGBA5551 palette, romrender rr.texture_from_tex with no palette group)
// scaled to 64 x 64 by rr.resample's box weights, 15 colours by rr.quantise: photo.bin (the palette, colour 0 clear, then CI4)
Bytes photo_from_picture(const Bytes& tex, const Bytes& pal) {
    rr::i64 w = tex.at(0) + 1, h = tex.at(1) + 1;
    size_t n = pal.size() / 2;
    auto c8 = [](int c5) { return (c5 << 3) | (c5 >> 2); };               // (rr.c8: the RDP's 5 -> 8 bits)
    std::vector<uint8_t> px;
    for (rr::i64 i = 0; i < w * h; i++) {
        size_t k;
        if (tex[2] == 1) k = tex.at(8 + i);
        else { uint8_t b = tex.at(8 + (i >> 1)); k = (i & 1) == 0 ? (b >> 4) : (b & 15); }
        int c = k < n ? (pal[2 * k] << 8) | pal[2 * k + 1] : 0;
        px.push_back((uint8_t)c8((c >> 11) & 31)); px.push_back((uint8_t)c8((c >> 6) & 31)); px.push_back((uint8_t)c8((c >> 1) & 31));
        px.push_back(255);                                                 // (rgb_of(c) + (255,): every texel opaque)
    }
    std::vector<uint8_t> sm = rr::resample(px, w, h, 64, 64);
    auto rdiv = [](rr::i64 a, rr::i64 b) { return (2 * a + b) / (2 * b); };   // (rr.rdiv; all positive here)
    std::vector<rr::C3> cols;
    for (size_t i = 0; i < 64 * 64; i++) cols.push_back({ rdiv(sm[4 * i] * 31, 255), rdiv(sm[4 * i + 1] * 31, 255), rdiv(sm[4 * i + 2] * 31, 255) });
    std::vector<rr::C3> pal16; std::vector<rr::i64> idx;
    rr::quantise(cols, 15, pal16, idx);
    Bytes out(32 + 2048, 0);
    for (size_t i = 0; i < pal16.size() && i < 15; i++)
        put_be16(out.data() + 2 * (i + 1), (uint32_t)((pal16[i][0] << 11) | (pal16[i][1] << 6) | (pal16[i][2] << 1) | 1));
    for (size_t i = 0; i < 4096; i += 2) out[32 + i / 2] = (uint8_t)(((idx[i] + 1) << 4) | (idx[i + 1] + 1));
    return out;
}

// ---- Badd Blood's record name (baddblood\game.py plain_name, as ri_imports2.cpp has it) --------------------------------------
std::string latin1_utf8(const std::string& s) {
    std::string o;
    for (unsigned char c : s) { if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } }
    return o;
}
std::string strip(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\v\f"), b = s.find_last_not_of(" \t\r\n\v\f");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
// the select screen's markup -> (full, short, nick): {nick} only the nickname, <short> the short name, the rest the full name
void plain_name(const std::string& raw, std::string& full, std::string& shrt, std::optional<std::string>& nick) {
    std::string body;
    for (size_t i = 0; i < raw.size();) {
        size_t j;
        if (raw[i] == '{' && (j = raw.find('}', i + 1)) != std::string::npos) { if (!nick) nick = raw.substr(i + 1, j - i - 1); i = j + 1; continue; }
        body += raw[i++];
    }
    std::optional<std::string> s;
    std::string f;
    for (size_t i = 0; i < body.size(); i++) {
        size_t j;
        if (body[i] == '<' && !s && (j = body.find('>', i + 1)) != std::string::npos) s = body.substr(i + 1, j - i - 1);
        if (body[i] != '<' && body[i] != '>') f += body[i];
    }
    full = strip(f);
    shrt = s ? strip(*s) : full;
}

}  // namespace

void goldust_folder(Roms& R, const RiData& D, const fs::path& out) {
    auto nmp_rom = R.rom("nmplus_v3");
    auto bb_rom = R.rom("baddblood");
    if (!nmp_rom) fail("the No Mercy Plus V3 ROM isn't there");
    if (!bb_rom) fail("the Badd Blood ROM isn't there");
    if (!R.rom("nomercy")) fail("the No Mercy ROM isn't there");
    const RomFiles& nmp = *R.files("nmplus_v3");
    const RomFiles& nm = *R.files("nomercy");
    const RomFiles& bb = *R.files("baddblood");

    // NMPlus(): its Superstar Select RAM image (gen/mkroster.py's input), its code there its ROM's word for word (checked)
    Bytes ram = make_image("nm_pick", D.dir / "loadmaps", R);
    if (std::memcmp(ram.data() + 0x400, nmp_rom->data() + 0x1000, 0x3FC00)) fail("the RAM image isn't this No Mercy Plus");
    FrozenRemap rm{ load_ojson(D.dir / "frozen" / "nmplus_goldust_remap.json") };
    const std::string nmp_sha = sha1_hex(*nmp_rom);
    if (rm.j["mod_sha1"].get<std::string>() != nmp_sha) fail("the frozen remap is for another No Mercy Plus");

    // its roster has no Goldust (checked each run); its Test: the base his parts go on (NMPlus.find("TEST"))
    int test = 0;
    for (int o = 1; o < 100; o++) {
        auto n = nm_name(nmp, costume_struct(ram, o, 0).names);
        if (n && *n == "GOLDUST") fail("No Mercy Plus's roster has a Goldust now: the handler should read him from it");
        if (n && *n == "TEST" && !test) test = o;
    }
    if (!test) fail("No Mercy Plus has no TEST");
    Bytes base = appearance(nmp, costume_struct(ram, test, 0).appearance);
    std::vector<Bytes> apps = goldust_apps(base, rm);

    // ---- nmplus.py build: his model as No Mercy Plus's builder makes it -----------------------------------------------------
    std::set<int> ff;          // the descriptor's file places (frozen\model_file_fields.json = gen_data\model_file_fields.pkl)
    const ojson ffj = load_ojson(D.dir / "frozen" / "model_file_fields.json");
    for (auto& v : ffj["fields"]) ff.insert(v.get<int>());
    // a file of No Mercy Plus's own: one in the files' range that it has and No Mercy hasn't the same
    auto ours = [&](uint32_t v) {
        if (v < 0x800 || v >= 0x7802) return false;
        const Bytes* b = nmp.get(v);
        if (!b || b->empty()) return false;
        const Bytes* g = v <= nm.count() ? nm.get(v) : nullptr;
        return !g || *g != *b;
    };
    std::vector<Bytes> descs;
    std::vector<std::vector<int>> keeps;
    std::set<uint32_t> named;
    Bytes first_raw;           // costume 0's descriptor in the first state, before its file numbers are taken out (the photo)
    for (int c = 0; c < 4; c++) {
        Bytes un = un_of(apps[c]);
        std::vector<Bytes> ms;
        for (auto& st : STATES) ms.push_back(nm_model(ram, un, st));
        // the bytes the game's state changes (each build its own: kept as the game makes them), and the descriptor's flags
        std::set<int> keep;
        for (int k = 0; k < 0x254; k++) for (auto& m : ms) if (m[k] != ms[0][k]) keep.insert(k);
        for (int k = 0x258; k < 0x270; k++) for (auto& m : ms) if (m[k] != ms[0][k]) keep.insert(k);
        for (int k = 0x254; k < 0x258; k++) keep.insert(k);
        const Bytes& d = ms[0];
        if (c == 0) first_raw = d;
        for (int k = 0; k < 0x270; k += 2) {
            uint32_t v = be16(d, k);
            if (keep.count(k) || !ours(v)) continue;
            if (!ff.count(k)) fail("goldust costume %d: %04x at %x, not a file's place", c, v, k);
            named.insert(v);
        }
        descs.push_back(d); keeps.push_back(std::vector<int>(keep.begin(), keep.end()));
    }
    std::set<uint32_t> want;
    for (uint32_t v : named) { want.insert(v); if (ours(v + 1)) want.insert(v + 1); }      // (a palette's picture: the one after)
    std::vector<uint32_t> order(want.begin(), want.end());
    std::map<uint32_t, int> index;
    for (size_t i = 0; i < order.size(); i++) index[order[i]] = (int)i;
    std::vector<ojson> fields;
    for (int c = 0; c < 4; c++) {
        Bytes& d = descs[c];
        std::set<int> keep(keeps[c].begin(), keeps[c].end());
        ojson fl = ojson::array();
        for (int k = 0; k < 0x270; k += 2) {
            uint32_t v = be16(d, k);
            if (keep.count(k) || !index.count(v) || !ff.count(k)) continue;
            fl.push_back(ojson::array({ k, index[v] })); put_be16(d.data() + k, 0);
        }
        fields.push_back(fl);
    }

    // ---- the folder's model (imports2.py goldust: aki\model.py's format, the files in file-number order) ------------------
    fs::path d = out / "goldust";
    fs::create_directories(d / "model" / "files");
    ojson names_ = ojson::array();
    for (uint32_t v : order) {
        const Bytes& b = nmp(v);
        std::string n = strf("files/%04x_%s.bin", v, sha1_hex(b).substr(0, 12).c_str());
        write_new_file(d / "model" / n, b); names_.push_back(n);
    }
    ojson cos = ojson::array();
    for (int c = 0; c < 4; c++) {
        write_new_file(d / "model" / strf("c%d.bin", c), descs[c]);
        ojson e = ojson::object();
        e["descriptor"] = strf("c%d.bin", c); e["keep"] = keeps[c]; e["fields"] = fields[c];
        cos.push_back(e);
    }
    ojson mj = ojson::object();
    mj["format"] = 1;
    mj["about"] = "his model as No Mercy Plus V3's own builder makes it of his looks (handlers\\customs\\nmplus.py); a file named "
                  "'NNNN_...' is No Mercy Plus's file NNNN";
    mj["files"] = names_; mj["costumes"] = cos;
    write_new_file(d / "model" / "model.json", json_file_text(mj, 1));      // (json.dump(indent=1) in text mode: CRLF)

    // ---- Badd Blood's Goldust: record, moveset, theme, video ----------------------------------------------------------------
    const int K = 32;
    std::vector<WmRecord> roster = wm_roster(wm_main_ram(*bb_rom));
    const WmRecord& e = roster.at(K);
    std::string full, shrt; std::optional<std::string> nick;
    plain_name(e.raw_name, full, shrt, nick);
    if (full != "GOLDUST") fail("Badd Blood's record %d is %s, not Goldust", K, e.raw_name.c_str());
    // stock("test"): No Mercy's Test (ordinal 21) -- his looks' photo number, and the stance / weight class of his moves
    const int REF = 21;
    Bytes van = make_image("van_sel", D.dir / "loadmaps", R);
    auto tbase = nm_base_looks(van, nm, REF);
    WmPortedMoveset mv = baddblood_moveset(R, D.dir, e, REF, "goldust", GOLDUST_MOVES_ABOUT);

    // the photo: his face paint's picture (its palette named at 0x230 of costume 0's model, the picture the file after it)
    uint32_t p_ = be16(first_raw, 0x230);
    write_new_file(d / "photo.bin", photo_from_picture(nmp(p_ + 1), nmp(p_)));
    write_new_file(d / "moves.json", mv.moves_json);

    ojson looks = ojson::array();
    for (int c = 0; c < 4; c++) {
        Bytes t = tame(apps[c], tbase[c].back(), rm);
        t[field("hat_c")] = (uint8_t)c;                       // (hat colour = the costume: imports2.py plain_looks says why)
        looks.push_back(looks_of(t));
    }
    ojson w = ojson::object();
    w["about"] = "Goldust, the customs case: his looks are No Mercy Plus V3's Create-a-Wrestler Goldust parts (its SmackDown "
                 "Mall's 'Goldust 1' and 'Goldust 2': ring attire, boots, face paint) on No Mercy Plus's Test, and his model is "
                 "the one No Mercy Plus's own code builds of them. No Mercy Plus has no Goldust on its roster, so his name, size, "
                 "moveset, theme and Titantron are Badd Blood's Goldust's (a WrestleMania 2000 mod). Imported by tools\\handlers\\"
                 "imports2.py from the user's own ROMs. Edit freely.";
    w["id"] = "0x0390";                                       // (pinned, as Vader's)
    w["name"] = latin1_utf8(full.substr(0, 20)); w["short_name"] = latin1_utf8(shrt.substr(0, 10));
    w["nick"] = nick ? ojson(latin1_utf8(*nick)) : ojson(nullptr);
    w["height"] = e.height; w["weight"] = e.weight; w["based_on"] = "test"; w["looks"] = looks;
    w["moveset"] = mv.packed_hex; w["model"] = "model/model.json";
    ojson lm = ojson::object();
    lm["game"] = "No Mercy Plus V3 (data\\nmplus\\nmplus.z64 = No Mercy v1.0 + its xdelta)";
    lm["handler"] = "customs\\nmplus (imports2, 2026-11-13)";
    lm["rom_sha1"] = nmp_sha;
    lm["base"] = "its TEST, appearance " + hexs(base);
    ojson ci = ojson::array(); for (auto& a : apps) ci.push_back(hexs(a));
    lm["costumes_in_its_numbers"] = ci;
    ojson parts = ojson::object();
    parts["ring attire"] = ojson::array({ RING[0], RING[1] }); parts["mask (face paint)"] = ojson::array({ MASK[0], MASK[1] });
    parts["boots"] = BOOTS;
    lm["parts"] = parts;
    ojson its = ojson::array(); for (uint32_t v : order) its.push_back(strf("0x%04X", v));
    lm["its_files"] = its;
    ojson rv = ojson::object();
    rv["game"] = "WWF WarZone - Badd Blood (a WrestleMania 2000 mod)"; rv["rom_sha1"] = sha1_hex(*bb_rom);
    rv["roster_index"] = K; rv["record_name"] = latin1_utf8(e.raw_name); rv["game_id"] = strf("0x%04X", e.id);
    rv["moves_file"] = strf("0x%X", e.moves); rv["abilities_file"] = strf("0x%X", e.abilities);
    rv["why"] = "No Mercy Plus V3 has no Goldust Superstar: no record, moves, theme or video";
    ojson s = ojson::object();
    s["looks_and_model"] = lm; s["record_moves_theme_video"] = rv;
    s["photo"] = strf("his face paint's picture (No Mercy Plus file 0x%04X), a stand-in made by tools\\romrender", p_ + 1);
    s["moveset_fallbacks"] = mv.fell;
    // imports2.py write_theme_video: his music slot's sound and his video, from Badd Blood's select screen RAM
    Bytes sel = make_image("bb_vsel", D.dir / "loadmaps", R);
    FolderTheme t = wm_theme_folder(*bb_rom, sel, e.music);
    write_new_file(d / "theme.bin", t.bin); write_new_file(d / "theme.meta", t.meta);
    FolderVideo vd = wm_video(bb, sel, e.video);
    write_new_file(d / "video.bin", vd.bin);
    s["theme"] = t.about; s["video"] = vd.about;
    w["source"] = s;
    write_new_file(d / "wrestler.json", py_dumps(w, PyDump{ 1 }));
}

}  // namespace ri
