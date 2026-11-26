// [commentary2 2026-11-18] Test CLI for the whole builder, as the game runs it on first launch:
//   comm_build_test <recipe dir> <disc image (.rvz/.wia) or folder of the disc's files> <out.pak> [threads]
// Prints how many clips came out identical / equivalent / wrong / missing and the pack's SHA-1 against the target.
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "../comm_builder.h"

using namespace comm_build;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: comm_build_test <recipe dir> <disc> <out.pak> [threads]\n"); return 2; }
    Recipe r;
    std::string why;
    if (!load_recipe(argv[1], r, why)) { std::fprintf(stderr, "recipe: %s\n", why.c_str()); return 3; }
    std::unique_ptr<Media> m = Media::open(argv[2], why);
    if (!m) { std::fprintf(stderr, "disc: %s\n", why.c_str()); return 4; }
    size_t needed = 0;
    const size_t found = recipe_media_found(r, *m, needed);
    std::printf("disc %s: %zu media; the recipe's lines on it: %zu of %zu\n", m->game_id().c_str(), m->count(), found, needed);
    std::vector<uint8_t> pack;
    BuildReport rep;
    const int threads = argc > 4 ? std::atoi(argv[4]) : 4;
    if (!build_pack(*m, r, pack, rep, why, threads, [](size_t d, size_t n) { if (d % 500 == 0 || d == n) std::printf("  %zu / %zu\n", d, n); }, nullptr)) {
        std::fprintf(stderr, "build: %s\n", why.c_str());
        return 5;
    }
    std::printf("identical %zu, equivalent %zu, wrong %zu, missing %zu\n", rep.identical, rep.equivalent, rep.wrong, rep.missing);
    for (const std::string& s : rep.notes) std::printf("  %s\n", s.c_str());
    std::printf("pack: %zu clips, %zu lines, %zu bytes, sha1 %s (%s the target %s)\n", rep.clips, rep.lines, pack.size(),
                rep.pack_sha1.c_str(), rep.same_as_target ? "THE SAME AS" : "not", r.target_sha1.c_str());
    std::ofstream o(argv[3], std::ios::binary);
    o.write((const char*)pack.data(), (std::streamsize)pack.size());
    return rep.same_as_target ? 0 : 1;
}
