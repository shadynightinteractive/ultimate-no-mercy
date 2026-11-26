// Texture packs: replacement pictures for anything the game draws, from PNG files.
//
// How it works: every picture the game draws gets a name from its contents, a 16-digit number worked out from its
// pixels. A pack is a folder inside the game's "texturepacks" folder, full of PNGs named by that number. When the game
// first uses a picture with a matching name, the PNG goes in its place, at whatever size the PNG is. The name doesn't
// depend on where the picture sits in memory, so packs keep working when the game's memory layout changes.
//
// Finding the names: make a folder called "_dump" inside "texturepacks". The game then saves every picture it draws
// there as a PNG with its name, and anyone can edit those and move them into a pack.
//
// Packs load in name order, and a later pack wins if two have the same picture. A folder starting with an underscore is
// skipped, so renaming a pack is how to switch it off. WCW_TEXTURE_PACKS_DIR replaces the "texturepacks" folder (QA).
//
// Not handled: a picture that changes every frame (a video) gets a new name each time, so it can't be replaced this way.
//
// The renderer calls these from one thread: the one reading the game's lists.
#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "lodepng/lodepng.h"
#include "arcade_host.h"

namespace {
bool scanned = false;
std::filesystem::path dump_folder;                                  // Empty: not saving pictures.
std::unordered_map<uint64_t, std::filesystem::path> replacements;   // Picture name -> its PNG.
std::unordered_set<uint64_t> dumped;                                // Saved this run: the file check is only done once.

void scan() {
    if (scanned) return;
    scanned = true;
    const char* from_environment = getenv("WCW_TEXTURE_PACKS_DIR");
    const std::filesystem::path root = (from_environment && from_environment[0]) ? std::filesystem::path(from_environment) : arcade::program_folder() / "texturepacks";
    std::error_code ec;
    if (std::filesystem::is_directory(root / "_dump", ec)) {
        dump_folder = root / "_dump";
        fprintf(stderr, "[textures] saving every picture to %s\n", arcade::shown(dump_folder).c_str());   // (privacy 2026-11-13: relative to the game folder)
    }
    std::vector<std::filesystem::path> packs;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && entry.path().filename().string()[0] != '_') packs.push_back(entry.path());
    }
    std::sort(packs.begin(), packs.end());
    for (const auto& pack : packs) {
        size_t count = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(pack, ec)) {
            if (file.path().extension() != ".png") continue;
            char* end = nullptr;
            const std::string stem = file.path().stem().string();
            const uint64_t name = strtoull(stem.c_str(), &end, 16);
            if (stem.size() != 16 || *end) continue;       // Not named by a picture's number, so it's something else.
            replacements[name] = file.path();
            count++;
        }
        fprintf(stderr, "[textures] pack %s: %zu pictures\n", pack.filename().string().c_str(), count);
    }
    fflush(stderr);
}
}  // namespace

// Called with a picture the game is using for the first time, as RGBA bytes. Saves it if that's switched on, and if a pack
// has a replacement, puts that in w, h and rgba instead. Returns true if it did. A PNG that can't be read is reported
// once, and the game's own picture stays.
bool arcade_texpacks_apply(uint64_t name, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgba) {
    scan();
    if (!dump_folder.empty() && dumped.insert(name).second) {
        char file[32];
        snprintf(file, sizeof file, "%016" PRIX64 ".png", name);
        const std::filesystem::path path = dump_folder / file;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) lodepng::encode(path.string(), rgba, w, h);
    }
    const auto it = replacements.find(name);
    if (it == replacements.end()) return false;
    std::vector<uint8_t> pixels;
    unsigned new_w = 0, new_h = 0;
    const unsigned error = lodepng::decode(pixels, new_w, new_h, it->second.string());
    if (error) {
        fprintf(stderr, "[textures] %s can't be read: %s\n", arcade::shown(it->second).c_str(), lodepng_error_text(error));
        replacements.erase(it);
        return false;
    }
    w = new_w; h = new_h;
    rgba = std::move(pixels);
    return true;
}
