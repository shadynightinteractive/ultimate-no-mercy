// ri_data.cpp (phase3, 2026-11-19): make_data.py's names / rev_movesets (ri_data.h).
#include "ri_data.h"
#include "json/json.hpp"

namespace ri {

// a text in a game file: from its offset to the next zero byte, latin-1 (made UTF-8 here)
static std::string text_at(const RomFiles& F, uint32_t f, uint32_t o) {
    const Bytes& d = F(f);
    std::string s;
    for (size_t i = o; i < d.size() && d[i]; i++) {
        uint8_t c = d[i];
        if (c < 0x80) s += (char)c;
        else { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}

MoveNames move_names(Roms& R, const fs::path& ix_path) {
    Bytes t = read_file(ix_path);
    auto ix = nlohmann::ordered_json::parse(std::string(t.begin(), t.end()));
    MoveNames out;
    for (const char* game : { "nomercy", "nmplus_v3" }) {
        const RomFiles* F = R.files(game);
        if (!F) continue;                                  // (No Mercy Plus: only when the player has it)
        auto& dst = std::string(game) == "nomercy" ? out.nomercy : out.nmplus;
        for (auto& e : ix[game]) {
            int key = e[0].get<int>();
            if (e[1].is_null()) dst.push_back({ key, e[2].get<std::string>() });      // (a literal: ours)
            else dst.push_back({ key, text_at(*F, e[1].get<uint32_t>(), e[2].get<uint32_t>()) });
        }
    }
    return out;
}

// The game's table of group arrays (u16 file numbers, each array 4-byte aligned, its padding a 0) whose pointers are at ROM
// 0x39664 (0-terminated; RAM = ROM + 0x7FFFF400); a group runs to the next one's start (the last to the pointer table).
RevMovesets rev_movesets(Roms& R) {
    auto rv = R.rom("revenge");
    if (!rv) fail("the Revenge ROM isn't there");
    const RomFiles& RF = *R.files("revenge");
    const uint32_t PT = 0x39664;
    std::vector<uint32_t> ptrs;
    while (uint32_t p = be32(rv->data() + PT + 4 * ptrs.size())) ptrs.push_back(p - 0x7FFFF400);
    RevMovesets out;
    for (size_t g = 0; g < ptrs.size(); g++) {
        uint32_t s = ptrs[g], e = g + 1 < ptrs.size() ? ptrs[g + 1] : PT;
        std::vector<uint32_t> fs;
        for (uint32_t a = s; a + 2 <= e; a += 2) fs.push_back(be16(rv->data() + a));
        while (!fs.empty() && fs.back() == 0) fs.pop_back();
        for (size_t r = 0; r < fs.size(); r++) out.rows.push_back({ (int)g, (int)r, (int)fs[r] });
    }
    for (auto& row : out.rows) {
        const Bytes& d = RF(row.file + 2);
        if (d.size() < 763 * 2) fail("Revenge moveset file 0x%X is short", row.file + 2);
        std::vector<uint16_t> l(763);
        for (int i = 0; i < 763; i++) l[i] = be16(&d[2 * i]);
        out.lists[{ row.group, row.row }] = std::move(l);
    }
    return out;
}

}  // namespace ri
