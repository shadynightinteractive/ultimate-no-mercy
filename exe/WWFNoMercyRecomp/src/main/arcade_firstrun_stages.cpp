// The first start's stage sets: the files the mod's pack holds from 0x5000 on that mod/gen/arenas_raw.py and
// arenas_stages.py make from No Mercy's own files -- the game's ten arenas' stage sets, the close scenes' detailed sets
// brought into the match -- made again from the player's ROM (firstrun, 2026-11-17; arcade_firstrun_ports.h).
//
// Byte for byte the build's, so Python's arithmetic exactly: coordinates as Python has them (whole numbers that may become
// floats on the way), round() to even on a tie, the first of equals for min / max.
//
// RAW IS WAR (venue 0) as arenas_raw.py makes it; venues 1-9 as arenas_stages.py does, from the same tables (written into
// arcade_firstrun_stages_tables.inc by tools/firstrun/stages_tables.py); the one packed picture by Pillow's own median cut
// (pillow_mediancut/, Pillow's code as it is), as PIL's Image.quantize made it.
// [stagelists 2026-11-19] RAW IS WAR's close scene lists (the game's own file numbers) are read from the player's ROM
// (raw_lists below: an offset per ROM and a check number), no longer copied here; so are venues 1-9's close scenes
// (close_scenes below): the .inc holds only our own settings (SPEC, NEW).
#pragma clang fp contract(off)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "arcade_firstrun_ports.h"

namespace firstrun {
namespace {

using Bytes = std::vector<uint8_t>;
int s8(uint8_t b) { return b > 127 ? int(b) - 256 : int(b); }
double py_round(double x) { return std::nearbyint(x); }      // (Python's round(): halves to even -- the default rounding mode)

// ---- arenas_ring.py Mesh: a mesh's points (world), texels (the page added), colours, triangles, its two flag bytes
struct Mesh {
    std::vector<std::array<double, 3>> pts;
    std::vector<std::array<int, 2>> uv;
    std::vector<std::array<uint8_t, 3>> rgb;
    std::vector<std::array<int, 3>> tris;
    int b1 = 0, b3 = 0;
    int alt = 0;         // (arenas_stages.py Mesh: its other vertex form -- u, v u16, the colour 200 grey)
};

// arenas_ring.py Mesh_from_file: No Mercy's mesh file (func_8000C398's format)
bool mesh_from_file(const Bytes& d, Mesh& m) {
    if (d.size() < 8) return false;
    const int sc = (d[0] & 0x7F) + 1, nv = d[1] & 0x7F, nt = d[2];
    const int off[3] = { s8(d[4]) * 16, s8(d[5]) * 16, s8(d[6]) * 16 };
    const int pu = (d[7] >> 4) * 16, pv = (d[7] & 15) * 16;
    if (d.size() < size_t(8 + 8 * nv + 3 * nt)) return false;
    m = Mesh();
    for (int i = 0; i < nv; i++) {
        const uint8_t* v = d.data() + 8 + 8 * i;
        m.pts.push_back({ double((s8(v[0]) + off[0]) * sc), double((s8(v[1]) + off[1]) * sc), double((s8(v[2]) + off[2]) * sc) });
        m.uv.push_back({ v[3] + pu, v[4] + pv });
        m.rgb.push_back({ v[5], v[6], v[7] });
    }
    const size_t o = 8 + 8 * size_t(nv);
    for (int i = 0; i < nt; i++) m.tris.push_back({ d[o + 3 * i], d[o + 3 * i + 1], d[o + 3 * i + 2] });
    m.b1 = d[1] & 0x80; m.b3 = d[3];
    return true;
}

// Mesh.__add__: one after the other (the second's triangles' points moved past the first's)
Mesh join(const std::vector<Mesh>& ms) {
    Mesh out = ms[0];
    for (size_t k = 1; k < ms.size(); k++) {
        const Mesh& o = ms[k];
        const int n = int(out.pts.size());
        out.pts.insert(out.pts.end(), o.pts.begin(), o.pts.end());
        out.uv.insert(out.uv.end(), o.uv.begin(), o.uv.end());
        out.rgb.insert(out.rgb.end(), o.rgb.begin(), o.rgb.end());
        for (const auto& t : o.tris) out.tris.push_back({ t[0] + n, t[1] + n, t[2] + n });
        out.alt |= o.alt;
    }
    return out;
}

// Mesh.file: No Mercy's mesh file -- the smallest scale its points fit at, one texture page
bool mesh_file(const Mesh& m, Bytes& out, std::string& why) {
    const size_t n = m.pts.size();
    if (n == 0 || n > 127 || m.tris.size() > 127) { why = "a stage set's mesh is too big"; return false; }
    int sc = 0;
    std::array<int, 3> off{};
    std::vector<std::array<double, 3>> q(n);
    for (int s = 1; s <= 128 && !sc; s++) {
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            double lo = m.pts[0][k], hi = m.pts[0][k];
            for (const auto& p : m.pts) { lo = std::min(lo, p[k]); hi = std::max(hi, p[k]); }
            off[k] = int(py_round((lo + hi) / 2 / s / 16));
            ok = ok && off[k] >= -128 && off[k] <= 127;
        }
        for (size_t i = 0; i < n && ok; i++)
            for (int k = 0; k < 3 && ok; k++) {
                const double c = m.pts[i][k] / s - off[k] * 16;
                q[i][k] = c;
                ok = c >= -128 && c <= 127 && c == std::trunc(c);
            }
        if (ok) sc = s;
    }
    if (!sc) { why = "a stage set's mesh fits no scale"; return false; }
    auto page = [&](int which) {
        for (int p = 15; p >= 0; p--) {
            bool all = true;
            for (const auto& t : m.uv) all = all && t[which] - 16 * p >= 0 && t[which] - 16 * p <= 255;
            if (all) return p;
        }
        return -1;
    };
    const int pu = page(0), pv = page(1);
    if (pu < 0 || pv < 0) { why = "a stage set's mesh has texels no page holds"; return false; }
    out = { uint8_t(sc - 1), uint8_t(m.b1 | int(n)), uint8_t(m.tris.size()), uint8_t(m.b3),
            uint8_t(off[0] & 0xFF), uint8_t(off[1] & 0xFF), uint8_t(off[2] & 0xFF), uint8_t(pu << 4 | pv) };
    for (size_t i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) out.push_back(uint8_t(int(q[i][k]) & 0xFF));
        out.push_back(uint8_t(m.uv[i][0] - 16 * pu)); out.push_back(uint8_t(m.uv[i][1] - 16 * pv));
        out.insert(out.end(), m.rgb[i].begin(), m.rgb[i].end());
    }
    for (const auto& t : m.tris) { out.push_back(uint8_t(t[0])); out.push_back(uint8_t(t[1])); out.push_back(uint8_t(t[2])); }
    if (out.size() & 1) out.push_back(0);
    return true;
}

// arenas_ring.py grid_scale: the power of two a part's points go on
int grid_scale(const Mesh& m) {
    double span = 0;
    for (int k = 0; k < 3; k++) {
        double lo = m.pts[0][k], hi = m.pts[0][k];
        for (const auto& p : m.pts) { lo = std::min(lo, p[k]); hi = std::max(hi, p[k]); }
        span = std::max(span, hi - lo);
    }
    int sc = 1;
    while (sc * 230 < span) sc *= 2;
    return sc;
}

