// [commentary2 2026-11-18] Test CLI for comm_moves: the moves table made from the player's ROM by the recipe's references.
//   comm_moves_test <recipe dir> <folder with the No Mercy (USA) (Rev 1) ROM> [<old moves.tsv to compare with>]
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../comm_moves.h"

using namespace comm_build;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: comm_moves_test <recipe dir> <rom folder> [old moves.tsv]\n"); return 2; }
    Recipe r;
    std::string why;
    if (!load_recipe(argv[1], r, why)) { std::fprintf(stderr, "recipe: %s\n", why.c_str()); return 3; }
    r.moves.clear();   // (always from the ROM here)
    if (!moves_from_rom(r, find_nomercy_rom({ argv[2] }), why)) { std::fprintf(stderr, "moves: %s\n", why.c_str()); return 4; }
    size_t rows = 0;
    for (char c : r.moves) rows += c == '\n';
    std::printf("moves table from the ROM: %zu rows, %zu bytes\n", rows, r.moves.size());
    if (argc > 3) {
        std::ifstream in(argv[3], std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const bool same = ss.str() == r.moves;
        std::printf("the old moves.tsv: %s\n", same ? "THE SAME, byte for byte" : "DIFFERENT");
        return same ? 0 : 1;
    }
    return 0;
}
