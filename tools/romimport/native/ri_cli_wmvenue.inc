// ri_cli_wmvenue.inc (phase3 p3wmvenues, 2026-11-19): ri_cli wmvenue -- WrestleMania 2000's venues as arcade arenas
// (ri_wmvenues: mod\gen\arenas_wm.py's data side), dumped as text for tools\romimport\native\wmvenue_check.py.
//   ri_cli wmvenue <roms> <ri data> <v | all> <out folder>
// <ri data> = tools\romimport (its loadmaps\ for the RAM images the scene / venue tables are read from, its frozen\ for the
// painter's drawing orders). Writes <out>\<v>.txt: names, the hex of every mesh and picture, the tron rows' numbers --
// the same lines wmvenue_check.py dump() writes from the Python.
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_wmvenues.h"

static std::string wmvenue_text(const WmVenue& V) {
    auto hex = [](const Bytes& b) { static const char* d = "0123456789abcdef"; std::string s; s.reserve(b.size() * 2);
                                    for (uint8_t c : b) { s += d[c >> 4]; s += d[c & 15]; } return s; };
    std::string o;
    auto line = [&](const std::string& s) { o += s; o += "\n"; };
    auto pic = [&](const WmPic& p) { return hex(p.tex) + " " + hex(p.pal); };
    auto piece = [&](const char* sec, size_t i, const WmPiece& p) {
        line(strf("%s %zu %s ", sec, i, p.name.empty() ? "-" : p.name.c_str()) + hex(p.mesh) + " " + pic(p.pic));
    };
    line(strf("venue %d %s", V.v, V.name.c_str()));
    line(strf("has_screen %d", V.has_screen ? 1 : 0));
    line(strf("stage %zu", V.stage.size()));
    for (size_t i = 0; i < V.stage.size(); i++) piece("stage", i, V.stage[i]);
    line(strf("close %zu", V.close.size()));
    for (size_t i = 0; i < V.close.size(); i++) piece("close", i, V.close[i]);
    piece("A", 0, V.A); line("A_cage " + hex(V.A_cage));
    line(strf("apron %zu", V.apron.size()));
    for (size_t i = 0; i < V.apron.size(); i++) { piece("apron", i, V.apron[i]); line(strf("apron_cage %zu ", i) + hex(V.apron_cage[i])); }
    line(strf("floor %zu", V.floor.size()));
    for (size_t i = 0; i < V.floor.size(); i++) { piece("floor", i, V.floor[i]); line(strf("floor_cage %zu ", i) + hex(V.floor_cage[i])); }
    line(strf("barricade %zu", V.barricade.size()));
    for (size_t i = 0; i < V.barricade.size(); i++) piece("barricade", i, V.barricade[i]);
    line(strf("corner %zu", V.corner.size()));
    for (size_t i = 0; i < V.corner.size(); i++) { piece("corner", i, V.corner[i]); line(strf("corner_cage %zu ", i) + hex(V.corner_cage[i])); }
    static const char* const G[5] = { "stands", "g1", "g2", "g3", "g4" };
    for (int g = 0; g < 5; g++) {
        line(strf("group %d %s %zu", g, G[g], V.groups[g].size()));
        for (size_t i = 0; i < V.groups[g].size(); i++)
            line(strf("item %d %zu ", g, i) + hex(V.groups[g][i].mesh) + " " + pic(V.groups[g][i].a) + " " + pic(V.groups[g][i].b));
    }
    line("empty " + hex(V.empty));
    line(strf("rr %zu", V.rr.size()));
    for (size_t i = 0; i < V.rr.size(); i++)
        line(strf("rr %zu ", i) + hex(V.rr[i].mesh) + " " + hex(V.rr[i].tex) + " " + hex(V.rr[i].pal) + strf(" %d", V.rr[i].turns));
    line("logo " + hex(V.logo_tex) + " " + hex(V.logo_pal));
    if (V.has_screen) {
        line(strf("tron_match %d %d %d %d", V.tron_match[0], V.tron_match[1], V.tron_match[2], V.tron_match[3]));
        line(strf("tron_close %d %d %d %d", V.tron_close[0], V.tron_close[1], V.tron_close[2], V.tron_close[3]));
        line(strf("sig %08x %08x", V.sig[0], V.sig[1]));
    }
    for (int k : { 13, 22 }) for (auto& r : wm_tron_rows(V, k)) line("row " + r);
    return o;
}

static int wmvenue_cmd(int argc, char** argv, Roms& R) {
    if (argc != 6) { fprintf(stderr, "usage: ri_cli wmvenue <roms> <ri data> <v | all> <out folder>\n"); return 2; }
    fs::path rd = argv[3], o = argv[5];
    const RomFiles* wm = R.files("wm2000");
    if (!wm) fail("WrestleMania 2000: not found");
    ojson scenes = wm3d_scenes(make_image("wm_close1", rd / "loadmaps", R));
    ojson venues = wm_venues(make_image("wm_fly1", rd / "loadmaps", R));
    std::string which = argv[4];
    for (int v = 0; v < 7; v++) {
        if (which != "all" && atoi(which.c_str()) != v) continue;
        auto t0 = std::chrono::steady_clock::now();
        WmVenue V = wm_venue(v, *wm, scenes, venues, rd / "frozen");
        write_file(o / strf("%d.txt", v), wmvenue_text(V));
        printf("venue %d %s: stage %zu, close %zu, rr %zu (%.2fs)\n", v, V.name.c_str(), V.stage.size(), V.close.size(), V.rr.size(),
               std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    return 0;
}
