// ri_cli.cpp (phase3, 2026-11-19): the ROM importer's steps run one at a time from the command line, for the developer's
// checks against the Python reference (tools\romimport\native\ri_check.py compares what each step writes with the files the
// Python chain made). The game itself calls the same code in its first-start builder; players never run this.
//
//   ri_cli roms   <roms folder>                                  the games found, their sizes and where from
//   ri_cli files  <roms folder> <game> <out folder>              every file of an AKI game, unpacked (%04x.bin)
//   ri_cli image  <roms folder> <load maps folder> <name> <out>  a RAM image (make_data.py's), with its extras
//   ri_cli jsonrt <file> <indent|-1> <out> [noascii] [compact] [crlf]   a JSON file read and written back as Python writes it (the writer's check)
//   ri_cli models <roms folder> <ri data> <out data folder> <wmport|rig|baddblood|all|revports>   the source games' models
//   ri_cli arenascan <roms folder> <ri data> <out data folder>   the arena sets' JSON (stage3d, wm3d, venues)
//   ri_cli capture <x> wm <ram> <slot> | rev <ram> <calls.txt> <slot>   a model read from a match image (md5 lines)
//   ri_cli data <roms folder> <ri data> <out folder>   names.json (move names), rev_movesets.json (as the pickles hold them)
//   ri_cli movelib_rev <roms folder> <library_index.json> <out>   content\movelib_revenge.bin
//   ri_cli theme <roms folder> <0214 | redux:<name>> <out folder>   a folder theme (theme.bin + theme.meta)
//   ri_cli reduxtest <roms folder> <empty scratch folder>   reduxthemes.py --selftest's rules, natively
//   ri_cli nmbank <roms folder> <load maps folder> <out>         No Mercy Plus V3's bank image (nmbank.pkl's "ram")
//   ri_cli wmfolders <roms folder> <source root> <out folder> [sid,...]   WrestleMania 2000's folders (wm2k_import + refresh)
//   ri_cli wmmedia <roms folder> <load maps folder> <wm2000 fixes folder> <out folder> [sid,...] [--steps <dir>]
//                  WrestleMania 2000's men's theme.bin / theme.meta / video.bin and source theme / video dicts
//   ri_cli wmmoves <roms folder> <ri data> <wm2000 fixes folder> <out folder> [sid,...]
//                  the WrestleMania 2000 move library (wmlib.py lib) and the men's moves.json / packed movesets
//   ri_cli revmoves <roms folder> <ri data> <revenge handler folder> <out folder> [sid,...]   the Revenge men's movesets
//   ri_cli bbmoves <roms folder> <ri data> <out folder>   Badd Blood's Vader's / Goldust's movesets (imports2.py)
//   ri_cli goldust <roms folder> <source root> <out collection folder>   imports2.py goldust: <out>\goldust\ (ri_goldust)
//   ri_cli collide <roms folder> <wmport.bin> <headers folder> <out folder> [<arenas_collide.py>] [--wmport-h <wmport_files.h>]
//                  every arena's walls and map (arenas_collide.py: data\stagecollide\result\<n>.json, ri_cli_collide.inc)
//   ri_cli wmvenue <roms folder> <ri data> <v | all> <out folder>   WrestleMania 2000's venues as arcade arenas (arenas_wm.py's
//                  data side: <out>\<v>.txt for wmvenue_check.py, ri_cli_wmvenue.inc)
//   ri_cli hdrfill <hdrfill.bin> <out modextra.bin> <text folder>... [--computed <symbol>=<hex bytes>]...
//                  the extras holes filled from the headers' text (the folders searched in order for each header / source
//                  file), written as mkplan.py writes modextra.bin (tools\firstrun\hdrfill_check.py compares); mod_binary.holes and
//                  plan.bin must be beside hdrfill.bin (the pointer words: relclean 2026-11-19)
//   ri_cli photos <roms folder> <ri data> <out folder> [ports | nm]   the mod's photo headers and tv_carriers.h (ri_photos:
//                  mkportphotos.py's three, the lost producers' five; tools\romimport\native\photos_check.py compares)
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_images.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_json.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_models.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_arenascan.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_capture.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_data.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_movelib.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_themes.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_redux.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_content.h"
#include <map>
#include <set>
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_hdrfill.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_wmimport.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_imports2.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_goldust.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_build.h"
#include <chrono>
#include <cstdio>