// arenas_ring.py snap_parts: each part's points on its grid, a point two parts share on the coarser of the two
void snap_parts(std::vector<Mesh*>& ms) {
    auto key = [](const std::array<double, 3>& p) {
        return std::array<long long, 3>{ (long long)py_round(p[0] * 4), (long long)py_round(p[1] * 4), (long long)py_round(p[2] * 4) };
    };
    std::map<std::array<long long, 3>, int> most;
    for (Mesh* m : ms) {
        if (!m) continue;
        const int sc = grid_scale(*m);
        for (const auto& p : m->pts) { int& v = most[key(p)]; v = std::max(v, sc); }
    }
    for (Mesh* m : ms) {
        if (!m) continue;
        for (auto& p : m->pts) {
            const int g = most[key(p)];
            for (int k = 0; k < 3; k++) p[k] = double((long long)py_round(p[k] / g) * g);
        }
    }
}

// ---- arenas_raw.py: RAW IS WAR's match stage set made the entrances' close set (venue 0)
constexpr double RAW_SCALE = 1.0, RAW_MOVE[3] = { 0.0, 160.0, 1245.0 };
constexpr int DECK_FRONT = -440, FACE_Z = -290;
// [stagelists 2026-11-19] The close scene's pieces and their palettes (arenas_raw.py CLOSE, CLOSE_PAL; EXTRA): the game's own
// lists of file numbers, so read from the player's ROM, not carried in the source (it held a copy until 2026-11-19). In the
// ROM they are three big-endian 16-bit lists one after the other, each padded to 4 bytes: the main list's 73 pieces (+0),
// their 73 palettes (+0x94), the extra list's 13 pieces (+0x128) -- 0x142 bytes, at one place in each ROM (the same bytes).
struct RawLists { std::vector<uint16_t> close, close_pal, extra; };
struct ListPlace { uint32_t offset; const char* rom; };
constexpr ListPlace RAW_LIST_PLACES[] = {
    { 0x514A0u, "No Mercy (USA) Rev 1" },          // (also the six-man port ROM's place: it leaves these bytes as they are)
    { 0x51540u, "No Mercy (USA) v1.0" },
};
constexpr uint32_t RAW_LISTS_BYTES = 0x142u;
constexpr uint32_t CLOSE_AT = 0x0u, CLOSE_PAL_AT = 0x94u, CLOSE_N = 73, EXTRA_AT = 0x128u, EXTRA_N = 13;
// The check number: FNV-1a 64 of the 0x142 bytes, as the ROM holds them (a number, not the bytes).
constexpr uint64_t RAW_LISTS_FNV = 0xEC9595F4A422D990ull;

uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

// The three lists from the ROM: the place whose bytes have the check number, else false and why (never a guess).
bool raw_lists(const Rom& rom, RawLists& out, std::string& why) {
    const std::span<const uint8_t> r = rom.rom;
    for (const ListPlace& place : RAW_LIST_PLACES) {
        if (size_t(place.offset) + RAW_LISTS_BYTES > r.size()) continue;
        const uint8_t* b = r.data() + place.offset;
        if (fnv1a64(b, RAW_LISTS_BYTES) != RAW_LISTS_FNV) continue;
        auto list = [&](uint32_t at, uint32_t n) {
            std::vector<uint16_t> v;
            for (uint32_t i = 0; i < n; i++) v.push_back(uint16_t(b[at + 2 * i] << 8 | b[at + 2 * i + 1]));
            return v;
        };
        out = { list(CLOSE_AT, CLOSE_N), list(CLOSE_PAL_AT, CLOSE_N), list(EXTRA_AT, EXTRA_N) };
        return true;
    }
    char text[240];
    std::snprintf(text, sizeof text, "the ROM (%zu bytes) does not hold RAW IS WAR's close scene lists where No Mercy (USA) Rev 1 "
        "(0x%X) or v1.0 (0x%X) has them", r.size(), RAW_LIST_PLACES[0].offset, RAW_LIST_PLACES[1].offset);
    why = text;
    return false;
}
int pal_of(const RawLists& L, uint16_t f) {
    for (size_t i = 0; i < L.close.size(); i++) if (L.close[i] == f) return L.close_pal[i];
    return -1;
}

// arenas_raw.py cut_deck: a deck floor's points in front of the face brought back to it, texels and colour along
Mesh cut_deck(const Mesh& m) {
    Mesh o = m;
    for (size_t i = 0; i < m.pts.size(); i++) {
        const auto& p = m.pts[i];
        if (p[2] >= DECK_FRONT) continue;
        int j = -1;                                    // (its row's next point back: the same x, the least z past it -- the first)
        for (size_t k = 0; k < m.pts.size(); k++)
            if (m.pts[k][0] == p[0] && m.pts[k][2] > p[2] && (j < 0 || m.pts[k][2] < m.pts[size_t(j)][2])) j = int(k);
        const double t = (DECK_FRONT - p[2]) / (m.pts[size_t(j)][2] - p[2]);
        o.pts[i] = { p[0], p[1], double(DECK_FRONT) };
        for (int c = 0; c < 2; c++) o.uv[i][c] = int(py_round(m.uv[i][c] + t * (m.uv[size_t(j)][c] - m.uv[i][c])));
        for (int c = 0; c < 3; c++) o.rgb[i][c] = uint8_t(int(py_round(m.rgb[i][c] + t * (m.rgb[size_t(j)][c] - m.rgb[i][c]))));
    }
    return o;
}

// arenas_raw.py close_face: 0xA92 with its middle closed (the picture carried on mirrored to x 0), at the deck's front
bool close_face(const Mesh& m, Mesh& o, std::string& why) {
    o = m;
    auto at = [&](double x, double y) {                // (a dict of points -> index: the last one wins)
        int found = -1;
        for (size_t i = 0; i < m.pts.size(); i++) if (m.pts[i][0] == x && m.pts[i][1] == y && m.pts[i][2] == FACE_Z) found = int(i);
        return found;
    };
    const int top = at(-180, 0), bot = at(-180, -200);
    const int a_[2] = { at(-180, 0), at(180, 0) }, b_[2] = { at(-180, -200), at(180, -200) };
    if (top < 0 || bot < 0 || a_[1] < 0 || b_[1] < 0) { why = "RAW IS WAR's face isn't the mesh it should be"; return false; }
    const int u_mid = int(py_round(180 / 860.0 * 127));
    const int n = int(o.pts.size());
    o.pts.push_back({ 0, 0, double(FACE_Z) }); o.pts.push_back({ 0, -200, double(FACE_Z) });
    o.uv.push_back({ u_mid, 0 }); o.uv.push_back({ u_mid, 31 });
    o.rgb.push_back(o.rgb[size_t(top)]); o.rgb.push_back(o.rgb[size_t(bot)]);
    for (int s = 0; s < 2; s++) {
        const int a = a_[s], b = b_[s];
        const std::array<int, 3> ts[2] = { { a, n, b }, { n, n + 1, b } };
        for (const auto& t : ts) {
            const auto &p = o.pts[size_t(t[0])], &q = o.pts[size_t(t[1])], &r = o.pts[size_t(t[2])];
            const double nz = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
            o.tris.push_back(nz < 0 ? t : std::array<int, 3>{ t[0], t[2], t[1] });
        }
    }
    for (auto& p : o.pts) if (p[2] == FACE_Z) p[2] = DECK_FRONT;
    return true;
}

struct Part { std::string name; Mesh mesh; int game_mesh = -1; int pal = 0; };

