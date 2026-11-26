// ri_roms.h (phase3, 2026-11-19): the player's ROMs, found by fingerprint -- the C++ twin of tools\romimport\roms.py.
//
// A ROM is known by the SHA-1 of its bytes in big-endian (.z64) order, whatever its file name, its byte order (.z64 / .v64
// / .n64) or whether it sits in a .zip. Two games may also come as a clean ROM plus the third-party patch the player has
// (an xdelta, applied in memory by the project's own VCDIFF decoder, the result checked by its fingerprint): No Mercy Plus
// V3 (No Mercy v1.0 + "No Mercy Plus V3.xdelta") and WCW/nWo Revenge Redux (Revenge + its patch, alone or in the .zip it
// is downloaded as). Nothing is written and nothing in an archive is run.
//
// An AKI game's files (every AKI N64 game keeps them the same way: a table of u32 offsets from a base, bit 0 = packed with
// AKI's LZSS, a u32 unpacked size first; files numbered from 1) are read by number, unpacked on first use.
#pragma once
#include "ri_core.h"
#include <map>
#include <memory>
#include <optional>

namespace ri {

// The games the importer knows: their fingerprints (roms.py SHA1) and file tables (handlers\aki\romtable.py GAMES).
struct GameInfo {
    const char* name;          // "nomercy", "revenge", ...
    const char* sha1;          // of the big-endian ROM
    uint32_t table, entries, base;   // the AKI file table: its ROM offset, its entries (files + 1), the data base (0: none)
};
const GameInfo* game_info(const std::string& name);
extern const char* const ALL_GAMES[];        // every name game_info knows, null-terminated

// A game's files by number (1-based), unpacked as roms.py RomFiles does (the No Mercy Plus rules: an entry whose end is
// before its start or past the ROM has no bytes; a packed entry under 4 bytes is kept as it is; an unpacked size of
// 0x400000 or more isn't one; the unpacker stops where the packed data ends).
class RomFiles {
public:
    RomFiles(std::shared_ptr<const Bytes> rom, const GameInfo& g);
    uint32_t count() const { return n_ - 1; }
    // nullptr for an entry with no bytes
    const Bytes* get(uint32_t i) const;
    const Bytes& operator()(uint32_t i) const;           // Error for an entry with no bytes
    const Bytes& rom() const { return *rom_; }
private:
    std::shared_ptr<const Bytes> rom_;
    uint32_t n_, base_;
    std::vector<uint32_t> t_;
    mutable std::map<uint32_t, std::optional<Bytes>> cache_;
};

Bytes aki_lzss(const uint8_t* src, size_t src_n, size_t n);   // AKI's LZSS (roms.py lzss: stops where the data ends)

// A ROM file's bytes in big-endian order; a .zip's first ROM (.z64 / .n64 / .v64) read from the archive in memory
// (modtables.read_rom). Empty if it can't be read or holds no ROM.
Bytes read_rom_file(const fs::path& p);

class Roms {
public:
    explicit Roms(fs::path folder) : folder_(std::move(folder)) {}
    // The game's ROM (big-endian), or nullptr when the player hasn't got it (or its patch doesn't give the fingerprint).
    std::shared_ptr<const Bytes> rom(const std::string& game);
    // Where the game's ROM file is (empty if not found as a file: a patched one is made, not found).
    fs::path path(const std::string& game);
    // The game's files by number, or nullptr.
    const RomFiles* files(const std::string& game);
    // The bytes of the .xdelta in the folder whose SHA-1 is sha1 -- a file, or a member of a .zip (read in memory).
    std::optional<Bytes> patch(const std::string& sha1);
    // (romsets 2026-11-24) Whether the patch a patched game is made by ("nmplus_v3", "revenge_redux") is in the folder -- with
    // its base ROM or without it: the start-up status window says one that is there without its base, as left out.
    bool has_patch(const std::string& game);
    const fs::path& folder() const { return folder_; }
private:
    void scan();
    fs::path folder_;
    bool scanned_ = false;
    std::map<std::string, fs::path> by_sha_;                          // SHA-1 -> the first file with it (sorted walk)
    std::map<std::string, std::shared_ptr<const Bytes>> roms_;         // made / read, by game
    std::map<std::string, std::unique_ptr<RomFiles>> files_;
    std::map<std::string, bool> tried_;
};

}  // namespace ri
