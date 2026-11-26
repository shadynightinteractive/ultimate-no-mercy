// [commentary2 2026-11-18] Test CLI for the recipe + pack writer: takes each recipe clip from an existing pack by its
// SHA-1 (the clips the developer's tools made), builds the pack the way the game's builder does, prints its SHA-1.
//   comm_pack_test <recipe dir> <pack> [out] [rom folder]   (<pack>: commentary.pak, its parts .pak.1 ... read after it;
//   the moves table from the No Mercy ROM in <rom folder>, as the game makes it, when the recipe has no moves.tsv)
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "../comm_moves.h"
#include "../comm_pack.h"

using namespace comm_build;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: comm_pack_test <recipe dir> <pack> [out]\n"); return 2; }
    Recipe r;
    std::string why;
    if (!load_recipe(argv[1], r, why)) { std::fprintf(stderr, "recipe: %s\n", why.c_str()); return 3; }
    if (r.moves.empty() && (argc < 5 || !moves_from_rom(r, find_nomercy_rom({ argv[4] }), why))) {
        std::fprintf(stderr, "the moves table: %s\n", argc < 5 ? "give the ROM's folder" : why.c_str());
        return 3;
    }
    std::vector<uint8_t> pak;
    for (int k = 0;; k++) {
        std::ifstream in(std::string(argv[2]) + (k ? "." + std::to_string(k) : ""), std::ios::binary);
        if (!in) break;
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string s = ss.str();
        pak.insert(pak.end(), s.begin(), s.end());
    }
    auto le32 = [&](size_t at) { return (uint32_t)pak[at] | (uint32_t)pak[at + 1] << 8 | (uint32_t)pak[at + 2] << 16 | (uint32_t)pak[at + 3] << 24; };
    if (pak.size() < 28) return 4;
    const Sha1 whole = sha1(pak.data(), pak.size());
    std::printf("source pack: %zu bytes, sha1 %s, %u clips\n", pak.size(), hex(whole.data(), 20).c_str(), le32(8));
    std::map<Sha1, std::vector<uint8_t>> by;
    const uint32_t nd = le32(8), to = le32(24);
    for (uint32_t i = 0; i < nd; i++) {
        const uint32_t off = le32(to + 8 * i), len = le32(to + 8 * i + 4);
        by[sha1(pak.data() + off, len)] = std::vector<uint8_t>(pak.begin() + off, pak.begin() + off + len);
    }
    std::vector<std::vector<uint8_t>> have(r.clips.size());
    size_t found = 0;
    for (size_t i = 0; i < r.clips.size(); i++) {
        // (commdisc 2026-11-18: a pack of the canonical clips is found by disc_sha1, the developer's old pack by sha1)
        auto f = r.clips[i].has_disc_sha ? by.find(r.clips[i].disc_sha) : by.end();
        if (f == by.end()) f = by.find(r.clips[i].sha);
        if (f != by.end()) { have[i] = f->second; found++; }
    }
    size_t lines = 0;
    const std::vector<uint8_t> out = pack_from_recipe(r, have, lines);
    const Sha1 s = sha1(out.data(), out.size());
    std::printf("recipe %s: %zu clips, %zu found by SHA-1 in the source pack\n", r.name.c_str(), r.clips.size(), found);
    const std::string h = hex(s.data(), 20);
    const char* which = h == r.target_sha1 ? "THE SAME AS the recipe's target" : h == r.old_target_sha1 ? "THE SAME AS the recipe's old_target (the developer's old pack)" : "not the recipe's target";
    std::printf("built: %zu clips, %zu lines, %zu bytes, sha1 %s (%s)\n", found, lines, out.size(), h.c_str(), which);
    if (argc > 3) { std::ofstream o(argv[3], std::ios::binary); o.write((const char*)out.data(), (std::streamsize)out.size()); }
    return h == r.target_sha1 || h == r.old_target_sha1 ? 0 : 1;
}
