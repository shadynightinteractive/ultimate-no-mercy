// Loads the game's added content from files into game memory, instead of it being built into the mod.
//
// Why: the added themes, Titantron frames, ported wrestlers' files and full-detail models used to be 44.6 MB of data
// compiled into the mod's image. That meant every addition needed a rebuild, and the image had nearly filled the room
// it was given. As plain files, they can grow without touching the code, and this is the first step towards wrestlers
// and arenas that load from folders.
//
// Where they go: one after another from CONTENT_BASE (0x88000000), up to where the game heap's high region starts
// (0x90000000, see arcade_heap_service in the mod's logic.c). That leaves room for 128 MB. The mod's code asks for a
// pack by number and is told its address, so nothing depends on the order or the sizes.
//
// Where they're read from: the "content" folder beside the program, or the folder in the environment variable
// WCW_CONTENT_DIR (the QA runs use this to read the freshly built files in Development\mod\build).
//
// Byte order: game memory keeps each 32-bit word the PC's way round, so a byte at address A is stored at A ^ 3. The
// files are in the N64's order, so each word is swapped as it's copied in.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"
#include "arcade_host.h"

std::vector<std::filesystem::path> arcade_wrestlers_hd_packs();     // (arcade_wrestlers.cpp)
std::vector<std::filesystem::path> arcade_wrestlers_theme_packs();
std::vector<std::filesystem::path> arcade_wrestlers_video_packs();
const std::vector<std::filesystem::path>& arcade_arenas_files();     // (arcade_arenas.cpp)
uint64_t arcade_firstrun_pack_size(const std::string& name);         // (arcade_firstrun.cpp: a built-in pack's size, 0 unknown)

// The content pack number of a folder's first theme: they come after the built-in packs (PACK_FILES) and the full-detail models.
int arcade_packs_first_theme();

namespace {
constexpr uint32_t CONTENT_BASE = 0x88000000u;
constexpr uint32_t CONTENT_TOP = 0x90000000u;

// The packs, in the order the mod numbers them (PACK_* in the mod's arcade.h). (no2k15, 2026-11-23: hd.bin and hd_rock.bin, the two
// WWE 2K15 test slots' full-detail models, are gone with them; a wrestler folder's own hd.bin comes after these, as before.)
constexpr const char* PACK_FILES[] = { "content.bin", "wmport.bin" };
constexpr int PACK_COUNT = int(sizeof(PACK_FILES) / sizeof(PACK_FILES[0]));

std::vector<uint32_t> pack_address;     // PACK_FILES' packs, then the full-detail models from wrestler folders.
size_t arena_files_first = 0;   // Where the arena folders' files start in pack_address.
uint32_t packs_end = 0;      // The address just past the last pack; 0 until they're loaded.
uint32_t reserve_next = 0;   // Where the next working buffer goes (recomp_arcade_pack_reserve): after the packs.

std::filesystem::path content_folder() {
    const char* from_environment = getenv("WCW_CONTENT_DIR");
    if (from_environment && from_environment[0]) return std::filesystem::path(from_environment);
    return arcade::program_folder() / "content";
}

// Stops the game with a message: without its content it can't run correctly, and carrying on would read empty memory.
[[noreturn]] void give_up(const std::string& why) {
    fprintf(stderr, "[packs] %s\n", why.c_str());
    fflush(stderr);
    arcade::message_box(why.c_str());
    std::exit(1);
}

// Reads one file into game memory at `address`. Returns its size in bytes.
uint32_t load_one(uint8_t* rdram, const std::filesystem::path& path, uint32_t address) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.good()) give_up("A content file is missing: " + arcade::shown(path) + "\n\nThe content files are made from game data, so a download of the game has none: see README.txt.");   // (newuser 2026-11-17: the folder relative, as arcade::shown has it -- the full path named the PC's folders; and no "reinstalling restores it": the package has no content)
    const uint64_t size = uint64_t(file.tellg());
    if (uint64_t(address) + size > CONTENT_TOP) give_up("The content files are too big to fit: " + arcade::shown(path));
    std::vector<uint8_t> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
    if (!file.good()) give_up("A content file couldn't be read: " + arcade::shown(path));

    uint8_t* to = rdram + (address - 0x80000000u);
    uint64_t i = 0;
    for (; i + 4 <= size; i += 4) {
        uint32_t word;
        memcpy(&word, bytes.data() + i, 4);
        word = __builtin_bswap32(word);
        memcpy(to + i, &word, 4);
    }
    for (; i < size; i++) to[i ^ 3u] = bytes[i];     // Any bytes left over after the last whole word.
    return uint32_t(size);
}

