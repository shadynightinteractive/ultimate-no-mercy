// The first start's "labels" port: the arcade's menu lettering, cut from the game's own labels again, from the player's ROM
// (firstrun, 2026-11-17; arcade_firstrun_ports.h says what a port is).
//
//   label_custom_story, label_versus   the championship list's "Custom Story" and "Versus" rows: mod/gen/labeltex.py's make(),
//                                      the same steps -- each letter's columns cut from a menu label, set on a 128 x 32 CI4
//                                      with "World Heavyweight"'s palette (each colour to the nearest in brightness), an outline
//                                      round the whole word.
//   label_lumberjack                   the Lumberjack Match row (ljround8): labeltex.py's make_squeezed, the same steps.
//   label_lj_count, label_lj_pick      the Lumberjack MATCH page's settings rows (ljmenu10): "Lumberjacks: 2" .. "Lumberjacks:
//                                      30" and "Picked: Random" / "Picked: Select" -- make_squeezed again, with more letters
//                                      cut from the labels and the NUMBER page's figures (file 0x4588) set at the labels'
//                                      capital height (labeltex.py figure), on Single Match's palette (file 0x44A1).
//   hdr_versus                         the Multi Play sub-header "exhibition" re-cut to read "versus" (logic.c header_versus):
//                                      v from "pay per view", e r from "per", s u s from "guest referee" (files 0x4498, 0x449A),
//                                      each colour to the nearest of the "exhibition" palette's (file 0x4496); where two
//                                      letters meet, each pixel is one of the two letters' or blank. Its maker wasn't kept;
//                                      this is how its bytes are made, found from them and checked against them (the build's).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "arcade_firstrun_ports.h"

// (the bytes must be the Python's: no multiply-add fused into one rounding, on any CPU -- ljmenu10)
#pragma clang fp contract(off)

