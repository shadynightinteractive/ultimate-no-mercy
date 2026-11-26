// ri_wmimport.cpp (phase3 p3folders, 2026-11-19): WrestleMania 2000's roster as wrestler folders -- see ri_wmimport.h.
#include "ri_wmimport.h"
#include "ri_json.h"
#include "ri_images.h"
#include "ri_wmmedia.h"
#include "ri_wmmoves.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ri {

static constexpr uint32_t SCALE0 = 0x8005B138, SCALE_STRIDE = 0xC0;   // (wm2k_import.py: the whole-model scale per match slot)
static constexpr uint32_t HAND_KINDS = 0x80041CC4;                    // (wm2000\game.py: a byte per looks build: the hands' kind)
static constexpr int SKEL_WM_HANDS = 0x262, NM_PAL_SIZE = 0x200, LOOKS_BUILD = 0, BUILD_WOMAN = 7;

static ojson load_json(const fs::path& p) { Bytes b = read_file(p); return ojson::parse(std::string(b.begin(), b.end())); }

// ---- the rig's captures ---------------------------------------------------------------------------------------------------
WmCaps wm_rig_caps(Roms& R, const RiData& D) {
    WmCaps C;
    ojson idx = load_json(D.dir / "frozen" / "wm2000_rig_index.json");      // ("k:c" -> [file, slot]: the plan of the loads)
    std::map<std::string, std::vector<std::pair<int, std::pair<int, int>>>> by_file;
    for (auto it = idx.begin(); it != idx.end(); ++it) {
        std::string key = it.key(); size_t colon = key.find(':');
        int k = std::stoi(key.substr(0, colon)), c = std::stoi(key.substr(colon + 1));
        std::string fn = it.value()[0].get<std::string>(); int slot = it.value()[1].get<int>();
        C.index[{ k, c }] = { fn, slot };
        by_file[fn].push_back({ slot, { k, c } });
    }
    Bytes kinds = wm_main_ram(*R.rom("wm2000"));
    kinds = Bytes(kinds.begin() + (HAND_KINDS & 0x7FFFFF), kinds.begin() + (HAND_KINDS & 0x7FFFFF) + 0x40);
    const std::string pre = "handlers_rig/wm2000/cap/";
    wm_models(R, D, "rig", [&](const std::string& rel, const Bytes& ram) {
        if (rel.compare(0, pre.size(), pre) != 0) return;            // (the wmport loads come first: not these)
        auto f = by_file.find(rel.substr(pre.size()));
        if (f == by_file.end()) return;
        // (build_model_caps: the match's hands' kinds table must be the ROM's)
        if (std::memcmp(ram.data() + (HAND_KINDS & 0x7FFFFF), kinds.data(), 0x40)) fail("%s: the match's hands' kinds table isn't the ROM's", rel.c_str());
        for (auto& [slot, kc] : f->second) {
            // scale_of: x, y, z at + 0, 0x14, 0x28 of his slot's entry -- one uniform scale
            float s[3];
            for (int i = 0; i < 3; i++) {
                uint32_t v = be32(ram, (SCALE0 + SCALE_STRIDE * slot + 0x14 * i) & 0x7FFFFF);
                std::memcpy(&s[i], &v, 4);
            }
            double mx = std::max({ (double)s[0], (double)s[1], (double)s[2] }), mn = std::min({ (double)s[0], (double)s[1], (double)s[2] });
            if (!(mx - mn < 1e-6 && 0.9 < s[0] && s[0] < 1.2)) fail("slot %d: no uniform scale at 0x%X", slot, SCALE0);
            C.by[kc] = { wm_capture(ram, slot), (double)s[0] };
        }
    });
    return C;
}

