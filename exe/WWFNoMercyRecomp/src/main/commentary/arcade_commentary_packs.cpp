// [commentary2 2026-11-18] Which commentary packs the game loads -- automatically, whatever is there:
//   <game>\commentary\*.pak   every pack in the commentary folder (with its parts .pak.1, .pak.2, ... read after it)
//   <game>\commentary.pak     the old place (still works: a pack of source "wwe13")
// A pack's SOURCE is its file name up to the first '.' or '_' (wwe13.pak, wwe13_built.pak -> "wwe13"). One pack per
// source is loaded; the player's own pack always beats one the game built (the user's rule: existing content wins) --
// the game's own are the ones content\firstrun.json's "commentary" record names. Among the player's own of one source:
// the commentary folder's before the old place, then by name. Packs of different sources are all loaded, their lines
// mixed (merged into one pack in memory, each clip renumbered). One pack alone is loaded exactly as it is.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../arcade_host.h"
#include "json/json.hpp"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> read_pack(const fs::path& first) {
    std::vector<uint8_t> out;
    for (int k = 0;; k++) {
        const fs::path p = k ? fs::path(first.string() + "." + std::to_string(k)) : first;
        std::error_code ec;
        const auto size = fs::file_size(p, ec);
        if (ec) break;
        std::ifstream in(p, std::ios::binary);
        if (!in) break;
        const size_t at = out.size();
        out.resize(at + (size_t)size);
        in.read(reinterpret_cast<char*>(out.data() + at), (std::streamsize)size);
        out.resize(at + (size_t)in.gcount());
    }
    return out;
}

uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool pack_ok(const std::vector<uint8_t>& b) {
    return b.size() >= 28 && std::memcmp(b.data(), "NMCP", 4) == 0 && le32(b.data() + 4) >= 2 && le32(b.data() + 4) <= 4;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string source_of(const std::string& file_name) {
    const std::string n = lower(file_name);
    return n.substr(0, n.find_first_of("._"));
}

// several packs as one (each version-4 pack's rows renumbered past the clips before it; the first pack's moves table).
// Only packs of the same version merge; otherwise the first alone.
std::vector<uint8_t> merge(const std::vector<std::vector<uint8_t>>& packs) {
    const uint32_t ver = le32(packs[0].data() + 4);
    std::string rows, moves;
    std::vector<std::pair<const uint8_t*, uint32_t>> clips;
    for (const std::vector<uint8_t>& b : packs) {
        if (le32(b.data() + 4) != ver) continue;
        const uint32_t nd = le32(b.data() + 8), io = le32(b.data() + 16), il = le32(b.data() + 20), to = le32(b.data() + 24);
        if ((uint64_t)io + il > b.size() || (uint64_t)to + 8ull * nd > b.size()) continue;
        const std::string idx((const char*)b.data() + io, il);
        const size_t mv = idx.find("#moves\n");
        const std::string part = idx.substr(0, mv == std::string::npos ? idx.size() : mv);
        if (moves.empty() && mv != std::string::npos) moves = idx.substr(mv + 7);
        const size_t base = clips.size();
        size_t p = 0;
        while (p < part.size()) {
            size_t e = part.find('\n', p);
            if (e == std::string::npos) e = part.size();
            const std::string line = part.substr(p, e - p);
            p = e + 1;
            const size_t t = line.find('\t');
            if (t == std::string::npos || line.empty() || line[0] == '#') continue;
            rows += std::to_string(base + (size_t)std::atoi(line.substr(0, t).c_str())) + line.substr(t) + "\n";
        }
        for (uint32_t i = 0; i < nd; i++) {
            uint32_t off = le32(b.data() + to + 8 * i), len = le32(b.data() + to + 8 * i + 4);
            if ((uint64_t)off + len > b.size()) { off = 0; len = 0; }
            clips.emplace_back(b.data() + off, len);
        }
    }
    // (the pack layout of tools\commentary\compak_build.py / packbuild\comm_pack.cpp)
    const std::string index = rows + "#moves\n" + moves;
    uint32_t nrows = 0;
    for (char ch : rows) nrows += ch == '\n';
    uint32_t toff = 28 + (uint32_t)index.size();
    toff += (4 - toff % 4) % 4;
    std::vector<uint8_t> out(toff, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) out[at + (size_t)i] = (uint8_t)(v >> (8 * i)); };
    std::memcpy(out.data(), "NMCP", 4);
    put32(4, ver); put32(8, (uint32_t)clips.size()); put32(12, nrows); put32(16, 28); put32(20, (uint32_t)index.size()); put32(24, toff);
    std::memcpy(out.data() + 28, index.data(), index.size());
    uint32_t base = toff + 8 * (uint32_t)clips.size();
    out.resize(base);
    for (size_t i = 0; i < clips.size(); i++) {
        put32(toff + 8 * i, clips[i].second ? base : 0);
        put32(toff + 8 * i + 4, clips[i].second);
        base += clips[i].second;
    }
    for (const auto& c : clips) out.insert(out.end(), c.first, c.first + c.second);
    return out;
}

}  // namespace

// the packs the game itself built (content\firstrun.json "commentary": { <source>: { "pack": "<file name>" } })
std::vector<std::string> arcade_commentary_built_packs() {
    std::vector<std::string> out;
    std::ifstream in(arcade::program_folder() / "content" / "firstrun.json", std::ios::binary);
    if (!in) return out;
    try {
        const nlohmann::json rec = nlohmann::json::parse(in);
        if (rec.is_object() && rec.contains("commentary") && rec["commentary"].is_object())
            for (const auto& item : rec["commentary"].items())
                if (item.value().is_object() && item.value().contains("pack")) out.push_back(lower(item.value().value("pack", std::string())));
    } catch (const nlohmann::json::exception&) {
    }
    return out;
}

// The pack to load (empty: none), and what was chosen, for the log.
std::vector<uint8_t> arcade_commentary_find_packs(std::vector<std::string>& log) {
    struct Cand { fs::path path; std::string name, source; int rank; };   // rank: 0 player's in commentary\, 1 old place, 2 built
    std::vector<Cand> cands;
    const std::vector<std::string> built = arcade_commentary_built_packs();
    const fs::path top = arcade::program_folder(), dir = top / "commentary";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || lower(e.path().extension().string()) != ".pak") continue;
        const std::string name = e.path().filename().string();
        const bool ours = std::find(built.begin(), built.end(), lower(name)) != built.end();
        cands.push_back({ e.path(), "commentary\\" + name, source_of(name), ours ? 2 : 0 });
    }
    if (fs::is_regular_file(top / "commentary.pak", ec)) cands.push_back({ top / "commentary.pak", "commentary.pak", "wwe13", 1 });
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.source != b.source ? a.source < b.source : a.rank != b.rank ? a.rank < b.rank : lower(a.name) < lower(b.name);
    });
    std::vector<std::vector<uint8_t>> packs;
    std::string last_source = "\x01";
    for (const Cand& c : cands) {
        if (c.source == last_source) { log.push_back(c.name + ": not loaded (" + c.source + "'s lines come from the pack above)"); continue; }
        std::vector<uint8_t> b = read_pack(c.path);
        if (!pack_ok(b)) { log.push_back(c.name + ": not a commentary pack, left out"); continue; }
        last_source = c.source;
        log.push_back(c.name + ": loaded (source " + c.source + (c.rank == 2 ? ", built by the game from the disc" : "") + ")");
        packs.push_back(std::move(b));
    }
    if (packs.empty()) return {};
    if (packs.size() == 1) return std::move(packs[0]);
    return merge(packs);
}
