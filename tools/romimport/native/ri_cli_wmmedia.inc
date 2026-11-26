// ri_cli_wmmedia.inc (phase3 p3wmmedia, 2026-11-19): ri_cli's "wmmedia" step -- WrestleMania 2000's men's themes and
// Titantrons as wm2k_import.py write_theme_video makes them (ri_wmmedia.h), for tools\romimport\native\wmmedia_check.py.
//
//   ri_cli wmmedia <roms folder> <load maps folder> <wm2000 fixes folder> <out folder> [sid,sid,...] [--steps <dir>]
//
// For each man of <fixes folder>\*.json (tools\handlers\handlers\wm2000\wrestlers) without a "legacy_game_id" (the two built-in
// ports are made by ported() and then removed: skipped), writes <out>\<string id>\ theme.bin, theme.meta (when his music slot
// plays a sound), video.bin (when his record's video isn't 0), source_theme.json and source_video.json (wrestler.json's
// "source"."theme" / "source"."video": the dict, or the "none" text, as json.dumps(indent=1) writes it). --steps also writes
// the theme's intermediate arrays (float64 / int16, raw) per man under <dir>\<string id>\ for the step-by-step comparison.
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_wmmedia.h"
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_wmroster.h"

static int wmmedia_cmd(int argc, char** argv, Roms& R) {
    if (argc < 6) { fprintf(stderr, "usage: ri_cli wmmedia <roms> <load maps> <fixes folder> <out> [sid,...] [--steps <dir>]\n"); return 2; }
    fs::path maps = argv[3], fixes = argv[4], out = argv[5], steps_dir;
    std::string only;
    for (int i = 6; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--steps" && i + 1 < argc) steps_dir = argv[++i];
        else only = "," + a + ",";
    }
    auto rom = R.rom("wm2000");
    const RomFiles* files = R.files("wm2000");
    if (!rom || !files) fail("no WrestleMania 2000 ROM");
    auto t0 = std::chrono::steady_clock::now();
    Bytes sel = make_image("wm_sel1", maps, R);
    std::vector<WmRecord> roster = wm_roster(wm_main_ram(*rom));
    double t_img = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::vector<fs::path> men;
    for (auto& p : fs::directory_iterator(fixes)) if (p.path().extension() == ".json") men.push_back(p.path());
    std::sort(men.begin(), men.end());
    int n = 0, themes = 0, videos = 0;
    PyDump ind; ind.indent = 1;
    for (auto& p : men) {
        Bytes t = read_file(p);
        ojson fx = ojson::parse(std::string(t.begin(), t.end()));
        if (fx.contains("legacy_game_id")) continue;
        std::string sid = fx["string_id"];
        if (!only.empty() && only.find("," + sid + ",") == std::string::npos) continue;
        const WmRecord& e = roster.at(fx["wm2000"]["roster_index"].get<int>());
        fs::path d = out / sid;
        fs::create_directories(d);
        ojson th, vi;
        if (wm_theme_sound(sel, e.music) < 0) th = wm_theme_none(e.music);
        else {
            WmThemeSteps st;
            FolderTheme T = wm_theme_folder(*rom, sel, e.music, steps_dir.empty() ? nullptr : &st);
            write_file(d / "theme.bin", T.bin); write_file(d / "theme.meta", T.meta);
            th = T.about; themes++;
            if (!steps_dir.empty()) {
                fs::path s = steps_dir / sid;
                fs::create_directories(s);
                auto dump = [&](const char* nm, const std::vector<double>& v) { write_file(s / nm, v.data(), v.size() * sizeof(double)); };
                dump("pcm.bin", st.pcm); dump("h.bin", st.h); dump("y.bin", st.y); dump("y2.bin", st.y2);
                ojson info = { { "src_rate", st.src_rate }, { "med", st.med }, { "up", st.up }, { "down", st.down }, { "ls", st.ls } };
                write_file(s / "info.json", py_dumps(info));
            }
        }
        if (e.video) { FolderVideo V = wm_video(*files, sel, e.video); write_file(d / "video.bin", V.bin); vi = V.about; videos++; }
        else vi = wm_video_none();
        write_file(d / "source_theme.json", py_dumps(th, ind));
        write_file(d / "source_video.json", py_dumps(vi, ind));
        n++;
    }
    printf("%d men: %d themes, %d videos (select screen image %.2f s)\n", n, themes, videos, t_img);
    return 0;
}
