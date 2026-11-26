// ri_cli_revfolders.inc (phase3 p3revfolders, 2026-11-19): ri_cli's "revfolders" step -- WCW/nWo Revenge's never-ported
// Superstars as wrestler folders (ri_revimport.h), for tools\romimport\native\revfolders_check.py.
//
//   ri_cli revfolders <roms folder> <source root> <out folder> [sid,sid,...] [--moveset-from <folder>[;<folder>...]]
//
// <source root>: the source tree (its tools\romimport and tools\handlers\handlers\revenge are read). Writes <out>\<string id>\
// for each man: model\, photo.bin, theme.bin, theme.meta, wrestler.json -- without the moveset (ported separately).
// --moveset-from (a check of the plug-in point only): each man's moveset pieces taken from these folders' <sid>\wrestler.json
// ("moveset", "source"."win_pose", "source"."abilities") and <sid>\moves.json, a later folder's file winning -- so the folders
// can be compared whole with the Python's.
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_revimport.h"

#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_revmoves.h"
#include <memory>

static int revfolders_cmd(int argc, char** argv, Roms& R) {
    if (argc < 5) { fprintf(stderr, "usage: ri_cli revfolders <roms> <source root> <out> [sid,...] [--moveset-from <dir>[;<dir>...]]\n"); return 2; }
    fs::path src = argv[3], out = argv[4];
    std::vector<std::string> only, from;
    for (int i = 5; i < argc; i++) {
        std::string a = argv[i];
        std::vector<std::string>* into = &only;
        if (a == "--moveset-from" && i + 1 < argc) { a = argv[++i]; into = &from; }
        char sep = into == &from ? ';' : ',';
        for (size_t p = 0; p <= a.size();) {
            size_t q = a.find(sep, p); if (q == std::string::npos) q = a.size();
            if (q > p) into->push_back(a.substr(p, q - p));
            p = q + 1;
        }
    }
    RevImportData D{ { src / "tools" / "romimport" }, src / "tools" / "handlers" / "handlers" / "revenge" };
    RevMovesetFn mv = nullptr;
    if (!from.empty()) {
        mv = [&](const RevManInfo& m) -> std::optional<RevMovesetPiece> {
            RevMovesetPiece p;
            for (auto& dir : from) {
                Bytes b;
                if (read_file_if(fs::path(dir) / m.sid / "wrestler.json", b)) {
                    ojson w = ojson::parse(std::string(b.begin(), b.end()));
                    p.moveset.reset(); p.win_pose.reset(); p.abilities.reset();
                    if (w.contains("moveset")) p.moveset = w["moveset"].get<std::string>();
                    if (w["source"].contains("win_pose")) p.win_pose = w["source"]["win_pose"].get<std::string>();
                    if (w["source"].contains("abilities")) p.abilities = w["source"]["abilities"].get<std::string>();
                }
                if (read_file_if(fs::path(dir) / m.sid / "moves.json", b)) p.moves_json = std::string(b.begin(), b.end());
            }
            return p;
        };
    }
    // (no --moveset-from: the movesets ported natively, ri_revmoves -- the game's own way)
    std::unique_ptr<RevMoves> native;
    if (from.empty()) { native = std::make_unique<RevMoves>(R, D.ri.dir, D.handler_dir); mv = native->fn(); }
    auto t0 = std::chrono::steady_clock::now();
    RevFoldersResult r = revenge_folders(R, D, out, only, mv);
    for (auto& l : r.log) printf("%s\n", l.c_str());
    printf("%zu folders in %s (%.1f s)\n", r.made.size(), out.string().c_str(),
           std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}