// arenas_raw.py parts(): the close scene's pieces in drawing order, pieces of one picture joined into parts of <= 127
bool raw_parts(const Rom& rom, std::vector<Part>& out, std::string& why) {
    RawLists L;                                        // [stagelists 2026-11-19] (from the ROM)
    if (!raw_lists(rom, L, why)) return false;
    auto pal_of = [&](uint16_t f) { return firstrun::pal_of(L, f); };
    auto M = [&](uint16_t f, Mesh& m) {
        if (!mesh_from_file(rom.file(f), m)) { why = "RAW IS WAR's close scene's mesh isn't there"; return false; }
        return true;
    };
    auto group = [&](const std::string& name, const std::vector<uint16_t>& fs, int pal) {
        std::vector<Mesh> cur; size_t n_p = 0, n_t = 0; int k = 0;
        for (uint16_t f : fs) {
            Mesh m;
            if (!M(f, m)) return false;
            if (!cur.empty() && (n_p + m.pts.size() > 127 || n_t + m.tris.size() > 127)) {
                out.push_back({ name + " " + std::to_string(k), join(cur), -1, pal }); k++; cur.clear(); n_p = n_t = 0;
            }
            n_p += m.pts.size(); n_t += m.tris.size(); cur.push_back(std::move(m));
        }
        if (!cur.empty()) out.push_back({ k ? name + " " + std::to_string(k) : name, join(cur), -1, pal });
        return true;
    };
    auto one = [&](const std::string& name, uint16_t f, int pal) { Mesh m; if (!M(f, m)) return false; out.push_back({ name, m, -1, pal }); return true; };
    auto hexname = [](const char* what, uint16_t f) { char b[32]; snprintf(b, sizeof b, "%s%X", what, f); return std::string(b); };
    Mesh m, face;
    if (!M(0xA92, m) || !close_face(m, face, why)) return false;
    out.push_back({ "face", face, -1, 0xE1C });
    if (!M(0xA99, m)) return false;
    out.push_back({ "walkway", cut_deck(m), -1, 0xE2C });
    for (uint16_t f : { 0xA95, 0xA96, 0xA97, 0xA98 }) { if (!M(f, m)) return false; out.push_back({ hexname("floor ", f), cut_deck(m), -1, pal_of(f) }); }
    out.push_back({ "ramp", Mesh(), 0x576, 0x811 });
    for (uint16_t f : { 0xA90, 0xA91, 0xA93, 0xAEA }) if (!one(hexname("wall ", f), f, pal_of(f))) return false;
    if (!group("logo back", { 0xA9E, 0xAA0, 0xAA2 }, 0xE40) || !group("logo back red", { 0xAA4 }, 0xE42) ||
        !group("logo", { 0xA9D, 0xA9F, 0xAA1 }, 0xE40) || !group("logo red", { 0xAA3 }, 0xE42)) return false;
    if (!one("fist L", 0xAA5, 0xE44) || !one("fist R", 0xAA6, 0xE4C) || !one("screen", 0xAA7, 0xE32)) return false;
    std::vector<uint16_t> frame, posts, towers, arches;
    for (size_t i = 0; i < L.close.size(); i++) {
        const int p = L.close_pal[i];
        if (p == 0xE3C) frame.push_back(L.close[i]);
        if (p == 0xE3E) posts.push_back(L.close[i]);
        if (p == 0xE38 || p == 0xE3A) towers.push_back(L.close[i]);
        if (p == 0xE36) arches.push_back(L.close[i]);
    }
    if (!group("frame", frame, 0xE3C) || !group("posts", posts, 0xE3E)) return false;
    const size_t half_t = towers.size() / 2;
    for (int side = 0; side < 2; side++) {
        std::vector<uint16_t> beams, tposts;
        for (size_t i = side ? half_t : 0; i < (side ? towers.size() : half_t); i++)
            (pal_of(towers[i]) == 0xE38 ? beams : tposts).push_back(towers[i]);
        const std::string s = side ? "L" : "R";
        if (!group("tower " + s, beams, 0xE38) || !group("tower " + s + " posts", tposts, 0xE3A)) return false;
    }
    if (!group("arches", arches, 0xE36)) return false;
    std::vector<uint16_t> back;
    for (uint16_t f : L.extra) if (f != 0xA94) back.push_back(f);
    if (!group("arches back", back, 0xE36)) return false;
    if (!one("sign WWF.com", 0xA94, 0xE22) || !one("sign AKI", 0xA8F, 0xE16)) return false;
    out.push_back({ "ring floor", Mesh(), 0x581, 0x827 });
    // in the match's place (p -> SCALE p + MOVE), on the grid of each part's scale (snap_parts)
    std::vector<Mesh*> ms;
    for (Part& p : out) {
        if (p.game_mesh >= 0) { ms.push_back(nullptr); continue; }
        for (auto& q : p.mesh.pts) for (int k = 0; k < 3; k++) q[k] = RAW_SCALE * q[k] + RAW_MOVE[k];
        ms.push_back(&p.mesh);
    }
    snap_parts(ms);
    return true;
}

const Table* find(const std::vector<Table>& all, const char* name) {
    for (const Table& t : all) if (t.name == name) return &t;
    return nullptr;
}
uint16_t be16(const std::vector<uint8_t>& b, size_t at) { return uint16_t(b[at] << 8 | b[at + 1]); }

// ---- arenas_stages.py: No Mercy's own arenas (venues 1-9), each one's match stage set made the detailed one its entrances'
//      close shots draw. The tables (SPEC, NEW) come from the Python by tools/firstrun/stages_tables.py; the close scenes'
//      lists from the ROM (close_scenes, [stagelists 2026-11-19]).
struct Pc { int f, p; };
struct CloseScene { int v; int place[3]; std::vector<Pc> main, x1; int x1_off[3]; int x2[2]; int x3[2]; std::vector<Pc> m61; };
struct Keep { std::vector<Pc> meshes; int single; int pal; int where_kind; int where_f; };   // (where: 0 start, 1 end, 2 after where_f's part, 3 at it)
struct Clip { std::vector<int> ids; bool all; int axis; bool ge; double at; };
struct Cell { int pal, x, y, w, h; };
struct Pack { int W, H; std::vector<Cell> cells; };
struct Spec {
    int v; double s; double d[3]; int z1; std::vector<Keep> keep; std::vector<int> drop; std::vector<Clip> clip;
    std::vector<std::pair<int, double>> cos; double cos_all; std::vector<std::pair<int, int>> order; std::vector<int> alone;
    std::vector<int> bands; std::vector<int> band_pals; std::vector<Pack> pack;
};
struct NewPieces { int v; int screen[4]; int z, beam, screen_pic, truss_pic, grey; };
#include "arcade_firstrun_stages_tables.inc"

// [stagelists 2026-11-19] The close scenes (CloseScene: venue v's is scene index v) are the game's own tables, so they are
// read from the player's ROM, not carried in the .inc (it held them until 2026-11-19). The game's data segment holds them:
// RAM 0x80050370..0x800527E0 (Rev 1's addresses) = Rev 1 ROM 0x50F70, v1.0 ROM 0x51010 (whose pointers are 0xA0 further
// on). Each ROM's 0x2470 bytes there are checked by a check number; the tables are read as the game reads them
// (data/arenaport/entrance/scenetab.py; mod/gen/arenas_stages_tab.py reads them the same way for the build).
struct ScenePlace { uint32_t offset; uint32_t shift; uint64_t fnv; const char* rom; };
constexpr uint32_t SCENE_LO = 0x80050370u, SCENE_BYTES = 0x2470u;
constexpr ScenePlace SCENE_PLACES[] = {
    { 0x50F70u, 0x00u, 0x7DAB693F1BBBD8DBull, "No Mercy (USA) Rev 1" },    // (FNV-1a 64 of the 0x2470 bytes: a number, not the bytes)
    { 0x51010u, 0xA0u, 0x562D0EDED587C460ull, "No Mercy (USA) v1.0" },
};
// the game's tables (Rev 1 addresses), by scene index
constexpr uint32_t SC_PLACE = 0x80050690u;                                              // (16 bytes an index: x, y, z floats)
constexpr uint32_t SC_MAIN_MESH = 0x80052280u, SC_MAIN_PAL = 0x800522F8u, SC_MAIN_N = 0x80052334u;
constexpr uint32_t SC_X1_MESH = 0x80052354u, SC_X1_PAL = 0x80052390u, SC_X1_N = 0x800523CCu, SC_X1_OFF = 0x800523ECu;   // (12 bytes an index)
constexpr uint32_t SC_X2_MESH = 0x800526C0u, SC_X2_PALS = 0x800526FCu, SC_X2_N = 0x80052738u;  // (one mesh, its palettes by frame)
constexpr uint32_t SC_X3_MESH = 0x80052758u, SC_X3_PALS = 0x80052790u, SC_X3_N = 0x800527CCu;
constexpr uint32_t SC_M61_MESH = 0x800524A0u, SC_M61_PAL = 0x800524DCu, SC_M61_N = 0x80052518u;

