// ri_cli_wmmoves.inc (phase3 p3wmmoves, 2026-11-19): ri_cli's "wmmoves" step -- the WrestleMania 2000 move library and the
// 56 men's movesets (ri_wmmoves.h), for tools\romimport\native\wmmoves_check.py.
//
//   ri_cli wmmoves <roms folder> <ri data folder> <wm2000 fixes folder> <out folder> [sid,sid,...]
//
// <ri data folder> is tools\romimport (its loadmaps\, names_index.json); <fixes folder> tools\handlers\handlers\wm2000\wrestlers.
// Writes the chain step "wmlib" (wmlib.py lib <out>): <out>\movelib_wm2000.bin, move_lib_wm2000.json, rows_wm2000.json; and
// for each man without a "legacy_game_id" (the two built-in ports are made by ported() and then removed: skipped) --
// <out>\<string id>\moves.json (as his folder has it after wm2k_import.py and wm2k_refresh.py) and <out>\<string id>\moveset.json
// ({"moveset": the packed hex, "moveset_fallbacks": n, "stance": note, "win_pose": note}: wrestler.json's values; a note the
// refresh didn't write is left out).
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_wmmoves.h"

static int wmmoves_cmd(int argc, char** argv, Roms& R) {
    if (argc < 6) { fprintf(stderr, "usage: ri_cli wmmoves <roms> <ri data> <fixes folder> <out> [sid,...]\n"); return 2; }
    fs::path data = argv[3], fixes = argv[4], out = argv[5];
    std::string only = argc >= 7 ? argv[6] : "";
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    WmMoveLib L(R, data, fixes);
    fprintf(stderr, "  library built: %.2f s\n", secs());
    L.write_lib(out);
    fprintf(stderr, "  wmlib files written: %.2f s\n", secs());
    std::vector<WmRecord> roster = wm_roster(wm_main_ram(*R.rom("wm2000")));
    int n = 0;
    for (auto& w : L.who()) {
        if (w.ported) continue;
        if (!only.empty() && ("," + only + ",").find("," + w.sid + ",") == std::string::npos) continue;
        WmMoves m = L.man(w.sid, roster.at(w.roster_index), w.reference_ordinal);
        fs::path d = out / w.sid;
        write_file(d / "moves.json", m.moves_json);
        ojson j = ojson::object();
        j["moveset"] = m.packed_hex; j["moveset_fallbacks"] = m.fell;
        if (m.stance_note) j["stance"] = *m.stance_note;
        if (m.win_pose_note) j["win_pose"] = *m.win_pose_note;
        PyDump h; h.indent = 1;
        write_file(d / "moveset.json", py_dumps(j, h));
        n++;
    }
    printf("wmmoves: library + %d men (%.2f s)\n", n, secs());
    return 0;
}

// ri_cli bbmoves <roms folder> <ri data folder> <out folder>: Badd Blood's two men's movesets (imports2.py vader / goldust:
// baddblood\game.py moveset + pack_moveset) -- <out>\<id>\moves.json and <out>\<id>\moveset.json ({"moveset",
// "moveset_fallbacks"}), for tools\romimport\native\wmmoves_check.py --bb. (Roster indexes 19 / 32, references No Mercy's
// Viscera 45 / Test 21: imports2.py's.)
static int bbmoves_cmd(int argc, char** argv, Roms& R) {
    if (argc < 5) { fprintf(stderr, "usage: ri_cli bbmoves <roms> <ri data> <out>\n"); return 2; }
    fs::path data = argv[3], out = argv[4];
    auto rom = R.rom("baddblood");
    if (!rom) fail("no Badd Blood ROM");
    std::vector<WmRecord> roster = wm_roster(wm_main_ram(*rom));
    struct Man { const char* id; int k, ref; const char* about; };
    for (const Man& m : { Man{ "vader", 19, 45, VADER_MOVES_ABOUT }, Man{ "goldust", 32, 21, GOLDUST_MOVES_ABOUT } }) {
        WmPortedMoveset p = baddblood_moveset(R, data, roster.at(m.k), m.ref, m.id, m.about);
        write_file(out / m.id / "moves.json", p.moves_json);
        ojson j = ojson::object(); j["moveset"] = p.packed_hex; j["moveset_fallbacks"] = p.fell;
        PyDump h; h.indent = 1;
        write_file(out / m.id / "moveset.json", py_dumps(j, h));
        printf("%s: %d slots fell back\n", m.id, p.fell);
    }
    return 0;
}
