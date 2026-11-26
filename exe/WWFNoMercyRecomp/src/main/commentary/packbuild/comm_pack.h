// [commentary2 2026-11-18] The commentary pack's recipe (assets\commentary\<source>\: which lines of the player's disc
// make which clips, and the director's index with the transcripts) and the pack writer (the "NMCP" version 4 format that
// director.cpp reads; the same bytes as tools\commentary\compak_build.py).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace comm_build {

using Sha1 = std::array<uint8_t, 20>;
Sha1 sha1(const uint8_t* p, size_t n);
std::string hex(const uint8_t* p, size_t n);

struct RecipeClip {
    int clip = 0;               // its number in the recipe (lines.tsv's first column)
    bool cut = false;           // false: a whole line of the disc ("w13"); true: a piece of one ("cut")
    uint32_t media = 0;         // the Wwise media id of the line on the disc
    bool placed = true;         // a cut whose place is known (start/gain not "?")
    int64_t start = 0, length = 0;   // a cut: from this sample of the trimmed line, this many samples
    double gain = 1.0;               //        times this
    int64_t fade_in = 0, fade_out = 0;   //    with linear ramps this long at its ends
    int64_t samples = 0;        // the clip's length (the Ogg file's last granule)
    double rms = 0;             // its loudness, dB
    uint32_t serial = 0;        // its Ogg stream serial number
    Sha1 sha{};                 // the clip's SHA-1 as the developer's tools made it
    // [commdisc 2026-11-18] the clip's SHA-1 as this builder makes it from the WWE '13 disc (clips.tsv "disc_sha1"): the
    // canonical clip since the user's decision of 2026-11-18 (the developer's old pack is reference only); "identical"
    // means these bytes when the recipe has them
    Sha1 disc_sha{};
    bool has_disc_sha = false;
};

struct Recipe {
    std::string name, source, pack;  // "WWE '13 (Wii)", "wwe13", "wwe13.pak"
    std::vector<uint8_t> comment;    // the Vorbis comment packet every clip carries
    int rate = 20000;
    std::string target_sha1;         // the whole pack's SHA-1 when every clip comes out identical
    std::string old_target_sha1;     // [commdisc 2026-11-18] the developer's old pack (reference only: comm_pack_test)
    std::string lines;               // lines.tsv as it is (its last column: our tags, not the text -- make_tags.py)
    std::string moves;               // the moves table: made from the player's ROM (comm_moves.cpp: moves_from_rom), or a
                                     // developer's moves.tsv when there is one beside the recipe
    std::vector<std::array<uint32_t, 3>> move_refs;   // moves_refs.tsv: move number, AKI file, offset of its name
    std::vector<RecipeClip> clips;
};

bool load_recipe(const std::filesystem::path& dir, Recipe& r, std::string& why);

// The pack: rows (lines.tsv's, each ending "\n"), the moves table, the clips in order (row i's first column = index).
std::vector<uint8_t> pack_build(const std::string& rows, const std::string& moves, const std::vector<std::vector<uint8_t>>& clips);

// The pack from the recipe and the clips that were made (have[i] empty: left out): clips renumbered in order, only their
// rows kept (rebuild.py cmd_build). lines_kept: how many rows.
std::vector<uint8_t> pack_from_recipe(const Recipe& r, const std::vector<std::vector<uint8_t>>& have, size_t& lines_kept);

}  // namespace comm_build
