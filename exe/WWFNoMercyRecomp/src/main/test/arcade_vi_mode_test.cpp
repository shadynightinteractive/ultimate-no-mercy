// [relclean 2026-11-19] Test for arcade_vi_mode (the runtime's stand-in video mode read from the player's ROM).
//   arcade_vi_mode_test <ROM> [<ROM> ...]
// For each ROM: the mode read by the very function the game calls at start-up must equal, field for field and byte for byte
// of the struct, the one ultramodern's events.cpp built from its numbers until 2026-11-19 (OLD_MODE below: that code, as it
// was), and the place it was found is printed. Then the failures: a ROM with one byte of the mode changed, a ROM too short,
// and no ROM must each be refused, the mode left as it was. Exit 0 = all passed.
// Build (from the source root, the project's toolchain):
//   toolchain\llvm-mingw\bin\clang++ -std=c++20 -O2 -I exe\WWFNoMercyRecomp\src\main
//     -I exe\WWFNoMercyRecomp\lib\N64ModernRuntime\ultramodern\include -I exe\WWFNoMercyRecomp\lib\N64ModernRuntime\ultramodern\include\ultramodern
//     exe\WWFNoMercyRecomp\src\main\test\arcade_vi_mode_test.cpp exe\WWFNoMercyRecomp\src\main\arcade_vi_mode.cpp
//     -o <scratch>\arcade_vi_mode_test.exe
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "arcade_vi_mode.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); failures++; } } while (0)

// events.cpp's dummy_mode until 2026-11-19 (N64ModernRuntime's code, its flag names spelled out as their values)
static OSViMode old_mode() {
    OSViMode ret{};
    ret.type = 2;
    ret.comRegs.ctrl = 0x00002 | 0x00004 | 0x00008 | 0x00010 | 0x00100 | 0x03000;
    ret.comRegs.width = 0x140;
    ret.comRegs.burst = 0x03E52239;
    ret.comRegs.vSync = 0x20D;
    ret.comRegs.hSync = 0xC15;
    ret.comRegs.leap = 0x0C150C15;
    ret.comRegs.hStart = 0x006C02EC;
    ret.comRegs.xScale = 0x200;
    ret.comRegs.vCurrent = 0x0;
    for (int field = 0; field < 2; field++) {
        ret.fldRegs[field].origin = 0x280;
        ret.fldRegs[field].yScale = 0x400;
        ret.fldRegs[field].vStart = 0x2501FF;
        ret.fldRegs[field].vBurst = 0xE0204;
        ret.fldRegs[field].vIntr = 0x2;
    }
    return ret;
}

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static void to_z64(std::vector<uint8_t>& r) {
    if (r.size() < 4) return;
    if (r[0] == 0x37 && r[1] == 0x80) for (size_t i = 0; i + 1 < r.size(); i += 2) std::swap(r[i], r[i + 1]);
    else if (r[0] == 0x40 && r[1] == 0x12) for (size_t i = 0; i + 3 < r.size(); i += 4) { std::swap(r[i], r[i + 3]); std::swap(r[i + 1], r[i + 2]); }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: arcade_vi_mode_test <ROM> [<ROM> ...]\n"); return 2; }
    const OSViMode want = old_mode();
    for (int k = 1; k < argc; k++) {
        std::vector<uint8_t> rom = read_file(argv[k]);
        to_z64(rom);
        OSViMode got;
        std::memset(&got, 0x55, sizeof got);
        std::string why; uint32_t at = 0;
        const bool ok = arcade_vi_mode_from_rom(rom.data(), rom.size(), got, why, &at);
        CHECK(ok, "ROM %d: not read: %s", k, why.c_str());
        if (!ok) continue;
        const bool same = std::memcmp(&got, &want, sizeof got) == 0;
        CHECK(same, "ROM %d: the mode read is not the one events.cpp had", k);
        std::printf("ROM %d: the mode from 0x%X: %s the old struct (%zu bytes)\n", k, at, same ? "byte for byte" : "NOT", sizeof got);
        // One byte of the mode changed: refused, the mode untouched.
        std::vector<uint8_t> bad = rom;
        bad[at + 0x0D] ^= 0x01;
        OSViMode keep;
        std::memset(&keep, 0x55, sizeof keep);
        OSViMode probe = keep;
        CHECK(!arcade_vi_mode_from_rom(bad.data(), bad.size(), probe, why), "ROM %d with one mode byte changed was accepted", k);
        CHECK(std::memcmp(&probe, &keep, sizeof probe) == 0, "ROM %d: a refused ROM changed the mode", k);
        std::printf("ROM %d, one mode byte changed: refused (%s)\n", k, why.c_str());
        CHECK(!arcade_vi_mode_from_rom(rom.data(), 0x54000, probe, why), "a ROM cut short was accepted");
    }
    std::string why; OSViMode probe{};
    CHECK(!arcade_vi_mode_from_rom(nullptr, 0, probe, why), "no ROM was accepted");
    std::printf("no ROM: refused (%s)\n", why.c_str());
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
