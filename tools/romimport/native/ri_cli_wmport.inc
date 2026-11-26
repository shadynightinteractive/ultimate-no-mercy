// ri_cli_wmport.inc (phase3 p3wmport, 2026-11-19): ri_cli wmport -- the mod's pack wmport.bin and its headers (ri_wmport),
// for tools\romimport\native\wmport_check.py.
//   ri_cli wmport <roms> <ri data> <rev_anims.json> <out folder> [--rma] [--results <stagecollide result folder> | --collide | --collide-py <arenas_collide.py>]
// writes <out>\wmport.bin, <out>\src\<header>.h (as the Python writes them), <out>\models.txt ("name costume hex" lines:
// wmport_models.pkl's items), <out>\videos.txt ("name video"), <out>\result\<n>.json (the collision results used).
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_wmport.h"

static int wmport_cmd(int argc, char** argv, Roms& R) {
    if (argc < 6) { fprintf(stderr, "usage: ri_cli wmport <roms> <ri data> <rev_anims.json> <out> [--rma] [--results <dir>]\n"); return 2; }
    RiData D{ argv[3] };
    WmportIn in;
    Bytes ra = read_file(argv[4]);
    in.rev_anims_json.assign(ra.begin(), ra.end());
    fs::path o = argv[5];
    for (int i = 6; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--rma") in.with_rma = true;
        else if (a == "--collide") in.collide = native_collide(R);     // (the collision step natively; the key without the .py)
        else if (a == "--collide-py" && i + 1 < argc) { static Bytes py = read_file(argv[++i]); in.collide = native_collide(R, &py); }
        else if (a == "--results" && i + 1 < argc) {
            fs::path rd = argv[++i];
            for (int n = 0; n < 64; n++) {
                Bytes t;
                if (read_file_if(rd / strf("%d.json", n), t)) in.collide_results[n] = std::string(t.begin(), t.end());
            }
        }
    }
    WmportOut W = make_wmport(R, D, in);
    write_file(o / "wmport.bin", W.pack);
    for (auto& [name, text] : W.headers) write_file(o / "src" / name, text);
    std::string m;
    for (auto& [key, b] : W.models) {
        m += strf("%s %d ", key.first.c_str(), key.second);
        for (uint8_t c : b) m += strf("%02x", c);
        m += "\n";
    }
    write_file(o / "models.txt", m);
    std::string v;
    for (auto& [name, n] : W.videos) v += strf("%s %d\n", name.c_str(), n);
    write_file(o / "videos.txt", v);
    for (auto& [n, t] : W.collide_json) write_file(o / "result" / strf("%d.json", n), t);
    for (auto& l : W.log) printf("%s\n", l.c_str());
    printf("wmport: %zu bytes, %zu headers\n", W.pack.size(), W.headers.size());
    return 0;
}