// ---- the model (wm2000\game.py build_model_caps) --------------------------------------------------------------------------
// head_tone: his face's skin colour (parts 4 and 5), weighted by how much of the picture each colour covers
static std::optional<std::array<double, 3>> head_tone(const Bytes& d, const Pool& pool) {
    double acc[3] = { 0, 0, 0 }; long n = 0;
    for (int k : { 4, 5 }) {
        uint32_t t = be16(d, 8 * k + 2), q = be16(d, 8 * k + 4);
        if (!(t && pool.ours(t) && pool.ours(q))) continue;
        const Bytes& pal = pool.get(q);
        std::vector<std::array<int, 3>> cs;
        for (size_t i = 0; i + 1 < pal.size(); i += 2) cs.push_back(rgb5551(be16(pal, i)));
        for (int i : tex_pixels(pool.get(t)))
            if (i < (int)cs.size() && wm_skin(cs[i][0], cs[i][1], cs[i][2])) { for (int j = 0; j < 3; j++) acc[j] += cs[i][j]; n++; }
    }
    if (!n) return std::nullopt;
    return std::array<double, 3>{ acc[0] / n, acc[1] / n, acc[2] / n };
}

WmModel wm_like_model(const RomFiles& wmf, const RomFiles& nm, const std::vector<const WmCapture*>& caps,
                      const std::vector<const uint8_t*>& kinds, const WmRecord& e, const std::string& sid, bool wm_rules) {
    WmModel M;
    for (int c = 0; c < 4; c++) {
        const WmCapture& cap = *caps[c];
        Bytes nmd = nm_descriptor(cap.desc);
        for (int o = 8; o < 0xB0; o += 8) std::memset(&nmd[o], 0, 6);
        for (int o = 0xC8; o < 0xE0; o += 8) std::memset(&nmd[o], 0, 6);
        for (auto& p : cap.parts) {
            int o = p.k <= 20 ? p.at : 0xC8 + 8 * (p.k - 22);
            uint32_t m = M.pool.one(wmf(p.mesh)), t = 0, q = 0;
            if (p.tex && !p.tex->empty()) {
                if (!p.pal) fail("%s costume %d part %d: a picture with no palette", sid.c_str(), c, p.k);
                std::tie(t, q) = M.pool.pair(*p.tex, *p.pal);
            }
            put_be16(&nmd[o], m); put_be16(&nmd[o + 2], t); put_be16(&nmd[o + 4], q);
        }
        nmd[SKEL_WM_HANDS] = kinds[c][e.looks[c][0]];
        M.descs.push_back(nmd);
    }
    // skinned(): nm_hands on each costume; one with no bare skin waits for the others' tone (and with none anywhere, his face's)
    Skins skins;
    std::vector<Bytes> out(4);
    std::vector<int> later;
    // (the Badd Blood handler -- wm_rules false -- has neither: a man's hands always, and a costume with no skin to go by fails)
    auto kit = [&](int c) -> const HandKit& { return wm_rules && e.looks[c][LOOKS_BUILD] == BUILD_WOMAN ? woman_kit() : man_kit(); };
    for (int c = 0; c < 4; c++) {
        try { out[c] = nm_hands(M.descs[c], M.pool, nm, skins, sid, kit(c)); }
        catch (const NoSkin&) { if (!wm_rules) throw; later.push_back(c); }
    }
    if (!later.empty() && !skins.tone.count(sid)) {
        std::optional<std::array<double, 3>> tone;
        for (int c = 0; c < 4 && !tone; c++) tone = head_tone(M.descs[c], M.pool);
        if (!tone) fail("%s: no bare skin in any costume, nor in his face: say his tone in his fixes", sid.c_str());
        skins.tone[sid] = *tone;
    }
    for (int c : later) out[c] = nm_hands(M.descs[c], M.pool, nm, skins, sid, kit(c));
    M.descs = fit_wrists(out, M.pool, nm);
    // the palettes the descriptors name, padded to No Mercy's palette size
    std::set<uint32_t> pals;
    for (auto& d : M.descs)
        for (int kk = 1; kk < 28; kk++) { uint32_t q = be16(d, part_offset(kk) + 4); if (M.pool.ours(q)) pals.insert(q); }
    for (uint32_t q : pals) { Bytes& b = M.pool.data[q - POOL0]; if (b.size() < (size_t)NM_PAL_SIZE) b.resize(NM_PAL_SIZE, 0); }
    return M;
}

