// ri_cli_ports.inc (phase3 p3ports, 2026-11-19): ri_cli ports -- the built-in ports' tables (ri_ports), for
// tools\romimport\native\ports_check.py.
//   ri_cli ports <roms> <ri data> <out folder> [step ...]
// steps: revanims (<out>\rev_anims.json, movex_data.h), mkroster (roster_ext.h), mkparts (parts_ext.h, parts_pkl.json),
// mkovr (parts_ovr.h: wmport's models from <out>\models.txt, else wmport made here from <out>\rev_anims.json);
// none given: all of them.
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_ports.h"

static int ports_cmd(int argc, char** argv, Roms& R) {
    if (argc < 5) { fprintf(stderr, "usage: ri_cli ports <roms> <ri data> <out> [step ...]\n"); return 2; }
    fs::path o = argv[4];
    std::set<std::string> want;
    for (int i = 5; i < argc; i++) want.insert(argv[i]);
    auto on = [&](const char* s) { return want.empty() || want.count(s); };
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    Ports P(R, argv[3]);
    printf("set up %.2f s\n", secs());
    if (on("revanims")) {
        Ports::RevAnims A = P.revanims();
        write_file(o / "rev_anims.json", A.json);
        write_file(o / "movex_data.h", A.movex_h);
        for (auto& l : A.log) printf("%s\n", l.c_str());
        printf("revanims %.2f s\n", secs());
    }
    if (on("mkroster")) {
        write_file(o / "roster_ext.h", P.roster_ext_h());
        printf("mkroster %.2f s\n", secs());
    }
    if (on("mkparts")) {
        Ports::Parts X = P.mkparts();
        write_file(o / "parts_ext.h", X.parts_ext_h);
        write_file(o / "parts_pkl.json", X.parts_pkl_json);
        printf("mkparts %.2f s\n", secs());
    }
    if (on("movelib")) {
        // (move_lib.h: movelib_revenge.bin made here by ri_movelib, the revanims rows from <out>\rev_anims.json)
        Bytes lib = movelib_revenge(R, fs::path(argv[3]).parent_path().parent_path() / "tools" / "handlers" / "handlers" / "revenge" / "library_index.json");
        Bytes ra = read_file(o / "rev_anims.json");
        write_file(o / "move_lib.h", P.move_lib_h(lib, std::string(ra.begin(), ra.end())));
        printf("movelib %.2f s\n", secs());
    }
    if (on("mkovr")) {
        // wmport's models: <out>\models.txt as ri_cli wmport writes it ("name costume hex" lines), else made here
        std::vector<std::pair<std::pair<std::string, int>, Bytes>> models;
        Bytes mt;
        if (read_file_if(o / "models.txt", mt)) {
            std::string t(mt.begin(), mt.end()), line;
            size_t a = 0;
            while (a < t.size()) {
                size_t e = t.find('\n', a); if (e == std::string::npos) e = t.size();
                line = t.substr(a, e - a); a = e + 1;
                char name[64]; int c; static char hx[4096];
                if (sscanf(line.c_str(), "%63s %d %4095s", name, &c, hx) != 3) continue;
                Bytes b; for (size_t i = 0; hx[i] && hx[i + 1]; i += 2) { unsigned v; sscanf(hx + i, "%2x", &v); b.push_back((uint8_t)v); }
                models.push_back({ { name, c }, b });
            }
        } else {
            Bytes ra = read_file(o / "rev_anims.json");
            WmportOut W = wmport_build(R, RiData{ argv[3] }, std::string(ra.begin(), ra.end()));
            models = W.models;
        }
        write_file(o / "parts_ovr.h", P.parts_ovr_h(models));
        printf("mkovr %.2f s\n", secs());
    }
    return 0;
}