void load_all(uint8_t* rdram) {
    if (packs_end) return;
    const std::filesystem::path folder = content_folder();
    uint32_t next = CONTENT_BASE;
    const auto add = [&](const std::filesystem::path& path) {
        pack_address.push_back(next);
        const uint32_t size = load_one(rdram, path, next);
        fprintf(stderr, "[packs] %s: %u KB at 0x%08X\n", arcade::shown(path).c_str(), size / 1024u, next);   // (privacy 2026-11-13: relative to the game folder)
        next = (next + size + 15u) & ~15u;
    };
    // (firstrun 2026-11-17) The built-in packs are the built-in extras' (the ported Superstars, No Mercy Plus's media):
    // game data no release has, made only from other games and recordings. Without one, the first start made
    // the mod without the extras (arcade_firstrun.cpp: the mod's extras flag is 0, so it never asks for these), so a missing
    // one is said, not a reason to stop. Folders' packs below are still checked as before. A missing one keeps its room (the
    // size the first start's plan says it has: arcade_firstrun.cpp), so every pack after it is where it would be with it.
    // wmport.bin's place is taken by content\wmport_base.bin when it isn't there: the first start made that from the ROM --
    // the game's own arenas' stage sets, at their places in the pack.
    // (c2hands 2026-11-19) A wmport.bin the first start's importer made without WrestleMania 2000 (Revenge's ports alone) ends
    // before the pack's last files, the game's own arenas' stage sets, whose rows of the pack's table are the base's -- at the
    // build's places, which that shorter pack doesn't reach. So when wmport.bin ends before content\wmport_base.bin's first
    // byte of them (the base: those files at those places, zeros before), the base is read first and wmport.bin over its
    // start; the room is the base's. A pack that reaches them holds them itself (the importer's full one, the developer's):
    // read alone, as always.
    const auto stage_start = [](const std::filesystem::path& base) -> uint64_t {   // (the base's first byte that isn't zero)
        std::ifstream file(base, std::ios::binary);
        std::vector<char> b(1u << 16);
        uint64_t at = 0;
        while (file.read(b.data(), std::streamsize(b.size())) || file.gcount() > 0) {
            const size_t n = size_t(file.gcount());
            for (size_t i = 0; i < n; i++) if (b[i]) return at + i;
            at += n;
        }
        return at;
    };
    const auto add_optional = [&](const std::filesystem::path& path) {
        std::error_code ec;
        const std::filesystem::path base = path.parent_path() / "wmport_base.bin";
        if (std::filesystem::is_regular_file(path, ec)) {
            if (path.filename() == "wmport.bin" && std::filesystem::is_regular_file(base, ec)) {
                const uint64_t size = std::filesystem::file_size(path, ec);
                if (size <= stage_start(base)) {
                    pack_address.push_back(next);
                    const uint32_t base_size = load_one(rdram, base, next);
                    load_one(rdram, path, next);
                    fprintf(stderr, "[packs] %s: %u KB at 0x%08X, over %s (%u KB: the game's own arenas' stage sets)\n",
                            arcade::shown(path).c_str(), unsigned(size / 1024u), next, arcade::shown(base).c_str(), base_size / 1024u);
                    next = (next + base_size + 15u) & ~15u;
                    return;
                }
            }
            add(path); return;
        }
        if (path.filename() == "wmport.bin" && std::filesystem::is_regular_file(base, ec)) { add(base); return; }
        pack_address.push_back(next);
        const uint64_t room = arcade_firstrun_pack_size(path.filename().string());
        fprintf(stderr, "[packs] %s: not there (a built-in extras' pack: the game plays without them; %u KB kept for it)\n",
                arcade::shown(path).c_str(), unsigned(room / 1024u));
        next = uint32_t((next + room + 15u) & ~uint64_t(15));
    };
    for (int i = 0; i < PACK_COUNT; i++) add_optional(folder / PACK_FILES[i]);
    for (const auto& path : arcade_wrestlers_hd_packs()) add(path);
    for (const auto& path : arcade_wrestlers_theme_packs()) add(path);
    for (const auto& path : arcade_wrestlers_video_packs()) add(path);
    arena_files_first = pack_address.size();
    for (const auto& path : arcade_arenas_files()) add(path);
    packs_end = next;
    reserve_next = (next + 0xFFFu) & ~0xFFFu;    // (A new page: QA makes the packs read-only, and these get written.)
    fflush(stderr);
}

// recomp_arcade_pack_load(which): the mod asks where a pack is. The first call loads them all. which = -1 asks for the
// address just past the last one instead, which the QA runs use to make the packs read-only.
void pack_load(uint8_t* rdram, recomp_context* ctx) {
    const int32_t which = _arg<0, int32_t>(rdram, ctx);
    load_all(rdram);
    uint32_t answer = packs_end;
    if (which >= 0) {
        if (size_t(which) >= pack_address.size()) give_up("The mod asked for a content pack that doesn't exist.");
        answer = pack_address[size_t(which)];
    }
    _return<uint32_t>(ctx, answer);
}

// recomp_arcade_pack_reserve(bytes): working memory for the mod, sized when it knows what's been loaded, after the packs
// and below the game heap's high region. It starts zeroed, and it isn't given back.
void pack_reserve(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bytes = _arg<0, uint32_t>(rdram, ctx);
    load_all(rdram);
    const uint32_t at = reserve_next;
    if (uint64_t(at) + bytes > CONTENT_TOP) give_up("The content files leave too little memory for the full-detail models.");
    reserve_next = (at + bytes + 15u) & ~15u;
    _return<uint32_t>(ctx, at);
}
}  // namespace

int arcade_packs_first_theme() { return PACK_COUNT + int(arcade_wrestlers_hd_packs().size()); }

// Where an arena folder's file landed: its index in arcade_arenas_files(). The packs are loaded by then: the mod only asks
// for a file after it has asked for a pack.
uint32_t arcade_packs_address_of_file(size_t index) {
    return arena_files_first + index < pack_address.size() ? pack_address[arena_files_first + index] : 0;
}

void arcade_packs_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_pack_reserve", pack_reserve);
    recomp::overlays::register_base_export("recomp_arcade_pack_load", pack_load);
}