WmModel wm_build_model(Roms& R, const WmCaps& caps, const WmRecord& e, const std::string& sid) {
    Bytes main = wm_main_ram(*R.rom("wm2000"));
    const uint8_t* kinds = main.data() + (HAND_KINDS & 0x7FFFFF);      // (the ROM's table: each capture was checked to have it)
    std::vector<const WmCapture*> cc;
    double scales[4];
    for (int c = 0; c < 4; c++) {
        auto it = caps.by.find({ e.k, c });
        if (it == caps.by.end()) fail("%s: no capture of costume %d", sid.c_str(), c);
        cc.push_back(&it->second.cap); scales[c] = it->second.scale;
    }
    WmModel M = wm_like_model(*R.files("wm2000"), *R.files("nomercy"), cc, { kinds, kinds, kinds, kinds }, e, sid, true);
    std::memcpy(M.scales, scales, sizeof scales);
    return M;
}

// ---- his looks (aki_import.py base_looks, imports2.py plain_looks; nomercy\game.py's looks order) -------------------------------
// The appearance entry's 38 bytes (No Mercy's file 1, 0x26 bytes a costume) and the folder's 38 "looks" fields are the same
// values in another order: nomercy\game.py APP_TO_LOOKS (appearance byte -> unpacked looks offset); the folder lists them by
// looks offset (LOOK_OFFSETS, ascending).
static const int APP_TO_LOOKS[38][2] = {
    { 0x00, 0x00 }, { 0x01, 0x01 }, { 0x02, 0x03 }, { 0x03, 0x04 }, { 0x04, 0x05 }, { 0x05, 0x06 }, { 0x06, 0x07 }, { 0x07, 0x08 },
    { 0x08, 0x09 }, { 0x09, 0x0A }, { 0x0A, 0x0B }, { 0x1C, 0x0C }, { 0x1D, 0x0F }, { 0x1E, 0x12 }, { 0x1F, 0x13 }, { 0x20, 0x15 },
    { 0x21, 0x18 }, { 0x22, 0x1B }, { 0x0C, 0x1E }, { 0x0D, 0x1F }, { 0x11, 0x21 }, { 0x12, 0x22 }, { 0x13, 0x24 }, { 0x14, 0x25 },
    { 0x0F, 0x27 }, { 0x10, 0x28 }, { 0x15, 0x2A }, { 0x16, 0x2B }, { 0x17, 0x2D }, { 0x18, 0x2E }, { 0x19, 0x30 }, { 0x1A, 0x31 },
    { 0x1B, 0x32 }, { 0x0E, 0x33 }, { 0x23, 0x36 }, { 0x24, 0x37 }, { 0x0B, 0x39 }, { 0x25, 0x3C } };