// The close scenes of `venues` from the ROM; false and why if its bytes aren't the game's tables (never a guess).
bool close_scenes(const Rom& rom, const std::vector<int>& venues, std::vector<CloseScene>& out, std::string& why) {
    const std::span<const uint8_t> r = rom.rom;
    const ScenePlace* place = nullptr;
    for (const ScenePlace& p : SCENE_PLACES)
        if (size_t(p.offset) + SCENE_BYTES <= r.size() && fnv1a64(r.data() + p.offset, SCENE_BYTES) == p.fnv) { place = &p; break; }
    if (!place) {
        char text[240];
        std::snprintf(text, sizeof text, "the ROM (%zu bytes) does not hold the arenas' close scene tables where No Mercy (USA) Rev 1 "
            "(0x%X) or v1.0 (0x%X) has them", r.size(), SCENE_PLACES[0].offset, SCENE_PLACES[1].offset);
        why = text;
        return false;
    }
    const uint8_t* b = r.data() + place->offset;
    bool bad = false;
    auto at = [&](uint32_t a, uint32_t n) -> const uint8_t* {      // (a Rev 1 address inside the span read)
        static const uint8_t zero[4] = {};
        if (a < SCENE_LO || uint64_t(a) + n > uint64_t(SCENE_LO) + SCENE_BYTES) { bad = true; return zero; }
        return b + (a - SCENE_LO);
    };
    auto u16 = [&](uint32_t a) { const uint8_t* p = at(a, 2); return int(p[0] << 8 | p[1]); };
    auto s16 = [&](uint32_t a) { const int v = u16(a); return v > 0x7FFF ? v - 0x10000 : v; };
    auto u32 = [&](uint32_t a) { const uint8_t* p = at(a, 4); return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; };
    auto ptr = [&](uint32_t a) { const uint32_t v = u32(a); return v ? v - place->shift : 0u; };   // (taken back to Rev 1's address)
    auto whole = [&](uint32_t a) {                                  // (a float the game keeps a whole number in)
        const uint32_t v = u32(a);
        float f; std::memcpy(&f, &v, 4);
        if (!(std::fabs(f) <= 1e6f) || f != std::trunc(f)) { bad = true; return 0; }
        return int(f);
    };
    auto pcs = [&](uint32_t pm, uint32_t pp, int n) {
        std::vector<Pc> v;
        if (n < 0) { bad = true; return v; }
        if (pm && pp) for (int k = 0; k < n; k++) v.push_back({ u16(pm + 2u * uint32_t(k)), u16(pp + 2u * uint32_t(k)) });
        return v;
    };
    for (int v : venues) {
        const uint32_t i = uint32_t(v);
        CloseScene cs{};
        cs.v = v;
        for (uint32_t k = 0; k < 3; k++) { cs.place[k] = whole(SC_PLACE + 16 * i + 4 * k); cs.x1_off[k] = whole(SC_X1_OFF + 12 * i + 4 * k); }
        cs.main = pcs(ptr(SC_MAIN_MESH + 4 * i), ptr(SC_MAIN_PAL + 4 * i), s16(SC_MAIN_N + 2 * i));
        cs.x1 = pcs(ptr(SC_X1_MESH + 4 * i), ptr(SC_X1_PAL + 4 * i), s16(SC_X1_N + 2 * i));
        cs.m61 = pcs(ptr(SC_M61_MESH + 4 * i), ptr(SC_M61_PAL + 4 * i), u16(SC_M61_N + 2 * i));
        const uint32_t xs[2][3] = { { SC_X2_MESH, SC_X2_PALS, SC_X2_N }, { SC_X3_MESH, SC_X3_PALS, SC_X3_N } };
        for (int x = 0; x < 2; x++) {
            int* dst = x ? cs.x3 : cs.x2;
            dst[0] = dst[1] = -1;
            if (!u16(xs[x][2] + 2 * i)) continue;
            const uint32_t pm = ptr(xs[x][0] + 4 * i), pp = ptr(xs[x][1] + 4 * i);
            if (!pm || !pp) { bad = true; continue; }
            const uint32_t p0 = ptr(pp);
            if (!p0) { bad = true; continue; }
            dst[0] = u16(pm); dst[1] = u16(p0);                    // (the mesh, its first frame's palette)
        }
        if (bad) { why = std::string("the arenas' close scene tables in the ROM (") + place->rom + ") aren't as the game reads them"; return false; }
        out.push_back(cs);
    }
    return true;
}

