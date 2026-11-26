// ri_wmroster.cpp (phase3 p3folders, 2026-11-19): WrestleMania 2000's roster records -- see ri_wmroster.h.
#include "ri_wmroster.h"

namespace ri {

static constexpr uint32_t MAIN_VRAM = 0x80000400, MAIN_ROM = 0x1000, MAIN_END = 0x144AA0;   // (wm2000\game.py)
static constexpr uint32_t ROSTER = 0x800411B0, ROSTER_N = 0x4C;

Bytes wm_main_ram(const Bytes& rom) {
    if (rom.size() < MAIN_END) fail("wm_main_ram: the ROM is too short");
    Bytes img(0x800000);
    std::memcpy(img.data() + (MAIN_VRAM & 0x7FFFFF), rom.data() + MAIN_ROM, MAIN_END - MAIN_ROM);
    return img;
}

std::vector<WmRecord> wm_roster(const Bytes& ram) {
    auto r32 = [&](uint32_t a) { return be32(ram, a & 0x7FFFFF); };
    auto r16 = [&](uint32_t a) { return (int)be16(ram, a & 0x7FFFFF); };
    std::vector<WmRecord> out;
    for (uint32_t k = 0; k < ROSTER_N; k++) {
        uint32_t rec = r32(ROSTER + 4 * k);
        WmRecord e;
        e.k = (int)k;
        uint32_t a = r32(rec) & 0x7FFFFF;                    // (BaddBlood.cstr: up to the first 0 byte)
        while (ram[a]) e.raw_name += (char)ram[a++];
        e.id = r16(rec + 4); e.ord = r16(rec + 6); e.height = r16(rec + 8); e.weight = r16(rec + 10);
        e.moves = r16(rec + 12); e.abilities = r16(rec + 14);
        e.music = ram[(rec + 16) & 0x7FFFFF]; e.video = ram[(rec + 17) & 0x7FFFFF];
        for (int i = 0; i < 4; i++) {
            uint32_t l = r32(rec + 0x14 + 4 * i) & 0x7FFFFF;
            e.looks[i] = Bytes(ram.begin() + l, ram.begin() + l + 0x24);
        }
        out.push_back(std::move(e));
    }
    return out;
}

// Python's str.strip() for the latin-1 text these names are (whitespace: ASCII's, 0x1C-0x1F, 0x85, 0xA0)
static std::string pystrip(const std::string& s) {
    auto ws = [](unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0; };
    size_t a = 0, b = s.size();
    while (a < b && ws((unsigned char)s[a])) a++;
    while (b > a && ws((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

void wm_names(const std::string& raw, std::string& full, std::string& short_name) {
    // full: re.sub(r"\{[^}]*\}", "", raw) with the angle brackets dropped
    std::string f;
    for (size_t i = 0; i < raw.size();) {
        size_t j;
        if (raw[i] == '{' && (j = raw.find('}', i + 1)) != std::string::npos) { i = j + 1; continue; }
        if (raw[i] != '<' && raw[i] != '>') f += raw[i];
        i++;
    }
    full = pystrip(f);
    // short: re.findall(r"\{([^}]*)\}|<([^>]*)>", raw) joined (the first alternative tried first at each place)
    std::string s;
    for (size_t i = 0; i < raw.size();) {
        size_t j;
        if (raw[i] == '{' && (j = raw.find('}', i + 1)) != std::string::npos) { s += raw.substr(i + 1, j - i - 1); i = j + 1; continue; }
        if (raw[i] == '<' && (j = raw.find('>', i + 1)) != std::string::npos) { s += raw.substr(i + 1, j - i - 1); i = j + 1; continue; }
        i++;
    }
    short_name = pystrip(s);
    if (short_name.empty()) short_name = full;
}

}  // namespace ri