static std::vector<int> looks_of_app(const uint8_t* app, bool remap) {
    std::vector<std::pair<int, int>> by_off;               // (looks offset, appearance byte), sorted by offset
    for (auto& p : APP_TO_LOOKS) by_off.push_back({ p[1], p[0] });
    std::sort(by_off.begin(), by_off.end());
    std::vector<int> out;
    for (auto& [off, a] : by_off) {
        int v = app[a];
        if (remap) {   // (mkparts.py costume_src: the extended parts' numbers taken back to the game's -- its NEW maps inverted)
            if (off == 3) { if (v == 131) v = 129; else if (v == 143) v = 130; else if (v == 151) v = 131; }
            else if (off == 6) { if (v == 107) v = 0x80; }
            else if (off == 0xF) { if (v == 82) v = 0x80; else if (v == 83) v = 0x81; else if (v == 132) v = 0x82; }
        }
        out.push_back(v);
    }
    return out;
}
// base_looks(ordinal): the game's own Superstar of this ordinal, his four costumes' looks (mkparts.py vapp + costume_src)
static std::vector<std::vector<int>> base_looks(const Bytes& van, const RomFiles& nm, int ord) {
    std::vector<std::vector<int>> out;
    const Bytes& app = nm(1);
    for (int c = 0; c < 4; c++) {
        uint32_t a = be16(van, (be32(van, (0x80047124 + 16 * ord + 4 * c) & 0x7FFFFF) + 0xE) & 0x7FFFFF);
        out.push_back(looks_of_app(app.data() + a * 0x26, true));
    }
    return out;
}
// plain_looks(body, photo, c): imports2.py's plain costume -- the ported men's base (mkparts.py JJ_BASE, our numbers), every
// part its plainest, hat colour = the costume
static std::vector<int> plain_looks(int body, int photo, int c) {
    static const uint8_t JJ_BASE[38] = { 0x02, 0x03, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x08, 0x00, 0x1c, 0x00, 0x00, 0x01, 0x09,
                                         0x09, 0x02, 0x09, 0x00, 0x00, 0x00, 0x00, 0x09 };
    uint8_t a[38]; std::memcpy(a, JJ_BASE, 38);
    // (customs\nmplus.py APP_FIELDS: body 0, ring 2-4, upper 5-7, wrist 15-16, lknee 21-22, rknee 23-24, boots 25-27, face 29,
    //  hair 30, front_hair 32, facial_hair 33, mask 34, hat 35, hat_c 36, photo 37)
    a[0] = (uint8_t)body; a[2] = 111; a[3] = a[4] = 0; a[5] = a[6] = a[7] = 0; a[15] = a[16] = 0; a[21] = a[22] = 0; a[23] = a[24] = 0;
    a[25] = 1; a[26] = a[27] = 0; a[29] = 0; a[30] = 2; a[32] = 0; a[33] = 0; a[34] = 0; a[35] = 0; a[36] = (uint8_t)c; a[37] = (uint8_t)photo;
    return looks_of_app(a, false);
}

// ---- revenge\body.py's size rules ---------------------------------------------------------------------------------------------
static const int NM_BELT[] = { 0, 0, 1, 1, 2, 2, 3, 4, 1, 5, 1, 0, 2, 6, 6, 7, 7, 8, 9, 9, 9, 10, 1, 1 };   // 0x80048A48[body type]
static const int NM_HAND_CLASS[] = { 0, 1, 1, 2, 3, 2, 4, 4, 4, 4, 4 };                                     // 0x80047FE8[belt]
static double scale_of_size(int size) { return py_round(0.97 + size * 0.00047, 4); }
static double nm_scale(int height) { int h = std::max(0, std::min(height, 35)); return py_round(0.97 + (h * 255 / 36) * 0.00047, 4); }
static int nm_size_for_scale(double s) { return (int)std::max<long long>(0, std::min<long long>(255, py_round_int((s - 0.97) / 0.00047))); }

static std::string latin1_utf8(const std::string& s) {
    std::string o;
    for (unsigned char c : s) { if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } }
    return o;
}
static std::string hexs(const Bytes& b) { std::string s; for (uint8_t x : b) s += strf("%02x", x); return s; }

