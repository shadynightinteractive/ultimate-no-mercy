// ri_cli_collide.inc (phase3 p3collide, 2026-11-19): ri_cli's "collide" step -- every arena's walls and map as
// mod/gen/arenas_collide.py makes them (ri_collide.h), for tools\romimport\native\collide_check.py.
//
//   ri_cli collide <roms folder> <wmport.bin> <headers folder> <out folder> [<arenas_collide.py>] [--wmport-h <wmport_files.h>]
//
// <headers folder>: the mod's generated headers (arena_raw_files.h, arena_stage_files.h, arena_ring_files.h, arena_files.h,
// wmport_files.h -- the last from --wmport-h when given). Writes <out>\<n>.json for n = 0 .. 22, the text json.dump writes.
// The key needs arenas_collide.py's own bytes: without the file it is "" (the game never writes these files). Each result is
// also read back (parse_result_json) and written again: the same text, or the step fails.
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_collide.h"

static int collide_cmd(int argc, char** argv, Roms& R) {
    if (argc < 6) { fprintf(stderr, "usage: ri_cli collide <roms> <wmport.bin> <headers folder> <out> [<arenas_collide.py>] [--wmport-h <file>]\n"); return 2; }
    fs::path pack_path = argv[3], hdrs = argv[4], out = argv[5], py, wmport_h;
    for (int i = 6; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--wmport-h" && i + 1 < argc) wmport_h = argv[++i];
        else py = a;
    }
    if (wmport_h.empty()) wmport_h = hdrs / "wmport_files.h";
    auto text = [](const fs::path& p) { Bytes b = read_file(p); return std::string(b.begin(), b.end()); };
    collide::HeaderTexts h;
    h.wmport_files = text(wmport_h);
    h.arena_raw_files = text(hdrs / "arena_raw_files.h");
    h.arena_stage_files = text(hdrs / "arena_stage_files.h");
    h.arena_ring_files = text(hdrs / "arena_ring_files.h");
    h.arena_files = text(hdrs / "arena_files.h");
    const Bytes pack = read_file(pack_path);
    Bytes py_bytes;
    if (!py.empty()) py_bytes = read_file(py);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<collide::Collide> all = collide::collide_all(pack, h, R, py.empty() ? nullptr : &py_bytes);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fs::create_directories(out);
    for (size_t n = 0; n < all.size(); n++) {
        const std::string j = collide::result_json(all[n]);
        if (collide::result_json(collide::parse_result_json(j)) != j) fail("arena %d: the result doesn't read back the same", int(n));
        write_file(out / (std::to_string(n) + ".json"), j);
        const auto& c = all[n];
        size_t props = c.props.size();
        printf("%2d %-26s walk %5d walls %3d (path %3d, %2d props) grid %dx%d y %g..%g\n", int(n), c.name.c_str(), c.walk_cells, c.n_walls,
               int(c.path.size()), int(props), c.gw, c.gh, c.ymin, c.ymax);
    }
    printf("%d arenas in %.3f s%s\n", int(all.size()), secs, py.empty() ? " (no arenas_collide.py given: key \"\")" : "");
    return 0;
}
