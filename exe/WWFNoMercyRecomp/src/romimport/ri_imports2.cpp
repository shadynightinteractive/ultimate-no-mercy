// ri_imports2.cpp (phase3 p3folders, 2026-11-19): the "Imported" collection -- see ri_imports2.h.
#include "ri_imports2.h"
#include "ri_images.h"
#include "ri_json.h"
#include "ri_wmmedia.h"
#include "ri_wmmoves.h"

namespace ri {

static std::string latin1_utf8(const std::string& s) {
    std::string o;
    for (unsigned char c : s) { if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } }
    return o;
}
static std::string strip(const std::string& s) {      // (str.strip() on these ASCII names)
    size_t a = s.find_first_not_of(" \t\r\n\v\f"), b = s.find_last_not_of(" \t\r\n\v\f");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// baddblood\game.py plain_name: the select screen's markup -> (full, short, nick): {nick} only the nickname, <short> the
// short name (the first), the rest the full name
static void plain_name(const std::string& raw, std::string& full, std::string& shrt, std::optional<std::string>& nick) {
    std::string body;
    for (size_t i = 0; i < raw.size();) {
        size_t j;
        if (raw[i] == '{' && (j = raw.find('}', i + 1)) != std::string::npos) { if (!nick) nick = raw.substr(i + 1, j - i - 1); i = j + 1; continue; }
        body += raw[i++];
    }
    std::optional<std::string> s;
    std::string f;
    for (size_t i = 0; i < body.size();) {
        size_t j;
        if (body[i] == '<' && !s && (j = body.find('>', i + 1)) != std::string::npos) s = body.substr(i + 1, j - i - 1);
        if (body[i] != '<' && body[i] != '>') f += body[i];
        i++;
    }
    full = strip(f);
    shrt = s ? strip(*s) : full;
}

static std::string hexs(const Bytes& b) { std::string s; for (uint8_t x : b) s += strf("%02x", x); return s; }

void vader_folder(Roms& R, const RiData& D, const fs::path& out, const VaderMoveset& moveset) {
    auto rom = R.rom("baddblood");
    if (!rom) fail("the Badd Blood ROM isn't there");
    const RomFiles& bb = *R.files("baddblood");
    const RomFiles& nm = *R.files("nomercy");
    const int K = 19;                                   // (his roster index: imports2.py vader, R[19], checked by his name)
    std::vector<WmRecord> roster = wm_roster(wm_main_ram(*rom));
    const WmRecord& e = roster.at(K);
    std::string full, shrt; std::optional<std::string> nick;
    plain_name(e.raw_name, full, shrt, nick);
    if (full != "VADER") fail("Badd Blood's record %d is %s, not Vader", K, e.raw_name.c_str());
    // his four costumes: Badd Blood's own loads (ri_models "baddblood": him in slot 0), each with its match's hands' kinds
    std::vector<Bytes> rams(4);
    wm_models(R, D, "baddblood", [&](const std::string& rel, const Bytes& ram) {
        for (int c = 0; c < 4; c++) if (rel == strf("handlers_rig/imports2/cap/vader_c%d_ram.bin", c)) rams[c] = ram;
    });
    std::vector<WmCapture> caps;
    for (int c = 0; c < 4; c++) caps.push_back(wm_capture(rams[c], 0));
    std::vector<const WmCapture*> cc;
    std::vector<const uint8_t*> kinds;
    for (int c = 0; c < 4; c++) { cc.push_back(&caps[c]); kinds.push_back(rams[c].data() + 0x41CC4); }
    WmModel M = wm_like_model(bb, nm, cc, kinds, e, "vader", false);
    // stock("viscera"): No Mercy's biggest man, his looks' body type and photo (No Mercy's ordinal 45, the stock folder's
    // "source"."ordinal") -- the stance and weight class his moves are made with
    const int REF = 45;
    Bytes van = make_image("van_sel", D.dir / "loadmaps", R);
    auto base = nm_base_looks(van, nm, REF);
    int body[4];
    ojson looks = ojson::array();
    for (int c = 0; c < 4; c++) { body[c] = base[c][0]; looks.push_back(nm_plain_looks(body[c], base[c].back(), c)); }
    std::optional<VaderMoves> mv;
    if (moveset) mv = moveset(R, e, REF);
    else {      // (baddblood\game.py moveset + pack_moveset natively: ri_wmmoves)
        WmPortedMoveset p = baddblood_moveset(R, D.dir, e, REF, "vader", VADER_MOVES_ABOUT);
        mv = VaderMoves{ p.moves_json, p.packed_hex, p.fell };
    }
    fs::path d = out / "vader";
    fs::create_directories(d);
    write_new_file(d / "photo.bin", photo_from_pool(M.descs[0], M.pool, nm));
    model_write(d, M.descs, M.pool);
    if (mv) write_new_file(d / "moves.json", mv->moves_json);
    ojson w = ojson::object();
    w["about"] = "Vader from WWF WarZone - Badd Blood (a WrestleMania 2000 mod), imported by tools\\handlers\\imports2.py from the "
                 "user's own ROM: his model as Badd Blood builds it in a match (captured in the rig), his name, size, moveset, "
                 "theme and Titantron from the ROM. Edit freely: run the handler again to start over.";
    w["id"] = "0x0391";
    w["name"] = latin1_utf8(full.substr(0, 20)); w["short_name"] = latin1_utf8(shrt.substr(0, 10));
    w["nick"] = nick ? ojson(latin1_utf8(*nick)) : ojson(nullptr);
    w["height"] = e.height; w["weight"] = e.weight; w["based_on"] = "viscera"; w["looks"] = looks;
    w["moveset"] = mv ? ojson(mv->packed_hex) : ojson(nullptr);
    w["model"] = "model/model.json";
    ojson s = ojson::object();
    s["game"] = "WWF WarZone - Badd Blood (a WrestleMania 2000 mod)"; s["handler"] = "baddblood (imports2, 2026-11-13)";
    s["rom_sha1"] = sha1_hex(*rom); s["roster_index"] = K; s["game_id"] = strf("0x%04X", e.id);
    s["record_name"] = latin1_utf8(e.raw_name);
    s["moves_file"] = strf("0x%X", e.moves); s["abilities_file"] = strf("0x%X", e.abilities);
    ojson cl = ojson::array(); for (int c = 0; c < 4; c++) cl.push_back(hexs(e.looks[c]));
    s["costume_looks"] = cl;
    s["body_type"] = strf("Viscera's ([%d, %d, %d, %d]): with his own model it picks only his belt and his hands' class",
                          body[0], body[1], body[2], body[3]);
    s["model"] = "rig\\capture.py: a match, slot 0, each costume (cap\\vader_c<k>_ram.bin)";
    s["photo"] = PHOTO_HOW;
    s["moveset_fallbacks"] = mv ? ojson(mv->fell) : ojson(nullptr);
    // imports2.py write_theme_video: his music slot's sound and his video, from Badd Blood's select screen RAM
    Bytes sel = make_image("bb_vsel", D.dir / "loadmaps", R);
    FolderTheme t = wm_theme_folder(*rom, sel, e.music);
    write_new_file(d / "theme.bin", t.bin); write_new_file(d / "theme.meta", t.meta);
    FolderVideo v = wm_video(bb, sel, e.video);
    write_new_file(d / "video.bin", v.bin);
    s["theme"] = t.about; s["video"] = v.about;
    w["source"] = s;
    write_new_file(d / "wrestler.json", py_dumps(w, PyDump{ 1 }));
}

}  // namespace ri