// one()'s wrestler.json (before the theme, video and moveset parts: wm_wrestler_finish)
static ojson wm_wrestler_json(Roms& R, const Bytes& van, const std::map<int, std::string>& id_names, const std::string& rom_sha1,
                              const WmCaps& caps, const ojson& fx, const WmRecord& e, const WmModel& M) {
    const RomFiles& nm = *R.files("nomercy");
    std::string sid = fx["string_id"].get<std::string>();
    std::string full, shrt;
    wm_names(e.raw_name, full, shrt);
    std::string name = fx.contains("name") ? fx["name"].get<std::string>() : latin1_utf8(full);   // (the fixes' text is UTF-8 already)
    int size = nm_size_for_scale(M.scales[0]);
    // body_type(e, c): his looks' byte 8 (byte 0 = 7: the women's build -> No Mercy's women's type 15)
    static const char* TYPE_NAMES[] = { "Skinny 1", "Skinny 2", "Medium 1", "Medium 2", "Thick 1", "Thick 2", "Fat 1", "Fat 2" };
    int bodies[4]; std::string body_why;
    for (int c = 0; c < 4; c++) {
        const Bytes& lk = e.looks[c];
        std::string why;
        if (lk[0] == BUILD_WOMAN) { bodies[c] = 15; why = "WrestleMania 2000's women's build (looks byte 0 = 7): No Mercy's women's type"; }
        else {
            bodies[c] = lk[8];
            why = strf("WrestleMania 2000's body type (looks byte 8 = %d: No Mercy's %s)", lk[8], lk[8] < 8 ? TYPE_NAMES[lk[8]] : "?");
        }
        if (c == 0) body_why = why;
    }
    int body = bodies[0];
    int ref = fx["moveset"]["reference_ordinal"].get<int>();
    uint32_t ref_id = be16(van, be32(van, (0x80047124 + 16 * ref) & 0x7FFFFF) & 0x7FFFFF);
    auto base = base_looks(van, nm, ref);
    ojson looks = ojson::array();
    std::string looks_from;
    if (body == 15) {
        // (a woman's type needs a woman's parts in the looks: No Mercy's ordinal 51's four costumes, their photo field his base's)
        auto wl = base_looks(van, nm, 51);
        for (int c = 0; c < 4; c++) { std::vector<int> l = wl[c]; l[0] = bodies[c]; l.back() = base[c].back(); looks.push_back(l); }
        looks_from = "No Mercy's ordinal 51's four costumes (a woman's parts for a woman's type), their photo field his base's";
    } else {
        for (int c = 0; c < 4; c++) looks.push_back(plain_looks(bodies[c], base[c].back(), c));
        looks_from = "a plain costume (imports2.py plain_looks: the ported men's base, every part its plainest, hat colour = costume)";
    }
    ojson w = ojson::object();
    w["about"] = name + " from WWF WrestleMania 2000, imported by tools\\handlers\\wm2k_import.py from the user's own ROM: his model as "
                 "WrestleMania 2000 builds it in a match (captured in the rig), his name, size, body type, moveset, theme and "
                 "Titantron from the ROM. Edit freely: run the handler again to start over.";
    w["id"] = strf("0x%04X", 0x03A0 + e.k);
    w["name"] = name.substr(0, 20); w["short_name"] = latin1_utf8(shrt.substr(0, 10)); w["nick"] = nullptr;
    w["height"] = e.height; w["weight"] = e.weight;
    auto nmit = id_names.find((int)ref_id);
    w["based_on"] = nmit != id_names.end() ? nmit->second : strf("0x%04X", ref_id);
    w["size"] = size; w["looks"] = looks;
    w["moveset"] = nullptr;                                   // (the packed moveset: filled by wm_wrestler_finish)
    w["model"] = "model/model.json";
    ojson s = ojson::object();
    s["game"] = "WWF WrestleMania 2000"; s["handler"] = "wm2000 (wm2kall, 2026-11-13)"; s["rom_sha1"] = rom_sha1;
    s["roster_index"] = e.k; s["game_id"] = strf("0x%04X", e.id); s["record_name"] = latin1_utf8(e.raw_name);
    s["fixes"] = "handlers\\wm2000\\wrestlers\\" + sid + ".json";
    s["moves_file"] = strf("0x%X", e.moves); s["abilities_file"] = strf("0x%X", e.abilities);
    ojson cl = ojson::array(); for (int c = 0; c < 4; c++) cl.push_back(hexs(e.looks[c]));
    s["costume_looks"] = cl;
    ojson bt = ojson::object();
    bt["nm_type"] = body; bt["why"] = body_why; bt["per_costume"] = ojson::array({ bodies[0], bodies[1], bodies[2], bodies[3] });
    bt["nm_belt"] = NM_BELT[body]; bt["nm_hand_class"] = NM_HAND_CLASS[NM_BELT[body]];
    s["body_type"] = bt;
    s["looks_from"] = looks_from;
    ojson sz = ojson::object();
    ojson sc = ojson::array(); for (int c = 0; c < 4; c++) sc.push_back(py_round(M.scales[c], 5));
    sz["wm2000_scale"] = sc; sz["nm_size"] = size; sz["nm_scale"] = scale_of_size(size); sz["nm_height_scale"] = nm_scale(e.height);
    sz["why"] = "WrestleMania 2000's own whole-model scale of him in a match (x = y = z, his slot's), as No Mercy's size code nearest "
                "it (0.97 + 0.00047 x size)";
    s["size"] = sz;
    ojson rf = ojson::object();
    rf["ordinal"] = ref; rf["why"] = fx["moveset"].contains("reference_why") ? fx["moveset"]["reference_why"] : ojson("");
    s["reference"] = rf;
    std::string mdl = "rig\\wm_capture_all.py captures, its Tag Match with the costume forced (";
    for (int c = 0; c < 4; c++) {
        auto& ix = caps.index.at({ e.k, c });
        mdl += strf("%sc%d: %s slot %d", c ? ", " : "", c, ix.first.c_str(), ix.second);
    }
    s["model"] = mdl + ")";
    s["photo"] = PHOTO_HOW;
    s["moveset_fallbacks"] = nullptr;                         // (filled by wm_wrestler_finish)
    if (fx.contains("name_why")) { ojson nf = ojson::object(); nf["record"] = latin1_utf8(full); nf["why"] = fx["name_why"]; s["name_fix"] = nf; }
    // (womenfix 2026-11-15: WrestleMania 2000's 600 placeholder weight -> No Mercy's codes: a woman's 500, a non-wrestler's 501)
    if (e.weight >= 500 && !(e.weight >= 500 && e.weight <= 503)) {
        int nw = body == 15 ? 500 : 501;
        w["weight"] = nw;
        s["weight_from"] = strf("WrestleMania 2000's record weighs him %d, its placeholder for those it gives no weight (No Mercy would "
                                "read it as 599 lbs, a super heavyweight): No Mercy's own %d, its %s's code -- the select screen's "
                                "text for one and the engine's weight class at %d lbs (handlers\\revenge\\body.py; 2026-11-15)",
                                e.weight, nw, body == 15 ? "woman" : "non-wrestler", nw == 500 ? 220 : 240);
    }
    w["source"] = s;
    return w;
}