constexpr int LIMIT = 127;
bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
long long floordiv(long long a, long long b) { long long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

// arenas_stages.py mesh_of: a mesh file in either vertex form
bool mesh_of(const Bytes& d, Mesh& m) {
    if (!mesh_from_file(d, m)) return false;
    m.alt = d[0] >> 7;
    if (m.alt) {
        for (size_t i = 0; i < m.pts.size(); i++) {
            const uint8_t* v = d.data() + 8 + 8 * i;
            m.uv[i] = { v[4] << 8 | v[5], v[6] << 8 | v[7] };
            m.rgb[i] = { 200, 200, 200 };
        }
    }
    return true;
}

// arenas_stages.py mesh_file: the byte form if its texels fit a page, else the u16 one
bool mesh_file2(const Mesh& m, Bytes& out, std::string& why) {
    std::vector<int> pu, pv;
    for (int p = 0; p < 16; p++) {
        bool a = true, b = true;
        for (const auto& t : m.uv) { a = a && t[0] - 16 * p >= 0 && t[0] - 16 * p <= 255; b = b && t[1] - 16 * p >= 0 && t[1] - 16 * p <= 255; }
        if (a) pu.push_back(p);
        if (b) pv.push_back(p);
    }
    if (!m.alt && !pu.empty() && !pv.empty()) return mesh_file(m, out, why);
    // the u16 form: the same scale and offsets (mesh_file's search), no page, colour 200
    Mesh byte_form = m; byte_form.uv.assign(m.uv.size(), { 0, 0 });
    Bytes b;
    if (!mesh_file(byte_form, b, why)) return false;
    out.assign(b.begin(), b.begin() + 8);
    out[0] = uint8_t(0x80 | out[0]); out[7] = 0;
    for (size_t i = 0; i < m.pts.size(); i++) {
        if (m.uv[i][0] < 0 || m.uv[i][0] > 0xFFFF || m.uv[i][1] < 0 || m.uv[i][1] > 0xFFFF) { why = "a stage set's texel out of range"; return false; }
        out.insert(out.end(), b.begin() + 8 + 8 * i, b.begin() + 8 + 8 * i + 3);
        out.push_back(0);
        out.push_back(uint8_t(m.uv[i][0] >> 8)); out.push_back(uint8_t(m.uv[i][0])); out.push_back(uint8_t(m.uv[i][1] >> 8)); out.push_back(uint8_t(m.uv[i][1]));
    }
    for (const auto& t : m.tris) { out.push_back(uint8_t(t[0])); out.push_back(uint8_t(t[1])); out.push_back(uint8_t(t[2])); }
    if (out.size() & 1) out.push_back(0);
    return true;
}

// Mesh.sub: the mesh of these triangles (and the points they use, in their order)
Mesh sub(const Mesh& m, const std::vector<std::array<int, 3>>& tris) {
    std::vector<int> used;
    for (const auto& t : tris) for (int i : t) used.push_back(i);
    std::sort(used.begin(), used.end()); used.erase(std::unique(used.begin(), used.end()), used.end());
    std::map<int, int> at;
    for (size_t k = 0; k < used.size(); k++) at[used[k]] = int(k);
    Mesh o; o.b1 = m.b1; o.b3 = m.b3; o.alt = m.alt;
    for (int i : used) { o.pts.push_back(m.pts[size_t(i)]); o.uv.push_back(m.uv[size_t(i)]); o.rgb.push_back(m.rgb[size_t(i)]); }
    for (const auto& t : tris) o.tris.push_back({ at[t[0]], at[t[1]], at[t[2]] });
    return o;
}

std::array<double, 3> normal(const Mesh& m, const std::array<int, 3>& t) {
    const auto &a = m.pts[size_t(t[0])], &b = m.pts[size_t(t[1])], &c = m.pts[size_t(t[2])];
    const double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    return { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
}

// clip: m cut by the plane p[axis] = at, what's on the kept side; false: nothing left
bool clip(Mesh& m, int axis, bool keep_ge, double at) {
    auto side = [&](const std::array<double, 3>& p) { return (p[axis] - at) * (keep_ge ? 1 : -1) >= -1e-6; };
    Mesh o = m; o.tris.clear();
    std::map<std::pair<int, int>, int> cache;
    auto cut = [&](int i, int j) {
        const std::pair<int, int> key{ std::min(i, j), std::max(i, j) };
        auto f = cache.find(key);
        if (f != cache.end()) return f->second;
        const auto &p = m.pts[size_t(i)], &q = m.pts[size_t(j)];
        const double t = (at - p[axis]) / (q[axis] - p[axis]);
        std::array<double, 3> np;
        for (int k = 0; k < 3; k++) np[k] = k != axis ? p[k] + t * (q[k] - p[k]) : at;
        o.pts.push_back(np);
        std::array<int, 2> nu; for (int k = 0; k < 2; k++) nu[k] = int(py_round(m.uv[size_t(i)][k] + t * (m.uv[size_t(j)][k] - m.uv[size_t(i)][k])));
        std::array<uint8_t, 3> nc; for (int k = 0; k < 3; k++) nc[k] = uint8_t(int(py_round(m.rgb[size_t(i)][k] + t * (m.rgb[size_t(j)][k] - m.rgb[size_t(i)][k]))));
        o.uv.push_back(nu); o.rgb.push_back(nc);
        const int idx = int(o.pts.size()) - 1;
        cache[key] = idx;
        return idx;
    };
    for (const auto& t : m.tris) {
        bool ins[3];
        for (int k = 0; k < 3; k++) ins[k] = side(m.pts[size_t(t[k])]);
        if (ins[0] && ins[1] && ins[2]) { o.tris.push_back(t); continue; }
        if (!ins[0] && !ins[1] && !ins[2]) continue;
        std::vector<int> poly;
        for (int k = 0; k < 3; k++) {
            const int i = t[k], j = t[(k + 1) % 3];
            if (ins[k]) poly.push_back(i);
            if (ins[k] != ins[(k + 1) % 3]) poly.push_back(cut(i, j));
        }
        for (size_t k = 1; k + 1 < poly.size(); k++) o.tris.push_back({ poly[0], poly[k], poly[k + 1] });
    }
    if (o.tris.empty()) return false;
    m = sub(o, o.tris);
    return true;
}

// seen: the triangles facing one of the match's cameras at more than cmin
bool seen(Mesh& m, int z1, double cmin) {
    std::vector<std::array<double, 3>> cams;
    for (int x : { -900, 0, 900 }) for (int y : { 200, 700 }) for (int z : { -2400, -600 }) cams.push_back({ double(x), double(y), double(z) });
    cams.push_back({ 0, 300, double(z1) });
    std::vector<std::array<int, 3>> keep;
    for (const auto& t : m.tris) {
        const auto n = normal(m, t);
        const double ln = std::sqrt(0.0 + n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (ln == 0) continue;
        double c0[3];
        for (int k = 0; k < 3; k++) c0[k] = (0.0 + m.pts[size_t(t[0])][k] + m.pts[size_t(t[1])][k] + m.pts[size_t(t[2])][k]) / 3.0;
        for (const auto& c : cams) {
            const double d[3] = { c[0] - c0[0], c[1] - c0[1], c[2] - c0[2] };
            const double ld = std::sqrt(0.0 + d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (0.0 + n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > cmin * ln * ld) { keep.push_back(t); break; }
        }
    }
    if (keep.empty()) return false;
    m = sub(m, keep);
    return true;
}

// ---- pictures: the game's (a palette file, its texture the next file) as RGBA5551 rows
bool rgba_of(const Rom& rom, int pal, std::vector<std::vector<uint16_t>>& rows, int& w, int& h) {
    const Bytes &t = rom.file(uint32_t(pal + 1)), &q = rom.file(uint32_t(pal));
    if (t.size() < 8 || q.size() < 32) return false;
    w = t[0] + 1; h = t[1] + 1;
    if (t.size() < size_t(8 + w * h / 2)) return false;
    rows.assign(size_t(h), std::vector<uint16_t>(size_t(w)));
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const int k = y * w + x; const uint8_t b = t[8 + k / 2]; const int ix = (k & 1) ? b & 15 : b >> 4;
        rows[size_t(y)][size_t(x)] = uint16_t(q[2 * ix] << 8 | q[2 * ix + 1]);
    }
    return true;
}
std::array<int, 4> c5551(uint16_t c) { return { (c >> 11) & 31, (c >> 6) & 31, (c >> 1) & 31, c & 1 }; }
// resize: by whole factors down (opaque texels averaged; clear if most were), else nearest
std::vector<std::vector<uint16_t>> resize(const std::vector<std::vector<uint16_t>>& rows, int w, int h) {
    const int H = int(rows.size()), W = int(rows[0].size());
    if (W == w && H == h) return rows;
    const double fx = W / double(w), fy = H / double(h);
    std::vector<std::vector<uint16_t>> out;
    for (int y = 0; y < h; y++) {
        std::vector<uint16_t> r;
        for (int x = 0; x < w; x++) {
            if (fx >= 1 && fy >= 1 && fx == std::trunc(fx) && fy == std::trunc(fy)) {
                std::vector<std::array<int, 4>> op; int n = 0;
                for (int j = 0; j < int(fy); j++) for (int i = 0; i < int(fx); i++) {
                    const auto c = c5551(rows[size_t(int(y * fy) + j)][size_t(int(x * fx) + i)]); n++;
                    if (c[3]) op.push_back(c);
                }
                if (op.size() * 2 < size_t(n)) { r.push_back(0); continue; }
                int mm[3];
                for (int k = 0; k < 3; k++) { long long s = 0; for (const auto& c : op) s += c[k]; mm[k] = int(py_round(double(s) / double(op.size()))); }
                r.push_back(uint16_t(mm[0] << 11 | mm[1] << 6 | mm[2] << 1 | 1));
            } else r.push_back(rows[size_t(std::min(H - 1, int((y + 0.5) * fy)))][size_t(std::min(W - 1, int((x + 0.5) * fx)))]);
        }
        out.push_back(r);
    }
    return out;
}
}  // namespace
}  // namespace firstrun
extern "C" int firstrun_median_cut(const unsigned char* rgb, uint32_t n, uint32_t colors, unsigned char* palette, uint32_t* count);
namespace firstrun {
namespace {
struct Pic { Bytes tex, pal; };
// make_picture: a CI4 picture of RGBA5551 rows, 16 colours by Pillow's median cut over the opaque texels (colour 0 the clear one)
bool make_picture(const std::vector<std::vector<uint16_t>>& rows, Pic& pic, std::string& why) {
    const int h = int(rows.size()), w = int(rows[0].size());
    std::vector<uint16_t> flat;
    for (const auto& r : rows) flat.insert(flat.end(), r.begin(), r.end());
    bool clear = false;
    std::vector<unsigned char> solid;
    for (uint16_t c : flat) {
        if (!(c & 1)) { clear = true; continue; }
        const auto q = c5551(c);
        for (int k = 0; k < 3; k++) solid.push_back(uint8_t(q[k] * 255 / 31));
    }
    const uint32_t k = clear ? 15 : 16;
    unsigned char qp[48]; uint32_t n = 0;
    if (!firstrun_median_cut(solid.data(), uint32_t(solid.size() / 3), k, qp, &n)) { why = "a stage set's picture couldn't be quantized"; return false; }
    std::vector<uint16_t> pal;
    if (clear) pal.push_back(0);
    for (uint32_t i = 0; i < n; i++) pal.push_back(uint16_t((qp[3 * i] >> 3) << 11 | (qp[3 * i + 1] >> 3) << 6 | (qp[3 * i + 2] >> 3) << 1 | 1));
    while (pal.size() < 16) pal.push_back(0);
    const size_t lo = clear ? 1 : 0;
    auto near = [&](uint16_t c) {
        if (!(c & 1)) return 0;
        const auto q = c5551(c);
        long long best = -1; int at = 0;
        for (size_t i = lo; i < pal.size(); i++) {
            long long d = 1ll << 30;
            if (pal[i] & 1) { const auto p = c5551(pal[i]); d = 0ll + (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]); }
            if (best < 0 || d < best) { best = d; at = int(i); }
        }
        return at;
    };
    int lw = 0, lh = 0; while ((1 << lw) < w) lw++; while ((1 << lh) < h) lh++;
    pic.tex = { uint8_t(w - 1), uint8_t(h - 1), 0, 16, 2, 2, uint8_t(lw), uint8_t(lh) };
    for (size_t i = 0; i + 1 < flat.size(); i += 2) pic.tex.push_back(uint8_t(near(flat[i]) << 4 | near(flat[i + 1])));
    pic.pal.clear();
    for (uint16_t c : pal) { pic.pal.push_back(uint8_t(c >> 8)); pic.pal.push_back(uint8_t(c)); }
    return true;
}

std::array<int, 2> size_of(const Rom& rom, int pal) { const Bytes& t = rom.file(uint32_t(pal + 1)); return { t[0] + 1, t[1] + 1 }; }

// into_cell: a piece's texels moved into its picture's cell of a packed one
bool into_cell(const Rom& rom, Mesh& m, int pal, const Cell& cell, std::string& why) {
    const auto wh = size_of(rom, pal); const int w = wh[0], h = wh[1];
    int minu = m.uv[0][0], maxu = minu, minv = m.uv[0][1], maxv = minv;
    for (const auto& t : m.uv) { minu = std::min(minu, t[0]); maxu = std::max(maxu, t[0]); minv = std::min(minv, t[1]); maxv = std::max(maxv, t[1]); }
    const long long ku = floordiv(minu, w), kv = floordiv(minv, h);
    if (maxu > (ku + 1) * w || maxv > (kv + 1) * h) { why = "a piece whose picture repeats can't be packed"; return false; }
    for (auto& t : m.uv) {
        t = { cell.x + std::min(cell.w - 1, int(py_round((t[0] - ku * w) * cell.w / double(w)))),
              cell.y + std::min(cell.h - 1, int(py_round((t[1] - kv * h) * cell.h / double(h)))) };
    }
    return true;
}
// norm_uv: texels moved by whole even repeats to start near 0
void norm_uv(const Rom& rom, Mesh& m, int pal) {
    const auto wh = size_of(rom, pal);
    int minu = m.uv[0][0], minv = m.uv[0][1];
    for (const auto& t : m.uv) { minu = std::min(minu, t[0]); minv = std::min(minv, t[1]); }
    const long long ku = floordiv(minu, 2 * wh[0]) * 2 * wh[0], kv = floordiv(minv, 2 * wh[1]) * 2 * wh[1];
    if (ku || kv) for (auto& t : m.uv) t = { int(t[0] - ku), int(t[1] - kv) };
}

struct Piece { int f, p; Mesh m; };
// new_pieces: SURVIVOR SERIES' screen in its truss (the close scene's space, its place not added)
std::vector<Piece> new_pieces(const Spec& sp, const CloseScene& cs) {
    std::vector<Piece> out;
    for (const NewPieces& n : NEW_PIECES) {
        if (n.v != sp.v) continue;
        auto to_close = [&](double x, double y, double z) {
            const double q[3] = { x, y, z };
            std::array<double, 3> r;
            for (int k = 0; k < 3; k++) r[k] = double((long long)py_round((q[k] - sp.d[k]) / sp.s - cs.place[k]));
            return r;
        };
        const int x0 = n.screen[0], x1 = n.screen[1], y0 = n.screen[2], y1 = n.screen[3], z = n.z, b = n.beam;
        const std::array<uint8_t, 3> g{ uint8_t(n.grey), uint8_t(n.grey), uint8_t(n.grey) };
        auto quad = [&](int xa, int xb, int ya, int yb, std::vector<std::array<int, 2>> uv) {
            Mesh m;
            m.pts = { to_close(xa, ya, z), to_close(xb, ya, z), to_close(xb, yb, z), to_close(xa, yb, z) };
            m.uv = uv; m.rgb = { g, g, g, g };
            m.tris = { { 0, 1, 2 }, { 0, 2, 3 }, { 0, 2, 1 }, { 0, 3, 2 } };
            return m;
        };
        Mesh scr = quad(x0, x1, y0, y1, { { 63, 55 }, { 0, 55 }, { 0, 8 }, { 63, 8 } });
        std::vector<Mesh> beams;
        const int spans[4][5] = { { x0 - b, x1 + b, y1, y1 + b, 'x' }, { x0 - b, x1 + b, y0 - b, y0, 'x' }, { x0 - b, x0, y0, y1, 'y' }, { x1, x1 + b, y0, y1, 'y' } };
        for (const auto& s : spans) {
            const int L = s[4] == 'x' ? s[1] - s[0] : s[3] - s[2];
            const int u = int(py_round(L / (4.0 * b) * 128));
            beams.push_back(quad(s[0], s[1], s[2], s[3], s[4] == 'x' ? std::vector<std::array<int, 2>>{ { u, 31 }, { 0, 31 }, { 0, 0 }, { u, 0 } }
                                                                      : std::vector<std::array<int, 2>>{ { 0, 31 }, { 0, 0 }, { u, 0 }, { u, 31 } }));
        }
        out.push_back({ NEW_SCREEN, n.screen_pic, scr });
        out.push_back({ NEW_SCREEN + 1, n.truss_pic, join(beams) });
    }
    return out;
}

// close_pieces: the close scene's pieces in its drawing order, with its place (and the extra list's offset) added
bool close_pieces(const Rom& rom, const Spec& sp, const CloseScene& cs, std::vector<Piece>& out, std::string& why) {
    auto add = [&](int f, int p, const int* off) {
        Mesh m;
        if (!mesh_of(rom.file(uint32_t(f)), m)) { why = "a close scene's mesh isn't there"; return false; }
        for (auto& q : m.pts) for (int k = 0; k < 3; k++) q[k] = q[k] + cs.place[k] + off[k];
        out.push_back({ f, p, m });
        return true;
    };
    const int zero[3] = { 0, 0, 0 };
    for (const Pc& c : cs.main) if (!add(c.f, c.p, zero)) return false;
    for (const Pc& c : cs.x1) if (!add(c.f, c.p, cs.x1_off)) return false;
    if (cs.x2[0] >= 0 && !add(cs.x2[0], cs.x2[1], zero)) return false;
    if (cs.x3[0] >= 0 && !add(cs.x3[0], cs.x3[1], zero)) return false;
    for (const Pc& c : cs.m61) if (!add(c.f, c.p, zero)) return false;
    for (Piece& n : new_pieces(sp, cs)) {
        for (auto& q : n.m.pts) for (int k = 0; k < 3; k++) q[k] = q[k] + cs.place[k];
        out.push_back(n);
    }
    return true;
}

// pieces: in the match's place (p -> s p + d), cut, culled
bool venue_pieces(const Rom& rom, const Spec& sp, const CloseScene& cs, std::vector<Piece>& out, std::string& why) {
    std::vector<Piece> cl;
    if (!close_pieces(rom, sp, cs, cl, why)) return false;
    for (Piece& pc : cl) {
        if (has(sp.drop, pc.f)) continue;
        for (auto& q : pc.m.pts) for (int k = 0; k < 3; k++) q[k] = sp.s * q[k] + sp.d[k];
        bool gone = false;
        for (const Clip& c : sp.clip)
            if (c.all || has(c.ids, pc.f)) { if (!clip(pc.m, c.axis, c.ge, c.at)) { gone = true; break; } }
        if (gone) continue;
        double cmin = sp.cos_all >= 0 ? sp.cos_all : COS_MIN;     // (cos.get(p, cos.get(f, cos.get("all", COS_MIN))))
        bool byf = false, byp = false; double vf = 0, vp = 0;
        for (const auto& kv : sp.cos) { if (kv.first == pc.p) { byp = true; vp = kv.second; } if (kv.first == pc.f) { byf = true; vf = kv.second; } }
        if (byp) cmin = vp; else if (byf) cmin = vf;
        if (!seen(pc.m, sp.z1, cmin)) continue;
        norm_uv(rom, pc.m, pc.p);
        out.push_back(pc);
    }
    return true;
}

struct Row { std::string name; Mesh mesh; int game_mesh = -1; int pal = -1; int pic = -1; std::vector<int> names; };

// parts: the venue's parts in drawing order (arenas_stages.py parts)
bool venue_parts(const Rom& rom, const Spec& sp, const CloseScene& cs, std::vector<Row>& out, std::vector<Pic>& pics, std::string& why) {
    // packed(v): palette -> (its packed picture, its cell)
    std::map<int, std::pair<int, Cell>> pk;
    for (const Pack& pa : sp.pack) {
        std::vector<std::vector<uint16_t>> rows(size_t(pa.H), std::vector<uint16_t>(size_t(pa.W), 0));
        for (const Cell& c : pa.cells) {
            std::vector<std::vector<uint16_t>> src; int w, h;
            if (!rgba_of(rom, c.pal, src, w, h)) { why = "a packed picture's file isn't there"; return false; }
            const auto r = resize(src, c.w, c.h);
            for (int j = 0; j < c.h; j++) for (int i = 0; i < c.w; i++) rows[size_t(c.y + j)][size_t(c.x + i)] = r[size_t(j)][size_t(i)];
        }
        Pic pic;
        if (!make_picture(rows, pic, why)) return false;
        pics.push_back(pic);
        for (const Cell& c : pa.cells) pk[c.pal] = { int(pics.size()) - 1, c };
    }
    std::vector<Piece> ps;
    if (!venue_pieces(rom, sp, cs, ps, why)) return false;
    std::map<int, int> idx;                                     // (f -> its last index)
    for (size_t i = 0; i < ps.size(); i++) idx[ps[i].f] = int(i);
    struct Group { std::string key; int pal; int pic; Mesh m; double i; std::vector<int> names; };
    std::vector<Group> groups;
    for (size_t ii = 0; ii < ps.size(); ii++) {
        Piece& pc = ps[ii];
        double i = double(ii);
        for (const auto& o : sp.order) if (o.first == pc.f) i = idx[o.second] - 0.5;
        std::string key; int pal = pc.p, pic = -1;
        const auto pkp = pk.find(pc.p);
        if (pkp != pk.end()) {
            pic = pkp->second.first; pal = -1; key = "pack " + std::to_string(pic);
            if (!into_cell(rom, pc.m, pc.p, pkp->second.second, why)) return false;
        } else {
            const Bytes &t = rom.file(uint32_t(pc.p + 1)), &q = rom.file(uint32_t(pc.p));      // (the same picture under two ids: one)
            key = "pic " + std::to_string(t.size()) + ":" + std::string(t.begin(), t.end()) + "|" + std::string(q.begin(), q.end());
        }
        if (has(sp.alone, pc.f)) key = "alone " + std::to_string(pc.f);
        if (!sp.bands.empty() && (sp.band_pals.empty() || has(sp.band_pals, pc.p))) {
            double zc = 0.0;
            for (const auto& q : pc.m.pts) zc += q[2];
            zc = zc / double(pc.m.pts.size());
            int nb = 0; for (int b : sp.bands) if (zc > b) nb++;
            key += " band " + std::to_string(nb);
        }
        std::vector<Mesh> chunks{ pc.m };
        while (true) {
            auto big = std::find_if(chunks.begin(), chunks.end(), [](const Mesh& c) { return int(c.pts.size()) > LIMIT || int(c.tris.size()) > LIMIT; });
            if (big == chunks.end()) break;
            Mesh c = *big; chunks.erase(big);
            const size_t n = c.tris.size() / 2;
            chunks.push_back(sub(c, std::vector<std::array<int, 3>>(c.tris.begin(), c.tris.begin() + std::ptrdiff_t(n))));
            chunks.push_back(sub(c, std::vector<std::array<int, 3>>(c.tris.begin() + std::ptrdiff_t(n), c.tris.end())));
        }
        for (const Mesh& c : chunks) {
            Group* g = nullptr;
            for (Group& x : groups)
                if (x.key == key && int(x.m.pts.size() + c.pts.size()) <= LIMIT && int(x.m.tris.size() + c.tris.size()) <= LIMIT) { g = &x; break; }
            if (g) { g->m = join({ g->m, c }); g->names.push_back(pc.f); }
            else groups.push_back({ key, pal, pic, c, i, { pc.f } });
        }
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.i < b.i; });
    std::vector<Mesh*> ms;
    for (Group& g : groups) ms.push_back(&g.m);
    snap_parts(ms);
    auto hx = [](int f) { char b[16]; snprintf(b, sizeof b, "%X", f); return std::string(b); };
    for (Group& g : groups)
        out.push_back({ hx(g.names[0]) + (g.names.size() > 1 ? "+" + std::to_string(g.names.size() - 1) : ""), g.m, -1, g.pal, g.pic, g.names });
    for (const Keep& k : sp.keep) {
        Row r;
        if (k.single < 0) {
            std::vector<Mesh> parts;
            for (const Pc& c : k.meshes) {
                Mesh m;
                if (!mesh_of(rom.file(uint32_t(c.f)), m)) { why = "a kept piece's mesh isn't there"; return false; }
                const auto pkp = pk.find(c.p);
                if (pkp != pk.end() && !into_cell(rom, m, c.p, pkp->second.second, why)) return false;
                parts.push_back(m);
            }
            r.mesh = join(parts);
            std::vector<Mesh*> one{ &r.mesh };
            snap_parts(one);
            const auto pkp = pk.find(k.meshes[0].p);
            if (pkp != pk.end()) r.pic = pkp->second.first; else r.pal = k.meshes[0].p;
            r.name = "old ";
            for (size_t j = 0; j < k.meshes.size(); j++) r.name += (j ? "+" : "") + hx(k.meshes[j].f);
        } else { r.game_mesh = k.single; r.pal = k.pal; r.name = "old " + hx(k.single); }
        size_t at = 0;
        auto find_row = [&](int f) -> size_t { for (size_t j = 0; j < out.size(); j++) if (has(out[j].names, f)) return j; return out.size(); };
        if (k.where_kind == 0) at = 0;
        else if (k.where_kind == 1) at = out.size();
        else if (k.where_kind == 2) at = find_row(k.where_f) + 1;
        else at = find_row(k.where_f);
        if (at > out.size()) { why = "a kept piece's place isn't in the set"; return false; }
        out.insert(out.begin() + std::ptrdiff_t(at), r);
    }
    return true;
}

// Venue v's files: each part's mesh file at the number xstage<v>_mesh gives it, each made picture's palette and texture at
// the number xstage<v>_pal gives it (and the next); the new pieces' close files at xstage<v>_close_mesh's.
bool make_venue_files(const Rom& rom, const Spec& sp, const CloseScene& cs, const std::vector<Table>& all, const std::vector<uint8_t>& image,
                      std::map<uint32_t, Bytes>& files, std::string& why) {
    std::vector<Row> rows; std::vector<Pic> pics;
    if (!venue_parts(rom, sp, cs, rows, pics, why)) return false;
    const std::string vs = std::to_string(sp.v);
    const Table* ml = find(all, ("xstage" + vs + "_mesh").c_str());
    const Table* pl = find(all, ("xstage" + vs + "_pal").c_str());
    if (!ml || !pl || ml->size != 2 * rows.size() || pl->size != 2 * rows.size()) { why = "xstage" + vs + "'s lists aren't the set's parts"; return false; }
    for (size_t k = 0; k < rows.size(); k++) {
        const uint16_t mid = be16(image, ml->address - 0x81000000u + 2 * k), pid = be16(image, pl->address - 0x81000000u + 2 * k);
        if (rows[k].game_mesh >= 0) { if (mid != rows[k].game_mesh) { why = "xstage" + vs + "_mesh names another game piece"; return false; } }
        else {
            Bytes b;
            if (!mesh_file2(rows[k].mesh, b, why)) return false;
            if (mid < 0x5000) { why = "xstage" + vs + "_mesh names a game file for a made part"; return false; }
            files[mid] = b;
        }
        if (rows[k].pic >= 0) { files[pid] = pics[size_t(rows[k].pic)].pal; files[pid + 1u] = pics[size_t(rows[k].pic)].tex; }
        else if (pid != rows[k].pal) { why = "xstage" + vs + "_pal names another game picture"; return false; }
    }
    const std::vector<Piece> nw = new_pieces(sp, cs);
    if (!nw.empty()) {
        const Table* cl = find(all, ("xstage" + vs + "_close_mesh").c_str());
        if (!cl || cl->size != 2 * nw.size()) { why = "xstage" + vs + "_close_mesh isn't the new pieces' list"; return false; }
        for (size_t k = 0; k < nw.size(); k++) {
            Bytes b;
            if (!mesh_file2(nw[k].m, b, why)) return false;
            files[be16(image, cl->address - 0x81000000u + 2 * k)] = b;
        }
    }
    return true;
}

}  // namespace

