// Arenas added from folders, without rebuilding the game.
//
// Each arena is a folder inside the game's "arenas" folder, holding an arena.json. Folders starting with an underscore are
// skipped, so arenas\_example can sit there as a template. At the start of the game this reads them all, the content pack
// loader puts their files in game memory, and the mod lists them after the built-in arenas (mod src/arena.c).
//
// What an arena folder holds:
//   arena.json     "name": up to 27 characters. "based_on": the number of the arena it starts as a copy of, from 10 up, the
//                  added arenas in the order of arena_files.h (tools/arena_folder.py lists them).
//   files\XXXX.bin the game file with that hexadecimal ID, used in place of the game's own while this arena is in play. This
//                  is how it gets its own stage, ring, crowd and pictures. tools/arena_folder.py exports them to edit.
//   label.bin      its row on the ARENA page: a 128 x 32, 16-colour picture, as the game's own label files are.
//
// What it can't do yet: new collision walls or a different number of stage parts (it keeps those of the arena it's based
// on). Only the mod's added files can be replaced, not the original cartridge's.
//
// The limit is the ARENA page's list, whose entries are a byte each.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "json/json.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "arcade_host.h"

uint32_t arcade_packs_address_of_file(size_t index);     // (arcade_packs.cpp)

namespace {
using json = nlohmann::json;
constexpr int NAME_BYTES = 28;
constexpr uint32_t LABEL_ID = 0xFFFFu;       // The key a folder's label.bin goes under: no file has this ID.

struct OneFile { size_t index; uint32_t size; };     // Its place in file_list, and its bytes.
struct Arena {
    int based_on = 0;
    std::string name;
    std::unordered_map<uint32_t, OneFile> files;      // By the game file ID it replaces.
};
std::vector<Arena> arenas;
std::vector<std::filesystem::path> file_list;         // Every file of every arena, in the order the pack loader loads them.
bool scanned = false;

void add_file(Arena& a, uint32_t id, const std::filesystem::path& path) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size == 0) return;
    a.files[id] = { file_list.size(), uint32_t(size) };
    file_list.push_back(path);
}

// Reads one arena.json. Returns an empty string, or what's wrong with it.
std::string read_one(const std::filesystem::path& folder, Arena& a) {
    std::ifstream file(folder / "arena.json");
    if (!file.good()) return "has no arena.json";
    const json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return "arena.json isn't valid JSON";
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) return "arena.json needs a \"name\"";
    if (!j.contains("based_on") || !j["based_on"].is_number_integer()) return "arena.json needs \"based_on\", an arena's number";
    a.name = j["name"].get<std::string>().substr(0, size_t(NAME_BYTES) - 1);
    a.based_on = j["based_on"].get<int>();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder / "files", ec)) {
        if (entry.path().extension() != ".bin") continue;
        char* end = nullptr;
        const std::string stem = entry.path().stem().string();
        const unsigned long id = strtoul(stem.c_str(), &end, 16);
        if (stem.empty() || *end || id == 0 || id >= LABEL_ID) continue;
        add_file(a, uint32_t(id), entry.path());
    }
    add_file(a, LABEL_ID, folder / "label.bin");
    return "";
}

void scan() {
    if (scanned) return;
    scanned = true;
    const char* from_environment = getenv("WCW_ARENAS_DIR");          // (QA: the test arenas)
    const std::filesystem::path root = (from_environment && from_environment[0]) ? std::filesystem::path(from_environment) : arcade::program_folder() / "arenas";
    std::error_code ec;
    std::vector<std::filesystem::path> folders;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && entry.path().filename().string()[0] != '_') folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());
    for (const auto& folder : folders) {
        Arena a;
        const std::string wrong = read_one(folder, a);
        if (!wrong.empty()) { fprintf(stderr, "[arenas] %s: %s: skipped\n", folder.filename().string().c_str(), wrong.c_str()); continue; }
        fprintf(stderr, "[arenas] %s: \"%s\", based on %d, %zu files\n", folder.filename().string().c_str(), a.name.c_str(), a.based_on, a.files.size());
        arenas.push_back(std::move(a));
    }
    fflush(stderr);
}

// recomp_arcade_arena_count() -> how many folders were read.
void arena_count(uint8_t* rdram, recomp_context* ctx) {
    scan();
    _return<int32_t>(ctx, int32_t(arenas.size()));
}

// recomp_arcade_arena_get(i, dst): writes arena i into the mod's FolderArena (mod src/arena.c): the arena it's based on,
// whether it has a label, its name.
void arena_get(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= arenas.size()) return;
    const Arena& a = arenas[size_t(i)];
    MEM_W(0, dst) = a.based_on;
    MEM_W(4, dst) = a.files.count(LABEL_ID) ? 1 : 0;
    for (int k = 0; k < NAME_BYTES; k++) MEM_B(8 + k, dst) = size_t(k) < a.name.size() ? a.name[size_t(k)] : 0;
}

// recomp_arcade_arena_file(i, id, size_at) -> the address of arena i's replacement for file `id`, with its size written to
// size_at, or 0 if it has none. id 0xFFFF is its label.
void arena_file(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const uint32_t id = _arg<1, uint32_t>(rdram, ctx);
    const PTR(void) size_at = _arg<2, PTR(void)>(rdram, ctx);
    uint32_t address = 0;
    if (i >= 0 && size_t(i) < arenas.size()) {
        const auto it = arenas[size_t(i)].files.find(id);
        if (it != arenas[size_t(i)].files.end()) {
            address = arcade_packs_address_of_file(it->second.index);
            MEM_W(0, size_at) = int32_t(it->second.size);
        }
    }
    _return<uint32_t>(ctx, address);
}
}  // namespace

// Every file of every arena, in order, for the content pack loader.
const std::vector<std::filesystem::path>& arcade_arenas_files() {
    scan();
    return file_list;
}

// Each folder arena's name in capitals, with its number: stories name arenas by these. The added arenas' numbers carry on
// after the built-in ones, and `first` is the first number free.
void arcade_arenas_each_name(int first, const std::function<void(const std::string&, int)>& use) {
    scan();
    int number = first;
    for (const Arena& a : arenas) {
        std::string name = a.name;
        for (char& c : name) if (c >= 'a' && c <= 'z') c = char(c - 32);
        use(name, number++);
    }
}

void arcade_arenas_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_arena_count", arena_count);
    recomp::overlays::register_base_export("recomp_arcade_arena_get", arena_get);
    recomp::overlays::register_base_export("recomp_arcade_arena_file", arena_file);
}