using namespace ri;
#include "ri_selftest_redux.inc"
#include "ri_cli_wmmedia.inc"     // ri_cli wmmedia <roms> <load maps> <wm2000 fixes folder> <out> [sid,...] [--steps <dir>]
#include "ri_cli_wmmoves.inc"     // ri_cli wmmoves <roms> <ri data> <wm2000 fixes folder> <out> [sid,...]
#include "ri_cli_revmoves.inc"    // ri_cli revmoves <roms> <ri data> <revenge handler folder> <out> [sid,...]
#include "ri_cli_wmport.inc"      // ri_cli wmport <roms> <ri data> <rev_anims.json> <out> [--rma] [--results <dir>]
#include "ri_cli_revfolders.inc"  // ri_cli revfolders <roms> <source root> <out> [sid,...] [--moveset-from <dir>[;<dir>...]]
#include "ri_cli_collide.inc"     // ri_cli collide <roms> <wmport.bin> <headers folder> <out> [<arenas_collide.py>] [--wmport-h <file>]
#include "ri_cli_wmvenue.inc"     // ri_cli wmvenue <roms> <ri data> <v | all> <out>
#include "ri_cli_ports.inc"       // ri_cli ports <roms> <ri data> <out> [step ...]
#include "ri_cli_photos.inc"      // ri_cli photos <roms> <ri data> <out> [ports | nm]

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: ri_cli <step> <args...> (see the file's head)\n"); return 2; }
    std::string cmd = argv[1];
    auto t0 = std::chrono::steady_clock::now();
    try {
        if (cmd == "hdrfill" && argc >= 5) {
            // (no ROMs: the text and the layouts only)
            HdrFill F = load_hdrfill(argv[2]);     // (relclean: with mod_binary.holes and plan.bin beside it, for the pointer words)
            std::vector<fs::path> dirs; std::map<std::string, Bytes> given;
            for (int i = 4; i < argc; i++) {
                std::string a = argv[i];
                if (a == "--computed" && i + 1 < argc) {
                    std::string v = argv[++i]; size_t eq = v.find('=');
                    if (eq == std::string::npos || (v.size() - eq - 1) % 2) fail("--computed <symbol>=<hex bytes>");
                    Bytes b;
                    for (size_t k = eq + 1; k < v.size(); k += 2) b.push_back((uint8_t)strtoul(v.substr(k, 2).c_str(), nullptr, 16));
                    given[v.substr(0, eq)] = b;
                } else dirs.push_back(a);
            }
            auto text = [&](const std::string& name) -> std::string {
                for (auto& d : dirs) { Bytes b; if (read_file_if(d / name, b)) return std::string(b.begin(), b.end()); }
                fail("%s: not in the text folders", name.c_str());
            };
            auto computed = [&](const HdrHole& h) -> Bytes {
                auto it = given.find(h.symbol);
                if (it == given.end()) fail("%s: a computed hole: give it with --computed", h.symbol.c_str());
                return it->second;
            };
            std::vector<Bytes> holes = fill_holes(F, text, computed);
            Bytes x = modextra_bin(F, holes);
            write_file(argv[3], x);
            size_t n = 0; for (auto& h : holes) n += h.size();
            printf("%zu holes, %zu bytes; modextra %zu bytes\n", holes.size(), n, x.size());
            fprintf(stderr, "(%s: %.2f s)\n", cmd.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            return 0;
        }
        Roms R(argv[2]);
        if (cmd == "reduxtest" && argc == 4) return redux_selftest(argv[2], argv[3]);
        if (cmd == "wmmedia") return wmmedia_cmd(argc, argv, R);    // (WrestleMania 2000's themes and videos: ri_cli_wmmedia.inc)
        if (cmd == "wmmoves") return wmmoves_cmd(argc, argv, R);    // (WrestleMania 2000's move library and movesets: ri_cli_wmmoves.inc)
        if (cmd == "bbmoves") return bbmoves_cmd(argc, argv, R);    // (Badd Blood's Vader / Goldust movesets: ri_cli_wmmoves.inc)
        if (cmd == "revmoves") return revmoves_cmd(argc, argv, R);  // (the never-ported Revenge men's movesets: ri_cli_revmoves.inc)
        if (cmd == "wmport") return wmport_cmd(argc, argv, R);      // (the mod's pack wmport.bin and its headers: ri_cli_wmport.inc)
        if (cmd == "revfolders") return revfolders_cmd(argc, argv, R);   // (Revenge's never-ported men's folders: ri_cli_revfolders.inc)
        if (cmd == "collide") return collide_cmd(argc, argv, R);    // (the arenas' walls and maps: arenas_collide.py, ri_cli_collide.inc)
        if (cmd == "wmvenue") return wmvenue_cmd(argc, argv, R);    // (WrestleMania 2000's venues as arcade arenas: arenas_wm.py, ri_cli_wmvenue.inc)
        if (cmd == "ports") return ports_cmd(argc, argv, R);        // (the built-in ports' tables: ri_cli_ports.inc)
        if (cmd == "photos") return photos_cmd(argc, argv, R);      // (the photo headers and tv_carriers.h: ri_cli_photos.inc)
        if (cmd == "roms") {
            for (int i = 0; ALL_GAMES[i]; i++) {
                auto b = R.rom(ALL_GAMES[i]);
                fs::path p = R.path(ALL_GAMES[i]);
                printf("%-14s %s\n", ALL_GAMES[i], b ? strf("%zu bytes, %s", b->size(), p.empty() ? "made from its patch" : p.filename().string().c_str()).c_str() : "not found");
            }
        } else if (cmd == "files" && argc == 5) {
            const RomFiles* F = R.files(argv[3]);
            if (!F) fail("%s: not found", argv[3]);
            fs::create_directories(argv[4]);
            uint32_t n = 0;
            for (uint32_t f = 1; f <= F->count(); f++) {
                const Bytes* b = F->get(f);
                if (!b) continue;
                write_file(fs::path(argv[4]) / strf("%04x.bin", f), *b); n++;
            }
            printf("%s: %u files\n", argv[3], n);
        } else if (cmd == "image" && argc == 6) {
            write_file(argv[5], make_image(argv[4], argv[3], R));
        } else if (cmd == "jsonrt" && argc >= 5) {
            Bytes t = read_file(argv[2]);
            ojson v = ojson::parse(std::string(t.begin(), t.end()));
            PyDump h; h.indent = atoi(argv[3]);
            for (int i = 5; i < argc; i++) {
                if (std::string(argv[i]) == "noascii") h.ensure_ascii = false;
                if (std::string(argv[i]) == "compact") { h.item_sep = ","; h.key_sep = ":"; }
            }
            std::string s = py_dumps(v, h);
            bool crlf = false;
            for (int i = 5; i < argc; i++) if (std::string(argv[i]) == "crlf") crlf = true;
            if (!t.empty() && t.back() == 10) s += (char)10;
            if (crlf) { std::string c; for (char ch : s) { if (ch == 10) c += (char)13; c += ch; } s = c; }
            write_file(argv[4], s);
        } else if (cmd == "models" && argc == 6) {
            RiData D{ argv[3] }; fs::path o = argv[4]; std::string want = argv[5];
            if (want == "revports")
                rev_port_models(R, D, [&](const std::string& rel, const RevLoad& l) {
                    write_file(o / rel, l.ram);
                    std::string c; for (auto& x : l.calls) c += strf("%08x %08x %08x\n", x.a0, x.a1, x.a2);
                    std::string cr = rel.substr(0, rel.size() - 8) + "_calls.txt";
                    write_file(o / cr, c);
                    printf("%s\n", rel.c_str()); fflush(stdout);
                });
            else wm_models(R, D, want, [&](const std::string& rel, const Bytes& m) { write_file(o / rel, m); printf("%s\n", rel.c_str()); fflush(stdout); });
        } else if (cmd == "arenascan" && argc == 5) {
            fs::path md = fs::path(argv[3]) / "loadmaps", o = argv[4];
            ojson S = rev_scenes(make_image("rev_ent0", md, R));
            write_file(o / "arenaport/stage3d/scenes.json", json_file_text(S, 1));
            for (int k = 0; k < 9; k++)
                write_file(o / "arenaport/stage3d" / REV_SCENE_NAMES[k] / "meshes.json", json_file_text(rev_scene_meshes(k, S, *R.files("revenge")), -1));
            write_file(o / "wmarena/wm3d_scenes.json", json_file_text(wm3d_scenes(make_image("wm_close1", md, R)), 1));
            write_file(o / "wmarena/wm_venues.json", json_file_text(wm_venues(make_image("wm_fly1", md, R)), 0));
        } else if (cmd == "capture" && argc >= 6) {
            std::string kind = argv[3];
            Bytes ram = read_file(argv[4]);
            auto h = [](const std::optional<Bytes>& b) { return b ? md5_hex(*b) : std::string("None"); };
            if (kind == "wm") {
                WmCapture c = wm_capture(ram, atoi(argv[5]));
                printf("desc %s nm %s\n", md5_hex(c.desc).c_str(), md5_hex(nm_descriptor(c.desc)).c_str());
                for (auto& p : c.parts) printf("%d %d %x %x %x %s %s\n", p.k, p.at, p.mesh, p.tex_id, p.pal_id, h(p.tex).c_str(), h(p.pal).c_str());
            } else {
                std::vector<RevCall> calls;
                Bytes t = read_file(argv[5]);
                std::string txt(t.begin(), t.end());
                for (size_t i = 0; i + 26 <= txt.size(); i = txt.find('\n', i) + 1) {
                    RevCall c; sscanf(txt.c_str() + i, "%x %x %x", &c.a0, &c.a1, &c.a2); calls.push_back(c);
                    if (txt.find('\n', i) == std::string::npos) break;
                }
                for (auto& [k, p] : rev_capture(ram, calls, atoi(argv[6]))) printf("%d %x %s %s\n", k, p.mesh, h(p.tex).c_str(), h(p.pal).c_str());
            }
        } else if (cmd == "data" && argc == 5) {
            fs::path o = argv[4];
            MoveNames N = move_names(R, fs::path(argv[3]) / "names_index.json");
            ojson nj = ojson::array();
            for (auto* L : { &N.nomercy, &N.nmplus }) { ojson a = ojson::array(); for (auto& [k, v] : *L) a.push_back(ojson::array({ k, v })); nj.push_back(a); }
            write_file(o / "names.json", py_dumps(nj));
            RevMovesets M = rev_movesets(R);
            ojson rows = ojson::array(), lists = ojson::array();
            for (auto& r : M.rows) { rows.push_back(ojson::array({ r.group, r.row, r.file })); lists.push_back(M.lists[{ r.group, r.row }]); }
            ojson mj = ojson::array({ rows, lists });
            write_file(o / "rev_movesets.json", py_dumps(mj));
        } else if (cmd == "movelib_rev" && argc == 5) {
            Bytes b = movelib_revenge(R, argv[3]);
            if (b.empty()) fail("no Revenge ROM");
            write_file(argv[4], b);
            printf("%zu bytes, md5 %s\n", b.size(), md5_hex(b).c_str());
        } else if (cmd == "theme" && argc == 5) {
            std::string what = argv[3];
            ThemeFiles t;
            if (what.rfind("redux:", 0) == 0) {
                auto rom = R.rom("revenge_redux");
                if (!rom) fail("no Redux");
                std::string nm = what.substr(6);
                auto song = nm.rfind("0x", 0) == 0 ? redux_song_for(*rom, (uint32_t)strtoul(nm.c_str() + 2, nullptr, 16)) : redux_song_of(*rom, nm);
                if (!song) fail("no Redux song for %s", what.c_str());
                t = redux_theme_files(*rom, *song);
            } else {
                auto rom = R.rom("revenge");
                if (!rom) fail("no Revenge");
                t = revenge_theme_files(*rom, (int)strtol(what.c_str(), nullptr, 16));
            }
            write_file(fs::path(argv[4]) / "theme.bin", t.bin); write_file(fs::path(argv[4]) / "theme.meta", t.meta);
            puts(t.about.c_str());
        } else if (cmd == "content" && argc == 7) {
            // ri_cli content <roms> <loadmaps> <roster_base.json> <roster_ext.h> <out folder>: content.bin + content_index.h
            Bytes rb = read_file(argv[4]), re = read_file(argv[5]);
            ContentPack c = make_content(R, argv[3], std::string(rb.begin(), rb.end()), std::string(re.begin(), re.end()));
            write_file(fs::path(argv[6]) / "content.bin", c.bin); write_file(fs::path(argv[6]) / "content_index.h", c.index_h);
            for (auto& l : c.log) puts(l.c_str());
        } else if (cmd == "nmbank" && argc == 5) {
            NmBank b = nmplus_bank(make_image("nm_pick", argv[3], R), R);
            write_file(argv[4], b.ram);
            printf("B %08x C %08x\n", b.B, b.C);
        } else if (cmd == "wmfolders" && argc >= 5) {
            // ri_cli wmfolders <roms> <source root> <out collection folder> [sid,sid...]: the WrestleMania 2000 folders (p3folders)
            fs::path src = argv[3];
            WmImportData D{ { src / "tools" / "romimport" }, src / "tools" / "handlers" / "handlers" / "wm2000" / "wrestlers",
                            src / "mod" / "gen" / "roster_ids.json" };
            std::vector<std::string> only;
            if (argc >= 6) {
                std::string a = argv[5];
                for (size_t i = 0; i <= a.size();) {
                    size_t j = a.find(',', i); if (j == std::string::npos) j = a.size();
                    if (j > i) only.push_back(a.substr(i, j - i));
                    i = j + 1;
                }
            }
            wm2000_folders(R, D, argv[4], only);
        } else if (cmd == "imported" && argc == 5) {
            // ri_cli imported <roms> <source root> <out collection folder>: imports2.py's Imported folders (p3folders)
            vader_folder(R, RiData{ fs::path(argv[3]) / "tools" / "romimport" }, argv[4]);
        } else if (cmd == "goldust" && argc == 5) {
            // ri_cli goldust <roms> <source root> <out collection folder>: imports2.py goldust -- No Mercy Plus V3's Goldust
            // parts on its Test, Badd Blood's Goldust's record / moveset / theme / video (ri_goldust, p3goldust)
            goldust_folder(R, RiData{ fs::path(argv[3]) / "tools" / "romimport" }, argv[4]);
        } else if (cmd == "build" && argc == 4) {
            // ri_cli build <roms> <game folder>: the first start's importer (ri_build romimport_build) on a scratch game folder
            // laid out as the package has it (firstrun\romimport\ our data, content\, wrestlers\); content\firstrun.json kept
            fs::path g = argv[3];
            BuildPaths P{ g, argv[2], g / "content", g / "wrestlers", g / "firstrun" / "romimport" };
            fs::create_directories(P.content); fs::create_directories(P.wrestlers);
            nlohmann::json rec = nlohmann::json::object();
            Bytes b;
            if (read_file_if(P.content / "firstrun.json", b)) rec = nlohmann::json::parse(std::string(b.begin(), b.end()));
            auto save = [&] { write_file(P.content / "firstrun.json", rec.dump(1)); };
            BuildResult res = romimport_build(P, rec, save);
            save();
            for (auto& l : res.log) printf("%s\n", l.c_str());
        } else if (cmd == "modextra" && (argc == 5 || argc == 7)) {
            // ri_cli modextra <roms> <game folder> <out> [--over <dir>]: the pack maker "modextra.bin" alone (ri_build
            // modextra_made) on a game folder laid out as the package has it (firstrun\hdrfill.bin, firstrun\romimport\):
            // <out>\src\<header> (every header text made), then <out>\modextra.bin filled from them -- with --over, a header in
            // <dir> replaces the made one (the developer's check against a build made from other headers)
            fs::path g = argv[3], o = argv[4];
            BuildPaths P{ g, argv[2], g / "content", g / "wrestlers", g / "firstrun" / "romimport" };
            std::map<std::string, std::string> texts = extras_texts(R, P);
            // (hands.c: only its wm_pose initializer is made -- written as hands.c.wm_pose, never to be taken for the mod's own file)
            for (auto& [name, text] : texts) write_file(o / "src" / (name == "hands.c" ? std::string("hands.c.wm_pose") : name), text);
            if (argc == 7 && std::string(argv[5]) == "--over")
                for (auto& de : fs::directory_iterator(argv[6])) {
                    Bytes t = read_file(de.path());
                    texts[de.path().filename().string()] = std::string(t.begin(), t.end());
                }
            Bytes mx = modextra_from_texts(load_hdrfill(P.data.parent_path() / "hdrfill.bin"), texts, sources_present(R));
            write_file(o / "modextra.bin", mx);
            made_reset();
            printf("modextra.bin %zu bytes, %zu headers\n", mx.size(), texts.size());
        } else { fprintf(stderr, "ri_cli: unknown step or wrong arguments\n"); return 2; }
    } catch (const std::exception& e) {
        fprintf(stderr, "ri_cli %s: %s\n", cmd.c_str(), e.what());
        return 1;
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "(%s: %.2f s)\n", cmd.c_str(), s);
    return 0;
}