// Venue 0's files: each part's mesh file at the number xraw_set_mesh gives it (the plan's copy of that list is in the image).
bool make_raw_files(const Rom& rom, const std::vector<Table>& all, const std::vector<uint8_t>& image,
                    std::map<uint32_t, std::vector<uint8_t>>& files, std::string& why) {
    std::vector<Part> parts;
    if (!raw_parts(rom, parts, why)) return false;
    const Table* list = find(all, "xraw_set_mesh");
    if (!list || list->size != 2 * parts.size()) { why = "xraw_set_mesh isn't the list RAW IS WAR's parts go in"; return false; }
    for (size_t k = 0; k < parts.size(); k++) {
        const uint16_t id = be16(image, list->address - 0x81000000u + 2 * k);
        if (parts[k].game_mesh >= 0) {
            if (id != parts[k].game_mesh) { why = "xraw_set_mesh doesn't name the game's own pieces where it should"; return false; }
            continue;
        }
        Bytes b;
        if (!mesh_file(parts[k].mesh, b, why)) return false;
        if (id < 0x5000) { why = "xraw_set_mesh names a game file for a made part"; return false; }
        files[id] = std::move(b);
    }
    return true;
}

// For the ROM importer's pack builder (src/romimport ri_wmarenas.cpp, gen/wmport.py's own order): the stage sets' rows as
// arenas_raw.py build / arenas_stages.py build go through them -- their names, mesh files (made) or the game's mesh, the
// game's palette or the made picture; venue v's new close pieces. Nothing here changes what make_stage_files makes.
bool stage_rows(const Rom& rom, int v, std::vector<StageRow>& rows, std::vector<StageRow>& close_new, std::string& why) {
    rows.clear(); close_new.clear();
    if (v == 0) {
        std::vector<Part> parts;
        if (!raw_parts(rom, parts, why)) return false;
        for (const Part& p : parts) {
            StageRow r; r.name = p.name; r.game_mesh = p.game_mesh; r.pal = p.pal;
            if (p.game_mesh < 0 && !mesh_file(p.mesh, r.mesh, why)) return false;
            rows.push_back(std::move(r));
        }
        return true;
    }
    const Spec* sp = nullptr; const CloseScene* cs = nullptr;
    for (const Spec& s : SPECS) if (s.v == v) sp = &s;
    std::vector<CloseScene> scenes;                    // (the close scenes from the ROM, as make_stage_files reads them)
    if (sp && !close_scenes(rom, { v }, scenes, why)) return false;
    for (const CloseScene& c : scenes) if (c.v == v) cs = &c;
    if (!sp || !cs) { why = "no stage spec for venue " + std::to_string(v); return false; }
    std::vector<Row> rs; std::vector<Pic> pics;
    if (!venue_parts(rom, *sp, *cs, rs, pics, why)) return false;
    for (const Row& x : rs) {
        StageRow r; r.name = x.name; r.game_mesh = x.game_mesh; r.pal = x.pal;
        if (x.game_mesh < 0 && !mesh_file2(x.mesh, r.mesh, why)) return false;
        if (x.pic >= 0) { r.pal = -1; r.pic_tex = pics[size_t(x.pic)].tex; r.pic_pal = pics[size_t(x.pic)].pal; }
        rows.push_back(std::move(r));
    }
    for (const Piece& p : new_pieces(*sp, *cs)) {
        StageRow r; r.name = std::to_string(p.f); r.pal = p.p; r.new_id = p.f;
        if (!mesh_file2(p.m, r.mesh, why)) return false;
        close_new.push_back(std::move(r));
    }
    return true;
}

bool make_stage_files(const Rom& rom, const std::vector<Table>& all, const std::vector<uint8_t>& image,
                      std::map<uint32_t, std::vector<uint8_t>>& files, std::string& why) {
    if (!make_raw_files(rom, all, image, files, why)) return false;
    std::vector<int> venues;                           // [stagelists 2026-11-19] the close scenes from the ROM
    for (const Spec& sp : SPECS) venues.push_back(sp.v);
    std::vector<CloseScene> scenes;
    if (!close_scenes(rom, venues, scenes, why)) return false;
    for (const Spec& sp : SPECS) {
        const CloseScene* cs = nullptr;
        for (const CloseScene& c : scenes) if (c.v == sp.v) cs = &c;
        if (!cs) { why = "no close scene for venue " + std::to_string(sp.v); return false; }
        if (!make_venue_files(rom, sp, *cs, all, image, files, why)) { why = "venue " + std::to_string(sp.v) + ": " + why; return false; }
    }
    return true;
}

}  // namespace firstrun