namespace firstrun {
namespace {

// A CI4 TEX file of the game: "TEX\0", width, height (u16 at 4, 6), the palette's size (u16 at 10), the palette (RGBA5551
// from 16), then the pixels, two to a byte, the first in the high half.
struct Tex {
    int w = 0, h = 0;
    std::vector<uint16_t> pal;
    std::vector<uint8_t> px;           // (one index a pixel, row by row)
    std::vector<uint8_t> head;         // (the header and palette as they are: 16 + 2 x the palette's size)
};
bool load(const std::vector<uint8_t>& b, Tex& t) {
    if (b.size() < 16 || memcmp(b.data(), "TEX\0", 4)) return false;
    t.w = b[4] << 8 | b[5]; t.h = b[6] << 8 | b[7];
    const int npal = b[10] << 8 | b[11];
    const size_t at = 16 + 2 * size_t(npal);
    if (b.size() < at + size_t(t.w) * t.h / 2) return false;
    for (int i = 0; i < npal; i++) t.pal.push_back(uint16_t(b[16 + 2 * i] << 8 | b[17 + 2 * i]));
    t.head.assign(b.begin(), b.begin() + std::ptrdiff_t(at));
    t.px.resize(size_t(t.w) * t.h);
    for (size_t i = 0; i < t.px.size(); i += 2) { t.px[i] = b[at + i / 2] >> 4; t.px[i + 1] = b[at + i / 2] & 15; }
    return true;
}
int lum(uint16_t c) { return ((c >> 11) & 31) + ((c >> 6) & 31) + ((c >> 1) & 31); }
int sq(int v) { return v * v; }
int rgb_distance(uint16_t a, uint16_t b) { return sq(((a >> 11) & 31) - ((b >> 11) & 31)) + sq(((a >> 6) & 31) - ((b >> 6) & 31)) + sq(((a >> 1) & 31) - ((b >> 1) & 31)); }

// ---- labeltex.py make(text): 128 x 32, "World Heavyweight"'s palette (file 0x4617)
struct Glyph { char c; uint32_t file; int x0, x1; };
const Glyph GLYPHS[] = { { 'C', 0x448E, 5, 15 }, { 'm', 0x448E, 36, 50 }, { 'o', 0x448E, 68, 76 }, { 'u', 0x448B, 17, 27 },
                         { 's', 0x448B, 37, 46 }, { 't', 0x448B, 46, 53 }, { 'S', 0x448D, 14, 23 }, { 'r', 0x4491, 55, 62 },
                         { 'y', 0x4488, 28, 38 }, { 'V', 0x4489, 79, 90 }, { 'e', 0x448B, 28, 37 } };
constexpr int SPACE = 5, LW = 128, LH = 32;

bool make_label(const Rom& rom, const char* text, std::vector<uint8_t>& out, std::string& why) {
    Tex base;
    if (!load(rom.file(0x4617), base) || base.pal.size() < 16) { why = "file 0x4617 isn't the label it should be"; return false; }
    std::array<int, 16> target{};
    for (int j = 0; j < 16; j++) target[j] = lum(base.pal[j]);
    // the word: each letter's body (its pixels 2..15; 0 and 1 -- clear and outline -- left out), its colours mapped, a
    // column after each letter (the outline's; the last one dropped); a space is SPACE clear columns with none after it
    std::vector<std::array<uint8_t, LH>> cols;
    bool spacer_last = false;
    for (const char* p = text; *p; p++) {
        if (*p == ' ') { for (int k = 0; k < SPACE; k++) cols.push_back({}); spacer_last = false; continue; }
        const Glyph* g = nullptr;
        for (const Glyph& x : GLYPHS) if (x.c == *p) g = &x;
        Tex t;
        if (!g || !load(rom.file(g->file), t) || t.h != LH || t.pal.size() < 16 || g->x1 > t.w) { why = "a label's letter isn't where it should be"; return false; }
        std::array<uint8_t, 16> m{};          // (its palette's brightness -> the target palette's nearest index, 2..15: the first nearest)
        for (int i = 2; i < 16; i++) {
            int best = 2;
            for (int j = 3; j < 16; j++) if (std::abs(target[j] - lum(t.pal[i])) < std::abs(target[best] - lum(t.pal[i]))) best = j;
            m[i] = uint8_t(best);
        }
        for (int x = g->x0; x < g->x1; x++) {
            std::array<uint8_t, LH> c{};
            for (int y = 0; y < LH; y++) { const uint8_t v = t.px[size_t(y) * t.w + x]; c[y] = v < 2 ? 0 : m[v]; }
            cols.push_back(c);
        }
        cols.push_back({}); spacer_last = true;
    }
    if (spacer_last) cols.pop_back();
    const int ww = int(cols.size());
    if (ww > LW - 4) { why = "a label's word is too wide"; return false; }
    std::vector<uint8_t> a(size_t(LW) * LH, 0);
    const int x0 = (LW - ww) / 2;
    for (int x = 0; x < ww; x++) for (int y = 0; y < LH; y++) a[size_t(y) * LW + x0 + x] = cols[size_t(x)][size_t(y)];
    // the outline: every clear pixel next to the word's body (8 ways round, the picture's edges wrapping as numpy's roll does) is 1
    std::vector<uint8_t> ring(a.size(), 0);
    for (int y = 0; y < LH; y++) for (int x = 0; x < LW; x++) {
        bool near = false;
        for (int dy = -1; dy <= 1 && !near; dy++) for (int dx = -1; dx <= 1 && !near; dx++)
            near = a[size_t((y - dy + LH) % LH) * LW + size_t((x - dx + LW) % LW)] > 0;
        ring[size_t(y) * LW + x] = near;
    }
    for (size_t i = 0; i < a.size(); i++) if (ring[i] && !a[i]) a[i] = 1;
    // labeltex.py tex_bytes: the palette file's header and palette, then the pixels
    out = base.head;
    for (size_t i = 0; i < a.size(); i += 2) out.push_back(uint8_t(a[i] << 4 | a[i + 1]));
    return true;
}

// ---- labeltex.py make_squeezed(parts, pal_file, room=14, up): "Lumberjack Match" (ljround8, 2026-11-23) -- L a r "Match"
// from "Ladder Match", u m b e from "Royal Rumble", c k from "Smackdown Mall", the j the i of "Exhibition" carried down with a
// tail; squeezed to 114 columns by brightness (each new column the average of the columns it takes in), one row up, the
// outline round it. The same steps and the same arithmetic as the Python (doubles, the same order), so the bytes are equal.
// (ljmenu10: the Lumberjack MATCH page's settings rows the same way -- s P i d, the colon and the space after it, the words
// "Random" and "Select" whole, and the figures, labeltex.py figure: below)
struct Piece { const char* name; uint32_t file; int x0, x1; };
const Piece PIECES[] = { { "L", 0x448C, 7, 15 }, { "a", 0x448C, 15, 25 }, { "r", 0x448C, 56, 64 }, { "Match", 0x448C, 70, 121 },
                         { "u", 0x4488, 68, 78 }, { "m", 0x4488, 78, 94 }, { "b", 0x4488, 94, 104 }, { "e", 0x4488, 110, 120 },
                         { "c", 0x448F, 34, 43 }, { "k", 0x448F, 43, 52 },
                         { "s", 0x448B, 37, 46 }, { "P", 0x4489, 9, 19 }, { "i", 0x4487, 34, 39 }, { "d", 0x448C, 25, 35 },
                         { ":", 0x44A6, 62, 69 }, { "Random", 0x44B1, 15, 82 }, { "Select", 0x44B2, 21, 73 } };
using Col = std::array<uint8_t, LH>;
// a piece: its columns, its pixels 2..15 mapped to the target palette's nearest (first nearest, 2..15), 0 and 1 cleared
bool piece_cols(const Rom& rom, uint32_t file, int x0, int x1, const std::array<int, 16>& target, std::vector<Col>& out) {
    Tex t;
    if (!load(rom.file(file), t) || t.h != LH || t.pal.size() < 16 || x1 > t.w) return false;
    std::array<uint8_t, 16> m{};
    for (int i = 2; i < 16; i++) {
        int best = 2;
        for (int j = 3; j < 16; j++) if (std::abs(target[j] - lum(t.pal[i])) < std::abs(target[best] - lum(t.pal[i]))) best = j;
        m[i] = uint8_t(best);
    }
    out.clear();
    for (int x = x0; x < x1; x++) {
        Col c{};
        for (int y = 0; y < LH; y++) { const uint8_t v = t.px[size_t(y) * t.w + x]; c[y] = v < 2 ? 0 : m[v]; }
        out.push_back(c);
    }
    return true;
}
// labeltex.py figure(d): the NUMBER page's figure d (file 0x4588, ten of 32 x 48 down it) -- its fill (index 3 up; 2 is its
// green border) -- at the labels' capital height, 16 rows from row 7: each new pixel its cover by the fill (the area of
// the old pixels it takes in), shown from a quarter, its brightness the cover of the palette's white (the nearest, 2..15)
constexpr uint32_t FIGURES = 0x4588;
constexpr int FIG_CAP = 16, FIG_TOP = 7;
bool figure_cols(const Rom& rom, int d, const std::array<int, 16>& target, std::vector<Col>& out) {
    Tex t;
    if (d < 0 || d > 9 || !load(rom.file(FIGURES), t) || t.w != 32 || t.h != 480) return false;
    int r0 = -1, r1 = -1, c0 = -1, c1 = -1;
    for (int y = 0; y < 48; y++) for (int x = 0; x < t.w; x++)
        if (t.px[size_t(d * 48 + y) * t.w + size_t(x)] >= 3) { if (r0 < 0) r0 = y; r1 = y; if (c0 < 0 || x < c0) c0 = x; if (x > c1) c1 = x; }
    if (r0 < 0) return false;
    const int h0 = r1 - r0 + 1, w0 = c1 - c0 + 1, h1 = FIG_CAP, w1 = std::max(1, (2 * w0 * h1 + h0) / (2 * h0));
    auto fill = [&](int sy, int sx) -> double { return t.px[size_t(d * 48 + r0 + sy) * t.w + size_t(c0 + sx)] >= 3 ? 1.0 : 0.0; };
    out.assign(size_t(w1), Col{});
    for (int y = 0; y < h1; y++)
        for (int x = 0; x < w1; x++) {
            const double ya = double(y * h0) / h1, yb = double((y + 1) * h0) / h1;
            const double xa = double(x * w0) / w1, xb = double((x + 1) * w0) / w1;
            double acc = 0.0, area = 0.0;
            for (int sy = int(ya); sy < std::min(h0, int(std::ceil(yb))); sy++) {
                const double wy = std::min(yb, double(sy + 1)) - std::max(ya, double(sy));
                for (int sx = int(xa); sx < std::min(w0, int(std::ceil(xb))); sx++) {
                    const double wx = std::min(xb, double(sx + 1)) - std::max(xa, double(sx));
                    acc += fill(sy, sx) * wy * wx; area += wy * wx;
                }
            }
            const double c = acc / area;
            if (c >= 0.25) {
                const double lv = target[15] * std::min(1.0, c);
                int best = 2;
                for (int j = 3; j < 16; j++) if (std::abs(target[j] - lv) < std::abs(target[best] - lv)) best = j;
                out[size_t(x)][size_t(FIG_TOP + y)] = uint8_t(best);
            }
        }
    return true;
}
bool make_squeezed(const Rom& rom, const std::vector<std::string>& parts, uint32_t pal_file, int up, std::vector<uint8_t>& out, std::string& why) {
    Tex base;
    if (!load(rom.file(pal_file), base) || base.pal.size() < 16) { why = "a label's palette file isn't the label it should be"; return false; }
    std::array<int, 16> target{};
    for (int j = 0; j < 16; j++) target[j] = lum(base.pal[j]);
    std::vector<std::vector<Col>> cols;                     // (pieces and spacers, as the Python's list)
    for (const std::string& name : parts) {
        if (name == " ") { cols.push_back(std::vector<Col>(SPACE)); continue; }
        std::vector<Col> g;
        if (name == "j") {                                  // (the i of "Exhibition", its stem carried down, its tail turned left)
            std::vector<Col> i;
            if (!piece_cols(rom, 0x4487, 34, 39, target, i)) { why = "a label's letter isn't where it should be"; return false; }
            int basey = -1;
            for (int y = 0; y < LH; y++) for (const Col& c : i) if (c[y]) basey = y;
            if (basey < 2 || basey + 3 >= LH) { why = "the i isn't where it should be"; return false; }
            g.assign(i.size() + 3, Col{});
            for (size_t x = 0; x < i.size(); x++) g[x + 3] = i[x];
            std::vector<uint8_t> rowv(i.size());            // (row base - 2 of the i, column by column)
            for (size_t x = 0; x < i.size(); x++) rowv[x] = i[x][size_t(basey - 2)];
            for (int y = basey - 1; y <= basey + 2; y++) for (size_t x = 0; x < i.size(); x++) g[x + 3][size_t(y)] = rowv[x];
            uint8_t v = 0;
            for (uint8_t q : rowv) if (q > v) v = q;
            if (!v) v = 15;
            for (int y : { basey + 2, basey + 3 }) { for (int x = 0; x < 3; x++) g[size_t(x)][size_t(y)] = v; for (int x = 3; x < 5; x++) g[size_t(x)][size_t(y)] = v; }
            for (size_t x = 3; x < g.size(); x++) g[x][size_t(basey + 3)] = 0;
            for (int x = 3; x < 5; x++) g[size_t(x)][size_t(basey + 3)] = v;
            if (cols.size() >= 2) {                         // (tucked under the letter before: no spacer, its 3 tail columns over)
                cols.pop_back();
                std::vector<Col>& prev = cols.back();
                prev.push_back(Col{});
                for (int k = 0; k < 3; k++) { Col& d = prev[prev.size() - 3 + size_t(k)]; for (int y = 0; y < LH; y++) d[size_t(y)] = std::max(d[size_t(y)], g[size_t(k)][size_t(y)]); }
                g.erase(g.begin(), g.begin() + 3);
            }
        } else if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
            if (!figure_cols(rom, name[0] - '0', target, g)) { why = "the NUMBER page's figures aren't where they should be"; return false; }
        } else {
            const Piece* p = nullptr;
            for (const Piece& q : PIECES) if (name == q.name) p = &q;
            if (!p || !piece_cols(rom, p->file, p->x0, p->x1, target, g)) { why = "a label's letter isn't where it should be"; return false; }
        }
        cols.push_back(g);
        cols.push_back(std::vector<Col>(1));
    }
    cols.pop_back();
    std::vector<Col> word;
    for (const auto& c : cols) word.insert(word.end(), c.begin(), c.end());
    const int w0 = int(word.size()), w1 = std::min(w0, LW - 14);
    std::vector<Col> sq(size_t(w1), Col{});
    if (w1 < w0) {
        const double span = double(w0) / w1;
        for (int y = 0; y < LH; y++)
            for (int x = 0; x < w1; x++) {
                const double a0 = double(x * w0) / w1, a1 = double((x + 1) * w0) / w1;
                double ol = 0.0, ob = 0.0;
                const int s_end = std::min(w0, int(std::ceil(a1)));
                for (int sx = int(a0); sx < s_end; sx++) {
                    const double wgt = std::min(a1, double(sx + 1)) - std::max(a0, double(sx));
                    if (wgt > 0) {
                        const uint8_t v = word[size_t(sx)][size_t(y)];
                        ol += double(v >= 2 ? target[v] : 0) * wgt;
                        ob += double(v >= 2 ? 1 : 0) * wgt;
                    }
                }
                if (ob / span >= 0.5) {
                    const double lv = ol / ob;
                    int best = 2;
                    for (int j = 3; j < 16; j++) if (std::abs(target[j] - lv) < std::abs(target[best] - lv)) best = j;
                    sq[size_t(x)][size_t(y)] = uint8_t(best);
                }
            }
    } else sq = word;
    std::vector<uint8_t> a(size_t(LW) * LH, 0);
    const int x0 = (LW - w1) / 2;
    for (int x = 0; x < w1; x++) for (int y = 0; y < LH; y++) a[size_t(y) * LW + x0 + x] = sq[size_t(x)][size_t(y)];
    for (int k = 0; k < up; k++) {                          // (up rows up)
        for (int y = 0; y < LH - 1; y++) for (int x = 0; x < LW; x++) a[size_t(y) * LW + x] = a[size_t(y + 1) * LW + x];
        for (int x = 0; x < LW; x++) a[size_t(LH - 1) * LW + x] = 0;
    }
    std::vector<uint8_t> ring(a.size(), 0);
    for (int y = 0; y < LH; y++) for (int x = 0; x < LW; x++) {
        bool near = false;
        for (int dy = -1; dy <= 1 && !near; dy++) for (int dx = -1; dx <= 1 && !near; dx++)
            near = a[size_t((y - dy + LH) % LH) * LW + size_t((x - dx + LW) % LW)] > 0;
        ring[size_t(y) * LW + x] = near;
    }
    for (size_t i = 0; i < a.size(); i++) if (ring[i] && !a[i]) a[i] = 1;
    out = base.head;
    for (size_t i = 0; i < a.size(); i += 2) out.push_back(uint8_t(a[i] << 4 | a[i + 1]));
    return true;
}
bool make_lumberjack(const Rom& rom, std::vector<uint8_t>& out, std::string& why) {
    return make_squeezed(rom, { "L", "u", "m", "b", "e", "r", "j", "a", "c", "k", " ", "Match" }, 0x4617, 1, out, why);
}
// (ljmenu10) labeltex.py's label_lj_count / label_lj_pick: the pictures one after another, on Single Match's palette, not moved up
constexpr int LJ_COUNT_MIN = 2, LJ_COUNT_MAX = 30;
bool make_lj_count(const Rom& rom, std::vector<uint8_t>& out, std::string& why) {
    out.clear();
    for (int n = LJ_COUNT_MIN; n <= LJ_COUNT_MAX; n++) {
        std::vector<std::string> parts = { "L", "u", "m", "b", "e", "r", "j", "a", "c", "k", "s", ":" };
        for (char ch : std::to_string(n)) parts.push_back(std::string(1, ch));
        std::vector<uint8_t> b;
        if (!make_squeezed(rom, parts, 0x44A1, 0, b, why)) return false;
        out.insert(out.end(), b.begin(), b.end());
    }
    return true;
}
bool make_lj_pick(const Rom& rom, std::vector<uint8_t>& out, std::string& why) {
    out.clear();
    for (const char* w : { "Random", "Select" }) {
        std::vector<uint8_t> b;
        if (!make_squeezed(rom, { "P", "i", "c", "k", "e", "d", ":", w }, 0x44A1, 0, b, why)) return false;
        out.insert(out.end(), b.begin(), b.end());
    }
    return true;
}

// ---- hdr_versus: 96 x 16, the pixels only (the "exhibition" texture's, file 0x4496, are written over in the menus' heap)
struct Block { uint32_t file; int shift, d0, d1; };          // destination columns d0 .. d1 - 1 from the file's columns x + shift
const Block BLOCKS[] = { { 0x4498, 67, 2, 16 }, { 0x4498, 34, 15, 33 }, { 0x449A, -3, 32, 43 }, { 0x449A, -30, 42, 52 }, { 0x449A, -21, 50, 60 } };
struct Seam { int x; uint16_t second, blank; };               // where two blocks meet: the rows from the second block, the rows left blank
const Seam SEAMS[] = { { 15, 0x1C80, 0 }, { 32, 0xF800, 0 }, { 42, 0x1ED0, 0x0020 }, { 50, 0x1000, 0 }, { 51, 0x1600, 0 } };
constexpr int HW = 96, HH = 16;

bool make_versus(const Rom& rom, std::vector<uint8_t>& out, std::string& why) {
    Tex ex;
    if (!load(rom.file(0x4496), ex) || ex.pal.size() < 16 || ex.w != HW || ex.h != HH) { why = "file 0x4496 isn't the sub-header it should be"; return false; }
    // each source's colours mapped to the nearest of "exhibition"'s (1..15, the first nearest; 0 and 1 kept)
    auto mapped = [&](uint32_t file, Tex& t) -> bool {
        if (!load(rom.file(file), t) || t.h != HH || t.pal.size() < 16) return false;
        std::array<uint8_t, 16> m{};
        m[0] = 0; m[1] = 1;
        for (int i = 2; i < 16; i++) {
            int best = 1;
            for (int j = 2; j < 16; j++) if (rgb_distance(ex.pal[j], t.pal[i]) < rgb_distance(ex.pal[best], t.pal[i])) best = j;
            m[i] = uint8_t(best);
        }
        for (uint8_t& v : t.px) v = m[v];
        return true;
    };
    Tex pay, guest;
    if (!mapped(0x4498, pay) || !mapped(0x449A, guest)) { why = "files 0x4498 / 0x449A aren't the sub-headers they should be"; return false; }
    auto src = [&](const Block& b) -> const Tex& { return b.file == 0x4498 ? pay : guest; };
    std::vector<uint8_t> a(size_t(HW) * HH, 0);
    for (int x = 0; x < HW; x++) {
        const Block* first = nullptr; const Block* second = nullptr;
        for (const Block& b : BLOCKS) if (x >= b.d0 && x < b.d1) { if (!first) first = &b; else if (!second) second = &b; }
        if (!first) continue;
        const Seam* seam = nullptr;
        for (const Seam& s : SEAMS) if (s.x == x) seam = &s;
        for (int y = 0; y < HH; y++) {
            const Block* b = (seam && second && (seam->second >> y & 1)) ? second : first;
            const Tex& t = src(*b);
            const int sx = x + b->shift;
            if (sx < 0 || sx >= t.w) { why = "a sub-header's letter isn't where it should be"; return false; }
            a[size_t(y) * HW + x] = (seam && (seam->blank >> y & 1)) ? 0 : t.px[size_t(y) * t.w + sx];
        }
    }
    out.clear();
    for (size_t i = 0; i < a.size(); i += 2) out.push_back(uint8_t(a[i] << 4 | a[i + 1]));
    return true;
}

}  // namespace

bool port_labels(const Rom& rom, const std::vector<Table>& mine, std::vector<uint8_t>& image, std::string& why) {
    for (const Table& t : mine) {
        std::vector<uint8_t> b;
        bool ok = false;
        if (t.name == "label_custom_story") ok = make_label(rom, "Custom Story", b, why);
        else if (t.name == "label_versus") ok = make_label(rom, "Versus", b, why);
        else if (t.name == "label_lumberjack") ok = make_lumberjack(rom, b, why);   // (ljround8: the Lumberjack Match row)
        else if (t.name == "label_lj_count") ok = make_lj_count(rom, b, why);       // (ljmenu10: the Lumberjack MATCH page's settings rows)
        else if (t.name == "label_lj_pick") ok = make_lj_pick(rom, b, why);
        else if (t.name == "hdr_versus") ok = make_versus(rom, b, why);
        else { why = "no table " + t.name + " in this port"; return false; }
        if (!ok) return false;
        if (b.size() != t.size || size_t(t.address - 0x81000000u) + b.size() > image.size()) { why = t.name + " came out the wrong size"; return false; }
        memcpy(image.data() + (t.address - 0x81000000u), b.data(), b.size());
    }
    return true;
}

}  // namespace firstrun
