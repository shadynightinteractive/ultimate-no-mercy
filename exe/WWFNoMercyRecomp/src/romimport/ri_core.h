// ri_core.h (phase3, 2026-11-19): the small things every part of the ROM importer uses.
//
// The ROM importer ("ri") builds the extras -- the other games' wrestlers, arenas, themes, move libraries and the mod's
// tables for them -- from the ROMs the player puts in roms\, on the game's first start (and again only for a ROM that is
// new). Each part is the C++ twin of a Python tool of the project (tools\romimport\..., mod\gen\..., tools\handlers\...),
// giving the same bytes; the Python stays the developer's reference, the player never needs it.
//
// Here: byte buffers, big-endian access (the N64 is big-endian), files read and written whole, hashes (SHA-1 names a ROM,
// MD5 is what the Python tools compare files by, SHA-256 the first start's files'), a plain error type.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ri {

namespace fs = std::filesystem;
typedef std::vector<uint8_t> Bytes;

// Something the importer can't do with what it was given: said plainly, never guessed around.
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] void fail(const char* fmt, ...);

// ---- big-endian access ---------------------------------------------------------------------------------------------
inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
inline void put_be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
inline void put_be16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
inline uint32_t be32(const Bytes& b, size_t at) { return be32(b.data() + at); }
inline uint16_t be16(const Bytes& b, size_t at) { return be16(b.data() + at); }
inline int32_t s32(uint32_t v) { return (int32_t)v; }
inline int32_t sx16(uint32_t v) { return (int16_t)(uint16_t)v; }
inline int32_t sx8(uint32_t v) { return (int8_t)(uint8_t)v; }

// ---- files ----------------------------------------------------------------------------------------------------------
Bytes read_file(const fs::path& p);                       // the whole file (Error if it can't be read)
bool read_file_if(const fs::path& p, Bytes& out);         // false if it isn't there or can't be read
// Written whole: a temporary file beside it, then renamed over (a stop half way never leaves half a file).
void write_file(const fs::path& p, const void* data, size_t n);
inline void write_file(const fs::path& p, const Bytes& b) { write_file(p, b.data(), b.size()); }
inline void write_file(const fs::path& p, const std::string& s) { write_file(p, s.data(), s.size()); }
// Written straight to its place, no temporary: only for files in a folder the importer moves in whole once complete
// (its temp folder) -- many small files, where the temporary and rename per file cost most of the time.
void write_new_file(const fs::path& p, const void* data, size_t n);
inline void write_new_file(const fs::path& p, const Bytes& b) { write_new_file(p, b.data(), b.size()); }
inline void write_new_file(const fs::path& p, const std::string& s) { write_new_file(p, s.data(), s.size()); }

// ---- hashes ---------------------------------------------------------------------------------------------------------
std::string sha1_hex(const uint8_t* p, size_t n);
inline std::string sha1_hex(const Bytes& b) { return sha1_hex(b.data(), b.size()); }
std::string md5_hex(const uint8_t* p, size_t n);
std::string sha256_hex(const uint8_t* p, size_t n);
inline std::string sha256_hex(const Bytes& b) { return sha256_hex(b.data(), b.size()); }
inline std::string md5_hex(const Bytes& b) { return md5_hex(b.data(), b.size()); }
std::array<uint8_t, 32> sha256(const uint8_t* p, size_t n);   // (FIPS 180-4: the firstrun files' hashes, modextra.bin's)

std::string strf(const char* fmt, ...);                   // printf into a std::string

}  // namespace ri
