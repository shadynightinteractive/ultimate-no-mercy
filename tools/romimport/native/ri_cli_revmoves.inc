// ri_cli_revmoves.inc (phase3 p3wmmoves, 2026-11-19): ri_cli's "revmoves" step -- the never-ported Revenge men's movesets
// (ri_revmoves.h), for tools\romimport\native\revmoves_check.py.
//
//   ri_cli revmoves <roms folder> <ri data folder> <revenge handler folder> <out folder> [sid,sid,...]
//
// <ri data folder> is tools\romimport; <revenge handler folder> tools\handlers\handlers\revenge. The men: Revenge's roster
// (ri_revimport rev_roster) whose string ID library_ids.json names (the 52 never-ported men the folders are made for), each
// with aki_import.py's reference (4 Kane at 200 lbs - 100 and up, else 1 The Rock; 51 Stephanie McMahon for a valet: group 9,
// No Mercy's women's body type 15). Writes <out>\<string id>\moves.json and <out>\<string id>\moveset.json ({"moveset",
// "win_pose", "abilities"}: the wrestler.json values; a note not made is left out).
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_revmoves.h"

static int revmoves_cmd(int argc, char** argv, Roms& R) {
    if (argc < 6) { fprintf(stderr, "usage: ri_cli revmoves <roms> <ri data> <revenge handler folder> <out> [sid,...]\n"); return 2; }
    fs::path data = argv[3], hdir = argv[4], out = argv[5];
    std::string only = argc >= 7 ? argv[6] : "";
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    RevMoves M(R, data, hdir);
    fprintf(stderr, "  set up: %.2f s\n", secs());
    Bytes lt = read_file(hdir / "library_ids.json");
    ojson lib = ojson::parse(std::string(lt.begin(), lt.end()));
    std::vector<RevRecord> roster = rev_roster(R, RiData{ data });
    int n = 0;
    for (auto& e : roster) {
        std::string sid = rev_slug(e.name);
        if (!lib.contains(sid) || sid == "about") continue;
        if (!only.empty() && ("," + only + ",").find("," + sid + ",") == std::string::npos) continue;
        bool woman = (e.code >> 8) == 9;
        int ref = woman ? 51 : ((e.weight ? *e.weight : 0) >= 200 ? 4 : 1);
        RevManInfo info{ &e, sid, ref, woman ? 15 : 0 };
        auto p = M.man(info);
        if (!p) { printf("%s: no moveset file\n", sid.c_str()); continue; }
        fs::path d = out / sid;
        if (p->moves_json) write_file(d / "moves.json", *p->moves_json);
        ojson j = ojson::object();
        if (p->moveset) j["moveset"] = *p->moveset;
        if (p->win_pose) j["win_pose"] = *p->win_pose;
        if (p->abilities) j["abilities"] = *p->abilities;
        PyDump h; h.indent = 1;
        write_file(d / "moveset.json", py_dumps(j, h));
        n++;
    }
    printf("revmoves: %d men (%.2f s)\n", n, secs());
    return 0;
}
