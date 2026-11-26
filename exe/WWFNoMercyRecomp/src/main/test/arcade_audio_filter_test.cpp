// [audiotable 2026-11-18] Test for arcade_audio_filter (the sound resampler's filter read from the player's ROM).
//   arcade_audio_filter_test <old arcade_audio_table.h> <ROM> [<ROM> ...]
// For each ROM: the filter read by the very function the game calls at start-up must equal, number for number, the 256
// numbers of the header the game used to carry (from git history: git show 57cd65a:exe/WWFNoMercyRecomp/src/main/
// arcade_audio_table.h -- kept out of the tree), and the place it was found is printed. Then the failures: a ROM with one
// byte of the filter changed, a ROM too short, and no ROM must each be refused (no filter of zeros). Exit 0 = all passed.
// Build (from the source root, the project's toolchain):
//   toolchain\llvm-mingw\bin\clang++ -std=c++20 -O2 -I exe\WWFNoMercyRecomp\src\main
//     exe\WWFNoMercyRecomp\src\main\test\arcade_audio_filter_test.cpp exe\WWFNoMercyRecomp\src\main\arcade_audio_filter.cpp
//     -o <scratch>\arcade_audio_filter_test.exe
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "arcade_audio_filter.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); failures++; } } while (0)

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The numbers between the header's { and }: signed decimals.
static std::vector<int> header_numbers(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss; ss << in.rdbuf();
    const std::string s = ss.str();
    const size_t a = s.find('{'), b = s.find('}', a);
    std::vector<int> v;
    if (a == std::string::npos || b == std::string::npos) return v;
    for (size_t i = a + 1; i < b;) {
        if (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')) { char* end; v.push_back(int(std::strtol(s.c_str() + i, &end, 10))); i = size_t(end - s.c_str()); }
        else i++;
    }
    return v;
}

// The .z64 byte order from .v64 (byte-swapped) or .n64 (little-endian), as the runtime holds a ROM.
static void to_z64(std::vector<uint8_t>& r) {
    if (r.size() < 4) return;
    if (r[0] == 0x37 && r[1] == 0x80) for (size_t i = 0; i + 1 < r.size(); i += 2) std::swap(r[i], r[i + 1]);
    else if (r[0] == 0x40 && r[1] == 0x12) for (size_t i = 0; i + 3 < r.size(); i += 4) { std::swap(r[i], r[i + 3]); std::swap(r[i + 1], r[i + 2]); }
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: arcade_audio_filter_test <old arcade_audio_table.h> <ROM> [<ROM> ...]\n"); return 2; }
    const std::vector<int> old = header_numbers(argv[1]);
    CHECK(old.size() == 256, "the old header has %zu numbers, not 256", old.size());
    if (old.size() != 256) return 1;
    for (int k = 2; k < argc; k++) {
        std::vector<uint8_t> rom = read_file(argv[k]);
        to_z64(rom);
        std::memset(arcade_audio_filter, 0, sizeof arcade_audio_filter);
        std::string why; uint32_t at = 0;
        const bool ok = arcade_audio_filter_from_rom(rom.data(), rom.size(), why, &at);
        CHECK(ok, "ROM %d: not read: %s", k - 1, why.c_str());
        if (!ok) continue;
        int same = 0;
        for (int i = 0; i < 256; i++) same += arcade_audio_filter[i] == old[size_t(i)];
        CHECK(same == 256, "ROM %d: %d of 256 numbers equal the old header's", k - 1, same);
        // (and exactly one place in the ROM holds those 512 bytes)
        uint8_t want[512];
        for (int i = 0; i < 256; i++) { want[2 * i] = uint8_t(uint16_t(old[size_t(i)]) >> 8); want[2 * i + 1] = uint8_t(old[size_t(i)]); }
        int places = 0;
        for (size_t o = 0; o + 512 <= rom.size(); o++) if (rom[o] == want[0] && std::memcmp(rom.data() + o, want, 512) == 0) places++;
        CHECK(places == 1, "ROM %d: the 512 bytes are at %d places, not 1", k - 1, places);
        std::printf("ROM %d: the filter from 0x%X: %d of 256 numbers equal the old header's; the bytes at %d place(s) in the ROM\n", k - 1, at, same, places);

        // One byte of the filter changed: refused, the filter untouched.
        std::vector<uint8_t> bad = rom;
        bad[at + 0x101] ^= 0x01;
        std::memset(arcade_audio_filter, 0x55, sizeof arcade_audio_filter);
        CHECK(!arcade_audio_filter_from_rom(bad.data(), bad.size(), why), "ROM %d with one filter byte changed was accepted", k - 1);
        bool untouched = true;
        for (int16_t v : arcade_audio_filter) untouched &= v == 0x5555;
        CHECK(untouched, "ROM %d: a refused ROM changed the filter", k - 1);
        std::printf("ROM %d, one filter byte changed: refused (%s)\n", k - 1, why.c_str());
        // Too short for either place: refused.
        CHECK(!arcade_audio_filter_from_rom(rom.data(), 0x55000, why), "a ROM cut short was accepted");
    }
    std::string why;
    CHECK(!arcade_audio_filter_from_rom(nullptr, 0, why), "no ROM was accepted");
    std::printf("no ROM: refused (%s)\n", why.c_str());
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
