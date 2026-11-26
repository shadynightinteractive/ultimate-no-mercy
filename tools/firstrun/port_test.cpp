// port_test.cpp: the first start's walls and ringstage ports (exe src/main/arcade_firstrun_ports.cpp) run against the
// mod's build, table by table (firstrun, 2026-11-17). tools/firstrun/port_test.ps1 builds and runs it.
//
//   port_test <root> <symbols>    root: the worktree's top (default "."); symbols: tools/firstrun/port_syms.py's list
//
// From <root>: the ROM (roms/WWF No Mercy (USA) (Rev 1).z64), made six-man by six_rompatch.h's words; its files (AKI LZSS,
// as arcade_firstrun.cpp unpacks them); the mod's made files (ids 0x5000 up: mod/build/wmport.bin by mod/src/wmport_files.h's
// table); the mod's image (mod/build/mod_six/mod_binary.bin). The ports' tables are zeroed in a copy of the image, the
// ports run (twice: the same bytes each time), and each table is compared with the build's.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "arcade_firstrun_ports.h"
#include "six_rompatch.h"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

uint32_t be32(const Bytes& b, size_t at) { return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]; }

// AKI's LZSS (a copy of arcade_firstrun.cpp lzss).
Bytes lzss(const uint8_t* src, size_t size, size_t n) {
    Bytes out(n, 0);
    uint8_t ring[0x1000] = {};
    size_t r = 0xFEE, i = 4, o = 0;
    unsigned flags = 0;
    while (o < n && i < size) {
        flags >>= 1;
        if (!(flags & 0x100)) flags = src[i++] | 0xFF00u;
        if (flags & 1) {
            if (i >= size) break;
            const uint8_t c = src[i++];
            out[o++] = c;
            ring[r] = c;
            r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= size) break;
            const unsigned lo = src[i], hi = src[i + 1];
            i += 2;
            const unsigned pos = lo | ((hi & 0xF0u) << 4), len = (hi & 0xFu) + 3;
            for (unsigned k = 0; k < len; k++) {
                const uint8_t c = ring[(pos + k) & 0xFFF];
                if (o < n) out[o++] = c;
                ring[r] = c;
                r = (r + 1) & 0xFFF;
            }
        }
    }
    return out;
}

// File i of No Mercy (Rev 1)'s file table, unpacked (a copy of arcade_firstrun.cpp aki_file).
Bytes aki_file(const Bytes& rom, uint32_t i) {
    constexpr uint32_t TABLE = 0x16C31D8, ENTRIES = 19459, BASE = 0x1BD150;
    if (i < 1 || i >= ENTRIES) return {};
    const uint32_t t0 = be32(rom, TABLE + 4 * (i - 1)), t1 = be32(rom, TABLE + 4 * i);
    const size_t a = std::min<size_t>((t0 & ~1u) + size_t(BASE), rom.size()), b = std::min<size_t>((t1 & ~1u) + size_t(BASE), rom.size());
    if (b <= a) return {};
    if ((t0 & 1) && b - a >= 4) {
        const uint32_t n = be32(rom, a);
        if (n <= 0x400000u) return lzss(rom.data() + a, b - a, n);
    }
    return Bytes(rom.begin() + a, rom.begin() + b);
}

// mod/src/wmport_files.h's table: {offset in the pack, size} of file 0x5000 + i.
std::vector<std::pair<uint32_t, uint32_t>> pack_table(const std::string& header) {
    std::ifstream f(header);
    std::string line;
    std::vector<std::pair<uint32_t, uint32_t>> out;
    bool in_table = false;
    while (std::getline(f, line)) {
        if (line.find("wmp_files[WMP_FILES_N][2]") != std::string::npos) { in_table = true; continue; }
        if (!in_table) continue;
        if (line.rfind("};", 0) == 0) break;
        unsigned off = 0, size = 0;
        if (std::sscanf(line.c_str(), " { 0x%x, %u }", &off, &size) == 2) out.push_back({off, size});
    }
    return out;
}

