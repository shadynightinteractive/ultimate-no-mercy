// [arcade] Disc image reader for the first-launch builder: lists and reads the files on the player's
// own disc image (WWE '13 for Wii, as an RVZ or WIA file made by Dolphin) with no emulator, script
// language or Nintendo key.
//
// Why no key: RVZ and WIA store a Wii disc's game partition already decrypted, with its hash blocks
// left out (0x7C00 data bytes kept from each 0x8000-byte sector), so the files can be read directly.
// A plain .iso / .wbfs of a Wii disc is encrypted: [commentary2 2026-11-18, the user's decision] it is read
// only with the Wii key file the PLAYER supplies in roms\ (find_wii_keys); without one it is refused with
// a short reason (add the key file, or convert to RVZ). No key is shipped, embedded, downloaded or logged.
//
// Supported: RVZ (compression none or Zstandard, with RVZ packing of junk data) and WIA (compression
// none), Wii or GameCube; a plain GameCube .iso/.gcm (unencrypted) is read directly; a plain Wii .iso
// or a .wbfs with the player's key.
// Format reference: Dolphin's docs/WiaAndRvz.md (see wii_disc.cpp for notes).
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace comm_disc {

// One file of the disc's file system.
struct File {
  std::string path;   // '/' separated, as on the disc, e.g. "Audio/English(US)/x.pck"
  uint64_t offset;    // where it starts in the data space: the Wii game partition's decrypted data
                      // (hashes not counted), or the whole disc for GameCube
  uint64_t size;      // bytes
};

// A Wii common key (16 bytes), as the player supplied it.
using WiiKey = std::array<uint8_t, 16>;

// The key candidates in `folder` (the player's files, in the forms people have them): BootMii's keys.bin (as Dolphin
// reads it: the common key at 0x114), or common-key.bin / common_key.bin / commonkey.bin / ckey.bin (the 16 bytes), or
// the same names as .txt (32 hex digits). Each is only tried on a disc (its partition must decrypt to a Wii boot
// header); nothing is ever logged or written.
std::vector<WiiKey> find_wii_keys(const std::filesystem::path& folder);

class Disc {
public:
  // Opens an image. Returns nullptr and a plain reason in `why` when it cannot be read. keys: for a plain (encrypted)
  // Wii .iso / .wbfs, the player's key candidates (find_wii_keys).
  static std::unique_ptr<Disc> open(const std::filesystem::path& image, std::string& why,
                                    const std::vector<WiiKey>& keys = {});
  virtual ~Disc();

  std::string game_id() const { return game_id_; }            // 6 chars from the disc header, e.g. "SWWE01"
  const std::vector<File>& files() const { return files_; }   // every file of the game's file system

  // Reads n bytes of f starting at byte `at` of the file. False (with a reason) if out of range or
  // the image is damaged. One Disc per thread: it keeps a file handle and a decoded-chunk cache.
  bool read(const File& f, uint64_t at, void* dst, size_t n, std::string& why);

protected:
  Disc() = default;
  // Reads from the data space (see File::offset).
  virtual bool read_data(uint64_t offset, uint8_t* dst, size_t n, std::string& why) = 0;
  // Fills game_id_ and files_ from the boot header at data offset 0 (shift: 2 on Wii, where FST
  // offsets and sizes are stored divided by 4; 0 on GameCube).
  bool load_file_system(int shift, std::string& why);

  std::string game_id_;
  std::vector<File> files_;
};

}  // namespace comm_disc
