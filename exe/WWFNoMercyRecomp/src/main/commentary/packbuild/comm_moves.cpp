// [commentary2 2026-11-18] See comm_moves.h. The AKI file reader follows tools/romimport/roms.py (RomFiles, lzss) and
// tools/handlers/handlers/aki/romtable.py's No Mercy entry.
#include "comm_moves.h"

#include <cstring>
#include <fstream>
#include <sstream>

namespace comm_build {

namespace {

constexpr uint32_t kTable = 0x16C31D8, kFiles = 19459, kBase = 0x1BD150;   // No Mercy (USA) (Rev 1)
const char* const kSha1 = "91cee3d096f4a76644d8b35b9aead6448909abd1";       // its bytes, big-endian

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// AKI's LZSS: a 4-byte size, then flag bytes (LSB first: 1 = a literal byte, 0 = a 12-bit ring position + 4-bit length - 3)
std::vector<uint8_t> lzss(const std::vector<uint8_t>& src, size_t n) {
    std::vector<uint8_t> out(n, 0);
    uint8_t ring[0x1000] = {};
    size_t r = 0xFEE, i = 4, o = 0;
    unsigned flags = 0;
    while (o < n && i < src.size()) {
        flags >>= 1;
        if (!(flags & 0x100)) flags = src[i++] | 0xFF00u;
        if (flags & 1) {
            if (i >= src.size()) break;
            const uint8_t c = src[i++];
            out[o++] = c; ring[r] = c; r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= src.size()) break;
            const uint8_t lo = src[i], hi = src[i + 1];
            i += 2;
            const size_t pos = lo | ((size_t)(hi & 0xF0) << 4), len = (size_t)(hi & 0xF) + 3;
            for (size_t k = 0; k < len; k++) {
                const uint8_t c = ring[(pos + k) & 0xFFF];
                if (o < n) out[o++] = c;
                ring[r] = c; r = (r + 1) & 0xFFF;
            }
        }
    }
    return out;
}

}  // namespace

std::vector<uint8_t> find_nomercy_rom(const std::vector<std::filesystem::path>& folders) {
    std::error_code ec;
    for (const std::filesystem::path& dir : folders) {
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (!e.is_regular_file(ec) || (ext != ".z64" && ext != ".v64" && ext != ".n64")) continue;
            if (e.file_size(ec) != 0x2000000) continue;   // (No Mercy: 32 MB)
            std::ifstream in(e.path(), std::ios::binary);
            std::vector<uint8_t> b(0x2000000);
            if (!in.read((char*)b.data(), (std::streamsize)b.size())) continue;
            if (b[0] == 0x37 && b[1] == 0x80) {                                  // .v64: bytes swapped in pairs
                for (size_t i = 0; i + 1 < b.size(); i += 2) std::swap(b[i], b[i + 1]);
            } else if (b[0] == 0x40 && b[1] == 0x12) {                           // .n64: little-endian words
                for (size_t i = 0; i + 3 < b.size(); i += 4) { std::swap(b[i], b[i + 3]); std::swap(b[i + 1], b[i + 2]); }
            }
            const Sha1 h = sha1(b.data(), b.size());
            if (hex(h.data(), h.size()) == kSha1) return b;
        }
    }
    return {};
}

bool aki_file(const std::vector<uint8_t>& rom, uint32_t index, std::vector<uint8_t>& out) {
    if (index < 1 || index >= kFiles || rom.size() < kTable + 4ull * kFiles) return false;
    const uint32_t t0 = be32(rom.data() + kTable + 4 * (index - 1)), t1 = be32(rom.data() + kTable + 4 * index);
    const uint64_t a = (uint64_t)(t0 & ~1u) + kBase, b = (uint64_t)(t1 & ~1u) + kBase;
    if (b < a || b > rom.size()) return false;
    std::vector<uint8_t> raw(rom.begin() + (ptrdiff_t)a, rom.begin() + (ptrdiff_t)b);
    if ((t0 & 1) && raw.size() >= 4) {
        const uint32_t n = be32(raw.data());
        if (n < 0x400000) raw = lzss(raw, n);
    }
    out.swap(raw);
    return true;
}

bool moves_from_rom(Recipe& r, const std::vector<uint8_t>& rom, std::string& why) {
    if (rom.empty()) { why = "the No Mercy (USA) (Rev 1) ROM isn't in roms\\ (the moves table is made from it)"; return false; }
    std::string moves;
    std::vector<uint8_t> file;
    uint32_t have = 0;
    for (const auto& ref : r.move_refs) {
        if (ref[1] != have) {
            if (!aki_file(rom, ref[1], file)) { why = "the ROM's file " + std::to_string(ref[1]) + " can't be read"; return false; }
            have = ref[1];
        }
        if (ref[2] >= file.size()) { why = "a move name's place is past its file's end"; return false; }
        const uint8_t* p = file.data() + ref[2];
        const void* z = std::memchr(p, 0, file.size() - ref[2]);
        if (!z) { why = "a move name has no end"; return false; }
        std::string name((const char*)p, (const char*)z);
        for (char& c : name) c = (char)std::tolower((unsigned char)c);   // (ASCII names: the same as Python's lower(), comm_moves_test)
        moves += std::to_string(ref[0]) + "\t" + name + "\n";
    }
    r.moves = moves;
    return true;
}

}  // namespace comm_build
