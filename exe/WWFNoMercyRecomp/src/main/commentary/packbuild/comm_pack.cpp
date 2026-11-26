// [commentary2 2026-11-18] See comm_pack.h.
#include "comm_pack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "json/json.hpp"

namespace comm_build {

// ---- SHA-1 (FIPS 180-4), for the clips' and the pack's fingerprints ------------------------------------------------
Sha1 sha1(const uint8_t* p, size_t n) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    auto rol = [](uint32_t x, int k) { return (x << k) | (x >> (32 - k)); };
    auto block = [&](const uint8_t* b) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999u; }
            else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(bb, 30); bb = a; a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) block(p + i);
    uint8_t last[128] = {};
    const size_t rest = n - i;
    std::memcpy(last, p + i, rest);
    last[rest] = 0x80;
    const size_t total = rest + 9 <= 64 ? 64 : 128;
    const uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) last[total - 1 - k] = (uint8_t)(bits >> (8 * k));
    block(last);
    if (total == 128) block(last + 64);
    Sha1 out;
    for (int k = 0; k < 5; k++)
        for (int j = 0; j < 4; j++) out[(size_t)(4 * k + j)] = (uint8_t)(h[k] >> (24 - 8 * j));
    return out;
}

std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

// ---- the recipe -------------------------------------------------------------------------------------------------
namespace {

bool read_text(const std::filesystem::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::vector<std::string> tabs(const std::string& line) {
    std::vector<std::string> f;
    size_t q = 0;
    for (;;) {
        const size_t t = line.find('\t', q);
        f.push_back(line.substr(q, t == std::string::npos ? std::string::npos : t - q));
        if (t == std::string::npos) break;
        q = t + 1;
    }
    return f;
}

bool unhex(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        char* end = nullptr;
        const std::string b = s.substr(2 * i, 2);
        const unsigned long v = std::strtoul(b.c_str(), &end, 16);
        if (*end) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

}  // namespace

bool load_recipe(const std::filesystem::path& dir, Recipe& r, std::string& why) {
    std::string js, clips;
    std::string refs;
    if (!read_text(dir / "recipe.json", js) || !read_text(dir / "clips.tsv", clips) || !read_text(dir / "lines.tsv", r.lines) ||
        !read_text(dir / "moves_refs.tsv", refs)) {
        why = "the recipe's files aren't all there (recipe.json, clips.tsv, lines.tsv, moves_refs.tsv)";
        return false;
    }
    read_text(dir / "moves.tsv", r.moves);   // (a developer's own table, never shipped; else made from the ROM)
    {
        std::istringstream rs(refs);
        std::string l;
        while (std::getline(rs, l)) {
            if (l.empty() || l[0] == '#') continue;
            const std::vector<std::string> f = tabs(l);
            if (f.size() != 3) { why = "moves_refs.tsv: a bad row"; return false; }
            r.move_refs.push_back({ (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10), (uint32_t)std::strtoul(f[1].c_str(), nullptr, 10),
                                    (uint32_t)std::strtoul(f[2].c_str(), nullptr, 10) });
        }
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(js);
        r.name = j.value("name", std::string());
        r.source = j.value("source", std::string());
        r.pack = j.value("pack", std::string());
        r.rate = j.value("rate", 20000);
        r.target_sha1 = j.value("target_sha1", std::string());
        r.old_target_sha1 = j.value("old_target_sha1", std::string());
        const std::string c = j.value("comment_packet", std::string());
        r.comment.assign(c.size() / 2, 0);
        if (c.empty() || !unhex(c, r.comment.data(), r.comment.size())) { why = "recipe.json: bad comment_packet"; return false; }
    } catch (const nlohmann::json::exception&) {
        why = "recipe.json isn't valid JSON";
        return false;
    }
    if (r.source.empty() || r.pack.empty()) { why = "recipe.json: no source / pack name"; return false; }
    std::istringstream in(clips);
    std::string line;
    std::vector<std::string> head;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> f = tabs(line);
        if (head.empty()) { head = f; continue; }
        if (f.size() != head.size()) { why = "clips.tsv: a row with the wrong number of columns"; return false; }
        auto col = [&](const char* name) -> const std::string& {
            static const std::string none = "-";
            for (size_t i = 0; i < head.size(); i++) if (head[i] == name) return f[i];
            return none;
        };
        RecipeClip c;
        c.clip = std::atoi(col("clip").c_str());
        const std::string& kind = col("kind");
        const std::string& src = col("source");
        if (kind == "w13") {
            c.media = (uint32_t)std::strtoul(src.c_str(), nullptr, 10);
        } else if (kind == "cut") {
            c.cut = true;
            if (src.rfind("w13:", 0) == 0) c.media = (uint32_t)std::strtoul(src.c_str() + 4, nullptr, 10);
            c.placed = c.media != 0 && col("start") != "?" && col("gain") != "?";
            if (c.placed) {
                c.start = std::atoll(col("start").c_str());
                c.length = std::atoll(col("length").c_str());
                c.gain = std::strtod(col("gain").c_str(), nullptr);
                c.fade_in = std::atoll(col("fade_in").c_str());
                c.fade_out = std::atoll(col("fade_out").c_str());
            }
        } else {
            why = "clips.tsv: unknown kind " + kind;
            return false;
        }
        c.samples = std::atoll(col("samples").c_str());
        c.rms = std::strtod(col("rms").c_str(), nullptr);
        c.serial = (uint32_t)std::strtoul(col("serial").c_str(), nullptr, 10);
        if (!unhex(col("sha1"), c.sha.data(), c.sha.size())) { why = "clips.tsv: bad sha1"; return false; }
        if (col("disc_sha1") != "-") {
            if (!unhex(col("disc_sha1"), c.disc_sha.data(), c.disc_sha.size())) { why = "clips.tsv: bad disc_sha1"; return false; }
            c.has_disc_sha = true;
        }
        if (c.clip != (int)r.clips.size()) { why = "clips.tsv: clips out of order"; return false; }
        r.clips.push_back(c);
    }
    if (r.clips.empty()) { why = "clips.tsv has no clips"; return false; }
    return true;
}

// ---- the pack (compak_build.py build: "NMCP", version 4, clips, rows, header 28, index; the clip table 4-aligned) ----
std::vector<uint8_t> pack_build(const std::string& rows, const std::string& moves, const std::vector<std::vector<uint8_t>>& clips) {
    const std::string index = rows + "#moves\n" + moves;
    uint32_t nrows = 0;
    for (char ch : rows) nrows += ch == '\n';
    const uint32_t head = 28;
    uint32_t toff = head + (uint32_t)index.size();
    toff += (4 - toff % 4) % 4;
    std::vector<uint8_t> out(toff, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) out[at + (size_t)i] = (uint8_t)(v >> (8 * i)); };
    std::memcpy(out.data(), "NMCP", 4);
    put32(4, 4);
    put32(8, (uint32_t)clips.size());
    put32(12, nrows);
    put32(16, head);
    put32(20, (uint32_t)index.size());
    put32(24, toff);
    std::memcpy(out.data() + head, index.data(), index.size());
    uint32_t base = toff + 8 * (uint32_t)clips.size();
    out.resize(base);
    for (size_t i = 0; i < clips.size(); i++) {
        put32(toff + 8 * i, base);
        put32(toff + 8 * i + 4, (uint32_t)clips[i].size());
        base += (uint32_t)clips[i].size();
    }
    for (const std::vector<uint8_t>& c : clips) out.insert(out.end(), c.begin(), c.end());
    return out;
}

std::vector<uint8_t> pack_from_recipe(const Recipe& r, const std::vector<std::vector<uint8_t>>& have, size_t& lines_kept) {
    std::vector<int> renum(r.clips.size(), -1);
    std::vector<std::vector<uint8_t>> clips;
    for (size_t i = 0; i < r.clips.size() && i < have.size(); i++)
        if (!have[i].empty()) { renum[i] = (int)clips.size(); clips.push_back(have[i]); }
    std::string rows;
    lines_kept = 0;
    size_t p = 0;
    while (p < r.lines.size()) {
        size_t e = r.lines.find('\n', p);
        if (e == std::string::npos) e = r.lines.size();
        const std::string line = r.lines.substr(p, e - p);
        p = e + 1;
        const size_t t = line.find('\t');
        if (t == std::string::npos) continue;
        const int c = std::atoi(line.substr(0, t).c_str());
        if (c < 0 || c >= (int)renum.size() || renum[(size_t)c] < 0) continue;
        rows += std::to_string(renum[(size_t)c]) + line.substr(t) + "\n";
        lines_kept++;
    }
    return pack_build(rows, r.moves, clips);
}

}  // namespace comm_build
