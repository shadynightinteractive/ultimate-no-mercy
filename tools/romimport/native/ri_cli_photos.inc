// ri_cli_photos.inc (phase3 p3photos, 2026-11-19): ri_cli photos -- the mod's photo headers and tv_carriers.h (ri_photos),
// for tools\romimport\native\photos_check.py.
//   ri_cli photos <roms> <ri data> <out folder> [ports | nm]
// writes <out>\<header> for each header made (ports: rev_photos.h, jarrett_photo.h, meanie_photo.h; nm: photos_nm.h,
// mideon_photo.h, droz_photo.h, bigshow_photo.h, tv_carriers.h; none given: both), and prints each one's time.
#include "../../../exe/WWFNoMercyRecomp/src/romimport/ri_photos.h"

static int photos_cmd(int argc, char** argv, Roms& R) {
    if (argc < 5) { fprintf(stderr, "usage: ri_cli photos <roms> <ri data> <out> [ports | nm]\n"); return 2; }
    fs::path o = argv[4];
    fs::create_directories(o);
    std::string only = argc > 5 ? argv[5] : "";
    RiData D{ argv[3] };
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    auto put = [&](const std::vector<std::pair<std::string, std::string>>& hs, const char* what) {
        for (auto& [name, text] : hs) { write_file(o / name, text); printf("  %s (%zu bytes)\n", name.c_str(), text.size()); }
        printf("%s: %zu headers, %.2f s\n", what, hs.size(), secs());
    };
    if (only.empty() || only == "nm") put(nm_photo_headers(R, D), "nm");
    if (only.empty() || only == "ports") put(port_photo_headers(R, D), "ports");
    return 0;
}