std::vector<std::vector<int>> nm_base_looks(const Bytes& van, const RomFiles& nm, int ord) { return base_looks(van, nm, ord); }
std::vector<int> nm_plain_looks(int body, int photo, int c) { return plain_looks(body, photo, c); }

// ---- the collection -------------------------------------------------------------------------------------------------------
void wm2000_folders(Roms& R, const WmImportData& D, const fs::path& out, const std::vector<std::string>& only) {
    if (!R.rom("wm2000")) fail("the WrestleMania 2000 ROM isn't there");
    if (!R.rom("nomercy")) fail("the No Mercy ROM isn't there");
    // the fixes, sorted by roster index (wm2k_import.py main); the two built-in ports (legacy_game_id) left out
    std::vector<std::pair<int, std::pair<std::string, ojson>>> men;
    for (auto& f : fs::directory_iterator(D.fixes_dir)) {
        if (f.path().extension() != ".json") continue;
        ojson fx = load_json(f.path());
        if (fx.contains("legacy_game_id")) continue;
        std::string sid = fx["string_id"].get<std::string>();
        if (!only.empty() && std::find(only.begin(), only.end(), sid) == only.end()) continue;
        men.push_back({ fx["wm2000"]["roster_index"].get<int>(), { sid, fx } });
    }
    std::sort(men.begin(), men.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<WmRecord> roster = wm_roster(wm_main_ram(*R.rom("wm2000")));
    WmCaps caps = wm_rig_caps(R, D.ri);
    const RomFiles& nm = *R.files("nomercy");
    // No Mercy's Superstars as its Superstar Select has them (mkroster.py's EV: data\gen_data\van_sel_ram.bin) and the
    // string IDs of its game numbers (mod\gen\roster_ids.json, ours): whom a man is based on
    Bytes van = make_image("van_sel", D.ri.dir / "loadmaps", R);
    std::map<int, std::string> id_names;
    {
        ojson ids = load_json(D.roster_ids);
        for (auto it = ids.begin(); it != ids.end(); ++it) id_names[it.value().get<int>()] = it.key();   // ({v: k}: the last wins)
    }
    std::string rom_sha1 = sha1_hex(*R.rom("wm2000"));
    // the move library from all 58 movesets, once a run (wm2k_import.py main: wmlib.Library(wm)); the select screen's RAM
    // (its music bank and entrance table: the themes and Titantrons)
    WmMoveLib lib(R, D.ri.dir, D.fixes_dir);
    Bytes sel = make_image("wm_sel1", D.ri.dir / "loadmaps", R);
    for (auto& [k, sf] : men) {
        auto& [sid, fx] = sf;
        const WmRecord& e = roster.at(k);
        fs::path d = out / sid;
        fs::create_directories(d);
        WmModel M = wm_build_model(R, caps, e, sid);
        Bytes photo = photo_from_pool(M.descs[0], M.pool, nm);
        write_new_file(d / "photo.bin", photo);
        model_write(d, M.descs, M.pool);
        ojson w = wm_wrestler_json(R, van, id_names, rom_sha1, caps, fx, e, M);
        ojson& s = w["source"];
        // his moveset (wm2k_import.py one() + wm2k_refresh.py: ri_wmmoves)
        WmMoves mv = lib.man(sid, e, fx["moveset"]["reference_ordinal"].get<int>());
        w["moveset"] = mv.packed_hex; s["moveset_fallbacks"] = mv.fell;
        write_new_file(d / "moves.json", mv.moves_json);
        // his theme and Titantron (write_theme_video: ri_wmmedia), after the notes one() wrote
        if (wm_theme_sound(sel, e.music) < 0) s["theme"] = wm_theme_none(e.music);
        else {
            FolderTheme t = wm_theme_folder(*R.rom("wm2000"), sel, e.music);
            write_new_file(d / "theme.bin", t.bin); write_new_file(d / "theme.meta", t.meta);
            s["theme"] = t.about;
        }
        if (e.video) {
            FolderVideo v = wm_video(*R.files("wm2000"), sel, e.video);
            write_new_file(d / "video.bin", v.bin);
            s["video"] = v.about;
        } else s["video"] = wm_video_none();
        // wm2k_refresh.py's notes (added at the end of "source"), then "weight_from" last (where the installed folders have it)
        if (mv.stance_note) s["stance"] = *mv.stance_note;
        if (mv.win_pose_note) s["win_pose"] = *mv.win_pose_note;
        if (s.contains("weight_from")) { ojson wf = s["weight_from"]; s.erase("weight_from"); s["weight_from"] = wf; }
        // (rostertags, 2026-11-21: what he is -- last; game.py wrestler_class: his fixes' "class", else his record's group / code)
        if (fx.contains("class")) { w["class"] = fx["class"]; w["class_from"] = fx["class_why"]; }
        else {
            const char* c = (e.id >> 8) == 0x0A ? "woman" : (e.id >> 8) == 0x09 ? "official" : e.height == 37 ? "manager" : "wrestler";
            w["class"] = c;
            w["class_from"] = c[0] == 'w' && c[1] == 'o' ? "WrestleMania 2000's own women's records (67-74, its IDs 0x0Axx)" : c[0] == 'o' ? "WrestleMania 2000's own group 9 (its IDs 0x09xx): its officials and announcers, who don't wrestle" : c[0] == 'm' ? "WrestleMania 2000's own non-wrestler's height code (37) on his record, and no weight (600): one who doesn't wrestle" : "WrestleMania 2000's own wrestlers' records (a height and weight of his own)";
        }
        write_new_file(d / "wrestler.json", py_dumps(w, PyDump{ 1 }));      // (json.dump(indent=1), "\n" line ends: aki\folder.py)
        printf("%s\n", sid.c_str());
    }
}

}  // namespace ri