// Which field of a table a byte is in (for a difference).
std::string field_of(const std::string& table, uint32_t off) {
    char text[64];
    if (table.rfind("xw", 0) == 0) {
        const uint32_t r = off / 0x30, o = off % 0x30;
        if (o < 0x18) std::snprintf(text, sizeof(text), "record %u c[%u]", r, o / 2);
        else if (o < 0x24) std::snprintf(text, sizeof(text), "record %u n[%u]", r, (o - 0x18) / 4);
        else {
            const char* names[] = {"id", "fl", "kind", "pad", "next", "next"};
            std::snprintf(text, sizeof(text), "record %u %s", r, names[(o - 0x24) / 2]);
        }
    } else if (table == "xrs_arenas") {
        const uint32_t e = off / 28, o = off % 28;
        const char* names[] = {"x0", "z0", "w", "h", "cells", "cells", "front", "own_y", "ax", "az", "qx", "qz", "crowd_z", "crowd_y"};
        std::snprintf(text, sizeof(text), "entry %u %s", e, names[o / 2]);
    } else {
        std::snprintf(text, sizeof(text), "cell %u", off);
    }
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string syms_path = argc > 2 ? argv[2] : root + "/mod/build/mod_six/port_syms.txt";
    Bytes rom = read_all(root + "/roms/WWF No Mercy (USA) (Rev 1).z64");
    Bytes pack = read_all(root + "/mod/build/wmport.bin");
    const Bytes built = read_all(root + "/mod/build/mod_six/mod_binary.bin");
    const auto table = pack_table(root + "/mod/src/wmport_files.h");
    if (rom.size() < 0x2000000 || pack.empty() || built.empty() || table.empty()) {
        std::printf("missing input (ROM %zu, wmport.bin %zu, mod_binary.bin %zu, pack table %zu)\n", rom.size(), pack.size(), built.size(), table.size());
        return 2;
    }
    // the six-man ROM: the engine's words (main.cpp six_engine_init), big-endian
    Bytes six = rom;
    for (const auto& w : six_rom_patch) {
        six[w[0]] = uint8_t(w[1] >> 24);
        six[w[0] + 1] = uint8_t(w[1] >> 16);
        six[w[0] + 2] = uint8_t(w[1] >> 8);
        six[w[0] + 3] = uint8_t(w[1]);
    }
    std::map<uint32_t, Bytes> files;
    const Bytes none;
    firstrun::Rom r;
    r.rom = rom;
    r.six = six;
    r.file = [&](uint32_t i) -> const Bytes& {
        auto at = files.find(i);
        if (at != files.end()) return at->second;
        Bytes b;
        if (i >= 0x5000) {
            if (i - 0x5000 < table.size()) {
                const auto [off, size] = table[i - 0x5000];
                if (size_t(off) + size <= pack.size()) b.assign(pack.begin() + off, pack.begin() + off + size);
            }
        } else {
            b = aki_file(rom, i);
        }
        return files.emplace(i, std::move(b)).first->second;
    };
    // the symbols: each port's own tables, and everything (as the plan's `all`)
    std::vector<firstrun::Table> all, walls, ringstage;
    {
        std::ifstream f(syms_path);
        std::string name;
        std::string addr;
        uint32_t size;
        while (f >> name >> addr >> size) {
            firstrun::Table t{name, uint32_t(std::stoul(addr, nullptr, 16)), size};
            if (name == "xrs_arenas") t.size = 10 * 28;   // (its first ten entries: the game's arenas)
            all.push_back(t);
            if (name.size() == 7 && name.rfind("xw", 0) == 0 && name.substr(3) == "_rec") walls.push_back(t);
            if ((name.rfind("xrs", 0) == 0 && name.find("_cells") != std::string::npos) || name == "xrs_arenas") ringstage.push_back(t);
        }
    }
    if (walls.size() != 10 || ringstage.size() != 11) {
        std::printf("symbols: %zu walls tables, %zu ringstage tables (want 10, 11)\n", walls.size(), ringstage.size());
        return 2;
    }
    Bytes first;
    int failures = 0;
    for (int round = 0; round < 2; round++) {
        Bytes image = built;
        for (const auto* list : {&walls, &ringstage})
            for (const auto& t : *list) std::memset(image.data() + (t.address - 0x81000000u), 0, t.size);
        for (const auto& [port, mine] : {std::pair{std::string("walls"), &walls}, std::pair{std::string("ringstage"), &ringstage}}) {
            std::string why;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = firstrun::run_port(port, r, *mine, all, image, why);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("run %d: port %-9s %s%s (%.0f ms)\n", round + 1, port.c_str(), ok ? "true" : "FALSE: ", ok ? "" : why.c_str(), ms);
            if (!ok) failures++;
        }
        if (round == 0) {
            for (const auto* list : {&walls, &ringstage})
                for (const auto& t : *list) {
                    const size_t at = t.address - 0x81000000u;
                    size_t diff = t.size;
                    for (size_t i = 0; i < t.size; i++)
                        if (image[at + i] != built[at + i]) { diff = i; break; }
                    if (diff == t.size) {
                        std::printf("  %-12s %5u bytes SAME\n", t.name.c_str(), t.size);
                    } else {
                        failures++;
                        size_t n_diff = 0;
                        for (size_t i = 0; i < t.size; i++) n_diff += image[at + i] != built[at + i];
                        std::printf("  %-12s %5u bytes DIFFERENT (%zu bytes) from +0x%zX (%s): made %02X, build %02X\n", t.name.c_str(), t.size, n_diff,
                                    diff, field_of(t.name, uint32_t(diff)).c_str(), image[at + diff], built[at + diff]);
                    }
                }
            {
                std::string why;
                std::printf("  port labels: %s\n", firstrun::run_port("labels", r, {}, all, image, why) ? "true" : ("false (" + why + ")").c_str());
            }
            first = image;
        } else {
            const bool same = image == first;
            std::printf("  second run: %s\n", same ? "the same bytes" : "DIFFERENT bytes");
            if (!same) failures++;
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "ALL SAME");
    return failures ? 1 : 0;
}
